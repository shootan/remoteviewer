// bitrate-hard-cap r1 -- the hard wire-rate cap, driven through the REAL send path.
//
// Two layers, both deterministic on a fake clock (no sockets, no threads, pure-logic tag):
//   1. the WireLimiter bucket math on its own: depth B = max(Lmax, R/100), refill without
//      truncation drift, SetRate preserves credit (no refill lane), cancel on stop/epoch,
//      TryAcquire suppression.
//   2. the product send functions send_udp_chunks_timed / send_udp_chunk_indices charged through
//      one shared limiter with a recording sink: an overproducing 60 fps stream, a large IDR, a
//      concurrent NACK replay, and a mid-stream rate drop -- every sliding 1 s and 250 ms window of
//      the bytes that actually left (payload + header + 28) is measured and asserted under the cap.
//      Negative controls: the same stream with the limiter off, and with the NACK path NOT sharing
//      the bucket, both overrun the window -- so the window test is detecting the shaper, not luck.
//
// This drives the same Acquire / TryAcquire / SetRate the host calls; it does not re-implement the
// bucket and assert on the re-implementation.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "time_utils.hpp"

#include "host_encode_admission.hpp"
#include "host_net_io.hpp"
#include "host_wire_limiter.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"

using namespace remote60::native_poc;

