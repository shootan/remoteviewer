p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep("""  const int pings = host.pings;
  const uint64_t until = mono_ms() + 7000;
  while (mono_ms() < until && host.pings == pings) lease.Pump(50);
  check("...and the lease pings its own target again", host.pings > pings && lease.state() == State::Parked);
}
""", """  const int pings = host.pings;
  const uint64_t until = mono_ms() + 7000;
  while (mono_ms() < until && host.pings == pings) lease.Pump(50);
  check("...and the lease pings its own target again", host.pings > pings && lease.state() == State::Parked);
}

// r2 (Codex codex_ka_e_src_r1.md N2, its probe): a SYNCHRONOUS capability source (a PC's directory calls, in the
// CapabilityFn) observes from the socket it is given. After a session tied that socket to its peer, a fresh capability
// (a restart: Lost / re-admission) must still be able to observe -- before r2 it observed from the tied socket and failed.
void sync_capability_test(FakeHost& host) {
  std::printf("\\n--- r2 N2: a synchronous capability after a session observes from a fresh socket ---\\n");
  Echo directory;
  check("an observe endpoint up", directory.Start());
  std::atomic<int> calls{0}, observed{0};
  auto capability = [&](n::SocketHandle s, n::StandbyCapability* out) {
    ++calls;
    if (!n::standby_observe_on(s, "127.0.0.1", directory.port, "obs-sync")) return false;
    ++observed;
    out->token = "cap-test";
    out->ip = "127.0.0.1";
    out->port = host.port;
    return true;
  };
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, capability, mono_ms);
  check("parked by a synchronous capability (its first observe answered)", park(lease) && calls == 1 && observed == 1);
  n::ClientSessionConnectArgs args;
  uint64_t op = 0;
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(host.port);
  inet_pton(AF_INET, "127.0.0.1", &peer.sin_addr);
  const bool session = lease.ActivationArgs(&args, nullptr, &op) &&
                       connect(args.preparedUdpSocket, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) == 0;
  n::close_socket(&args.preparedUdpSocket);
  check("a session tied the shared socket to its peer; SessionOver", session && lease.SessionOver(op));
  lease.Start();  // a fresh capability (what a Lost / a re-admission does)
  check("...the fresh capability's observe is answered (a fresh socket), and the lease parks again",
        calls == 2 && observed == 2 && park(lease), "calls=" + std::to_string(calls.load()) + " observed=" +
        std::to_string(observed.load()));
  // the same while the pump reads: the reopen waits for the read in flight (no descriptor closed under a read)
  std::atomic<bool> stopPump{false};
  std::thread pump([&] {
    while (!stopPump) lease.Pump(30);
  });
  n::ClientSessionConnectArgs args2;
  uint64_t op2 = 0;
  bool session2 = false;
  {
    // the pump is running: take the socket as a session would, tie it, give it back
    session2 = wait_until([&] { return lease.ActivationArgs(&args2, nullptr, &op2); }, 3000) &&
               connect(args2.preparedUdpSocket, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) == 0;
    n::close_socket(&args2.preparedUdpSocket);
    session2 = session2 && lease.SessionOver(op2);
  }
  lease.Start();  // from this thread while the pump may be reading
  const bool again = wait_until([&] { return observed == 3 && lease.state() == State::Parked; }, 7000);
  stopPump = true;
  pump.join();
  check("...and while the pump reads: deferred to its next turn, observed from a fresh socket, parked",
        session2 && again, "calls=" + std::to_string(calls.load()) + " observed=" + std::to_string(observed.load()));
}

// r2 (Codex D1): the old session's tail is OBSERVED -- what the host still sends to the socket after SessionOver.
void tail_test(FakeHost& host) {
  std::printf("\\n--- r2 D1: the old session's tail is observed, not assumed ---\\n");
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  check("parked; no tail before any session", park(lease) && !lease.snapshot().tailActive && lease.snapshot().strays == 0);
  n::ClientSessionConnectArgs args;
  uint64_t op = 0;
  check("a session over", lease.ActivationArgs(&args, nullptr, &op) && [&] {
    n::close_socket(&args.preparedUdpSocket);
    return lease.SessionOver(op);
  }());
  uint8_t video[600];
  std::memset(video, 0x5A, sizeof(video));  // not a standby datagram: what a still-running session would send
  for (int i = 0; i < 10; ++i) {
    host.SendRaw(video, sizeof(video));
    lease.Pump(20);
  }
  const auto during = lease.snapshot();
  check("the host still sends the old session's datagrams: tail active, counted", during.tailActive && during.strays >= 5,
        "strays=" + std::to_string(during.strays));
  const uint64_t until = mono_ms() + 2500;
  while (mono_ms() < until) lease.Pump(50);
  const auto after = lease.snapshot();
  check("...it stops: no tail any more (an observation, not a proof the grant ended), the count kept",
        !after.tailActive && after.strays == during.strays && lease.state() == State::Parked);
}
""")
rep("""  shared_socket_test(host);
""", """  shared_socket_test(host);
  sync_capability_test(host);
  tail_test(host);
""")
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
