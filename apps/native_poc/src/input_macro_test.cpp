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

  // --- pause ----------------------------------------------------------------------
  {
    // Pausing a recording must leave no trace: events during it are dropped, and the pause
    // duration does not stretch the next step's delay.
    InputMacro macro;
    macro.StartRecording(1000);
    macro.RecordEvent(event(2, 10, 10, 0x01, 1), 1000);
    macro.SetPaused(true, 1050);
    macro.RecordEvent(event(1, 999, 999, 0, 1), 3000);   // during the pause; must vanish
    check("paused recording reports paused", macro.IsPaused());
    macro.SetPaused(false, 5000);
    macro.RecordEvent(event(3, 10, 10, 0x01, 0), 5030);
    macro.StopRecording();
    const auto steps = macro.Steps();
    check("events during a recording pause are dropped", steps.size() == 2,
          "count=" + std::to_string(steps.size()));
    check("pause time is not written into the delay",
          steps.size() == 2 && steps[1].delayMs == 30,
          steps.size() == 2 ? "delay=" + std::to_string(steps[1].delayMs) : "");
  }

  {
    // Pausing playback holds the schedule; resuming shifts it by exactly the paused time.
    InputMacro macro;
    record_a_click(macro);   // gaps: 0, 50, 40
    MacroPlaybackOptions options;
    options.repeatCount = 1;
    macro.StartPlayback(options, 0, 3);
    MacroStep step;
    check("first step plays before the pause", macro.PollDueStep(1, &step));
    macro.SetPaused(true, 10);
    check("nothing plays while paused", !macro.PollDueStep(500, &step));
    check("still counted as playing while paused", macro.IsPlaying() && macro.IsPaused());
    macro.SetPaused(false, 1000);   // paused for 990; second step was due at 51 -> now 1041
    check("resume does not fire early", !macro.PollDueStep(1040, &step));
    const auto rest = drain(macro, 1041, 3000);
    check("resumed playback finishes the pass", rest.size() == 2 && !macro.IsPlaying(),
          "count=" + std::to_string(rest.size()));
  }

  // --- editing --------------------------------------------------------------------
  {
    InputMacro macro;
    record_a_click(macro);   // press(0) drag(+50) release(+40)
    check("removing mid step merges its delay into the next", macro.RemoveStep(1));
    auto steps = macro.Steps();
    check("removed step is gone", steps.size() == 2 && steps[1].kind == 3);
    check("surrounding timing is preserved", steps.size() == 2 && steps[1].delayMs == 90,
          steps.size() == 2 ? "delay=" + std::to_string(steps[1].delayMs) : "");

    check("removing the first step clears the leading wait", macro.RemoveStep(0));
    steps = macro.Steps();
    check("new first step starts immediately", steps.size() == 1 && steps[0].delayMs == 0);

    check("out-of-range remove is refused", !macro.RemoveStep(5));
    check("update rewrites position and delay", macro.UpdateStep(0, 300, 400, 77));
    steps = macro.Steps();
    check("updated values are held",
          steps[0].x == 300 && steps[0].y == 400 && steps[0].delayMs == 77);
  }

  {
    InputMacro macro;
    record_a_click(macro);
    macro.StartPlayback(MacroPlaybackOptions{}, 0, 1);
    check("editing while playing is refused",
          !macro.RemoveStep(0) && !macro.UpdateStep(0, 1, 1, 1));
    macro.StopPlayback();
  }

  // --- save and load --------------------------------------------------------------
  {
    InputMacro macro;
    record_a_click(macro);
    const std::string text = macro.Serialize();

    InputMacro other;
    check("serialized macro loads elsewhere", other.LoadSerialized(text));
    const auto original = macro.Steps();
    const auto loaded = other.Steps();
    bool same = original.size() == loaded.size();
    for (size_t i = 0; same && i < original.size(); ++i) {
      same = original[i].kind == loaded[i].kind && original[i].x == loaded[i].x &&
             original[i].y == loaded[i].y && original[i].keyCode == loaded[i].keyCode &&
             original[i].buttons == loaded[i].buttons && original[i].delayMs == loaded[i].delayMs;
    }
    check("load reproduces every step exactly", same,
          "count=" + std::to_string(loaded.size()));

    check("garbage is refused", !other.LoadSerialized("not a macro"));
    check("an empty body is refused", !other.LoadSerialized("gnlink-macro-v1\n"));
    check("a bad line is refused", !other.LoadSerialized("gnlink-macro-v1\n2 xx yy\n"));
    check("refusal leaves the held macro intact", other.StepCount() == original.size());
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
