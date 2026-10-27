// keepalive-r2 FINDING r2 (Codex codex_ka_punch_thread_src_r1.md N1): the host agent's punch queue lives exactly one
// Start..Stop. The real HostAgent (directory_client.cpp) with its real Start / Punch / Stop and its punch thread; the
// directory is an address nobody answers on (127.0.0.1:9 -- its cycle only fails and retries); the send function
// records what would go out. Loopback only, nothing is sent anywhere (the recorder sends nothing).

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"

namespace d = remote60::native_poc::directory;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Recorder {
  std::mutex mu;
  std::vector<std::pair<uint16_t, std::string>> sent;  // (port, the proof the punch carried)
  void Add(const void* data, size_t len, const sockaddr_in& to) {
    std::string proof;
    if (len >= 16 + 33) proof = std::string(static_cast<const char*>(data) + 16);  // UdpHelloPacket::authToken (offset 16)
    std::lock_guard<std::mutex> lock(mu);
    sent.emplace_back(ntohs(to.sin_port), proof);
  }
  int To(uint16_t port) {
    std::lock_guard<std::mutex> lock(mu);
    int n = 0;
    for (const auto& s : sent) n += s.first == port ? 1 : 0;
    return n;
  }
  bool ProofSeen(const std::string& proof) {
    std::lock_guard<std::mutex> lock(mu);
    for (const auto& s : sent)
      if (s.second == proof) return true;
    return false;
  }
};
}  // namespace

int main() {
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
  char exe[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string dir(exe);
  dir = dir.substr(0, dir.find_last_of("\\/") + 1);
  const std::string cache = dir + "host_agent_punch_test.cache";  // its own; removed at the end
  DeleteFileA(cache.c_str());

  d::HostAgentConfig cfg;
  cfg.url = "http://127.0.0.1:9";  // nobody answers: the cycle fails and retries, nothing else happens
  cfg.preloadedHostId = "punch-test-host";
  cfg.preloadedHostToken = "punch-test-token";
  cfg.cachePath = cache;
  cfg.heartbeatSeconds = 5;
  in_addr lo{};
  inet_pton(AF_INET, "127.0.0.1", &lo);

  Recorder rec;
  d::HostAgent agent;
  std::string err;
  check("the agent starts (an unreachable directory)",
        agent.Start(cfg, [&](const void* p, size_t n, const sockaddr_in& to) { rec.Add(p, n, to); }, &err), err);

  std::printf("\n--- a Stop in the middle of a train ---\n");
  agent.PunchForTest(lo.s_addr, 40001, "proof-old");
  sleep_ms(450);
  const int during = rec.To(40001);
  check("the train runs beside the cycle (punches out, more queued)", during >= 2 && agent.PendingPunchJobsForTest() == 1,
        "sent=" + std::to_string(during));
  agent.Stop();
  check("Stop: the queue is gone (the rest of the train is discarded, not kept)", agent.PendingPunchJobsForTest() == 0,
        "pending=" + std::to_string(agent.PendingPunchJobsForTest()));
  const int atStop = rec.To(40001);
  sleep_ms(500);
  check("...and nothing more goes out after the Stop", rec.To(40001) == atStop);

  std::printf("\n--- a late Punch after Stop (the cycle's step between its check and its push) ---\n");
  agent.PunchForTest(lo.s_addr, 40003, "proof-late");
  check("a Punch while stopped queues nothing", agent.PendingPunchJobsForTest() == 0);

  std::printf("\n--- the next Start ---\n");
  check("the agent starts again", agent.Start(cfg, [&](const void* p, size_t n, const sockaddr_in& to) { rec.Add(p, n, to); }, &err),
        err);
  sleep_ms(800);
  check("...the old train never comes back: no punch to its target, its host proof not on the wire again",
        rec.To(40001) == atStop && rec.To(40003) == 0 && !rec.ProofSeen("proof-late"),
        "old=" + std::to_string(rec.To(40001) - atStop) + " late=" + std::to_string(rec.To(40003)));
  agent.PunchForTest(lo.s_addr, 40002, "proof-new");
  sleep_ms(450);
  check("...a new train of this Start goes out (with its own proof)", rec.To(40002) >= 2 && rec.ProofSeen("proof-new") &&
        rec.To(40001) == atStop);
  agent.Stop();
  check("Stop again: empty", agent.PendingPunchJobsForTest() == 0);

  DeleteFileA(cache.c_str());
  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT PASS" : "RESULT FAIL");
  WSACleanup();
  return gFailures == 0 ? 0 : 1;
}
