#pragma once

// Record a stretch of pointer input and play it back.
//
// Lives in the shared core because both clients already speak the same input protocol: a macro
// is nothing more than the events they were going to send anyway, with the gaps between them
// remembered. Recording taps what the client is about to transmit, and playback pushes the same
// events back through the same queue, so the host cannot tell the two apart and needs no
// changes at all.
//
// Timing can be varied on playback. Replaying to the exact millisecond is unrealistic: a run
// that only works at one precise cadence tends to pass while hiding a race, and a repeat that
// fires on a metronome is obvious to anything watching. The variation is a small jitter around
// each recorded gap, plus an optional random wait between repeats.

#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

/** One recorded pointer action, with the delay that preceded it. */
struct MacroStep {
  uint16_t kind = 0;      // matches ControlInputEventMessage::kind
  int32_t x = 0;          // remote pixels
  int32_t y = 0;
  int32_t wheelDelta = 0;
  uint32_t keyCode = 0;
  uint16_t buttons = 0;
  /** Milliseconds waited after the previous step; 0 for the first. */
  uint32_t delayMs = 0;
};

/** How much life to breathe into a replay. All zero replays exactly as recorded. */
struct MacroPlaybackOptions {
  /** Random shift applied to each step's delay, plus or minus, in milliseconds. */
  uint32_t timingJitterMs = 0;
  /** Random shift applied to each click position, plus or minus, in remote pixels. */
  uint32_t positionJitterPx = 0;
  /** 0 repeats forever; 1 plays once. */
  uint32_t repeatCount = 1;
  /** Wait between repeats, drawn uniformly from this range. */
  uint32_t repeatGapMinMs = 0;
  uint32_t repeatGapMaxMs = 0;
};

/**
 * Holds one macro and turns it into a schedule.
 *
 * Deliberately has no timer of its own and sends nothing: the caller asks what is due and
 * dispatches it. That keeps the class testable without waiting in real time, and lets each UI
 * drive it from whatever loop it already runs.
 */
class InputMacro {
 public:
  enum class State : uint8_t { Idle, Recording, Playing };

  /** Begins a recording, discarding whatever was held before. */
  void StartRecording(uint64_t nowMs);

  /**
   * Offers an event to the recorder. Ignored unless recording.
   * Only pointer actions are kept; typing is a separate stream and replaying keystrokes
   * blindly is a good way to fill a document with rubbish.
   */
  void RecordEvent(const ControlInputEventMessage& event, uint64_t nowMs);

  void StopRecording();

  /** Starts playback from the beginning. False when there is nothing to play. */
  bool StartPlayback(const MacroPlaybackOptions& options, uint64_t nowMs, uint32_t seed);

  void StopPlayback();

  /**
   * Suspends the current recording or playback without ending it.
   *
   * The paused stretch leaves no trace: a recording resumes as if the pause had not happened
   * (events during it are dropped and its duration is not written into the next step's delay),
   * and a playback's schedule shifts by exactly the paused time.
   */
  void SetPaused(bool paused, uint64_t nowMs);
  bool IsPaused() const;

  /**
   * Returns the next step if it is due, having advanced past it.
   *
   * Called repeatedly by the caller's loop. Steps that are late are returned immediately rather
   * than skipped, so a stalled loop delays a macro instead of dropping half of it.
   */
  bool PollDueStep(uint64_t nowMs, MacroStep* out);

  State state() const;
  bool IsRecording() const { return state() == State::Recording; }
  bool IsPlaying() const { return state() == State::Playing; }

  std::vector<MacroStep> Steps() const;
  size_t StepCount() const;
  void Clear();

  /**
   * Drops one step. The removed step's delay folds into its successor so the surrounding
   * timing is preserved; deleting the first step starts the macro immediately rather than
   * silently waiting out an invisible gap. Refused while recording or playing.
   */
  bool RemoveStep(size_t index);

  /** Rewrites a step's position and preceding delay in place. Refused unless idle. */
  bool UpdateStep(size_t index, int32_t x, int32_t y, uint32_t delayMs);

  /** The whole macro as text, for writing to a file. */
  std::string Serialize() const;

  /**
   * Replaces the held macro with a serialized one. False (and no change) when the text is not
   * a macro or the state is not idle.
   */
  bool LoadSerialized(const std::string& text);

  /** 1-based index of the step played next, for progress display. */
  size_t PlaybackPosition() const;
  uint32_t CompletedRepeats() const;

  /** One line per step, for the list a user reads to check what was captured. */
  static std::string DescribeStep(const MacroStep& step, size_t index);

 private:
  uint64_t ScheduleNext(uint64_t fromMs);

  mutable std::mutex mu_;
  State state_ = State::Idle;
  std::vector<MacroStep> steps_;

  uint64_t lastRecordMs_ = 0;
  bool paused_ = false;
  uint64_t pauseStartMs_ = 0;

  MacroPlaybackOptions options_;
  std::mt19937 rng_;
  size_t nextIndex_ = 0;
  uint64_t nextDueMs_ = 0;
  uint32_t completedRepeats_ = 0;
};

}  // namespace remote60::native_poc
