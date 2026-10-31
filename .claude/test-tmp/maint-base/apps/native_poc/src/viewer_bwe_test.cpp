// The C0 receive-side estimator against synthetic arrival sequences: steady, a queue building,
// a queue draining, jitter, an application-limited stream, a still screen, a new generation, and
// the sampling policy (duplicates, parity, retransmits, reordering, discontinuities).
//
// Every expectation is about STRUCTURE -- which state, which direction, which bound -- not about
// tuned numbers, because the constants are provisional (viewer_bwe.hpp).

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "viewer_bwe.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
}

/**
 * A synthetic stream. Frame i leaves the host at sendBase + i*interval (host clock) and its first
 * chunk reaches the viewer at that plus a fixed clock offset plus `queueUs(i)` (viewer clock). The
 * clock offset is large and arbitrary on purpose: nothing may compare the two clocks directly.
 */
struct Stream {
  ViewerBandwidthEstimator bwe;
  uint64_t generation = 7;
  uint64_t sendBase = 5000000000ull;
  uint64_t clockOffset = 123456789ull;
  uint64_t intervalUs = 33333;
  uint32_t frameBytes = 25000;  // 30 fps x 25 kB = 6 Mbps
  uint32_t chunkBytes = 1200;
  uint32_t seq = 100;
  uint64_t frameIndex = 0;
  uint64_t nextReportUs = 0;
  std::vector<BweReport> reports;

  explicit Stream(BweConfig cfg = {}) : bwe(cfg) {}

  uint64_t send_time(uint64_t i) const { return sendBase + i * intervalUs; }

  /** One frame, all its chunks back to back 50 us apart. */
  void frame(int64_t queueUs, uint16_t flags = 0, bool duplicateEachChunk = false) {
    const uint64_t sendUs = send_time(frameIndex);
    const uint64_t arrival = static_cast<uint64_t>(static_cast<int64_t>(sendUs + clockOffset) + queueUs);
    const uint32_t chunks = (frameBytes + chunkBytes - 1) / chunkBytes;
    for (uint32_t c = 0; c < chunks; ++c) {
      BweChunkSample s;
      s.streamGeneration = generation;
      s.seq = seq;
      s.chunkIndex = static_cast<uint16_t>(c);
      s.flags = flags;
      s.sendQpcUs = sendUs;
      s.recvUs = arrival + c * 50;
      s.payloadBytes = std::min(chunkBytes, frameBytes - c * chunkBytes);
      s.wireBytes = s.payloadBytes + 88;
      bwe.OnChunk(s);
      if (duplicateEachChunk) bwe.OnChunk(s);
    }
    ++seq;
    ++frameIndex;
    poll(arrival + chunks * 50);
  }

  void poll(uint64_t nowUs) {
    BweReport r;
    if (bwe.Report(nowUs, 0, &r)) reports.push_back(r);
  }

  /** `seconds` of frames, with the queue delay a function of the frame's index within the run. */
  void run(double seconds, const std::function<int64_t(uint64_t)>& queue, uint16_t flags = 0) {
    const uint64_t n = static_cast<uint64_t>(seconds * 1e6 / intervalUs);
    for (uint64_t k = 0; k < n; ++k) frame(queue(k), flags);
  }
};

std::string describe(const BweReport& r) {
  char text[256];
  std::snprintf(text, sizeof(text),
                "bwe=%u usage=%s rate=%s appLimited=%d static=%d goodput=%u wire=%u grad=%d thr=%d samples=%u",
                r.bweBps, to_string(r.usage), to_string(r.rateState), r.appLimited ? 1 : 0,
                r.staticHold ? 1 : 0, r.goodputUniqueBps, r.wireLoadBps, r.delayGradientUs,
                r.thresholdUs, r.delaySamples);
  return text;
}

}  // namespace

