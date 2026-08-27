// See viewer_video_receiver.hpp. Bodies are the recvThread lambda of native_video_client_main.cpp,
// verbatim (viewer split refactor Phase 2-1).

#include "viewer_video_receiver.hpp"

#include "viewer_frame_gate.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

void VideoReceiver::DecoderSink::reset_decoder() { dec.decoder.reset(); }

bool VideoReceiver::DecoderSink::rebuild_decoder() {
  return dec.decoder.initialize(dec.decoderW, dec.decoderH, args.fpsHint);
}

void VideoReceiver::DecoderSink::request_keyframe(uint16_t reason) { viewer::request_keyframe(reason); }

PresentCounterSnapshot VideoReceiver::load_present_counters() {
    PresentCounterSnapshot s{};
    s.d3dPresentSuccess = gPresent.d3dPresentSuccessCount.load(std::memory_order_relaxed);
    s.d3dPresentFail = gPresent.d3dPresentFailCount.load(std::memory_order_relaxed);
    s.gdiFallbackPresented = gPresent.gdiFallbackPresentedCount.load(std::memory_order_relaxed);
    s.fallbackInitFail = gPresent.fallbackInitFailCount.load(std::memory_order_relaxed);
    s.fallbackRenderFail = gPresent.fallbackRenderFailCount.load(std::memory_order_relaxed);
    s.fallbackNv12ConvertFail = gPresent.fallbackNv12ConvertFailCount.load(std::memory_order_relaxed);
    s.paintCoalesced = gFrameBuf.paintCoalescedCount.load(std::memory_order_relaxed);
    s.overwriteBeforePresent = gFrameBuf.overwriteBeforePresentCount.load(std::memory_order_relaxed);
    return s;
}

void VideoReceiver::append_present_counter_fields(std::ostream& os) {
    const PresentCounterSnapshot nowCounters = load_present_counters();
    const uint64_t d3dPresentSuccess = nowCounters.d3dPresentSuccess - st.lastPresentCounters.d3dPresentSuccess;
    const uint64_t d3dPresentFail = nowCounters.d3dPresentFail - st.lastPresentCounters.d3dPresentFail;
    const uint64_t gdiFallbackPresented =
        nowCounters.gdiFallbackPresented - st.lastPresentCounters.gdiFallbackPresented;
    const uint64_t fallbackInitFail = nowCounters.fallbackInitFail - st.lastPresentCounters.fallbackInitFail;
    const uint64_t fallbackRenderFail = nowCounters.fallbackRenderFail - st.lastPresentCounters.fallbackRenderFail;
    const uint64_t fallbackNv12ConvertFail =
        nowCounters.fallbackNv12ConvertFail - st.lastPresentCounters.fallbackNv12ConvertFail;
    const uint64_t paintCoalesced = nowCounters.paintCoalesced - st.lastPresentCounters.paintCoalesced;
    const uint64_t overwriteBeforePresent =
        nowCounters.overwriteBeforePresent - st.lastPresentCounters.overwriteBeforePresent;
    const uint64_t d3dAttempts = d3dPresentSuccess + d3dPresentFail;
    const uint64_t gdiFallbackRateX1000 = (d3dAttempts > 0)
        ? ((gdiFallbackPresented * 1000ULL) / d3dAttempts)
        : 0;
    os << " d3dPresentSuccess=" << d3dPresentSuccess
       << " d3dPresentFail=" << d3dPresentFail
       << " gdiFallbackPresented=" << gdiFallbackPresented
       << " gdiFallbackRateX1000=" << gdiFallbackRateX1000
       << " fallbackInitFail=" << fallbackInitFail
       << " fallbackRenderFail=" << fallbackRenderFail
       << " fallbackNv12ConvertFail=" << fallbackNv12ConvertFail
       << " paintCoalesced=" << paintCoalesced
       << " overwriteBeforePresent=" << overwriteBeforePresent;
    st.lastPresentCounters = nowCounters;
}

