#pragma once

// Clipboard image v1: how fast the bulk channel may send ("증속 합의" + "증속 2차 합의" of
// clip_image_debate_2026-09-28.md).
//
// Role:    BulkRateController -- starts at a small trial rate, doubles it once per round trip while
//          the evidence says the path has room (initial probe), and after the first congestion only
//          grows linearly with elapsed time (at most +25 % of the post-decrease rate per second).
//          What counts as congestion (2nd agreement ①):
//            * a delay rise -- pull or control RTT over max(2x, +50 ms) of its baseline;
//            * a retransmission timeout (a whole message resent by the timer: progress was lost);
//            * loss together with a weak delay rise (over max(1.25x, +10 ms));
//            * sustained high loss: over lossHighPerMille of the unique original fragments across a
//              rolling horizon with enough samples (②; too few samples = Unknown, never "low").
//          An isolated, recoverable loss is NOT congestion: that round holds and the channel
//          retransmits. Congestion halves the rate once per EVENT (the signals trailing a decrease
//          for the round already in flight are the same event); repeated congestion pauses and
//          probes rather than resuming on time alone.
//          A raise (③) needs pending work, fresh on-time pull round trips within the raise bounds, a
//          stable control RTT, no loss in the round, and no yield to control/video; a raise that
//          brings no goodput gain is followed by Hold until new headroom is seen.
// Thread:  the bulk serving thread only (not synchronised, pure).
// Callers: the viewer's bulk sender (clip_image_client), clip_image_core_test.
//
// The cap is the budget: SetCapBps moves it at runtime (min(16 Mbps, same-direction wire budget
// less media, FEC, retransmission, control)); a shrink applies at once, and a cap of 0 sends nothing.
// The floor bounds only the controller's own decreases -- it never overrides the cap (④).

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <set>

