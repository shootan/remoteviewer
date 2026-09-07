#pragma once

// Budget for the SYSTEM agent's "inject landed" diagnostic lines.
//
// Role:    decides whether one more landing line may be written. Pure; the agent feeds it the
//          clock. Unit-tested in secure_input_mapping_test.
// Thread:  the agent's single message thread.
//
// The line records where a click actually landed (mapped point vs GetCursorPos vs the live
// virtual screen) and is the only evidence for "UAC clicks are off". The first budget was 12
// lines per agent PROCESS, one-shot: the agent is recreated only when the desktop name changes,
// so one process served both UAC prompts of 2026-09-07 and the second (15:15) left no record
// (field report 9.4). An episode budget fixes that without opening the door to a line per event:
// a quiet gap refills it, and a per-minute ceiling caps a pathological stream of clicks.

#include <cstdint>

namespace remote60::native_poc {

struct DiagLandingBudget {
  static constexpr int kPerEpisode = 12;
  static constexpr int kPerMinute = 60;
  static constexpr uint64_t kEpisodeGapUs = 2'000'000;  // this much silence starts a new episode
  static constexpr uint64_t kMinuteUs = 60'000'000;

  int remaining = kPerEpisode;
  int minuteCount = 0;
  uint64_t lastEventUs = 0;
  uint64_t minuteStartUs = 0;
};

/** True when a landing line may be written for a button/key/wheel event at `nowUs`. */
inline bool diag_landing_budget_take(DiagLandingBudget& b, uint64_t nowUs) {
  if (b.lastEventUs != 0 && nowUs >= b.lastEventUs + DiagLandingBudget::kEpisodeGapUs) {
    b.remaining = DiagLandingBudget::kPerEpisode;
  }
  b.lastEventUs = nowUs;
  if (b.minuteStartUs == 0 || nowUs >= b.minuteStartUs + DiagLandingBudget::kMinuteUs) {
    b.minuteStartUs = nowUs;
    b.minuteCount = 0;
  }
  if (b.remaining <= 0 || b.minuteCount >= DiagLandingBudget::kPerMinute) return false;
  --b.remaining;
  ++b.minuteCount;
  return true;
}

}  // namespace remote60::native_poc