void VideoReceiver::publish_metrics(uint32_t metricW, uint32_t metricH, uint64_t nowUs,
    uint64_t avgLatencyUs, uint64_t maxLatencyUsLocal,
    uint64_t avgDecodeTailUs, uint64_t maxDecodeTailUsLocal,
    double mbpsLocal) {
    const uint64_t cappedRecvFpsX100 = std::min<uint64_t>(st.recvFrames * 100ULL, 0xFFFFFFFFULL);
    const uint64_t cappedDecodedFpsX100 = std::min<uint64_t>(st.decodedFrames * 100ULL, 0xFFFFFFFFULL);
    const double mbpsX1000 = mbpsLocal * 1000.0;
    uint32_t recvMbpsX1000 = 0;
    if (mbpsX1000 > 0.0) {
      recvMbpsX1000 = static_cast<uint32_t>(
          std::min<double>(mbpsX1000, static_cast<double>(0xFFFFFFFFu)));
    }
    gMetrics.client.width = metricW;
    gMetrics.client.height = metricH;
    gMetrics.client.recvFpsX100 = static_cast<uint32_t>(cappedRecvFpsX100);
    gMetrics.client.decodedFpsX100 = static_cast<uint32_t>(cappedDecodedFpsX100);
    gMetrics.client.recvMbpsX1000 = recvMbpsX1000;
    gMetrics.client.skippedFrames = static_cast<uint32_t>(std::min<uint64_t>(st.skippedQueued, 0xFFFFFFFFULL));
    gMetrics.client.avgLatencyUs = avgLatencyUs;
    gMetrics.client.maxLatencyUs = maxLatencyUsLocal;
    gMetrics.client.avgDecodeTailUs = avgDecodeTailUs;
    gMetrics.client.maxDecodeTailUs = maxDecodeTailUsLocal;
    gMetrics.client.congestionState = static_cast<uint32_t>(gate.congestionState);
    gMetrics.client.congestionTransitions =
        static_cast<uint32_t>(std::min<uint64_t>(gate.congestionTransitionCount, 0xFFFFFFFFULL));
    gMetrics.client.congestionRecoveryCount =
        static_cast<uint32_t>(std::min<uint64_t>(gate.congestionRecoveryCount, 0xFFFFFFFFULL));
    gMetrics.client.congestionRecoveryReq =
        static_cast<uint32_t>(std::min<uint64_t>(gate.congestionRecoveryRequestCount, 0xFFFFFFFFULL));
    gMetrics.client.congestionRecoveryMaxUs =
        static_cast<uint32_t>(std::min<uint64_t>(gate.congestionRecoveryMaxUs, 0xFFFFFFFFULL));
    gMetrics.client.queueDepthMax = st.queueDepthFramesMax;
    gMetrics.client.queueDepthH4p =
        static_cast<uint32_t>(std::min<uint64_t>(st.queueDepthHist[4], 0xFFFFFFFFULL));
    gMetrics.client.udpAssemblyDropPm = st.udpAssemblyDropPmLast;
    gMetrics.client.seq.fetch_add(1);
    gMetrics.client.updatedQpcUs = nowUs;
    push_overlay_metric_sample(gMetrics.client.recvFpsX100.load(std::memory_order_relaxed),
                               gMetrics.client.decodedFpsX100.load(std::memory_order_relaxed),
                               gMetrics.client.recvMbpsX1000.load(std::memory_order_relaxed),
                               gMetrics.client.avgLatencyUs.load(std::memory_order_relaxed),
                               nowUs);
    if (gSession.hwnd && !gPicker.visible.load(std::memory_order_relaxed)) {
      if (!gFrameBuf.paintQueued.exchange(true)) {
        InvalidateRect(gSession.hwnd, nullptr, FALSE);
      } else {
        ++gFrameBuf.paintCoalescedCount;
      }
    }
}

// The once-a-second stats line + metrics publish, formerly copied at five early-return sites of
// process_h264_frame and the raw path (F-08). `divideByRecvFrames` keeps the raw path's average
// divisor (recvFrames) apart from the H.264 path's (decodedFrames) -- F-02; `codedSize` adds the
// post-decode copy's " codedSize=" field. Output is byte-identical to the five copies.
void VideoReceiver::flush_stats_if_due(uint64_t nowUs, uint32_t w, uint32_t h, bool codedSize,
                                       uint32_t codedW, uint32_t codedH, bool divideByRecvFrames) {
  if (nowUs >= st.statAtUs) {
    const uint64_t frames = divideByRecvFrames ? st.recvFrames : st.decodedFrames;
    const uint64_t avgLatencyUs = (frames > 0) ? (st.sumLatencyUs / frames) : 0;
    const uint64_t avgDecodeTailUs = (frames > 0) ? (st.sumDecodeTailUs / frames) : 0;
    const double mbps = (st.recvBytes * 8.0) / (1000.0 * 1000.0);
    const double decodedRawMbps = (st.decodedBytes * 8.0) / (1000.0 * 1000.0);
    const uint64_t decodeRatioX100 =
        (st.recvBytes > 0) ? ((st.decodedBytes * 100ULL) / st.recvBytes) : 0;
    publish_metrics(w, h, nowUs,
                    avgLatencyUs, st.maxLatencyUs, avgDecodeTailUs, st.maxDecodeTailUs, mbps);
    std::ostringstream oss;
    oss << "[native-video-client] recvFrames=" << st.recvFrames
        << " decodedFrames=" << st.decodedFrames
        << " skippedQueued=" << st.skippedQueued
        << " avgLatencyUs=" << avgLatencyUs
        << " maxLatencyUs=" << st.maxLatencyUs
        << " avgDecodeTailUs=" << avgDecodeTailUs
        << " maxDecodeTailUs=" << st.maxDecodeTailUs
        << " mbps=" << mbps
        << " decodedRawMbps=" << decodedRawMbps
        << " decodeRatioX100=" << decodeRatioX100
        << " size=" << w << "x" << h;
    if (codedSize) oss << " codedSize=" << codedW << "x" << codedH;
    fg.append_congestion_fields(oss);
    append_present_counter_fields(oss);
    log_client_line(oss.str());
    st.recvFrames = 0;
    st.decodedFrames = 0;
    st.skippedQueued = 0;
    st.recvBytes = 0;
    st.decodedBytes = 0;
    st.sumLatencyUs = 0;
    st.maxLatencyUs = 0;
    st.sumDecodeTailUs = 0;
    st.maxDecodeTailUs = 0;
    st.statAtUs += 1000000ULL;
  }
}