namespace remote60::native_poc {

enum class BulkRateEvalUnit : uint8_t {
  WallClock = 0,  // one evaluation per evalWindowUs
  AckRound,       // one evaluation per completed round trip, never faster than max(SRTT, minEvalIntervalUs)
};

struct BulkRateConfig {
  uint32_t startBps = 256000;
  uint32_t capBps = 16000000;      // configured ceiling; SetCapBps applies the budget at runtime
  uint32_t floorBps = 64000;       // lower bound of the controller's decreases (not of the cap)
  uint32_t probeBps = 64000;
  // Initial probe: x slowStartNumerator/slowStartDenominator per raise, until the first congestion.
  uint32_t slowStartNumerator = 2, slowStartDenominator = 1;
  // After the first congestion: linear, at most this percentage of the post-decrease rate per second.
  uint32_t caPercentPerSec = 25;
  // When to evaluate.
  BulkRateEvalUnit evalUnit = BulkRateEvalUnit::AckRound;
  uint64_t evalWindowUs = 2000000;      // WallClock
  uint32_t ackRoundsPerEval = 1;        // AckRound
  uint64_t minEvalIntervalUs = 100000;  // AckRound: never more often than max(SRTT, this)
  // Delay: strong rise = over max(2x, +50 ms) of baseline; weak rise = over max(1.25x, +10 ms).
  uint64_t rttRiseAbsUs = 50000;
  uint32_t weakRisePercent = 125;
  uint64_t weakRiseAbsUs = 10000;
  uint64_t pullRaiseAbsUs = 15000;   // a raise needs the pull RTT within max(1.5x, +15 ms) of baseline
  uint64_t rttUpSlackUs = 30000;     // a raise needs the control RTT within +30 ms of baseline
  // Loss rate over a rolling horizon of unique original fragments (②).
  uint32_t lossHighPerMille = 50;         // experimental threshold (5 %)
  uint32_t lossHorizonFragments = 500;    // the horizon ends once this many fragments are covered
  uint32_t lossMinSamples = 300;          // fewer fragments than this = Unknown
  uint64_t lossHorizonMaxUs = 10000000;   // and never older than this
  // A raise with no goodput gain holds (③): judged on the smoothed goodput (EWMA over loss-free
  // windows), the settle window after the raise skipped; "no gain" = not above what it was when the
  // rate was raised. Raising resumes on new headroom (smoothed goodput up by headroomPercent) or
  // after plateauRetryUs.
  uint32_t headroomPercent = 10;
  uint64_t plateauRetryUs = 5000000;
  uint32_t pauseAfterDecreases = 3;
  uint64_t pauseUs = 2000000;
  uint64_t probeUs = 1000000;
};

/** One evaluation's worth of evidence. Zero RTTs mean "not measured in this window". */
struct BulkRateWindow {
  uint32_t lossEvents = 0;         // messages with at least one fragment resent (recoverable losses)
  uint64_t pullRttP50Us = 0;       // chunk sent -> the pull it completed arrives, median of valid samples
  uint64_t pullSrttUs = 0;         // the smoothed pull RTT (the evaluation interval; delay if no current)
  uint64_t pullCurrentUs = 0;      // the current pull delay: min of the last 4 valid samples (0 = none yet)
  uint64_t pingRttUs = 0;          // the control channel's ping RTT (0 = none fresh)
  uint64_t deliveredBps = 0;       // what the pacer let out in the window
  uint32_t uniqueFragmentsSent = 0;  // original data fragments first sent in the window
  uint32_t uniqueFragmentsLost = 0;  // original fragments that needed a resend (each counted once)
  uint32_t rtoEvents = 0;          // whole messages resent by the retransmission timer
  uint32_t rttSamples = 0;         // valid (fresh, not resent, not bunched) pull RTT samples
  uint64_t goodputBps = 0;         // completed chunk bytes in the window
  bool workPending = true;         // the host was asking for more (a pull was served in the window)
  bool yielded = false;            // bulk stood aside for control / video in the window
};

enum class BulkRateAction : uint8_t { Hold = 0, Raise, Lower, Pause, Probe };
enum class BulkLossState : uint8_t { Unknown = 0, Low, High };

/**
 * Which pull round-trip samples are evidence, and the smoothed RTT the evaluation interval needs.
 * Excluded: a chunk any fragment of which was resent (Karn), a pull that repeats one already
 * counted (duplicate), and a pull bunched right behind the previous one (ACK compression) -- sooner
 * than a quarter of one chunk's send time after it, and never less than 1 ms -- and a lone spike (see
 * OnSample).
 */
class BulkRttEstimator {
 public:
  bool OnSample(uint64_t rttUs, uint64_t sinceLastPullUs, uint64_t chunkSendUs, bool resent, bool duplicate) {
    if (resent || duplicate || rttUs == 0) return false;
    const uint64_t bunched = (std::max<uint64_t>)(1000, chunkSendUs / 4);
    if (sinceLastPullUs != 0 && sinceLastPullUs < bunched) return false;
    // The current delay (LEDBAT's filter): the minimum of the last few valid samples, spikes included.
    // A queue lifts every sample; a pull the host had to resend -- once, twice in a row, or its
    // follower waiting behind it on the head-only channel -- lifts a few (measured: 296 then 543 ms
    // on a 42 ms path, while the samples around them stayed at 42-77 ms).
    recent_[recentNext_] = rttUs;
    recentNext_ = (recentNext_ + 1) % kRecent;
    if (recentCount_ < kRecent) ++recentCount_;
    // A lone spike -- over max(2 x SRTT, SRTT + 100 ms) -- is held back until the next sample says
    // whether it is a queue (the next one is high too: both count) or a pull the host had to resend
    // after the reverse path lost it (measured: 250 ms on a 40 ms path, alone). Only one is held.
    if (srttUs_ && rttUs > (std::max)(srttUs_ * 2, srttUs_ + 100000)) {
      if (!heldSpikeUs_) {
        heldSpikeUs_ = rttUs;
        return false;
      }
      const uint64_t held = heldSpikeUs_;
      heldSpikeUs_ = 0;
      // ...unless it is the lost pull's follower: the host's channel is head-only, so the next pull
      // waited behind the resend and lands one round trip after it, a little less late (measured: 17
      // of 17 such pairs 40-43 ms apart, the second 15-100 ms lower). Not a queue -- both dropped.
      if (sinceLastPullUs != 0 && sinceLastPullUs < srttUs_ * 3 / 2 && rttUs < held) return false;
      srttUs_ = (srttUs_ * 7 + held) / 8;
    } else {
      heldSpikeUs_ = 0;  // the spike stood alone: dropped
    }
    srttUs_ = srttUs_ ? (srttUs_ * 7 + rttUs) / 8 : rttUs;
    return true;
  }
  uint64_t srttUs() const { return srttUs_; }
  uint64_t currentUs() const {
    uint64_t m = 0;
    for (uint32_t i = 0; i < recentCount_; ++i) m = m ? (std::min)(m, recent_[i]) : recent_[i];
    return m;
  }

