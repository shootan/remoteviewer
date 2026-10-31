p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''#include "standby_lease.hpp"
''', '''#include "standby_lease.hpp"
#include "standby_runner.hpp"
''')

rep('''  ca.Disconnect();
  cb.Disconnect();
  host.Stop();

  if (realDirectory) {''', r'''  ca.Disconnect();
  cb.Disconnect();

  std::printf("  -- the app's StandbyRunner (a pump thread per PC): start, activate, session over, release --\n");
  {
    MemGenStore storeR;
    auto randR = [](uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xD1); return true; };
    GenerationAllocator gensR(storeR, randR);
    remote60::native_poc::StandbyRunner runner(gensR, features, [&](const std::string&) { return cap; }, mono_ms);
    check("the runner starts a lease for the PC (its own thread)", runner.Start("h-standby") && runner.size() == 1);
    check("...it parks without anyone pumping it here",
          wait_until([&] { return runner.StatusOf("h-standby").state == StandbyLease::State::Parked; }, 20000));
    NullSink sinkR;
    remote60::native_poc::ClientSessionController cr;
    remote60::native_poc::ClientSessionConnectArgs argsR;
    argsR.encodedFrameSink = &sinkR;
    argsR.controlIntervalMs = 200;
    const size_t epochsR = count_of(host.log(), "(standby activation");
    const bool upR = runner.TakeActivation("h-standby", &argsR) && cr.Connect(argsR) &&
                     wait_until([&] { return cr.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
    check("TakeActivation + Connect: the runner's PC takes the session by activation (video)", upR &&
          count_of(host.log(), "(standby activation") == epochsR + 1 && wait_until([&] { return sinkR.frames() > 0; }, 10000),
          cr.Snapshot().lastError);
    check("...while handed off the runner reports it so (the pump leaves the socket alone)",
          runner.StatusOf("h-standby").handedOff);
    cr.Disconnect();
    runner.SessionOver("h-standby");
    check("session over: parked again by the runner's own pings",
          wait_until([&] { const auto st = runner.StatusOf("h-standby");
                           return st.state == StandbyLease::State::Parked && !st.handedOff; }, 15000));
    const size_t releasedBefore = count_of(host.log(), "[standby] released");
    runner.Release("h-standby");
    check("Release: the lease is gone here and released at the host (one StandbyRelease)",
          runner.size() == 0 && host.WaitFor("[standby] released", 3000) && count_of(host.log(), "[standby] released") == releasedBefore + 1,
          host.tail());
  }
  host.Stop();

  if (realDirectory) {''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
