#pragma once

// The recv thread's frame-gating state: congestion state machine, stale-frame anchor, keyframe
// wait and the timelines the lag estimates are aligned on (Phase 1-12 state struct).
//
// Role:    the decision state process_h264_frame keeps between frames (Phase 2-2 turns it into
//          FrameGate::Decide with these fields as its memory), plus the env-tunable thresholds main()
//          reads at startup.
// Thread:  recv only; main() fills the config fields before the thread starts.
// Input:   per-frame timestamps and decode outcomes.
// Output:  drop / keyframe-request / decoder-reset decisions and their counters.
// Callers: recv thread (process_h264_frame, transition_congestion_state, aligned_lag_us).
//
// Fields are the former locals of the recvThread lambda and main()'s env-derived config values,
// initial values unchanged (viewer split refactor Phase 1-12); frameIntervalUs is assigned at thread
// start where the local was initialised.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

enum class ClientCongestionState : uint8_t {
  Normal = 0,
  Recovering = 1,
  Congested = 2,
};

inline const char* congestion_state_name(ClientCongestionState state) {
  switch (state) {
    case ClientCongestionState::Normal:
      return "normal";
    case ClientCongestionState::Recovering:
      return "recovering";
    case ClientCongestionState::Congested:
      return "congested";
    default:
      return "unknown";
  }
}

struct FrameGateState {
  // config: main() sets from env before the recv thread starts.
  uint64_t catchupReenterMinIntervalUs = 0;
  uint64_t staleCaptureDropUs = 0;
  uint64_t congestionRecoverMinUs = 0;
  uint64_t congestionRecoveryTimeoutUs = 0;
  // Congestion-entry thresholds (F-18). Formerly constants: the trigger is "decode-queue lag past
  // decodeQueueLagDropUs, or stream lag past catchupLagDropUs, on lagTriggerStreakMin consecutive
  // frames that arrived within denseArrivalMaxGapUs of each other". Local CPU contention alone can
  // satisfy that -- an e2e run next to a build tripped it once -- so the values are tunable from
  // the environment (REMOTE60_NATIVE_CONGEST_*) until field measurements settle them.
  uint64_t decodeQueueLagDropUs = 0;
  uint64_t catchupLagDropUs = 0;
  uint64_t denseArrivalMaxGapUs = 0;
  uint32_t lagTriggerStreakMin = 0;
  uint64_t frameIntervalUs = 0;  // from args.fpsHint, set at thread start
  // Hold non-key frames until the next IDR (set at startup to useH264, after every decoder reset).
  bool waitForKeyFrame = false;
  // Consecutive hard decode failures. A flush (decoder.reset) recovers a corrupt frame, but
  // not a wedged hardware MFT or a lost D3D device -- and the viewer's only recovery for a
  // same-resolution decode error was that flush, so once the decoder wedged (a YouTube scene
  // change on a busy GPU could do it) every following frame failed identically and the
  // picture froze until the app was restarted. Past a threshold, rebuild the decoder instead.
  static constexpr uint32_t kDecodeRebuildThreshold = 8;
  uint32_t decodeConsecutiveFailCount = 0;
  uint64_t decodeEmptyStreak = 0;
  uint64_t decodeEmptyStreakStartUs = 0;
  uint64_t waitingKeyDropCount = 0;
  uint64_t lagDropCount = 0;
  uint64_t lastPacketRecvUs = 0;
  uint32_t lagTriggerStreak = 0;
  uint64_t lastCatchupEnterUs = 0;
  uint64_t catchupEnterThrottledCount = 0;
  bool catchupMode = false;
  // lastPresentedCaptureUs is gFrameBuf.lastPresentedCaptureUs (atomic, updated after actual present)
  bool captureTimelineReady = false;
  uint64_t captureRemoteBaseUs = 0;
  uint64_t captureLocalBaseUs = 0;
  bool sendTimelineReady = false;
  uint64_t sendRemoteBaseUs = 0;
  uint64_t sendLocalBaseUs = 0;
  ClientCongestionState congestionState = ClientCongestionState::Normal;
  uint64_t congestionStateEnterUs = 0;
  uint64_t congestionTransitionCount = 0;
  uint64_t congestionRecoveryCount = 0;
  uint64_t congestionRecoveryTotalUs = 0;
  uint64_t congestionRecoveryMaxUs = 0;
  uint64_t congestionRecoveryRequestCount = 0;
  uint64_t staleDropCount = 0;
  uint64_t holdLatestDropCount = 0;
  uint64_t burstDropCount = 0;
  uint64_t staleReferenceRecoveryCount = 0;
  // Capture timestamp of the newest keyframe the decoder has successfully consumed. A stale
  // frame OLDER than this anchor was already resynced past (safe to quiet-drop); one AT OR
  // AFTER it still sits in the live reference chain, so dropping it needs an IDR resync.
  uint64_t lastDecodedKeyCaptureUs = 0;
  uint64_t latestCaptureSeenUs = 0;
  uint64_t recoveringSinceUs = 0;
  uint32_t recoveringHealthyStreak = 0;
  uint64_t lastRecoveryRequestUs = 0;
};

}  // namespace remote60::native_poc::viewer
