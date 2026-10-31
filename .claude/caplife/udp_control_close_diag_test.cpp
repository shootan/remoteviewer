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

int main() {
  std::printf("udp_control_close_diag_test (old-ref r1)\n");
  {
    Pair p;
    const auto m = message(37);
    p.a.Send(m.data(), m.size());
    for (int i = 0; i < 200 && !p.a.IsClosed(); ++i) {
      p.pump(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      p.a.Tick();
      p.b.Tick();
    }
    const std::string line = p.a.CloseDiagLine();
    check("a peer that never ACKs: peer-lost", p.a.IsClosed() && p.a.CloseReason() == ControlCloseReason::PeerLost,
          to_string(p.a.CloseReason()));
    check("the line names the head that ran out: seq 1, its MessageHeader type 37, attempts maxAttempts + 1 (4)",
          line.find("lost{seq=1 type=37 attempts=4 ") != std::string::npos, line);
    check("... and that no ACK was accepted", line.find(" accepted=0 ") != std::string::npos);
    check("nothing of the payload past its header is in the line", line.find("NEVER-LOGGED") == std::string::npos);
    // the ACKs the peer did send, arriving only now: counted after the close, not accepted
    // B acknowledged every copy it got; those ACK datagrams were dropped on the way -- delivered only now
    size_t replayed = 0;
    for (auto& d : p.bDropped) { p.a.OnPacket(d.data(), d.size()); ++replayed; }
    const std::string after = p.a.CloseDiagLine();
    check("ACKs arriving after the close: counted (afterClose), still none accepted",
          replayed > 0 && after.find(" afterClose=0 ") == std::string::npos && after.find(" accepted=0 ") != std::string::npos,
          "replayed=" + std::to_string(replayed) + " " + after.substr(0, 160));
  }
  {
    Pair p;
    for (uint16_t t = 40; t < 43; ++t) {
      const auto m = message(t);
      p.a.Send(m.data(), m.size());
    }
    std::vector<uint8_t> got;
    int delivered = 0;
    for (int i = 0; i < 200 && delivered < 3; ++i) {
      p.pump(false);
      p.a.Tick();
      p.b.Tick();
      while (p.b.Receive(&got, 0)) ++delivered;
    }
    for (int i = 0; i < 5; ++i) p.pump(false);
    const std::string line = p.a.CloseDiagLine();
    check("a normal exchange: all three delivered, three ACKs accepted, no head left, no peer-lost",
          delivered == 3 && line.find(" accepted=3 ") != std::string::npos && line.find("head{none}") != std::string::npos &&
              line.find("lost{none}") != std::string::npos && !p.a.IsClosed(), line);
  }
  std::printf("\nudp_control_close_diag_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
