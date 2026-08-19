#include "macro_shell_bridge.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "json_profile.hpp"

namespace remote60::native_poc {

using json_profile::json_get_string;
using json_profile::json_get_u32;

namespace {

std::string escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
        break;
    }
  }
  return out;
}

const char* state_name(InputMacro::State state) {
  switch (state) {
    case InputMacro::State::Recording: return "recording";
    case InputMacro::State::Playing: return "playing";
    default: return "idle";
  }
}

/** What each recorded action is, in words the page shows rather than a protocol number. */
const char* kind_name(uint16_t kind) {
  switch (kind) {
    case 1: return "move";
    case 2: return "down";
    case 3: return "up";
    case 4: return "wheel";
    case 5: return "keyDown";
    case 6: return "keyUp";
    default: return "other";
  }
}

}  // namespace

std::string macro_state_json(const MacroUiState& state) {
  std::ostringstream oss;
  oss << "{\"type\":\"state\""
      << ",\"state\":\"" << state_name(state.state) << "\""
      << ",\"paused\":" << (state.paused ? "true" : "false")
      << ",\"stepCount\":" << state.stepCount
      << ",\"playbackPosition\":" << state.playbackPosition
      << ",\"completedRepeats\":" << state.completedRepeats
      << ",\"steps\":[";
  for (size_t i = 0; i < state.steps.size(); ++i) {
    const auto& step = state.steps[i];
    if (i > 0) oss << ",";
    oss << "{\"kind\":\"" << kind_name(step.kind) << "\""
        << ",\"x\":" << step.x
        << ",\"y\":" << step.y
        << ",\"wheelDelta\":" << step.wheelDelta
        << ",\"keyCode\":" << step.keyCode
        << ",\"buttons\":" << step.buttons
        << ",\"delayMs\":" << step.delayMs << "}";
  }
  oss << "],\"saved\":[";
  for (size_t i = 0; i < state.savedNames.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "\"" << escape(state.savedNames[i]) << "\"";
  }
  oss << "]}";
  return oss.str();
}

std::string macro_notice_json(const std::string& detail, bool isError) {
  std::ostringstream oss;
  oss << "{\"type\":\"notice\",\"detail\":\"" << escape(detail) << "\",\"error\":"
      << (isError ? "true" : "false") << "}";
  return oss.str();
}

std::string macro_message_type(const std::string& json) {
  std::string type;
  if (!json_get_string(json, "type", &type)) return {};
  return type;
}

bool macro_parse_play(const std::string& json, MacroPlaybackOptions* out) {
  if (!out || macro_message_type(json) != "play") return false;
  *out = MacroPlaybackOptions{};
  json_get_u32(json, "timingJitterMs", &out->timingJitterMs);
  json_get_u32(json, "positionJitterPx", &out->positionJitterPx);
  // Absent means once, not forever. Zero is a deliberate choice the page has to make.
  if (!json_get_u32(json, "repeatCount", &out->repeatCount)) out->repeatCount = 1;
  json_get_u32(json, "repeatGapMinMs", &out->repeatGapMinMs);
  json_get_u32(json, "repeatGapMaxMs", &out->repeatGapMaxMs);
  // A range the wrong way round would make the gap draw from an empty interval.
  if (out->repeatGapMaxMs < out->repeatGapMinMs) {
    std::swap(out->repeatGapMinMs, out->repeatGapMaxMs);
  }
  return true;
}

bool macro_parse_edit(const std::string& json, size_t* outIndex, MacroStep* out) {
  if (!outIndex || !out || macro_message_type(json) != "editStep") return false;
  uint32_t index = 0;
  if (!json_get_u32(json, "index", &index)) return false;
  *outIndex = index;
  uint32_t value = 0;
  if (json_get_u32(json, "x", &value)) out->x = static_cast<int32_t>(value);
  if (json_get_u32(json, "y", &value)) out->y = static_cast<int32_t>(value);
  if (json_get_u32(json, "delayMs", &value)) out->delayMs = value;
  return true;
}

bool macro_parse_index(const std::string& json, const char* type, size_t* outIndex) {
  if (!outIndex || macro_message_type(json) != type) return false;
  uint32_t index = 0;
  if (!json_get_u32(json, "index", &index)) return false;
  *outIndex = index;
  return true;
}

bool macro_parse_name(const std::string& json, const char* type, std::string* outName) {
  if (!outName || macro_message_type(json) != type) return false;
  return json_get_string(json, "name", outName) && !outName->empty();
}

bool macro_name_is_valid(const std::string& name) {
  if (name.empty() || name.size() > 64) return false;
  // A name that is only spaces produces a file nobody can pick out of a list.
  if (name.find_first_not_of(" \t") == std::string::npos) return false;
  for (const char c : name) {
    if (static_cast<unsigned char>(c) < 0x20) return false;
    if (std::strchr("\\/:*?\"<>|", c) != nullptr) return false;
  }
  // Leading dots would let a name climb out of the macro directory.
  if (name.front() == '.') return false;
  // Reserved device names are still reserved with an extension on the end.
  static const char* kReserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3",
                                    "COM4", "LPT1", "LPT2", "LPT3"};
  std::string upper;
  for (const char c : name) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (const char* reserved : kReserved) {
    if (upper == reserved) return false;
  }
  return true;
}

}  // namespace remote60::native_poc