namespace {
int g_checks = 0, g_failed = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

constexpr uint32_t kMtu = 1200;
constexpr uint64_t kHdr28 = 28;  // IP+UDP, matches host_net_io.cpp kWireIpUdpHeaderBytes

// A fake clock the limiter reads and its wait advances; one per scenario.
struct FakeClock {
  uint64_t nowUs = 1'000'000;  // start away from 0
  uint64_t cancelAt = 0;       // if non-zero, the wait returns "cancelled" once now >= this
  std::function<uint64_t()> nowFn() {
    return [this] { return nowUs; };
  }
  // Advance to the deadline (this is where virtual time passes). Returns false (cancel) if a
  // scenario armed cancelAt and we've reached it, so the limiter re-checks its fence.
  std::function<bool(uint64_t, uint64_t)> waitFn() {
    return [this](uint64_t deadlineUs, uint64_t) -> bool {
      if (cancelAt && nowUs >= cancelAt) return false;
      if (deadlineUs > nowUs) nowUs = deadlineUs;
      return true;
    };
  }
};

struct WireEvent {
  uint64_t t;
  uint64_t bytes;  // on the wire: datagram length + 28
  bool parity;
  bool nack;
};

// Peak bytes in any sliding window of `windowUs`, as a bits/s rate. The max of a sliding window is
// reached at some event's timestamp (its right edge), so test every event as a right edge.
double peak_bps(const std::vector<WireEvent>& ev, uint64_t windowUs) {
  double peak = 0;
  for (size_t j = 0; j < ev.size(); ++j) {
    const uint64_t hi = ev[j].t;
    const uint64_t lo = hi >= windowUs ? hi - windowUs : 0;
    uint64_t sum = 0;
    for (size_t i = 0; i <= j; ++i) {
      if (ev[i].t > lo && ev[i].t <= hi) sum += ev[i].bytes;
    }
    const double bps = static_cast<double>(sum) * 8.0 * 1'000'000.0 / static_cast<double>(windowUs);
    if (bps > peak) peak = bps;
  }
  return peak;
}

UdpVideoChunkHeader base_header(uint32_t seq, bool key) {
  UdpVideoChunkHeader h{};
  h.seq = seq;
  h.streamGeneration = 1;
  h.flags = key ? 0x1u : 0x0u;
  return h;
}

// Drive one AU through the real chunker/paritier with the shared limiter + a recording sink.
UdpSendOutcome send_frame(WireLimiter* limiter, std::vector<WireEvent>* out, FakeClock* clk, uint32_t seq,
                          bool key, size_t auBytes, const std::atomic<uint64_t>* liveEpoch = nullptr,
                          uint64_t itemEpoch = 0) {
  std::vector<uint8_t> payload(auBytes, static_cast<uint8_t>(seq & 0xFF));
  WireEgress wire;
  wire.limiter = limiter;
  wire.sink = [out, clk](const uint8_t*, int len, bool parity) -> int {
    out->push_back({clk->nowUs, static_cast<uint64_t>(len) + kHdr28, parity, false});
    return len;
  };
  SendPathStats stats{};
  UdpEgressConfig egress;  // pacePeakBps 0 => no intra-frame pacing; the limiter alone shapes
  return send_udp_chunks_timed(INVALID_SOCKET, sockaddr_in{}, payload.data(), payload.size(),
                               base_header(seq, key), kMtu, &stats, liveEpoch, itemEpoch, egress, &wire);
}

// ------------------------------------------------------------------ 1. the bucket math
void test_bucket_math() {
  std::printf("\n--- the bucket math ---\n");
  const uint32_t lmax = clamp_udp_mtu(kMtu) + 28u;  // 1228
  {
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(6'000'000, lmax);  // R = 750000 B/s; B = max(1228, 7500) = 7500
    check("6 Mbps: R = capBps/8", lim.rate_bytes_for_test() == 750000, std::to_string(lim.rate_bytes_for_test()));
    check("6 Mbps: B = max(Lmax, R/100) = 7500", lim.depth_for_test() == 7500, std::to_string(lim.depth_for_test()));
  }
  {
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(1'200'000, lmax);  // R=150000, R/100=1500 > Lmax(1228) -> B=1500
    check("1.2 Mbps: B = R/100 = 1500", lim.depth_for_test() == 1500, std::to_string(lim.depth_for_test()));
    lim.SetRate(900'000, lmax);  // R=112500, R/100=1125 < Lmax -> B=Lmax=1228 (one datagram floor)
    check("0.9 Mbps: B floors at Lmax = 1228 (a single datagram always fits)",
          lim.depth_for_test() == lmax, std::to_string(lim.depth_for_test()));
  }
  {
    // Refill has no truncation drift: after N us the tokens are exactly floor(N*R/1e6), carried.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(6'000'000, lmax);  // R=750000 => 0.75 bytes/us
    lim.TryAcquire(7500);          // drain the full bucket
    check("bucket drained to 0", lim.tokens_for_test() == 0, std::to_string(lim.tokens_for_test()));
    // advance 1000 us in 1-us steps; a naive (elapsed*R/1e6) per step would add 0 each time (0.75->0)
    for (int i = 0; i < 1000; ++i) {
      clk.nowUs += 1;
      (void)lim.tokens_for_test();  // forces a refill each microsecond
    }
    // 1000 us at 750000 B/s = 750 bytes, exactly, despite per-us sub-byte refills.
    check("no truncation drift over 1000x 1us refills: 750 bytes", lim.tokens_for_test() == 750,
          std::to_string(lim.tokens_for_test()));
  }
  {
    // SetRate preserves credit -- a rate change is not a refill lane.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(6'000'000, lmax);
    lim.TryAcquire(7500);  // empty
    check("empty before rate change", lim.tokens_for_test() == 0);
    lim.SetRate(1'500'000, lmax);  // lower the rate at the same instant
    check("rate DOWN does not refill the bucket (still ~0, not B)", lim.tokens_for_test() < 100,
          std::to_string(lim.tokens_for_test()));
    lim.SetRate(12'000'000, lmax);  // raise it
    check("rate UP does not refill the bucket either", lim.tokens_for_test() < 100,
          std::to_string(lim.tokens_for_test()));
  }
  {
    // A decrease clamps credit down to the new, smaller depth.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(12'000'000, lmax);  // B = 15000
    check("full after SetRate... no -- starts at 0", lim.tokens_for_test() == 0);
    clk.nowUs += 1'000'000;  // 1 s -> fill to B=15000
    check("filled to B=15000 after 1 s", lim.tokens_for_test() == 15000, std::to_string(lim.tokens_for_test()));
    lim.SetRate(6'000'000, lmax);  // new B=7500 -> credit clamped down
    check("decrease clamps credit to the new depth 7500", lim.tokens_for_test() == 7500,
          std::to_string(lim.tokens_for_test()));
  }
  {
    // Cancel on epoch and on stop.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(6'000'000, lmax);
    lim.TryAcquire(7500);  // empty; next Acquire must wait
    std::atomic<uint64_t> epoch{5};
    clk.cancelAt = clk.nowUs;  // the wait returns "cancelled" immediately
    epoch.store(6);            // epoch moved off itemEpoch=5
    check("Acquire returns Cancelled when the epoch moved during the wait",
          lim.Acquire(2000, &epoch, 5) == WireLimiter::Acq::Cancelled);
    FakeClock clk2;
    WireLimiter lim2(clk2.nowFn(), clk2.waitFn());
    lim2.SetRate(6'000'000, lmax);
    lim2.TryAcquire(7500);
    clk2.cancelAt = clk2.nowUs;
    lim2.Stop();
    check("Acquire returns Cancelled after Stop()", lim2.Acquire(2000, nullptr, 0) == WireLimiter::Acq::Cancelled);
  }
  {
    // Disabled cap (capBps 0): everything permitted, nothing shaped.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(0, lmax);
    check("disabled: Acquire permits at once", lim.Acquire(1'000'000, nullptr, 0) == WireLimiter::Acq::Permitted);
    check("disabled: TryAcquire always true", lim.TryAcquire(1'000'000));
    check("disabled: enabled() false", !lim.enabled());
  }
}

// ------------------------------------------------------------------ 2. the real send path, windows
// Feed an overproducing 60 fps stream so the limiter is always the binding constraint, plus one IDR.
// Returns the recorded wire events.
std::vector<WireEvent> run_stream(WireLimiter* lim, FakeClock* clk, uint64_t capBps, int frames,
                                  bool withNack) {
  std::vector<WireEvent> ev;
  const uint64_t frameUs = 16'667;  // 60 fps
  // Each delta is ~2x the per-frame cap share (the encoder overshoots on motion); one IDR early.
  const size_t deltaBytes = static_cast<size_t>(capBps / 8 / 60 * 2);  // ~2x cap/60
  const size_t idrBytes = static_cast<size_t>(capBps / 8 / 4);          // ~250 ms of payload
  uint64_t enqueueUs = clk->nowUs;
  for (int f = 0; f < frames; ++f) {
    enqueueUs += frameUs;
    if (clk->nowUs < enqueueUs) clk->nowUs = enqueueUs;  // the frame becomes available at its cadence
    const bool key = (f == 10);
    send_frame(lim, &ev, clk, static_cast<uint32_t>(f), key, key ? idrBytes : deltaBytes);
    if (withNack && f == 30) {
      // A NACK replay of 20 chunks of an earlier AU, through the SAME bucket (non-blocking).
      std::vector<uint8_t> payload(deltaBytes, 0x5A);
      std::vector<uint16_t> idx;
      for (uint16_t i = 0; i < 20; ++i) idx.push_back(i);
      WireEgress wire;
      wire.limiter = lim;
      wire.sink = [&ev, clk](const uint8_t*, int len, bool) -> int {
        ev.push_back({clk->nowUs, static_cast<uint64_t>(len) + kHdr28, false, true});
        return len;
      };
      uint64_t wb = 0, dg = 0, sup = 0;
      send_udp_chunk_indices(INVALID_SOCKET, sockaddr_in{}, payload.data(), payload.size(),
                             base_header(1, false), kMtu, true, idx.data(), 20, &wb, &dg, &wire, &sup);
    }
  }
  return ev;
}

void test_real_path_windows() {
  std::printf("\n--- the real send path: sliding windows under the cap ---\n");
  const uint32_t lmax = clamp_udp_mtu(kMtu) + 28u;
  const uint64_t cap = 6'000'000;
  {
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(cap, lmax);
    const auto ev = run_stream(&lim, &clk, cap, 240, /*withNack=*/true);  // 4 s of 60 fps
    const double p1s = peak_bps(ev, 1'000'000);
    const double p250 = peak_bps(ev, 250'000);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "1s=%.0f (%.1f%%) 250ms=%.0f (%.1f%%) cap=%llu events=%zu",
                  p1s, 100.0 * p1s / cap, p250, 100.0 * p250 / cap, (unsigned long long)cap, ev.size());
    check("CAP HELD: every 1 s window <= cap +10%", p1s <= cap * 1.10, buf);
    check("CAP HELD: every 250 ms window <= cap +10%", p250 <= cap * 1.10, buf);
    check("tight: 1 s window <= cap +1.5%", p1s <= cap * 1.015, buf);
    check("tight: 250 ms window <= cap +5%", p250 <= cap * 1.05, buf);
    check("the stream actually ran (data and FEC parity reached the sink)",
          ev.size() > 500 && std::any_of(ev.begin(), ev.end(), [](const WireEvent& e) { return e.parity; }));
    // Under saturation the data stream keeps the bucket empty, so the NACK replay (non-blocking)
    // is charged to the SAME bucket and fully suppressed -- it never opens a lane around the cap.
    check("under saturation the NACK replay is suppressed by the shared bucket (no free lane)",
          lim.suppressed_count() > 0 && std::none_of(ev.begin(), ev.end(), [](const WireEvent& e) { return e.nack; }),
          "suppressed=" + std::to_string(lim.suppressed_count()));
  }
  {
    // Non-saturated stream (~0.5x cap): the bucket has slack, so a NACK replay DOES go out, shares
    // the budget, and the window still holds -- the shared bucket is a budget, not an on/off gate.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(cap, lmax);
    std::vector<WireEvent> ev;
    const uint64_t frameUs = 16'667;
    uint64_t enqueueUs = clk.nowUs;
    for (int f = 0; f < 240; ++f) {
      enqueueUs += frameUs;
      if (clk.nowUs < enqueueUs) clk.nowUs = enqueueUs;
      const size_t deltaBytes = static_cast<size_t>(cap / 8 / 60 / 2);  // ~0.5x cap/60: slack
      send_frame(&lim, &ev, &clk, static_cast<uint32_t>(f), false, deltaBytes);
      if (f % 20 == 10) {
        clk.nowUs += 10'000;  // a NACK arrives ~10 ms into the idle gap: the bucket has refilled
        std::vector<uint8_t> payload(static_cast<size_t>(cap / 8 / 60), 0x5A);
        std::vector<uint16_t> idx;
        for (uint16_t i = 0; i < 5; ++i) idx.push_back(i);
        WireEgress wire;
        wire.limiter = &lim;
        wire.sink = [&ev, &clk](const uint8_t*, int len, bool) -> int {
          ev.push_back({clk.nowUs, static_cast<uint64_t>(len) + kHdr28, false, true});
          return len;
        };
        uint64_t wb = 0, dg = 0, sup = 0;
        send_udp_chunk_indices(INVALID_SOCKET, sockaddr_in{}, payload.data(), payload.size(),
                               base_header(f, false), kMtu, true, idx.data(), 5, &wb, &dg, &wire, &sup);
      }
    }
    const double p1s = peak_bps(ev, 1'000'000);
    const bool nackWent = std::any_of(ev.begin(), ev.end(), [](const WireEvent& e) { return e.nack; });
    char buf[128];
    std::snprintf(buf, sizeof(buf), "nackWent=%d 1s=%.0f (%.1f%%)", nackWent ? 1 : 0, p1s, 100.0 * p1s / cap);
    check("with slack the NACK replay goes out AND the window still holds under the cap",
          nackWent && p1s <= cap * 1.10, buf);
  }
  {
    // Mid-stream rate drop 6 -> 1.5 Mbps: windows that lie wholly after the switch respect 1.5.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(cap, lmax);
    std::vector<WireEvent> ev;
    const uint64_t frameUs = 16'667;
    uint64_t enqueueUs = clk.nowUs;
    uint64_t switchUs = 0;
    for (int f = 0; f < 300; ++f) {
      enqueueUs += frameUs;
      if (clk.nowUs < enqueueUs) clk.nowUs = enqueueUs;
      if (f == 150) {
        lim.SetRate(1'500'000, lmax);
        switchUs = clk.nowUs;
      }
      const uint64_t activeCap = f < 150 ? cap : 1'500'000;
      const size_t deltaBytes = static_cast<size_t>(activeCap / 8 / 60 * 2);
      send_frame(&lim, &ev, &clk, static_cast<uint32_t>(f), false, deltaBytes);
    }
    // Windows fully after switchUs + 1 s settle.
    std::vector<WireEvent> after;
    for (const auto& e : ev)
      if (e.t >= switchUs + 1'100'000) after.push_back(e);
    const double p1s = peak_bps(after, 1'000'000);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "after-switch 1s=%.0f (%.1f%% of 1.5M) events=%zu", p1s,
                  100.0 * p1s / 1'500'000.0, after.size());
    check("after a drop to 1.5 Mbps, windows wholly in the new rate are <= 1.5 Mbps +10%",
          !after.empty() && p1s <= 1'500'000 * 1.10, buf);
  }
}

// ------------------------------------------------------------------ 3. negative controls
void test_negative_controls() {
  std::printf("\n--- negative controls (the window test detects the shaper, not luck) ---\n");
  const uint32_t lmax = clamp_udp_mtu(kMtu) + 28u;
  const uint64_t cap = 6'000'000;
  {
    // Limiter OFF: the same overproducing stream bursts well over the cap in a 250 ms window.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(0, lmax);  // disabled
    const auto ev = run_stream(&lim, &clk, cap, 240, false);
    const double p250 = peak_bps(ev, 250'000);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "250ms=%.0f (%.0f%% of cap)", p250, 100.0 * p250 / cap);
    check("NEGATIVE: with the cap OFF the overproducing stream exceeds cap +10% (shaper is what holds it)",
          p250 > cap * 1.10, buf);
  }
  {
    // NACK NOT sharing the bucket (the old separate-lane bug): a NACK burst on top of a capped data
    // stream pushes the TOTAL over the cap, because the replay was not charged to the same bucket.
    FakeClock clk;
    WireLimiter lim(clk.nowFn(), clk.waitFn());
    lim.SetRate(cap, lmax);
    std::vector<WireEvent> ev;
    const uint64_t frameUs = 16'667;
    uint64_t enqueueUs = clk.nowUs;
    for (int f = 0; f < 120; ++f) {
      enqueueUs += frameUs;
      if (clk.nowUs < enqueueUs) clk.nowUs = enqueueUs;
      const size_t deltaBytes = static_cast<size_t>(cap / 8 / 60 * 2);
      send_frame(&lim, &ev, &clk, static_cast<uint32_t>(f), false, deltaBytes);
      // A heavy NACK burst that BYPASSES the limiter (sink only, no TryAcquire) every frame.
      std::vector<uint8_t> payload(cap / 8 / 60 * 2, 0x5A);
      const uint32_t stride = clamp_udp_mtu(kMtu) - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
      std::vector<uint16_t> idx;
      for (uint16_t i = 0; i < 10; ++i) idx.push_back(i);
      WireEgress bypass;  // NO limiter -> the replay is not charged to the shared bucket
      bypass.sink = [&ev, &clk](const uint8_t*, int len, bool) -> int {
        ev.push_back({clk.nowUs, static_cast<uint64_t>(len) + kHdr28, false, true});
        return len;
      };
      (void)stride;
      uint64_t wb = 0, dg = 0;
      send_udp_chunk_indices(INVALID_SOCKET, sockaddr_in{}, payload.data(), payload.size(),
                             base_header(f, false), kMtu, true, idx.data(), 10, &wb, &dg, &bypass, nullptr);
    }
    const double p1s = peak_bps(ev, 1'000'000);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "1s=%.0f (%.0f%% of cap)", p1s, 100.0 * p1s / cap);
    check("NEGATIVE: a NACK lane that bypasses the shared bucket pushes the total over the cap",
          p1s > cap * 1.10, buf);
  }
}

// ------------------------------------------------------------------ 4. the encode-input gate
// A faithful model of the gate's effect, driving the REAL decide_encode_admission: the encoder
// overproduces (a frame every tick, each larger than the wire drains in a tick), the wire drains a
// fraction of a frame per tick. With the gate the sender queue stays bounded (fps drops); with it
// off (the pre-gate behaviour) the queue grows without bound -- which is the overflow -> resync-IDR
// loop the gate exists to pre-empt. This exercises the product gate, not a re-implementation of it.
struct QueueModel {
  double queue = 0;        // sender-queue depth (fractional: AUs waiting)
  double mftPending = 0;   // inputs accepted by the MFT, not yet drained
  double drainPerTick;     // AUs the wire can clear per tick (< 1 when overproducing)
  uint32_t maxQueueSeen = 0;
  uint64_t idrRequests = 0;  // a DropAndResync (queue overflow past 2) would request an IDR
  void tick(bool gateOn) {
    // Drain first (the sender thread sends what the wire allows this tick).
    queue = std::max(0.0, queue - drainPerTick);
    // The MFT releases one accepted input as a queued AU per tick (steady async cadence).
    if (mftPending >= 1.0) {
      mftPending -= 1.0;
      queue += 1.0;
    }
    // Admit (or skip) a new capture frame.
    EncodeAdmissionInputs adm;
    adm.wireCapActive = gateOn;
    adm.keyWanted = false;
    adm.servedBootstrap = false;
    adm.senderQueueDepth = static_cast<uint32_t>(queue + 0.5);
    adm.mftPendingDepth = static_cast<uint32_t>(mftPending + 0.5);
    if (decide_encode_admission(adm) == EncodeAdmission::Admit) {
      mftPending += 1.0;  // the frame is accepted by the MFT (will drain into the queue next ticks)
    }
    // Overflow past the 2-frame queue is what the product clears with a resync IDR.
    if (queue > 2.0) {
      ++idrRequests;
      queue = 0;  // DropAndResync clears the backlog
    }
    maxQueueSeen = std::max(maxQueueSeen, static_cast<uint32_t>(queue + 0.5));
  }
};

void test_admission_gate() {
  std::printf("\n--- the encode-input gate (decide_encode_admission) ---\n");
  // The pure decisions, every branch.
  auto admit = [](EncodeAdmissionInputs in) { return decide_encode_admission(in) == EncodeAdmission::Admit; };
  EncodeAdmissionInputs base;
  base.wireCapActive = true;
  base.senderQueueMax = 2;
  base.mftPendingMax = 4;
  check("cap off: always admit (legacy)", admit([&] { auto i = base; i.wireCapActive = false; i.senderQueueDepth = 99; return i; }()));
  check("a keyframe is admitted even when backlogged", admit([&] { auto i = base; i.keyWanted = true; i.senderQueueDepth = 9; return i; }()));
  check("a bootstrap synthetic is admitted even when backlogged",
        admit([&] { auto i = base; i.servedBootstrap = true; i.senderQueueDepth = 9; return i; }()));
  check("a full sender queue skips a delta", !admit([&] { auto i = base; i.senderQueueDepth = 2; return i; }()));
  check("a backed-up MFT skips a delta", !admit([&] { auto i = base; i.mftPendingDepth = 4; return i; }()));
  check("an idle wire admits", admit([&] { auto i = base; i.senderQueueDepth = 0; i.mftPendingDepth = 1; return i; }()));

  // The effect: overproduction (each frame needs ~2 ticks of wire) -- the queue stays bounded with
  // the gate, and the resync-IDR path is never taken.
  {
    QueueModel m;
    m.drainPerTick = 0.5;  // the wire clears half a frame per tick (2x overproduction)
    for (int t = 0; t < 600; ++t) m.tick(/*gateOn=*/true);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "maxQueue=%u idrRequests=%llu", m.maxQueueSeen, (unsigned long long)m.idrRequests);
    check("GATE ON: the sender queue stays bounded and no resync IDR is forced", m.maxQueueSeen <= 2 && m.idrRequests == 0, buf);
  }
  {
    // NEGATIVE: gate off -> the queue overflows and the resync-IDR loop fires repeatedly.
    QueueModel m;
    m.drainPerTick = 0.5;
    for (int t = 0; t < 600; ++t) m.tick(/*gateOn=*/false);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "idrRequests=%llu", (unsigned long long)m.idrRequests);
    check("NEGATIVE: with the gate off the overproducing stream forces repeated resync IDRs", m.idrRequests > 10, buf);
  }
}

