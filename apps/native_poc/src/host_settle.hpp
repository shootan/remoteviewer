#pragma once

// motion-settle r1 M1: a short, bounded refinement of the screen right after it stops changing.
//
// The field (2026-10-12, AMD host): after a scroll or an edit the last picture went out at the motion
// quality and was then improved only by the 1 Hz static refresh -- one small step a second, 3 s and
// more before small text was sharp again. Worse, when the LAST change of a burst was held back by the
// input gate (the wire was still busy with the burst), nothing re-served it: the trailing kick only
// flushes an input the encoder already holds, so the final picture first appeared with the next
// static refresh, up to a second late (M0 harness: noise -> still, first seen +1.9 s).
//
// SettleRefiner decides when to feed the cached LATEST capture to the encoder again as an ordinary P
// input (no forced key, no per-frame QP/bitrate override), a few times in quick succession, once the
// screen has been still for quietUs:
//   - an episode is armed by a real content change (a capture whose bytes differ from the last sent
//     one; repeats of the same picture and synthetic inputs never arm it) and bound to the identity of
//     what was captured: media/input epoch, stream generation, attachment (target) and content version;
//   - it starts after quietUs without a new change (a change while waiting re-arms; it is never sent
//     in place of a change -- a real frame waiting always goes first);
//   - it then submits up to `inputs` refinement inputs (hard ceiling kMaxInputs), at most one per
//     minSpacingUs and never while the sender queue holds anything, and gives up at deadlineUs after it
//     started -- both are bounds, not targets: an input the encoder refuses or a queue that never
//     empties ends the episode by time, not by retrying for ever;
//   - an identity change (new epoch, selection, resize, reconnect, lock/secure -- KickTryFill refuses
//     the cache then) cancels it, as does a new real change.
// The host cannot see decoded quality, so it never claims "sharp": the log says requested /
// submitted / emitted / expired / cancelled. The wire limiter, the 2 s ledger and the FEC/NACK budgets
// apply to these inputs like to any other P (no admit-always exception); a recovery or selection key
// owed to the viewer takes precedence (the caller does not serve a refinement while a key is pending).
//
// Thread: main encode loop only. Pure (time is an argument); host_settle_test.cpp.

#include <algorithm>
#include <cstdint>

namespace remote60::native_poc {

struct SettleIdentity {
  uint64_t mediaEpoch = 0;
  uint64_t inputEpoch = 0;
  uint64_t generation = 0;
  uint64_t attachment = 0;   // the capture target (window id / hwnd / monitor) the cache must match
  bool operator==(const SettleIdentity& o) const {
    return mediaEpoch == o.mediaEpoch && inputEpoch == o.inputEpoch && generation == o.generation &&
           attachment == o.attachment;
  }
  bool operator!=(const SettleIdentity& o) const { return !(*this == o); }
};

struct SettleConfig {
  bool enabled = true;
  uint64_t quietUs = 170000;      // still this long after the last real change
  uint32_t inputs = 8;            // refinement inputs per episode (clamped to [1, kMaxInputs]); 8: M0 harness, 3 vs 8
  uint64_t minSpacingUs = 33333;  // between two refinement inputs
  uint64_t deadlineUs = 1000000;  // after the episode starts: no further input
};

enum class SettleEnd : uint8_t { None, Complete, Expired, Cancelled, Superseded, Budget };
inline const char* settle_end_name(SettleEnd e) {
  switch (e) {
    case SettleEnd::None: return "none";
    case SettleEnd::Complete: return "complete";
    case SettleEnd::Expired: return "expired";
    case SettleEnd::Cancelled: return "cancelled";
    case SettleEnd::Superseded: return "superseded";
    case SettleEnd::Budget: return "budget";
  }
  return "?";
}

// What Poll() tells the loop this tick.
struct SettleAction {
  bool serve = false;          // feed the cached latest capture as a refinement input now
  bool started = false;        // an episode started this tick (log "requested")
  SettleEnd ended = SettleEnd::None;  // an episode ended this tick (log it)
};

class SettleRefiner {
 public:
  // r2 (Codex A): the hard ceilings. The default episode stays 8 inputs / 1 s; a comparison candidate
  // may raise it to these -- 30 accepted inputs, 60 submit attempts, 3 s from the episode's start -- as
  // bounds, never as a quota (a real frame, a key owed, a busy sender or an identity change still win).
  static constexpr uint32_t kMaxInputs = 30;
  static constexpr uint32_t kMaxAttempts = 60;
  static constexpr uint64_t kMaxDeadlineUs = 3000000;

  void Configure(const SettleConfig& c) {
    cfg_ = c;
    cfg_.inputs = std::clamp<uint32_t>(c.inputs, 1, kMaxInputs);
    cfg_.deadlineUs = std::clamp<uint64_t>(c.deadlineUs, 100000, kMaxDeadlineUs);
  }
  const SettleConfig& config() const { return cfg_; }

  // A real capture whose content differs from the last one sent (changePm > 0, or nothing to compare
  // with), whether it is then encoded or held back by a gate. Re-arms; an active episode is superseded.
  SettleEnd NoteRealChange(uint64_t nowUs, const SettleIdentity& id, uint64_t contentVersion) {
    SettleEnd ended = SettleEnd::None;
    if (state_ == State::Active) ended = End(SettleEnd::Superseded);
    if (!cfg_.enabled) return ended;
    state_ = State::Armed;
    armedId_ = id;
    lastChangeUs_ = nowUs;
    contentVersion_ = contentVersion;
    return ended;
  }