bool VideoReceiver::process_h264_frame(const EncodedFrameHeader& h, std::vector<uint8_t>* payloadPtr,
    uint64_t packetNowUs) {
    if (!payloadPtr) return true;
    ++st.recvFrames;
    st.recvBytes += h.payloadSize;
    const uint64_t recvGapUs = fg.note_packet(packetNowUs);

    if (!dec.useH264) {
      ++st.skippedQueued;
      return true;
    }

    // Target-selection gate (mobile parity, Android commit 4892dea). While the user's pick is
    // resolving, keep the picker up and present nothing until the acknowledged generation's
    // first frame decodes.
    if (gSel.epoch.load(std::memory_order_acquire) != dec.recvSelectionEpoch) {
      // A fresh pick: drop stale reference frames and hold for the new generation's keyframe.
      dec.recvSelectionEpoch = gSel.epoch.load(std::memory_order_acquire);
      dec.decoder.reset();
      gate.waitForKeyFrame = true;
    }
    if (gSel.pending.load(std::memory_order_acquire)) {
      if (gSel.awaitingAck.load(std::memory_order_acquire)) {
        // No ack yet: every frame here is either the old target or an unconfirmed guess.
        ++st.skippedQueued;
        return true;
      }
      const uint64_t expectedGen = gSel.expectedGeneration.load(std::memory_order_acquire);
      if (expectedGen != 0 && h.streamGeneration != expectedGen) {
        // The previous target's stream still draining after the ack; not what we selected.
        ++st.skippedQueued;
        return true;
      }
    } else {
      // No selection in flight. After a reveal, only the active target's generation is welcome:
      // a late straggler from the previously selected target, still in flight on the wire, would
      // otherwise flash on screen. gSel.activeStreamGeneration==0 means no PC-side selection has
      // taken effect (legacy stream-view start, or before the first pick), so accept anything as
      // before. Host auto-resolution changes keep the same generation, so this does not fight
      // them -- only a host-side target selection bumps the generation.
      const uint64_t activeGen = gSel.activeStreamGeneration.load(std::memory_order_acquire);
      if (activeGen != 0 && h.streamGeneration != activeGen) {
        ++st.skippedQueued;
        return true;
      }
    }

    if (!dec.decoderReady || dec.decoderW != h.width || dec.decoderH != h.height) {
      if (!dec.decoder.initialize(h.width, h.height, args.fpsHint)) {
        std::cerr << "[native-video-client] H264 decoder initialize failed size=" << h.width << "x" << h.height
                  << "\n";
        return false;
      }
  const std::string requestedDecoderBackend = env_string_or_empty("REMOTE60_NATIVE_DECODER_BACKEND");
  const std::string requestedDecoderBackendPrint =
      requestedDecoderBackend.empty() ? "default(mft_auto)" : requestedDecoderBackend;
      const std::string backendFallbackReason =
          backend_fallback_reason(requestedDecoderBackend, dec.decoder.backend_name());
      std::cout << "[native-video-client] H264 decoder backend=" << dec.decoder.backend_name()
                << " backendRequested=" << requestedDecoderBackendPrint
                << " backendResolved=" << dec.decoder.backend_name()
                << " backendFallbackReason=" << backendFallbackReason
                << " hw=" << (dec.decoder.using_hardware() ? 1 : 0)
                << " size=" << h.width << "x" << h.height << "\n";
      dec.decoderReady = true;
      dec.decoderW = h.width;
      dec.decoderH = h.height;
      gate.waitForKeyFrame = true;
    }

    const bool keyFrame = ((h.flags & 1u) != 0);
    FrameGateInputs in{};
    in.captureQpcUs = h.captureQpcUs;
    in.seq = h.seq;
    in.keyFrame = keyFrame;
    in.packetNowUs = packetNowUs;
    in.recvGapUs = recvGapUs;
    in.presentedCapUs = gFrameBuf.lastPresentedCaptureUs.load(std::memory_order_relaxed);
    // The picker overlay pauses presents on purpose; lag measured against a frozen present
    // anchor is not congestion. Same for the short post-close grace until the anchor is fresh.
    in.catchupSuppressed =
        gPicker.visible.load(std::memory_order_relaxed) ||
        packetNowUs < gFrameBuf.catchupSuppressUntilUs.load(std::memory_order_relaxed);
    FrameGateLag lag{};
    switch (fg.admit(in, &lag)) {
      case FrameGateVerdict::DropStale:
      case FrameGateVerdict::DropCongested:
        return true;
      case FrameGateVerdict::DropWaitingKeyframe:
        flush_stats_if_due(packetNowUs, h.width, h.height, false, 0, 0, false);
        return true;
      case FrameGateVerdict::Decode:
        break;
    }

    const uint64_t decodeStartUs = qpc_now_us();
    std::vector<DecodedFrameNv12> outFrames;
    const int64_t inputSampleTimeHns = static_cast<int64_t>(h.captureQpcUs) * 10;
    bool pendingTimestampOverflow = false;
    if (!dec.decoder.decode_access_unit(*payloadPtr, keyFrame, inputSampleTimeHns, &outFrames,
                                    &pendingTimestampOverflow)) {
      fg.note_decode_failure(in, lag);
      flush_stats_if_due(packetNowUs, h.width, h.height, false, 0, 0, false);
      return true;
    }
    fg.note_decode_ok();
    if (pendingTimestampOverflow) {
      fg.note_timestamp_overflow(in, lag);
      return true;
    }
    fg.note_reference_sync(in);
    if (outFrames.empty()) {
      fg.note_decode_empty(in, lag);
      flush_stats_if_due(packetNowUs, h.width, h.height, false, 0, 0, false);
      return true;
    }
    fg.clear_empty_streak();

    auto& decoded = outFrames.back();
    const bool tsFromMft = decoded.sampleTimeFromOutput && (decoded.sampleTimeHns > 0);
    const bool tsFromInputFallback = (!decoded.sampleTimeFromOutput) && (decoded.sampleTimeHns > 0);
    const bool tsFromHeaderFallback = (decoded.sampleTimeHns <= 0);
    const uint64_t decodedCaptureUs =
        tsFromHeaderFallback ? h.captureQpcUs : static_cast<uint64_t>(decoded.sampleTimeHns / 10);
    const char* tsSource = tsFromMft ? "mft" : (tsFromInputFallback ? "input_fallback" : "header_fallback");
    if (decoded.bytes.empty() && !decoded.surfaceTexture) {
      ++st.skippedQueued;
      gate.waitForKeyFrame = true;
      return true;
    }
    const uint64_t decodedPayloadBytes = decoded.bytes.empty()
        ? (static_cast<uint64_t>(decoded.width) * decoded.height * 3 / 2)
        : static_cast<uint64_t>(decoded.bytes.size());
    const uint64_t decodeEndUs = qpc_now_us();
    std::shared_ptr<std::vector<uint8_t>> frameNv12;
    if (!decoded.bytes.empty()) {
      frameNv12 = std::make_shared<std::vector<uint8_t>>(std::move(decoded.bytes));
      if (!frameNv12 || frameNv12->empty()) {
        ++st.skippedQueued;
        gate.waitForKeyFrame = true;
        return true;
      }
    }

    const uint64_t nowUs = qpc_now_us();
    const uint64_t queueSetUs = nowUs;
    const uint64_t decodeToQueueUs = (queueSetUs >= decodeEndUs) ? (queueSetUs - decodeEndUs) : 0;
    {
      std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
      const uint64_t prevVersion = gFrameBuf.frame.version;
      const uint64_t lastPresentedVersion = gFrameBuf.lastPresentedVersion.load(std::memory_order_relaxed);
      // Overwrites while the picker covers the stream are the intended latest-wins behavior of a
      // deliberately paused present, not a symptom -- keep them out of the telemetry.
      if (prevVersion > lastPresentedVersion &&
          !gPicker.visible.load(std::memory_order_relaxed)) {
        ++gFrameBuf.overwriteBeforePresentCount;
      }
      gFrameBuf.frame.format = SharedFrame::PixelFormat::Nv12;
      gFrameBuf.frame.width = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
      gFrameBuf.frame.height = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
      gFrameBuf.frame.codedWidth = decoded.width;
      gFrameBuf.frame.codedHeight = decoded.height;
      gFrameBuf.frame.visibleLeft = decoded.visibleLeft;
      gFrameBuf.frame.visibleTop = decoded.visibleTop;
      gFrameBuf.frame.stride = decoded.width;
      gFrameBuf.frame.seq = h.seq;
      gFrameBuf.frame.captureUs = decodedCaptureUs;
      gFrameBuf.frame.encodeStartUs = h.encodeStartQpcUs;
      gFrameBuf.frame.encodeEndUs = h.encodeEndQpcUs;
      gFrameBuf.frame.sendUs = h.sendQpcUs;
      gFrameBuf.frame.recvUs = packetNowUs;
      gFrameBuf.frame.decodeStartUs = decodeStartUs;
      gFrameBuf.frame.decodeEndUs = decodeEndUs;
      gFrameBuf.frame.queueSetUs = queueSetUs;
      gFrameBuf.frame.decodeToQueueUs = decodeToQueueUs;
      gFrameBuf.frame.streamGeneration = h.streamGeneration;
      gFrameBuf.frame.key = keyFrame;
      gFrameBuf.frame.version = prevVersion + 1;
      gFrameBuf.frame.bytes = std::move(frameNv12);
      gFrameBuf.frame.surfaceSample = std::move(decoded.surfaceSample);
      gFrameBuf.frame.surfaceTexture = std::move(decoded.surfaceTexture);
      gFrameBuf.frame.surfaceSubresource = decoded.surfaceSubresource;
    }
    // First real frame of the acknowledged selection just landed. The gate above guarantees it
    // belongs to the selected generation; record the candidate and post the reveal once. The
    // picker flip, input guard and toolbar are committed on the UI thread (after revalidation),
    // not here, so a racing cancel/new-selection/disconnect cannot wrongly close the picker.
    if (gSel.pending.load(std::memory_order_acquire) &&
        !gSel.awaitingAck.load(std::memory_order_acquire)) {
      post_pc_selection_reveal(h.streamGeneration,
                               gSel.epoch.load(std::memory_order_acquire));
    }
    // While the picker overlays a live stream, WM_PAINT redraws the picker (not the video), so
    // a per-frame invalidate would repaint the whole card grid at video cadence for nothing.
    // The reveal above and the picker-close handler invalidate on their own, so the newest
    // decoded frame still shows the moment the picker leaves.
    if (gSession.hwnd && !gPicker.visible.load(std::memory_order_relaxed)) {
      if (!gFrameBuf.paintQueued.exchange(true)) {
        InvalidateRect(gSession.hwnd, nullptr, FALSE);
      } else {
        ++gFrameBuf.paintCoalescedCount;
      }
    }

    if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
        (args.traceMax == 0 || gPresent.traceRecvPrinted.load() < args.traceMax)) {
      const auto nowPrinted = gPresent.traceRecvPrinted.fetch_add(1) + 1;
      if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
        std::ostringstream oss;
        oss << "[native-video-client][trace_recv] seq=" << h.seq
            << " captureUs=" << decodedCaptureUs
            << " hdrCaptureUs=" << h.captureQpcUs
            << " encodeStartUs=" << h.encodeStartQpcUs
            << " encodeEndUs=" << h.encodeEndQpcUs
            << " sendUs=" << h.sendQpcUs
            << " recvUs=" << packetNowUs
            << " decodeStartUs=" << decodeStartUs
            << " decodeEndUs=" << decodeEndUs
            << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
            << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
            << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
            << " netUs=" << ((packetNowUs >= h.sendQpcUs) ? (packetNowUs - h.sendQpcUs) : 0)
            << " r2dUs=" << ((decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0)
            << " decUs=" << ((decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0)
            << " decodeQueueLagUs=" << ((h.captureQpcUs >= decodedCaptureUs) ? (h.captureQpcUs - decodedCaptureUs) : 0)
            << " tsSource=" << tsSource
            << " bytes=" << h.payloadSize
            << " key=" << (keyFrame ? 1 : 0);
        log_client_line(oss.str());
      }
    }

    // GNLink stream telemetry (diagnostics only): one line per decoded keyframe, plus any
    // non-key frame whose decode cost ran past 1.5x the expected frame interval. Joins the host
    // 'wire seq=' log by seq+gen. decodeQueueLagUs is the capture-lag estimate already computed
    // for scheduling (not a literal decoder input-queue count -- the MFT does not expose one).
    {
      const uint64_t decodeUs = (decodeEndUs >= decodeStartUs) ? (decodeEndUs - decodeStartUs) : 0;
      const uint64_t decodeAnomalyUs = (gate.frameIntervalUs * 3ULL) / 2ULL;
      if (keyFrame || decodeUs > decodeAnomalyUs) {
        const uint64_t r2dUs = (decodeStartUs >= packetNowUs) ? (decodeStartUs - packetNowUs) : 0;
        std::ostringstream telem;
        telem << "[native-video-client][telemetry] stage=decode"
              << " seq=" << h.seq
              << " gen=" << h.streamGeneration
              << " key=" << (keyFrame ? 1 : 0)
              << " recvUs=" << packetNowUs
              << " decodeStartUs=" << decodeStartUs
              << " decodeEndUs=" << decodeEndUs
              << " decodeUs=" << decodeUs
              << " r2dUs=" << r2dUs
              << " decodeQueueLagUs=" << lag.decodeQueueLagEstimateUs
              << " pts=" << decodedCaptureUs;
        log_client_line(telem.str());
      }
    }

    ++st.decodedFrames;
    st.decodedBytes += decodedPayloadBytes;
    // lastPresentedCaptureUs is now updated by render thread via gFrameBuf.lastPresentedCaptureUs
    const uint64_t latencyUs = FrameGate::aligned_lag_us(
        decodedCaptureUs, nowUs, gate.captureTimelineReady, gate.captureRemoteBaseUs, gate.captureLocalBaseUs);
    const uint64_t decodeTailUs = FrameGate::aligned_lag_us(
        h.sendQpcUs, nowUs, gate.sendTimelineReady, gate.sendRemoteBaseUs, gate.sendLocalBaseUs);
    st.sumLatencyUs += latencyUs;
    st.sumDecodeTailUs += decodeTailUs;
    st.maxLatencyUs = std::max(st.maxLatencyUs, latencyUs);
    st.maxDecodeTailUs = std::max(st.maxDecodeTailUs, decodeTailUs);

    const uint32_t visibleW = (decoded.visibleWidth > 0) ? decoded.visibleWidth : decoded.width;
    const uint32_t visibleH = (decoded.visibleHeight > 0) ? decoded.visibleHeight : decoded.height;
    flush_stats_if_due(nowUs, visibleW, visibleH, true, decoded.width, decoded.height, false);
    return true;
}

