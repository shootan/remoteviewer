// old-ref r1 (Codex codex_old_apk_lane_peer_lost_decision_r1.md): UdpControlChannel's TEST-ONLY ACK / head record
// (REMOTE60_CONTROL_CLOSE_DIAG) -- the line the PinnedSelectTest host appends to "udp control session ended". Two real
// channels wired to each other in memory:
//   * a peer that never ACKs -> peer-lost; the line names the head that ran out (seq, its control TYPE from the
//     MessageHeader, attempts = maxAttempts + 1), ACKs accepted 0;
//   * a normal exchange -> ACKs accepted, no head left, no peer-lost;
//   * ACKs that arrive only after the channel closed are counted (afterClose), not accepted;
//   * nothing of the payload past its header is ever in the line.
// Tag: pure-logic (no socket).
//
//   remote60_udp_control_close_diag_test

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "udp_control_channel.hpp"

using namespace remote60::native_poc;

namespace {
int gFailures = 0, gChecks = 0;
void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}
// a control message as the host sends one: its MessageHeader, then a body that must never be logged
std::vector<uint8_t> message(uint16_t type) {
  const char body[] = "NEVER-LOGGED-BODY-0123456789";
  std::vector<uint8_t> m(sizeof(MessageHeader) + sizeof(body));
  MessageHeader h{};
  h.type = type;
  h.size = static_cast<uint16_t>(m.size());
  std::memcpy(m.data(), &h, sizeof(h));
  std::memcpy(m.data() + sizeof(h), body, sizeof(body));
  return m;
}
struct Pair {
  UdpControlChannel a, b;
  std::vector<std::vector<uint8_t>> aOut, bOut;  // datagrams each side sent, not yet delivered
  std::vector<std::vector<uint8_t>> bDropped;    // B's datagrams that never reached A (its ACKs)
  Pair() {
    a.Configure([this](const void* d, size_t n) { aOut.emplace_back((const uint8_t*)d, (const uint8_t*)d + n); return true; },
                11, 22, 1200);
    b.Configure([this](const void* d, size_t n) { bOut.emplace_back((const uint8_t*)d, (const uint8_t*)d + n); return true; },
                22, 11, 1200);
    UdpControlChannel::Timings t;
    t.retransmitIntervalUs = 2000;
    t.maxAttempts = 3;
    a.SetTimings(t);
    b.SetTimings(t);
  }
  // deliver what each side sent; dropBA: B's datagrams (its ACKs) never reach A
  void pump(bool dropBA) {
    auto ao = std::move(aOut); aOut.clear();
    auto bo = std::move(bOut); bOut.clear();
    for (auto& d : ao) b.OnPacket(d.data(), d.size());
    if (!dropBA) for (auto& d : bo) a.OnPacket(d.data(), d.size());
    else for (auto& d : bo) bDropped.push_back(d);
  }
};
}  // namespace


int main(){Pair p;auto m=message(37);p.a.Send(m.data(),m.size());
 for(int i=0;i<200&&!p.a.IsClosed();++i){p.pump(true);std::this_thread::sleep_for(std::chrono::milliseconds(3));p.a.Tick();p.b.Tick();}
 auto before=p.a.CloseDiagLine();std::this_thread::sleep_for(std::chrono::milliseconds(5));p.a.Tick();auto after=p.a.CloseDiagLine();
 printf("FIRST %s\nLATER %s\n",before.c_str(),after.c_str());return 0;}
