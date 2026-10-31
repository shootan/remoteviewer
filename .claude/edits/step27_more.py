import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

# CMake: the real-sender boundary test
edit(ROOT + r'\apps\native_poc\CMakeLists.txt', [
(r"""add_executable(remote60_host_sender_queue_policy_test
  src/host_sender_queue_policy_test.cpp
)
target_include_directories(remote60_host_sender_queue_policy_test PRIVATE src)
""",
 r"""add_executable(remote60_host_sender_queue_policy_test
  src/host_sender_queue_policy_test.cpp
)
target_include_directories(remote60_host_sender_queue_policy_test PRIVATE src)

# P11 sender boundary on the real sender thread over loopback UDP: old-epoch AUs queued when the
# flush lands never start on the wire, an AU already being chunked completes (the documented
# exception), the new epoch's IDR is the next AU, and the fence is the epoch, not the generation.
add_executable(remote60_host_sender_epoch_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/host_sender_epoch_test.cpp
)
target_include_directories(remote60_host_sender_epoch_test PRIVATE src)
target_link_libraries(remote60_host_sender_epoch_test PRIVATE common ws2_32)
"""),
])

# viewer frame gate tests: the four conditions
edit(S + r'\viewer_frame_gate_test.cpp', [
(r"""void test_idle_resume_no_false_congestion() {""",
 r"""// P11 guard: the host's hold is small (15 ms) but the RENDERER is stuck (presented anchor frozen)
// while frames keep arriving densely -- a real viewer-side backlog the host stamps say nothing
// about. The pending rule never engages (nothing was held) and the lag rule trips as before.
void test_small_host_hold_renderer_stalled_still_trips() {
  std::printf("[T-P11c] small host hold, renderer stalled: the decode-queue lag still trips\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in, false, t + 10 * kMs) == FrameGateVerdict::Decode);
  r.decoded(in);
  const uint64_t resume = t + 800 * kMs;
  bool tripped = false;
  for (int i = 0; i < 40 && !tripped; ++i) {
    const uint64_t at = resume + i * 2 * kMs;   // dense arrival (network delivered a bundle)
    const uint64_t c = resume + i * 16 * kMs;   // captures advance normally...
    (void)r.feed(at, c, false, t, false, nullptr, &in, false, c + 15 * kMs);  // ...each held only 15 ms
    tripped = r.gate.congestionState != ClientCongestionState::Normal;
  }
  CHECK(tripped);
  CHECK(r.gate.heldResumeFrames == 0);
  CHECK(r.sink.requests(1) >= 1);
}

// P11 lifetime: a pending resume anchor ends on a keyframe wait, on a decoded keyframe, and on a
// congestion state change; it never outlives the episode it started in.
void test_pending_lifetime_ends_on_state_changes() {
  std::printf("[T-P11d] pending ends on keyframe wait / decoded keyframe / state change\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in, false, t + 10 * kMs) == FrameGateVerdict::Decode);
  r.decoded(in);
  const uint64_t heldAt = t + 830 * kMs;
  (void)r.feed(heldAt, t + 30 * kMs, false, t, false, nullptr, &in, false, heldAt);  // held resume frame
  CHECK(r.gate.resumeAnchorPending);
  // A decoded keyframe (resync) ends it.
  (void)r.feed(heldAt + 5 * kMs, heldAt, true, t + 30 * kMs, false, nullptr, &in, false, heldAt + 6 * kMs);
  r.decoded(in);
  CHECK(!r.gate.resumeAnchorPending);
  // Start another pending, then a keyframe wait ends it before the next frame is judged.
  const uint64_t held2 = heldAt + 1200 * kMs;
  (void)r.feed(held2, heldAt + 40 * kMs, false, heldAt, false, nullptr, &in, false, held2);
  CHECK(r.gate.resumeAnchorPending);
  r.gate.waitForKeyFrame = true;
  (void)r.feed(held2 + 2 * kMs, held2, false, heldAt, false, nullptr, &in, false, held2 + 3 * kMs);
  CHECK(!r.gate.resumeAnchorPending);
}

// P11 stamps: a missing (0) or reversed (send < capture) stamp is no evidence of freshness or of
// holding -- the resume frame follows the pre-P11 rule (anchors at once), and never starts pending.
void test_unknown_or_reversed_stamps_use_old_rule() {
  std::printf("[T-P11e] unknown / reversed stamps: the pre-P11 rule, no pending\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in, false, t + 10 * kMs) == FrameGateVerdict::Decode);
  r.decoded(in);
  const uint64_t resume = t + 830 * kMs;
  (void)r.feed(resume, t + 30 * kMs, false, t, false, nullptr, &in, false, 0);  // no send stamp
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs == t + 30 * kMs);  // anchored as before P11
  const uint64_t resume2 = resume + 900 * kMs;
  (void)r.feed(resume2, resume2 + 5 * kMs, false, t + 30 * kMs, false, nullptr, &in, false, resume2);  // send < capture
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs == resume2 + 5 * kMs);
  CHECK(r.gate.heldResumeFrames == 0);
}

// P11 budget: a second held gap frame inside a pending spends the same budget instead of renewing
// it; the budget expires on time from the FIRST held frame.
void test_pending_budget_not_renewed_by_repeated_gaps() {
  std::printf("[T-P11f] repeated held gap frames do not renew the pending budget\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in, false, t + 10 * kMs) == FrameGateVerdict::Decode);
  r.decoded(in);
  const uint64_t held1 = t + 830 * kMs;
  (void)r.feed(held1, t + 30 * kMs, false, t, false, nullptr, &in, false, held1);
  CHECK(r.gate.resumeAnchorPending && r.gate.resumeAnchorPendingSinceUs == held1);
  const uint64_t held2 = held1 + 300 * kMs;  // another gap, another held frame
  (void)r.feed(held2, t + 60 * kMs, false, t + 30 * kMs, false, nullptr, &in, false, held2);
  CHECK(r.gate.resumeAnchorPending);
  CHECK(r.gate.resumeAnchorPendingSinceUs == held1);  // not renewed
  CHECK(r.gate.resumeAnchorPendingFrames == 1);
  // 500 ms after the FIRST held frame the budget is spent, even though the second was 300 ms ago.
  const uint64_t late = held1 + 510 * kMs;
  (void)r.feed(late, t + 90 * kMs, false, t + 60 * kMs, false, nullptr, &in, false, late);
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.heldResumeExpired == 1);
}

void test_idle_resume_no_false_congestion() {"""),
(r"""  test_held_resume_frame_does_not_anchor();
  test_backlog_after_silence_still_trips();
""",
 r"""  test_held_resume_frame_does_not_anchor();
  test_backlog_after_silence_still_trips();
  test_small_host_hold_renderer_stalled_still_trips();
  test_pending_lifetime_ends_on_state_changes();
  test_unknown_or_reversed_stamps_use_old_rule();
  test_pending_budget_not_renewed_by_repeated_gaps();
"""),
])
print('step27 edits ok')