 private:
  static constexpr uint32_t kRecent = 4;
  uint64_t recent_[kRecent] = {};
  uint32_t recentNext_ = 0;
  uint32_t recentCount_ = 0;
  uint64_t srttUs_ = 0;
  uint64_t heldSpikeUs_ = 0;
};

/**
 * The loss ratio's parts, from the sender's own transmissions (3rd agreement ①). Denominator: original
 * fragments sent for the first time. Numerator: original fragments that needed recovery, each ONCE --
 * a fragment NACKed and resent again and again is still one. Kept apart, never in the ratio: a resend
 * of a chunk the host already confirmed (its ACK was lost on the way back) and a repeat resend of a
 * fragment already counted. A message all of whose fragments came back through the resend path was
 * resent whole by the timer (RTO). Reset per bulk session (a new path starts from nothing).
 */
class BulkLossCounter {
 public:
  enum class Resend : uint8_t { NewLoss = 0, Repeat, AckLost };
  struct Window {
    uint32_t sent = 0, lost = 0, rto = 0;
  };

  void OnOriginal() { ++win_.sent; }

  Resend OnResend(uint32_t seq, uint16_t fragIndex, uint16_t fragCount, bool chunkConfirmed) {
    if (chunkConfirmed) {
      ++ackLost_;
      return Resend::AckLost;
    }
    const uint64_t key = (static_cast<uint64_t>(seq) << 16) | fragIndex;
    if (!keys_.insert(key).second) {
      ++repeats_;
      return Resend::Repeat;
    }
    ++win_.lost;
    if (++fragsPerSeq_[seq] == fragCount) ++win_.rto;
    while (fragsPerSeq_.size() > 64) fragsPerSeq_.erase(fragsPerSeq_.begin());
    if (keys_.size() > 4096) keys_.erase(keys_.begin());
    return Resend::NewLoss;
  }

  /** This window's counts, and a fresh window. */
  Window Take() {
    const Window w = win_;
    win_ = {};
    return w;
  }
  uint64_t ackLostResends() const { return ackLost_; }
  uint64_t repeatResends() const { return repeats_; }

  void Reset() { *this = BulkLossCounter{}; }

 private:
  Window win_;
  std::set<uint64_t> keys_;              // (seq << 16 | frag) already counted as lost
  std::map<uint32_t, uint32_t> fragsPerSeq_;
  uint64_t ackLost_ = 0;
  uint64_t repeats_ = 0;
};

class BulkRateController {
 public:
  explicit BulkRateController(BulkRateConfig c = {}) : c_(c), rate_(c.startBps) {}

  /** The rate to feed the token bucket now: the controller's rate, never above the cap (0 = send nothing). */
  uint32_t RateBps(uint64_t nowUs) const {
    uint32_t r = rate_;
    if (nowUs < pauseUntilUs_) r = 0;
    else if (nowUs < probeUntilUs_) r = c_.probeBps;
    return (std::min)(r, c_.capBps);
  }

  bool Due(uint64_t nowUs, uint64_t roundsCompleted, uint64_t srttUs = 0) const {
    if (c_.evalUnit == BulkRateEvalUnit::WallClock) return nowUs - lastEvalUs_ >= c_.evalWindowUs;
    const uint64_t minGap = (std::max)(srttUs, c_.minEvalIntervalUs);
    return roundsCompleted >= lastEvalRounds_ + (std::max<uint32_t>)(1, c_.ackRoundsPerEval) &&
           nowUs - lastEvalUs_ >= minGap;
  }

  void MarkEvaluated(uint64_t nowUs, uint64_t roundsCompleted) {
    lastEvalUs_ = nowUs;
    lastEvalRounds_ = roundsCompleted;
  }

  /**
   * The budget: min(16 Mbps, same-direction wire budget less reservations). Applies at once; 0 and
   * values under the floor are honoured as they are -- the floor never lifts the rate above the cap.
   */
  void SetCapBps(uint32_t capBps) {
    c_.capBps = capBps;
    if (rate_ > capBps) rate_ = (std::max)(capBps, c_.floorBps);  // the controller's state; RateBps clamps to the cap
  }

  /** The horizon's loss state (②), as of the last evaluation. */
  BulkLossState LossState() const { return lossState_; }

