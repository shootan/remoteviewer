#pragma once

// C0 stage 1 -- a receive-side bandwidth estimate, for OBSERVATION ONLY.
//
// Role:    turns the video chunks the viewer receives into, once a second, an estimate of the
//          bandwidth available on the host->viewer path, the state of the delay-based detector,
//          and two goodput figures. The viewer reports it to the host, which LOGS it and does
//          nothing else: no encoder, pacing or ABR input reads it (c0_bandwidth_plan.md §8,
//          Codex PLAN_REVIEW: observation stage only). Whether any of it is good enough to act
//          on is what the observation is for.
// Thread:  one owner (the viewer's receive thread). Not thread-safe; the report is copied out.
// Input:   per chunk -- the header fields below and the local arrival time. Per report -- the
//          assembly drop permille the receiver already computes.
// Output:  BweReport.
//
// STRUCTURE, after the GCC draft (draft-ietf-rmcat-gcc), and stated as "after", not "equal to":
//   delay-based detector   per frame, d = (arrival delta) - (departure delta); accumulated,
//                          smoothed, and a least-squares slope over the last N samples gives a
//                          trend, compared with an adaptive threshold -> {normal, underuse,
//                          overuse}. The trendline form and its defaults are the ones WebRTC's
//                          TrendlineEstimator uses.
//   rate controller        normal -> INCREASE, underuse -> HOLD, overuse -> DECREASE. (Not
//                          "normal holds, underuse increases": with an empty queue the state is
//                          normal, and that rule could never climb back. Codex Q4.)
//                          The estimate is never allowed above maxOverReceived x the received
//                          rate, which is what stops an idle or still stream from "discovering"
//                          bandwidth it never used.
//   application-limited    received rate well below the estimate with no queue signal: the
//                          sender is not using the link, and the estimate is not a capacity
//                          measurement. Flagged, never used as a ceiling.
//   still screen           a window whose frames are mostly synthetic refreshes: HOLD.
//
// SAMPLING POLICY (fixed here, pinned by viewer_bwe_test.cpp):
//   - sendQpcUs is stamped by the host AFTER inter-frame pacing and BEFORE the frame's chunks go
//     out (host_encoded_sender.cpp: `item.udpHdr.sendQpcUs = sendStartUs` just before
//     send_udp_chunks_timed), so every chunk of a frame carries the same value. A frame is
//     therefore one delay sample, taken from the first chunk of it that arrives.
//   - Only a frame NEWER than every frame seen so far is sampled. A NACK retransmit carries the
//     cached header -- the original sendQpcUs -- and arrives late by design; it is always for a
//     frame already superseded, so this rule excludes it. So is anything reordered behind a newer
//     frame, and any duplicate.
//   - A departure delta that is not positive, or larger than maxSendGapUs, is a discontinuity
//     (host restart, a clock jump, a long pause): the detector restarts, the estimate is kept.
//   - A new streamGeneration is a new stream: everything restarts, the estimate included.
//   - Goodput: "unique" = the payload of the FIRST arrival of each (seq, chunk) that is media,
//     not FEC parity -- duplicates add nothing, a retransmit that fills a gap counts once.
//     "wire" = every datagram as received, header, parity, duplicates and retransmits included.
//   - Clocks are never compared across machines: only differences of host times with host times
//     and viewer times with viewer times.
//
// Every constant in BweConfig is PROVISIONAL, chosen to observe with; none is a decision.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <set>

