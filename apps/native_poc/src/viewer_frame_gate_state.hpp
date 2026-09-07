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
  // Two arrival clocks (history #390 item 4). lastPacketRecvUs is the heartbeat: every completed
  // frame, host kicks and static refreshes included. lastRealPacketRecvUs is the real-content
  // clock. The idle re-anchor and the dense-arrival test judge a REAL frame by the gap since the
  // last REAL frame: a kick 150 ms after a sparse capture used to make the next real burst look
  // dense (gap < 250 ms -> no re-anchor) while its capture sat 350-570 ms past the last presented
  // real frame -> "decode backlog" -> Congested + IDR, with streamLag ~0 and presentBacklog 0.
  uint64_t lastPacketRecvUs = 0;
  uint64_t lastRealPacketRecvUs = 0;
  uint32_t lagTriggerStreak = 0;
  uint64_t lastCatchupEnterUs = 0;
  uint64_t catchupEnterThrottledCount = 0;
  bool catchupMode = false;
  // lastPresentedCaptureUs is ctx.frameBuf.lastPresentedCaptureUs (atomic, updated after actual present)
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
  // Capture-time floor for the decode-queue-lag estimate. Re-anchored to the resume frame's capture
  // whenever the source was idle (a large recvGap): on a static screen nothing is captured for
  // seconds, so captureQpc - lastPresentedCapture would otherwise read that idle time as a decode
  // backlog and trip false congestion -> catchup -> keyframe-wait freeze. Only a genuine backlog
  // (frames arriving DENSELY faster than they present) leaves the floor stale and still trips it.
  uint64_t presentAnchorFloorUs = 0;
  // Rate-limit for stale-reference recovery (decoder reset + IDR request). Within
  // staleReferenceRecoveryMinIntervalUs of the last one, a behind-latest in-chain frame is decoded
  // in order instead of storming another IDR. (0.2.94: post-UAC keyframe churn.)
  uint64_t staleReferenceRecoveryMinIntervalUs = 0;
  uint64_t lastStaleRecoveryUs = 0;
  uint64_t recoveringSinceUs = 0;
  uint32_t recoveringHealthyStreak = 0;
  uint64_t lastRecoveryRequestUs = 0;
  // Time-based keyframe recovery (FrameGate::tick). The frame-driven re-asks (reason 3 every 30
  // dropped frames, the Recovering timeout) never ran while Congested and never ran at all when no
  // frame arrived, so a lost recovery IDR or a stalled source left the gate waiting for an IDR
  // nobody would send (history #390 item 2: 60 s of P frames, 0 re-requests). tick() re-asks on
  // the clock instead: recoveryRetryIntervalUs after the wait began, then doubling up to
  // recoveryRetryMaxIntervalUs, reset when an IDR decodes. 0 = off (config, REMOTE60_NATIVE_KEY_RECOVERY_RETRY_US).
  uint64_t recoveryRetryIntervalUs = 0;
  uint64_t recoveryRetryMaxIntervalUs = 0;
  uint64_t recoveryRetryDeferMaxUs = 0;  // config: the longest a due re-ask waits for a NACK repair
  uint64_t recoveryDeferSinceUs = 0;     // first deferral of the current due re-ask (0 = none)
  uint64_t keyWaitSinceUs = 0;          // when the current wait began (0 = not waiting)
  uint64_t recoveryNextRetryUs = 0;     // the next scheduled re-ask
  uint64_t recoveryRetryCurrentUs = 0;  // the current backoff step
  uint64_t recoveryRetryCount = 0;      // telemetry: timer-driven requests (reason 7)
  uint64_t recoveryRetryDeferred = 0;   // telemetry: ticks that held the re-ask for a NACK repair
  uint64_t recoveryRetryEpisodes = 0;   // telemetry: waits that needed at least one retry
  uint64_t keyWaitMaxUs = 0;            // telemetry: the longest wait for an IDR so far
};

}  // namespace remote60::native_poc::viewer
