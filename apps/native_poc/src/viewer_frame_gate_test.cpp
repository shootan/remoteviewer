// Unit test for the viewer's FrameGate (viewer split refactor Phase 2 / T1): the stale-drop rule
// with the reference-chain anchor, the congestion state machine (catch-up entry, re-entry throttle,
// keyframe exit, healthy-streak recovery, recovery timeout), the keyframe wait, the decoder-wedge
// rebuild threshold, the empty-output streak recovery and the timestamp overflow path -- driven with
// time as an argument and a recording sink, no decoder and no sockets.
//
// Build: remote60_viewer_frame_gate_test (CMake). Run: prints "viewer_frame_gate_test: PASS" and exits 0.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "viewer_constants.hpp"
#include "viewer_frame_gate.hpp"

using namespace remote60::native_poc::viewer;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
      ++gFailures;                                                                   \
    }                                                                                \
  } while (0)

struct RecordingSink : FrameGateSink {
  int resets = 0;
  int rebuilds = 0;
  bool rebuildResult = true;
  std::vector<uint16_t> keyframeReasons;
  void reset_decoder() override { ++resets; }
  bool rebuild_decoder() override { ++rebuilds; return rebuildResult; }
  void request_keyframe(uint16_t reason) override { keyframeReasons.push_back(reason); }
  int requests(uint16_t reason) const {
    int n = 0;
    for (uint16_t r : keyframeReasons) n += (r == reason) ? 1 : 0;
    return n;
  }
};

struct Rig {
  FrameGateState gate;
  RecvStats st;
  RecordingSink sink;
  FrameGate fg{gate, st, sink};
  uint32_t seq = 0;
  Rig() {
    // the env defaults main() reads (viewer_constants.hpp) and a 60 fps hint
    gate.catchupReenterMinIntervalUs = kCatchupReenterMinIntervalUsDefault;   // 600 ms
    gate.staleCaptureDropUs = kStaleCaptureDropUs;                            // 50 ms
    gate.congestionRecoverMinUs = kCongestionRecoverMinUsDefault;             // 250 ms
    gate.congestionRecoveryTimeoutUs = kCongestionRecoveryTimeoutUsDefault;   // 1.5 s
    gate.frameIntervalUs = 16667;
    gate.waitForKeyFrame = true;  // an H.264 session starts waiting for its first IDR
  }
  // One frame: packet arrival at `t`, capture timestamp `captureUs`, present anchor `presented`.
  FrameGateVerdict feed(uint64_t t, uint64_t captureUs, bool key, uint64_t presented, bool suppressed,
                        FrameGateLag* lag = nullptr, FrameGateInputs* inOut = nullptr) {
    FrameGateInputs in{};
    in.captureQpcUs = captureUs;
    in.seq = ++seq;
    in.keyFrame = key;
    in.packetNowUs = t;
    in.recvGapUs = fg.note_packet(t);
    in.presentedCapUs = presented;
    in.catchupSuppressed = suppressed;
    FrameGateLag l{};
    const FrameGateVerdict v = fg.admit(in, &l);
    if (lag) *lag = l;
    if (inOut) *inOut = in;
    return v;
  }
  // A frame that decoded: what the receiver does on the Decode verdict when the decoder succeeds.
  void decoded(const FrameGateInputs& in) {
    fg.note_decode_ok();
    fg.note_reference_sync(in);
    fg.clear_empty_streak();
  }
};

const uint64_t kUs = 1;
const uint64_t kMs = 1000 * kUs;
const uint64_t kFrame = 16667;

void test_keyframe_wait_then_decode() {
  std::printf("[T1] keyframe wait: non-key frames drop (request reason 3 on the first), the IDR decodes\n");
  Rig r;
  uint64_t t = 1000 * kMs;
  // 5 non-key frames while waiting: all dropped, one keyframe request (waitingKeyDropCount % 30 == 1)
  for (int i = 0; i < 5; ++i) {
    CHECK(r.feed(t, t, false, 0, false) == FrameGateVerdict::DropWaitingKeyframe);
    t += kFrame;
  }
  CHECK(r.gate.waitingKeyDropCount == 5);
  CHECK(r.sink.requests(3) == 1);
  CHECK(r.st.skippedQueued == 5);
  // the IDR passes; after decode the wait ends and the anchor moves
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  CHECK(!r.gate.waitForKeyFrame);
  CHECK(r.gate.lastDecodedKeyCaptureUs == t);
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.sink.resets == 0);
}

