// The per-frame stage of the viewer's recv thread: the decoder path of process_h264_frame (decoder
// init + backend log, decode, publish to the FrameBuffer, trace / telemetry) and the once-a-second
// stats line. Same class as viewer_video_receiver.cpp (the socket loops); split by stage so each file
// reads in one go (viewer split refactor Phase 2-3). Bodies verbatim.

#include "viewer_video_receiver.hpp"

#include <iostream>
#include <sstream>

#include "viewer_decoder_backend.hpp"
#include "viewer_env_util.hpp"
#include "viewer_log.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_picker.hpp"

namespace remote60::native_poc::viewer {

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

}  // namespace remote60::native_poc::viewer
