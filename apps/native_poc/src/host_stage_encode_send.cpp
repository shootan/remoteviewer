// Stage 11: raw send / H.264 encode and sender enqueue.
//
// Host split refactor Phase 3.5: moved verbatim out of host_main_loop.cpp so each stage reads on its
// own; see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.


#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture_backend_dxgi.hpp"
#include "d3d_capture_readback.hpp"
#include "encode_resolution_ladder.hpp"
#include "gdi_capture_process.hpp"
#include "host_abr.hpp"
#include "host_args.hpp"
#include "host_backend_policy.hpp"
#include "host_bgra_scale.hpp"
#include "host_bottleneck.hpp"
#include "host_capture_device.hpp"
#include "host_capture_session.hpp"
#include "host_client_metrics.hpp"
#include "host_control_session.hpp"
#include "host_encoded_sender.hpp"
#include "host_encoder_manager.hpp"
#include "host_frame_gate.hpp"
#include "host_frame_state.hpp"
#include "host_gpu_scaler.hpp"
#include "host_input_inject.hpp"
#include "host_input_router.hpp"
#include "host_kick.hpp"
#include "host_log.hpp"
#include "host_main_loop.hpp"
#include "host_net_io.hpp"
#include "host_session.hpp"
#include "host_stats.hpp"
#include "host_string_util.hpp"
#include "host_watchdog.hpp"
#include "host_window_enum.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using remote60::host::DesktopCaptureBackend;
using remote60::host::DxgiDesktopCaptureConfig;
using remote60::host::DxgiDesktopCaptureSession;

