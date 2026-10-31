import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'

# History entry #393: fill in the verification line.
p = r'D:\remote\remote\.claude\edits\history_393.md'
t = open(p, 'rb').read().decode('utf-8')
t = t.replace('- 검증: 아래 결과 참조(빌드·테스트 로그).',
              '- 검증: `remote60_viewer_frame_gate_test` PASS(신규 2건 포함), `remote60_viewer_udp_recovery_test` **9/9 PASS**(S8: 무프레임 중 타이머 재요청 waitedUs 506,217·1,521,060 관측, S9: Congested→첫 IDR 손실→retry(509ms)→Recovering→Normal), GNLinkViewer 빌드 OK. 관찰: keyframe-wait 중 폐기되는 P 프레임이 lag 추정을 키워 Congested 진입(reason=decode_queue)을 일으키는 기존 동작을 S8 로그에서 확인 — 3단계에서 keyframe-wait 폐기 프레임은 혼잡 트리거에서 제외.')
open(p, 'wb').write(t.encode('utf-8'))
print('history_393 verification filled')

# Stage 3 tweak: a frame that the keyframe wait is about to drop must not feed the congestion
# trigger -- nothing is being presented while waiting, so the lag estimate only measures the wait.
edit(SRC + r'\viewer_frame_gate.cpp', [
("""  const bool lagTrigger =
      (decodeQueueLagEstimateUs > gate.decodeQueueLagDropUs) ||
      (in.presentedCapUs > 0 && streamLagUs > gate.catchupLagDropUs);
  // denseArrival computed above (reused by the stale-behind-latest gate).
  if (lagTrigger && denseArrival && !in.catchupSuppressed && !in.synthetic) {
""",
"""  const bool lagTrigger =
      (decodeQueueLagEstimateUs > gate.decodeQueueLagDropUs) ||
      (in.presentedCapUs > 0 && streamLagUs > gate.catchupLagDropUs);
  // A non-key frame the keyframe wait is about to drop is not evidence of a backlog: nothing is
  // presented while the gate waits, so the lag estimate only measures the wait itself, and the
  // streak it built entered Congested (decoder reset + reason-1 request) on top of a wait that
  // was already asking for the same IDR. Integration test S8 showed this on every resume.
  const bool keyWaitDrop = gate.waitForKeyFrame && !in.keyFrame;
  // denseArrival computed above (reused by the stale-behind-latest gate).
  if (lagTrigger && denseArrival && !in.catchupSuppressed && !in.synthetic && !keyWaitDrop) {
"""),
])

edit(SRC + r'\viewer_frame_gate_test.cpp', [
("""int main() {
  test_synthetic_gap_then_real_burst_no_false_congestion();
""",
"""// While the gate waits for an IDR nothing is presented, so every dropped P frame reads as more
// "decode lag"; that streak used to enter Congested (reset + reason-1 request) on top of a wait that
// was already asking for the IDR. The wait's own drops must not feed the congestion trigger.
void test_keyframe_wait_drops_do_not_enter_congested() {
  std::printf("[T1] keyframe wait: dropped P frames with a stale present anchor do not enter Congested\\n");
  Rig r;
  FrameGateInputs in{};
  const uint64_t start = 1000 * kMs;
  CHECK(r.feed(start, start, true, 0, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  r.gate.waitForKeyFrame = true;  // a lost frame (the receiver's discontinuity path)
  for (int n = 1; n <= 60; ++n) {
    const uint64_t now = start + n * kFrame;
    CHECK(r.feed(now, now, false, /*presented=*/start, false) == FrameGateVerdict::DropWaitingKeyframe);
  }
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.gate.lagTriggerStreak == 0);
  CHECK(r.sink.requests(1) == 0);
  CHECK(r.sink.resets == 0);
  // The IDR decodes; a genuine backlog afterwards still trips as before.
  const uint64_t tIdr = start + 61 * kFrame;
  CHECK(r.feed(tIdr, tIdr, true, start, false, nullptr, &in) == FrameGateVerdict::Decode);
  r.decoded(in);
  for (int n = 1; n <= 3; ++n) {
    const uint64_t now = tIdr + n * kFrame;
    r.feed(now, now, false, now - 400 * kMs, false, nullptr, &in);
    if (n < 3) r.decoded(in);
  }
  CHECK(r.gate.congestionState == ClientCongestionState::Congested);
}

int main() {
  test_keyframe_wait_drops_do_not_enter_congested();
  test_synthetic_gap_then_real_burst_no_false_congestion();
"""),
])