int main() {
  // ------------------------------------------------------------------ steady: normal -> increase
  {
    Stream s;
    s.run(8.0, [](uint64_t) { return 20000; });
    bool allNormal = !s.reports.empty();
    bool allIncrease = !s.reports.empty();
    for (const auto& r : s.reports) {
      allNormal = allNormal && r.usage == BweUsage::Normal;
      allIncrease = allIncrease && r.rateState == BweRateState::Increase;
    }
    check("steady delay reads as normal throughout", allNormal,
          s.reports.empty() ? "no reports" : describe(s.reports.back()));
    check("...and normal INCREASES (the draft's structure, not hold)", allIncrease);
    check("...so the estimate climbs above where it started",
          !s.reports.empty() && s.reports.back().bweBps > BweConfig{}.initialBps,
          describe(s.reports.back()));
    const double goodput = s.reports.back().goodputUniqueBps;
    check("unique goodput is the media rate (6 Mbps +-5%)",
          goodput > 5.7e6 && goodput < 6.3e6, std::to_string(goodput));
    check("wire load includes the headers, so it is above unique goodput",
          s.reports.back().wireLoadBps > s.reports.back().goodputUniqueBps);
    check("the estimate never exceeds 1.5 x what was received",
          s.reports.back().bweBps <= 1.5 * goodput * 1.001, describe(s.reports.back()));
  }

  // ------------------------------------------------------------------ a queue builds: overuse
  size_t onsetReport = 0;
  {
    Stream s;
    s.run(4.0, [](uint64_t) { return 20000; });
    onsetReport = s.reports.size();
    // Each frame arrives 3 ms later than the last: a queue growing by 90 ms a second.
    s.run(3.0, [](uint64_t k) { return 20000 + static_cast<int64_t>(k) * 3000; });
    size_t firstOveruse = 0;
    for (size_t i = onsetReport; i < s.reports.size(); ++i) {
      if (s.reports[i].usage == BweUsage::Overuse) { firstOveruse = i; break; }
    }
    check("a growing queue is detected as overuse", firstOveruse != 0,
          s.reports.size() > onsetReport ? describe(s.reports[onsetReport]) : "no reports");
    check("...within two reports of the onset", firstOveruse != 0 && firstOveruse - onsetReport <= 2,
          std::to_string(firstOveruse - onsetReport) + " reports after");
    if (firstOveruse != 0) {
      const BweReport& r = s.reports[firstOveruse];
      check("...and overuse DECREASES", r.rateState == BweRateState::Decrease, describe(r));
      check("...to at most 0.85 x what got through",
            r.bweBps <= 0.85 * r.goodputUniqueBps * 1.001 + 1, describe(r));
      check("...and is not called application-limited", !r.appLimited);
    }

    // The queue drains: frames arrive earlier than the last. Underuse holds.
    const size_t drainStart = s.reports.size();
    // The queue that built for three seconds at 90 ms/s drains at the same rate, back to where it
    // started -- long enough for the smoothed trend to turn.
    s.run(3.0, [](uint64_t k) { return 290000 - static_cast<int64_t>(k) * 3000; });
    bool sawUnderuse = false;
    bool underuseHeld = true;
    for (size_t i = drainStart; i < s.reports.size(); ++i) {
      if (s.reports[i].usage == BweUsage::Underuse) {
        sawUnderuse = true;
        underuseHeld = underuseHeld && s.reports[i].rateState == BweRateState::Hold;
      }
    }
    check("a draining queue is detected as underuse", sawUnderuse,
          s.reports.size() > drainStart ? describe(s.reports[drainStart]) : "no reports");
    check("...and underuse HOLDS", sawUnderuse && underuseHeld);
  }

  // ------------------------------------------------------------------ jitter is not a queue
  {
    Stream s;
    s.run(10.0, [](uint64_t k) { return 20000 + ((k & 1) ? 4000 : -4000); });
    int overuse = 0;
    for (const auto& r : s.reports) overuse += r.usage == BweUsage::Overuse ? 1 : 0;
    check("zero-mean jitter of +-4 ms never reads as overuse", overuse == 0,
          std::to_string(overuse) + " of " + std::to_string(s.reports.size()));
  }

  // ------------------------------------------------------------------ application-limited
  {
    Stream s;
    s.frameBytes = 1000;  // 30 fps x 1 kB = 240 kbps on a path that is not queueing
    s.run(6.0, [](uint64_t) { return 20000; });
    bool allLimited = s.reports.size() > 1;
    bool bounded = true;
    for (size_t i = 1; i < s.reports.size(); ++i) {
      allLimited = allLimited && s.reports[i].appLimited;
      bounded = bounded && s.reports[i].bweBps <= 1.5 * std::max<double>(s.reports[i].goodputUniqueBps, 150000) * 1.001 + 1;
    }
    check("a sender using a fraction of the path is flagged application-limited", allLimited,
          s.reports.empty() ? "" : describe(s.reports.back()));
    check("...and its estimate is bounded by what it actually sent, not left at 4 Mbps", bounded,
          s.reports.empty() ? "" : describe(s.reports.back()));
  }

  // ------------------------------------------------------------------ still screen holds
  {
    Stream s;
    s.run(3.0, [](uint64_t) { return 20000; });
    const uint32_t before = s.reports.back().bweBps;
    s.frameBytes = 1000;  // a still screen's refresh frames are small
    s.run(3.0, [](uint64_t) { return 20000; }, /*flags=*/0x40);  // synthetic refresh frames
    bool heldStatic = true;
    for (size_t i = s.reports.size() - 2; i < s.reports.size(); ++i) {
      heldStatic = heldStatic && s.reports[i].staticHold && s.reports[i].rateState == BweRateState::Hold;
    }
    check("a still screen (synthetic frames) holds the estimate", heldStatic,
          describe(s.reports.back()));
    // Hold means KEPT, not merely "not increased": a still screen sends almost nothing, and that
    // is not a statement about the path. (The first run of the e2e caught the estimate collapsing.)
    check("...and it does not collapse to what a still screen sends",
          s.reports.back().bweBps >= before * 0.9,
          std::to_string(before) + " -> " + describe(s.reports.back()));
    // The first window after the switch is part real, part synthetic; the fully still ones hold.
    check("...it does not climb", s.reports.back().bweBps == s.reports[s.reports.size() - 2].bweBps,
          std::to_string(s.reports[s.reports.size() - 2].bweBps) + " -> " + describe(s.reports.back()));
    (void)before;
  }

  // ------------------------------------------------------------------ a new generation resets
  {
    Stream s;
    s.run(6.0, [](uint64_t) { return 20000; });
    check("(the estimate had moved)", s.reports.back().bweBps != BweConfig{}.initialBps);
    s.generation = 8;
    s.seq = 1;  // a new stream counts from the start again
    s.run(1.2, [](uint64_t) { return 20000; });
    check("a new stream generation starts the estimate over",
          s.reports.back().streamGeneration == 8 && s.bwe.generation() == 8,
          describe(s.reports.back()));
    check("...from the initial value, not the old stream's",
          s.reports.back().bweBps <= BweConfig{}.initialBps * 1.2, describe(s.reports.back()));
  }

  // ------------------------------------------------------------------ the sampling policy
  {
    Stream plain;
    Stream dup;
    for (int i = 0; i < 60; ++i) {
      plain.frame(20000);
      dup.frame(20000, 0, /*duplicateEachChunk=*/true);
    }
    plain.poll(plain.send_time(plain.frameIndex) + plain.clockOffset + 1500000);
    dup.poll(dup.send_time(dup.frameIndex) + dup.clockOffset + 1500000);
    check("duplicates add nothing to unique goodput",
          !plain.reports.empty() && !dup.reports.empty() &&
              dup.reports.back().goodputUniqueBps == plain.reports.back().goodputUniqueBps,
          std::to_string(plain.reports.empty() ? 0 : plain.reports.back().goodputUniqueBps) + " vs " +
              std::to_string(dup.reports.empty() ? 0 : dup.reports.back().goodputUniqueBps));
    check("...but do count in the wire load",
          !plain.reports.empty() && !dup.reports.empty() &&
              dup.reports.back().wireLoadBps > plain.reports.back().wireLoadBps * 1.9);
    check("...and are not extra delay samples",
          !plain.reports.empty() && !dup.reports.empty() &&
              dup.reports.back().delaySamples == plain.reports.back().delaySamples);
  }
  {
    Stream s;
    s.frameBytes = 1200;  // one chunk per frame, so parity is easy to place
    for (int i = 0; i < 30; ++i) s.frame(20000);
    const uint32_t lastSeq = s.seq - 1;
    // A parity datagram for the last frame: wire only.
    BweChunkSample parity;
    parity.streamGeneration = s.generation;
    parity.seq = lastSeq;
    parity.chunkIndex = 1;
    parity.flags = 0x10;
    parity.sendQpcUs = s.send_time(s.frameIndex - 1);
    parity.recvUs = parity.sendQpcUs + s.clockOffset + 20000;
    parity.payloadBytes = 1200;
    parity.wireBytes = 1288;
    s.bwe.OnChunk(parity);
    // A retransmit of a frame 10 back: the cached header's OLD sendQpcUs, arriving now, late.
    BweChunkSample retx;
    retx.streamGeneration = s.generation;
    retx.seq = lastSeq - 10;
    retx.chunkIndex = 0;
    retx.sendQpcUs = s.send_time(s.frameIndex - 11);
    retx.recvUs = parity.recvUs + 300000;
    retx.payloadBytes = 1200;
    retx.wireBytes = 1288;
    s.bwe.OnChunk(retx);
    // A frame that arrives AFTER a newer one (reordered).
    const uint64_t skippedIndex = s.frameIndex;
    s.frameIndex += 1;
    s.seq += 1;
    s.frame(20000);  // the newer frame
    BweChunkSample late;
    late.streamGeneration = s.generation;
    late.seq = s.seq - 2;
    late.chunkIndex = 0;
    late.sendQpcUs = s.send_time(skippedIndex);
    late.recvUs = s.send_time(s.frameIndex) + s.clockOffset + 25000;
    late.payloadBytes = 1200;
    late.wireBytes = 1288;
    s.bwe.OnChunk(late);
    BweReport r;
    s.bwe.Report(late.recvUs + 2000000, 0, &r);

    // The control: the same in-order frames and nothing else.
    Stream c;
    c.frameBytes = 1200;
    for (int i = 0; i < 30; ++i) c.frame(20000);
    c.frameIndex += 1;
    c.seq += 1;
    c.frame(20000);
    BweReport rc;
    c.bwe.Report(late.recvUs + 2000000, 0, &rc);
    uint32_t samplesS = r.delaySamples, samplesC = rc.delaySamples, framesS = r.frames, framesC = rc.frames;
    for (const auto& x : s.reports) { samplesS += x.delaySamples; framesS += x.frames; }
    for (const auto& x : c.reports) { samplesC += x.delaySamples; framesC += x.frames; }
    check("a retransmit and a reordered frame are not delay samples",
          samplesS == samplesC && framesS == framesC,
          "samples " + std::to_string(samplesS) + " vs control " + std::to_string(samplesC) +
              ", frames " + std::to_string(framesS) + " vs " + std::to_string(framesC));
    check("...and do not read as a queue", r.usage != BweUsage::Overuse, describe(r));
  }
  {
    // FEC parity is load on the wire, not media.
    ViewerBandwidthEstimator e;
    BweChunkSample p;
    p.streamGeneration = 3;
    p.seq = 10;
    p.flags = 0x10;
    p.sendQpcUs = 1000;
    p.recvUs = 2000;
    p.payloadBytes = 1200;
    p.wireBytes = 1288;
    for (int i = 0; i < 100; ++i) {
      p.chunkIndex = static_cast<uint16_t>(i);
      e.OnChunk(p);
    }
    BweReport r;
    e.Report(2000, 0, &r);
    e.Report(1002000, 0, &r);
    check("FEC parity counts in the wire load and not in unique goodput",
          r.goodputUniqueBps == 0 && r.wireLoadBps > 0, describe(r));
  }
  {
    // The host restarts inside the same generation: its clock steps backwards.
    Stream s;
    s.run(3.0, [](uint64_t) { return 20000; });
    // Only the HOST's clock moves: arrivals on the viewer carry on as before. (The first version
    // moved both, so the difference was zero and the check could not fail.)
    const size_t before = s.reports.size();
    s.sendBase -= 60000000ull;     // sixty seconds earlier on the host
    s.clockOffset += 60000000ull;  // ...while the viewer's arrivals continue unbroken
    s.run(2.0, [](uint64_t) { return 20000; });
    int overuse = 0;
    int32_t worstGradient = 0;
    for (size_t i = before; i < s.reports.size(); ++i) {
      overuse += s.reports[i].usage == BweUsage::Overuse ? 1 : 0;
      worstGradient = std::max(worstGradient, std::abs(s.reports[i].delayGradientUs));
    }
    check("a departure clock that steps backwards restarts the detector, it is not a queue",
          overuse == 0, describe(s.reports.back()));
    check("...and leaves no sixty-second step in the trend",
          worstGradient < s.reports.back().thresholdUs,
          "worst |gradient| " + std::to_string(worstGradient));
  }

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED",
              gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