  // Once per loop tick. `cur` is the identity the cache would be served under now; `queueBusy` the
  // sender queue is not empty; `realWaiting` a real capture is ready to encode; `keyOwed` a key the
  // viewer needs is pending (it goes first; refinements wait, the deadline keeps running).
  SettleAction Poll(uint64_t nowUs, const SettleIdentity& cur, bool queueBusy, bool realWaiting, bool keyOwed) {
    SettleAction a;
    if (state_ == State::Idle) return a;
    if (cur != armedId_) {
      if (state_ == State::Active) a.ended = End(SettleEnd::Cancelled);
      state_ = State::Idle;
      return a;
    }
    if (state_ == State::Armed) {
      if (nowUs < lastChangeUs_ + cfg_.quietUs) return a;
      state_ = State::Active;
      startUs_ = nowUs;
      submitted_ = 0;
      attempts_ = 0;
      lastServeUs_ = 0;
      emittedThisEpisode_ = 0;
      emittedBytesThisEpisode_ = 0;
      ++episodes_;
      a.started = true;
    }
    // Active.
    if (nowUs >= startUs_ + cfg_.deadlineUs) {
      a.ended = End(submitted_ >= cfg_.inputs ? SettleEnd::Complete : SettleEnd::Expired);
      return a;
    }
    if (submitted_ >= cfg_.inputs) {
      a.ended = End(SettleEnd::Complete);
      return a;
    }
    // r3: a byte budget per episode (Codex r2 A: "a small fixed bundle + wall time / byte / backlog
    // budgets first") -- the refinement must leave the 2 s wire window room for the next motion: an
    // A-sized train under a low ceiling used it up and a motion frame waited 477 ms for tokens.
    if (byteBudget_ > 0 && emittedBytesThisEpisode_ >= byteBudget_) {
      a.ended = End(SettleEnd::Budget);
      return a;
    }
    if (queueBusy || realWaiting || keyOwed) return a;
    if (lastServeUs_ != 0 && nowUs < lastServeUs_ + cfg_.minSpacingUs) return a;
    // Every serve counts against the bound, accepted or not: a cache the identity check refuses or
    // an encoder that keeps saying "not now" cannot keep the episode going past twice its inputs (<= 60).
    if (attempts_ >= std::min(kMaxAttempts, cfg_.inputs * 2)) {
      a.ended = End(SettleEnd::Expired);
      return a;
    }
    ++attempts_;
    lastServeUs_ = nowUs;
    a.serve = true;
    return a;
  }

  // The served input was handed to the encoder (KickTryFill found the cache, encode accepted it).
  void NoteSubmitted() {
    if (state_ == State::Active) {
      ++submitted_;
      ++submittedTotal_;
    }
  }
  // An AU produced from a settle input left the encoder (telemetry only; the host cannot see quality).
  void NoteEmitted(uint64_t bytes) {
    ++emittedTotal_;
    emittedBytesTotal_ += bytes;
    ++emittedThisEpisode_;  // the latest episode's, including AUs the async MFT returns after it ended
    emittedBytesThisEpisode_ += bytes;
  }

  // Per-instance bounds (r3 candidate): the caller widens them on the AMD live-ceiling instance only.
  // Takes effect from the next episode; clamped to the hard ceilings like Configure.
  void SetBounds(uint32_t inputs, uint64_t deadlineUs) {
    cfg_.inputs = std::clamp<uint32_t>(inputs, 1, kMaxInputs);
    cfg_.deadlineUs = std::clamp<uint64_t>(deadlineUs, 100000, kMaxDeadlineUs);
  }
  // The episode's byte budget (0 = none), set by the caller from the current wire rate.
  void SetByteBudget(uint64_t bytes) { byteBudget_ = bytes; }
  bool active() const { return state_ == State::Active; }
  bool armed() const { return state_ == State::Armed; }
  uint32_t submitted() const { return submitted_; }
  uint32_t attempts() const { return attempts_; }
  uint64_t start_us() const { return startUs_; }
  uint64_t last_change_us() const { return lastChangeUs_; }
  uint64_t content_version() const { return contentVersion_; }
  uint64_t episodes() const { return episodes_; }
  uint64_t submitted_total() const { return submittedTotal_; }
  uint64_t emitted_total() const { return emittedTotal_; }
  uint64_t emitted_bytes_total() const { return emittedBytesTotal_; }
  uint64_t ended_total(SettleEnd e) const { return endedCount_[static_cast<int>(e)]; }
  // AUs from settle inputs since the latest episode started (the async MFT may return the last ones
  // after the episode ended).
  uint32_t emitted_this_episode() const { return emittedThisEpisode_; }
  uint64_t emitted_bytes_this_episode() const { return emittedBytesThisEpisode_; }

 private:
  enum class State { Idle, Armed, Active };
  SettleEnd End(SettleEnd why) {
    state_ = State::Idle;
    lastEnded_ = why;
    ++endedCount_[static_cast<int>(why)];
    return why;
  }

  SettleConfig cfg_;
  State state_ = State::Idle;
  SettleIdentity armedId_;
  uint64_t lastChangeUs_ = 0;
  uint64_t contentVersion_ = 0;
  uint64_t startUs_ = 0;
  uint64_t lastServeUs_ = 0;
  uint32_t submitted_ = 0;
  uint32_t attempts_ = 0;
  uint64_t episodes_ = 0;
  uint64_t submittedTotal_ = 0;
  uint64_t emittedTotal_ = 0;
  uint64_t emittedBytesTotal_ = 0;
  uint32_t emittedThisEpisode_ = 0;
  uint64_t emittedBytesThisEpisode_ = 0;
  SettleEnd lastEnded_ = SettleEnd::None;
  uint64_t endedCount_[6] = {0, 0, 0, 0, 0, 0};
  uint64_t byteBudget_ = 0;
};

}  // namespace remote60::native_poc
