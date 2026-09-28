#pragma once

// Clipboard image v1: how fast the bulk channel may send (plan r2 §2, "증속 합의" of
// clip_image_debate_2026-09-28.md).
//
// Role:    BulkRateController -- starts at a small trial rate, doubles it once per round trip while
//          the evidence says the path has room (initial probe), and after the first congestion only
//          grows linearly with elapsed time (at most +25 % of the post-decrease rate per second; no
//          exponential growth). Congestion halves the rate once per EVENT: the signals that trail a
//          decrease for the round already in flight are the same event, not new ones. Repeated
//          congestion pauses and probes rather than resuming on time alone.
// Thread:  the bulk serving thread only (not synchronised, pure).
// Callers: the viewer's bulk sender (clip_image_client), clip_image_core_test.
//
// The encoder target is not the link capacity, so nothing here derives "free bandwidth" from it
// (Codex 2). The cap is a configured value moved at runtime by the caller (SetCapBps) -- the
// minimum of 16 Mbps and the same-direction wire budget less media, FEC, retransmission and
// control; a budget that shrinks takes effect at once.
//
// Evidence a raise may NOT stand on (the window says so and the caller fills it): app-limited
// windows (the pacer ran dry), windows in which bulk yielded to control/video, retransmitted RTT
// samples, duplicate or bunched acknowledgements. Those are excluded before they reach here.

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

/** What makes loss count as congestion. */
enum class BulkLossRule : uint8_t {
  AnyEvent = 0,       // any loss event (the agreed rule)
  RttOrRoundRate,     // loss only with an RTT rise, or when the round lost more than lossRoundPerMille of its datagrams
};

/** What a raise needs besides clean RTTs. */
enum class BulkRaiseRule : uint8_t {
  UsedRate = 0,       // the window used >= 90 % of the rate, and was neither app-limited nor yielded (agreed)
  TimelyRound,        // the round's requested chunks arrived on time (valid pull RTT samples, all within the
                      // raise bounds) -- the pacer's idle time while the head-only channel waits is not held
                      // against it; yielded windows still hold
};

enum class BulkRateEvalUnit : uint8_t {
  WallClock = 0,  // one evaluation per evalWindowUs
  AckRound,       // one evaluation per completed round trip, never faster than max(SRTT, minEvalIntervalUs)
};

struct BulkRateConfig {
  uint32_t startBps = 256000;
  uint32_t capBps = 16000000;      // configured ceiling; SetCapBps applies the budget at runtime
  uint32_t floorBps = 64000;
  uint32_t probeBps = 64000;
  // Initial probe: x slowStartNumerator/slowStartDenominator per evaluation, until the first
  // congestion (or the cap).
  uint32_t slowStartNumerator = 2, slowStartDenominator = 1;
  // After the first congestion: linear, at most this percentage of the post-decrease rate per
  // second of elapsed time.
  uint32_t caPercentPerSec = 25;
  // When to evaluate.
  BulkRateEvalUnit evalUnit = BulkRateEvalUnit::AckRound;
  uint64_t evalWindowUs = 2000000;      // WallClock
  uint32_t ackRoundsPerEval = 1;        // AckRound
  uint64_t minEvalIntervalUs = 100000;  // AckRound: never more often than max(SRTT, this)
  // Congestion is an RTT over BOTH twice the baseline and the baseline + 50 ms: "twice" alone fires
  // on LAN jitter (1 ms -> 2 ms), "+50 ms" alone never fires on a long path.
  uint64_t rttRiseAbsUs = 50000;
  uint64_t pullRaiseAbsUs = 15000;   // a raise needs the pull RTT within max(1.5x, +15 ms) of baseline
  uint64_t rttUpSlackUs = 30000;     // a raise needs the control RTT within +30 ms of baseline
  // Loss that counts as congestion: more than this many per mille of the window's datagrams.
  // 0 = any loss event (the agreed rule).
  uint32_t lossTolerancePerMille = 0;
  // Over how many recent data datagrams the loss ratio is judged (the caller aggregates; 0 = the
  // evaluation window alone). Measurement option, not the agreed rule.
  uint32_t lossHorizonDatagrams = 0;
  // Candidate rules under discussion (defaults = the agreed ones).
  BulkLossRule lossRule = BulkLossRule::AnyEvent;
  uint32_t lossRoundPerMille = 50;   // RttOrRoundRate: > 5 % of the round's datagrams lost
  BulkRaiseRule raiseRule = BulkRaiseRule::UsedRate;
  uint32_t pauseAfterDecreases = 3;
  uint64_t pauseUs = 2000000;
  uint64_t probeUs = 1000000;
};

