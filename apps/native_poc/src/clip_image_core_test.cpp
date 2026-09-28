// clip-image: the pure core -- limits, DIB header check, bulk demux and generations, bulk rate.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bulk_rate_controller.hpp"
#include "clip_image_core.hpp"

using namespace remote60::native_poc;

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}
constexpr uint64_t MiB = 1024ull * 1024ull;

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) { std::memcpy(b.data() + at, &v, 2); }

// A packed DIB: header of `hs` bytes, optional masks, palette, then rows.
std::vector<uint8_t> make_dib(uint32_t hs, int32_t w, int32_t h, uint16_t bpp, uint32_t comp, uint32_t clrUsed = 0,
                              uint32_t csType = kLcsSrgb, bool withRows = true) {
  const uint64_t stride = ((uint64_t)w * bpp + 31) / 32 * 4;
  const uint32_t absH = (uint32_t)(h < 0 ? -h : h);
  uint64_t size = hs;
  if (comp == kDibBiBitfields && hs == 40) size += 12;
  const uint32_t pal = bpp <= 8 ? (clrUsed ? clrUsed : (1u << bpp)) : clrUsed;
  size += (uint64_t)pal * 4;
  if (withRows) size += stride * absH;
  std::vector<uint8_t> b((size_t)size, 0);
  put32(b, 0, hs);
  put32(b, 4, (uint32_t)w);
  put32(b, 8, (uint32_t)h);
  put16(b, 12, 1);
  put16(b, 14, bpp);
  put32(b, 16, comp);
  put32(b, 32, clrUsed);
  if (hs >= 108) put32(b, 56, csType);
  return b;
}
}  // namespace

