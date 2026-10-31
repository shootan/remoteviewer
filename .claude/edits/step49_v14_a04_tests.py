import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
S = r'D:\remote\remote\apps\native_poc\src'
NL = chr(92) + 'n'
F = S + r'\viewer_udp_recovery_test.cpp'

NOISE = '''  void CorruptNextKey() { corruptNextKey_ = true; }
  uint64_t corrupted_keys() const { return corruptedKeys_; }
  // A datagram the viewer's receive loop reads and ignores (too short for any video header, not a
  // control kind it handles): the shape of a control keepalive / ACK. Enough of them and recv()
  // never times out, which is the V14 starvation. (S16)
  void SendNoise() {
    sockaddr_in peer{};
    {
      std::lock_guard<std::mutex> lk(peerMu_);
      if (!peerKnown_.load()) return;
      peer = peer_;
    }
    uint8_t buf[16]{};
    const uint32_t magic = kMagic;
    const uint16_t kind = static_cast<uint16_t>(UdpPacketKind::Punch);
    const uint16_t size = static_cast<uint16_t>(sizeof(buf));
    std::memcpy(buf, &magic, sizeof(magic));
    std::memcpy(buf + 4, &kind, sizeof(kind));
    std::memcpy(buf + 6, &size, sizeof(size));
    (void)sendto(sock_, reinterpret_cast<const char*>(buf), sizeof(buf), 0,
                 reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
  }
'''

IDLE_NOISE = '''void idle(FakeHost& host, ViewerRig& rig, uint32_t ms) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(ms) * 1000ULL;
  while (qpc_now_us() < endUs) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    sleep_ms(2);
  }
}

// Idle wait (no frames) that also hands the viewer an ignorable datagram every `everyMs`: the
// receive loop then never sees a timeout -- a control-only link. (S16)
void idle_with_noise(FakeHost& host, ViewerRig& rig, uint32_t ms, uint32_t everyMs) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(ms) * 1000ULL;
  uint64_t nextUs = qpc_now_us();
  while (qpc_now_us() < endUs) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    const uint64_t nowUs = qpc_now_us();
    if (nowUs >= nextUs) {
      host.SendNoise();
      nextUs = nowUs + static_cast<uint64_t>(everyMs) * 1000ULL;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
'''