/** One evaluation's worth of evidence. Zero RTTs mean "not measured in this window". */
struct BulkRateWindow {
  uint32_t lossOrNacks = 0;        // loss EVENTS (messages with a resend), not datagrams
  uint64_t pullRttP50Us = 0;       // chunk sent -> the pull it completed arrives, median of valid samples
  uint64_t pingRttUs = 0;          // the control channel's ping RTT
  uint64_t deliveredBps = 0;       // what actually left in the window
  uint32_t datagramsSent = 0;      // data datagrams sent in the window (the loss ratio's base)
  bool appLimited = false;         // the sender ran out of work: says nothing about room on the path
  bool yielded = false;            // bulk stood aside for control / video in the window
  uint32_t lostDatagrams = 0;      // datagrams resent after they had left (RttOrRoundRate's ratio)
  uint32_t rttSamples = 0;         // valid pull RTT samples in the window (TimelyRound needs >= 1)
};

enum class BulkRateAction : uint8_t { Hold = 0, Raise, Lower, Pause, Probe };

/**
 * Which pull round-trip samples are evidence, and the smoothed RTT the evaluation interval needs.
 * Excluded: a chunk any fragment of which was resent (Karn -- the sample would time the resend,
 * or the original, nobody knows which), a pull that repeats one already counted (duplicate), and a
 * pull that arrived bunched right behind the previous one (ACK compression -- it measures the
 * queue that released them together, not the path). Bunched = sooner than a quarter of one
 * chunk's send time after the previous pull, and never less than 1 ms.
 */
class BulkRttEstimator {
 public:
  /** True when the sample was used. `sinceLastPullUs` 0 = the first pull of the transfer. */
  bool OnSample(uint64_t rttUs, uint64_t sinceLastPullUs, uint64_t chunkSendUs, bool resent, bool duplicate) {
    if (resent || duplicate || rttUs == 0) return false;
    const uint64_t bunched = (std::max<uint64_t>)(1000, chunkSendUs / 4);
    if (sinceLastPullUs != 0 && sinceLastPullUs < bunched) return false;
    srttUs_ = srttUs_ ? (srttUs_ * 7 + rttUs) / 8 : rttUs;
    return true;
  }
  uint64_t srttUs() const { return srttUs_; }

 private:
  uint64_t srttUs_ = 0;
};

class BulkRateController {
 public:
  explicit BulkRateController(BulkRateConfig c = {}) : c_(c), rate_((std::min)(c.startBps, c.capBps)) {}

  /** The rate to feed the token bucket now (0 while paused). */
  uint32_t RateBps(uint64_t nowUs) const {
    if (nowUs < pauseUntilUs_) return 0;
    if (nowUs < probeUntilUs_) return c_.probeBps;
    return rate_;
  }

  /**
   * Whether an evaluation is due. `roundsCompleted` is the caller's running count of completed round
   * trips; `srttUs` its smoothed pull RTT (0 = none yet). Only AckRound reads them.
   */
  bool Due(uint64_t nowUs, uint64_t roundsCompleted, uint64_t srttUs = 0) const {
    if (c_.evalUnit == BulkRateEvalUnit::WallClock) return nowUs - lastEvalUs_ >= c_.evalWindowUs;
    const uint64_t minGap = (std::max)(srttUs, c_.minEvalIntervalUs);
    return roundsCompleted >= lastEvalRounds_ + (std::max<uint32_t>)(1, c_.ackRoundsPerEval) &&
           nowUs - lastEvalUs_ >= minGap;
  }

  /** Records that an evaluation happened now (the caller resets its window evidence too). */
  void MarkEvaluated(uint64_t nowUs, uint64_t roundsCompleted) {
    lastEvalUs_ = nowUs;
    lastEvalRounds_ = roundsCompleted;
  }

  /**
   * Moves the cap (min(16 Mbps, budget), recomputed by the caller). A rate above the new cap is
   * brought down to it at once; a cap raised above the rate does not by itself raise the rate.
   */
  void SetCapBps(uint32_t capBps) {
    c_.capBps = (std::max)(capBps, c_.floorBps);
    if (rate_ > c_.capBps) rate_ = c_.capBps;
  }

