// Read-only review probe: use the fixed candidate's real lease and existing loopback test host.
#define main candidate_test_main_unused
#include "C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease_test.cpp"
#undef main

int main() {
  FakeHost host;
  if (!host.Start()) return 2;
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  int confirmed = 0;
  for (int i = 0; i < 12 && confirmed == 0; ++i) {
    n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
    if (!park(lease)) return 3;
    n::ClientSessionConnectArgs args;
    uint64_t op = 0;
    if (!lease.ActivationArgs(&args, nullptr, &op)) return 4;
    {
      const int before = host.activates.load();
      Handshaking h(args.udpHandshake, args.preparedUdpSocket);
      if (!wait_until([&] { return host.activates.load() > before; }, 2000)) return 5;
      const uint32_t gen = host.lastActivateGen.load();
      host.HelloAck(kGoodAck); // only one half of completion; the grant is held back
      std::this_thread::sleep_for(std::chrono::milliseconds(83 + i));
      const bool doneBefore = h.done.load();
      lease.Release();        // cancellation completes before the grant is sent
      host.Pong(gen, true);   // the outstanding Receive may return this packet
      const bool returned = wait_until([&] { return h.done.load(); }, 2000);
      if (!returned) return 6;
      std::printf("case=%d done_before_release=%d handshake_success_after_release=%d state=%d\n",
                  i, doneBefore ? 1 : 0, h.ok ? 1 : 0, static_cast<int>(lease.state()));
      if (!doneBefore && h.ok) ++confirmed;
    }
    n::close_socket(&args.preparedUdpSocket);
    lease.SessionOver(op);
  }
  host.Stop();
  std::printf("cancel_before_final_pong_success_count=%d\n", confirmed);
  return confirmed ? 0 : 1; // 0 means the reviewed defect was reproduced
}