SCEN = '''
// S16 (V14): control / keepalive datagrams every 5 ms keep recv() from ever timing out. The receive
// loop's clock work (hold drain, NACK rounds, keyframe recovery timer) used to run only after a
// video chunk or a receive timeout, so a link carrying nothing but control traffic starved the
// recovery timer: a lost frame's keyframe wait was never re-asked. Same shape as S8, plus noise.
void scenario_control_noise_does_not_starve_recovery_timer() {
  std::printf("[S16] keyframe wait + 5 ms control noise, no video: the recovery timer still re-asks''' + NL + '''");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> lostP{0};
  LossPlan plan;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t, bool key, uint16_t) {
    if (!key && seq >= 40 && lostP.load() == 0) lostP = seq;
    return seq == lostP.load();  // the whole frame
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  host.SetHonorKeyframeRequests(false);
  CHECK(pump_until(host, rig, 3000, [&]() { return rig.gate.waitForKeyFrame; }), "the lost P frame opened a keyframe wait");
  idle(host, rig, 50);
  const uint64_t requestsAtStop = host.keyframe_requests();
  idle_with_noise(host, rig, 2600, 5);
  const uint64_t retries = host.keyframe_requests() - requestsAtStop;
  CHECK(retries >= 2 && retries <= 4, "timer re-asks under 5 ms control noise: " + std::to_string(retries));
  CHECK(rig.gate.waitForKeyFrame, "still waiting: no video was sent");
  host.SetHonorKeyframeRequests(true);
  const bool resumed = pump_until(host, rig, 5000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.maxPublishedSeq.load() > lostP.load() + 5;
  });
  CHECK(resumed, "resumed after the IDR (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(host.keyframe_requests() <= requestsAtStop + 6, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsAtStop) + ")");
}

// Shared body of S17 / S18 (A04 / N3): a static screen -- the LAST AU's final chunk is lost, the
// host does not answer NACKs and sends nothing after it. Nothing complete sits behind the stuck
// head, so the hold never released it and no discontinuity was raised: the viewer kept the old
// picture with a silent, exhausted NACK. Now the spent head is given up and the keyframe path
// recovers with one request; the host's next IDR resumes the stream.
void run_static_tail_loss_unanswered(const char* label, bool lostIsKey) {
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> armFrom{0};
  std::atomic<uint32_t> target{0};
  LossPlan plan;
  plan.answerNacks = false;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t idx, bool key, uint16_t count) {
    if (armFrom.load() == 0 || seq < armFrom.load() || key != lostIsKey || count < 2) return false;
    if (target.load() == 0) target = seq;
    return seq == target.load() && idx == count - 1;
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  idle(host, rig, 200);  // everything in flight settles; the screen is static from here
  const uint64_t requestsBefore = host.keyframe_requests();
  armFrom = host.last_seq() + 1;
  // One picture at a time until the drop happens, then nothing more.
  for (int i = 0; i < 20 && target.load() == 0; ++i) {
    (void)host.SendFrame(lostIsKey, false, qpc_now_us());
    sleep_ms(20);
  }
  const uint32_t t = target.load();
  CHECK(t != 0, std::string(label) + ": a multi-chunk AU lost its last chunk");
  CHECK(host.last_seq() == t, std::string(label) + ": nothing was sent after it (last seq " + std::to_string(host.last_seq()) + ")");
  idle(host, rig, 900);
  const uint64_t requests = host.keyframe_requests() - requestsBefore;
  std::printf("  %s: seq=%u nacks=%llu keyReq=+%llu waitForKey=%d''' + NL + '''", label, t,
              static_cast<unsigned long long>(host.nacks_for(t)), static_cast<unsigned long long>(requests),
              rig.gate.waitForKeyFrame ? 1 : 0);
  CHECK(host.nacks_for(t) >= 1 && host.nacks_for(t) <= 3, std::string(label) + ": bounded NACK rounds (" + std::to_string(host.nacks_for(t)) + ")");
  CHECK(requests >= 1 && requests <= 2, std::string(label) + ": the spent head was given up and a keyframe asked for (+" + std::to_string(requests) + ", timer re-ask allowed)");
  CHECK(rig.gate.waitForKeyFrame, std::string(label) + ": waiting for the keyframe it asked for");
  // The host answers with an IDR: the viewer resumes, and the complete IDR closes the gap itself.
  const uint64_t publishedBefore = rig.publishedCount.load();
  const KeyOnWire idr = send_key_and_wait_on_wire(host, label);
  CHECK(idr.ok, std::string(label) + ": the recovery IDR went out");
  const bool resumed = pump_until(host, rig, 3000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.publishedCount.load() > publishedBefore && rig.published_key(idr.keySeq);
  });
  CHECK(resumed, std::string(label) + ": resumed on the IDR seq " + std::to_string(idr.keySeq) + " (key seqs " + rig.published_key_seqs() + ")");
  CHECK(host.keyframe_requests() <= requestsBefore + 3, std::string(label) + ": no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, std::string(label) + ": state " + state_name(rig));
}

void scenario_static_last_p_tail_loss_unanswered_recovers() {
  std::printf("[S17] static screen: last P's tail chunk lost, NACK unanswered, no successor -> give up, keyframe request, recovery''' + NL + '''");
  run_static_tail_loss_unanswered("S17", false);
}

void scenario_static_last_idr_tail_loss_unanswered_recovers() {
  std::printf("[S18] static screen: last IDR's tail chunk lost, NACK unanswered, no successor -> give up, keyframe request, recovery''' + NL + '''");
  run_static_tail_loss_unanswered("S18", true);
}
'''

edit(F, [
("  void CorruptNextKey() { corruptNextKey_ = true; }\n  uint64_t corrupted_keys() const { return corruptedKeys_; }\n", NOISE),
("""void idle(FakeHost& host, ViewerRig& rig, uint32_t ms) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(ms) * 1000ULL;
  while (qpc_now_us() < endUs) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    sleep_ms(2);
  }
}
""", IDLE_NOISE),
("}  // namespace\n\nint main() {", SCEN + "\n}  // namespace\n\nint main() {"),
("  scenario_corrupted_idr_does_not_wedge_or_storm();\n",
 "  scenario_corrupted_idr_does_not_wedge_or_storm();\n  scenario_control_noise_does_not_starve_recovery_timer();\n  scenario_static_last_p_tail_loss_unanswered_recovers();\n  scenario_static_last_idr_tail_loss_unanswered_recovers();\n"),
])
print('V14+A04 test edits ok')
