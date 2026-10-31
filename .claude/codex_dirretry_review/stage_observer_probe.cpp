// Review-only probe. Candidate product/test code stays unchanged.
#define main candidate_test_main_unused
#include "C:/work/gnlink-wt/dirretry-r2/apps/native_poc/src/directory_retry_test.cpp"
#undef main

int main() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 2;
  FakeDirectory dir;
  if (!dir.Start()) return 3;
  dir.Script("/api/host/register", {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'd') + "\"}"}});
  dir.Script("/api/host/heartbeat", {
      Reply{401, "{\"error\":\"unknown host token\",\"code\":\"account_inactive\"}"},
      Reply{200, "{\"ok\":true}"}});
  bool reproduced = false;
  {
    // Cache is created and cleaned beside THIS probe, under the repository root.
    StagedHost host(dir, "codex-observer-20261028", false, dir.udpPort());
    std::this_thread::sleep_for(std::chrono::milliseconds(17000)); // delay only the observer
    StagedHost::Cycle c1;
    const bool got = host.Wait(1, &c1);
    const int heartbeatsNow = dir.Count("/api/host/heartbeat");
    const int registrationsNow = dir.Count("/api/host/register");
    const bool candidateAssertion = got && !c1.heartbeatOk && heartbeatsNow == 1 && registrationsNow == 1;
    reproduced = got && !c1.heartbeatOk && heartbeatsNow > 1 && registrationsNow == 1;
    std::printf("REVIEW got_cycle1=%d cycle1_heartbeat_ok=%d live_heartbeat_count=%d live_register_count=%d candidate_stage_assertion=%d\n",
                got ? 1 : 0, c1.heartbeatOk ? 1 : 0, heartbeatsNow, registrationsNow, candidateAssertion ? 1 : 0);
  }
  dir.Stop();
  std::printf("REVIEW mixed_cycle_snapshot_and_live_count_reproduced=%d\n", reproduced ? 1 : 0);
  return reproduced ? 0 : 1; // 0 = the review finding was reproduced, not product PASS
}
