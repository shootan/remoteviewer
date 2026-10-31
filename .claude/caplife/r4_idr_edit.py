p = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\native_poc\src\android_decoder_idr_recovery_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
N = '\r\n'


def rep(a, b, cnt=1):
    global s
    a = a.replace('\n', N)
    b = b.replace('\n', N)
    assert s.count(a) == cnt, (s.count(a), a[:70])
    s = s.replace(a, b)


rep("""// each with a host that keeps sending deltas and an idle one (only the IDRs asked for).
""", """// each with a host that keeps sending deltas and an idle one (only the IDRs asked for).
// r4 (Codex codex_apk_decoder_liveness_src_r1.md N3): the codec's create FAILS (not slow) until ready, the host idle:
//   U5   failing until --late-ms: every IDR the 8 asks bring is spent on a failed create and the budget is gone; once
//        a create can succeed, the decoder's own bounded retry makes the codec and re-arms the owed IDR (a new token:
//        the same 8 asks) -- judged from ready like the others;
//   U5b  failing for 26 s, past the decoder's retries (20 x 1 s): the failure is TERMINAL (codecState failed_terminal,
//        logged) -- nothing recovers by itself and nothing is asked for 3 s after the create could succeed; then a
//        new surface (the explicit path) brings the codec back -- judged from that re-attach.
""")
rep("""  bool newer;        // a newer selection at lateMs / 2
};""", """  bool newer;        // a newer selection at lateMs / 2
  bool failCreate = false;  // r4 N3: the codec's create FAILS until ready (instead of being held)
  bool terminal = false;    // r4 N3: ...for longer than the decoder's own retries; then a re-attach
  int lateOverrideMs = 0;   // this case's own "ready" (0: --late-ms)
};""")
rep("""  bool firstIsCurrentIdr = false, oldGenQueued = false;
  std::string detail;""", """  bool firstIsCurrentIdr = false, oldGenQueued = false;
  // r4 N3 (U5b): the state before ready, and what happened in the 3 s after the create could succeed again
  bool terminalBefore = false, selfRecovered = false;
  size_t asksIdle = 0;
  std::string detail;""")
rep("""Outcome run_case(const Case& k, int lateMs) {
  Outcome o;""", """Outcome run_case(const Case& k, int lateMs) {
  Outcome o;
  if (k.lateOverrideMs > 0) lateMs = k.lateOverrideMs;""")
rep("""  int surface = 1;
  if (k.holdCreate) {""", """  int surface = 1, surface2 = 2;
  if (k.failCreate) {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::controls().createFails = true;
  }
  if (k.holdCreate) {""")
rep("""  const size_t q0 = queued_count();
  const auto ready = Clock::now();
  o.asksBefore = host.RequestsBetween(t0, ready);
  o.pingGapMs = host.PingGapMs(t0, ready);
  if (!surfaceSet) sink.SetSurface(env, &surface);  // U1: the surface comes now
  letGo = true;
  fake_ndk::barrier().held.store(false);""", """  const size_t q0 = queued_count();
  auto ready = Clock::now();
  o.asksBefore = host.RequestsBetween(t0, ready);
  o.pingGapMs = host.PingGapMs(t0, ready);
  o.terminalBefore = sink.DebugStatus().find(" codecState=failed_terminal") != std::string::npos;
  if (k.failCreate) {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::controls().createFails = false;  // a create can succeed from here
  }
  if (!surfaceSet) sink.SetSurface(env, &surface);  // U1: the surface comes now
  letGo = true;
  fake_ndk::barrier().held.store(false);
  if (k.terminal) {
    // U5b: terminal -- nothing by itself for 3 s (no codec, no IDR in, no ask); then the explicit path: a new surface
    const auto idle0 = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    for (uint8_t t : queued_tags_from(q0)) o.selfRecovered = o.selfRecovered || t == idr_tag(currentGen);
    o.asksIdle = host.RequestsBetween(idle0, Clock::now());
    sink.SetSurface(env, &surface2);
    ready = Clock::now();
  }""")
rep("""  o.detail = "pingGap=" + std::to_string(o.pingGapMs) + "ms asks before ready=" + std::to_string(o.asksBefore) + " after=" + std::to_string(o.asksAfter) +""",
    """  o.detail = "pingGap=" + std::to_string(o.pingGapMs) + "ms asks before ready=" + std::to_string(o.asksBefore) + " after=" + std::to_string(o.asksAfter) +
             (k.terminal ? " terminalBefore=" + std::to_string(o.terminalBefore ? 1 : 0) + " idleAsks=" +
                               std::to_string(o.asksIdle) + " selfRecovered=" + std::to_string(o.selfRecovered ? 1 : 0)
                         : std::string()) +""")
rep("""  if (k.newer) check(name + ": nothing of the superseded generation was queued", !o.oldGenQueued, o.detail);
}""", """  if (k.newer) check(name + ": nothing of the superseded generation was queued", !o.oldGenQueued, o.detail);
  if (k.failCreate && !k.terminal) {
    check(name + ": the owed budget was spent on failed creates before ready (>= 8 asks)", o.asksBefore >= 8, o.detail);
  }
  if (k.terminal) {
    check(name + ": past the decoder's retries the failure is TERMINAL (codecState=failed_terminal)", o.terminalBefore,
          o.detail);
    check(name + ": terminal means terminal: nothing recovered by itself, nothing asked, for 3 s",
          !o.selfRecovered && o.asksIdle == 0, o.detail);
  }
}""")
rep("""      {"U3 newer idle", 2000, true, false, true},
  };""", """      {"U3 newer idle", 2000, true, false, true},
      // r4 N3
      {"U5 create fails idle", -1, false, false, false, true, false, 0},
      {"U5b create fails past the retries idle, then a new surface", -1, false, false, false, true, true, 26000},
  };""")
rep("""  std::printf("\\nandroid_decoder_idr_recovery_test: %d checks, %d failed\\nRESULT: %s\\n", gChecks, gFailures,""",
    """  {
    std::lock_guard<std::mutex> lk(fake_ndk::mu());
    fake_ndk::controls().createFails = false;
  }
  std::printf("\\nandroid_decoder_idr_recovery_test: %d checks, %d failed\\nRESULT: %s\\n", gChecks, gFailures,""")
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
