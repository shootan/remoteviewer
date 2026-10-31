p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/host_standby_admission_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''#include "host_standby_table.hpp"
''', '''#include "host_standby_table.hpp"
#include "standby_lease.hpp"
''')

rep('''int wmain(int argc, wchar_t** argv) {''', r'''// ===================================================================== keepalive-r2 (2) d: the PRODUCT client
struct MemGenStore : remote60::native_poc::StandbyGenerationStore {
  std::array<uint8_t, 16> id{};
  uint32_t last = 0;
  bool have = false;
  bool Load(std::array<uint8_t, 16>* d, uint32_t* l) override {
    if (!have) return false;
    *d = id;
    *l = last;
    return true;
  }
  bool Save(const std::array<uint8_t, 16>& d, uint32_t l) override {
    id = d;
    last = l;
    have = true;
    return true;
  }
};

uint64_t mono_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Pumps the lease until `done` or the time runs out.
bool pump_until(remote60::native_poc::StandbyLease& lease, const std::function<bool()>& done, uint32_t budgetMs) {
  const uint64_t until = mono_ms() + budgetMs;
  while (mono_ms() < until) {
    lease.Pump(100);
    if (done()) return true;
  }
  return done();
}

std::string last_events(const remote60::native_poc::StandbyLease& lease) {
  std::string out;
  for (const auto& e : lease.events()) out += e + "; ";
  return out;
}

// Two product clients (StandbyLease + ClientSessionController -- the code the apps will run) at a real host: both
// parked without a session; A activates (a session through the lease's own socket, the activation's Pong + HelloAck);
// B's Pings leave it alone; B takes over explicitly; A, parked again on a new socket, takes it back with a larger
// generation. An older host: the lease is Unsupported and never sends a Hello.
int run_product_client(const std::vector<std::string>& tokens, const std::wstring& oldHost) {
  using remote60::native_poc::StandbyLease;
  using remote60::native_poc::GenerationAllocator;
  const char* kIssuedIp = "211.218.222.1";
  const uint16_t kIssuedPort = 60420;
  std::printf("[product client: StandbyLease + ClientSessionController at a real host]\n");
  FakeDirectory dir;
  check("the fake directory starts", dir.Start());
  ScriptDirectory(dir, "h-standby", capabilitiesBody(tokens, kIssuedIp, kIssuedPort));
  SpawnedHost host;
  const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"1");
  check("a real GNLinkStream starts",
        StartHost(&host, dir.url(), mediaPort, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "standby"));
  check("the host registers", host.WaitFor("directory registered", 20000) || host.WaitFor("directory online", 20000),
        host.tail());
  size_t nextToken = 0;
  auto cap = [&]() { return nextToken < tokens.size() ? tokens[nextToken++] : std::string(); };
  MemGenStore storeA, storeB;
  auto randA = [](uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xA1); return true; };
  auto randB = [](uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0xB1); return true; };
  GenerationAllocator gensA(storeA, randA), gensB(storeB, randB);
  const uint32_t features = remote60::native_poc::kUdpFeatureVideoFec | remote60::native_poc::kUdpFeatureVideoFecInterleaved;
  StandbyLease a(gensA, features, cap, mono_ms), b(gensB, features, cap, mono_ms);
  std::string err;
  check("A's and B's leases open (one socket each)", a.Open("127.0.0.1", mediaPort, &err) && b.Open("127.0.0.1", mediaPort, &err), err);
  a.Start();
  b.Start();
  // the first capability lands with a heartbeat (seen up to ~50 s after registering)
  const bool parkedA = pump_until(a, [&] { return a.state() == StandbyLease::State::Parked; }, 70000);
  const bool parkedB = pump_until(b, [&] { return b.state() == StandbyLease::State::Parked; }, 20000);
  check("A parks (its StandbyHello admitted)", parkedA, last_events(a));
  check("B parks, its own entry", parkedB && b.entryId() != a.entryId(), last_events(b));
  check("...and NEITHER started a session", count_of(host.log(), "first session") == 0 &&
        count_of(host.log(), "session epoch=") == 0, host.tail());

  NullSink sinkA, sinkB;
  remote60::native_poc::ClientSessionController ca, cb;
  remote60::native_poc::ClientSessionConnectArgs argsA;
  argsA.encodedFrameSink = &sinkA;
  argsA.controlIntervalMs = 200;
  check("A's activation arguments: its lease's socket and the activation handshake", a.ActivationArgs(&argsA));
  const bool upA = ca.Connect(argsA) &&
                   wait_until([&] { return ca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
  check("A's ClientSessionController connects THROUGH the activation (no Hello)", upA, ca.Snapshot().lastError + " | " + last_events(a));
  check("...the host admitted it as the first session by activation", host.WaitFor("first session by activation", 3000), host.tail());
  check("...video reaches A's sink", wait_until([&] { return sinkA.frames() > 0; }, 10000));
  ca.RequestWindowList();
  check("...and A's control works (its window list arrives)", wait_until([&] { return ca.Snapshot().latestWindowListCount > 0; }, 8000));

  std::printf("  -- B, parked, pings for 12 s; A must not notice --\n");
  const size_t epochs = count_of(host.log(), "session epoch=");
  const size_t ended = count_of(host.log(), "control session ended");
  pump_until(b, [] { return false; }, 12000);
  check("B stays parked (its Pings answered), no entry lost", b.state() == StandbyLease::State::Parked && b.lostCount() == 0,
        last_events(b));
  check("NO new session, NO control-session end while B pinged", count_of(host.log(), "session epoch=") == epochs &&
        count_of(host.log(), "control session ended") == ended, host.tail());
  check("A is still connected", ca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected);

  std::printf("  -- B takes over explicitly; A's session ends; A parks again; A takes it back --\n");
  remote60::native_poc::ClientSessionConnectArgs argsB;
  argsB.encodedFrameSink = &sinkB;
  argsB.controlIntervalMs = 200;
  check("B's activation arguments", b.ActivationArgs(&argsB));
  const bool upB = cb.Connect(argsB) &&
                   wait_until([&] { return cb.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
  check("B connects through ITS activation", upB, cb.Snapshot().lastError + " | " + last_events(b));
  check("...a takeover: a new epoch for B", host.WaitFor("(standby activation", 3000) &&
        count_of(host.log(), "session epoch=") == epochs + 1, host.tail());
  check("...video reaches B", wait_until([&] { return sinkB.frames() > 0; }, 10000));
  const bool aEnded =
      wait_until([&] { return ca.Snapshot().state != remote60::native_poc::ClientSessionState::Connected; }, 25000);
  check("A's session ends (the host moved on)", aEnded, ca.Snapshot().status);
  ca.Disconnect();
  a.SessionOver();
  pump_until(a, [] { return false; }, 7000);
  check("A parks again on a new socket: its entry answers its Pings", a.state() == StandbyLease::State::Parked &&
        a.lostCount() == 0, last_events(a));
  remote60::native_poc::ClientSessionConnectArgs argsA2;
  argsA2.encodedFrameSink = &sinkA;
  argsA2.controlIntervalMs = 200;
  const uint64_t framesBefore = sinkA.frames();
  const bool upA2 = a.ActivationArgs(&argsA2) && ca.Connect(argsA2) &&
                    wait_until([&] { return ca.Snapshot().state == remote60::native_poc::ClientSessionState::Connected; }, 15000);
  check("A takes it back (a larger generation)", upA2, ca.Snapshot().lastError + " | " + last_events(a));
  check("...video reaches A again", wait_until([&] { return sinkA.frames() > framesBefore; }, 10000));
  check("no Hello was ever sent by either product client (no ordinary-Hello admission in the host log)",
        count_of(host.log(), "hello admitted") == 0 && count_of(host.log(), "[udp-hello]") == 0, host.tail());
  ca.Disconnect();
  cb.Disconnect();
  host.Stop();

  if (!oldHost.empty()) {
    std::printf("\n[product client at an OLDER host: Unsupported, no Hello]\n");
    FakeDirectory dir2;
    check("the fake directory starts", dir2.Start());
    ScriptDirectory(dir2, "h-old", capabilitiesBody(tokens, kIssuedIp, kIssuedPort));
    SpawnedHost old;
    const uint16_t port2 = remote60::native_poc::e2e::e2e_pick_free_udp_port();
    check("the older GNLinkStream starts",
          StartHost(&old, dir2.url(), port2, remote60::native_poc::e2e::e2e_pick_free_tcp_port(), "old", oldHost));
    old.WaitFor("directory registered", 20000) || old.WaitFor("directory online", 20000);
    MemGenStore storeC;
    GenerationAllocator gensC(storeC, randA);
    StandbyLease c(gensC, features, cap, mono_ms);
    check("a lease opens at the older host", c.Open("127.0.0.1", port2, &err), err);
    c.Start();
    const bool unsupported = pump_until(c, [&] { return c.state() == StandbyLease::State::Unsupported; }, 10000);
    check("no answer: Unsupported (no automatic connect to it)", unsupported, last_events(c));
    check("...and its session table untouched: no session, no Hello admission",
          count_of(old.log(), "first session") == 0 && count_of(old.log(), "client connected") == 0, old.tail());
    old.Stop();
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  WSACleanup();
  return gFailures ? 1 : 0;
}

int wmain(int argc, wchar_t** argv) {''')

rep('''  std::printf("host_standby_admission_e2e_test (keepalive-r2 A spike)\\n\\n");''',
    '''  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--product-client") return run_product_client(tokens, oldHost);
  }
  std::printf("host_standby_admission_e2e_test (keepalive-r2 A spike)\\n\\n");''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
