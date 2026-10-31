p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
anchor = '    const size_t releasedBefore = count_of(host.log(), "[standby] released");\n'
assert s.count(anchor) == 1
block = r'''    std::printf("  -- the product's activation of a session entry (standby_session_link.hpp: its gate + the link) --\n");
    {
      remote60::native_poc::ConnectOpGate gate;
      remote60::native_poc::StandbySessionLink link;
      remote60::native_poc::ClientSessionController cl;
      NullSink sinkL;
      auto prep = [&](remote60::native_poc::ClientSessionConnectArgs& a) {
        a.encodedFrameSink = &sinkL;
        a.controlIntervalMs = 200;
      };
      auto settled = [&] {
        return wait_until([&] { const auto st = runner.StatusOf("h-standby");
                                return st.state == StandbyLease::State::Parked && !st.handedOff; }, 3000);
      };
      const size_t epochsL = count_of(host.log(), "(standby activation");
      const int64_t stale = gate.Next();
      gate.Next();  // a newer operation (another tap): `stale` is not current any more
      const auto r1 = remote60::native_poc::standby_adopt(gate, stale, runner, "h-standby", cl, link, prep);
      check("an operation stale before its connect: not adopted, the arguments abandoned, the lease keeps its socket",
            r1.taken && r1.generation == 0 && r1.why == "stale_operation" && !link.active() && settled() &&
                count_of(host.log(), "(standby activation") == epochsL,
            r1.why);
      const int64_t op2 = gate.Next();
      const auto r2 = remote60::native_poc::standby_adopt(gate, op2, runner, "h-standby", cl, link, prep,
                                                          [](const remote60::native_poc::ClientSessionConnectArgs&) { return false; });
      check("Connect refused before its worker: abandoned (the handle closed by the lease), parked again",
            r2.taken && r2.generation == 0 && r2.why == "connect_failed" && !link.active() && settled(), r2.why);
      const int64_t op3 = gate.Next();
      const auto r3 = remote60::native_poc::standby_adopt(gate, op3, runner, "h-standby", cl, link, prep);
      const bool upL = r3.generation != 0 &&
                       wait_until([&] { return cl.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
      check("the entry's own operation: adopted, connected by activation, video", upL && link.active() &&
            count_of(host.log(), "(standby activation") == epochsL + 1 && wait_until([&] { return sinkL.frames() > 0; }, 10000),
            r3.why + " | " + cl.Snapshot().lastError);
      check("...while it runs the lease is handed off (the pump leaves the socket to the session)",
            runner.StatusOf("h-standby").handedOff);
      const size_t endedL = count_of(host.log(), "control session ended");
      // the bridge's SessionEntry::Close: the gate's explicit close, Disconnect then the link's SessionOver
      gate.CloseExplicit([&] { cl.Disconnect(); link.Over(); }, nullptr, nullptr);
      check("closed as the bridge closes it: the lease is parked again on its own socket (SessionOver after Disconnect)",
            !link.active() && settled());
      check("...a second Over says nothing", !link.Over());
      check("...the host sees that session end",
            wait_until([&] { return count_of(host.log(), "control session ended") > endedL; }, 30000), host.tail());
      check("...and the lease stays parked by its own pings",
            wait_until([&] { const auto st = runner.StatusOf("h-standby");
                             return st.state == StandbyLease::State::Parked && !st.handedOff && st.lost == 0; }, 12000));
    }
'''
s = s.replace(anchor, block + anchor)
inc = '#include "standby_runner.hpp"\n'
if inc in s and '#include "standby_session_link.hpp"' not in s:
    s = s.replace(inc, inc + '#include "standby_session_link.hpp"\n', 1)
else:
    print('include anchor?', inc in s)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