// ------------------------------------------------------------------ 5. real OS UDP, real clock
// The deterministic layers above measure the shaper's own maths. This one measures the OS network
// observation the contract asks for separately: the real send path driving a REAL loopback UDP
// socket, the limiter on the real qpc clock with a real cancellable sleep, a receiver thread that
// timestamps every datagram by its arrival (qpc) and length, and the sliding 1 s / 250 ms windows
// computed over those arrivals. Tag: network (loopback), display/gpu independent. ~3 s.
void test_real_udp_window() {
  std::printf("\n--- real OS UDP loopback, real clock: sliding windows under the cap ---\n");
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    check("WSAStartup", false, "WSAStartup failed");
    return;
  }
  SOCKET rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  SOCKET tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(rx, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  int alen = sizeof(addr);
  getsockname(rx, reinterpret_cast<sockaddr*>(&addr), &alen);  // the OS-chosen port
  int rcvbuf = 8 * 1024 * 1024;
  setsockopt(rx, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
  DWORD to = 500;
  setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));

  std::vector<WireEvent> ev;
  std::atomic<bool> rxStop{false};
  std::thread rxThread([&] {
    std::vector<char> buf(2048);
    for (;;) {
      const int n = recv(rx, buf.data(), static_cast<int>(buf.size()), 0);
      const uint64_t t = qpc_now_us();
      if (n > 0) {
        const bool parity = n >= 2 && (static_cast<uint8_t>(buf[0]) & 0x10u);  // header flags low byte
        ev.push_back({t, static_cast<uint64_t>(n) + kHdr28, parity, false});
      } else if (rxStop.load()) {
        break;
      }
    }
  });

  const uint64_t cap = 6'000'000;
  const uint32_t lmax = clamp_udp_mtu(kMtu) + 28u;
  WireLimiter lim([] { return qpc_now_us(); },
                  [&lim](uint64_t deadlineUs, uint64_t seq) -> bool {
                    for (;;) {
                      const uint64_t now = qpc_now_us();
                      if (now >= deadlineUs) return true;
                      if (lim.cancel_seq() != seq || lim.stopped()) return false;
                      udp_pace_wait_until(std::min<uint64_t>(deadlineUs, now + 1000ULL));
                    }
                  });
  lim.SetRate(cap, lmax);
  // Overproduce ~2x the cap at 60 fps for ~3 s; the limiter is the binding constraint (its real
  // waits pace the sender), so the output rate on the wire is the cap.
  const size_t deltaBytes = static_cast<size_t>(cap / 8 / 60 * 2);
  WireEgress wire;
  wire.limiter = &lim;  // no sink -> real sendto
  const uint64_t t0 = qpc_now_us();
  int seq = 0;
  while (qpc_now_us() - t0 < 3'000'000) {
    std::vector<uint8_t> payload(deltaBytes, static_cast<uint8_t>(seq & 0xFF));
    SendPathStats st{};
    UdpEgressConfig egress;
    send_udp_chunks_timed(tx, addr, payload.data(), payload.size(), base_header(seq, seq == 5), kMtu, &st,
                          nullptr, 0, egress, &wire);
    ++seq;
  }
  // Let the receiver drain, then stop it.
  udp_pace_wait_until(qpc_now_us() + 300'000);
  rxStop.store(true);
  lim.Stop();
  closesocket(tx);
  // nudge the recv timeout
  udp_pace_wait_until(qpc_now_us() + 600'000);
  if (rxThread.joinable()) rxThread.join();
  closesocket(rx);
  WSACleanup();

  if (ev.size() < 200) {
    check("real-UDP: received enough datagrams to measure", false,
          "only " + std::to_string(ev.size()) + " received");
    return;
  }
  const double p1s = peak_bps(ev, 1'000'000);
  const double p250 = peak_bps(ev, 250'000);
  char buf[192];
  std::snprintf(buf, sizeof(buf), "1s=%.0f (%.1f%%) 250ms=%.0f (%.1f%%) received=%zu over ~3s", p1s,
                100.0 * p1s / cap, p250, 100.0 * p250 / cap, ev.size());
  check("real-UDP: every 1 s window <= cap +10%", p1s <= cap * 1.10, buf);
  check("real-UDP: every 250 ms window <= cap +10%", p250 <= cap * 1.10, buf);
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const bool skipNet = argc > 1 && std::string(argv[1]) == "--no-net";
  test_bucket_math();
  test_real_path_windows();
  test_admission_gate();
  test_negative_controls();
  if (!skipNet) test_real_udp_window();  // network (loopback); --no-net to skip
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
