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
#include <utility>
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

// A FRESH, DETERMINISTIC non-binding limiter per call (Codex 89c08de V: the control must not depend on
// machine timing). WireLimiter fills its token bucket from ELAPSED TIME, not at SetRate, so a limiter on
// the real clock starts empty and TryAcquire's success depends on how much wall time has passed -- the
// exact non-determinism the verifier hit. Here the clock is a FAKE value we advance ONCE after SetRate so
// the first refill fills the bucket to its full depth B deterministically; it then stays fixed, so the
// bucket only depletes (never racing the real clock). The rate is huge so B dwarfs any test's bytes --
// the limiter always permits, leaving the 2s WINDOW as the only throttle. enabled() is true (R_!=0) so
// the ledger admission engages. WireLimiter holds a mutex (not movable), so this is a macro, not a
// factory. `name##_t` is captured by reference and must outlive `name` (same local scope).
#define MAKE_NONBINDING_LIMITER(name)                                                            \
  uint64_t name##_t = 0;                                                                         \
  WireLimiter name([&name##_t] { return name##_t; }, [](uint64_t, uint64_t) { return true; });   \
  name.SetRate(1'000'000'000ULL, 1600);                                                          \
  name##_t = 1'000'000'000ULL /* advance the fake clock so the first refill fills B (deterministic) */

// Replay a `count`-chunk AU through the real send_udp_chunk_indices with a recording sink and a fresh
// non-binding limiter. Returns the bytes the sink actually accepted (len+28 per datagram, the wire cost).
uint64_t replay_through_real_sender(BurstLedger* ledger, uint32_t count, uint32_t mtu) {
  MAKE_NONBINDING_LIMITER(limiter);
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

// Send one NORMAL AU (no grant) of ~payloadBytes through the REAL original send function
// (send_udp_chunks_impl). Returns the sink wire bytes. ledger=null => the window is not consulted
// (the C1 bypass). The limiter is non-binding so the only possible throttle is the 2s window.
uint64_t send_normal_through_real_sender(BurstLedger* ledger, uint32_t payloadBytes, uint32_t mtu) {
  MAKE_NONBINDING_LIMITER(limiter);
  std::vector<uint8_t> payload(payloadBytes, 0x33);
  UdpVideoChunkHeader base{};
  base.magic = kMagic;
  base.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  base.codec = static_cast<uint16_t>(UdpCodec::H264);
  base.seq = 9;
  base.streamGeneration = 1;
  base.payloadSize = payloadBytes;
  uint64_t sinkWireBytes = 0;
  WireEgress wire;
  wire.limiter = &limiter;
  wire.burstLedger = ledger;          // null = C1 bypass (window not consulted)
  wire.auHasGrant = false;            // NORMAL traffic (no grant)
  wire.sink = [&](const uint8_t*, int len, bool) -> int {
    sinkWireBytes += static_cast<uint64_t>(len) + 28u;
    return len;
  };
  SendPathStats stats{};
  UdpEgressConfig egress;
  egress.pacePeakBps = 0;             // disable intra-frame pacing so a with-room send is immediate
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  send_udp_chunks_impl(INVALID_SOCKET, peer, payload.data(), payload.size(), base, mtu, &stats,
                       /*liveEpoch=*/nullptr, /*itemEpoch=*/0, egress, &wire);
  return sinkWireBytes;
}
}  // namespace

int main() {
  std::printf("host_burst_sender_admission_test\n");
  const uint64_t cap = 1'500'000;          // r=187500, 2r=375000
  const uint64_t r = cap / 8;              // 187500 bytes/s
  const uint64_t twoR = 2 * (cap / 8);
  const uint32_t mtu = 1200;
  const uint64_t L = mtu + 28;             // one datagram's wire cost: the agreed admission slack

  // A limiter set so high it never binds, so the ledger admission is the ONLY possible throttle.

  const uint64_t now = qpc_now_us();

  // --- Case 1: replay pointer load-bearing. A window pre-filled to near 2r, then a 60-chunk replay. ---
  {
    // Positive: the real ledger. The replay is admitted only up to the 2s cap; the rest is suppressed.
    BurstLedger led;
    led.SetRate(cap, now - 3'000'000);     // warm
    led.Reserve(now, 370'000); led.CommitSent(now, 370'000);  // committed history: 370000 (< 2r)
    const uint64_t posSink = replay_through_real_sender(&led, 60, mtu);
    const uint64_t posWindow = 370'000 + posSink;
    check("case1 positive: real send_udp_chunk_indices admission keeps 370000 + replay <= 2r + one datagram",
          posWindow <= twoR + L, "window=" + std::to_string(posWindow) + " 2r=" + std::to_string(twoR));

    // Negative (same assertion, replay pointer REMOVED): no ledger -> no admission -> all 60 chunks go out.
    const uint64_t negSink = replay_through_real_sender(nullptr, 60, mtu);
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
    const uint64_t replaySink = replay_through_real_sender(&led, 60, mtu);
    check("case2: the overlapping replay is admission-bound -- window stays <= 2r + one datagram",
          win0 + replaySink <= twoR + L, "total=" + std::to_string(win0 + replaySink));
    check("case2: the replay did NOT spend the grant owner's grant (foreign AU)",
          replaySink <= (twoR - win0) + L, "replaySink=" + std::to_string(replaySink));
  }

  // --- Case 3: C1 (Codex 89c08de) -- the REAL original send function (send_udp_chunks_impl) window-
  //     admits NORMAL traffic too, so a prior grant burst + following normal cannot exceed 2r + B + L.
  //     The B1 bound is the WireLimiter 2r + B envelope; B = max(L, r/100). ----------------------------
  {
    // C2 (Codex 597aca2): the B1 bound is 2r + one datagram, NOT 2r + B (the bucket depth B is the
    // legacy strict-only envelope; the unified admission gates every datagram at 2r).
    const uint64_t bound = twoR + L;
    // Positive: a prior grant burst committed 150000; a normal 100000 AU (fits under 2r) sent through the
    // REAL send function is window-ADMITTED (committed into the window), total <= bound.
    BurstLedger led;
    led.SetRate(cap, now - 3'000'000);
    led.Reserve(now, 150'000); led.CommitSent(now, 150'000);  // prior grant burst
    const uint64_t before = led.window_bytes(now);
    const uint64_t posSink = send_normal_through_real_sender(&led, 100'000, mtu);
    const uint64_t afterW = led.window_bytes(now);
    check("case3 C1 positive: the real send_udp_chunks_impl window-ADMITS normal traffic (window grew by it)",
          afterW >= before + posSink - L && posSink > 0, "before=" + std::to_string(before) +
          " after=" + std::to_string(afterW) + " sink=" + std::to_string(posSink));
    check("case3 C1 positive: prior burst + normal stays within 2r + B + L",
          led.max_window_bytes() <= bound, "max=" + std::to_string(led.max_window_bytes()) +
          " bound=" + std::to_string(bound));
    // Negative: the SAME prior 150000 burst, but the normal AU BYPASSES the window (ledger pointer
    // removed). It all goes out, so the effective 2s wire exceeds 2r + B + L -- the C1 arithmetic.
    const uint64_t negSink = send_normal_through_real_sender(nullptr, 300'000, mtu);
    const uint64_t negEffective = 150'000 + negSink;
    check("case3 C1 NEGATIVE: bypassing the window on the real original send path exceeds 2r + B + L",
          negEffective > bound, "effective=" + std::to_string(negEffective) + " bound=" + std::to_string(bound));
  }

  // --- Case 4: D2 (Codex 597aca2) -- DETERMINISTIC real-send grant->quota-exhaust->normal-tail on ONE
  //     injected clock shared by the limiter, the admission loop, the ledger window and the wait. The
  //     independent TX trace's 2s window must stay <= 2r + L (the tail IS window-gated); a regression to
  //     r6 (tail accounted but not gated) would push it past 2r, so this assertion catches r6. The wait
  //     counts no-progress ticks, so the D1 busy-spin (a non-advancing loop) is caught, not hung. --------
  {
    uint64_t clk = 10'000'000;          // shared fake clock (us)
    uint64_t noProgress = 0;            // D1 busy-spin detector: waits that did not advance time
    WireLimiter lim([&] { return clk; }, [](uint64_t, uint64_t) { return true; });
    lim.SetRate(cap, 1600);
    clk += 3'000'000;                   // advance so the bucket fills to B at the real rate (deterministic)
    BurstLedger led;
    led.SetRate(cap, clk - 3'000'000);  // warm (>= 2s of history on the shared clock)
    const BurstAuId owner{1, 1, 1, 200, true};
    const uint64_t grant = led.GrantForIdr(clk, owner);
    check("case4 D2: a grant is issued", grant > 0, "grant=" + std::to_string(grant));

    std::vector<std::pair<uint64_t, uint64_t>> trace;  // (send clk, wire bytes) -- the independent TX trace
    const uint32_t mtu4 = 1200;
    const uint32_t stride = mtu4 - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    std::vector<uint8_t> payload(static_cast<size_t>(stride) * 330, 0x6B);  // ~400KB: grant quota exhausts mid-AU
    UdpVideoChunkHeader base{};
    base.magic = kMagic; base.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
    base.codec = static_cast<uint16_t>(UdpCodec::H264); base.seq = 200; base.streamGeneration = 1;
    base.payloadSize = static_cast<uint32_t>(payload.size());
    WireEgress wire;
    wire.limiter = &lim;
    wire.burstLedger = &led;
    wire.auHasGrant = true;
    wire.burstOwner = owner;
    wire.nowFn = [&] { return clk; };
    wire.waitFn = [&](uint64_t target) { if (target <= clk) { ++noProgress; ++clk; } else clk = target; };
    wire.sink = [&](const uint8_t*, int len, bool) -> int {
      trace.emplace_back(clk, static_cast<uint64_t>(len) + 28u);
      return len;
    };
    SendPathStats st{};
    UdpEgressConfig eg; eg.pacePeakBps = 0;
    sockaddr_in peer{}; peer.sin_family = AF_INET;
    const UdpSendOutcome oc = send_udp_chunks_impl(INVALID_SOCKET, peer, payload.data(), payload.size(), base,
                                                   mtu4, &st, nullptr, 0, eg, &wire);
    check("case4 D2: the AU sent to completion (no busy-spin / hang)", oc == UdpSendOutcome::Sent,
          "datagrams=" + std::to_string(trace.size()));
    check("case4 D2 (D1): the admission loop made progress every wait (no busy-spin)", noProgress == 0,
          "noProgress=" + std::to_string(noProgress));
    // 2s window peak over the independent TX trace.
    uint64_t maxWin = 0;
    for (size_t i = 0; i < trace.size(); ++i) {
      uint64_t sum = 0;
      for (size_t j = i + 1; j-- > 0;) {
        if (trace[i].first - trace[j].first >= 2'000'000ULL) break;
        sum += trace[j].second;
      }
      maxWin = std::max(maxWin, sum);
    }
    check("case4 D2: grant burst + window-gated normal tail keeps every 2s window <= 2r + L (catches r6)",
          maxWin <= twoR + L, "maxWin=" + std::to_string(maxWin) + " 2r+L=" + std::to_string(twoR + L));
  }

  // --- Case 5: E1 (Codex 26ce462) -- the D1 defect was the TIME GAP between two clock reads (the loop's
  //     `now` vs NextReadyUs's own read). case4's clock advances only on WAIT, so consecutive reads are
  //     equal and it cannot reproduce that gap. Here an ADVANCING-READ clock (every read is strictly
  //     later) shows: the FIXED NextReadyUs (0-sentinel) reports ready in bounded steps regardless of the
  //     gap; the OLD "return a fresh nowUs_() and compare deadline <= earlier-snapshot" never reaches
  //     ready -- the busy-spin. (noProgress==0 in case4 alone does NOT prove D1; this does.) ------------
  {
    uint64_t rc = 100'000'000;
    WireLimiter lim([&rc] { return rc += 20'000; }, [](uint64_t, uint64_t) { return true; });  // +20ms every READ
    lim.SetRate(cap, 1600);
    bool everReadyNew = false, everReadyOld = false;
    int stepsToReady = -1;
    for (int i = 0; i < 2000; ++i) {
      const uint64_t nowSnap = rc + 7;                 // the admission loop's own `now` read (advances)
      const uint64_t d = lim.NextReadyUs(1228);        // FIXED: 0 == ready now (no fresh clock read)
      const bool readyNew = (d == 0);
      // OLD behaviour modelled: NextReadyUs returned nowUs_() for the ready case -- a LATER read than the
      // loop's snapshot -- so `deadline <= now` compared a later time against an earlier one.
      const uint64_t dOld = rc + 7;                    // a fresh (later) read, as the old code did
      const bool readyOld = (dOld <= nowSnap);
      if (readyNew && stepsToReady < 0) stepsToReady = i;
      everReadyNew = everReadyNew || readyNew;
      everReadyOld = everReadyOld || readyOld;
    }
    check("case5 E1: fixed NextReadyUs (0-sentinel) reaches ready under an advancing-read clock (bounded)",
          everReadyNew && stepsToReady >= 0, "stepsToReady=" + std::to_string(stepsToReady));
    check("case5 E1: the OLD fresh-now-return comparison NEVER reaches ready (the D1 busy-spin)",
          !everReadyOld);
  }

  // --- Case 6: E2 (Codex 26ce462) -- the SAME input as a grant burst, but the normal TAIL bypasses the
  //     window Reserve while still being SENT/accounted (the r6 defect). The independent TX trace's 2s
  //     window must then FAIL the same 2r + L bound -- so the bound genuinely catches r6, not only the
  //     null-ledger bypass of case3. (Not case3's different-size inputs.) --------------------------------
  {
    uint64_t clk = 10'000'000;
    WireLimiter lim([&] { return clk; }, [](uint64_t, uint64_t) { return true; });
    lim.SetRate(cap, 1600);
    clk += 3'000'000;
    BurstLedger led;
    led.SetRate(cap, clk - 3'000'000);
    const BurstAuId owner{1, 1, 1, 201, true};
    (void)led.GrantForIdr(clk, owner);
    std::vector<std::pair<uint64_t, uint64_t>> trace;
    const uint32_t mtu6 = 1200;
    const uint32_t stride = mtu6 - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    std::vector<uint8_t> payload(static_cast<size_t>(stride) * 150, 0x6C);  // ~grant-quota-sized burst
    UdpVideoChunkHeader base{};
    base.magic = kMagic; base.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
    base.codec = static_cast<uint16_t>(UdpCodec::H264); base.seq = 201; base.streamGeneration = 1;
    base.payloadSize = static_cast<uint32_t>(payload.size());
    WireEgress wire;
    wire.limiter = &lim; wire.burstLedger = &led; wire.auHasGrant = true; wire.burstOwner = owner;
    wire.nowFn = [&] { return clk; };
    wire.waitFn = [&](uint64_t target) { if (target > clk) clk = target; };
    wire.sink = [&](const uint8_t*, int len, bool) -> int {
      trace.emplace_back(clk, static_cast<uint64_t>(len) + 28u);
      return len;
    };
    SendPathStats st{}; UdpEgressConfig eg; eg.pacePeakBps = 0;
    sockaddr_in peer{}; peer.sin_family = AF_INET;
    (void)send_udp_chunks_impl(INVALID_SOCKET, peer, payload.data(), payload.size(), base, mtu6, &st,
                               nullptr, 0, eg, &wire);
    const uint64_t grantWire = [&] { uint64_t s = 0; for (auto& e : trace) s += e.second; return s; }();
    // r6 model: the normal TAIL is SENT/accounted but NOT window-gated -- CommitSent WITHOUT Reserve, at
    // the current clock (no throttle). Append it to the SAME trace.
    const uint64_t tailDg = static_cast<uint64_t>(mtu6) + 28u;
    uint64_t tail = 0;
    while (tail < 220'000) { led.CommitSent(clk, tailDg); trace.emplace_back(clk, tailDg); tail += tailDg; }
    uint64_t maxWin = 0;
    for (size_t i = 0; i < trace.size(); ++i) {
      uint64_t sum = 0;
      for (size_t j = i + 1; j-- > 0;) {
        if (trace[i].first - trace[j].first >= 2'000'000ULL) break;
        sum += trace[j].second;
      }
      maxWin = std::max(maxWin, sum);
    }
    check("case6 E2: an r6 tail (Reserve bypassed, still sent) FAILS the same 2s bound (the bound catches r6)",
          maxWin > twoR + L, "maxWin=" + std::to_string(maxWin) + " 2r+L=" + std::to_string(twoR + L) +
          " grantWire=" + std::to_string(grantWire));
  }

  std::printf("\nhost_burst_sender_admission_test: %s (%d checks, %d failed)\n", gFailed ? "FAIL" : "PASS",
              gChecks, gFailed);
  return gFailed ? 1 : 0;
}
