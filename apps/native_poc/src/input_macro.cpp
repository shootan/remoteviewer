#include "input_macro.hpp"

#include <algorithm>
#include <cstdio>

namespace remote60::native_poc {
namespace {

// A recording is a sequence of deliberate actions, not a session log; anything longer than this
// is a runaway rather than something a person meant to capture.
constexpr size_t kMaxSteps = 20000;

// Two actions closer together than a display refresh are one action as far as a person is
// concerned, and a replay that waits 0 ms between them just floods the queue.
constexpr uint32_t kMinDelayMs = 1;

/** Symmetric jitter in [-range, +range]; 0 range yields 0. */
int32_t jitter(std::mt19937& rng, uint32_t range) {
  if (range == 0) return 0;
  std::uniform_int_distribution<int32_t> dist(-static_cast<int32_t>(range),
                                              static_cast<int32_t>(range));
  return dist(rng);
}

const char* kind_name(uint16_t kind) {
  switch (kind) {
    case 1: return "move";
    case 2: return "press";
    case 3: return "release";
    case 4: return "wheel";
    default: return "other";
  }
}

const char* button_name(uint32_t keyCode) {
  switch (keyCode) {
    case 0x01: return "left";
    case 0x02: return "right";
    case 0x04: return "middle";
    default: return "";
  }
}

}  // namespace

void InputMacro::StartRecording(uint64_t nowMs) {
  std::lock_guard<std::mutex> lock(mu_);
  steps_.clear();
  state_ = State::Recording;
  lastRecordMs_ = nowMs;
  nextIndex_ = 0;
  completedRepeats_ = 0;
}

void InputMacro::RecordEvent(const ControlInputEventMessage& event, uint64_t nowMs) {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ != State::Recording) return;
  // Pointer actions only. Keys and typed text belong to the keyboard path, and replaying them
  // into whatever happens to be focused later does more harm than good.
  if (event.kind < 1 || event.kind > 4) return;
  if (steps_.size() >= kMaxSteps) return;

  MacroStep step;
  step.kind = event.kind;
  step.x = event.x;
  step.y = event.y;
  step.wheelDelta = event.wheelDelta;
  step.keyCode = event.keyCode;
  step.buttons = event.buttons;
  step.delayMs = steps_.empty()
                     ? 0u
                     : static_cast<uint32_t>(std::min<uint64_t>(nowMs - lastRecordMs_, 600000ULL));
  lastRecordMs_ = nowMs;
  steps_.push_back(step);
}

void InputMacro::StopRecording() {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ == State::Recording) state_ = State::Idle;
}

bool InputMacro::StartPlayback(const MacroPlaybackOptions& options, uint64_t nowMs, uint32_t seed) {
  std::lock_guard<std::mutex> lock(mu_);
  if (steps_.empty()) return false;
  options_ = options;
  if (options_.repeatGapMaxMs < options_.repeatGapMinMs) {
    std::swap(options_.repeatGapMinMs, options_.repeatGapMaxMs);
  }
  rng_.seed(seed);
  state_ = State::Playing;
  nextIndex_ = 0;
  completedRepeats_ = 0;
  // The first step's recorded delay is 0, so playback starts immediately.
  nextDueMs_ = ScheduleNext(nowMs);
  return true;
}

void InputMacro::StopPlayback() {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ == State::Playing) state_ = State::Idle;
}

uint64_t InputMacro::ScheduleNext(uint64_t fromMs) {
  const uint32_t recorded = steps_[nextIndex_].delayMs;
  const int64_t shifted = static_cast<int64_t>(recorded) + jitter(rng_, options_.timingJitterMs);
  const uint64_t delay = static_cast<uint64_t>(std::max<int64_t>(shifted, kMinDelayMs));
  return fromMs + delay;
}

bool InputMacro::PollDueStep(uint64_t nowMs, MacroStep* out) {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ != State::Playing || !out) return false;
  if (nextIndex_ >= steps_.size()) return false;
  if (nowMs < nextDueMs_) return false;

  MacroStep step = steps_[nextIndex_];
  if (step.kind != 4) {
    // Wheel events carry no meaningful position, so only pointer actions get nudged.
    step.x += jitter(rng_, options_.positionJitterPx);
    step.y += jitter(rng_, options_.positionJitterPx);
    step.x = std::max(0, step.x);
    step.y = std::max(0, step.y);
  }
  *out = step;

  ++nextIndex_;
  if (nextIndex_ < steps_.size()) {
    nextDueMs_ = ScheduleNext(nowMs);
    return true;
  }

  // Reached the end of a pass.
  ++completedRepeats_;
  if (options_.repeatCount != 0 && completedRepeats_ >= options_.repeatCount) {
    state_ = State::Idle;
    return true;
  }
  nextIndex_ = 0;
  uint64_t gap = options_.repeatGapMinMs;
  if (options_.repeatGapMaxMs > options_.repeatGapMinMs) {
    std::uniform_int_distribution<uint32_t> dist(options_.repeatGapMinMs, options_.repeatGapMaxMs);
    gap = dist(rng_);
  }
  nextDueMs_ = nowMs + std::max<uint64_t>(gap, kMinDelayMs);
  return true;
}

InputMacro::State InputMacro::state() const {
  std::lock_guard<std::mutex> lock(mu_);
  return state_;
}

std::vector<MacroStep> InputMacro::Steps() const {
  std::lock_guard<std::mutex> lock(mu_);
  return steps_;
}

size_t InputMacro::StepCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return steps_.size();
}

void InputMacro::Clear() {
  std::lock_guard<std::mutex> lock(mu_);
  steps_.clear();
  state_ = State::Idle;
  nextIndex_ = 0;
  completedRepeats_ = 0;
}

size_t InputMacro::PlaybackPosition() const {
  std::lock_guard<std::mutex> lock(mu_);
  return nextIndex_;
}

uint32_t InputMacro::CompletedRepeats() const {
  std::lock_guard<std::mutex> lock(mu_);
  return completedRepeats_;
}

std::string InputMacro::DescribeStep(const MacroStep& step, size_t index) {
  char buffer[160];
  if (step.kind == 4) {
    std::snprintf(buffer, sizeof(buffer), "%zu. wheel %+d  (+%u ms)", index + 1,
                  step.wheelDelta, step.delayMs);
  } else {
    const char* button = button_name(step.keyCode);
    std::snprintf(buffer, sizeof(buffer), "%zu. %s%s%s  %d, %d  (+%u ms)", index + 1,
                  kind_name(step.kind), *button ? " " : "", button, step.x, step.y,
                  step.delayMs);
  }
  return std::string(buffer);
}

}  // namespace remote60::native_poc
