// See viewer_frame_gate.hpp. Bodies are the gating blocks of VideoReceiver::process_h264_frame
// (native_video_client_main.cpp -> viewer_video_receiver.cpp), verbatim apart from the documented
// substitutions (viewer split refactor Phase 2-2).

#include "viewer_frame_gate.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

#include "viewer_constants.hpp"

namespace remote60::native_poc::viewer {

uint32_t FrameGate::queue_depth_frames(uint64_t lagUs) {
    if (lagUs == 0) return 0;
    const uint64_t depth64 = (lagUs + gate.frameIntervalUs - 1) / gate.frameIntervalUs;
    return static_cast<uint32_t>(std::min<uint64_t>(depth64, 1000ULL));
}

void FrameGate::sample_queue_depth(uint64_t lagUs) {
    const uint32_t depthFrames = queue_depth_frames(lagUs);
    ++st.queueDepthSampleCount;
    if (depthFrames > st.queueDepthFramesMax) st.queueDepthFramesMax = depthFrames;
    if (depthFrames == 0) {
      ++st.queueDepthHist[0];
    } else if (depthFrames == 1) {
      ++st.queueDepthHist[1];
    } else if (depthFrames == 2) {
      ++st.queueDepthHist[2];
    } else if (depthFrames == 3) {
      ++st.queueDepthHist[3];
    } else {
      ++st.queueDepthHist[4];
    }
}

void FrameGate::transition_congestion_state(ClientCongestionState nextState, uint64_t nowUs, const char* reason,
    uint64_t streamLagUs, uint64_t decodeQueueLagEstimateUs, uint32_t seq) {
    if (nextState == gate.congestionState) return;
    const ClientCongestionState prev = gate.congestionState;
    if (prev != ClientCongestionState::Normal &&
        nextState == ClientCongestionState::Normal &&
        gate.congestionStateEnterUs > 0 &&
        nowUs >= gate.congestionStateEnterUs) {
      const uint64_t recoverUs = nowUs - gate.congestionStateEnterUs;
      ++gate.congestionRecoveryCount;
      gate.congestionRecoveryTotalUs += recoverUs;
      if (recoverUs > gate.congestionRecoveryMaxUs) gate.congestionRecoveryMaxUs = recoverUs;
    }
    gate.congestionState = nextState;
    gate.congestionStateEnterUs = (nextState == ClientCongestionState::Normal) ? 0 : nowUs;
    if (nextState == ClientCongestionState::Recovering) {
      gate.recoveringSinceUs = nowUs;
      gate.recoveringHealthyStreak = 0;
    } else if (nextState != ClientCongestionState::Recovering) {
      gate.recoveringSinceUs = 0;
      gate.recoveringHealthyStreak = 0;
    }
    ++gate.congestionTransitionCount;
    std::cout << "[native-video-client][congestion] state=" << congestion_state_name(nextState)
              << " prev=" << congestion_state_name(prev)
              << " reason=" << reason
              << " streamLagUs=" << streamLagUs
              << " decodeQueueLagUs=" << decodeQueueLagEstimateUs
              << " seq=" << seq
              << "\n";
}

void FrameGate::append_congestion_fields(std::ostream& os) {
    const uint64_t recoveryAvgUs =
        (gate.congestionRecoveryCount > 0) ? (gate.congestionRecoveryTotalUs / gate.congestionRecoveryCount) : 0;
    os << " congestionState=" << congestion_state_name(gate.congestionState)
       << " congestionTransitions=" << gate.congestionTransitionCount
       << " congestionRecoveryCount=" << gate.congestionRecoveryCount
       << " congestionRecoveryAvgUs=" << recoveryAvgUs
       << " congestionRecoveryMaxUs=" << gate.congestionRecoveryMaxUs
       << " congestionRecoveryReq=" << gate.congestionRecoveryRequestCount
       << " staleDrops=" << gate.staleDropCount
       << " holdLatestDrops=" << gate.holdLatestDropCount
       << " burstDrops=" << gate.burstDropCount
       << " staleRefRecoveries=" << gate.staleReferenceRecoveryCount
       << " queueDepthSamples=" << st.queueDepthSampleCount
       << " queueDepthMax=" << st.queueDepthFramesMax
       << " queueDepthH0=" << st.queueDepthHist[0]
       << " queueDepthH1=" << st.queueDepthHist[1]
       << " queueDepthH2=" << st.queueDepthHist[2]
       << " queueDepthH3=" << st.queueDepthHist[3]
       << " queueDepthH4p=" << st.queueDepthHist[4];
}

uint64_t FrameGate::aligned_lag_us(uint64_t remoteTsUs, uint64_t localNowUs,
    bool& timelineReady, uint64_t& remoteBaseUs, uint64_t& localBaseUs) {
    if (!timelineReady || remoteTsUs < remoteBaseUs) {
      timelineReady = true;
      remoteBaseUs = remoteTsUs;
      localBaseUs = localNowUs;
      return 0;
    }
    const uint64_t remoteDeltaUs = remoteTsUs - remoteBaseUs;
    uint64_t expectedLocalUs = localBaseUs;
    if (std::numeric_limits<uint64_t>::max() - expectedLocalUs < remoteDeltaUs) {
      expectedLocalUs = std::numeric_limits<uint64_t>::max();
    } else {
      expectedLocalUs += remoteDeltaUs;
    }
    return (localNowUs >= expectedLocalUs) ? (localNowUs - expectedLocalUs) : 0;
}

uint64_t FrameGate::note_packet(uint64_t packetNowUs) {
  const uint64_t recvGapUs =
      (gate.lastPacketRecvUs > 0 && packetNowUs >= gate.lastPacketRecvUs) ? (packetNowUs - gate.lastPacketRecvUs) : 0;
  gate.lastPacketRecvUs = packetNowUs;
  if (recvGapUs > 250000) {
    // Sparse arrival usually means source/capture stall, not decoder backlog.
    gate.lagTriggerStreak = 0;
  }
  return recvGapUs;
}

FrameGateVerdict FrameGate::admit(const FrameGateInputs& in, FrameGateLag* lag) {
  if (in.captureQpcUs > gate.latestCaptureSeenUs) {
    gate.latestCaptureSeenUs = in.captureQpcUs;
  }
  const uint64_t streamLagUs = aligned_lag_us(
      in.captureQpcUs, in.packetNowUs, gate.captureTimelineReady, gate.captureRemoteBaseUs, gate.captureLocalBaseUs);
  // A large recvGap means the source was idle (a static screen produces no frames). Re-anchor the
  // decode-queue-lag floor to this resume frame so the seconds of idle are not misread as a decode
  // backlog. Sparse (typing) streams re-anchor every frame -> lag ~0 -> no false congestion; a dense
  // real backlog never re-anchors -> the floor stays old -> genuine catch-up still fires. (static
  // freeze root cause: idle time counted as decode lag.)
  // Only re-anchor while healthy. During Congested/Recovering the present anchor is intentionally
  // frozen (deltas are being dropped waiting for an IDR), and that lag is real -- re-anchoring there
  // would hide it and break the recovery-timeout re-request. So the floor (and its use) apply to the
  // congestion-ENTRY estimate in the Normal state only.
  const bool congestionHealthy = (gate.congestionState == ClientCongestionState::Normal);
  if (in.recvGapUs > 250000 && !in.synthetic && congestionHealthy &&
      in.captureQpcUs > gate.presentAnchorFloorUs) {
    gate.presentAnchorFloorUs = in.captureQpcUs;
  }
  const uint64_t effectivePresentedCapUs =
      congestionHealthy ? std::max(in.presentedCapUs, gate.presentAnchorFloorUs) : in.presentedCapUs;
  const uint64_t decodeQueueLagEstimateUs =
      (effectivePresentedCapUs > 0 && in.captureQpcUs >= effectivePresentedCapUs)
          ? (in.captureQpcUs - effectivePresentedCapUs)
          : 0;
  lag->streamLagUs = streamLagUs;
  lag->decodeQueueLagEstimateUs = decodeQueueLagEstimateUs;
  if (!in.synthetic) sample_queue_depth(decodeQueueLagEstimateUs);
  const uint64_t staleBehindPresentedUs =
      (in.presentedCapUs > 0 && in.presentedCapUs > in.captureQpcUs)
          ? (in.presentedCapUs - in.captureQpcUs)
          : 0;
  const uint64_t staleBehindLatestUs =
      (gate.latestCaptureSeenUs > in.captureQpcUs)
          ? (gate.latestCaptureSeenUs - in.captureQpcUs)
          : 0;
  // "Behind latest" means a newer capture is already queued, so this older frame can be skipped and
  // the newer one shown instead -- but that only holds when frames arrive DENSELY (a real backlog).
  // With a change-driven / low-fps source (typing on an otherwise static screen) frames legitimately
  // arrive 200-400ms apart, so every frame reads as >50ms "behind latest" even though it IS the
  // freshest live content and the client is not behind at all (decodeQueueLag ~= 0, presentBacklog
  // = 0). Treating it as stale drops it, resets the decoder and fires request_keyframe(6) every
  // frame -- which freezes the screen for seconds and triggers a full-IDR storm (field report:
  // "정적 화면에서 5초 넘게 멈춤"). Gate the latest-drop on dense arrival; sparse arrival is a slow
  // source, not congestion (same reasoning as note_packet's recvGap>250ms guard). staleBehindLatest
  // stays authoritative when frames really do pile up (dense), so genuine catch-up is unaffected.
  const bool denseArrival = (in.recvGapUs == 0 || in.recvGapUs <= gate.denseArrivalMaxGapUs);
  // Drop-as-stale ONLY on dense arrival. A sparse stream is a slow/idle source (typing on a static
  // screen), where the current frame is the freshest live content -- dropping it and requesting an
  // IDR just churns keyframes. A high-bitrate video the client has fallen behind on arrives DENSELY
  // (frames pile up) and is caught here; the idle re-anchor above already stops idle time from
  // faking a backlog. 0.2.92 also fired this on "sparse + decode-lag>50ms", but on a lossy static
  // screen a transient lag tripped it every few frames -> a 200KB-IDR keyframe storm (7 of 40
  // frames were keyframes) that itself dropped chunks and got slower. Reverted to dense-only. (0.2.93)
  const bool staleBehindLatest = (staleBehindLatestUs > gate.staleCaptureDropUs) && denseArrival;
  const bool staleBehindPresented = (staleBehindPresentedUs > gate.staleCaptureDropUs);
  if (staleBehindPresented || staleBehindLatest) {
    ++st.skippedQueued;
    ++gate.lagDropCount;
    ++gate.staleDropCount;
    if (staleBehindLatest) {
      ++gate.holdLatestDropCount;
    }
    // Dropping a frame that is NOT older than the last decoded keyframe breaks the still-live
    // reference chain. This is a B=0 low-latency IPPP stream and the wire header carries no
    // ref flag, so every such P must be treated as a reference: decoding later P-frames that
    // referenced the dropped one produces garbage (the corrupted text/scroll seen in the
    // field). Resync on the next IDR instead -- freeze on the last good frame until it lands.
    // A frame older than the anchor is a late/reordered straggler the decoder already resynced
    // past, so quiet-drop stays safe there. Recover once per gap; the wait gate below then
    // drops non-key frames until the IDR and request_keyframe's limiter throttles the ask.
    const bool inLiveReferenceChain = (in.captureQpcUs >= gate.lastDecodedKeyCaptureUs);
    if (inLiveReferenceChain && !gate.waitForKeyFrame) {
      gate.waitForKeyFrame = true;
      sink.reset_decoder();
      sink.request_keyframe(6);  // stale_reference_gap
      ++gate.congestionRecoveryRequestCount;
      ++gate.staleReferenceRecoveryCount;
      std::cout << "[native-video-client] stale-reference recovery seq=" << in.seq
                << " count=" << gate.staleReferenceRecoveryCount
                << " staleBehindLatestUs=" << staleBehindLatestUs << "\n";
    }
    if ((gate.lagDropCount % 120) == 1) {
      std::cout << "[native-video-client] stale frame drop count=" << gate.lagDropCount
                << " staleBehindPresentedUs=" << staleBehindPresentedUs
                << " staleBehindLatestUs=" << staleBehindLatestUs
                << " inRefChain=" << (inLiveReferenceChain ? 1 : 0)
                << " seq=" << in.seq << "\n";
    }
    return FrameGateVerdict::DropStale;
  }

  const bool lagTrigger =
      (decodeQueueLagEstimateUs > gate.decodeQueueLagDropUs) ||
      (in.presentedCapUs > 0 && streamLagUs > gate.catchupLagDropUs);
  // denseArrival computed above (reused by the stale-behind-latest gate).
  if (lagTrigger && denseArrival && !in.catchupSuppressed && !in.synthetic) {
    if (gate.lagTriggerStreak < std::numeric_limits<uint32_t>::max()) {
      ++gate.lagTriggerStreak;
    }
  } else {
    gate.lagTriggerStreak = 0;
  }
  if (gate.congestionState != ClientCongestionState::Congested &&
      gate.lagTriggerStreak >= gate.lagTriggerStreakMin) {
    gate.lagTriggerStreak = 0;
    const bool catchupEnterAllowed =
        (gate.lastCatchupEnterUs == 0) || (in.packetNowUs >= (gate.lastCatchupEnterUs + gate.catchupReenterMinIntervalUs));
    if (!catchupEnterAllowed) {
      ++gate.catchupEnterThrottledCount;
      if ((gate.catchupEnterThrottledCount % 120) == 1) {
        std::cout << "[native-video-client] catchup-enter-throttled count="
                  << gate.catchupEnterThrottledCount
                  << " streamLagUs=" << streamLagUs
                  << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                  << " minIntervalUs=" << gate.catchupReenterMinIntervalUs
                  << "\n";
      }
    } else {
      transition_congestion_state(ClientCongestionState::Congested, in.packetNowUs,
                                  (decodeQueueLagEstimateUs > gate.decodeQueueLagDropUs)
                                      ? "decode_queue"
                                      : "stream_lag_emergency",
                                  streamLagUs, decodeQueueLagEstimateUs, in.seq);
      gate.catchupMode = true;
      gate.lastCatchupEnterUs = in.packetNowUs;
      gate.waitForKeyFrame = true;
      sink.reset_decoder();
      sink.request_keyframe(1);
      ++gate.congestionRecoveryRequestCount;
      std::cout << "[native-video-client] catchup enter streamLagUs=" << streamLagUs
                << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                << " in.recvGapUs=" << in.recvGapUs
                << " reason="
                << ((decodeQueueLagEstimateUs > gate.decodeQueueLagDropUs) ? "decode_queue" : "stream_lag_emergency")
                << " seq=" << in.seq << "\n";
    }
  }
  if (gate.congestionState == ClientCongestionState::Congested && !in.keyFrame) {
    gate.decodeEmptyStreak = 0;
    gate.decodeEmptyStreakStartUs = 0;
    ++st.skippedQueued;
    ++gate.lagDropCount;
    ++gate.burstDropCount;
    if ((gate.lagDropCount % 120) == 1) {
      std::cout << "[native-video-client] catchup drops=" << gate.lagDropCount
                << " streamLagUs=" << streamLagUs
                << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
                << "\n";
    }
    return FrameGateVerdict::DropCongested;
  }
  if (gate.congestionState == ClientCongestionState::Congested && in.keyFrame) {
    gate.catchupMode = false;
    transition_congestion_state(ClientCongestionState::Recovering, in.packetNowUs, "keyframe",
                                streamLagUs, decodeQueueLagEstimateUs, in.seq);
    std::cout << "[native-video-client] catchup exit streamLagUs=" << streamLagUs
              << " decodeQueueLagEstUs=" << decodeQueueLagEstimateUs
              << " seq=" << in.seq << "\n";
  }
  if (gate.congestionState == ClientCongestionState::Recovering) {
    const bool lagHealthy =
        decodeQueueLagEstimateUs <= kDecodeQueueLagResumeUs &&
        streamLagUs <= kCatchupResumeKeyLagUs;
    if (lagHealthy) {
      if (gate.recoveringHealthyStreak < std::numeric_limits<uint32_t>::max()) {
        ++gate.recoveringHealthyStreak;
      }
    } else {
      gate.recoveringHealthyStreak = 0;
    }
    const bool recoverMinElapsed =
        gate.recoveringSinceUs > 0 && in.packetNowUs >= (gate.recoveringSinceUs + gate.congestionRecoverMinUs);
    if (lagHealthy && recoverMinElapsed && gate.recoveringHealthyStreak >= 3) {
      transition_congestion_state(ClientCongestionState::Normal, in.packetNowUs, "recover_stable",
                                  streamLagUs, decodeQueueLagEstimateUs, in.seq);
    } else if (!lagHealthy && !in.catchupSuppressed &&
               gate.recoveringSinceUs > 0 &&
               in.packetNowUs >= (gate.recoveringSinceUs + gate.congestionRecoveryTimeoutUs)) {
      const bool requestAllowed =
          (gate.lastRecoveryRequestUs == 0) || (in.packetNowUs >= (gate.lastRecoveryRequestUs + 300000));
      if (requestAllowed) {
        sink.request_keyframe(1);
        ++gate.congestionRecoveryRequestCount;
        gate.lastRecoveryRequestUs = in.packetNowUs;
      }
      gate.catchupMode = true;
      gate.waitForKeyFrame = true;
      sink.reset_decoder();
      gate.lastCatchupEnterUs = in.packetNowUs;
      transition_congestion_state(ClientCongestionState::Congested, in.packetNowUs, "recover_timeout",
                                  streamLagUs, decodeQueueLagEstimateUs, in.seq);
    }
  }

  if (gate.waitForKeyFrame && !in.keyFrame) {
    gate.decodeEmptyStreak = 0;
    gate.decodeEmptyStreakStartUs = 0;
    ++st.skippedQueued;
    ++gate.waitingKeyDropCount;
    ++gate.burstDropCount;
    if ((gate.waitingKeyDropCount % 30) == 1) {
      sink.request_keyframe(3);
    }
    if ((gate.waitingKeyDropCount % 120) == 1) {
      std::cout << "[native-video-client] waiting keyframe drops=" << gate.waitingKeyDropCount << "\n";
    }
    return FrameGateVerdict::DropWaitingKeyframe;
  }
  return FrameGateVerdict::Decode;
}

void FrameGate::note_decode_failure(const FrameGateInputs& in, const FrameGateLag& lag) {
  gate.decodeEmptyStreak = 0;
  gate.decodeEmptyStreakStartUs = 0;
  ++st.skippedQueued;
  ++st.decodeFailCount;
  sink.request_keyframe(4);
  ++gate.congestionRecoveryRequestCount;
  if ((st.decodeFailCount % 60) == 1) {
    std::cout << "[native-video-client] decode failed count=" << st.decodeFailCount << "\n";
  }
  gate.catchupMode = true;
  gate.lastCatchupEnterUs = in.packetNowUs;
  gate.waitForKeyFrame = true;
  if (++gate.decodeConsecutiveFailCount >= gate.kDecodeRebuildThreshold) {
    // Flush did not clear it: the transform or device is wedged. A full rebuild is the
    // only recovery, and it is what the resolution-change path already does -- reached
    // here without a resolution change so the wedge is not caught otherwise.
    std::cout << "[native-video-client] decoder wedged (consecutive fails="
              << gate.decodeConsecutiveFailCount << "); rebuilding\n";
    if (sink.rebuild_decoder()) {
      gate.decodeConsecutiveFailCount = 0;
    }
    // On rebuild failure, keep the streak so the next frame retries the rebuild.
  } else {
    sink.reset_decoder();
  }
  transition_congestion_state(ClientCongestionState::Congested, in.packetNowUs, "decode_fail",
                              lag.streamLagUs, lag.decodeQueueLagEstimateUs, in.seq);
}

// decode_access_unit succeeded: the transform is healthy, so the wedge streak is clear.
void FrameGate::note_decode_ok() {
  gate.decodeConsecutiveFailCount = 0;
}

void FrameGate::note_timestamp_overflow(const FrameGateInputs& in, const FrameGateLag& lag) {
  gate.decodeEmptyStreak = 0;
  gate.decodeEmptyStreakStartUs = 0;
  ++st.skippedQueued;
  ++st.decodeTimestampOverflowCount;
  sink.request_keyframe(4);
  ++gate.congestionRecoveryRequestCount;
  if ((st.decodeTimestampOverflowCount % 10ULL) == 1ULL) {
    std::cout << "[native-video-client] decoder timestamp queue overflow count="
              << st.decodeTimestampOverflowCount << "\n";
  }
  gate.catchupMode = true;
  gate.lastCatchupEnterUs = in.packetNowUs;
  gate.waitForKeyFrame = true;
  sink.reset_decoder();
  transition_congestion_state(ClientCongestionState::Congested, in.packetNowUs, "decode_ts_overflow",
                              lag.streamLagUs, lag.decodeQueueLagEstimateUs, in.seq);
}

void FrameGate::note_reference_sync(const FrameGateInputs& in) {
  gate.waitForKeyFrame = false;
  if (in.keyFrame) {
    // Advance the reference-chain anchor: a successfully decoded IDR resyncs the decoder, so
    // any later stale frame older than this is safe to quiet-drop.
    gate.lastDecodedKeyCaptureUs = in.captureQpcUs;
  }
}

void FrameGate::note_decode_empty(const FrameGateInputs& in, const FrameGateLag& lag) {
  ++st.decodeEmptyCount;
  ++gate.decodeEmptyStreak;
  if (gate.decodeEmptyStreak == 1) {
    gate.decodeEmptyStreakStartUs = in.packetNowUs;
  }
  const uint64_t emptyStreakUs =
      (gate.decodeEmptyStreakStartUs > 0 && in.packetNowUs >= gate.decodeEmptyStreakStartUs)
          ? (in.packetNowUs - gate.decodeEmptyStreakStartUs)
          : 0;
  if (gate.decodeEmptyStreak >= 12 || emptyStreakUs >= 300000) {
    const bool catchupEnterAllowed =
        (gate.lastCatchupEnterUs == 0) || (in.packetNowUs >= (gate.lastCatchupEnterUs + gate.catchupReenterMinIntervalUs));
    if (catchupEnterAllowed) {
      ++st.decodeEmptyRecoveryCount;
      gate.waitForKeyFrame = true;
      gate.catchupMode = true;
      gate.lastCatchupEnterUs = in.packetNowUs;
      sink.request_keyframe(5);
      ++gate.congestionRecoveryRequestCount;
      sink.reset_decoder();
      transition_congestion_state(ClientCongestionState::Congested, in.packetNowUs, "decode_empty",
                                  lag.streamLagUs, lag.decodeQueueLagEstimateUs, in.seq);
      if ((st.decodeEmptyRecoveryCount % 10) == 1) {
        std::cout << "[native-video-client] decode empty recovery count=" << st.decodeEmptyRecoveryCount
                  << " streak=" << gate.decodeEmptyStreak
                  << " emptyUs=" << emptyStreakUs
                  << "\n";
      }
    } else {
      ++gate.catchupEnterThrottledCount;
      if ((gate.catchupEnterThrottledCount % 120) == 1) {
        std::cout << "[native-video-client] decode-empty-recovery-throttled count="
                  << gate.catchupEnterThrottledCount
                  << " streak=" << gate.decodeEmptyStreak
                  << " emptyUs=" << emptyStreakUs
                  << " minIntervalUs=" << gate.catchupReenterMinIntervalUs
                  << "\n";
      }
    }
    gate.decodeEmptyStreak = 0;
    gate.decodeEmptyStreakStartUs = 0;
  }
  if ((st.decodeEmptyCount % 120) == 1) {
    std::cout << "[native-video-client] decode output empty count=" << st.decodeEmptyCount
              << " streak=" << gate.decodeEmptyStreak
              << " emptyUs=" << emptyStreakUs
              << "\n";
  }
}

void FrameGate::clear_empty_streak() {
  gate.decodeEmptyStreak = 0;
  gate.decodeEmptyStreakStartUs = 0;
}

}  // namespace remote60::native_poc::viewer
