// Stage 11 (H.264 path): refit debounce, force-key latch, MFT encode, starvation heartbeat, AU
// loop into the sender queue (UDP) or straight onto the TCP socket.
//
// Host split refactor Phase 3.5b: the H.264 arm of the former stage_encode_send if/else, verbatim;
// see host_main_loop.hpp for the loop, HostContext / TickContext and Flow.

//


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
#include "host_stage_encode_send_h264.hpp"
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

Flow encode_send_h264(HostContext& hx, TickContext& tc) {
  auto& args = hx.args;
  auto& useH264 = hx.useH264;
  auto& guardStalePreEncode = hx.guardStalePreEncode;
  auto& poppedNv12Slot = hx.poppedNv12Slot;
  auto& frameGating = hx.frameGating;
  auto& kick = hx.kick;
  auto& clientMetrics = hx.clientMetrics;
  auto& watchdog = hx.watchdog;
  auto& inputRouter = hx.inputRouter;
  auto& sender = hx.sender;
  auto& clientSession = hx.clientSession;
  auto& encoder = hx.encoder;
  auto& stats = hx.stats;
  auto& capture = hx.capture;
  auto& res = hx.res;
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

  // Keyframe requests are consumed once per tick in stage_time_limit, before the frame wait --
   // see MainLoopMailbox. Consuming them here meant a request only took effect after a real frame
   // was popped, which on a static desktop never happens. (Phase 4.)
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
    size_t senderBacklogBeforeBatch = 0;
    {
      std::lock_guard<std::mutex> lk(sender.mu);
      senderBacklogBeforeBatch = sender.queue.size();
    }
    const bool senderBacklogged = senderBacklogBeforeBatch >= 2;
    H264AuBatch b{scaleReadbackTiming, preEncodePrepUs, scaleUs, nv12Us, encodeStartUs, encodeInputUs,
                  queueToEncodeUs, callbackToEncodeStartUs, encodeStats, encodeEndUs, encoderResetTriggered,
                  sessionReconnectTriggered, countedRawForInput, senderBacklogged};
    for (auto& au : units) {  // non-const: the UDP path moves au.bytes (ledger H-08)
      const AuFlow f = encode_send_h264_emit_au(hx, tc, b, au);
      if (f == AuFlow::Continue) continue;
      if (f == AuFlow::Break) break;
    }

  if (encoderResetTriggered || sessionReconnectTriggered) {
    return Flow::Continue;
  }
  if (sendFailed) {
    std::cout << "[native-video-host] client disconnected\n";
    return Flow::Break;
  }
  return Flow::Next;
}

}  // namespace remote60::native_poc