  BulkRateAction Evaluate(const BulkRateWindow& w, uint64_t nowUs, uint64_t roundsCompleted = 0) {
    // Linear growth is measured from the last raise or decrease (not from the last evaluation: holds
    // in between -- a settle window, a lossy round -- must not eat the +25 %/s).
    if (!lastRaiseClockUs_) lastRaiseClockUs_ = nowUs;
    const uint64_t sinceLastUs = nowUs - lastRaiseClockUs_;
    UpdateLossHorizon(w, nowUs);
    if (nowUs < pauseUntilUs_) return BulkRateAction::Pause;
    if (w.pullRttP50Us && (!basePullRttUs_ || w.pullRttP50Us < basePullRttUs_)) basePullRttUs_ = w.pullRttP50Us;
    if (w.pingRttUs && (!basePingRttUs_ || w.pingRttUs < basePingRttUs_)) basePingRttUs_ = w.pingRttUs;

    // Delay rises are judged on the CURRENT pull delay (min of the last few samples): one late sample --
    // a pull the host had to resend because the reverse path lost it, measured at 250 ms on a 40 ms
    // path, 543 ms when lost twice -- is not a queue. The SRTT was judged before and carried one such
    // pair for seconds after the path was back at 42 ms (measured: 3 decreases and a pause on it).
    const uint64_t pullDelay = w.pullCurrentUs ? w.pullCurrentUs : (w.pullSrttUs ? w.pullSrttUs : w.pullRttP50Us);
    const bool strongDelay = Risen(pullDelay, basePullRttUs_) || Risen(w.pingRttUs, basePingRttUs_);
    const bool weakDelay = WeakRisen(pullDelay, basePullRttUs_) || WeakRisen(w.pingRttUs, basePingRttUs_);
    const bool loss = w.lossEvents > 0 || w.uniqueFragmentsLost > 0;
    const bool congested = strongDelay || w.rtoEvents > 0 || (loss && weakDelay) ||
                           (loss && lossState_ == BulkLossState::High);
    const bool inRecovery = haveDecreased_ && roundsCompleted != 0 && roundsCompleted <= recoveryEndRound_;
    lastCause_ = (strongDelay ? 1u : 0u) | (w.rtoEvents > 0 ? 2u : 0u) | (loss && weakDelay ? 4u : 0u) |
                 (loss && lossState_ == BulkLossState::High ? 8u : 0u);
    if (congested) {
      if (inRecovery) return BulkRateAction::Hold;  // the trailing signals of the event just answered
      slowStart_ = false;
      rate_ = (std::max)(c_.floorBps, rate_ / 2);
      caBaseBps_ = rate_;
      lastRaiseClockUs_ = nowUs;
      haveDecreased_ = true;
      recoveryEndRound_ = roundsCompleted + 1;
      plateau_ = false;
      justRaised_ = false;
      if (++decreases_ >= c_.pauseAfterDecreases) {
        decreases_ = 0;
        pauseUntilUs_ = nowUs + c_.pauseUs;
        probeUntilUs_ = pauseUntilUs_ + c_.probeUs;
        return BulkRateAction::Pause;
      }
      return BulkRateAction::Lower;
    }
    if (!inRecovery) decreases_ = 0;
    if (nowUs < probeUntilUs_) return BulkRateAction::Probe;
    // An isolated, recoverable loss: hold this round, the channel resends (①).
    if (loss) return BulkRateAction::Hold;
    // Smoothed goodput over loss-free windows (a lossy round's goodput says nothing about room).
    if (w.goodputBps) goodputEwma_ = goodputEwma_ ? (goodputEwma_ * 3 + w.goodputBps) / 4 : w.goodputBps;
    // Did the last raise pay? No goodput gain -> hold until new headroom is seen (③). Judged on the
    // SECOND window after the raise: the first still carries what was sent at the old rate, and
    // judging it read every raise as a failure (measured: LAN stuck 5 s at 4 Mbps). Nothing is raised
    // while the verdict is pending.
    if (justRaised_ && settleWindows_ > 0) {
      --settleWindows_;
      return BulkRateAction::Hold;
    }
    if (justRaised_ && w.goodputBps) {
      justRaised_ = false;
      if (goodputEwma_ <= preRaiseGoodputBps_) {  // no gain at all
        plateau_ = true;
        plateauGoodputBps_ = goodputEwma_;
        plateauSinceUs_ = nowUs;
      }
    }
    if (plateau_) {
      const bool headroom = goodputEwma_ * 100 > plateauGoodputBps_ * (100 + c_.headroomPercent);
      if (headroom || nowUs - plateauSinceUs_ >= c_.plateauRetryUs) plateau_ = false;
      else return BulkRateAction::Hold;
    }
    if (inRecovery || w.yielded || !w.workPending) return BulkRateAction::Hold;
    // Fresh, on-time round trips are the evidence; nothing measured is no evidence.
    if (w.rttSamples == 0 || !w.pullRttP50Us) return BulkRateAction::Hold;
    const bool pullOk = !basePullRttUs_ ||
                        w.pullRttP50Us <= (std::max)(basePullRttUs_ * 3 / 2, basePullRttUs_ + c_.pullRaiseAbsUs);
    const bool pingOk = !w.pingRttUs || !basePingRttUs_ || w.pingRttUs <= basePingRttUs_ + c_.rttUpSlackUs;
    if (!(pullOk && pingOk && rate_ < c_.capBps)) return BulkRateAction::Hold;
    uint64_t next = 0;
    if (slowStart_) {
      next = static_cast<uint64_t>(rate_) * c_.slowStartNumerator / c_.slowStartDenominator;
    } else {
      const uint64_t dt = (std::min<uint64_t>)(sinceLastUs, 1000000);
      const uint64_t inc = static_cast<uint64_t>(caBaseBps_) * c_.caPercentPerSec * dt / 100ull / 1000000ull;
      next = static_cast<uint64_t>(rate_) + (std::max<uint64_t>)(inc, 1);
    }
    rate_ = static_cast<uint32_t>((std::min<uint64_t>)(c_.capBps, next));
    if (rate_ >= c_.capBps && slowStart_) {
      slowStart_ = false;
      caBaseBps_ = rate_;
    }
    justRaised_ = true;
    settleWindows_ = 1;
    lastRaiseClockUs_ = nowUs;
    preRaiseGoodputBps_ = goodputEwma_;
    return BulkRateAction::Raise;
  }

