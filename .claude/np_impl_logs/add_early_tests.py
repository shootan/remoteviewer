import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')
anchor = 'int main() {\n'
assert s.count(anchor) == 1
block = r'''// ---- np-impl r1 (Codex NP design, contracts 1-5): the early relay race ----
//
// On a scripted clock: the clock hook returns gVirtualUs, and the receive hook either hands back the next
// scripted datagram (moving the clock to its time) or, when none falls inside the timeout PunchAny asked
// for, moves the clock to the end of that timeout and reports a timeout. So 999 / 1000 / 1001 ms are those
// exact instants, not whatever the scheduler made of them.

using remote60::native_poc::PunchRaceOptions;
using remote60::native_poc::PunchRaceReport;

struct Scripted {
  int64_t atMs;
  uint16_t port;      // the tuple it comes from (127.0.0.1:port)
  int shape;          // 0 = a valid Punch, 1 = wrong magic, 2 = 48 bytes, 3 = wrong kind, 4 = version 1, 5 = one byte
};

std::vector<Scripted> gScript;
size_t gScriptNext = 0;
std::atomic<int64_t> gVirtualUs{0};
int64_t gStopAtMs = -1;
std::atomic<bool>* gStopFlag = nullptr;

int64_t virtual_clock() { return gVirtualUs.load(); }

int scripted_recv(remote60::native_poc::SocketHandle, char* buf, int len, sockaddr_in* from, int* fromLen,
                  int* err) {
  const int64_t now = gVirtualUs.load();
  const int64_t limit = now + static_cast<int64_t>(remote60::native_poc::PunchLastRecvTimeoutMsForTest()) * 1000;
  if (gStopFlag && gStopAtMs >= 0 && limit >= gStopAtMs * 1000 &&
      (gScriptNext >= gScript.size() || gScript[gScriptNext].atMs * 1000 > gStopAtMs * 1000)) {
    gVirtualUs = (std::max)(now, gStopAtMs * 1000);
    gStopFlag->store(true);
    *err = WSAETIMEDOUT;
    return -1;
  }
  if (gScriptNext < gScript.size() && gScript[gScriptNext].atMs * 1000 <= limit) {
    const Scripted& e = gScript[gScriptNext++];
    gVirtualUs = (std::max)(now, e.atMs * 1000);
    UdpHelloPacket p{};
    p.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
    int n = static_cast<int>(sizeof(p));
    if (e.shape == 1) p.magic = 0x12345678u;
    if (e.shape == 2) n = static_cast<int>(sizeof(p)) - 1;
    if (e.shape == 3) p.kind = static_cast<uint16_t>(UdpPacketKind::Hello);
    if (e.shape == 4) p.version = 1;
    if (e.shape == 5) n = 1;
    std::memcpy(buf, &p, (std::min)(n, len));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(e.port);
    *from = a;
    *fromLen = static_cast<int>(sizeof(a));
    *err = 0;
    return n;
  }
  gVirtualUs = limit;
  *err = WSAETIMEDOUT;
  return -1;
}

struct EarlyResult {
  bool answered = false;
  RendezvousCandidate chosen;
  PunchRaceReport report;
  std::string error;
};

constexpr uint16_t kDirectPort = 41001;
constexpr uint16_t kRelayPort = 41002;
constexpr uint16_t kPrivatePort = 41003;

// One race on the scripted clock. commitMs 0 = the legacy race.
EarlyResult run_scripted(DirectoryRendezvous& rendezvous, std::vector<Scripted> script, uint32_t commitMs,
                         uint32_t budgetMs = 4000, std::atomic<bool>* stop = nullptr, int64_t stopAtMs = -1) {
  gScript = std::move(script);
  gScriptNext = 0;
  gVirtualUs = 1000000000;  // any origin; reported times are relative to T0
  for (auto& e : gScript) e.atMs += 1000000;  // the same origin, in ms
  gStopFlag = stop;
  gStopAtMs = stopAtMs >= 0 ? stopAtMs + 1000000 : -1;
  remote60::native_poc::SetPunchClockHookForTest(&virtual_clock);
  remote60::native_poc::SetPunchRecvHookForTest(&scripted_recv);
  const std::vector<RendezvousCandidate> candidates = {
      {"127.0.0.1", kPrivatePort, "private"},
      {"127.0.0.1", kDirectPort, "public"},
      {"127.0.0.1", kRelayPort, "relay"},
  };
  EarlyResult r;
  PunchRaceOptions options;
  options.relayCommitMs = commitMs;
  r.answered = rendezvous.PunchAny(candidates, budgetMs, options, &r.chosen, &r.report, &r.error, stop);
  remote60::native_poc::SetPunchRecvHookForTest(nullptr);
  remote60::native_poc::SetPunchClockHookForTest(nullptr);
  gStopFlag = nullptr;
  gStopAtMs = -1;
  return r;
}

std::string describe(const EarlyResult& r) {
  return "answered=" + std::to_string(r.answered) + " kind=" + r.chosen.kind + " decision=" + r.report.decision +
         " chosenMs=" + std::to_string(r.report.chosenMs) + " err=" + r.error;
}

void TestEarlyRaceBoundaries() {
  std::printf("early race: the commit point on a scripted clock (999 / 1000 / 1001, relay first/late/lost)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();

  // relay at once, direct just before the commit point: direct wins at 999
  auto r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {999, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 999 && r.report.decision == "direct" &&
             r.report.relayHeld && r.report.firstRelayMs == 20,
         "relay at 20, direct at 999: direct at 999 (" + describe(r) + ")");
  // direct exactly at the commit point: the commit comes first
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {1000, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000 && r.report.decision == "relay",
         "relay at 20, direct at 1000: relay at 1000 (" + describe(r) + ")");
  // direct just after: relay at 1000, not later
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {1001, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000,
         "relay at 20, direct at 1001: relay at 1000 (" + describe(r) + ")");
  // no relay answer by the commit point: no failure at 1000, a direct answer at 1001 still wins
  r = run_scripted(rendezvous, {{1001, kDirectPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 1001 && !r.report.relayHeld,
         "no relay, direct at 1001: direct at 1001 -- nothing ends at the commit point (" + describe(r) + ")");
  // relay later than the commit point (an older server's grace, or a slow one): taken when it comes
  r = run_scripted(rendezvous, {{2500, kRelayPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 2500 &&
             r.report.decision == "relay-after-commit",
         "relay only at 2500: relay at 2500 (" + describe(r) + ")");
  // relay late, direct between commit point and relay: direct
  r = run_scripted(rendezvous, {{1500, kDirectPort, 0}, {2500, kRelayPort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 1500,
         "direct at 1500, relay at 2500: direct at 1500 (" + describe(r) + ")");
  // relay lost, nothing else: the budget, not the commit point, ends it
  r = run_scripted(rendezvous, {}, 1000, 4000);
  expect(!r.answered && r.report.decision == "none" && r.error == "no candidate answered",
         "nothing answers: no answer at the budget (" + describe(r) + ")");
  // the private candidate is a direct one too
  r = run_scripted(rendezvous, {{10, kRelayPort, 0}, {30, kPrivatePort, 0}}, 1000);
  expect(r.answered && r.chosen.kind == "private" && r.report.chosenMs == 30,
         "relay at 10, private at 30: private (" + describe(r) + ")");
  // a commit point beyond the budget: a held relay answer is still taken when the budget ends
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}}, 5000, 4000);
  expect(r.answered && r.chosen.kind == "relay",
         "commit beyond the budget: the held relay is taken at the budget (" + describe(r) + ")");
  rendezvous.Close();
}

void TestLegacyRaceIsUnchanged() {
  std::printf("legacy race (no relayEarly): first valid answer wins, the relay included -- as before\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  // An older server's relay answers after its grace (2500): a direct answer before it wins.
  auto r = run_scripted(rendezvous, {{360, kDirectPort, 0}, {2500, kRelayPort, 0}}, 0);
  expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 360 && r.report.decision == "direct",
         "direct at 360, relay at 2500: direct at 360 (" + describe(r) + ")");
  r = run_scripted(rendezvous, {{2500, kRelayPort, 0}}, 0);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 2500 && r.report.decision == "relay",
         "relay only at 2500: relay at 2500 (" + describe(r) + ")");
  // Why the early race is only for an accepted connect: without the hold, a relay that answers at once
  // beats a direct path that would have answered 300 ms later.
  r = run_scripted(rendezvous, {{20, kRelayPort, 0}, {300, kDirectPort, 0}}, 0);
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 20,
         "legacy, relay at 20, direct at 300: relay at 20 (the reason the server keeps its grace) (" +
             describe(r) + ")");
  rendezvous.Close();
}

void TestAnswerShapeIsChecked() {
  std::printf("answers are checked for shape: garbage from a candidate is not ready (contract 5)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  for (int mode : {0, 1000}) {
    auto r = run_scripted(rendezvous,
                          {{10, kRelayPort, 1}, {20, kRelayPort, 2}, {30, kDirectPort, 3}, {40, kDirectPort, 4},
                           {50, kRelayPort, 5}, {1500, kDirectPort, 0}},
                          static_cast<uint32_t>(mode));
    expect(r.answered && r.chosen.kind == "public" && r.report.chosenMs == 1500 &&
               r.report.invalidFromCandidate == 5 && r.report.firstRelayMs < 0,
           std::string(mode ? "early" : "legacy") + ": five malformed answers ignored, the valid one at 1500 taken (" +
               describe(r) + " invalid=" + std::to_string(r.report.invalidFromCandidate) + ")");
  }
  // the pure function on its own
  UdpHelloPacket p{};
  p.kind = static_cast<uint16_t>(UdpPacketKind::Punch);
  expect(remote60::native_poc::punch_answer_is_valid(reinterpret_cast<const char*>(&p), sizeof(p)),
         "a real Punch is valid");
  expect(!remote60::native_poc::punch_answer_is_valid(reinterpret_cast<const char*>(&p), 0), "zero bytes are not");
  expect(!remote60::native_poc::punch_answer_is_valid(nullptr, sizeof(p)), "no buffer is not");
  p.size = 48;
  expect(!remote60::native_poc::punch_answer_is_valid(reinterpret_cast<const char*>(&p), sizeof(p)),
         "a declared size that is not 49 is not");
  rendezvous.Close();
}

void TestStrangersAreCountedNotChosen() {
  std::printf("datagrams from no candidate: counted per tuple, bounded, never chosen (contract 1)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  // A valid Punch from the host's public IP on another port (the H-a shape), twice, then the relay.
  auto r = run_scripted(rendezvous, {{100, 42000, 0}, {250, 42000, 0}, {260, 42001, 5}, {300, kRelayPort, 0}}, 1000);
  bool found = false;
  for (const auto& s : r.report.strangers) {
    if (s.port == 42000 && s.datagrams == 2 && s.validPunches == 2 && s.firstMs == 100 && s.firstBytes == 49) found = true;
  }
  expect(r.answered && r.chosen.kind == "relay" && r.report.chosenMs == 1000 && r.report.strangerDatagrams == 3 &&
             r.report.strangers.size() == 2 && found,
         "a valid Punch from 127.0.0.1:42000 is recorded (n=2 punch=2 firstMs=100), the relay is still chosen at 1000 (" +
             describe(r) + ")");
  // Twenty distinct strangers: the table keeps eight, the rest are counted.
  std::vector<Scripted> many;
  for (int i = 0; i < 20; ++i) many.push_back({10 + i, static_cast<uint16_t>(43000 + i), 0});
  r = run_scripted(rendezvous, many, 1000, 1500);
  expect(!r.answered && r.report.strangers.size() == PunchRaceReport::kMaxStrangers &&
             r.report.strangersUntracked == 12 && r.report.strangerDatagrams == 20,
         "20 strangers: 8 tracked, 12 untracked, none chosen (" + describe(r) + ")");
  rendezvous.Close();
}

void TestCancelOutranksTheChoice() {
  std::printf("a cancel outranks any choice, at the commit point too (contract 4)\n");
  ObserveStub directory;
  if (!directory.Start()) {
    expect(false, "test rig failed to start");
    return;
  }
  DirectoryRendezvous rendezvous;
  std::string observed, error;
  expect(rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error), "observe");
  directory.Stop();
  std::atomic<bool> stop{false};
  // The relay is held; the stop lands just as the clock reaches the commit point.
  auto r = run_scripted(rendezvous, {{20, kRelayPort, 0}}, 1000, 4000, &stop, 1000);
  expect(!r.answered && r.error == "cancelled" && r.report.decision == "cancelled" && r.report.relayHeld,
         "relay held, stop at 1000: cancelled, nothing chosen (" + describe(r) + ")");
  stop = false;
  r = run_scripted(rendezvous, {{1500, kDirectPort, 0}}, 1000, 4000, &stop, 600);
  expect(!r.answered && r.error == "cancelled", "stop at 600 before any answer: cancelled (" + describe(r) + ")");
  rendezvous.Close();
}

// The same on real sockets and the real clock, with the margin the scheduler needs: a direct path that
// opens at 300 ms beats an immediate relay; a dead direct path gives the relay at about 1000 ms.
void TestEarlyRaceOnRealSockets() {
  std::printf("early race on real sockets: direct at ~300 beats an immediate relay; no direct -> relay at ~1000\n");
  {
    ObserveStub directory;
    Responder relay;
    LateResponder host;
    if (!directory.Start() || !relay.Start() || !host.Start(300)) {
      expect(false, "test rig failed to start");
      return;
    }
    DirectoryRendezvous rendezvous;
    std::string observed, error;
    rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
    const std::vector<RendezvousCandidate> candidates = {
        {"127.0.0.1", dead_port(), "private"},
        {"127.0.0.1", host.port(), "public"},
        {"127.0.0.1", relay.port(), "relay"},
    };
    PunchRaceOptions options;
    options.relayCommitMs = 1000;
    PunchRaceReport report;
    RendezvousCandidate chosen;
    const bool answered = rendezvous.PunchAny(candidates, 4000, options, &chosen, &report, &error);
    expect(answered && chosen.kind == "public" && report.relayHeld && report.chosenMs >= 250 && report.chosenMs < 700,
           "direct chosen at " + std::to_string(report.chosenMs) + " ms with the relay held since " +
               std::to_string(report.firstRelayMs) + " ms");
    rendezvous.Close();
    host.Stop();
    relay.Stop();
    directory.Stop();
  }
  {
    ObserveStub directory;
    Responder relay;
    if (!directory.Start() || !relay.Start()) {
      expect(false, "test rig failed to start");
      return;
    }
    DirectoryRendezvous rendezvous;
    std::string observed, error;
    rendezvous.Observe("127.0.0.1", directory.port(), "token", &observed, &error);
    const std::vector<RendezvousCandidate> candidates = {
        {"127.0.0.1", dead_port(), "private"},
        {"127.0.0.1", dead_port(), "public"},
        {"127.0.0.1", relay.port(), "relay"},
    };
    PunchRaceOptions options;
    options.relayCommitMs = 1000;
    PunchRaceReport report;
    RendezvousCandidate chosen;
    const auto start = std::chrono::steady_clock::now();
    const bool answered = rendezvous.PunchAny(candidates, 4000, options, &chosen, &report, &error);
    const long long elapsed = ms_since(start);
    expect(answered && chosen.kind == "relay" && report.chosenMs >= 1000 && report.chosenMs < 1100 && elapsed < 1150,
           "relay chosen at " + std::to_string(report.chosenMs) + " ms (call " + std::to_string(elapsed) + " ms)");
    rendezvous.Close();
    relay.Stop();
    directory.Stop();
  }
}

'''
s = s.replace(anchor, block + anchor)
calls_anchor = '  TestContinuousTtlKeepsTheCadence();\n'
assert s.count(calls_anchor) == 1
s = s.replace(calls_anchor, calls_anchor + '''  TestEarlyRaceBoundaries();
  TestLegacyRaceIsUnchanged();
  TestAnswerShapeIsChecked();
  TestStrangersAreCountedNotChosen();
  TestCancelOutranksTheChoice();
  TestEarlyRaceOnRealSockets();
''')
if '#include <cstring>' not in s:
    s = s.replace('#include <cstdio>\n', '#include <cstdio>\n#include <cstring>\n', 1)
if '#include <algorithm>' not in s:
    s = s.replace('#include <atomic>\n', '#include <algorithm>\n#include <atomic>\n', 1)
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
