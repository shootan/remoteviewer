#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace remote60::common::input {

enum class EventType {
  KeyDown,
  KeyUp,
  MouseMove,
  MouseButtonDown,
  MouseButtonUp,
  MouseWheel,
  Unknown,
};

struct Event {
  EventType type = EventType::Unknown;
  int keyCode = 0;
  int x = 0;
  int y = 0;
  int button = 0;
  int wheelDelta = 0;
};

std::string to_json(const Event& e);
std::optional<Event> from_json(const std::string& json);
std::string to_string(EventType t);

}  // namespace remote60::common::input
