import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
ROOT = r'D:\remote\remote'
F = ROOT + r'\apps\native_poc\src\viewer_udp_recovery_test.cpp'
NL = chr(92) + 'n'  # literal backslash-n for the C++ printf
edit(F, [
# 1) FakeHost: remember the seq the last key AU left with
("      if (au.keyFrame) ++keyframesSent_;\n    }\n    return lastSeq;",
 "      if (au.keyFrame) {\n        ++keyframesSent_;\n        lastKeySeq_ = lastSeq;\n      }\n    }\n    return lastSeq;"),
("  uint64_t keyframes_sent() const { return keyframesSent_; }\n",
 "  uint64_t keyframes_sent() const { return keyframesSent_; }\n  uint32_t last_key_seq() const { return lastKeySeq_; }  // seq the last key AU left with (F-22)\n"),
("  uint64_t keyframesSent_ = 0;\n  uint64_t droppedChunks_ = 0;",
 "  uint64_t keyframesSent_ = 0;\n  uint32_t lastKeySeq_ = 0;\n  uint64_t droppedChunks_ = 0;"),
# 2) helper after idle(): one forced key, then plain pictures until that key is observed on the wire
("""// Idle wait (no frames) that still drains keyframe requests so the count is exact.
void idle(FakeHost& host, ViewerRig& rig, uint32_t ms) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(ms) * 1000ULL;
  while (qpc_now_us() < endUs) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    sleep_ms(2);
  }
}
""",
"""// Idle wait (no frames) that still drains keyframe requests so the count is exact.
void idle(FakeHost& host, ViewerRig& rig, uint32_t ms) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(ms) * 1000ULL;
  while (qpc_now_us() < endUs) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    sleep_ms(2);
  }
}

// F-22 (2026-09-07): the product encoder may be asynchronous -- SendFrame(true) can emit the
// previous picture (a P) or nothing at all, and the forced IDR leaves in a later call. A scenario
// that needs "the IDR is on the wire" observes it (keyframes_sent / last_key_seq) within a bound
// instead of assuming it left in the same call. Exactly one forced key: the follow-up calls are
// plain pictures, never a second forced key, so a corrupt injection or a "next good IDR" stays
// one AU. The order is only recorded, not asserted: a synchronous encoder passes on call 1.
struct KeyOnWire {
  uint32_t firstSeq = 0;      // what the forcing call returned (0 = no AU left in that call)
  bool firstWasKey = false;   // that AU was the IDR itself (synchronous encoder)
  uint32_t keySeq = 0;        // the seq the IDR actually left with
  uint32_t calls = 0;         // SendFrame calls made, including the forcing one
  uint64_t waitedUs = 0;
  bool ok = false;
};
KeyOnWire send_key_and_wait_on_wire(FakeHost& host, const char* what, uint32_t maxExtraCalls = 30, uint32_t maxMs = 1500) {
  KeyOnWire r;
  const uint64_t keysBefore = host.keyframes_sent();
  const uint64_t startUs = qpc_now_us();
  r.firstSeq = host.SendFrame(true, false, startUs);
  r.calls = 1;
  r.firstWasKey = host.keyframes_sent() > keysBefore;
  uint64_t nextUs = startUs + kFrameIntervalUs;
  while (host.keyframes_sent() == keysBefore) {
    if (r.calls > maxExtraCalls || qpc_now_us() - startUs > static_cast<uint64_t>(maxMs) * 1000ULL) break;
    const uint64_t nowUs = qpc_now_us();
    if (nowUs < nextUs) {
      std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(nextUs - nowUs, 2000)));
      continue;
    }
    nextUs += kFrameIntervalUs;
    (void)host.SendFrame(false, false, nowUs);  // a plain picture: never a second forced key
    ++r.calls;
  }
  r.ok = host.keyframes_sent() > keysBefore;
  r.keySeq = r.ok ? host.last_key_seq() : 0;
  r.waitedUs = qpc_now_us() - startUs;
  std::printf("  S12 order (%s): first call emitted %s seq=%u; IDR seq=%u on call %u (%llu ms)""" + NL + """", what,
              r.firstSeq == 0 ? "nothing" : (r.firstWasKey ? "the IDR" : "P"), r.firstSeq, r.keySeq, r.calls,
              static_cast<unsigned long long>(r.waitedUs / 1000));
  return r;
}
"""),
# 3) S12: observe the zeroed IDR on the wire (assertion unchanged), then the good IDR's real seq
("""  host.SkipSeq();  // the gap the IDR closes
  host.CorruptNextKey();
  (void)host.SendFrame(true, false, qpc_now_us());
  CHECK(host.corrupted_keys() == 1, "a zeroed IDR went out");
""",
"""  host.SkipSeq();  // the gap the IDR closes
  host.CorruptNextKey();
  // F-22: the IDR may leave in a later call than the forcing one; wait for it on the wire (bounded,
  // one forced key only) and then assert exactly as before.
  const KeyOnWire zeroed = send_key_and_wait_on_wire(host, "zeroed IDR");
  CHECK(zeroed.ok, "the zeroed IDR reached the wire (" + std::to_string(zeroed.calls) + " calls, " + std::to_string(zeroed.waitedUs / 1000) + " ms)");
  CHECK(host.corrupted_keys() == 1, "a zeroed IDR went out");
"""),
("""  const uint64_t publishedBefore = rig.publishedCount.load();
  const uint32_t goodIdr = host.SendFrame(true, false, qpc_now_us());
  CHECK(goodIdr != 0, "a good IDR went out");
""",
"""  const uint64_t publishedBefore = rig.publishedCount.load();
  // F-22: the good IDR's seq is the one it actually left with, not the forcing call's return
  // (which can be 0 or the previous P on an asynchronous encoder).
  const KeyOnWire good = send_key_and_wait_on_wire(host, "good IDR");
  const uint32_t goodIdr = good.keySeq;
  CHECK(goodIdr != 0, "a good IDR went out");
"""),
("""  CHECK(shown, "the good IDR was decoded and published (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
}
""",
"""  CHECK(shown, "the good IDR was decoded and published (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
  CHECK(host.corrupted_keys() == 1, "exactly one zeroed IDR in the whole scenario (" + std::to_string(host.corrupted_keys()) + ")");
}
"""),
])
print('s12 harness edit ok')
