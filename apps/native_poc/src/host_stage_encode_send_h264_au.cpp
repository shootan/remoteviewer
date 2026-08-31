// Stage 11 (H.264 path), per access unit: AU timestamp / trailing-kick cancel, key-frame bookkeeping,
// sender-queue policy + enqueue (UDP) or the direct TCP send, per-AU telemetry.
//
// Host split refactor Phase 2-13: the body of the `for (auto& au : units)` loop of
// encode_send_h264(), verbatim (its continue / break became AuFlow); see host_stage_encode_send_h264.hpp.

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

AuFlow encode_send_h264_emit_au(HostContext& hx, TickContext& tc, H264AuBatch& b, H264AccessUnit& au) {
  auto& args = hx.args;
  auto& transport = hx.transport;
  auto& guardStaleEncoded = hx.guardStaleEncoded;
  auto& frameGating = hx.frameGating;
  auto& kick = hx.kick;
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
  auto& scaleReadbackTiming = b.scaleReadbackTiming;
  auto& preEncodePrepUs = b.preEncodePrepUs;
  auto& scaleUs = b.scaleUs;
  auto& nv12Us = b.nv12Us;
  auto& encodeStartUs = b.encodeStartUs;
  auto& encodeInputUs = b.encodeInputUs;
  auto& queueToEncodeUs = b.queueToEncodeUs;
  auto& callbackToEncodeStartUs = b.callbackToEncodeStartUs;
  auto& encodeStats = b.encodeStats;
  auto& encodeEndUs = b.encodeEndUs;
  auto& encoderResetTriggered = b.encoderResetTriggered;
  auto& sessionReconnectTriggered = b.sessionReconnectTriggered;
  auto& countedRawForInput = b.countedRawForInput;
  auto& senderBacklogged = b.senderBacklogged;
  if (au.bytes.empty()) return AuFlow::Continue;
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
      return AuFlow::Break;
    }
    encoder.ResetTimelineAnchors(capture);
    encoder.ResetStarvationEpisode();
    // Same contract as the reinit sites above: the reset discarded any pending key input.
    encoder.forceKeySubmittedAtUs = 0;
    ++encoder.resetCount;
    encoder.consecutiveStaleFrames = 0;
    encoder.forceKeyNext = true;
    encoderResetTriggered = true;
    return AuFlow::Break;
  }
  return AuFlow::Continue;
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
  return AuFlow::Continue;
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
  return AuFlow::Continue;
}

EncodedFrameHeader hdr{};
hdr.header.magic = remote60::native_poc::kMagic;
hdr.header.type = static_cast<uint16_t>(MessageType::EncodedFrameH264);
hdr.header.size = static_cast<uint16_t>(sizeof(hdr));
hdr.seq = ++encoder.encodedSeq;
hdr.width = encoder.activeEncodeW;
hdr.height = encoder.activeEncodeH;
hdr.payloadSize = static_cast<uint32_t>(au.bytes.size());
hdr.flags = encodedKeyFrame ? kEncodedFrameFlagKeyFrame : 0u;
// A kick / static-refresh frame is the cached picture re-encoded, not new content; tell the
// viewer so it keeps the frame out of its latency and congestion arithmetic. (F-10 / H-13.)
if (servedBootstrap) hdr.flags |= kEncodedFrameFlagSynthetic;
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
    item.udpHdr.flags = static_cast<uint16_t>(
        ((hdr.flags & kEncodedFrameFlagKeyFrame) ? 0x1u : 0u) |
        ((hdr.flags & kEncodedFrameFlagSynthetic) ? kUdpVideoChunkFlagSynthetic : 0u));
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
      switch (decide_sender_queue_action(item.keyFrame, sender.waitingForKey, senderBacklogged,
                                         sender.queue.size(), kSenderQueueMaxFrames)) {
        case SenderQueueAction::EnqueueKey: {
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
          // The backlog this batch was judged on is gone -- this key just cleared it. Leaving
          // the flag set made the next delta OF THE SAME BATCH drop the queue the IDR had just
          // re-anchored and ask for another key, which came back with the same stale reading:
          // an IDR loop on a link that was fine. One encode call can legitimately release
          // [key, delta] together, because the async MFT drains its accepted-input backlog and
          // that backlog can straddle a GOP boundary. (Ledger H-19.)
          senderBacklogged = false;
          break;
        }
        case SenderQueueAction::HoldForKey:
          // This delta references dropped frames; sending it would decode into
          // block garbage. Hold everything until the forced keyframe arrives.
          ++sender.nonKeyAuWhileWaiting;
          sender.dropCount.fetch_add(1, std::memory_order_relaxed);
          hx.mailbox.PostRequestKeyframe(kKeyframeReasonSenderBacklog);
          break;
        case SenderQueueAction::DropAndResync:
          // Backlogged: drop the stale frames AND this delta -- it references what
          // was just dropped -- then resync with a fresh IDR.
          sender.dropCount.fetch_add(sender.queue.size() + 1, std::memory_order_relaxed);
          // Frames already counted as sent are being erased here; move them to the held
          // tally so the reported wire rate does not include what never left.
          sender.heldFrames += sender.queue.size();
          sender.sentFrames -= std::min<uint64_t>(sender.sentFrames, sender.queue.size());
          sender.queue.clear();
          sender.waitingForKey = true;
          hx.mailbox.PostRequestKeyframe(kKeyframeReasonSenderBacklog);
          break;
        case SenderQueueAction::Enqueue:
          sender.queue.push_back(std::move(item));
          enqueuedForSend = true;
          break;
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
      return AuFlow::Break;
    }
  } else if (reconnect_tcp_data_session(hx, "h264_send_fail")) {
    sessionReconnectTriggered = true;
    return AuFlow::Break;
  }
  sendFailed = true;
  return AuFlow::Break;
}

// A frame the sender queue discarded never reaches the wire. Counting it kept fps and
// bitrate reporting a healthy stream straight through a cutout, which is precisely the
// window that is visible to the user as a freeze -- so count only what was handed on.
if (transport == VideoTransport::Udp && !enqueuedForSend) {
  ++sender.heldFrames;
  return AuFlow::Continue;
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
// The forced key is satisfied when the key AU is ACCEPTED by the send path, not when it lands on
// the wire: on UDP this point is past the enqueue, on TCP past the write. A key AU always takes
// the EnqueueKey branch of the queue policy, so the `!enqueuedForSend` early-out above never
// swallows one. A UDP send that fails afterwards re-arms the media barrier and posts
// RequestKeyframe{SenderBarrier}, which is what makes the weaker condition safe.
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
  return AuFlow::Next;
}

}  // namespace remote60::native_poc
