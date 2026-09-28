#pragma once

// Clipboard image v1: how fast the bulk channel may send (plan r2 §2).
//
// Role:    BulkRateController -- starts at a small trial rate (256 kbps), raises it in bounded steps
//          while the evidence says the path has room, halves it on congestion, and after repeated
//          congestion pauses and probes rather than resuming on time alone.
// Thread:  the bulk pacer thread only (not synchronised, pure).
// Callers: the bulk sender's pacer, bulk_rate_controller_test (via clip_image_core_test).
//
// The encoder target is not the link capacity, so nothing here derives "free bandwidth" from it
// (Codex 2). Every datagram the bulk sender puts on the wire -- first sends and retransmits --
// goes through the token bucket this rate feeds.

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

struct BulkRateConfig {
  uint32_t startBps = 256000;
  uint32_t capBps = 2000000;       // direction A (the viewer's uplink)
  uint32_t floorBps = 64000;
  uint32_t probeBps = 64000;
  uint32_t upNumerator = 3, upDenominator = 2;   // x1.5
  // Congestion is an RTT over BOTH twice the baseline and the baseline + 50 ms: "twice" alone fires
  // on LAN jitter (1 ms -> 2 ms), "+50 ms" alone never fires on a long path.
  uint64_t rttRiseAbsUs = 50000;
  uint64_t pullRaiseAbsUs = 15000; // a raise needs the pull RTT within max(1.5x, +15 ms) of baseline
  uint64_t rttUpSlackUs = 30000;   // a raise needs the control RTT within +30 ms of baseline
  uint32_t pauseAfterDecreases = 3;
  uint64_t pauseUs = 2000000;
  uint64_t probeUs = 1000000;
};

/** One evaluation window (2 s) of evidence. Zero RTTs mean "not measured in this window". */
struct BulkRateWindow {
  uint32_t lossOrNacks = 0;        // bulk datagrams lost or NACKed
  uint64_t pullRttP50Us = 0;       // BulkPull -> BulkChunk round trip, median
  uint64_t pingRttUs = 0;          // the control channel's ping RTT
  uint64_t deliveredBps = 0;       // what actually arrived in the window
};

enum class BulkRateAction : uint8_t { Hold = 0, Raise, Lower, Pause, Probe };

class BulkRateController {
 public:
  explicit BulkRateController(BulkRateConfig c = {}) : c_(c), rate_(c.startBps) {}

  /** The rate to feed the token bucket now (0 while paused). */
  uint32_t RateBps(uint64_t nowUs) const {
    if (nowUs < pauseUntilUs_) return 0;
    if (nowUs < probeUntilUs_) return c_.probeBps;
    return rate_;
  }

  BulkRateAction Evaluate(const BulkRateWindow& w, uint64_t nowUs) {
    if (nowUs < pauseUntilUs_) return BulkRateAction::Pause;
    // Baselines: the best seen, so a slow first window does not become the yardstick.
    if (w.pullRttP50Us && (!basePullRttUs_ || w.pullRttP50Us < basePullRttUs_)) basePullRttUs_ = w.pullRttP50Us;
    if (w.pingRttUs && (!basePingRttUs_ || w.pingRttUs < basePingRttUs_)) basePingRttUs_ = w.pingRttUs;

    const bool rttCongested = Risen(w.pullRttP50Us, basePullRttUs_) || Risen(w.pingRttUs, basePingRttUs_);
    const bool congested = w.lossOrNacks > 0 || rttCongested;
    if (congested) {
      rate_ = std::max(c_.floorBps, rate_ / 2);
      if (++decreases_ >= c_.pauseAfterDecreases) {
        decreases_ = 0;
        pauseUntilUs_ = nowUs + c_.pauseUs;
        probeUntilUs_ = pauseUntilUs_ + c_.probeUs;
        return BulkRateAction::Pause;
      }
      return BulkRateAction::Lower;
    }
    decreases_ = 0;
    if (nowUs < probeUntilUs_) return BulkRateAction::Probe;  // the probe window must finish first
    const bool pullOk = !w.pullRttP50Us || !basePullRttUs_ ||
                        w.pullRttP50Us <= std::max(basePullRttUs_ * 3 / 2, basePullRttUs_ + c_.pullRaiseAbsUs);
    const bool pingOk = !w.pingRttUs || !basePingRttUs_ || w.pingRttUs <= basePingRttUs_ + c_.rttUpSlackUs;
    const bool usedIt = w.deliveredBps * 10 >= static_cast<uint64_t>(rate_) * 9;
    if (pullOk && pingOk && usedIt && rate_ < c_.capBps) {
      rate_ = std::min<uint64_t>(c_.capBps, static_cast<uint64_t>(rate_) * c_.upNumerator / c_.upDenominator);
      return BulkRateAction::Raise;
    }
    return BulkRateAction::Hold;
  }

  uint32_t rate() const { return rate_; }

 private:
  bool Risen(uint64_t v, uint64_t base) const {
    if (!v || !base) return false;
    return v > std::max(base * 2, base + c_.rttRiseAbsUs);
  }

  BulkRateConfig c_;
  uint32_t rate_;
  uint64_t basePullRttUs_ = 0;
  uint64_t basePingRttUs_ = 0;
  uint32_t decreases_ = 0;
  uint64_t pauseUntilUs_ = 0;
  uint64_t probeUntilUs_ = 0;
};

}  // namespace remote60::native_poc