void VideoReceiver::run_udp() {
  std::array<uint8_t, 1600> datagram{};
  const uint32_t effectiveUdpSimDropSeed = (udpSimDropSeed > 0)
                                               ? udpSimDropSeed
                                               : static_cast<uint32_t>(qpc_now_us() & 0x7fffffffu);
  std::minstd_rand udpSimRng(effectiveUdpSimDropSeed);
  std::uniform_int_distribution<uint32_t> udpSimDropDist(0, 999);
  UdpH264FrameAssembler assembler;
  uint64_t assemblyDropped = 0;
  uint64_t oversizePayloadDropCount = 0;
  uint64_t udpSimDroppedCount = 0;
  uint64_t udpSimAcceptedCount = 0;
  uint64_t udpAssemblyStatAtUs = qpc_now_us() + 1000000ULL;
  uint64_t lastUdpChunkRecvCount = 0;
  uint64_t lastUdpAssemblyCompletedCount = 0;
  uint64_t lastUdpAssemblyDroppedCount = 0;
  uint64_t lastUdpAssemblyMalformedCount = 0;
  uint64_t lastUdpAssemblyReorderCount = 0;
  uint64_t lastUdpAssemblyKeyReqCount = 0;
  uint64_t lastUdpAssemblyFecRecoveredCount = 0;
  uint64_t lastUdpSimDroppedCount = 0;
  uint64_t lastUdpSimAcceptedCount = 0;

  while (gSession.running.load()) {
    const int n = recv(gSession.sock, reinterpret_cast<char*>(datagram.data()), static_cast<int>(datagram.size()), 0);
    if (n <= 0) {
      // A read timeout is not a dead socket. It is also the tunnel's heartbeat: the control
      // thread spends most of its time blocked waiting for a reply, so if retransmission
      // were driven from there it would stop exactly when a reply goes missing -- and the
      // host, hearing nothing, declares the client lost. This thread always runs.
      if (remote60::native_poc::last_socket_error_is_retryable()) {
        if (gControl.overUdp.load(std::memory_order_acquire)) gControl.udpControl.Tick();
        continue;
      }
      break;
    }
    if (gControl.overUdp.load(std::memory_order_acquire)) gControl.udpControl.Tick();
    // Control is offered the datagram BEFORE the video length guard, and the order is the
    // whole point. A control message is not bounded below by the video header: a
    // single-fragment input ack is 32 + 28 = 60 bytes against an 88-byte video header, so
    // checking the video size first silently ate every small reply -- input acks and window
    // selections -- while the larger ones (pong, window lists) came through and made the
    // channel look healthy. OnPacket claims only its own kinds, so video cannot be stolen.
    if (gControl.overUdp.load(std::memory_order_acquire) &&
        gControl.udpControl.OnPacket(datagram.data(), static_cast<size_t>(n))) {
      continue;
    }
    // Remote hardware-cursor sample: smaller than the video header, so it must be claimed
    // before the size guard below silently eats it. Latest-wins into atomics; the UI timer
    // does the mapping and drawing.
    if (n == static_cast<int>(sizeof(remote60::native_poc::UdpCursorPosPacket))) {
      remote60::native_poc::UdpCursorPosPacket cp{};
      std::memcpy(&cp, datagram.data(), sizeof(cp));
      if (cp.magic == remote60::native_poc::kMagic &&
          cp.kind == static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::CursorPos) &&
          cp.size == sizeof(cp)) {
        // Bounds sanity before the values reach mapping math: a malformed peer packet must
        // not be able to feed the clamp arithmetic absurd dimensions. Claimed either way.
        if (cp.captureW >= 2 && cp.captureW <= 16384 && cp.captureH >= 2 &&
            cp.captureH <= 16384) {
          gCursor.x.store(cp.x, std::memory_order_relaxed);
          gCursor.y.store(cp.y, std::memory_order_relaxed);
          gCursor.capW.store(cp.captureW, std::memory_order_relaxed);
          gCursor.capH.store(cp.captureH, std::memory_order_relaxed);
          gCursor.generation.store(cp.streamGeneration, std::memory_order_relaxed);
          gCursor.visible.store((cp.flags & 0x1u) != 0, std::memory_order_relaxed);
          gCursor.updateUs.store(qpc_now_us(), std::memory_order_release);
        }
        continue;
      }
    }
    if (n < static_cast<int>(sizeof(UdpVideoChunkHeader))) continue;

    UdpVideoChunkHeader u{};
    std::memcpy(&u, datagram.data(), sizeof(u));
    if (u.magic != remote60::native_poc::kMagic ||
        u.kind != static_cast<uint16_t>(UdpPacketKind::VideoChunk) ||
        u.size != sizeof(UdpVideoChunkHeader)) {
      continue;
    }
    if (u.codec != static_cast<uint16_t>(UdpCodec::H264)) {
      ++st.skippedQueued;
      continue;
    }
    if (udpSimDropPm > 0) {
      const uint32_t samplePm = udpSimDropDist(udpSimRng);
      if (samplePm < udpSimDropPm) {
        ++udpSimDroppedCount;
        ++st.skippedQueued;
        continue;
      }
    }
    ++udpSimAcceptedCount;
    ++st.udpChunkRecvCount;

    const auto assembleResult = assembler.PushDatagram(datagram.data(), static_cast<size_t>(n));
    if (assembleResult.fecRecovered) {
      st.udpAssemblyFecRecoveredCount += assembleResult.fecRecoveredChunks;
    }
    bool discontinuityHandled = false;
    auto handle_udp_discontinuity = [&]() {
      if (discontinuityHandled) return;
      discontinuityHandled = true;
      gate.waitForKeyFrame = true;
      dec.decoder.reset();
      request_keyframe(2);
      ++st.udpAssemblyKeyReqCount;
    };
    if (assembleResult.droppedPreviousIncomplete) {
      ++assemblyDropped;
      ++st.udpAssemblyDroppedCount;
      handle_udp_discontinuity();
    }

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Malformed) {
      ++st.skippedQueued;
      ++st.udpAssemblyMalformedCount;
      handle_udp_discontinuity();
      if (assembleResult.oversizePayload && ((++oversizePayloadDropCount % 30ULL) == 1ULL)) {
        std::cout << "[native-video-client] dropped oversized udp payload bytes="
                  << assembleResult.rejectedPayloadSize
                  << " count=" << oversizePayloadDropCount << "\n";
      }
      continue;
    }

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Dropped) {
      ++st.skippedQueued;
      ++assemblyDropped;
      ++st.udpAssemblyDroppedCount;
      if (assembleResult.reorderDetected) ++st.udpAssemblyReorderCount;
      handle_udp_discontinuity();
      if ((assemblyDropped % 120) == 1) {
        std::cout << "[native-video-client] udp assembly drop count=" << assemblyDropped
                  << " seq=" << u.seq
                  << " expectedSeq=" << assembleResult.expectedSeq
                  << " chunkOffset=" << u.chunkOffset
                  << " nextOffset=" << assembleResult.expectedNextOffset
                  << "\n";
      }
      continue;
    }

    if (assembleResult.disposition == UdpH264AssemblyDisposition::Completed) {
      ++st.udpAssemblyCompletedCount;
      const uint64_t packetNowUs = qpc_now_us();
      // GNLink stream telemetry (diagnostics only): one line per assembled keyframe, plus any
      // non-key frame that needed FEC repair or showed loss/reorder, so a periodic-stutter
      // session joins the host 'wire seq=' log by seq+gen while steady play stays quiet.
      {
        const auto& fh = assembleResult.frame.header;
        const bool key = ((fh.flags & 1u) != 0);
        if (key || assembleResult.fecRecovered || assembleResult.reorderDetected ||
            assembleResult.droppedPreviousIncomplete) {
          std::ostringstream telem;
          telem << "[native-video-client][telemetry] stage=assembly"
                << " seq=" << fh.seq
                << " gen=" << fh.streamGeneration
                << " key=" << (key ? 1 : 0)
                << " lastChunkRecvUs=" << packetNowUs
                << " bytes=" << fh.payloadSize
                << " fecRecovered=" << (assembleResult.fecRecovered ? 1 : 0)
                << " fecRecoveredChunks=" << assembleResult.fecRecoveredChunks
                << " reorder=" << (assembleResult.reorderDetected ? 1 : 0)
                << " droppedPrev=" << (assembleResult.droppedPreviousIncomplete ? 1 : 0);
          log_client_line(telem.str());
        }
      }
      auto payload = std::move(assembleResult.frame.payload);
      if (!process_h264_frame(assembleResult.frame.header, &payload, packetNowUs)) break;
    }

    const uint64_t nowUs = qpc_now_us();
    if (nowUs >= udpAssemblyStatAtUs) {
      const uint64_t chunksDelta = st.udpChunkRecvCount - lastUdpChunkRecvCount;
      const uint64_t completedDelta = st.udpAssemblyCompletedCount - lastUdpAssemblyCompletedCount;
      const uint64_t droppedDelta = st.udpAssemblyDroppedCount - lastUdpAssemblyDroppedCount;
      const uint64_t malformedDelta = st.udpAssemblyMalformedCount - lastUdpAssemblyMalformedCount;
      const uint64_t reorderDelta = st.udpAssemblyReorderCount - lastUdpAssemblyReorderCount;
      const uint64_t keyReqDelta = st.udpAssemblyKeyReqCount - lastUdpAssemblyKeyReqCount;
      const uint64_t fecRecoveredDelta =
          st.udpAssemblyFecRecoveredCount - lastUdpAssemblyFecRecoveredCount;
      const uint64_t simDroppedDelta = udpSimDroppedCount - lastUdpSimDroppedCount;
      const uint64_t simAcceptedDelta = udpSimAcceptedCount - lastUdpSimAcceptedCount;
      const uint64_t simTotalDelta = simDroppedDelta + simAcceptedDelta;
      const uint64_t simDropPermille = (simTotalDelta > 0)
          ? ((simDroppedDelta * 1000ULL) / simTotalDelta)
          : 0;
      const uint64_t totalFramesDelta = completedDelta + droppedDelta;
      const uint64_t dropPermille = (totalFramesDelta > 0)
          ? ((droppedDelta * 1000ULL) / totalFramesDelta)
          : 0;
      st.udpAssemblyDropPmLast = static_cast<uint32_t>(std::min<uint64_t>(dropPermille, 1000ULL));
      std::cout << "[native-video-client] udp-assembly chunks=" << chunksDelta
                << " completed=" << completedDelta
                << " dropped=" << droppedDelta
                << " dropPm=" << dropPermille
                << " malformed=" << malformedDelta
                << " reorder=" << reorderDelta
                << " keyReq=" << keyReqDelta
                << " fecRecovered=" << fecRecoveredDelta
                << " simDropPm=" << simDropPermille
                << " simDropTotal=" << simDroppedDelta
                << " waitForKey=" << (gate.waitForKeyFrame ? 1 : 0)
                << " catchup=" << (gate.catchupMode ? 1 : 0)
                << "\n";
      lastUdpChunkRecvCount = st.udpChunkRecvCount;
      lastUdpAssemblyCompletedCount = st.udpAssemblyCompletedCount;
      lastUdpAssemblyDroppedCount = st.udpAssemblyDroppedCount;
      lastUdpAssemblyMalformedCount = st.udpAssemblyMalformedCount;
      lastUdpAssemblyReorderCount = st.udpAssemblyReorderCount;
      lastUdpAssemblyKeyReqCount = st.udpAssemblyKeyReqCount;
      lastUdpAssemblyFecRecoveredCount = st.udpAssemblyFecRecoveredCount;
      lastUdpSimDroppedCount = udpSimDroppedCount;
      lastUdpSimAcceptedCount = udpSimAcceptedCount;
      udpAssemblyStatAtUs += 1000000ULL;
    }
    if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
      break;
    }
  }

  gSession.running = false;
  if (gSession.hwnd) PostMessageW(gSession.hwnd, WM_CLOSE, 0, 0);
  return;
}

