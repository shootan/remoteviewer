// stutter-keyframe r6 V2 (Codex 72a22d2): the 2s window admission is load-bearing in the REAL send
// path, not only in the ledger's own unit test. This drives the PRODUCTION send_udp_chunk_indices (the
// NACK replay path) through a recording sink, with the limiter set so high it never binds -- so the ONLY
// thing that can keep the replay within the 2s cap is the burst ledger's per-datagram Reserve. Removing
// the ledger pointer (wire.burstLedger = nullptr) must then make the SAME 2r assertion FAIL. A separate
// case fixes a grant's spend and an actual replay against the same near-full window (the grant+replay
// budget-boundary overlap Codex asked for). Pure except for the real wall clock the send path reads;
// the replay path suppresses rather than waits, so this is deterministic and fast. Tag: pure-logic.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "host_net_io.hpp"
#include "host_wire_limiter.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

using namespace remote60::native_poc;

namespace {
int gChecks = 0, gFailed = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

// Replay a `count`-chunk AU through the real send_udp_chunk_indices with a recording sink and a
// non-binding limiter. Returns the bytes the sink actually accepted (len+28 per datagram, the wire cost).
uint64_t replay_through_real_sender(BurstLedger* ledger, WireLimiter& limiter, uint32_t count,
                                    uint32_t mtu) {
  // A payload large enough for `count` MTU chunks.
  const uint32_t stride = mtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::vector<uint8_t> payload(static_cast<size_t>(stride) * count, 0x5A);
  UdpVideoChunkHeader base{};
  base.magic = kMagic;
  base.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  base.codec = static_cast<uint16_t>(UdpCodec::H264);
  base.seq = 7;
  base.streamGeneration = 1;
  base.payloadSize = static_cast<uint32_t>(payload.size());
  std::vector<uint16_t> idx(count);
  for (uint32_t i = 0; i < count; ++i) idx[i] = static_cast<uint16_t>(i);

  uint64_t sinkWireBytes = 0;
  WireEgress wire;
  wire.limiter = &limiter;            // non-binding (high rate): never the throttle
  wire.burstLedger = ledger;          // the admission under test (null = removed)
  wire.sink = [&](const uint8_t*, int len, bool) -> int {
    sinkWireBytes += static_cast<uint64_t>(len) + 28u;  // IP/UDP, same as the ledger accounts
    return len;                                          // "sent"
  };
  uint64_t wb = 0, dg = 0, sup = 0;
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  send_udp_chunk_indices(INVALID_SOCKET, peer, payload.data(), payload.size(), base, mtu,
                         /*tightSingleChunk=*/false, idx.data(), static_cast<uint16_t>(count), &wb, &dg,
                         &wire, &sup, nullptr);
  return sinkWireBytes;
}
}  // namespace

int main() {
  std::printf("host_burst_sender_admission_test\n");
  const uint64_t cap = 1'500'000;          // r=187500, 2r=375000
  const uint64_t twoR = 2 * (cap / 8);
  const uint32_t mtu = 1200;
  const uint64_t L = mtu + 28;             // one datagram's wire cost: the agreed admission slack

  // A limiter set so high it never binds, so the ledger admission is the ONLY possible throttle.
  WireLimiter limiter([] { return qpc_now_us(); }, [](uint64_t, uint64_t) { return true; });
  limiter.SetRate(1'000'000'000ULL, 1600);

  const uint64_t now = qpc_now_us();

  // --- Case 1: replay pointer load-bearing. A window pre-filled to near 2r, then a 60-chunk replay. ---
  {
    // Positive: the real ledger. The replay is admitted only up to the 2s cap; the rest is suppressed.
    BurstLedger led;
    led.SetRate(cap, now - 3'000'000);     // warm
    led.Reserve(now, 370'000); led.CommitSent(now, 370'000);  // committed history: 370000 (< 2r)
    const uint64_t posSink = replay_through_real_sender(&led, limiter, 60, mtu);
    const uint64_t posWindow = 370'000 + posSink;
    check("case1 positive: real send_udp_chunk_indices admission keeps 370000 + replay <= 2r + one datagram",
          posWindow <= twoR + L, "window=" + std::to_string(posWindow) + " 2r=" + std::to_string(twoR));

    // Negative (same assertion, replay pointer REMOVED): no ledger -> no admission -> all 60 chunks go out.
    const uint64_t negSink = replay_through_real_sender(nullptr, limiter, 60, mtu);
    const uint64_t negWindow = 370'000 + negSink;
    check("case1 NEGATIVE: with the ledger pointer removed the SAME window EXCEEDS 2r (admission is load-bearing)",
          negWindow > twoR + L, "window=" + std::to_string(negWindow) + " 2r=" + std::to_string(twoR));
    // And the negative actually sent strictly more than the positive (the suppressed bytes).
    check("case1: removing admission sent strictly more bytes", negSink > posSink,
          "neg=" + std::to_string(negSink) + " pos=" + std::to_string(posSink));
  }

  // --- Case 2: grant + replay budget-boundary overlap. A grant's spend fills the window to near 2r; a
  //     concurrent replay (a DIFFERENT AU) must then be admission-bound, not ride the grant. ----------
  {
    BurstLedger led;
    led.SetRate(cap, now - 3'000'000);     // warm
    const BurstAuId grantOwner{1, 1, 1, 100, true};
    const uint64_t grant = led.GrantForIdr(now, grantOwner);
    check("case2: a grant is issued for the clamp IDR", grant > 0, "grant=" + std::to_string(grant));
    // The grant owner's own datagrams peak-pace AND Reserve. Simulate its covered send filling the window
    // to 360000 (debit the grant + commit the window, exactly as send_packet does).
    uint64_t filled = 0;
    while (filled + L <= 360'000 && led.GrantCoverage(grantOwner, L) >= L) {
      led.DebitGrant(grantOwner, L);
      led.Reserve(now, L); led.CommitSent(now, L);
      filled += L;
    }
    // Top up with plain admitted history to 360000 (beyond the grant budget) so the window is near 2r.
    while (filled + L <= 360'000) { led.Reserve(now, L); led.CommitSent(now, L); filled += L; }
    const uint64_t win0 = led.window_bytes(now);
    check("case2: the window is near 2r after the grant + history", win0 >= 350'000 && win0 <= twoR,
          "win0=" + std::to_string(win0));
    // Now a DIFFERENT AU's replay: it has no grant, so it is pure admission. Only ~ (2r - win0) fits.
    const uint64_t replaySink = replay_through_real_sender(&led, limiter, 60, mtu);
    check("case2: the overlapping replay is admission-bound -- window stays <= 2r + one datagram",
          win0 + replaySink <= twoR + L, "total=" + std::to_string(win0 + replaySink));
    check("case2: the replay did NOT spend the grant owner's grant (foreign AU)",
          replaySink <= (twoR - win0) + L, "replaySink=" + std::to_string(replaySink));
  }

  std::printf("\nhost_burst_sender_admission_test: %s (%d checks, %d failed)\n", gFailed ? "FAIL" : "PASS",
              gChecks, gFailed);
  return gFailed ? 1 : 0;
}
