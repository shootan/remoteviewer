#pragma once

// The contract between the macro window's interface and the macro engine.
//
// Same arrangement as the connect screen: the interface is a page, the engine is C++, and
// everything between them is JSON with a "type". Kept apart from the window itself so the shape
// of the messages can be tested without creating a window or waiting in real time -- a macro is
// mostly timing, and timing is the part that is painful to test through a UI.

#include <cstdint>
#include <string>
#include <vector>

#include "input_macro.hpp"

namespace remote60::native_poc {

/** Everything the page draws in one message: state, the steps, and where playback has reached. */
struct MacroUiState {
  InputMacro::State state = InputMacro::State::Idle;
  bool paused = false;
  size_t stepCount = 0;
  size_t playbackPosition = 0;
  uint32_t completedRepeats = 0;
  std::vector<MacroStep> steps;
  std::vector<std::string> savedNames;
};

std::string macro_state_json(const MacroUiState& state);

/** A one-line result the page shows after an action that can fail, such as saving to disk. */
std::string macro_notice_json(const std::string& detail, bool isError);

/** The "type" of a message from the page, or empty when it is not one we know. */
std::string macro_message_type(const std::string& json);

/** Playback settings out of a "play" message. Returns false when it is not one. */
bool macro_parse_play(const std::string& json, MacroPlaybackOptions* out);

/**
 * A step edit out of an "editStep" message.
 *
 * The index is what the page listed, so it is validated against the current step count by the
 * caller rather than trusted here.
 */
bool macro_parse_edit(const std::string& json, size_t* outIndex, MacroStep* out);

/** The step index out of a "deleteStep" message. */
bool macro_parse_index(const std::string& json, const char* type, size_t* outIndex);

/** The macro name out of a "save", "load" or "delete" message. */
bool macro_parse_name(const std::string& json, const char* type, std::string* outName);

/**
 * Whether a name is safe to turn into a file.
 *
 * These become files in the user's profile, so anything that could climb out of that directory
 * or collide with a device name is refused rather than sanitised -- silently renaming what
 * someone typed is worse than telling them.
 */
bool macro_name_is_valid(const std::string& name);

}  // namespace remote60::native_poc
