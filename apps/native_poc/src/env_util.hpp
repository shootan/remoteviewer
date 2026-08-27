#pragma once

// Environment variable and number parsing helpers shared by the host and the viewer.
//
// Role:    parse_u32, env_truthy, env_u32_clamped, env_string_or_empty -- pure functions.
// Thread:  none (pure; std::getenv reads only).
// Input:   C strings / environment variable names / fallbacks and clamps.
// Output:  parsed values.
// Callers: host_args.hpp (host parse_args + REMOTE60_NATIVE_* switches), viewer_env_util.hpp
//          (viewer parse_args + startup env switches).
//
// The host (host_args.hpp, host split Phase 0-7a) and the viewer (viewer_env_util.hpp, viewer split
// Phase 0-2) carried byte-identical copies; viewer split Phase 0-15 keeps one here. Bodies unchanged.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace remote60::native_poc {

inline std::string env_string_or_empty(const char* key) {
  if (!key) return std::string{};
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string{};
}

inline bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !out) return false;
  char* end = nullptr;
  const unsigned long v = std::strtoul(s, &end, 10);
  if (!end || *end != '\0') return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

inline bool env_truthy(const char* key) {
  if (!key) return false;
  const char* v = std::getenv(key);
  if (!v) return false;
  const std::string s = v;
  return s == "1" || s == "true" || s == "TRUE" || s == "on" || s == "ON";
}

inline uint32_t env_u32_clamped(const char* key, uint32_t fallback, uint32_t minValue, uint32_t maxValue) {
  if (!key) return fallback;
  const char* raw = std::getenv(key);
  if (!raw) return fallback;
  uint32_t parsed = 0;
  if (!parse_u32(raw, &parsed)) return fallback;
  return std::clamp<uint32_t>(parsed, minValue, maxValue);
}

}  // namespace remote60::native_poc