int main() {
  // ------------------------------------------------------------------ gate
  {
    const ClipImageGate g = clip_image_gate(8192, 2048, 16 * MiB, 1 * MiB);
    check("8192x2048 (64 MiB decoded) with a 16 MiB PNG and 1 MiB text passes: the stated boundary",
          g.ok() && g.decodedBytes == 64 * MiB && g.receiverPeakBytes == 32 * MiB + 1 * MiB + 64 * MiB + 1 * MiB);
    check("8192x2049 is refused (decoded over 64 MiB)", clip_image_gate(8192, 2049, 1000, 0).reason == ClipImageGateReason::DecodedTooLarge);
    check("8193 x 1 is refused (side over 8192)", clip_image_gate(8193, 1, 1000, 0).reason == ClipImageGateReason::SideTooLarge);
    check("8192x4096 (128 MiB) is refused, not a boundary", clip_image_gate(8192, 4096, 50000, 0).reason == ClipImageGateReason::DecodedTooLarge);
    check("a tiny PNG of 8192x2048 is judged by its decoded size, and passes", clip_image_gate(8192, 2048, 40000, 0).ok());
    check("a tiny PNG of 8192x4096 is refused on its dimensions alone", !clip_image_gate(8192, 4096, 40000, 0).ok());
    check("PNG over 16 MiB is refused", clip_image_gate(100, 100, 16 * MiB + 1, 0).reason == ClipImageGateReason::PngTooLarge);
    check("PNG of exactly 16 MiB passes", clip_image_gate(100, 100, 16 * MiB, 0).ok());
    check("text over 1 MiB is refused", clip_image_gate(10, 10, 100, 1 * MiB + 2).reason == ClipImageGateReason::TextTooLarge);
    check("zero size is refused", clip_image_gate(0, 10, 100, 0).reason == ClipImageGateReason::ZeroSize);
    check("a 32-bit product would overflow: computed in 64 bits", clip_image_gate(8192, 8192, 10, 0).decodedBytes == 256 * MiB);
  }
  // ------------------------------------------------------------------ DIB
  {
    auto d = make_dib(40, 4, 3, 24, kDibBiRgb);
    const DibInfo i = validate_dib(d.data(), d.size());
    check("24 bpp BI_RGB 4x3: stride rounds to 4 bytes (12), rows after the header", i.ok() && i.stride == 12 && i.pixelOffset == 40 && !i.topDown);
    auto td = make_dib(40, 4, -3, 32, kDibBiRgb);
    check("negative height is top-down", validate_dib(td.data(), td.size()).topDown);
    auto bf = make_dib(40, 2, 2, 32, kDibBiBitfields);
    const DibInfo ib = validate_dib(bf.data(), bf.size());
    check("BI_BITFIELDS with a 40-byte header: masks after it, rows after the masks", ib.ok() && ib.pixelOffset == 52);
    auto pal = make_dib(40, 8, 2, 8, kDibBiRgb);
    check("8 bpp without clrUsed has 256 palette entries", validate_dib(pal.data(), pal.size()).paletteEntries == 256);
    auto palBad = make_dib(40, 8, 2, 8, kDibBiRgb, 300);
    check("more palette entries than 8 bpp can index is refused", validate_dib(palBad.data(), palBad.size()).reason == DibReason::BadPalette);
    auto v5 = make_dib(124, 4, 4, 32, kDibBiBitfields);
    check("V5 sRGB with its masks inside the header", validate_dib(v5.data(), v5.size()).ok());
    auto v5emb = make_dib(124, 4, 4, 32, kDibBiRgb, 0, 0x4D424544u /* 'MBED' */);
    check("V5 with an embedded profile is refused (never recoloured)", validate_dib(v5emb.data(), v5emb.size()).reason == DibReason::UnsupportedColorSpace);
    auto v5cal = make_dib(124, 4, 4, 32, kDibBiRgb, 0, kLcsCalibratedRgb);
    check("V5 calibrated RGB is refused", validate_dib(v5cal.data(), v5cal.size()).reason == DibReason::UnsupportedColorSpace);
    auto trunc = make_dib(40, 100, 100, 24, kDibBiRgb, 0, kLcsSrgb, false);
    check("rows missing: truncated", validate_dib(trunc.data(), trunc.size()).reason == DibReason::Truncated);
    auto jpg = make_dib(40, 4, 4, 24, 4 /* BI_JPEG */);
    check("BI_JPEG is refused", validate_dib(jpg.data(), jpg.size()).reason == DibReason::BadCompression);
    auto bf24 = make_dib(40, 4, 4, 24, kDibBiBitfields);
    check("BI_BITFIELDS at 24 bpp is refused", validate_dib(bf24.data(), bf24.size()).reason == DibReason::BadCompression);
    auto hs = make_dib(40, 4, 4, 24, kDibBiRgb);
    put32(hs, 0, 64);
    check("an unknown header size is refused", validate_dib(hs.data(), hs.size()).reason == DibReason::BadHeaderSize);
    auto huge = make_dib(40, 1, 1, 32, kDibBiRgb);
    put32(huge, 4, 0x7FFFFFFF);
    put32(huge, 8, 0x7FFFFFFF);
    check("a wrap-sized header is refused before any multiplication matters", validate_dib(huge.data(), huge.size()).reason == DibReason::BadDimensions);
    auto bpp = make_dib(40, 4, 4, 24, kDibBiRgb);
    put16(bpp, 14, 12);
    check("12 bpp is refused", validate_dib(bpp.data(), bpp.size()).reason == DibReason::BadBitCount);
    auto planes = make_dib(40, 4, 4, 24, kDibBiRgb);
    put16(planes, 12, 2);
    check("planes != 1 is refused", validate_dib(planes.data(), planes.size()).reason == DibReason::BadPlanes);
    auto intMin = make_dib(40, 4, 4, 24, kDibBiRgb);
    put32(intMin, 8, 0x80000000u);
    check("height INT32_MIN is refused (cannot be negated)", validate_dib(intMin.data(), intMin.size()).reason == DibReason::BadDimensions);
  }
  // ------------------------------------------------------------------ bulk demux / generations
  {
    UdpControlChunkHeader dataHead{};
    dataHead.streamId = bulk_stream_id(7, kBulkStreamHostToClient);
    check("a ControlData datagram on a bulk stream id is bulk's", bulk_stream_claims(&dataHead, sizeof(dataHead)));
    UdpControlChunkHeader ctl{};
    ctl.streamId = kUdpControlStreamClientToHost;
    check("the control stream (1) is not", !bulk_stream_claims(&ctl, sizeof(ctl)));
    ctl.streamId = 0x80000000u | (5u << 2) | 1u;
    check("a control-resume stream id (bit31) is not", !bulk_stream_claims(&ctl, sizeof(ctl)));
    ctl.streamId = 0x80000000u | ((0x10000000u << 2) & 0x7FFFFFFCu) | 1u;  // a large resumeId sets bit30 too
    check("a control-resume id with bit30 also set (0xC0000001) is not bulk's", ctl.streamId == 0xC0000001u &&
          !bulk_stream_claims(&ctl, sizeof(ctl)));
    UdpControlAckPacket ack{};
    ack.kind = static_cast<uint16_t>(UdpPacketKind::ControlNack);
    ack.streamId = bulk_stream_id(9, kBulkStreamClientToHost);
    check("a NACK for a bulk stream is bulk's (stream id at the same offset)", bulk_stream_claims(&ack, sizeof(ack)));
    UdpVideoChunkHeader vid{};
    check("a video chunk is not", !bulk_stream_claims(&vid, sizeof(vid)));
    UdpControlChunkHeader wrongMagic = dataHead;
    wrongMagic.magic = 0;
    check("wrong magic is not", !bulk_stream_claims(&wrongMagic, sizeof(wrongMagic)));
    check("a truncated datagram is not", !bulk_stream_claims(&dataHead, 10));
    check("bulk ids keep the direction in the low bits and never collide with control ids",
          (bulk_stream_id(1, 1) & 3u) == 1u && bulk_stream_id(1, 1) != 1u && bulk_stream_id_is_bulk(bulk_stream_id(kBulkGenMask, 2)));

    BulkGenAllocator a;
    const uint32_t first = a.Next();
    check("generations start above zero", first != 0);
    BulkGenAllocator w;
    w.SeedForTest(kBulkGenMask - 1);  // next is 2^28-1, then 2^28 (low bits 0 -> skipped), then 2^28+1
    const uint32_t g1 = w.Next();
    const uint32_t g2 = w.Next();
    check("at the wrap, a value whose low 28 bits are 0 is skipped", (g1 & kBulkGenMask) == kBulkGenMask && (g2 & kBulkGenMask) == 1u);
    BulkGenAllocator r;
    std::vector<uint32_t> seen;
    for (int i = 0; i < 70; ++i) seen.push_back(r.Next() & kBulkGenMask);
    r.SeedForTest(0x10000000u + 69u);  // the counter wraps onto the recent 64 low values (6..69)
    const uint32_t after = r.Next() & kBulkGenMask;
    bool clash = false;
    for (size_t i = seen.size() - 64; i < seen.size(); ++i) clash = clash || seen[i] == after;
    check("after a wrap, none of the last 64 low values is reused", !clash);
  }
  // ------------------------------------------------------------------ bulk rate: the agreed values
  // ("증속 합의", clip_image_debate_2026-09-28.md): per-round evaluation no faster than
  // max(SRTT, 100 ms); 256 kbps start, x2 per round until the first congestion, then linear at most
  // +25 %/s of the post-decrease rate; /2 once per congestion event; cap 16 Mbps and the budget.
  {
    BulkRateController c;
    check("defaults: 256 kbps start, per-ACK rounds, 16 Mbps cap", c.RateBps(0) == 256000 && c.capBps() == 16000000 &&
                                                                     c.config().evalUnit == BulkRateEvalUnit::AckRound);
    uint64_t t = 1000000, rounds = 0;
    c.MarkEvaluated(t, rounds);
    check("nothing is due before a round completes", !c.Due(t + 500000, 0, 0));
    check("a round completed 50 ms ago is not due yet (min 100 ms)", !c.Due(t + 50000, 1, 0));
    check("...and is due at 100 ms", c.Due(t + 100000, 1, 0));
    check("with SRTT 300 ms the interval is the SRTT, not 100 ms", !c.Due(t + 200000, 1, 300000) && c.Due(t + 300000, 1, 300000));
    BulkRateWindow w{0, 20000, 10000, 0};
    w.datagramsSent = 100;
    auto clean = [&](uint64_t dtUs) {
      w.deliveredBps = c.rate();
      t += dtUs;
      ++rounds;
      const BulkRateAction a = c.Evaluate(w, t, rounds);
      c.MarkEvaluated(t, rounds);
      return a;
    };
    clean(100000);
    check("initial probe: x2 per round (256k -> 512k)", c.rate() == 512000 && c.inSlowStart());
    for (int i = 0; i < 3; ++i) clean(100000);
    check("x2 per round: 4096k after four rounds", c.rate() == 4096000);
    // ---- first congestion, and the same event's trailing signals (counterexample: a loss burst on a long RTT)
    BulkRateWindow lossy = w;
    lossy.lossOrNacks = 3;
    t += 100000;
    ++rounds;
    check("first congestion halves and ends the probe", c.Evaluate(lossy, t, rounds) == BulkRateAction::Lower &&
                                                             c.rate() == 2048000 && !c.inSlowStart());
    c.MarkEvaluated(t, rounds);
    t += 100000;
    ++rounds;
    check("the next round's loss is the same event: no second halving", c.Evaluate(lossy, t, rounds) == BulkRateAction::Hold &&
                                                                          c.rate() == 2048000);
    c.MarkEvaluated(t, rounds);
    // ---- linear growth, not exponential
    for (int i = 0; i < 10; ++i) clean(100000);
    const double oneSec = c.rate() / 2048000.0;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "after recovery: +25 %% of the post-decrease rate per second (x%.3f in ~1 s)", oneSec);
    check(buf, oneSec > 1.20 && oneSec < 1.26);
    for (int i = 0; i < 10; ++i) clean(100000);
    const double twoSec = c.rate() / 2048000.0;
    std::snprintf(buf, sizeof(buf), "linear, not exponential: x%.3f after ~2 s (1.5, not 1.5625)", twoSec);
    check(buf, twoSec > 1.44 && twoSec < 1.51);
    const uint32_t before = c.rate();
    clean(10000000);
    check("a long quiet spell banks at most one second of growth", c.rate() - before <= 2048000 / 4 + 1);
    // ---- evidence a raise may not stand on
    BulkRateWindow app = w;
    app.appLimited = true;
    app.deliveredBps = c.rate();
    const uint32_t r0 = c.rate();
    t += 100000;
    ++rounds;
    check("an app-limited window holds (the pacer ran dry)", c.Evaluate(app, t, rounds) == BulkRateAction::Hold && c.rate() == r0);
    BulkRateWindow yielded = w;
    yielded.yielded = true;
    yielded.deliveredBps = c.rate();
    t += 100000;
    ++rounds;
    check("a window that yielded to control/video holds", c.Evaluate(yielded, t, rounds) == BulkRateAction::Hold && c.rate() == r0);
    // ---- a budget that shrinks (counterexample: video starts)
    c.SetCapBps(1000000);
    check("a budget drop applies at once", c.rate() == 1000000);
  }
  {
    // ---- competing traffic: RTT rises and stays up -> one halving per event, then pause and probe.
    // Started at 2 Mbps so three halvings (250 kbps) cannot be mistaken for the 64 kbps probe.
    BulkRateConfig ct;
    ct.startBps = 2000000;
    BulkRateController c(ct);
    uint64_t t = 0, rounds = 0;
    BulkRateWindow base{0, 20000, 10000, 256000};
    c.Evaluate(base, t += 100000, ++rounds);
    BulkRateWindow queued{0, 80000, 10000, 512000};  // over max(2 x 20, 20 + 50) = 70 ms
    check("competing traffic (RTT past max(2x, +50 ms)) halves", c.Evaluate(queued, t += 100000, ++rounds) == BulkRateAction::Lower);
    check("...its next round is the same event", c.Evaluate(queued, t += 100000, ++rounds) == BulkRateAction::Hold);
    check("a later round still congested is a new event", c.Evaluate(queued, t += 100000, ++rounds) == BulkRateAction::Lower);
    c.Evaluate(queued, t += 100000, ++rounds);
    const BulkRateAction third = c.Evaluate(queued, t += 100000, ++rounds);
    check("three decreases in a row pause", third == BulkRateAction::Pause && c.RateBps(t + 1) == 0);
    check("after the pause comes a small probe, not the old rate", c.RateBps(t + 2000000 + 1) == 64000);
    check("a clean window inside the probe does not raise yet", c.Evaluate(base, t + 2500000, rounds + 10) == BulkRateAction::Probe);
    BulkRateController f;
    BulkRateWindow lossy{3, 20000, 10000, 2000000};
    uint64_t r = 0;
    for (int i = 0; i < 30; ++i) f.Evaluate(lossy, 3000000ull * (i + 1), r += 3);
    check("never below the floor (64 kbps)", f.rate() == 64000);
  }
  {
    BulkRateController lan;
    BulkRateWindow l1{0, 1000, 1000, 256000};
    lan.Evaluate(l1, 100000, 1);
    BulkRateWindow l2{0, 3000, 3000, 512000};  // 3x a 1 ms LAN baseline, but only +2 ms
    check("LAN jitter (1 ms -> 3 ms) is neither congestion nor a reason to stop raising",
          lan.Evaluate(l2, 200000, 2) == BulkRateAction::Raise);
    BulkRateController ping;
    BulkRateWindow p1{0, 20000, 10000, 256000};
    ping.Evaluate(p1, 100000, 1);
    BulkRateWindow p2{0, 20000, 45000, 512000};  // ping +35 ms: not congestion (<+50), but no raise (>+30)
    check("control RTT +35 ms holds the rate (no raise, no cut)",
          ping.Evaluate(p2, 200000, 2) == BulkRateAction::Hold && ping.rate() == 512000);
    BulkRateWindow idle{0, 20000, 10000, 100000};
    BulkRateController u;
    check("a window that did not use its rate does not raise it", u.Evaluate(idle, 100000, 1) == BulkRateAction::Hold);
    BulkRateController cap;
    BulkRateWindow cw{0, 1000, 1000, 0};
    uint64_t rr = 0;
    for (int i = 0; i < 30; ++i) {
      cw.deliveredBps = cap.rate();
      cap.Evaluate(cw, 100000ull * (i + 1), ++rr);
    }
    check("the probe stops at the cap (16 Mbps) and ends there", cap.rate() == 16000000 && !cap.inSlowStart());
    cap.SetCapBps(8000000);
    cap.SetCapBps(12000000);
    check("a cap moved up does not raise the rate by itself", cap.rate() == 8000000);
    cap.SetCapBps(1);
    check("the cap never goes below the floor", cap.capBps() == 64000 && cap.rate() == 64000);
    BulkRateConfig tol;
    tol.lossTolerancePerMille = 20;
    BulkRateController lt(tol);
    BulkRateWindow few{2, 1000, 1000, 256000};
    few.datagramsSent = 200;  // 10 per mille
    check("loss under a configured tolerance is not congestion", lt.Evaluate(few, 100000, 1) == BulkRateAction::Raise);
    BulkRateController strict;
    BulkRateWindow one{1, 1000, 1000, 256000};
    one.datagramsSent = 1000;
    check("tolerance 0 (agreed): a single loss event halves", strict.Evaluate(one, 100000, 1) == BulkRateAction::Lower);
    BulkRateConfig wallCfg;
    wallCfg.evalUnit = BulkRateEvalUnit::WallClock;
    BulkRateController wall(wallCfg);
    wall.MarkEvaluated(1000000, 0);
    check("wall-clock mode (a parameter): due every 2 s, rounds do not matter", !wall.Due(2999999, 100) && wall.Due(3000000, 0));
  }
  // ------------------------------------------------------------------ candidate rules (under discussion)
  {
    BulkRateConfig cand;
    cand.lossRule = BulkLossRule::RttOrRoundRate;
    cand.raiseRule = BulkRaiseRule::TimelyRound;
    auto win = [](uint32_t events, uint32_t lost, uint32_t sent, uint64_t pull) {
      BulkRateWindow w{events, pull, 1000, 0};
      w.lostDatagrams = lost;
      w.datagramsSent = sent;
      w.rttSamples = 1;
      return w;
    };
    BulkRateController a(cand);
    a.Evaluate(win(0, 0, 100, 20000), 100000, 1);  // baseline 20 ms
    check("candidate: 1 %% random loss without an RTT rise is not congestion",
          a.Evaluate(win(1, 1, 100, 21000), 200000, 2) != BulkRateAction::Lower);
    BulkRateController b(cand);
    b.Evaluate(win(0, 0, 100, 20000), 100000, 1);
    check("candidate: a round that lost 6 %% of its datagrams is congestion",
          b.Evaluate(win(2, 6, 100, 21000), 200000, 2) == BulkRateAction::Lower);
    BulkRateController c(cand);
    c.Evaluate(win(0, 0, 100, 20000), 100000, 1);
    check("candidate: loss with an RTT rise is congestion",
          c.Evaluate(win(1, 1, 100, 90000), 200000, 2) == BulkRateAction::Lower);
    BulkRateController d(cand);
    BulkRateWindow idle = win(0, 0, 100, 20000);
    idle.appLimited = true;  // the pacer idled while the head-only channel waited
    idle.deliveredBps = 50000;
    check("candidate: a timely round raises even though the pacer idled",
          d.Evaluate(idle, 100000, 1) == BulkRateAction::Raise);
    BulkRateController e(cand);
    BulkRateWindow none = win(0, 0, 100, 0);
    none.rttSamples = 0;
    none.deliveredBps = 256000;
    check("candidate: no valid sample in the round, no raise", e.Evaluate(none, 100000, 1) == BulkRateAction::Hold);
    BulkRateController f(cand);
    BulkRateWindow y = win(0, 0, 100, 20000);
    y.yielded = true;
    check("candidate: a yielded window still holds", f.Evaluate(y, 100000, 1) == BulkRateAction::Hold);
    BulkRateController g;  // agreed defaults: the same idle window does not raise
    check("agreed rule: that app-limited window holds", g.Evaluate(idle, 100000, 1) == BulkRateAction::Hold);
  }
  // ------------------------------------------------------------------ RTT samples
  {
    BulkRttEstimator e;
    check("the first sample is used", e.OnSample(40000, 0, 65000, false, false) && e.srttUs() == 40000);
    check("a resent chunk's sample is not (Karn)", !e.OnSample(400000, 70000, 65000, true, false) && e.srttUs() == 40000);
    check("a duplicate pull is not", !e.OnSample(40000, 70000, 65000, false, true));
    check("a pull bunched right behind the previous one is not (ACK compression)",
          !e.OnSample(5000, 2000, 65000, false, false) && e.srttUs() == 40000);
    check("an ordinary sample moves SRTT by 1/8", e.OnSample(48000, 70000, 65000, false, false) && e.srttUs() == 41000);
    check("the bunching floor is 1 ms at high rates", e.OnSample(40000, 1100, 1000, false, false));
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
