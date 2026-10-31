// Review-only: actual fixed candidate lease plus its existing loopback test endpoints.
#define main candidate_test_main_unused
#include "C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease_test.cpp"
#undef main

int main() {
  FakeHost host;
  Echo directory;
  if (!host.Start() || !directory.Start()) return 2;
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  int calls = 0;
  bool initial = false, after = true;
  auto capability = [&](n::SocketHandle s, n::StandbyCapability* out) {
    const bool observed = n::standby_observe_on(s, "127.0.0.1", directory.port, "obs-sync-review");
    if (++calls == 1) initial = observed; else after = observed;
    if (!observed) return false;
    return host.capability()(s, out);
  };
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, capability, mono_ms);
  if (!park(lease)) return 3;
  n::ClientSessionConnectArgs args;
  uint64_t op = 0;
  if (!lease.ActivationArgs(&args, nullptr, &op)) return 4;
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(host.port);
  inet_pton(AF_INET, "127.0.0.1", &peer.sin_addr);
  if (connect(args.preparedUdpSocket, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) != 0) return 5;
  n::close_socket(&args.preparedUdpSocket);
  if (!lease.SessionOver(op)) return 6;
  lease.Start(); // restart with a fresh capability through the synchronous factory
  std::printf("REVIEW capability_calls=%d initial_observe=%d after_session_observe=%d needs_capability=%d\n",
              calls, initial ? 1 : 0, after ? 1 : 0, lease.needsCapability() ? 1 : 0);
  host.Stop();
  directory.Stop();
  return initial && !after && calls == 2 ? 0 : 1; // 0 means the review failure was reproduced
}
