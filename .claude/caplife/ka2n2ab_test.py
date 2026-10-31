p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
anchor = 'void runner_test(FakeHost& host) {\n'
assert s.count(anchor) == 1
block = r'''// r3 N2-a (Codex codex_keepalive_r2_src_r3.md, reproduced there): a Release that finished while the handshake waited
// wins -- the answer that completes the activation, arriving after it, is not adopted (no true, no features). Both
// orders, several times (the wait the Release lands in is the scheduler's; every round must hold).
void cancel_then_answer_test(FakeHost& host) {
  std::printf("\n--- N2-a: Release finished, then the completing answer: not adopted ---\n");
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  check("parked at the fake host", park(lease));
  for (int order = 0; order < 2; ++order) {
    int adopted = 0, rounds = 0, stillActive = 0;
    for (int round = 0; round < 6; ++round) {
      const uint64_t until = mono_ms() + 7000;
      while (mono_ms() < until && lease.state() != State::Parked) lease.Pump(20);
      n::ClientSessionConnectArgs args;
      uint64_t op = 0;
      if (!lease.ActivationArgs(&args, nullptr, &op)) break;
      ++rounds;
      {
        const int before = host.activates.load();
        Handshaking h(args.udpHandshake, args.preparedUdpSocket);
        wait_until([&] { return host.activates > before; }, 2000);
        const uint32_t gen = host.lastActivateGen;
        if (order == 0) host.HelloAck(kGoodAck); else host.Pong(gen, true);  // the first half of the grant
        std::this_thread::sleep_for(std::chrono::milliseconds(60));          // it is in, the handshake waits again
        lease.Release();                                                     // finished before the second half
        if (order == 0) host.Pong(gen, true); else host.HelloAck(kGoodAck);
        wait_until([&] { return h.done.load(); }, 3000);
        if (h.ok || h.features != 0xFFFFFFFFu) ++adopted;
        if (lease.state() == State::Active) ++stillActive;
      }
      n::close_socket(&args.preparedUdpSocket);
      lease.SessionOver(op);  // its Connect failed: the lease reads its socket again
    }
    check(order == 0 ? "HelloAck, Release, then the current Pong: never adopted (6 rounds)"
                     : "Pong, Release, then a valid HelloAck: never adopted (6 rounds)",
          rounds == 6 && adopted == 0 && stillActive == 0,
          "rounds=" + std::to_string(rounds) + " adopted=" + std::to_string(adopted) + " active=" + std::to_string(stillActive));
  }
  const uint64_t until = mono_ms() + 7000;
  while (mono_ms() < until && lease.state() != State::Parked) lease.Pump(20);
  check("...the lease is parked again and activates normally afterwards", lease.state() == State::Parked && [&] {
    n::ClientSessionConnectArgs args;
    uint64_t op = 0;
    if (!lease.ActivationArgs(&args, nullptr, &op)) return false;
    bool ok = false;
    {
      const int before = host.activates.load();
      Handshaking h(args.udpHandshake, args.preparedUdpSocket);
      wait_until([&] { return host.activates > before; }, 2000);
      host.Pong(host.lastActivateGen, true);
      host.HelloAck(kGoodAck);
      ok = wait_until([&] { return h.done.load(); }, 3000) && h.ok && h.features == kGoodAck;
    }
    n::close_socket(&args.preparedUdpSocket);
    lease.SessionOver(op);
    return ok;
  }());
}

// r3 N2-b: a handoff (or an observation) registered while a pump read is in flight is served by the end of THAT read
// -- the pump starts no new one while it is pending. Deterministic: the pump's own read-end hook registers it.
void handoff_priority_test(FakeHost& host) {
  std::printf("\n--- N2-b: a pending handoff / observation stops the next pump read ---\n");
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  check("parked at the fake host", park(lease));
  for (int kind = 0; kind < 2; ++kind) {  // 0: ActivationArgs, 1: ObserveFrom
    std::atomic<bool> armed{true}, waiterRunning{false}, stopPump{false};
    std::atomic<int> readsWhilePending{0}, readsSeen{0};
    std::thread waiter;
    std::atomic<bool> waiterResult{false};
    n::ClientSessionConnectArgs args;
    uint64_t op = 0;
    lease.testHook = [&](int point, int intents) {
      if (point == n::StandbyLease::kTestReadStart) {
        ++readsSeen;
        if (intents > 0 || waiterRunning) ++readsWhilePending;  // a read begun while one waits for the socket
        return;
      }
      if (point != n::StandbyLease::kTestReadDone || !armed.exchange(false)) return;
      // the read is still "in flight" (reading_ set): the waiter registers and sleeps on it
      waiterRunning = true;
      waiter = std::thread([&] {
        bool ok = false;
        if (kind == 0) ok = lease.ActivationArgs(&args, nullptr, &op);
        else ok = lease.ObserveFrom("127.0.0.1", host.port, "obs-test");  // the fake host never answers: false, ~2 s
        waiterResult = ok;
        waiterRunning = false;
      });
      while (lease.pendingIntentsForTest() == 0) std::this_thread::yield();
    };
    std::thread pump([&] {
      while (!stopPump) lease.Pump(30);
    });
    const bool served = wait_until([&] { return !armed && !waiterRunning; }, 6000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stopPump = true;
    pump.join();
    if (waiter.joinable()) waiter.join();
    lease.testHook = nullptr;
    if (kind == 0) {
      check("handoff registered during a read: served at that read's end, no new read began meanwhile",
            served && waiterResult && lease.handedOff() && readsWhilePending == 0,
            "reads begun while pending=" + std::to_string(readsWhilePending.load()));
      n::close_socket(&args.preparedUdpSocket);
      check("...the arguments abandoned: the pump reads again", lease.Abandon(op, nullptr) && !lease.handedOff());
    } else {
      check("observation registered during a read: no pump read until it ended (it gets the socket alone)",
            served && readsWhilePending == 0, "reads begun while pending=" + std::to_string(readsWhilePending.load()));
    }
  }
  const int pings = host.pings;
  const uint64_t until = mono_ms() + 7000;
  while (mono_ms() < until && host.pings == pings) lease.Pump(50);
  check("...and the lease goes on by Ping", host.pings > pings && lease.state() == State::Parked);
}

'''
s = s.replace(anchor, block + anchor)
a = '  hello_ack_test(host);\n  runner_test(host);\n'
assert s.count(a) == 1
s = s.replace(a, '  hello_ack_test(host);\n  cancel_then_answer_test(host);\n  handoff_priority_test(host);\n  runner_test(host);\n')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
