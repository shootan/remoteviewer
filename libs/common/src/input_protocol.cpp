#include "common/input_protocol.hpp"

#include <sstream>

namespace remote60::common::input {
namespace {

bool has_token(const std::string& s, const std::string& k) { return s.find(k) != std::string::npos; }

int read_int_field(const std::string& s, const std::string& key, int fallback = 0) {
  const std::string token = "\"" + key + "\":";
  const auto p = s.find(token);
  if (p == std::string::npos) return fallback;
  size_t i = p + token.size();
  bool neg = false;
  if (i < s.size() && s[i] == '-') {
    neg = true;
    ++i;
  }
  int v = 0;
  bool any = false;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    any = true;
    v = (v * 10) + (s[i] - '0');
    ++i;
  }
  if (!any) return fallback;
  return neg ? -v : v;
}

}  // namespace

std::string to_string(EventType t) {
  switch (t) {
    case EventType::KeyDown:
      return "key_down";
    case EventType::KeyUp:
      return "key_up";
    case EventType::MouseMove:
      return "mouse_move";
    case EventType::MouseButtonDown:
      return "mouse_button_down";
    case EventType::MouseButtonUp:
      return "mouse_button_up";
    case EventType::MouseWheel:
      return "mouse_wheel";
    default:
      return "unknown";
  }
}

std::string to_json(const Event& e) {
  std::ostringstream oss;
  oss << "{\"type\":\"" << to_string(e.type) << "\"";
  oss << ",\"keyCode\":" << e.keyCode;
  oss << ",\"x\":" << e.x;
  oss << ",\"y\":" << e.y;
  oss << ",\"button\":" << e.button;
  oss << ",\"wheelDelta\":" << e.wheelDelta;
  oss << "}";
  return oss.str();
}

std::optional<Event> from_json(const std::string& json) {
  Event e;
  if (has_token(json, "\"type\":\"key_down\""))
    e.type = EventType::KeyDown;
  else if (has_token(json, "\"type\":\"key_up\""))
    e.type = EventType::KeyUp;
  else if (has_token(json, "\"type\":\"mouse_move\""))
    e.type = EventType::MouseMove;
  else if (has_token(json, "\"type\":\"mouse_button_down\""))
    e.type = EventType::MouseButtonDown;
  else if (has_token(json, "\"type\":\"mouse_button_up\""))
    e.type = EventType::MouseButtonUp;
  else if (has_token(json, "\"type\":\"mouse_wheel\""))
    e.type = EventType::MouseWheel;
  else
    return std::nullopt;

  e.keyCode = read_int_field(json, "keyCode", 0);
  e.x = read_int_field(json, "x", 0);
  e.y = read_int_field(json, "y", 0);
  e.button = read_int_field(json, "button", 0);
  e.wheelDelta = read_int_field(json, "wheelDelta", 0);
  return e;
}

}  // namespace remote60::common::input
