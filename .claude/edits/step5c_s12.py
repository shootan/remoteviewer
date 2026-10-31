import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'
edit(SRC + r'\viewer_udp_recovery_test.cpp', [
("""// S12: a complete but undecodable IDR (payload zeroed) must still recover through the decoder's
// own failure path -- an IDR request -- and the next good IDR resumes the stream. The
// no-extra-request rule for a complete IDR (S7) must not swallow this case. (Codex condition 3.)
void scenario_corrupted_idr_recovers_via_decode_failure() {
  std::printf("[S12] corrupted IDR: decode failure asks again, the next IDR resumes\\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  host.SkipSeq();  // the gap the IDR would otherwise close silently
  host.CorruptNextKey();
  (void)host.SendFrame(true, false, qpc_now_us());
  CHECK(host.corrupted_keys() == 1, "a zeroed IDR went out");
  const bool asked = pump_until(host, rig, 4000, [&]() { return host.keyframe_requests() > requestsBefore; });
  CHECK(asked, "the undecodable IDR produced a keyframe request (reason " + std::to_string(host.last_keyframe_reason()) + ")");
  const bool resumed = pump_until(host, rig, 5000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.gate.congestionState == ClientCongestionState::Normal;
  });
  CHECK(resumed, "resumed after the next good IDR (state " + state_name(rig) + ")");
  CHECK(host.keyframe_requests() <= requestsBefore + 4, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
}
""",
"""// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap. Observed on this
// machine's hardware MFT: the zeroed AU is swallowed without an error and without an output, and
// the P frames behind it decode against the references the (un-reset) decoder still holds. So
// there is no decoder-reported failure to recover from -- and no request must be manufactured
// either (that was the 90/90 churn). What is guaranteed: the gate is not stuck waiting, nothing
// storms, and the next good IDR is decoded and shown. A decoder that DOES report the failure
// takes the reason-4 path, which viewer_frame_gate_test covers (test_decode_failure_rebuild_
// threshold); a run of empty outputs takes the reason-5 path. (Codex condition 3.)
void scenario_corrupted_idr_does_not_wedge_or_storm() {
  std::printf("[S12] zeroed complete IDR behind a gap: no wedge, no storm, the next good IDR shows\\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  host.SkipSeq();  // the gap the IDR closes
  host.CorruptNextKey();
  (void)host.SendFrame(true, false, qpc_now_us());
  CHECK(host.corrupted_keys() == 1, "a zeroed IDR went out");
  pump(host, rig, 700);
  CHECK(!rig.gate.waitForKeyFrame, "not left waiting for a keyframe (state " + state_name(rig) + ")");
  CHECK(host.keyframe_requests() <= requestsBefore + 2,
        "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + " requests)");
  // The next good IDR is decoded and published.
  const uint64_t publishedBefore = rig.publishedCount.load();
  const uint32_t goodIdr = host.SendFrame(true, false, qpc_now_us());
  CHECK(goodIdr != 0, "a good IDR went out");
  const bool shown = pump_until(host, rig, 3000, [&]() {
    return rig.publishedCount.load() > publishedBefore && rig.maxPublishedSeq.load() >= goodIdr;
  });
  CHECK(shown, "the good IDR was decoded and published (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
}
"""),
("""  scenario_corrupted_idr_recovers_via_decode_failure();
""",
"""  scenario_corrupted_idr_does_not_wedge_or_storm();
"""),
])