void test_stale_drop_quiet_vs_reference_chain() {
  std::printf("[T1] stale drop: older than the anchor is quiet, inside the live chain resyncs once (reason 6)\n");
  Rig r;
  uint64_t t = 2000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);  // anchor = t
  // a straggler 100 ms OLDER than the anchor while the newest seen capture is t: quiet drop
  CHECK(r.feed(t + kFrame, t - 100 * kMs, false, 0, false) == FrameGateVerdict::DropStale);
  CHECK(r.gate.staleDropCount == 1);
  CHECK(r.gate.holdLatestDropCount == 1);
  CHECK(r.gate.staleReferenceRecoveryCount == 0);
  CHECK(r.sink.resets == 0);
  CHECK(r.sink.requests(6) == 0);
  // advance the newest capture by 200 ms, then a frame 100 ms behind it but AFTER the anchor:
  // it sits in the live reference chain, so dropping it needs an IDR resync
  const uint64_t t2 = t + 200 * kMs;
  CHECK(r.feed(t2, t2, false, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);  // non-key: anchor stays at t
  CHECK(r.feed(t2 + kFrame, t2 - 100 * kMs, false, 0, false) == FrameGateVerdict::DropStale);
  CHECK(r.gate.staleReferenceRecoveryCount == 1);
  CHECK(r.gate.waitForKeyFrame);
  CHECK(r.sink.resets == 1);
  CHECK(r.sink.requests(6) == 1);
  // a second stale frame in the same gap does not resync again (waitForKeyFrame already set)
  CHECK(r.feed(t2 + 2 * kFrame, t2 - 90 * kMs, false, 0, false) == FrameGateVerdict::DropStale);
  CHECK(r.gate.staleReferenceRecoveryCount == 1);
  CHECK(r.sink.resets == 1);
  // presented anchor far ahead of the frame also counts as stale (quiet: frame older than the key anchor)
  Rig r2;
  CHECK(r2.feed(t, t, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r2.decoded(in);
  CHECK(r2.feed(t + kFrame, t + kFrame, false, /*presented=*/t + kFrame + 80 * kMs, false) == FrameGateVerdict::DropStale);
  CHECK(r2.gate.staleDropCount == 1 && r2.gate.holdLatestDropCount == 0);
}

void test_catchup_entry_recovery_and_timeout() {
  std::printf("[T1] congestion: 3 dense lagging frames -> Congested (reason 1); IDR -> Recovering; 3 healthy + 250 ms -> Normal; timeout re-requests\n");
  Rig r;
  uint64_t t = 3000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  // decode-queue lag of 400 ms (> 300 ms) on three consecutive dense frames
  for (int i = 1; i <= 2; ++i) {
    t += kFrame;
    CHECK(r.feed(t, t, false, t - 400 * kMs, false, nullptr, &in) == FrameGateVerdict::Decode);
    r.decoded(in);
    CHECK(r.gate.lagTriggerStreak == static_cast<uint32_t>(i));
    CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  }
  t += kFrame;
  CHECK(r.feed(t, t, false, t - 400 * kMs, false) == FrameGateVerdict::DropCongested);
  CHECK(r.gate.congestionState == ClientCongestionState::Congested);
  CHECK(r.gate.catchupMode);
  CHECK(r.gate.lastCatchupEnterUs == t);
  CHECK(r.gate.waitForKeyFrame);
  CHECK(r.sink.resets == 1);
  CHECK(r.sink.requests(1) == 1);
  CHECK(r.gate.congestionTransitionCount == 1);
  CHECK(r.gate.burstDropCount == 1);
  // more non-key frames while Congested: dropped as catch-up burst
  t += kFrame;
  CHECK(r.feed(t, t, false, t - 400 * kMs, false) == FrameGateVerdict::DropCongested);
  CHECK(r.gate.burstDropCount == 2);
  // the IDR ends catch-up: Recovering, and it decodes
  t += kFrame;
  CHECK(r.feed(t, t, true, t - 400 * kMs, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  CHECK(r.gate.congestionState == ClientCongestionState::Recovering);
  CHECK(!r.gate.catchupMode);
  CHECK(r.gate.recoveringSinceUs == t);
  const uint64_t recoveringSince = t;
  // healthy frames (lag 100 ms <= 400 ms) but before 250 ms elapsed: still Recovering
  for (int i = 0; i < 3; ++i) {
    t += kFrame;
    CHECK(r.feed(t, t, false, t - 100 * kMs, false, nullptr, &in) == FrameGateVerdict::Decode);
    r.decoded(in);
  }
  CHECK(r.gate.congestionState == ClientCongestionState::Recovering);
  // the IDR that entered Recovering was itself evaluated (lag 400 ms <= 400 ms counts as healthy), so 1 + 3
  CHECK(r.gate.recoveringHealthyStreak == 4);
  // once 250 ms have elapsed with the streak healthy: Normal
  t = recoveringSince + 260 * kMs;
  CHECK(r.feed(t, t, false, t - 100 * kMs, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.gate.congestionRecoveryCount == 1);
  CHECK(r.gate.congestionRecoveryMaxUs > 0);

  // recovery timeout: back into Congested through an IDR while lagging, then stay unhealthy 1.5 s
  Rig r3;
  t = 5000 * kMs;
  CHECK(r3.feed(t, t, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r3.decoded(in);
  for (int i = 0; i < 3; ++i) { t += kFrame; r3.feed(t, t, false, t - 400 * kMs, false, nullptr, &in); if (i < 2) r3.decoded(in); }
  CHECK(r3.gate.congestionState == ClientCongestionState::Congested);
  t += kFrame;
  CHECK(r3.feed(t, t, true, t - 400 * kMs, false, nullptr, &in) == FrameGateVerdict::Decode);
  r3.decoded(in);
  CHECK(r3.gate.congestionState == ClientCongestionState::Recovering);
  const uint64_t since3 = t;
  const int reqBefore = r3.sink.requests(1);
  // unhealthy (lag 500 ms > 400 ms) but before the 1.5 s timeout: nothing
  t = since3 + 1000 * kMs;
  CHECK(r3.feed(t, t, false, t - 500 * kMs, false, nullptr, &in) == FrameGateVerdict::Decode);
  r3.decoded(in);
  CHECK(r3.gate.congestionState == ClientCongestionState::Recovering);
  CHECK(r3.sink.requests(1) == reqBefore);
  // past the timeout: one keyframe request, back to Congested; the next frame is a catch-up drop
  t = since3 + 1600 * kMs;
  r3.feed(t, t, false, t - 500 * kMs, false);
  CHECK(r3.gate.congestionState == ClientCongestionState::Congested);
  CHECK(r3.sink.requests(1) == reqBefore + 1);
  CHECK(r3.gate.lastRecoveryRequestUs == t);
  CHECK(r3.gate.waitForKeyFrame);
}

void test_catchup_suppressed_and_reentry_throttle() {
  std::printf("[T1] picker suppression keeps the lag streak at 0; re-entry within 600 ms is throttled\n");
  Rig r;
  uint64_t t = 7000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  for (int i = 0; i < 5; ++i) {
    t += kFrame;
    CHECK(r.feed(t, t, false, t - 400 * kMs, /*suppressed=*/true, nullptr, &in) == FrameGateVerdict::Decode);
    r.decoded(in);
  }
  CHECK(r.gate.lagTriggerStreak == 0);
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  // now enter catch-up for real, exit on an IDR, recover, then trigger again within 600 ms: throttled
  for (int i = 0; i < 3; ++i) { t += kFrame; r.feed(t, t, false, t - 400 * kMs, false, nullptr, &in); if (i < 2) r.decoded(in); }
  CHECK(r.gate.congestionState == ClientCongestionState::Congested);
  const uint64_t enteredAt = r.gate.lastCatchupEnterUs;
  t += kFrame;
  r.feed(t, t, true, t - 400 * kMs, false, nullptr, &in); r.decoded(in);   // Recovering
  for (int i = 0; i < 3; ++i) { t += kFrame; r.feed(t, t, false, t - 100 * kMs, false, nullptr, &in); r.decoded(in); }
  t = enteredAt + 300 * kMs;
  r.feed(t, t, false, t - 100 * kMs, false, nullptr, &in); r.decoded(in);   // Normal (250 ms elapsed, streak 4)
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  const int reqBefore = r.sink.requests(1);
  for (int i = 0; i < 3; ++i) { t += kFrame; r.feed(t, t, false, t - 400 * kMs, false, nullptr, &in); r.decoded(in); }
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);  // throttled: < 600 ms since the last entry
  CHECK(r.gate.catchupEnterThrottledCount == 1);
  CHECK(r.sink.requests(1) == reqBefore);
}

void test_decode_failure_rebuild_threshold() {
  std::printf("[T1] decode failure: reset x7, rebuild on the 8th (reason 4 each); success clears the streak\n");
  Rig r;
  uint64_t t = 9000 * kMs;
  FrameGateInputs in{};
  FrameGateLag lag{};
  CHECK(r.feed(t, t, true, 0, false, &lag, &in) == FrameGateVerdict::Decode);
  for (int i = 1; i <= 7; ++i) {
    r.fg.note_decode_failure(in, lag);
    CHECK(r.gate.decodeConsecutiveFailCount == static_cast<uint32_t>(i));
  }
  CHECK(r.sink.resets == 7);
  CHECK(r.sink.rebuilds == 0);
  CHECK(r.sink.requests(4) == 7);
  CHECK(r.gate.congestionState == ClientCongestionState::Congested);
  r.fg.note_decode_failure(in, lag);
  CHECK(r.sink.rebuilds == 1);
  CHECK(r.sink.resets == 7);
  CHECK(r.gate.decodeConsecutiveFailCount == 0);  // rebuild succeeded
  // a failed rebuild keeps the streak so the next failure retries it
  r.sink.rebuildResult = false;
  for (int i = 0; i < 8; ++i) r.fg.note_decode_failure(in, lag);
  CHECK(r.sink.rebuilds == 2);
  CHECK(r.gate.decodeConsecutiveFailCount == 8);
  r.fg.note_decode_failure(in, lag);
  CHECK(r.sink.rebuilds == 3);
  // a success clears it
  r.fg.note_decode_ok();
  CHECK(r.gate.decodeConsecutiveFailCount == 0);
  CHECK(r.st.decodeFailCount == 17);
}

void test_empty_output_streak_and_timestamp_overflow() {
  std::printf("[T1] empty output: 12 in a row -> recovery (reason 5) once per 600 ms; timestamp overflow -> reason 4\n");
  Rig r;
  uint64_t t = 11000 * kMs;
  FrameGateInputs in{};
  FrameGateLag lag{};
  CHECK(r.feed(t, t, true, 0, false, &lag, &in) == FrameGateVerdict::Decode);
  r.fg.note_decode_ok();
  r.fg.note_reference_sync(in);
  for (int i = 1; i <= 11; ++i) {
    r.fg.note_decode_empty(in, lag);
    CHECK(r.gate.decodeEmptyStreak == static_cast<uint64_t>(i));
  }
  CHECK(r.sink.requests(5) == 0);
  r.fg.note_decode_empty(in, lag);
  CHECK(r.sink.requests(5) == 1);
  CHECK(r.sink.resets == 1);
  CHECK(r.st.decodeEmptyRecoveryCount == 1);
  CHECK(r.gate.decodeEmptyStreak == 0);
  CHECK(r.gate.congestionState == ClientCongestionState::Congested);
  CHECK(r.gate.waitForKeyFrame);
  // 12 more within 600 ms of the last catch-up entry: throttled, no second request
  for (int i = 0; i < 12; ++i) r.fg.note_decode_empty(in, lag);
  CHECK(r.sink.requests(5) == 1);
  CHECK(r.gate.catchupEnterThrottledCount == 1);
  CHECK(r.st.decodeEmptyCount == 24);
  // the 300 ms rule: a streak that lasts 300 ms triggers even below 12
  Rig r2;
  t = 13000 * kMs;
  CHECK(r2.feed(t, t, true, 0, false, &lag, &in) == FrameGateVerdict::Decode);
  r2.fg.note_decode_ok();
  r2.fg.note_reference_sync(in);
  r2.fg.note_decode_empty(in, lag);  // streak 1 at t
  in.packetNowUs = t + 310 * kMs;
  r2.fg.note_decode_empty(in, lag);
  CHECK(r2.sink.requests(5) == 1);
  // timestamp overflow
  Rig r3;
  t = 15000 * kMs;
  CHECK(r3.feed(t, t, true, 0, false, &lag, &in) == FrameGateVerdict::Decode);
  r3.fg.note_decode_ok();
  r3.fg.note_timestamp_overflow(in, lag);
  CHECK(r3.sink.requests(4) == 1);
  CHECK(r3.sink.resets == 1);
  CHECK(r3.gate.waitForKeyFrame);
  CHECK(r3.gate.congestionState == ClientCongestionState::Congested);
  CHECK(r3.st.decodeTimestampOverflowCount == 1);
}

void test_aligned_lag_and_queue_depth() {
  std::printf("[T1] aligned_lag_us anchors on the first sample; queue depth histogram buckets by frame interval\n");
  bool ready = false;
  uint64_t remoteBase = 0, localBase = 0;
  CHECK(FrameGate::aligned_lag_us(1000, 5000, ready, remoteBase, localBase) == 0);
  CHECK(ready && remoteBase == 1000 && localBase == 5000);
  CHECK(FrameGate::aligned_lag_us(2000, 6000, ready, remoteBase, localBase) == 0);      // on time
  CHECK(FrameGate::aligned_lag_us(3000, 7500, ready, remoteBase, localBase) == 500);    // 500 late
  CHECK(FrameGate::aligned_lag_us(500, 9000, ready, remoteBase, localBase) == 0);       // remote went backwards: re-anchor
  CHECK(remoteBase == 500 && localBase == 9000);
  Rig r;
  CHECK(r.fg.queue_depth_frames(0) == 0);
  CHECK(r.fg.queue_depth_frames(1) == 1);
  CHECK(r.fg.queue_depth_frames(kFrame * 2) == 2);
  CHECK(r.fg.queue_depth_frames(kFrame * 5000) == 1000);  // capped
  r.fg.sample_queue_depth(0);
  r.fg.sample_queue_depth(kFrame);
  r.fg.sample_queue_depth(kFrame * 2);
  r.fg.sample_queue_depth(kFrame * 3);
  r.fg.sample_queue_depth(kFrame * 9);
  CHECK(r.st.queueDepthSampleCount == 5);
  CHECK(r.st.queueDepthHist[0] == 1 && r.st.queueDepthHist[1] == 1 && r.st.queueDepthHist[2] == 1 &&
        r.st.queueDepthHist[3] == 1 && r.st.queueDepthHist[4] == 1);
  CHECK(r.st.queueDepthFramesMax == 9);
}

}  // namespace

int main() {
  test_keyframe_wait_then_decode();
  test_stale_drop_quiet_vs_reference_chain();
  test_catchup_entry_recovery_and_timeout();
  test_catchup_suppressed_and_reentry_throttle();
  test_decode_failure_rebuild_threshold();
  test_empty_output_streak_and_timestamp_overflow();
  test_aligned_lag_and_queue_depth();
  if (gFailures != 0) {
    std::printf("viewer_frame_gate_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("viewer_frame_gate_test: PASS\n");
  return 0;
}
