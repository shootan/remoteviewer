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
  // ------------------------------------------------------------------ bulk rate: "증속 합의" + "증속 2차 합의"
  {
    // A clean round: fresh on-time sample, pending work, goodput following the rate.
    auto clean = [](uint64_t pull, uint64_t goodput) {
      BulkRateWindow w;
      w.pullRttP50Us = pull;
      w.pingRttUs = 1000;
      w.rttSamples = 1;
      w.uniqueFragmentsSent = 14;
      w.goodputBps = goodput;
      w.deliveredBps = goodput;
      return w;
    };
    BulkRateController c;
    check("defaults: 256 kbps start, per-ACK rounds, 16 Mbps cap", c.RateBps(0) == 256000 && c.capBps() == 16000000 &&
                                                                     c.config().evalUnit == BulkRateEvalUnit::AckRound);
    uint64_t t = 1000000, rounds = 0;
    c.MarkEvaluated(t, rounds);
    check("nothing is due before a round completes", !c.Due(t + 500000, 0, 0));
    check("a round completed 50 ms ago is not due yet (min 100 ms)", !c.Due(t + 50000, 1, 0));
    check("...and is due at 100 ms", c.Due(t + 100000, 1, 0));
    check("with SRTT 300 ms the interval is the SRTT, not 100 ms", !c.Due(t + 200000, 1, 300000) && c.Due(t + 300000, 1, 300000));
    auto step = [&](const BulkRateWindow& w) {
      t += 100000;
      ++rounds;
      const BulkRateAction a = c.Evaluate(w, t, rounds);
      c.MarkEvaluated(t, rounds);
      return a;
    };
    step(clean(20000, c.rate()));
    check("initial probe: x2 per round (256k -> 512k)", c.rate() == 512000 && c.inSlowStart());
    check("③ the round after a raise settles (its goodput still carries the old rate): hold",
          step(clean(20000, c.rate())) == BulkRateAction::Hold && c.rate() == 512000);
    for (int i = 0; i < 5; ++i) step(clean(20000, c.rate()));
    check("x2 per raise, a raise every other round: 4096k after seven rounds", c.rate() == 4096000);
    // ---- ① an isolated, recoverable loss holds; it does not halve
    BulkRateWindow iso = clean(20000, c.rate());
    iso.lossEvents = 1;
    iso.uniqueFragmentsLost = 1;
    check("① an isolated loss holds the round (no halving, probe continues)",
          step(iso) == BulkRateAction::Hold && c.rate() == 4096000 && c.inSlowStart());
    // ---- ① loss with a weak delay rise is congestion
    BulkRateWindow weak = clean(31000, c.rate());  // over max(1.25 x 20, 20 + 10) = 30 ms
    weak.lossEvents = 1;
    weak.uniqueFragmentsLost = 1;
    check("① loss with a weak delay rise halves and ends the probe",
          step(weak) == BulkRateAction::Lower && c.rate() == 2048000 && !c.inSlowStart());
    check("...its trailing round is the same event (no second halving)", step(weak) == BulkRateAction::Hold && c.rate() == 2048000);
    // ---- linear growth after the first congestion (goodput keeps rising, so no plateau)
    for (int i = 0; i < 10; ++i) step(clean(20000, c.rate()));
    const double oneSec = c.rate() / 2048000.0;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "after recovery: +25 %% of the post-decrease rate per second (x%.3f in ~1 s)", oneSec);
    check(buf, oneSec > 1.20 && oneSec < 1.26);
    for (int i = 0; i < 10; ++i) step(clean(20000, c.rate()));
    const double twoSec = c.rate() / 2048000.0;
    std::snprintf(buf, sizeof(buf), "linear, not exponential: x%.3f after ~2 s (1.5, not 1.5625)", twoSec);
    check(buf, twoSec > 1.44 && twoSec < 1.51);
    const uint32_t before = c.rate();
    t += 10000000;
    ++rounds;
    c.Evaluate(clean(20000, c.rate()), t, rounds);
    c.MarkEvaluated(t, rounds);
    check("a long quiet spell banks at most one second of growth", c.rate() - before <= 2048000 / 4 + 1);
    // ---- ③ what a raise may not stand on
    const uint32_t r0 = c.rate();
    BulkRateWindow noWork = clean(20000, r0 * 2);
    noWork.workPending = false;
    check("③ no pending work: hold", step(noWork) == BulkRateAction::Hold && c.rate() == r0);
    BulkRateWindow noSample = clean(0, r0 * 2);
    noSample.rttSamples = 0;
    check("③ no fresh round trip measured: hold", step(noSample) == BulkRateAction::Hold && c.rate() == r0);
    BulkRateWindow yielded = clean(20000, r0 * 2);
    yielded.yielded = true;
    check("③ a window that yielded to control/video: hold", step(yielded) == BulkRateAction::Hold && c.rate() == r0);
    BulkRateWindow slowPing = clean(20000, r0 * 2);
    slowPing.pingRttUs = 45000;  // control RTT +44 ms: unstable, not yet congestion
    check("③ control RTT not stable (+30 ms): hold", step(slowPing) == BulkRateAction::Hold && c.rate() == r0);
  }
  {
    // ---- ③ a raise that brings no goodput gain holds until new headroom
    BulkRateController c;
    auto w = [](uint64_t goodput) {
      BulkRateWindow x;
      x.pullRttP50Us = 20000;
      x.rttSamples = 1;
      x.uniqueFragmentsSent = 14;
      x.goodputBps = goodput;
      return x;
    };
    uint64_t t = 0, r = 0;
    c.Evaluate(w(250000), t += 100000, ++r);  // raise 256k -> 512k (pre-raise goodput 250k)
    check("③ a raise", c.rate() == 512000);
    c.Evaluate(w(250000), t += 100000, ++r);  // the settle round
    check("③ the next round shows no gain (250k again): hold, and stay there",
          c.Evaluate(w(250000), t += 100000, ++r) == BulkRateAction::Hold && c.onPlateau() && c.rate() == 512000);
    check("③ ...no repeated raise while nothing new is seen", c.Evaluate(w(250000), t += 100000, ++r) == BulkRateAction::Hold);
    check("③ goodput up by less than the raise is not new headroom", c.Evaluate(w(300000), t += 100000, ++r) == BulkRateAction::Hold);
    check("③ goodput up by the raise's full size is new headroom: raising resumes",
          c.Evaluate(w(520000), t += 100000, ++r) == BulkRateAction::Raise);
    BulkRateController d;
    d.Evaluate(w(250000), 100000, 1);
    d.Evaluate(w(250000), 200000, 2);  // settle
    d.Evaluate(w(250000), 300000, 3);  // plateau
    check("③ ...or a bounded retry after 5 s", d.Evaluate(w(250000), 5400000, 4) == BulkRateAction::Raise);
  }
  {
    // ---- ② the loss ratio: unique original fragments over a rolling horizon, Unknown until sampled
    BulkRateController c;
    auto lw = [](uint32_t sent, uint32_t lost, uint32_t events) {
      BulkRateWindow x;
      x.pullRttP50Us = 20000;
      x.rttSamples = 1;
      x.uniqueFragmentsSent = sent;
      x.uniqueFragmentsLost = lost;
      x.lossEvents = events;
      return x;
    };
    uint64_t t = 0, r = 0;
    c.Evaluate(lw(100, 0, 0), t += 100000, ++r);
    c.Evaluate(lw(100, 0, 0), t += 100000, ++r);
    check("② 200 fragments is too few: Unknown (never taken as low)", c.LossState() == BulkLossState::Unknown);
    c.Evaluate(lw(100, 0, 0), t += 100000, ++r);
    check("② 300 fragments, none lost: Low", c.LossState() == BulkLossState::Low);
    const uint32_t before = c.rate();
    check("② 1 % loss over the horizon is an isolated loss: hold",
          c.Evaluate(lw(100, 1, 1), t += 100000, ++r) == BulkRateAction::Hold && c.rate() == before &&
              c.LossState() == BulkLossState::Low);
    // 30 lost of the last ~500 = 6 %: sustained high loss
    c.Evaluate(lw(100, 29, 3), t += 100000, ++r);
    check("② sustained loss over 5 %% of the horizon is congestion", c.LossState() == BulkLossState::High && c.rate() < before);
    BulkRateController o;
    o.Evaluate(lw(400, 30, 3), 100000, 1);
    check("② ...and a horizon older than 10 s is forgotten (Unknown again)",
          o.LossState() == BulkLossState::High && (o.Evaluate(lw(0, 0, 0), 11000000, 2), o.LossState() == BulkLossState::Unknown));
  }
  {
    // ---- ① delay-only and RTO decreases are kept
    auto base = [] {
      BulkRateWindow x;
      x.pullRttP50Us = 20000;
      x.rttSamples = 1;
      x.uniqueFragmentsSent = 14;
      x.goodputBps = 256000;
      return x;
    };
    BulkRateController d;
    d.Evaluate(base(), 100000, 1);
    BulkRateWindow queued = base();
    queued.pullRttP50Us = 80000;  // over max(2 x 20, 20 + 50) = 70 ms, no loss at all
    check("① a delay rise alone (a shallow queue, competing traffic) still halves", d.Evaluate(queued, 200000, 2) == BulkRateAction::Lower);
    BulkRateController rto;
    rto.Evaluate(base(), 100000, 1);
    BulkRateWindow timer = base();
    timer.rtoEvents = 1;
    timer.lossEvents = 1;
    check("① a retransmission timeout (progress lost) halves", rto.Evaluate(timer, 200000, 2) == BulkRateAction::Lower);
    // ---- competing traffic that stays: one halving per event, then pause and probe
    BulkRateConfig ct;
    ct.startBps = 2000000;
    BulkRateController c(ct);
    uint64_t t = 0, rounds = 0;
    c.Evaluate(base(), t += 100000, ++rounds);
    check("competing traffic halves", c.Evaluate(queued, t += 100000, ++rounds) == BulkRateAction::Lower);
    check("...its next round is the same event", c.Evaluate(queued, t += 100000, ++rounds) == BulkRateAction::Hold);
    check("a later round still congested is a new event", c.Evaluate(queued, t += 100000, ++rounds) == BulkRateAction::Lower);
    c.Evaluate(queued, t += 100000, ++rounds);
    const BulkRateAction third = c.Evaluate(queued, t += 100000, ++rounds);
    check("three decreases in a row pause", third == BulkRateAction::Pause && c.RateBps(t + 1) == 0);
    check("after the pause comes a small probe, not the old rate", c.RateBps(t + 2000000 + 1) == 64000);
    check("a clean window inside the probe does not raise yet", c.Evaluate(base(), t + 2500000, rounds + 10) == BulkRateAction::Probe);
    BulkRateController f;
    uint64_t r = 0;
    f.Evaluate(base(), 100000, r += 3);  // the baseline first
    for (int i = 0; i < 30; ++i) f.Evaluate(queued, 3000000ull * (i + 1), r += 3);
    check("never below the floor (64 kbps) by its own decreases", f.rate() == 64000);
  }
  {
    // ---- ④ the cap is the budget: it wins over the floor, 0 included
    BulkRateController c;
    c.SetCapBps(0);
    check("④ a budget of 0 sends nothing (not the 64 kbps floor)", c.RateBps(0) == 0);
    c.SetCapBps(10000);
    check("④ a budget under the floor is honoured as it is (10 kbps)", c.RateBps(0) == 10000);
    c.SetCapBps(16000000);
    check("④ lifting the budget does not raise the rate by itself", c.RateBps(0) <= 256000);
    BulkRateConfig hi;
    hi.startBps = 8000000;
    BulkRateController v(hi);
    v.SetCapBps(1000000);
    check("④ video starts: the budget drops, the rate follows at once", v.RateBps(0) == 1000000);
    // the probe after a pause is under the cap too
    BulkRateWindow q;
    q.pullRttP50Us = 20000;
    q.rttSamples = 1;
    BulkRateController p;
    p.Evaluate(q, 100000, 1);
    q.pullRttP50Us = 90000;
    p.Evaluate(q, 200000, 3);
    p.Evaluate(q, 300000, 6);
    p.Evaluate(q, 400000, 9);  // pause
    p.SetCapBps(0);
    check("④ ...and the probe after a pause respects a budget of 0", p.RateBps(400000 + 2000000 + 1) == 0);
  }
  {
    BulkRateController lan;
    BulkRateWindow l1;
    l1.pullRttP50Us = 1000;
    l1.rttSamples = 1;
    l1.goodputBps = 256000;
    lan.Evaluate(l1, 100000, 1);
    BulkRateWindow l2 = l1;
    l2.pullRttP50Us = 3000;  // 3x a 1 ms LAN baseline, but only +2 ms
    l2.goodputBps = 512000;
    lan.Evaluate(l2, 200000, 2);  // the settle round
    check("LAN jitter (1 ms -> 3 ms) is neither congestion nor a reason to stop raising",
          lan.Evaluate(l2, 300000, 3) == BulkRateAction::Raise);
    BulkRateConfig wallCfg;
    wallCfg.evalUnit = BulkRateEvalUnit::WallClock;
    BulkRateController wall(wallCfg);
    wall.MarkEvaluated(1000000, 0);
    check("wall-clock mode (a parameter): due every 2 s, rounds do not matter", !wall.Due(2999999, 100) && wall.Due(3000000, 0));
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
    BulkRttEstimator s;
    s.OnSample(40000, 0, 65000, false, false);
    check("a lone 250 ms spike on a 40 ms path is held back (a resent pull, not a queue)",
          !s.OnSample(250000, 70000, 65000, false, false) && s.srttUs() == 40000);
    check("...and dropped when the next sample is normal", s.OnSample(41000, 70000, 65000, false, false) && s.srttUs() < 42000);
    s.OnSample(260000, 70000, 65000, false, false);
    check("two high samples in a row are a queue: both count",
          s.OnSample(270000, 70000, 65000, false, false) && s.srttUs() > 80000);
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
