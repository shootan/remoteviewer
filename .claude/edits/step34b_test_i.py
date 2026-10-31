import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
T = ROOT + r'\apps\native_poc\src\viewer_frame_gate_test.cpp'
edit(T, [
(r"""// (j) Keyframes: a FRESH IDR ends pending in admit (it is the anchor); a HELD IDR (hold > 300 ms)""",
 r"""// (i) A decode FAILURE while pending: the receiver's note_decode_failure requests a key (reason 4)
// and enters the keyframe wait; the next admit ends pending and drops non-key frames until the
// IDR -- the existing behaviour, unchanged by the P11 rule.
void test_decode_failure_while_pending_enters_keyframe_wait() {
  std::printf("[T-P11i] decode failure while pending: keyframe wait, pending ends at the next admit\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  FrameGateLag lag{};
  CHECK(r.feedDecoded(t, t, true, 0, t + 10 * kMs) == FrameGateVerdict::Decode);
  const uint64_t heldAt = t + 830 * kMs;
  const uint64_t heldCap = t + 30 * kMs;
  CHECK(r.feed(heldAt, heldCap, false, t, false, &lag, &in, false, heldAt) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);
  r.decodeFailed(in, lag);
  CHECK(r.gate.waitForKeyFrame);
  CHECK(r.sink.requests(4) >= 1);
  // The next (non-key) frame: pending ends at admit, and the frame is dropped for the key wait.
  const uint64_t next = heldAt + 5 * kMs;
  CHECK(r.feed(next, next - 15 * kMs, false, heldCap, false, &lag, &in, false, next) == FrameGateVerdict::DropWaitingKeyframe);
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.waitForKeyFrame);
  // The IDR arrives fresh: decodes, the wait ends, no pending.
  const uint64_t keyAt = next + 20 * kMs;
  CHECK(r.feedDecoded(keyAt, keyAt - 10 * kMs, true, heldCap, keyAt) == FrameGateVerdict::Decode);
  CHECK(!r.gate.waitForKeyFrame && !r.gate.resumeAnchorPending);
}

// (j) Keyframes: a FRESH IDR ends pending in admit (it is the anchor); a HELD IDR (hold > 300 ms)"""),
(r"""  test_held_resume_accepted_empty_keeps_pending();
  test_fresh_idr_ends_pending_held_idr_keeps_it();""",
 r"""  test_held_resume_accepted_empty_keeps_pending();
  test_decode_failure_while_pending_enters_keyframe_wait();
  test_fresh_idr_ends_pending_held_idr_keeps_it();"""),
])
print('step34b test (i) ok')
