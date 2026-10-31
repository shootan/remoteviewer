import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'
edit(SRC + r'\viewer_udp_recovery_test.cpp', [
("""  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
  std::printf("  seq=%u nacks=%llu retx=%llu keyReq=%llu published=%llu\\n", t,
""",
"""  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
  // The liveness heartbeat the UI watchdog reads (history #390 item 5) is wired into this path.
  {
    const auto& live = rig.ctx.recvLive;
    const uint64_t nowUs = qpc_now_us();
    CHECK(live.loopIterations.load() > 100, "recv loop heartbeat counted passes");
    CHECK(liveness_age_us(nowUs, live.lastDatagramUs.load()) < 200000, "recent datagram stamp");
    CHECK(liveness_age_us(nowUs, live.lastAssembledUs.load()) < 200000, "recent assembled stamp");
    CHECK(liveness_age_us(nowUs, live.lastDecodeReturnUs.load()) < 200000, "recent decode-return stamp");
    CHECK(liveness_age_us(nowUs, live.lastPublishUs.load()) < 200000, "recent publish stamp");
    CHECK(live.current_stage() == RecvStage::Recv, std::string("recv thread idles in recv (stage ") + recv_stage_name(live.current_stage()) + ")");
    SessionLivenessSample s;
    s.nowUs = nowUs;
    s.stage = live.current_stage();
    s.stageEnterUs = live.stageEnterUs.load();
    s.loopIterations = live.loopIterations.load();
    s.lastDatagramUs = live.lastDatagramUs.load();
    s.lastPublishUs = live.lastPublishUs.load();
    s.controlConnected = true;
    const auto v = evaluate_session_liveness(s, SessionLivenessConfig{});
    CHECK(!v.recvStalled && !v.linkSilent && !v.sessionDead, "healthy verdict on a live session");
  }
  std::printf("  seq=%u nacks=%llu retx=%llu keyReq=%llu published=%llu\\n", t,
"""),
])