namespace remote60::native_poc {

Flow stage_encode_send(HostContext& hx, TickContext& tc) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& useRaw = hx.useRaw;
  auto& transport = hx.transport;
  auto& guardStaleEncoded = hx.guardStaleEncoded;
  auto& guardStalePreEncode = hx.guardStalePreEncode;
  auto& poppedNv12Slot = hx.poppedNv12Slot;
  auto& item = hx.item;
  auto& frameGating = hx.frameGating;
  auto& rate = hx.rate;
  auto& kick = hx.kick;
  auto& clientMetrics = hx.clientMetrics;
  auto& backend = hx.backend;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
  auto& lastUserFeedbackUs = hx.lastUserFeedbackUs;
  auto& tickWaitUs = tc.tickWaitUs;
  auto& payload = tc.payload;
  auto& seq = tc.seq;
  auto& w = tc.w;
  auto& h = tc.h;
  auto& stride = tc.stride;
  auto& streamGeneration = tc.streamGeneration;
  auto& captureUs = tc.captureUs;
  auto& callbackUs = tc.callbackUs;
  auto& callbackIntervalUs = tc.callbackIntervalUs;
  auto& captureIntervalUs = tc.captureIntervalUs;
  auto& captureClockSkewUs = tc.captureClockSkewUs;
  auto& captureAgeAtCallbackUs = tc.captureAgeAtCallbackUs;
  auto& captureD3DWaitUs = tc.captureD3DWaitUs;
  auto& captureCopyMapUs = tc.captureCopyMapUs;
  auto& captureMemcpyUs = tc.captureMemcpyUs;
  auto& captureUnmapWaitUs = tc.captureUnmapWaitUs;
  auto& captureUnmapUs = tc.captureUnmapUs;
  auto& version = tc.version;
  auto& nv12Slot = tc.nv12Slot;
  auto& nv12Generation = tc.nv12Generation;
  auto& nv12W = tc.nv12W;
  auto& nv12H = tc.nv12H;
  auto& queueWaitReason = tc.queueWaitReason;
  auto& servedBootstrap = tc.servedBootstrap;
  auto& kickForcedKey = tc.kickForcedKey;
  auto& queueWaitUs = tc.queueWaitUs;
  auto& queueGapFrames = tc.queueGapFrames;
  auto& queueDepthAtPop = tc.queueDepthAtPop;
  auto& captureStampUs = tc.captureStampUs;
  auto& sendFailed = tc.sendFailed;
  auto& queuePopUs = tc.queuePopUs;
  auto& queueSelectWaitUs = tc.queueSelectWaitUs;
  auto& frameAgeAtSelectUs = tc.frameAgeAtSelectUs;
  auto& captureToCallbackUs = tc.captureToCallbackUs;
  auto& captureToQueueUs = tc.captureToQueueUs;
  if (useRaw) {
    RawFrameHeader hdr{};
    hdr.header.magic = remote60::native_poc::kMagic;
    hdr.header.type = static_cast<uint16_t>(MessageType::RawFrameBgra);
    hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
    hdr.seq = seq;
    hdr.width = w;
    hdr.height = h;
    hdr.stride = stride;
    hdr.payloadSize = static_cast<uint32_t>(payload->size());
    hdr.streamGeneration = streamGeneration;
    hdr.captureQpcUs = captureStampUs;
    hdr.encodeStartQpcUs = captureStampUs;
    hdr.encodeEndQpcUs = captureStampUs;
    SendPathStats sendPathStats{};
    const uint64_t sendStartUs = qpc_now_us();
    const uint64_t sendIntervalUs =
        (stats.lastSendStartUs > 0 && sendStartUs >= stats.lastSendStartUs) ? (sendStartUs - stats.lastSendStartUs) : 0;
    const uint64_t sendIntervalErrUs =
        (encoder.activeFrameIntervalUs > 0 && sendIntervalUs > 0)
            ? ((sendIntervalUs >= encoder.activeFrameIntervalUs) ? (sendIntervalUs - encoder.activeFrameIntervalUs)
                                                         : (encoder.activeFrameIntervalUs - sendIntervalUs))
            : 0;
    const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
    const uint64_t sendWaitUs = queueToSendUs;
    const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
    hdr.sendQpcUs = sendStartUs;
    const bool sentOk =
        (transport == VideoTransport::Tcp) &&
        send_all_timed(clientSession.clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                       &sendPathStats.headerCallCount) &&
        send_all_timed(clientSession.clientSock, payload->data(), payload->size(), &sendPathStats.payloadUs,
                       &sendPathStats.payloadCallCount);
    const uint64_t sendDoneUs = qpc_now_us();
    const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
    const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
    if (sentOk) {
      stats.lastSendStartUs = sendStartUs;
      capture.LogFirstSentGeneration(res, stats, "raw", streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);
      if (frameGating.enabled && useH264 && payload && !payload->empty()) {
        frameGating.lastSentUs = sendStartUs;
        frameGating.refPayload = payload;
        frameGating.refW = w;
        frameGating.refH = h;
        frameGating.refStride = stride;
      }
    }

    if (!sentOk) {
      if (reconnect_tcp_data_session(hx, "raw_send_fail")) {
        return Flow::Continue;
      }
      std::cout << "[native-video-host] client disconnected\n";
      return Flow::Break;
    }
    ++sender.sentFrames;
    sender.sentBytes += payload->size();
      if (args.traceEvery > 0 && (seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || stats.tracePrinted < args.traceMax)) {
      ++stats.tracePrinted;
      const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
      const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
      const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
      const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
          queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
        std::cout << "[native-video-host][trace] seq=" << seq
                  << " captureUs=" << hdr.captureQpcUs
                  << " encodeStartUs=" << hdr.encodeStartQpcUs
                  << " encodeEndUs=" << hdr.encodeEndQpcUs
                  << " sendUs=" << hdr.sendQpcUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " c2eUs=" << c2eUs
                  << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " captureD3DWaitUs=" << captureD3DWaitUs
                  << " captureCopyMapUs=" << captureCopyMapUs
                  << " captureMemcpyUs=" << captureMemcpyUs
                  << " captureUnmapWaitUs=" << captureUnmapWaitUs
                  << " captureUnmapUs=" << captureUnmapUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                  << " queueSelectWaitUs=" << queueSelectWaitUs
                 << " queueGapFrames=" << queueGapFrames
                 << " queueDepth=" << queueDepthAtPop
                 << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                 << " captureToQueueUs=" << captureToQueueUs
                 << " queueWaitUs=" << queueWaitUs
                 << " queueWaitReason=" << queueWaitReason
                 << " queueToSendUs=" << queueToSendUs
                 << " sendWaitUs=" << sendWaitUs
                 << " sendIntervalUs=" << sendIntervalUs
                 << " sendIntervalErrUs=" << sendIntervalErrUs
                 << " tickWaitUs=" << tickWaitUs
                 << " sendCallCount=" << sendCallCount
                 << " sendHeaderUs=" << sendPathStats.headerUs
                 << " sendPayloadUs=" << sendPathStats.payloadUs
                 << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                 << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                 << " sendChunkCount=" << sendPathStats.payloadChunkCount
                 << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                 << " sendStartUs=" << sendStartUs
                << " sendDoneUs=" << sendDoneUs
                << " sendDurUs=" << sendDurUs
                << " encUs=" << encUs
                << " e2sUs=" << e2sUs
                << " payloadBytes=" << hdr.payloadSize
                << "\n";
    }
    const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
    const uint64_t encUs = (hdr.encodeEndQpcUs >= hdr.encodeStartQpcUs) ? (hdr.encodeEndQpcUs - hdr.encodeStartQpcUs) : 0;
    const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
    const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
    const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
        queueWaitUs, 0, 0, 0, 0, encUs, queueToSendUs, sendDurUs, sendIntervalErrUs);
    if (pipeUs >= kHostUserFeedbackWarnUs &&
        (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
      std::cout << "[native-video-host][user-feedback] seq=" << seq
                << " codec=" << "raw"
                << " pipeUs=" << pipeUs
                << " bottleneckStageCode=" << bottleneck.code
                << " bottleneckStageUs=" << bottleneck.us
                << " bottleneckStageName=" << bottleneck.name
                << " captureToCallbackUs=" << captureToCallbackUs
                << " callbackIntervalUs=" << callbackIntervalUs
                << " captureIntervalUs=" << captureIntervalUs
                << " captureClockSkewUs=" << captureClockSkewUs
                << " captureD3DWaitUs=" << captureD3DWaitUs
                << " captureCopyMapUs=" << captureCopyMapUs
                << " captureMemcpyUs=" << captureMemcpyUs
                << " captureUnmapWaitUs=" << captureUnmapWaitUs
                << " captureUnmapUs=" << captureUnmapUs
                << " selectWaitUs=" << frameAgeAtSelectUs
                << " queueSelectWaitUs=" << queueSelectWaitUs
                << " captureToQueueUs=" << captureToQueueUs
                 << " queueWaitUs=" << queueWaitUs
                 << " queueWaitReason=" << queueWaitReason
                  << " queueGapFrames=" << queueGapFrames
                  << " queueDepth=" << queueDepthAtPop
                  << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                  << " queueToSendUs=" << queueToSendUs
                  << " sendIntervalUs=" << sendIntervalUs
                  << " sendIntervalErrUs=" << sendIntervalErrUs
                   << " captureClockSkewUs=" << captureClockSkewUs
                   << " sendWaitUs=" << sendWaitUs
                 << " tickWaitUs=" << tickWaitUs
                 << " sendCallCount=" << sendCallCount
                 << " sendHeaderUs=" << sendPathStats.headerUs
                 << " sendPayloadUs=" << sendPathStats.payloadUs
                 << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                 << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                 << " sendChunkCount=" << sendPathStats.payloadChunkCount
                 << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                 << " c2eUs=" << c2eUs
                << " cb2eUs=" << callbackToSendStartUs
                << " encUs=" << encUs
                << " e2sUs=" << e2sUs
                << " sendStartUs=" << sendStartUs
                << " sendDoneUs=" << sendDoneUs
                << " sendDurUs=" << sendDurUs
                << "\n";
      lastUserFeedbackUs = hdr.sendQpcUs;
    }
    } else {
      const uint8_t* encodeSrc = payload->data();
    uint32_t encodeSrcW = w;
    uint32_t encodeSrcH = h;
    uint32_t encodeSrcStride = stride;
    std::vector<uint8_t> scaledBgra;
    D3DReadbackTiming scaleReadbackTiming{};
    uint64_t preEncodePrepUs = 0;
    uint64_t scaleUs = 0;
    uint64_t nv12Us = 0;
    const uint64_t preEncodeStartUs = qpc_now_us();
    // A window selection or a resize changes the source geometry; re-fit the encode size
    // to the new aspect so the scaler never has to stretch. The source size changes on
    // EVERY frame of an interactive window drag, and apply_encoder_target tears the MFT
    // down, so two guards keep this from thrashing: the geometry must hold steady for a
    // settle period, and near-identical aspect (letterboxing under 2%) is left alone.
    if (!servedBootstrap && w > 0 && h > 0 && (w != encoder.encodeSourceW || h != encoder.encodeSourceH)) {
      const uint64_t nowRefitUs = qpc_now_us();
      if (w != encoder.pendingRefitW || h != encoder.pendingRefitH) {
        encoder.pendingRefitW = w;
        encoder.pendingRefitH = h;
        encoder.pendingRefitSinceUs = nowRefitUs;
      } else if (nowRefitUs - encoder.pendingRefitSinceUs >= kEncodeRefitSettleUs) {
        uint32_t refitW = encoder.activeEncodeW;
        uint32_t refitH = encoder.activeEncodeH;
        fit_size_preserving_aspect(w, h, encoder.nominalEncodeW, encoder.nominalEncodeH, &refitW, &refitH);
        const double activeAspect =
            static_cast<double>(encoder.activeEncodeW) / static_cast<double>(std::max(1u, encoder.activeEncodeH));
        const double refitAspect =
            static_cast<double>(refitW) / static_cast<double>(std::max(1u, refitH));
        const bool aspectClose =
            std::abs(refitAspect - activeAspect) <= activeAspect * 0.02;
        encoder.encodeSourceW = w;
        encoder.encodeSourceH = h;
        if ((refitW != encoder.activeEncodeW || refitH != encoder.activeEncodeH) && !aspectClose) {
          const uint32_t prevW = encoder.activeEncodeW;
          const uint32_t prevH = encoder.activeEncodeH;
          const uint32_t keepNominalW = encoder.nominalEncodeW;
          const uint32_t keepNominalH = encoder.nominalEncodeH;
          if (encoder.ApplyTarget(capture, res, frameGating, inputRouter, sender, keepNominalW, keepNominalH, encoder.activeFps, encoder.activeBitrate,
                                   encoder.activeKeyint)) {
            encoder.forceKeyNext = true;
            std::cout << "[native-video-host] encode-refit source=" << w << "x" << h
                      << " encode=" << prevW << "x" << prevH << " -> " << encoder.activeEncodeW << "x"
                      << encoder.activeEncodeH << "\n";
          } else {
            // apply_encoder_target already shut the encoder down; without a working encoder
            // every later frame fails silently, so treat this like the other callers do.
            std::cerr << "[native-video-host] encode-refit failed source=" << w << "x" << h
                      << "; stopping stream\n";
            return Flow::Break;
          }
        }
      }
    } else {
      encoder.pendingRefitW = 0;
      encoder.pendingRefitH = 0;
    }
    const bool wantSurfaceEncode = useH264 && nv12Slot >= 0 && encoder.surfaceEncodeHealthy &&
                                   nv12W == encoder.activeEncodeW && nv12H == encoder.activeEncodeH;
    if (!wantSurfaceEncode && (encoder.activeEncodeW != w || encoder.activeEncodeH != h)) {
      const uint64_t scaleStartUs = qpc_now_us();
      bool scaleOk = false;
      if (capture.gpuScalerHealthy) {
        ++stats.gpuScaleAttempts;
        scaleOk = res.gpuScaler.scale(payload->data(), w, h, stride, encoder.activeEncodeW, encoder.activeEncodeH,
                                  &scaledBgra, &scaleReadbackTiming);
        if (scaleOk) {
          ++stats.gpuScaleSuccess;
          ++stats.gpuScaleTimedCount;
          stats.gpuScaleD3DWaitSumUs += scaleReadbackTiming.d3dWaitUs;
          stats.gpuScaleD3DWaitMaxUs = std::max(stats.gpuScaleD3DWaitMaxUs, scaleReadbackTiming.d3dWaitUs);
          stats.gpuScaleCopyMapSumUs += scaleReadbackTiming.copyMapUs;
          stats.gpuScaleCopyMapMaxUs = std::max(stats.gpuScaleCopyMapMaxUs, scaleReadbackTiming.copyMapUs);
          stats.gpuScaleMemcpySumUs += scaleReadbackTiming.memcpyUs;
          stats.gpuScaleMemcpyMaxUs = std::max(stats.gpuScaleMemcpyMaxUs, scaleReadbackTiming.memcpyUs);
          stats.gpuScaleUnmapWaitSumUs += scaleReadbackTiming.unmapWaitUs;
          stats.gpuScaleUnmapWaitMaxUs = std::max(stats.gpuScaleUnmapWaitMaxUs, scaleReadbackTiming.unmapWaitUs);
          stats.gpuScaleUnmapSumUs += scaleReadbackTiming.unmapUs;
          stats.gpuScaleUnmapMaxUs = std::max(stats.gpuScaleUnmapMaxUs, scaleReadbackTiming.unmapUs);
        } else {
          ++stats.gpuScaleFail;
          capture.gpuScalerHealthy = false;
          std::cout << "[native-video-host] gpu scaler disabled after failure; fallback=cpu\n";
        }
      }
      if (!scaleOk) {
        ++stats.gpuScaleCpuFallback;
        if (!resize_bgra_bilinear(payload->data(), w, h, stride, encoder.activeEncodeW, encoder.activeEncodeH, &scaledBgra)) {
          return Flow::Continue;
        }
      }
      encodeSrc = scaledBgra.data();
      encodeSrcW = encoder.activeEncodeW;
      encodeSrcH = encoder.activeEncodeH;
      encodeSrcStride = encoder.activeEncodeW * 4;
      const uint64_t scaleDoneUs = qpc_now_us();
      scaleUs = (scaleDoneUs >= scaleStartUs) ? (scaleDoneUs - scaleStartUs) : 0;
    }

    const uint64_t prepDoneUs = qpc_now_us();
    preEncodePrepUs = (prepDoneUs >= preEncodeStartUs) ? (prepDoneUs - preEncodeStartUs) : 0;

    const uint64_t beforeEncodeUs = qpc_now_us();
    const uint64_t frameAgeBeforeEncodeUs =
        (callbackUs > 0 && beforeEncodeUs >= callbackUs) ? (beforeEncodeUs - callbackUs) : 0;
    uint64_t latestVersion = version;
    {
      std::lock_guard<std::mutex> lk(res.frame.mu);
      latestVersion = res.frame.version;
    }
    if (guardStalePreEncode &&
        frameAgeBeforeEncodeUs > kMaxPreEncodeFrameAgeUs && latestVersion != version) {
      ++stats.stalePreEncodeDropCount;
      return Flow::Continue;
    }

     if (clientMetrics.requestedKeyFrame.exchange(false)) {
      const uint16_t reason = clientMetrics.keyFrameReason.load();
      std::cout << "[native-video-host][control] keyframe-request-consumed reason=" << reason << "\n";
      encoder.forceKeyNext = true;
    }
    if (sender.requestKey.exchange(false, std::memory_order_acq_rel)) {
      // The sender dropped a backlog; the stream needs an IDR to resynchronize.
      encoder.forceKeyNext = true;
    }
     // The keyint schedule applies to REAL frames only. A kick/refresh-served synthetic frame
     // carries seq=0, and 0 % keyint == 0 made every one of them an IDR -- defeating the open-
     // barrier design of riding the held frame as a cheap P-frame (a 40-160KB IDR instead of a
     // few-KB P, once per kick/refresh). A closed barrier still gets its IDR via encoder.forceKeyNext.
     // A single submit latch (encoder.forceKeySubmittedAtUs) covers ALL key reasons -- request,
     // first-frame (encoder.encodedSeq==0), and the keyint schedule: one key input pending inside the
     // async MFT satisfies every one of them, so none may re-force while it is in flight. The
     // measured 4-5 consecutive-IDR trains came from forcing every input until the key finally
     // surfaced. The latch is stamped only after the encoder ACCEPTS the input (below), and
     // times out after 300ms so a lost key is retried.
      const uint64_t encodeStartUs = qpc_now_us();
     const bool forceKeyInFlight =
         encoder.forceKeySubmittedAtUs != 0 && encodeStartUs < encoder.forceKeySubmittedAtUs + 300'000;
     const bool scheduledKey =
         !servedBootstrap && (encoder.activeKeyint > 0) && ((seq % encoder.activeKeyint) == 0);
     const bool keyWanted = encoder.forceKeyNext || (encoder.encodedSeq == 0) || scheduledKey;
     const bool forceKeyFrame = keyWanted && !forceKeyInFlight;
      const uint64_t encodeInputUs = captureStampUs;
      if (capture.timelineOriginUs < 0) {
        capture.timelineOriginUs = static_cast<int64_t>(encodeInputUs);
      }
      const uint64_t queueToEncodeUs = (encodeStartUs >= queuePopUs) ? (encodeStartUs - queuePopUs) : 0;
     const uint64_t callbackToEncodeStartUs =
          (encodeStartUs >= callbackUs) ? (encodeStartUs - callbackUs) : 0;
      std::vector<H264AccessUnit> units;
      H264EncodeFrameStats encodeStats{};
      bool surfaceEncoded = false;
      // The MFT encode is the prime suspect for a driver/GPU wedge that stops the whole loop
      // without returning; mark the phase so the watchdog attributes a hang here correctly.
      watchdog.EnterMainPhase(MainLoopPhase::EncodeCall);
      if (wantSurfaceEncode) {
        auto nv12Tex = res.captureReadback.Nv12SlotTexture(nv12Slot, nv12Generation);
        if (nv12Tex &&
            encoder.codec.encode_frame_surface(nv12Tex.Get(), forceKeyFrame,
                                         static_cast<int64_t>(encodeInputUs) * 10, &units,
                                         &encodeStats)) {
          surfaceEncoded = true;
          ++encoder.nv12SurfaceEncodeCount;
          Nv12PendingRelease pending;
          pending.slot = nv12Slot;
          pending.generation = nv12Generation;
          pending.requiredOutputs = encoder.outputSamplesTotal + 1;
          encoder.nv12PendingReleases.push_back(pending);
          poppedNv12Slot = -1;  // ownership moved to the deferred-release queue
          // Accepting a DXGI sample is no proof the vendor path is fast: AMF accepts them
          // and then takes ~68ms a frame on internal synchronization (measured; the CPU
          // path runs 4.5ms). Probe the first frames and drop back for the session when
          // the surface path costs more than half the 33ms frame budget on average.
          encoder.surfaceEncodeProbeSumUs += encodeStats.encodeCallUs;
          if (++encoder.surfaceEncodeProbeCount == 30) {
            const uint64_t avgUs = encoder.surfaceEncodeProbeSumUs / encoder.surfaceEncodeProbeCount;
            if (avgUs > 16000) {
              encoder.surfaceEncodeHealthy = false;
              res.captureReadback.SetNv12Enabled(false);
              std::cout << "[native-video-host] nv12 surface encode too slow avgUs=" << avgUs
                        << " backend=" << encoder.codec.backend_name()
                        << "; reverting to cpu nv12\n";
            } else {
              std::cout << "[native-video-host] nv12 surface encode probe ok avgUs=" << avgUs
                        << " backend=" << encoder.codec.backend_name() << "\n";
            }
            encoder.surfaceEncodeProbeCount = 0;
            encoder.surfaceEncodeProbeSumUs = 0;
          }
        } else {
          // One rejection turns the path off for the session; this frame is dropped and
          // the next one takes the CPU route. Its slot is released at the next loop top.
          encoder.surfaceEncodeHealthy = false;
          res.captureReadback.SetNv12Enabled(false);
          std::cout << "[native-video-host] nv12 surface encode rejected backend="
                    << encoder.codec.backend_name() << "; falling back to cpu nv12\n";
          return Flow::Continue;
        }
      }
     if (!surfaceEncoded &&
         !encoder.codec.encode_frame_bgra(encodeSrc, encodeSrcW, encodeSrcH, encodeSrcStride,
                                    forceKeyFrame, static_cast<int64_t>(encodeInputUs) * 10,
                                    &units, &encodeStats)) {
      ++encoder.encodeFailCount;
      if ((encoder.encodeFailCount % 60) == 1) {
        std::cout << "[native-video-host] encode failed count=" << encoder.encodeFailCount << "\n";
      }
      return Flow::Continue;
    }
    // Encode returned; back to ordinary work for the watchdog's threshold.
    watchdog.EnterMainPhase(MainLoopPhase::Loop);
    if (forceKeyFrame) {
      // Latch/count only for inputs the encoder actually ACCEPTED: a failed encode never
      // reached the MFT, and arming the latch for it would suppress the retry for 300ms.
      ++encoder.forceKeyInputCount;
      encoder.forceKeySubmittedAtUs = encodeStartUs;
    }
    if (!surfaceEncoded) {
      nv12Us = encodeStats.colorConvertUs;
      preEncodePrepUs += nv12Us;
    }
    encoder.outputSamplesTotal += encodeStats.processOutputSamples;
    if (!servedBootstrap) {
      // A real frame was just handed to the async MFT; it becomes the encoder's held input until
      // the next frame arrives. Record its capture timestamp and (re)arm the trailing kick so the
      // deadline always trails the LAST real input -- continuous motion keeps pushing it out and
      // adds zero synthetic frames; only a genuine pause lets the kick fire to flush this frame.
      kick.lastRealInputCaptureUs = encodeInputUs;
      kick.Arm(qpc_now_us(), useH264);
    }
    while (!encoder.nv12PendingReleases.empty() &&
           encoder.nv12PendingReleases.front().requiredOutputs <= encoder.outputSamplesTotal) {
      res.captureReadback.ReleaseNv12Slot(encoder.nv12PendingReleases.front().slot,
                                      encoder.nv12PendingReleases.front().generation);
      encoder.nv12PendingReleases.pop_front();
    }
    const uint64_t encodeEndUs = qpc_now_us();

    // Encoder output-liveness heartbeat. Placed BEFORE the units.empty() early-out below so a
    // starved encoder -- which returns empty on every call -- is still observed here; the old
    // `continue` skipped the whole 1s stats / self-heal tail, so a wedge produced no telemetry at
    // all. A frame was just handed to the MFT this call, so input is advancing; only the OUTPUT is
    // in question. This block changes no control flow (diagnostic only).
    ++encoder.inputAcceptedTotal;
    if (servedBootstrap) {
      ++encoder.syntheticInputAccepted;
    } else {
      ++encoder.realInputAccepted;
    }
    if (encodeStats.processOutputSamples > 0) {
      encoder.outputAuTotal += encodeStats.processOutputSamples;
      encoder.lastOutputUs = encodeEndUs;
      encoder.noOutputSinceUs = 0;
      encoder.acceptedNoOutputStreak = 0;
      // Reset the episode so the next starvation logs its first line immediately, and clear the
      // per-streak async accumulators.
      encoder.lastStarvationLogUs = 0;
      encoder.starveNeedInputAccum = encoder.starveHaveOutputAccum = encoder.starveNoEventAccum = 0;
      encoder.starveNotAcceptingAccum = encoder.starveNeedMoreAccum = encoder.starveNeedInputOnlyCalls = 0;
      // Revive watchdog.mainLoopLastSeq (previously declared but never stored, so the watchdog record read
      // a constant 0): publish real encoder-output progress, not loop iterations. A follow-up can
      // make the watchdog fire on this age while input is still being accepted.
      watchdog.mainLoopLastSeq.store(encoder.outputSamplesTotal, std::memory_order_release);
    } else {
      ++encoder.acceptedNoOutputStreak;
      if (encoder.noOutputSinceUs == 0) encoder.noOutputSinceUs = encodeEndUs;
      encoder.starveNeedInputAccum += encodeStats.asyncPollNeedInputCount;
      encoder.starveHaveOutputAccum += encodeStats.asyncPollHaveOutputCount;
      encoder.starveNoEventAccum += encodeStats.asyncPollNoEventCount;
      encoder.starveNotAcceptingAccum += encodeStats.processInputNotAcceptingCount;
      encoder.starveNeedMoreAccum += encodeStats.processOutputNeedMoreInputCount;
      encoder.starveNeedInputOnlyCalls += encodeStats.asyncNeedInputOnlyCall;
      // Age is measured from when the streak began, NOT from encoder.lastOutputUs, so an encoder
      // that never emitted a single AU since startup (encoder.lastOutputUs==0) is still detected.
      const uint64_t noOutputAgeUs =
          (encoder.noOutputSinceUs > 0 && encodeEndUs > encoder.noOutputSinceUs)
              ? (encodeEndUs - encoder.noOutputSinceUs)
              : 0;
      // Stream active + encoder keeps accepting input but produces no output for a while = the
      // async-MFT output-starvation wedge (video frozen, main loop spinning, liveness watchdog
      // green). Emit one rate-limited anomaly line with the streak-accumulated async counters so a
      // field recurrence tells a host event-driving bug (NeedInput accrues, HaveOutput stays 0)
      // from a genuine vendor/hardware stall. Recovery is a separate follow-up; diagnostic only.
      if (clientSession.streamControlActive.load(std::memory_order_acquire) &&
          encoder.acceptedNoOutputStreak >= 8 && noOutputAgeUs >= 1000000ULL &&
          (encoder.lastStarvationLogUs == 0 ||
           encodeEndUs >= encoder.lastStarvationLogUs + 1000000ULL)) {
        encoder.lastStarvationLogUs = encodeEndUs;
        std::cout << "[native-video-host] encoder-output-starvation"
                  << " acceptedNoOutputStreak=" << encoder.acceptedNoOutputStreak
                  << " noOutputAgeUs=" << noOutputAgeUs
                  << " everOutput=" << (encoder.lastOutputUs > 0 ? 1 : 0)
                  << " realIn=" << encoder.realInputAccepted
                  << " synthIn=" << encoder.syntheticInputAccepted
                  << " outAu=" << encoder.outputAuTotal
                  << " asyncEnabled=" << static_cast<unsigned>(encodeStats.asyncEnabled)
                  << " streakNeedInput=" << encoder.starveNeedInputAccum
                  << " streakHaveOutput=" << encoder.starveHaveOutputAccum
                  << " streakNeedInputOnlyCalls=" << encoder.starveNeedInputOnlyCalls
                  << " streakNoEvent=" << encoder.starveNoEventAccum
                  << " streakNotAccepting=" << encoder.starveNotAcceptingAccum
                  << " streakNeedMore=" << encoder.starveNeedMoreAccum
                  << " pendingDepth=" << encodeStats.pendingInputDepth
                  << " pendingOverflow=" << encodeStats.pendingInputOverflowTotal
                  << "\n";
      }
    }

    if (units.empty()) return Flow::Continue;

    stats.captureAgeSumUs += captureAgeAtCallbackUs;
    stats.captureAgeMaxUs = std::max(stats.captureAgeMaxUs, captureAgeAtCallbackUs);
    stats.callbackToEncodeStartSumUs += callbackToEncodeStartUs;
    stats.callbackToEncodeStartMaxUs = std::max(stats.callbackToEncodeStartMaxUs, callbackToEncodeStartUs);

    bool encoderResetTriggered = false;
    bool sessionReconnectTriggered = false;
    bool countedRawForInput = false;
    if (sender.sendFailed.exchange(false, std::memory_order_acq_rel)) {
      // Same policy the inline path had: a UDP send failure on an endless session waits
      // for the peer to re-Hello rather than exiting.
      ++sender.udpTxFail;
      if (args.seconds == 0) {
        return Flow::Continue;
      }
    }
      // An async MFT can release several access units from one encode call. They are pushed
      // microseconds apart, so the sender thread has usually not been scheduled between them
      // and the queue depth reflects the burst rather than a backlogged wire. Counting that
      // as congestion discarded the whole GOP and forced an IDR on a perfectly healthy link.
      //
      // Judge congestion once, on the backlog that existed *before* this batch: that is the
      // only part of the queue the sender has genuinely failed to drain. Sizing the limit
      // from the batch instead would still overflow on the last unit whenever a frame was
      // already queued, and a large drain would authorise an equally large queue -- seconds
      // of latency -- so the absolute cap below bounds it regardless.
      constexpr size_t kSenderQueueMaxFrames = 6;
      size_t senderBacklogBeforeBatch = 0;
      {
        std::lock_guard<std::mutex> lk(sender.mu);
        senderBacklogBeforeBatch = sender.queue.size();
      }
      const bool senderBacklogged = senderBacklogBeforeBatch >= 2;
      for (const auto& au : units) {
        if (au.bytes.empty()) continue;
        const int64_t auCaptureUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : static_cast<int64_t>(encodeInputUs);
        // This AU carries the capture timestamp of the input frame it was produced from (the async
        // MFT preserves input sample times FIFO). Observing it is the proof a given real input has
        // finally come OUT of the encoder -- the cancel signal for the trailing kick. Track the
        // newest we have seen so a pending kick disarms once the latest real input has emerged.
        if (auCaptureUs > 0 && static_cast<uint64_t>(auCaptureUs) > kick.lastEmittedAuCaptureUs) {
          kick.lastEmittedAuCaptureUs = static_cast<uint64_t>(auCaptureUs);
        }
        if (encoder.auTimelineOriginUs < 0 && capture.timelineOriginUs >= 0) {
          encoder.auTimelineOriginUs = static_cast<int64_t>(auCaptureUs) -
                               (static_cast<int64_t>(encodeInputUs) - capture.timelineOriginUs);
        }
        const int64_t captureTimelineRelativeUs = static_cast<int64_t>(encodeInputUs) - capture.timelineOriginUs;
        const int64_t auTimelineRelativeUs = static_cast<int64_t>(auCaptureUs) - encoder.auTimelineOriginUs;
        const int64_t captureToAuTimelineDeltaUs = captureTimelineRelativeUs - auTimelineRelativeUs;
        const uint64_t captureToAuTimelineSkewUs =
            (captureToAuTimelineDeltaUs >= 0)
                ? static_cast<uint64_t>(captureToAuTimelineDeltaUs)
                : static_cast<uint64_t>(-captureToAuTimelineDeltaUs);
        const int64_t captureToAuSignedDeltaUs = static_cast<int64_t>(auCaptureUs) - static_cast<int64_t>(encodeInputUs);
        const uint64_t captureToAuSkewUs =
            (captureToAuSignedDeltaUs >= 0)
                ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
        const uint64_t captureToAuUs = (captureToAuSignedDeltaUs >= 0)
                                           ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                           : 0;
        const uint64_t encodedAgeUs =
            (encodeEndUs >= static_cast<uint64_t>(auCaptureUs))
                ? (encodeEndUs - static_cast<uint64_t>(auCaptureUs))
                : 0;
      if (guardStaleEncoded && encodedAgeUs > kMaxEncodedFrameAgeUs) {
        ++stats.staleEncodedDropCount;
        ++encoder.consecutiveStaleFrames;
        if ((stats.staleEncodedDropCount % 60) == 1) {
          std::cout << "[native-video-host] stale encoded drop count=" << stats.staleEncodedDropCount
                    << " encodedAgeUs=" << encodedAgeUs
                    << " thresholdUs=" << kMaxEncodedFrameAgeUs
                    << " consecutive=" << encoder.consecutiveStaleFrames
                    << "\n";
        }
        if (encoder.consecutiveStaleFrames >= kMaxConsecutiveStaleEncodedFrames) {
          std::cout << "[native-video-host] encoder reset due to stale output age="
                    << encodedAgeUs << "us consecutive=" << encoder.consecutiveStaleFrames << "\n";
          encoder.codec.shutdown();
          if (!encoder.codec.initialize(encoder.activeEncodeW, encoder.activeEncodeH, encoder.activeFps, encoder.activeBitrate, encoder.activeKeyint)) {
          std::cerr << "[native-video-host] encoder reinitialize failed\n";
            sendFailed = true;
            break;
          }
          encoder.ResetTimelineAnchors(capture);
          encoder.ResetStarvationEpisode();
          // Same contract as the reinit sites above: the reset discarded any pending key input.
          encoder.forceKeySubmittedAtUs = 0;
          ++encoder.resetCount;
          encoder.consecutiveStaleFrames = 0;
          encoder.forceKeyNext = true;
          encoderResetTriggered = true;
          break;
        }
        continue;
      }
      encoder.consecutiveStaleFrames = 0;

      // The requested IDR can be delayed behind older async MFT output. Only the AU's
      // actual CleanPoint/IDR state is safe to advertise as a keyframe.
      const bool encodedKeyFrame = au.keyFrame;
      // A barrier-opening kick (fresh viewer, no reference frames) must deliver a real IDR: a
      // non-IDR AU would decode into garbage. Drop anything but an IDR in that case. An ordinary
      // trailing-edge kick on an OPEN stream, however, is flushing out the last real held frame,
      // whose P-frame references the decoder already has -- so let it through.
      if (servedBootstrap && kickForcedKey && !encodedKeyFrame) {
        continue;
      }
      if (kick.selectionFirstKeyframePendingGeneration != 0 &&
          streamGeneration == kick.selectionFirstKeyframePendingGeneration &&
          !encodedKeyFrame) {
        ++kick.selectionFirstKeyframeDropCount;
        if ((kick.selectionFirstKeyframeDropCount % 30ULL) == 1ULL) {
          std::cout << "[native-video-host] selection generation waiting keyframe streamGen="
                    << streamGeneration
                    << " droppedAu=" << kick.selectionFirstKeyframeDropCount
                    << " forceKeyNext=" << (encoder.forceKeyNext ? 1 : 0)
                    << "\n";
        }
        continue;
      }

      EncodedFrameHeader hdr{};
      hdr.header.magic = remote60::native_poc::kMagic;
      hdr.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
      hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
      hdr.seq = ++encoder.encodedSeq;
      hdr.width = encoder.activeEncodeW;
      hdr.height = encoder.activeEncodeH;
      hdr.payloadSize = static_cast<uint32_t>(au.bytes.size());
      hdr.flags = encodedKeyFrame ? 1u : 0u;
      hdr.streamGeneration = streamGeneration;
      hdr.captureQpcUs =
          static_cast<uint64_t>(std::max<int64_t>(0, auCaptureUs));
      hdr.encodeStartQpcUs = encodeStartUs;
      hdr.encodeEndQpcUs = encodeEndUs;
      SendPathStats sendPathStats{};
      const uint64_t sendStartUs = qpc_now_us();
      const uint64_t sendIntervalUs =
          (stats.lastSendStartUs > 0 && sendStartUs >= stats.lastSendStartUs) ? (sendStartUs - stats.lastSendStartUs) : 0;
      const uint64_t sendIntervalErrUs =
          (encoder.activeFrameIntervalUs > 0 && sendIntervalUs > 0)
              ? ((sendIntervalUs >= encoder.activeFrameIntervalUs) ? (sendIntervalUs - encoder.activeFrameIntervalUs)
                                                           : (encoder.activeFrameIntervalUs - sendIntervalUs))
              : 0;
      const uint64_t queueToSendUs = (sendStartUs >= queuePopUs) ? (sendStartUs - queuePopUs) : 0;
      const uint64_t sendToEncodeUs = (sendStartUs >= encodeEndUs) ? (sendStartUs - encodeEndUs) : 0;
      const uint64_t encodeSpanUs = (encodeEndUs >= encodeStartUs) ? (encodeEndUs - encodeStartUs) : 0;
      const uint64_t sendWaitUs =
          (queueToSendUs >= (queueToEncodeUs + encodeSpanUs))
              ? (queueToSendUs - queueToEncodeUs - encodeSpanUs)
              : 0;
      const uint64_t callbackToSendStartUs = (sendStartUs >= callbackUs) ? (sendStartUs - callbackUs) : 0;
      hdr.sendQpcUs = sendStartUs;

      bool sentOk = false;
      bool enqueuedForSend = false;
      if (transport == VideoTransport::Tcp) {
        enqueuedForSend = true;
        sentOk = send_all_timed(clientSession.clientSock, &hdr, sizeof(hdr), &sendPathStats.headerUs,
                                &sendPathStats.headerCallCount) &&
                 send_all_timed(clientSession.clientSock, au.bytes.data(), au.bytes.size(), &sendPathStats.payloadUs,
                               &sendPathStats.payloadCallCount);
      } else {
        if (!sender.udpPeerReady) {
          ++sender.udpTxNoPeer;
          sentOk = false;
        } else {
          EncodedSendItem item;
          item.keyFrame = (hdr.flags & 1u) != 0;
          item.frameIntervalUs = encoder.activeFrameIntervalUs;
          item.udpHdr.magic = remote60::native_poc::kMagic;
          item.udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
          item.udpHdr.size = static_cast<uint16_t>(sizeof(item.udpHdr));
          item.udpHdr.seq = hdr.seq;
          item.udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
          item.udpHdr.flags = (hdr.flags & 1u) ? 0x1u : 0u;
          item.udpHdr.width = hdr.width;
          item.udpHdr.height = hdr.height;
          item.udpHdr.stride = 0;
          item.udpHdr.payloadSize = hdr.payloadSize;
          item.udpHdr.streamGeneration = hdr.streamGeneration;
          item.udpHdr.captureQpcUs = hdr.captureQpcUs;
          item.udpHdr.encodeStartQpcUs = hdr.encodeStartQpcUs;
          item.udpHdr.encodeEndQpcUs = hdr.encodeEndQpcUs;
          item.udpHdr.sendQpcUs = hdr.sendQpcUs;  // sender restamps at wire time
          item.bytes = std::move(au.bytes);
          {
            std::lock_guard<std::mutex> lk(sender.mu);
            // Stamp under the same lock the rollover bumps the epoch under, so the stamp is
            // consistent with the queue-clear: a delta stamped just after a rollover carries the
            // new epoch (and rides the fresh barrier); one stamped just before is dropped at
            // dequeue. This is also how the static bootstrap IDR gets tagged for the new epoch --
            // it flows through this same enqueue path and needs no special case.
            item.mediaEpoch = sender.mediaSessionEpoch.load(std::memory_order_acquire);
            item.enqueueUs = qpc_now_us();  // AU handed to sender; sender derives queueWaitUs
            if (item.keyFrame) {
              // A new IDR makes every queued frame irrelevant and re-anchors the stream. This is
              // also the barrier-open point: a real (or bootstrap) key AU for the current epoch
              // clears sender.waitingForKey so deltas may flow again.
              sender.dropCount.fetch_add(sender.queue.size(), std::memory_order_relaxed);
              sender.heldFrames += sender.queue.size();
              sender.sentFrames -= std::min<uint64_t>(sender.sentFrames, sender.queue.size());
              sender.queue.clear();
              sender.waitingForKey = false;
              if (sender.firstKeyEnqueuedUs == 0) sender.firstKeyEnqueuedUs = sendStartUs;
              sender.queue.push_back(std::move(item));
              enqueuedForSend = true;
            } else if (sender.waitingForKey) {
              // This delta references dropped frames; sending it would decode into
              // block garbage. Hold everything until the forced keyframe arrives.
              ++sender.nonKeyAuWhileWaiting;
              sender.dropCount.fetch_add(1, std::memory_order_relaxed);
              sender.requestKey.store(true, std::memory_order_release);
            } else if (senderBacklogged || sender.queue.size() >= kSenderQueueMaxFrames) {
              // Backlogged: drop the stale frames AND this delta -- it references what
              // was just dropped -- then resync with a fresh IDR.
              sender.dropCount.fetch_add(sender.queue.size() + 1, std::memory_order_relaxed);
              // Frames already counted as sent are being erased here; move them to the held
              // tally so the reported wire rate does not include what never left.
              sender.heldFrames += sender.queue.size();
              sender.sentFrames -= std::min<uint64_t>(sender.sentFrames, sender.queue.size());
              sender.queue.clear();
              sender.waitingForKey = true;
              sender.requestKey.store(true, std::memory_order_release);
            } else {
              sender.queue.push_back(std::move(item));
              enqueuedForSend = true;
            }
          }
          if (enqueuedForSend) sender.cv.notify_one();
          // Handing the frame off succeeded even when the queue policy discarded it; this
          // flag means "no transport failure", and clearing it here would tear the session
          // down. Whether the frame really went out is tracked by enqueuedForSend below.
          sentOk = true;
        }
      }
      const uint64_t sendDoneUs = qpc_now_us();
      const uint64_t sendDurUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
      const uint64_t sendCallCount = sendPathStats.headerCallCount + sendPathStats.payloadCallCount;
      if (sentOk) {
        stats.lastSendStartUs = sendStartUs;
        capture.LogFirstSentGeneration(res, stats, 
            transport == VideoTransport::Tcp ? "h264-tcp" : "h264-udp",
            streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);
        if (kick.selectionFirstKeyframePendingGeneration != 0 &&
            streamGeneration == kick.selectionFirstKeyframePendingGeneration &&
            (hdr.flags & 1u) != 0) {
          std::cout << "[native-video-host] selection first keyframe sent streamGen="
                    << streamGeneration
                    << " captureQpcUs=" << hdr.captureQpcUs
                    << " sendQpcUs=" << hdr.sendQpcUs
                    << " key=1"
                    << "\n";
          kick.selectionFirstKeyframePendingGeneration = 0;
          kick.selectionFirstKeyframeDropCount = 0;
        }
        // UDP tx counters are owned by the sender thread now; nothing to count here.
        if (!servedBootstrap && frameGating.enabled && enqueuedForSend && payload &&
            !payload->empty()) {
          frameGating.lastSentUs = sendStartUs;
          frameGating.refPayload = payload;
          frameGating.refW = w;
          frameGating.refH = h;
          frameGating.refStride = stride;
        }
      }
      if (!sentOk) {
        if (transport == VideoTransport::Udp) {
          ++sender.udpTxFail;
          if (args.seconds == 0) {
            sessionReconnectTriggered = true;
            break;
          }
        } else if (reconnect_tcp_data_session(hx, "h264_send_fail")) {
          sessionReconnectTriggered = true;
          break;
        }
        sendFailed = true;
        break;
      }

      // A frame the sender queue discarded never reaches the wire. Counting it kept fps and
      // bitrate reporting a healthy stream straight through a cutout, which is precisely the
      // window that is visible to the user as a freeze -- so count only what was handed on.
      if (transport == VideoTransport::Udp && !enqueuedForSend) {
        ++sender.heldFrames;
        continue;
      }
      // A trailing-edge kick is a single sparse frame; keep it out of the fps/bitrate and ABR
      // evidence (it is counted separately as kick.count). It still consumes the forced
      // keyframe below so the normal path does not re-force one on the next real frame.
      if (!servedBootstrap) {
        ++sender.sentFrames;
        ++encoder.encodedFrames;
        sender.sentBytes += hdr.payloadSize;
        if (!countedRawForInput) {
          stats.rawEquivalentBytes +=
              static_cast<uint64_t>(encoder.activeEncodeW) * static_cast<uint64_t>(encoder.activeEncodeH) * 3 / 2;
          countedRawForInput = true;
        }
      }
      if ((hdr.flags & 1u) != 0) {
        encoder.forceKeyNext = false;
        encoder.forceKeySubmittedAtUs = 0;
      }

      if (args.traceEvery > 0 && (hdr.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || stats.tracePrinted < args.traceMax)) {
        ++stats.tracePrinted;
        const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
        const uint64_t encQueueUs =
            (encodeStartUs >= static_cast<uint64_t>(auCaptureUs))
                ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs))
                : 0;
        const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
        const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
        const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                   : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
        const uint64_t encUs = (encodeSpanUs >= nv12Us) ? (encodeSpanUs - nv12Us) : 0;
        const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
        const char* encBackendName = encoder.codec.backend_name();
        const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
        const uint64_t encApiHw = encoder.codec.using_hardware() ? 1ull : 0ull;
        const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
            queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
            sendDurUs, sendIntervalErrUs);
        std::cout << "[native-video-host][trace] seq=" << hdr.seq
                  << " captureUs=" << hdr.captureQpcUs
                  << " encodeStartUs=" << hdr.encodeStartQpcUs
                  << " encodeEndUs=" << hdr.encodeEndQpcUs
                  << " sendUs=" << hdr.sendQpcUs
                  << " bottleneckStageCode=" << bottleneck.code
                  << " bottleneckStageUs=" << bottleneck.us
                  << " bottleneckStageName=" << bottleneck.name
                  << " c2eUs=" << c2eUs
                  << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " captureD3DWaitUs=" << captureD3DWaitUs
                  << " captureCopyMapUs=" << captureCopyMapUs
                  << " captureMemcpyUs=" << captureMemcpyUs
                  << " captureUnmapWaitUs=" << captureUnmapWaitUs
                  << " captureUnmapUs=" << captureUnmapUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                   << " queueSelectWaitUs=" << queueSelectWaitUs
                   << " queueGapFrames=" << queueGapFrames
                   << " encQueueUs=" << encQueueUs
                   << " encQueueAlignedUs=" << encQueueAlignedUs
                   << " captureToAuSkewUs=" << captureToAuSkewUs
                   << " captureToAuTimelineDeltaUs="
                   << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                    << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                    << " auTsFromOutput=" << auTsFromOutput
                    << " auTsSkewUs=" << auTsSkewUs
                    << " captureTimelineOriginUs=" << capture.timelineOriginUs
                   << " auTimelineOriginUs=" << encoder.auTimelineOriginUs
                   << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                   << " auTimelineRelativeUs=" << auTimelineRelativeUs
                    << " frameCaptureUs=" << captureStampUs
                    << " captureToAuUs=" << captureToAuUs
                    << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                    << " encodeInputUs=" << encodeInputUs
                    << " captureToQueueUs=" << captureToQueueUs
                   << " queueWaitUs=" << queueWaitUs
                   << " queueWaitReason=" << queueWaitReason
                   << " queueToEncodeUs=" << queueToEncodeUs
                   << " queueToSendUs=" << queueToSendUs
                   << " sendIntervalUs=" << sendIntervalUs
                   << " sendIntervalErrUs=" << sendIntervalErrUs
                   << " preEncodePrepUs=" << preEncodePrepUs
                   << " scaleUs=" << scaleUs
                   << " scaleD3DWaitUs=" << scaleReadbackTiming.d3dWaitUs
                   << " scaleCopyMapUs=" << scaleReadbackTiming.copyMapUs
                   << " scaleMemcpyUs=" << scaleReadbackTiming.memcpyUs
                   << " scaleUnmapWaitUs=" << scaleReadbackTiming.unmapWaitUs
                   << " scaleUnmapUs=" << scaleReadbackTiming.unmapUs
                   << " nv12Us=" << nv12Us
                   << " sendWaitUs=" << sendWaitUs
                   << " sendToEncodeUs=" << sendToEncodeUs
                   << " tickWaitUs=" << tickWaitUs
                   << " queueDepth=" << queueDepthAtPop
                  << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                  << " sendCallCount=" << sendCallCount
                  << " sendHeaderUs=" << sendPathStats.headerUs
                  << " sendPayloadUs=" << sendPathStats.payloadUs
                  << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                  << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                  << " sendChunkCount=" << sendPathStats.payloadChunkCount
                  << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                  << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << " cb2eUs=" << callbackToEncodeStartUs
                  << " capAgeUs=" << captureAgeAtCallbackUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " encApiPathCode=" << encApiPathCode
                  << " encApiHw=" << encApiHw
                  << " encApiInputUs=" << encodeStats.processInputUs
                  << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                  << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                  << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                  << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                  << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                  << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                  << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                  << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                  << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                  << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                  << " payloadBytes=" << hdr.payloadSize
                  << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                  << "\n";
      }
      const uint64_t c2eUs = (hdr.encodeStartQpcUs >= hdr.captureQpcUs) ? (hdr.encodeStartQpcUs - hdr.captureQpcUs) : 0;
      const uint64_t encQueueUs =
          (encodeStartUs >= static_cast<uint64_t>(auCaptureUs)) ? (encodeStartUs - static_cast<uint64_t>(auCaptureUs)) : 0;
      const uint64_t encQueueAlignedUs = (encodeStartUs >= encodeInputUs) ? (encodeStartUs - encodeInputUs) : 0;
      const uint64_t auTsFromOutput = au.sampleTimeFromOutput ? 1ull : 0ull;
      const uint64_t auTsSkewUs = (captureToAuSignedDeltaUs >= 0) ? static_cast<uint64_t>(captureToAuSignedDeltaUs)
                                                                 : static_cast<uint64_t>(-captureToAuSignedDeltaUs);
      const uint64_t encUs = (encodeSpanUs >= nv12Us) ? (encodeSpanUs - nv12Us) : 0;
      const uint64_t e2sUs = (hdr.sendQpcUs >= hdr.encodeEndQpcUs) ? (hdr.sendQpcUs - hdr.encodeEndQpcUs) : 0;
      const uint64_t pipeUs = (hdr.sendQpcUs >= hdr.captureQpcUs) ? (hdr.sendQpcUs - hdr.captureQpcUs) : 0;
      const char* encBackendName = encoder.codec.backend_name();
      const uint64_t encApiPathCode = encoder_api_path_code(encBackendName);
      const uint64_t encApiHw = encoder.codec.using_hardware() ? 1ull : 0ull;
      const HostBottleneckStage bottleneck = detect_host_bottleneck_stage(
          queueWaitUs, queueToEncodeUs, preEncodePrepUs, scaleUs, nv12Us, encUs, queueToSendUs,
          sendDurUs, sendIntervalErrUs);
      if (pipeUs >= kHostUserFeedbackWarnUs &&
          (hdr.sendQpcUs >= lastUserFeedbackUs + kHostUserFeedbackMinIntervalUs || lastUserFeedbackUs == 0)) {
      std::cout << "[native-video-host][user-feedback] seq=" << hdr.seq
                << " codec=" << "h264"
                << " pipeUs=" << pipeUs
                << " bottleneckStageCode=" << bottleneck.code
                << " bottleneckStageUs=" << bottleneck.us
                << " bottleneckStageName=" << bottleneck.name
                << " captureToCallbackUs=" << captureToCallbackUs
                  << " callbackIntervalUs=" << callbackIntervalUs
                  << " captureIntervalUs=" << captureIntervalUs
                  << " selectWaitUs=" << frameAgeAtSelectUs
                  << " queueSelectWaitUs=" << queueSelectWaitUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " captureD3DWaitUs=" << captureD3DWaitUs
                  << " captureCopyMapUs=" << captureCopyMapUs
                  << " captureMemcpyUs=" << captureMemcpyUs
                  << " captureUnmapWaitUs=" << captureUnmapWaitUs
                  << " captureUnmapUs=" << captureUnmapUs
                  << " captureToQueueUs=" << captureToQueueUs
                 << " queueWaitUs=" << queueWaitUs
                 << " queueWaitReason=" << queueWaitReason
                   << " queueGapFrames=" << queueGapFrames
                   << " queueDepth=" << queueDepthAtPop
                  << " queueDepthMax=" << stats.queueDepthMax.load(std::memory_order_relaxed)
                  << " queueToEncodeUs=" << queueToEncodeUs
                  << " queueToSendUs=" << queueToSendUs
                  << " sendIntervalUs=" << sendIntervalUs
                  << " sendIntervalErrUs=" << sendIntervalErrUs
                  << " captureClockSkewUs=" << captureClockSkewUs
                  << " sendWaitUs=" << sendWaitUs
                  << " sendToEncodeUs=" << sendToEncodeUs
                   << " tickWaitUs=" << tickWaitUs
                   << " preEncodePrepUs=" << preEncodePrepUs
                   << " scaleUs=" << scaleUs
                   << " scaleD3DWaitUs=" << scaleReadbackTiming.d3dWaitUs
                   << " scaleCopyMapUs=" << scaleReadbackTiming.copyMapUs
                   << " scaleMemcpyUs=" << scaleReadbackTiming.memcpyUs
                   << " scaleUnmapWaitUs=" << scaleReadbackTiming.unmapWaitUs
                   << " scaleUnmapUs=" << scaleReadbackTiming.unmapUs
                   << " nv12Us=" << nv12Us
                   << " c2eUs=" << c2eUs
                    << " encQueueUs=" << encQueueUs
                   << " encQueueAlignedUs=" << encQueueAlignedUs
                    << " captureToAuSkewUs=" << captureToAuSkewUs
                    << " captureToAuTimelineSkewUs=" << captureToAuTimelineSkewUs
                    << " auTsFromOutput=" << auTsFromOutput
                    << " auTsSkewUs=" << auTsSkewUs
                    << " captureToAuTimelineDeltaUs="
                    << (captureToAuTimelineDeltaUs >= 0 ? captureToAuTimelineDeltaUs : 0 - captureToAuTimelineDeltaUs)
                    << " captureTimelineOriginUs=" << capture.timelineOriginUs
                    << " auTimelineOriginUs=" << encoder.auTimelineOriginUs
                    << " captureTimelineRelativeUs=" << captureTimelineRelativeUs
                    << " auTimelineRelativeUs=" << auTimelineRelativeUs
                    << " frameCaptureUs=" << captureStampUs
                    << " captureToAuUs=" << captureToAuUs
                   << " auCaptureUs=" << static_cast<uint64_t>(auCaptureUs)
                   << " encodeInputUs=" << encodeInputUs
                 << " cb2eUs=" << callbackToEncodeStartUs
                 << " cb2sUs=" << callbackToSendStartUs
                  << " sendCallCount=" << sendCallCount
                  << " sendHeaderUs=" << sendPathStats.headerUs
                  << " sendPayloadUs=" << sendPathStats.payloadUs
                  << " sendHeaderCallCount=" << sendPathStats.headerCallCount
                  << " sendPayloadCallCount=" << sendPathStats.payloadCallCount
                  << " sendChunkCount=" << sendPathStats.payloadChunkCount
                  << " sendChunkMaxUs=" << sendPathStats.payloadChunkMaxUs
                  << " sendStartUs=" << sendStartUs
                  << " sendDoneUs=" << sendDoneUs
                  << " sendDurUs=" << sendDurUs
                  << " capAgeUs=" << captureAgeAtCallbackUs
                  << " encUs=" << encUs
                  << " e2sUs=" << e2sUs
                  << " encApiPathCode=" << encApiPathCode
                  << " encApiHw=" << encApiHw
                  << " encApiInputUs=" << encodeStats.processInputUs
                  << " encApiDrainUs=" << encodeStats.processOutputDrainUs
                  << " encApiNotAcceptingCount=" << encodeStats.processInputNotAcceptingCount
                  << " encApiNeedMoreInputCount=" << encodeStats.processOutputNeedMoreInputCount
                  << " encApiStreamChangeCount=" << encodeStats.processOutputStreamChangeCount
                  << " encApiOutputErrorCount=" << encodeStats.processOutputErrorCount
                  << " encApiAsyncEnabled=" << encodeStats.asyncEnabled
                  << " encApiAsyncPollCount=" << encodeStats.asyncPollCount
                  << " encApiAsyncNoEventCount=" << encodeStats.asyncPollNoEventCount
                  << " encApiAsyncNeedInputCount=" << encodeStats.asyncPollNeedInputCount
                  << " encApiAsyncHaveOutputCount=" << encodeStats.asyncPollHaveOutputCount
                  << " payloadBytes=" << hdr.payloadSize
                  << " key=" << ((hdr.flags & 1u) ? 1 : 0)
                  << "\n";
        lastUserFeedbackUs = hdr.sendQpcUs;
      }
    }

    if (encoderResetTriggered || sessionReconnectTriggered) {
      return Flow::Continue;
    }
    if (sendFailed) {
      std::cout << "[native-video-host] client disconnected\n";
      return Flow::Break;
    }
  }

  return Flow::Next;
}

}  // namespace remote60::native_poc
