#include "input_macro.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <sstream>

namespace remote60::native_poc {
namespace {

// A recording is a sequence of deliberate actions, not a session log; anything longer than this
// is a runaway rather than something a person meant to capture.
constexpr size_t kMaxSteps = 20000;

// Two actions closer together than a display refresh are one action as far as a person is
// concerned, and a replay that waits 0 ms between them just floods the queue.
constexpr uint32_t kMinDelayMs = 1;

constexpr uint32_t kMaxDelayMs = 600000;

// First line of a serialized macro. Versioned so a future format change can refuse cleanly
// instead of half-loading.
constexpr const char* kSerializeHeader = "gnlink-macro-v1";

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
  paused_ = false;
  lastRecordMs_ = nowMs;
  nextIndex_ = 0;
  completedRepeats_ = 0;
}

void InputMacro::RecordEvent(const ControlInputEventMessage& event, uint64_t nowMs) {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ != State::Recording || paused_) return;
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
  if (state_ == State::Recording) {
    state_ = State::Idle;
    paused_ = false;
  }
}

void InputMacro::SetPaused(bool paused, uint64_t nowMs) {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ == State::Idle || paused == paused_) return;
  paused_ = paused;
  if (paused) {
    pauseStartMs_ = nowMs;
    return;
  }
  const uint64_t pausedFor = nowMs >= pauseStartMs_ ? nowMs - pauseStartMs_ : 0;
  if (state_ == State::Recording) {
    // The next event's delay is measured from here, not from before the pause.
    lastRecordMs_ = nowMs;
  } else if (state_ == State::Playing) {
    nextDueMs_ += pausedFor;
  }
}

bool InputMacro::IsPaused() const {
  std::lock_guard<std::mutex> lock(mu_);
  return paused_ && state_ != State::Idle;
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
  paused_ = false;
  nextIndex_ = 0;
  completedRepeats_ = 0;
  // The first step's recorded delay is 0, so playback starts immediately.
  nextDueMs_ = ScheduleNext(nowMs);
  return true;
}

void InputMacro::StopPlayback() {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ == State::Playing) {
    state_ = State::Idle;
    paused_ = false;
  }
}

uint64_t InputMacro::ScheduleNext(uint64_t fromMs) {
  const uint32_t recorded = steps_[nextIndex_].delayMs;
  const int64_t shifted = static_cast<int64_t>(recorded) + jitter(rng_, options_.timingJitterMs);
  const uint64_t delay = static_cast<uint64_t>(std::max<int64_t>(shifted, kMinDelayMs));
  return fromMs + delay;
}

bool InputMacro::PollDueStep(uint64_t nowMs, MacroStep* out) {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ != State::Playing || paused_ || !out) return false;
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
  paused_ = false;
  nextIndex_ = 0;
  completedRepeats_ = 0;
}

bool InputMacro::RemoveStep(size_t index) {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ != State::Idle || index >= steps_.size()) return false;
  if (index + 1 < steps_.size()) {
    const uint64_t merged =
        static_cast<uint64_t>(steps_[index].delayMs) + steps_[index + 1].delayMs;
    steps_[index + 1].delayMs = static_cast<uint32_t>(std::min<uint64_t>(merged, kMaxDelayMs));
  }
  steps_.erase(steps_.begin() + static_cast<ptrdiff_t>(index));
  // A leading delay would replay as a silent wait before anything visible happens, which
  // reads as a hang rather than as timing.
  if (index == 0 && !steps_.empty()) steps_.front().delayMs = 0;
  return true;
}

bool InputMacro::UpdateStep(size_t index, int32_t x, int32_t y, uint32_t delayMs) {
  std::lock_guard<std::mutex> lock(mu_);
  if (state_ != State::Idle || index >= steps_.size()) return false;
  MacroStep& step = steps_[index];
  step.x = std::max(0, x);
  step.y = std::max(0, y);
  step.delayMs = std::min(delayMs, kMaxDelayMs);
  return true;
}

std::string InputMacro::Serialize() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::ostringstream out;
  out << kSerializeHeader << '\n';
  for (const MacroStep& s : steps_) {
    out << s.kind << ' ' << s.x << ' ' << s.y << ' ' << s.wheelDelta << ' ' << s.keyCode << ' '
        << s.buttons << ' ' << s.delayMs << '\n';
  }
  return out.str();
}

bool InputMacro::LoadSerialized(const std::string& text) {
  std::istringstream in(text);
  std::string header;
  if (!std::getline(in, header)) return false;
  // Tolerate a trailing \r from files that passed through Windows line endings.
  if (!header.empty() && header.back() == '\r') header.pop_back();
  if (header != kSerializeHeader) return false;

  std::vector<MacroStep> loaded;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line == "\r") continue;
    MacroStep s;
    unsigned kind = 0;
    unsigned key = 0;
    unsigned buttons = 0;
    unsigned delay = 0;
    if (std::sscanf(line.c_str(), "%u %d %d %d %u %u %u", &kind, &s.x, &s.y, &s.wheelDelta,
                    &key, &buttons, &delay) != 7) {
      return false;
    }
    if (kind < 1 || kind > 4) return false;
    s.kind = static_cast<uint16_t>(kind);
    s.keyCode = key;
    s.buttons = static_cast<uint16_t>(buttons & 0x7u);
    s.delayMs = std::min<unsigned>(delay, kMaxDelayMs);
    s.x = std::max(0, s.x);
    s.y = std::max(0, s.y);
    if (loaded.size() >= kMaxSteps) return false;
    loaded.push_back(s);
  }
  if (loaded.empty()) return false;

  std::lock_guard<std::mutex> lock(mu_);
  if (state_ != State::Idle) return false;
  steps_ = std::move(loaded);
  nextIndex_ = 0;
  completedRepeats_ = 0;
  return true;
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