  uint32_t rate() const { return rate_; }
  uint32_t capBps() const { return c_.capBps; }
  bool inSlowStart() const { return slowStart_; }
  bool onPlateau() const { return plateau_; }
  /** Why the last evaluation saw congestion: 1 delay, 2 RTO, 4 loss + weak delay, 8 sustained loss (diagnostics). */
  uint32_t lastCause() const { return lastCause_; }
  const BulkRateConfig& config() const { return c_; }

 private:
  bool Risen(uint64_t v, uint64_t base) const {
    if (!v || !base) return false;
    return v > (std::max)(base * 2, base + c_.rttRiseAbsUs);
  }
  bool WeakRisen(uint64_t v, uint64_t base) const {
    if (!v || !base) return false;
    return v > (std::max)(base * c_.weakRisePercent / 100, base + c_.weakRiseAbsUs);
  }
  void UpdateLossHorizon(const BulkRateWindow& w, uint64_t nowUs) {
    if (w.uniqueFragmentsSent || w.uniqueFragmentsLost) horizon_.push_back({nowUs, w.uniqueFragmentsSent, w.uniqueFragmentsLost});
    // Drop what is too old, and what the newer entries already cover.
    while (!horizon_.empty() && nowUs - horizon_.front().atUs > c_.lossHorizonMaxUs) horizon_.pop_front();
    uint64_t sent = 0, lost = 0;
    for (const auto& e : horizon_) sent += e.sent;
    while (horizon_.size() > 1 && sent - horizon_.front().sent >= c_.lossHorizonFragments) {
      sent -= horizon_.front().sent;
      horizon_.pop_front();
    }
    for (const auto& e : horizon_) lost += e.lost;
    if (sent < c_.lossMinSamples) lossState_ = BulkLossState::Unknown;
    else lossState_ = lost * 1000ull > static_cast<uint64_t>(c_.lossHighPerMille) * sent ? BulkLossState::High
                                                                                           : BulkLossState::Low;
  }

  struct HorizonEntry {
    uint64_t atUs;
    uint64_t sent;
    uint64_t lost;
  };

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
  std::deque<HorizonEntry> horizon_;
  BulkLossState lossState_ = BulkLossState::Unknown;
  bool justRaised_ = false;
  uint32_t settleWindows_ = 0;
  uint64_t preRaiseGoodputBps_ = 0;
  bool plateau_ = false;
  uint64_t plateauGoodputBps_ = 0;
  uint64_t plateauSinceUs_ = 0;
  uint64_t goodputEwma_ = 0;
  uint32_t lastCause_ = 0;
};

}  // namespace remote60::native_poc
