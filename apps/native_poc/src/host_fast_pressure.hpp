#pragma once

// motion-settle r1 M2: the fast half of the quantiser-ceiling loop, for the AMD legacy profile.
//
// The field (2026-10-12, AMD host, ceiling 32): the first frames of a motion were 3-4x the per-frame
// payload budget (40-71 KB against ~16-19 KB) and took 66-157 ms each on the wire, so an animation or
// a scroll ran at an effective 6-9 fps; the rate governor answers only after two over-target SECONDS
// (32 -> 35 at 18:07:09, back at :12), long after a sub-second animation is over.
//
// FastPressure watches the real P access units this instance produces. A P far over the sustainable
// per-frame payload while the sender already holds a backlog -- or two consecutive Ps clearly over it --
// latches pressure; the caller then raises the governor's fast ceiling (RateGovernor::OnFastPressure),
// which feeds the ONE desired ceiling the governor computes and the ONE apply path
// (EncoderState::ReconcileGovernorCeiling). It reacts to the first oversized AU, so it cannot make that
// AU smaller -- only the ones after it.
//
// Its own bounds, apart from the governor's:
//   - a ceiling Set at most every minSetIntervalUs (the caller asks CanSetNow / NoteSet);
//   - at most maxBoostsPerWindow boosts per windowUs (a stop/start storm cannot turn into a QP toggle
//     storm): past it the fast path is degraded until the window clears;
//   - maxFailures refused or unconfirmed fast-path Sets on one encoder instance degrade it for that
//     instance -- an alternating 32/38 desired value must not reset the governor's per-value failure
//     budget for ever.
// Not used for keys (a key is large by nature), synthetic inputs (a kick or a refinement says nothing
// about motion) or a static-ceiling profile (NVIDIA: the plan owns the ceiling, runtime Sets stay 0).
//
// Thread: main encode loop only. Pure (time is an argument); host_fast_pressure_test.cpp.

#include <cstdint>
#include <deque>

namespace remote60::native_poc {

struct FastPressureConfig {
  bool enabled = true;
  uint32_t oversizePercent = 250;      // one P this far over the per-frame budget, with a backlog
  uint32_t consecutivePercent = 200;   // ... or this many in a row this far over it
  uint32_t consecutiveNeeded = 2;
  uint64_t minSetIntervalUs = 250000;  // between two ceiling Sets from the fast path
  uint32_t maxBoostsPerWindow = 6;
  uint64_t windowUs = 10000000;
  uint32_t maxFailures = 2;            // per encoder instance
};

class FastPressure {
 public:
  void Configure(const FastPressureConfig& c) { cfg_ = c; }
  const FastPressureConfig& config() const { return cfg_; }

  // A real (non-key, non-synthetic) P access unit of `bytes`, against the sustainable per-frame payload
  // `budgetBytes`; `backlogged` = the sender queue was not empty when it was handed over. Returns true
  // when this AU latches pressure.
  bool NoteRealP(uint64_t bytes, uint64_t budgetBytes, bool backlogged) {
    if (!cfg_.enabled || budgetBytes == 0) return false;
    const uint64_t percent = bytes * 100ULL / budgetBytes;
    consecutive_ = percent > cfg_.consecutivePercent ? consecutive_ + 1 : 0;
    const bool single = percent > cfg_.oversizePercent && backlogged;
    const bool run = consecutive_ >= cfg_.consecutiveNeeded;
    if (single || run) {
      consecutive_ = 0;
      ++latches_;
      return true;
    }
    return false;
  }
  // Whether a boost may be taken now (the toggle bound); records it when it may.
  bool TakeBoost(uint64_t nowUs) {
    while (!boosts_.empty() && nowUs >= boosts_.front() + cfg_.windowUs) boosts_.pop_front();
    if (boosts_.size() >= cfg_.maxBoostsPerWindow) {
      ++boostsRefused_;
      return false;
    }
    boosts_.push_back(nowUs);
    return true;
  }
  bool CanSetNow(uint64_t nowUs) const { return lastSetUs_ == 0 || nowUs >= lastSetUs_ + cfg_.minSetIntervalUs; }
  void NoteSet(uint64_t nowUs) {
    lastSetUs_ = nowUs;
    ++sets_;
  }
  // A fast-path Set the instance refused or did not confirm.
  void NoteFailure(uint64_t instance) {
    if (instance != failInstance_) {
      failInstance_ = instance;
      failures_ = 0;
    }
    ++failures_;
  }
  bool Degraded(uint64_t instance) const { return instance == failInstance_ && failures_ >= cfg_.maxFailures; }
  void ResetRun() { consecutive_ = 0; }

  uint64_t latches() const { return latches_; }
  uint64_t sets() const { return sets_; }
  uint64_t boosts_refused() const { return boostsRefused_; }
  uint32_t failures() const { return failures_; }

 private:
  FastPressureConfig cfg_;
  uint32_t consecutive_ = 0;
  std::deque<uint64_t> boosts_;
  uint64_t lastSetUs_ = 0;
  uint64_t failInstance_ = 0;
  uint32_t failures_ = 0;
  uint64_t latches_ = 0;
  uint64_t sets_ = 0;
  uint64_t boostsRefused_ = 0;
};

}  // namespace remote60::native_poc