namespace remote60::native_poc {

struct BweChunkSample {
  uint64_t streamGeneration = 0;
  uint32_t seq = 0;          // the frame (AU) sequence number
  uint16_t chunkIndex = 0;
  uint16_t flags = 0;        // UdpVideoChunkHeader flags: bit4 parity, bit6 synthetic
  uint64_t sendQpcUs = 0;    // host clock
  uint64_t recvUs = 0;       // viewer clock
  uint32_t wireBytes = 0;    // the datagram as received
  uint32_t payloadBytes = 0; // the chunk's media payload
};

enum class BweUsage : uint8_t { Normal = 0, Underuse = 1, Overuse = 2 };
enum class BweRateState : uint8_t { Hold = 0, Increase = 1, Decrease = 2 };

inline const char* to_string(BweUsage u) {
  switch (u) {
    case BweUsage::Underuse: return "underuse";
    case BweUsage::Overuse: return "overuse";
    default: return "normal";
  }
}
inline const char* to_string(BweRateState s) {
  switch (s) {
    case BweRateState::Increase: return "increase";
    case BweRateState::Decrease: return "decrease";
    default: return "hold";
  }
}

/** PROVISIONAL -- observation values, not decisions (c0_bandwidth_plan.md §8). */
struct BweConfig {
  uint32_t initialBps = 4000000;
  uint32_t minBps = 150000;
  uint32_t maxBps = 400000000;
  // Trendline detector (WebRTC TrendlineEstimator defaults).
  double smoothing = 0.9;
  size_t window = 20;
  double gain = 4.0;
  double initialThresholdMs = 12.5;
  double kUp = 0.0087;
  double kDown = 0.039;
  double overuseTimeMs = 10.0;
  // Rate controller.
  double decreaseFactor = 0.85;
  double increasePerSecond = 0.08;
  double maxOverReceived = 1.5;
  double appLimitedRatio = 0.5;
  // Housekeeping.
  uint64_t reportIntervalUs = 1000000;
  uint64_t maxSendGapUs = 2000000;
  uint32_t dedupeSeqSpan = 1024;
};

struct BweReport {
  uint64_t streamGeneration = 0;
  uint32_t bweBps = 0;
  BweUsage usage = BweUsage::Normal;       // the worst state seen in the window
  BweRateState rateState = BweRateState::Hold;
  bool appLimited = false;
  bool staticHold = false;
  uint32_t goodputUniqueBps = 0;
  uint32_t wireLoadBps = 0;
  // The detector's modified trend at the end of the window, x1000 -- in the draft's units
  // (accumulated delay slope, scaled), NOT a physical delay. Compared against thresholdUs.
  int32_t delayGradientUs = 0;
  int32_t thresholdUs = 0;
  uint16_t lossPm = 0;
  uint32_t delaySamples = 0;               // frames sampled in the window
  uint32_t frames = 0;                      // new frames seen in the window
  uint32_t syntheticFrames = 0;
};

class ViewerBandwidthEstimator {
 public:
  explicit ViewerBandwidthEstimator(BweConfig config = {}) : cfg_(config) { ResetAll(0); }

  void OnChunk(const BweChunkSample& c) {
    if (c.streamGeneration != generation_) {
      ResetAll(c.streamGeneration);
      window_.startUs = c.recvUs;
    }
    if (window_.startUs == 0) window_.startUs = c.recvUs;
    window_.wireBytes += c.wireBytes;

    const bool parity = (c.flags & 0x10u) != 0;
    if (!parity && FirstArrival(c.seq, c.chunkIndex)) window_.uniqueBytes += c.payloadBytes;

    // One delay sample per frame, only for a frame newer than anything seen (see the policy).
    if (haveMaxSeq_ && static_cast<int32_t>(c.seq - maxSeq_) <= 0) return;
    maxSeq_ = c.seq;
    haveMaxSeq_ = true;
    ++window_.frames;
    if ((c.flags & 0x40u) != 0) ++window_.syntheticFrames;
    Sample(c.sendQpcUs, c.recvUs);
  }

