// Review only: a known Punch from the admitted target, before any activated session.
#define main candidate_test_main_unused
#include "C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease_test.cpp"
#undef main
int main() {
  FakeHost host;
  if (!host.Start()) return 2;
  MemStore store;
  n::GenerationAllocator gens(store, rnd);
  n::StandbyLease lease(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  if (!park(lease)) return 3;
  const auto before = lease.snapshot();
  n::UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(n::UdpPacketKind::Punch);
  host.SendRaw(&packet, sizeof(packet));
  lease.Pump(80);
  const auto after = lease.snapshot();
  std::printf("REVIEW no_session_ever=1 before_strays=%llu after_strays=%llu tail_active=%d\n",
      static_cast<unsigned long long>(before.strays), static_cast<unsigned long long>(after.strays), after.tailActive);
  host.Stop();
  return before.strays == 0 && after.strays > 0 && after.tailActive ? 0 : 1;
}