void VideoReceiver::run_tcp() {
  while (gSession.running.load()) {
    MessageHeader header{};
    if (!remote60::native_poc::recv_all(gSession.sock, &header, sizeof(header))) break;
    if (header.magic != remote60::native_poc::kMagic || header.size < sizeof(header)) break;
    const auto msgType = static_cast<MessageType>(header.type);

    if (msgType == MessageType::RawFrameBgra && header.size == sizeof(RawFrameHeader)) {
      RawFrameHeader h{};
      h.header = header;
      if (!remote60::native_poc::recv_all(gSession.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
      std::vector<uint8_t> payload(h.payloadSize);
      if (!remote60::native_poc::recv_all(gSession.sock, payload.data(), payload.size())) break;

      if (!dec.useRaw) {
        ++st.skippedQueued;
        continue;
      }

      const uint64_t nowUs = qpc_now_us();
      const uint64_t queueSetUs = nowUs;
      auto frameBgra = std::make_shared<std::vector<uint8_t>>(std::move(payload));
      if (!frameBgra || frameBgra->empty()) {
        ++st.skippedQueued;
        continue;
      }
      {
        std::lock_guard<std::mutex> lk(gFrameBuf.frame.mu);
        const uint64_t prevVersion = gFrameBuf.frame.version;
        const uint64_t lastPresentedVersion = gFrameBuf.lastPresentedVersion.load(std::memory_order_relaxed);
        if (prevVersion > lastPresentedVersion) {
          ++gFrameBuf.overwriteBeforePresentCount;
        }
        gFrameBuf.frame.format = SharedFrame::PixelFormat::Bgra32;
        gFrameBuf.frame.width = h.width;
        gFrameBuf.frame.height = h.height;
        gFrameBuf.frame.codedWidth = h.width;
        gFrameBuf.frame.codedHeight = h.height;
        gFrameBuf.frame.visibleLeft = 0;
        gFrameBuf.frame.visibleTop = 0;
        gFrameBuf.frame.stride = h.stride;
        gFrameBuf.frame.seq = h.seq;
        gFrameBuf.frame.captureUs = h.captureQpcUs;
        gFrameBuf.frame.encodeStartUs = h.encodeStartQpcUs;
        gFrameBuf.frame.encodeEndUs = h.encodeEndQpcUs;
        gFrameBuf.frame.sendUs = h.sendQpcUs;
        gFrameBuf.frame.recvUs = nowUs;
        gFrameBuf.frame.decodeStartUs = nowUs;
        gFrameBuf.frame.decodeEndUs = nowUs;
        gFrameBuf.frame.queueSetUs = queueSetUs;
        gFrameBuf.frame.decodeToQueueUs = 0;
        gFrameBuf.frame.streamGeneration = h.streamGeneration;
        gFrameBuf.frame.key = false;  // raw BGRA has no keyframe concept; keeps present telemetry quiet.
        gFrameBuf.frame.version = prevVersion + 1;
        gFrameBuf.frame.bytes = std::move(frameBgra);
        gFrameBuf.frame.surfaceSample.Reset();
        gFrameBuf.frame.surfaceTexture.Reset();
        gFrameBuf.frame.surfaceSubresource = 0;
      }
      if (gSession.hwnd) {
        if (!gFrameBuf.paintQueued.exchange(true)) {
          InvalidateRect(gSession.hwnd, nullptr, FALSE);
        } else {
          ++gFrameBuf.paintCoalescedCount;
        }
      }

      if (args.traceEvery > 0 && (h.seq % args.traceEvery) == 0 &&
          (args.traceMax == 0 || gPresent.traceRecvPrinted.load() < args.traceMax)) {
        const auto nowPrinted = gPresent.traceRecvPrinted.fetch_add(1) + 1;
        if (args.traceMax == 0 || nowPrinted <= args.traceMax) {
          std::ostringstream oss;
          oss << "[native-video-client][trace_recv] seq=" << h.seq
              << " captureUs=" << h.captureQpcUs
              << " encodeStartUs=" << h.encodeStartQpcUs
              << " encodeEndUs=" << h.encodeEndQpcUs
              << " sendUs=" << h.sendQpcUs
              << " recvUs=" << nowUs
              << " decodeStartUs=" << nowUs
              << " decodeEndUs=" << nowUs
              << " c2eUs=" << ((h.encodeStartQpcUs >= h.captureQpcUs) ? (h.encodeStartQpcUs - h.captureQpcUs) : 0)
              << " encUs=" << ((h.encodeEndQpcUs >= h.encodeStartQpcUs) ? (h.encodeEndQpcUs - h.encodeStartQpcUs) : 0)
              << " e2sUs=" << ((h.sendQpcUs >= h.encodeEndQpcUs) ? (h.sendQpcUs - h.encodeEndQpcUs) : 0)
              << " netUs=" << ((nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0)
              << " r2dUs=0"
              << " decUs=0"
              << " bytes=" << h.payloadSize;
          log_client_line(oss.str());
        }
      }

      ++st.recvFrames;
      ++st.decodedFrames;
      st.recvBytes += h.payloadSize;
      st.decodedBytes += static_cast<uint64_t>(h.payloadSize);
      const uint64_t latencyUs = (nowUs >= h.captureQpcUs) ? (nowUs - h.captureQpcUs) : 0;
      const uint64_t decodeTailUs = (nowUs >= h.sendQpcUs) ? (nowUs - h.sendQpcUs) : 0;
      st.sumLatencyUs += latencyUs;
      st.sumDecodeTailUs += decodeTailUs;
      st.maxLatencyUs = std::max(st.maxLatencyUs, latencyUs);
      st.maxDecodeTailUs = std::max(st.maxDecodeTailUs, decodeTailUs);

      flush_stats_if_due(nowUs, h.width, h.height, false, 0, 0, true);
    } else if (msgType == MessageType::EncodedFrameH264 && header.size == sizeof(EncodedFrameHeader)) {
      EncodedFrameHeader h{};
      h.header = header;
      if (!remote60::native_poc::recv_all(gSession.sock, &h.seq, sizeof(h) - sizeof(MessageHeader))) break;
      std::vector<uint8_t> payload(h.payloadSize);
      if (!remote60::native_poc::recv_all(gSession.sock, payload.data(), payload.size())) break;
      const uint64_t packetNowUs = qpc_now_us();
      if (!process_h264_frame(h, &payload, packetNowUs)) break;
    } else {
      const size_t bodySize = static_cast<size_t>(header.size - sizeof(header));
      if (bodySize > 0 && !remote60::native_poc::recv_discard(gSession.sock, bodySize)) break;
      ++st.skippedQueued;
    }

    const uint64_t nowUs = qpc_now_us();
    if (args.seconds > 0 && nowUs >= startUs + static_cast<uint64_t>(args.seconds) * 1000000ULL) {
      break;
    }
  }
  gSession.running = false;
  if (gSession.hwnd) PostMessageW(gSession.hwnd, WM_CLOSE, 0, 0);
}

void VideoReceiver::Run() {
  dec.recvSelectionEpoch = gSel.epoch.load(std::memory_order_acquire);
  st.statAtUs = qpc_now_us() + 1000000ULL;
  gate.frameIntervalUs = std::max<uint64_t>(
      1ULL, 1000000ULL / static_cast<uint64_t>(std::max<uint32_t>(1, args.fpsHint)));
  st.lastPresentCounters = load_present_counters();
  if (dec.transport == VideoTransport::Udp) {
    run_udp();
    return;
  }
  run_tcp();
}

}  // namespace remote60::native_poc::viewer
