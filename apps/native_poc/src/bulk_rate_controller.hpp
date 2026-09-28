#pragma once

// Clipboard image v1: how fast the bulk channel may send (plan r2 §2).
//
// Role:    BulkRateController -- starts at a small trial rate, raises it in bounded steps while the
//          evidence says the path has room, halves it on congestion, and after repeated congestion
//          pauses and probes rather than resuming on time alone.
// Thread:  the bulk pacer's owner only (not synchronised, pure).
// Callers: the viewer's bulk sender (clip_image_client), clip_image_core_test.
//
// The encoder target is not the link capacity, so nothing here derives "free bandwidth" from it
// (Codex 2). Every datagram the bulk sender puts on the wire -- first sends and retransmits --
// goes through the token bucket this rate feeds.
//
// What is still being decided (the reviewer with Codex, 2026-09-28) is a matter of VALUES, so all of
// it is a parameter: how often to evaluate (a wall-clock window, or once per acknowledged round
// trip), how steeply to raise (a slow-start multiplier until the first congestion, then a gentler
// one), and the cap (a config value that the caller may move at runtime, e.g. to
// min(16 Mbps, wire budget - media)). The defaults are plan r2's: 2 s windows, x1.5, 2 Mbps.

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

enum class BulkRateEvalUnit : uint8_t {
  WallClock = 0,  // one evaluation per evalWindowUs
  AckRound,       // one evaluation per ackRoundsPerEval completed round trips (~ one RTT each)
};

struct BulkRateConfig {
  uint32_t startBps = 256000;
  uint32_t capBps = 2000000;       // direction A (the viewer's uplink); SetCapBps moves it at runtime
  uint32_t floorBps = 64000;
  uint32_t probeBps = 64000;
  // Raise steps. Slow start applies until the first congestion (or the cap), the second one after.
  // Equal by default, which is plan r2's single x1.5.
  uint32_t slowStartNumerator = 3, slowStartDenominator = 2;
  uint32_t upNumerator = 3, upDenominator = 2;
  // When to evaluate.
  BulkRateEvalUnit evalUnit = BulkRateEvalUnit::WallClock;
  uint64_t evalWindowUs = 2000000;  // WallClock
  uint32_t ackRoundsPerEval = 1;    // AckRound
  uint64_t minEvalIntervalUs = 0;   // AckRound: never more often than this, however short the RTT
  // Congestion is an RTT over BOTH twice the baseline and the baseline + 50 ms: "twice" alone fires
  // on LAN jitter (1 ms -> 2 ms), "+50 ms" alone never fires on a long path.
  uint64_t rttRiseAbsUs = 50000;
  uint64_t pullRaiseAbsUs = 15000; // a raise needs the pull RTT within max(1.5x, +15 ms) of baseline
  uint64_t rttUpSlackUs = 30000;   // a raise needs the control RTT within +30 ms of baseline
  uint32_t pauseAfterDecreases = 3;
  uint64_t pauseUs = 2000000;
  uint64_t probeUs = 1000000;
};

/** One evaluation's worth of evidence. Zero RTTs mean "not measured in this window". */
struct BulkRateWindow {
  uint32_t lossOrNacks = 0;        // bulk datagrams lost or NACKed
  uint64_t pullRttP50Us = 0;       // chunk sent -> the pull it completed arrives, median
  uint64_t pingRttUs = 0;          // the control channel's ping RTT
  uint64_t deliveredBps = 0;       // what actually arrived in the window
};

enum class BulkRateAction : uint8_t { Hold = 0, Raise, Lower, Pause, Probe };

class BulkRateController {
 public:
  explicit BulkRateController(BulkRateConfig c = {}) : c_(c), rate_(std::min(c.startBps, c.capBps)) {}

  /** The rate to feed the token bucket now (0 while paused). */
  uint32_t RateBps(uint64_t nowUs) const {
    if (nowUs < pauseUntilUs_) return 0;
    if (nowUs < probeUntilUs_) return c_.probeBps;
    return rate_;
  }

  /**
   * Whether an evaluation is due. `roundsCompleted` is the caller's running count of completed round
   * trips (a chunk acknowledged by the pull it triggered); only AckRound reads it.
   */
  bool Due(uint64_t nowUs, uint64_t roundsCompleted) const {
    if (c_.evalUnit == BulkRateEvalUnit::WallClock) return nowUs - lastEvalUs_ >= c_.evalWindowUs;
    return roundsCompleted >= lastEvalRounds_ + std::max<uint32_t>(1, c_.ackRoundsPerEval) &&
           nowUs - lastEvalUs_ >= c_.minEvalIntervalUs;
  }

  /** Records that an evaluation happened now (the caller resets its window evidence too). */
  void MarkEvaluated(uint64_t nowUs, uint64_t roundsCompleted) {
    lastEvalUs_ = nowUs;
    lastEvalRounds_ = roundsCompleted;
  }

  /**
   * Moves the cap (e.g. min(16 Mbps, wire budget - media), recomputed by the caller). A rate above
   * the new cap is brought down to it at once; a cap raised above the rate does not by itself raise
   * the rate -- the evidence still has to.
   */
  void SetCapBps(uint32_t capBps) {
    c_.capBps = std::max(capBps, c_.floorBps);
    if (rate_ > c_.capBps) rate_ = c_.capBps;
  }

  BulkRateAction Evaluate(const BulkRateWindow& w, uint64_t nowUs) {
    if (nowUs < pauseUntilUs_) return BulkRateAction::Pause;
    // Baselines: the best seen, so a slow first window does not become the yardstick.
    if (w.pullRttP50Us && (!basePullRttUs_ || w.pullRttP50Us < basePullRttUs_)) basePullRttUs_ = w.pullRttP50Us;
    if (w.pingRttUs && (!basePingRttUs_ || w.pingRttUs < basePingRttUs_)) basePingRttUs_ = w.pingRttUs;

    const bool rttCongested = Risen(w.pullRttP50Us, basePullRttUs_) || Risen(w.pingRttUs, basePingRttUs_);
    const bool congested = w.lossOrNacks > 0 || rttCongested;
    if (congested) {
      slowStart_ = false;
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
      const uint64_t num = slowStart_ ? c_.slowStartNumerator : c_.upNumerator;
      const uint64_t den = slowStart_ ? c_.slowStartDenominator : c_.upDenominator;
      rate_ = static_cast<uint32_t>(std::min<uint64_t>(c_.capBps, static_cast<uint64_t>(rate_) * num / den));
      if (rate_ >= c_.capBps) slowStart_ = false;
      return BulkRateAction::Raise;
    }
    return BulkRateAction::Hold;
  }

  uint32_t rate() const { return rate_; }
  uint32_t capBps() const { return c_.capBps; }
  bool inSlowStart() const { return slowStart_; }

 private:
  bool Risen(uint64_t v, uint64_t base) const {
    if (!v || !base) return false;
    return v > std::max(base * 2, base + c_.rttRiseAbsUs);
  }

  BulkRateConfig c_;
  uint32_t rate_;
  bool slowStart_ = true;
  uint64_t basePullRttUs_ = 0;
  uint64_t basePingRttUs_ = 0;
  uint32_t decreases_ = 0;
  uint64_t pauseUntilUs_ = 0;
  uint64_t probeUntilUs_ = 0;
  uint64_t lastEvalUs_ = 0;
  uint64_t lastEvalRounds_ = 0;
};

}  // namespace remote60::native_poc
