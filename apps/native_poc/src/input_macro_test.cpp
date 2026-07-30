// Exercises record and replay against a fake clock, so timing behaviour is checked without
// waiting in real time.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "input_macro.hpp"

using remote60::native_poc::ControlInputEventMessage;
using remote60::native_poc::InputMacro;
using remote60::native_poc::MacroPlaybackOptions;
using remote60::native_poc::MacroStep;

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

ControlInputEventMessage event(uint16_t kind, int32_t x, int32_t y, uint32_t keyCode = 0,
                               uint16_t buttons = 0, int32_t wheel = 0) {
  ControlInputEventMessage e{};
  e.kind = kind;
  e.x = x;
  e.y = y;
  e.keyCode = keyCode;
  e.buttons = buttons;
  e.wheelDelta = wheel;
  return e;
}

/** Drains every step a playback produces, advancing a fake clock one millisecond at a time. */
std::vector<MacroStep> drain(InputMacro& macro, uint64_t startMs, uint64_t limitMs) {
  std::vector<MacroStep> out;
  for (uint64_t now = startMs; now <= limitMs && macro.IsPlaying(); ++now) {
    MacroStep step;
    while (macro.PollDueStep(now, &step)) {
      out.push_back(step);
      if (!macro.IsPlaying()) break;
    }
  }
  return out;
}

void record_a_click(InputMacro& macro) {
  macro.StartRecording(1000);
  macro.RecordEvent(event(2, 100, 200, 0x01, 1), 1000);   // press
  macro.RecordEvent(event(1, 140, 240, 0, 1), 1050);      // drag
  macro.RecordEvent(event(3, 140, 240, 0x01, 0), 1090);   // release
  macro.StopRecording();
}

}  // namespace