  /**
   * Once per reportIntervalUs. Returns false until the interval has passed. `lossPm` is the
   * receiver's own assembly drop permille for the same second.
   */
  bool Report(uint64_t nowUs, uint32_t lossPm, BweReport* out) {
    if (!out) return false;
    if (lastReportUs_ == 0) {
      lastReportUs_ = nowUs;
      return false;
    }
    if (nowUs - lastReportUs_ < cfg_.reportIntervalUs) return false;
    const double seconds = static_cast<double>(nowUs - lastReportUs_) / 1e6;
    lastReportUs_ = nowUs;

    BweReport r;
    r.streamGeneration = generation_;
    r.goodputUniqueBps = ClampU32(static_cast<double>(window_.uniqueBytes) * 8.0 / seconds);
    r.wireLoadBps = ClampU32(static_cast<double>(window_.wireBytes) * 8.0 / seconds);
    r.usage = window_.worst;
    r.delayGradientUs = static_cast<int32_t>(std::lround(std::clamp(trend_ * 1000.0, -2e9, 2e9)));
    r.thresholdUs = static_cast<int32_t>(std::lround(thresholdMs_ * 1000.0));
    r.lossPm = static_cast<uint16_t>(std::min<uint32_t>(lossPm, 1000));
    r.delaySamples = window_.samples;
    r.frames = window_.frames;
    r.syntheticFrames = window_.syntheticFrames;

    const double received = static_cast<double>(r.goodputUniqueBps);
    r.staticHold = window_.frames == 0 || window_.syntheticFrames * 2 > window_.frames;
    if (r.usage == BweUsage::Overuse) {
      // Decrease below what actually got through, which is what the queue can drain.
      const double base = received > 0 ? received : bwe_;
      bwe_ = std::min(bwe_, cfg_.decreaseFactor * base);
      r.rateState = BweRateState::Decrease;
    } else if (r.usage == BweUsage::Underuse || r.staticHold) {
      r.rateState = BweRateState::Hold;
    } else {
      bwe_ *= 1.0 + cfg_.increasePerSecond * seconds;
      r.rateState = BweRateState::Increase;
    }
    // Never more than a margin above what was actually received: an estimate the sender never
    // tested is not a measurement.
    bool heldByReceived = false;
    // Not on a still screen: HOLD means the value is kept. Measured in the observation e2e -- with
    // the received-rate ceiling applied while still, a still screen collapsed a 7 Mb/s estimate to
    // 225 kb/s in one second, and it took over thirty to climb back once the picture moved.
    if (r.rateState != BweRateState::Decrease && !r.staticHold) {
      const double ceiling = cfg_.maxOverReceived * std::max(received, static_cast<double>(cfg_.minBps));
      if (bwe_ > ceiling) {
        bwe_ = ceiling;
        heldByReceived = true;
      }
    }
    bwe_ = std::clamp(bwe_, static_cast<double>(cfg_.minBps), static_cast<double>(cfg_.maxBps));
    r.bweBps = ClampU32(bwe_);
    // Application-limited: no queue signal, and the estimate is being held down by what was
    // received rather than by anything the path said. Without probing, a sender that is not
    // using the link and a link that is exactly full but not yet queueing look the same here;
    // both mean "this number is not a capacity measurement".
    r.appLimited = r.usage != BweUsage::Overuse &&
                   (heldByReceived || received < cfg_.appLimitedRatio * bwe_);

    window_ = Window{};
    window_.startUs = nowUs;
    *out = r;
    return true;
  }

  uint64_t generation() const { return generation_; }
  double thresholdMs() const { return thresholdMs_; }
  double trend() const { return trend_; }

 private:
  struct Window {
    uint64_t startUs = 0;
    uint64_t uniqueBytes = 0;
    uint64_t wireBytes = 0;
    uint32_t frames = 0;
    uint32_t syntheticFrames = 0;
    uint32_t samples = 0;
    BweUsage worst = BweUsage::Normal;
  };

  static uint32_t ClampU32(double v) {
    if (!(v > 0)) return 0;
    return v >= 4294967295.0 ? 4294967295u : static_cast<uint32_t>(v);
  }

  void ResetAll(uint64_t generation) {
    generation_ = generation;
    bwe_ = cfg_.initialBps;
    haveMaxSeq_ = false;
    maxSeq_ = 0;
    seen_.clear();
    ResetDetector();
    window_ = Window{};
  }

  void ResetDetector() {
    havePrev_ = false;
    accumulated_ = 0;
    smoothed_ = 0;
    history_.clear();
    firstArrivalUs_ = 0;
    trend_ = 0;
    prevTrend_ = 0;
    thresholdMs_ = cfg_.initialThresholdMs;
    overuseMs_ = 0;
    overuseCount_ = 0;
    lastThresholdUpdateUs_ = 0;
    state_ = BweUsage::Normal;
  }

  bool FirstArrival(uint32_t seq, uint16_t chunkIndex) {
    const uint64_t key = (static_cast<uint64_t>(seq) << 16) | chunkIndex;
    if (!seen_.insert(key).second) return false;
    // Bounded: forget frames more than dedupeSeqSpan behind the newest one seen.
    const uint32_t newestSeq =
        (haveMaxSeq_ && static_cast<int32_t>(seq - maxSeq_) < 0) ? maxSeq_ : seq;
    if (newestSeq > cfg_.dedupeSeqSpan) {
      const uint64_t floor = static_cast<uint64_t>(newestSeq - cfg_.dedupeSeqSpan) << 16;
      while (!seen_.empty() && *seen_.begin() < floor) seen_.erase(seen_.begin());
    }
    return true;
  }

