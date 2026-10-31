// Pins the macro window's contract with its interface.
//
// A macro is mostly timing, which is the part that is painful to test through a UI, so the
// message shapes are checked here where nothing has to be waited for. The name validation gets
// the most attention: these become files in the user's profile, and a name that escapes that
// directory is the one bug in this file that would actually matter.

#include <cstdio>
#include <cstring>
#include <string>

#include "json_profile.hpp"
#include "macro_shell_bridge.hpp"

using namespace remote60::native_poc;

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  MacroUiState state{};
  state.state = InputMacro::State::Recording;
  state.paused = true;
  state.stepCount = 2;
  state.playbackPosition = 1;
  state.completedRepeats = 3;
  state.steps.push_back({2, 100, 200, 0, 0, 1, 0});
  state.steps.push_back({3, 110, 205, 0, 0, 1, 250});
  state.savedNames.push_back("daily \"run\"");

  const std::string json = macro_state_json(state);
  check("the state is named in words, not a number", contains(json, "\"state\":\"recording\""));
  check("pause is carried", contains(json, "\"paused\":true"));
  check("each action says what it is", contains(json, "\"kind\":\"down\"") &&
                                       contains(json, "\"kind\":\"up\""));
  check("the gap before a step travels with it", contains(json, "\"delayMs\":250"));
  check("a quoted macro name is escaped", contains(json, "\\\"run\\\""), json);

  MacroPlaybackOptions options{};
  // Parsed on its own line: as an argument it would race the detail string, which C++ may
  // evaluate first -- and a failing check would then print the value from before the parse.
  const bool parsedPlay =
      macro_parse_play(R"({"type":"play","timingJitterMs":40,"positionJitterPx":3,)"
                       R"("repeatCount":0,"repeatGapMinMs":500,"repeatGapMaxMs":1500})",
                       &options);
  check("play settings are read whole",
        parsedPlay && options.timingJitterMs == 40 && options.positionJitterPx == 3 &&
            options.repeatCount == 0 && options.repeatGapMinMs == 500,
        "repeat=" + std::to_string(options.repeatCount) +
            " jitter=" + std::to_string(options.timingJitterMs));

  // Absent must mean once. Defaulting to zero would mean forever, which is not what a page that
  // forgot to send the field intended.
  MacroPlaybackOptions bare{};
  macro_parse_play(R"({"type":"play"})", &bare);
  check("a play with no repeat count plays once", bare.repeatCount == 1,
        "repeat=" + std::to_string(bare.repeatCount));

  // A range the wrong way round would draw the gap from an empty interval.
  MacroPlaybackOptions swapped{};
  macro_parse_play(R"({"type":"play","repeatGapMinMs":900,"repeatGapMaxMs":100})", &swapped);
  check("a reversed gap range is put back in order",
        swapped.repeatGapMinMs == 100 && swapped.repeatGapMaxMs == 900,
        std::to_string(swapped.repeatGapMinMs) + ".." + std::to_string(swapped.repeatGapMaxMs));

  size_t index = 99;
  MacroStep edited{};
  check("a step edit is read",
        macro_parse_edit(R"({"type":"editStep","index":2,"x":50,"y":60,"delayMs":75})", &index,
                         &edited) &&
            index == 2 && edited.x == 50 && edited.y == 60 && edited.delayMs == 75);
  check("another type is not mistaken for an edit",
        !macro_parse_edit(R"({"type":"play","index":2})", &index, &edited));

  size_t deleteIndex = 0;
  check("a delete carries its index",
        macro_parse_index(R"({"type":"deleteStep","index":4})", "deleteStep", &deleteIndex) &&
            deleteIndex == 4);

  std::string name;
  check("a save carries its name",
        macro_parse_name(R"({"type":"save","name":"morning"})", "save", &name) &&
            name == "morning");
  check("a save with no name is refused",
        !macro_parse_name(R"({"type":"save","name":""})", "save", &name));

  // The ones that matter: these become file names.
  check("an ordinary name is allowed", macro_name_is_valid("daily run 2"));
  check("a path separator is refused", !macro_name_is_valid("a/b"));
  check("a backslash is refused", !macro_name_is_valid("a\\b"));
  check("climbing out of the directory is refused", !macro_name_is_valid("..\\..\\evil"));
  check("a leading dot is refused", !macro_name_is_valid(".hidden"));
  check("a wildcard is refused", !macro_name_is_valid("all*"));
  check("a colon is refused", !macro_name_is_valid("C:evil"));
  check("a reserved device name is refused", !macro_name_is_valid("CON"));
  check("and case does not get around it", !macro_name_is_valid("nul"));
  check("blank space alone is refused", !macro_name_is_valid("   "));
  check("an over-long name is refused", !macro_name_is_valid(std::string(200, 'a')));

  const std::string notice = macro_notice_json("saved", false);
  std::string detail;
  json_profile::json_get_string(notice, "detail", &detail);
  check("a notice carries its text", detail == "saved", detail);

  std::printf(gFailures == 0 ? "\nmacro_shell_bridge_test: PASS\n"
                             : "\nmacro_shell_bridge_test: %d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