int main() {
  // --- recording ------------------------------------------------------------------
  {
    InputMacro macro;
    record_a_click(macro);
    const auto steps = macro.Steps();
    check("records every pointer action", steps.size() == 3,
          "count=" + std::to_string(steps.size()));
    check("first step has no leading delay", !steps.empty() && steps[0].delayMs == 0);
    check("gaps between actions are kept",
          steps.size() == 3 && steps[1].delayMs == 50 && steps[2].delayMs == 40,
          steps.size() == 3 ? std::to_string(steps[1].delayMs) + "," + std::to_string(steps[2].delayMs)
                            : "");
    check("not recording after stop", !macro.IsRecording());
  }

  // Typing is a separate stream; replaying keys into whatever is focused later is destructive.
  {
    InputMacro macro;
    macro.StartRecording(0);
    macro.RecordEvent(event(5, 0, 0, 'A'), 10);   // key down
    macro.RecordEvent(event(6, 0, 0, 'A'), 20);   // key up
    macro.RecordEvent(event(2, 5, 5, 0x01, 1), 30);
    macro.StopRecording();
    check("keyboard events are not recorded", macro.StepCount() == 1,
          "count=" + std::to_string(macro.StepCount()));
  }

  {
    InputMacro macro;
    macro.StartRecording(0);
    macro.RecordEvent(event(2, 1, 1, 0x01, 1), 5);
    macro.StartRecording(100);
    check("starting a new recording discards the old one", macro.StepCount() == 0);
  }

  // --- exact replay ---------------------------------------------------------------
  {
    InputMacro macro;
    record_a_click(macro);
    MacroPlaybackOptions options;
    options.repeatCount = 1;
    check("playback starts", macro.StartPlayback(options, 5000, 1));
    const auto played = drain(macro, 5000, 6000);
    check("plays every step once", played.size() == 3, "count=" + std::to_string(played.size()));
    check("positions are untouched without jitter",
          played.size() == 3 && played[0].x == 100 && played[0].y == 200 &&
              played[2].x == 140 && played[2].y == 240);
    check("stops after a single pass", !macro.IsPlaying());
    check("counts the pass", macro.CompletedRepeats() == 1);
  }

  {
    InputMacro macro;
    check("empty macro refuses to play", !macro.StartPlayback(MacroPlaybackOptions{}, 0, 1));
  }

  // --- repeats --------------------------------------------------------------------
  {
    InputMacro macro;
    record_a_click(macro);
    MacroPlaybackOptions options;
    options.repeatCount = 3;
    options.repeatGapMinMs = 20;
    options.repeatGapMaxMs = 20;
    macro.StartPlayback(options, 0, 7);
    const auto played = drain(macro, 0, 10000);
    check("repeats the whole macro", played.size() == 9, "count=" + std::to_string(played.size()));
    check("counts every repeat", macro.CompletedRepeats() == 3,
          "repeats=" + std::to_string(macro.CompletedRepeats()));
  }

  // --- humanised replay -----------------------------------------------------------
  {
    InputMacro macro;
    record_a_click(macro);
    MacroPlaybackOptions options;
    options.repeatCount = 40;
    options.positionJitterPx = 3;
    options.repeatGapMinMs = 1;
    options.repeatGapMaxMs = 2;
    macro.StartPlayback(options, 0, 99);
    const auto played = drain(macro, 0, 100000);

    std::set<int32_t> pressXs;
    bool withinBounds = true;
    for (size_t i = 0; i < played.size(); i += 3) {
      pressXs.insert(played[i].x);
      if (played[i].x < 97 || played[i].x > 103) withinBounds = false;
    }
    check("position jitter varies the click point", pressXs.size() > 1,
          "distinct=" + std::to_string(pressXs.size()));
    check("position jitter stays within the requested range", withinBounds);
  }

  {
    // Timing jitter must not be able to drive a delay to zero and spin the caller's loop.
    InputMacro macro;
    macro.StartRecording(0);
    macro.RecordEvent(event(2, 1, 1, 0x01, 1), 0);
    macro.RecordEvent(event(3, 1, 1, 0x01, 0), 2);
    macro.StopRecording();
    MacroPlaybackOptions options;
    options.repeatCount = 1;
    options.timingJitterMs = 50;   // far larger than the 2 ms gap
    macro.StartPlayback(options, 0, 5);
    const auto played = drain(macro, 0, 5000);
    check("large timing jitter still completes", played.size() == 2,
          "count=" + std::to_string(played.size()));
  }

  {
    // A stalled caller must not cause a burst. Firing every overdue step at once would turn a
    // press-move-release into three simultaneous events, which is a double click as far as the
    // remote side is concerned. The macro slides later instead, keeping the gaps intact.
    InputMacro macro;
    record_a_click(macro);
    MacroPlaybackOptions options;
    options.repeatCount = 1;
    macro.StartPlayback(options, 0, 3);
    std::vector<MacroStep> played;
    MacroStep step;
    while (macro.PollDueStep(10000, &step)) played.push_back(step);
    check("a late poll does not burst the queue", played.size() == 1,
          "count=" + std::to_string(played.size()));

    // ...and the rest still arrives, in order, once time moves on.
    const auto rest = drain(macro, 10000, 11000);
    check("the remaining steps follow in order",
          rest.size() == 2 && rest[0].kind == 1 && rest[1].kind == 3,
          "count=" + std::to_string(rest.size()));
  }

  {
    InputMacro macro;
    record_a_click(macro);
    MacroPlaybackOptions options;
    options.repeatCount = 0;   // forever
    macro.StartPlayback(options, 0, 11);
    const auto played = drain(macro, 0, 2000);
    check("repeat count 0 keeps going", macro.IsPlaying() && played.size() > 3,
          "count=" + std::to_string(played.size()));
    macro.StopPlayback();
    check("stop ends playback", !macro.IsPlaying());
  }

  // --- the list a user reads ------------------------------------------------------
  {
    InputMacro macro;
    record_a_click(macro);
    const auto steps = macro.Steps();
    const std::string first = InputMacro::DescribeStep(steps[0], 0);
    const std::string third = InputMacro::DescribeStep(steps[2], 2);
    check("step description names the action and place",
          first.find("press") != std::string::npos && first.find("100, 200") != std::string::npos,
          first);
    check("step description shows the delay",
          third.find("+40 ms") != std::string::npos, third);
  }

  std::printf(gFailures == 0 ? "\nRESULT: ALL PASS\n" : "\nRESULT: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