  void Sample(uint64_t sendUs, uint64_t recvUs) {
    if (!havePrev_) {
      havePrev_ = true;
      prevSendUs_ = sendUs;
      prevRecvUs_ = recvUs;
      firstArrivalUs_ = recvUs;
      return;
    }
    const int64_t sendDelta = static_cast<int64_t>(sendUs - prevSendUs_);
    const int64_t recvDelta = static_cast<int64_t>(recvUs - prevRecvUs_);
    if (sendDelta <= 0 || static_cast<uint64_t>(sendDelta) > cfg_.maxSendGapUs || recvDelta < 0) {
      // A discontinuity, not a queue: restart the detector from this frame.
      ResetDetector();
      havePrev_ = true;
      prevSendUs_ = sendUs;
      prevRecvUs_ = recvUs;
      firstArrivalUs_ = recvUs;
      return;
    }
    prevSendUs_ = sendUs;
    prevRecvUs_ = recvUs;
    ++window_.samples;

    const double dMs = static_cast<double>(recvDelta - sendDelta) / 1000.0;
    accumulated_ += dMs;
    smoothed_ = cfg_.smoothing * smoothed_ + (1.0 - cfg_.smoothing) * accumulated_;
    const double tMs = static_cast<double>(recvUs - firstArrivalUs_) / 1000.0;
    history_.push_back({tMs, smoothed_});
    if (history_.size() > cfg_.window) history_.pop_front();

    if (history_.size() >= 2) {
      double meanX = 0, meanY = 0;
      for (const auto& p : history_) { meanX += p.x; meanY += p.y; }
      meanX /= history_.size();
      meanY /= history_.size();
      double num = 0, den = 0;
      for (const auto& p : history_) {
        num += (p.x - meanX) * (p.y - meanY);
        den += (p.x - meanX) * (p.x - meanX);
      }
      const double slope = den > 0 ? num / den : 0.0;
      prevTrend_ = trend_;
      trend_ = slope * static_cast<double>(std::min<size_t>(history_.size(), 60)) * cfg_.gain;
    }
    Detect(static_cast<double>(sendDelta) / 1000.0, recvUs);
  }

  void Detect(double tsDeltaMs, uint64_t nowUs) {
    const double t = trend_;
    if (t > thresholdMs_) {
      overuseMs_ += tsDeltaMs;
      ++overuseCount_;
      if (overuseMs_ > cfg_.overuseTimeMs && overuseCount_ > 1 && t >= prevTrend_) {
        overuseMs_ = 0;
        overuseCount_ = 0;
        state_ = BweUsage::Overuse;
      }
    } else if (t < -thresholdMs_) {
      overuseMs_ = 0;
      overuseCount_ = 0;
      state_ = BweUsage::Underuse;
    } else {
      overuseMs_ = 0;
      overuseCount_ = 0;
      state_ = BweUsage::Normal;
    }
    // Overuse outranks underuse outranks normal within a window (the enum is in that order).
    if (static_cast<uint8_t>(state_) > static_cast<uint8_t>(window_.worst)) window_.worst = state_;
    UpdateThreshold(t, nowUs);
  }

  void UpdateThreshold(double t, uint64_t nowUs) {
    if (lastThresholdUpdateUs_ == 0) lastThresholdUpdateUs_ = nowUs;
    const double absT = std::fabs(t);
    // A spike far beyond the threshold is not allowed to drag it (WebRTC's rule).
    if (absT > thresholdMs_ + 15.0) {
      lastThresholdUpdateUs_ = nowUs;
      return;
    }
    const double k = absT < thresholdMs_ ? cfg_.kDown : cfg_.kUp;
    const double dtMs = std::min(static_cast<double>(nowUs - lastThresholdUpdateUs_) / 1000.0, 100.0);
    thresholdMs_ += k * (absT - thresholdMs_) * dtMs;
    thresholdMs_ = std::clamp(thresholdMs_, 6.0, 600.0);
    lastThresholdUpdateUs_ = nowUs;
  }

  struct Point { double x; double y; };

  BweConfig cfg_;
  uint64_t generation_ = 0;
  double bwe_ = 0;
  bool haveMaxSeq_ = false;
  uint32_t maxSeq_ = 0;
  std::set<uint64_t> seen_;
  bool havePrev_ = false;
  uint64_t prevSendUs_ = 0;
  uint64_t prevRecvUs_ = 0;
  uint64_t firstArrivalUs_ = 0;
  double accumulated_ = 0;
  double smoothed_ = 0;
  std::deque<Point> history_;
  double trend_ = 0;
  double prevTrend_ = 0;
  double thresholdMs_ = 12.5;
  double overuseMs_ = 0;
  uint32_t overuseCount_ = 0;
  uint64_t lastThresholdUpdateUs_ = 0;
  BweUsage state_ = BweUsage::Normal;
  uint64_t lastReportUs_ = 0;
  Window window_;
};

}  // namespace remote60::native_poc