  /**
   * `roundsCompleted`: the round count at this evaluation (AckRound). Congestion seen before a full
   * round has passed since the last decrease is the same event and does not decrease again.
   */
  BulkRateAction Evaluate(const BulkRateWindow& w, uint64_t nowUs, uint64_t roundsCompleted = 0) {
    const uint64_t sinceLastUs = lastRaiseClockUs_ ? nowUs - lastRaiseClockUs_ : 0;
    lastRaiseClockUs_ = nowUs;
    if (nowUs < pauseUntilUs_) return BulkRateAction::Pause;
    // Baselines: the best seen, so a slow first window does not become the yardstick.
    if (w.pullRttP50Us && (!basePullRttUs_ || w.pullRttP50Us < basePullRttUs_)) basePullRttUs_ = w.pullRttP50Us;
    if (w.pingRttUs && (!basePingRttUs_ || w.pingRttUs < basePingRttUs_)) basePingRttUs_ = w.pingRttUs;

    const bool rttCongested = Risen(w.pullRttP50Us, basePullRttUs_) || Risen(w.pingRttUs, basePingRttUs_);
    const bool lossCongested =
        w.lossOrNacks > 0 &&
        static_cast<uint64_t>(w.lossOrNacks) * 1000ull > static_cast<uint64_t>(c_.lossTolerancePerMille) * w.datagramsSent;
    bool congested = lossCongested || rttCongested;
    if (c_.lossRule == BulkLossRule::RttOrRoundRate) {
      // Random loss on a lossy path is not a queue: loss counts only with an RTT rise, or when the
      // round lost more than lossRoundPerMille of its datagrams.
      const bool heavy = w.datagramsSent > 0 &&
                         static_cast<uint64_t>(w.lostDatagrams) * 1000ull >
                             static_cast<uint64_t>(c_.lossRoundPerMille) * w.datagramsSent;
      congested = rttCongested || (w.lossOrNacks > 0 && heavy);
    }
    const bool inRecovery = haveDecreased_ && roundsCompleted != 0 && roundsCompleted <= recoveryEndRound_;
    if (congested) {
      if (inRecovery) return BulkRateAction::Hold;  // the trailing signals of the event just answered
      slowStart_ = false;
      rate_ = (std::max)(c_.floorBps, rate_ / 2);
      caBaseBps_ = rate_;
      haveDecreased_ = true;
      recoveryEndRound_ = roundsCompleted + 1;  // the round already in flight belongs to this event
      if (++decreases_ >= c_.pauseAfterDecreases) {
        decreases_ = 0;
        pauseUntilUs_ = nowUs + c_.pauseUs;
        probeUntilUs_ = pauseUntilUs_ + c_.probeUs;
        return BulkRateAction::Pause;
      }
      return BulkRateAction::Lower;
    }
    if (!inRecovery) decreases_ = 0;
    if (nowUs < probeUntilUs_) return BulkRateAction::Probe;  // the probe window must finish first
    const bool timely = c_.raiseRule == BulkRaiseRule::TimelyRound;
    if (inRecovery || w.yielded || (!timely && w.appLimited)) return BulkRateAction::Hold;
    const bool pullOk = !w.pullRttP50Us || !basePullRttUs_ ||
                        w.pullRttP50Us <= (std::max)(basePullRttUs_ * 3 / 2, basePullRttUs_ + c_.pullRaiseAbsUs);
    const bool pingOk = !w.pingRttUs || !basePingRttUs_ || w.pingRttUs <= basePingRttUs_ + c_.rttUpSlackUs;
    const bool usedIt = timely ? (w.rttSamples > 0 && w.lossOrNacks == 0)
                               : w.deliveredBps * 10 >= static_cast<uint64_t>(rate_) * 9;
    if (!(pullOk && pingOk && usedIt && rate_ < c_.capBps)) return BulkRateAction::Hold;
    uint64_t next = 0;
    if (slowStart_) {
      next = static_cast<uint64_t>(rate_) * c_.slowStartNumerator / c_.slowStartDenominator;
    } else {
      // Linear in elapsed time; a long hold does not bank a jump (at most one second counts).
      const uint64_t dt = (std::min<uint64_t>)(sinceLastUs, 1000000);
      const uint64_t inc = static_cast<uint64_t>(caBaseBps_) * c_.caPercentPerSec * dt / 100ull / 1000000ull;
      next = static_cast<uint64_t>(rate_) + (std::max<uint64_t>)(inc, 1);
    }
    rate_ = static_cast<uint32_t>((std::min<uint64_t>)(c_.capBps, next));
    if (rate_ >= c_.capBps && slowStart_) {
      slowStart_ = false;
      caBaseBps_ = rate_;
    }
    return BulkRateAction::Raise;
  }

  uint32_t rate() const { return rate_; }
  uint32_t capBps() const { return c_.capBps; }
  bool inSlowStart() const { return slowStart_; }
  const BulkRateConfig& config() const { return c_; }

 private:
  bool Risen(uint64_t v, uint64_t base) const {
    if (!v || !base) return false;
    return v > (std::max)(base * 2, base + c_.rttRiseAbsUs);
  }

  BulkRateConfig c_;
  uint32_t rate_;
  bool slowStart_ = true;
  uint32_t caBaseBps_ = 0;
  bool haveDecreased_ = false;
  uint64_t recoveryEndRound_ = 0;
  uint64_t basePullRttUs_ = 0;
  uint64_t basePingRttUs_ = 0;
  uint32_t decreases_ = 0;
  uint64_t pauseUntilUs_ = 0;
  uint64_t probeUntilUs_ = 0;
  uint64_t lastEvalUs_ = 0;
  uint64_t lastEvalRounds_ = 0;
  uint64_t lastRaiseClockUs_ = 0;
};

}  // namespace remote60::native_poc
