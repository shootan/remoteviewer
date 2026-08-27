#pragma once

// Environment and string helpers of the viewer.
//
// Role:    parse_u32, env_truthy, env_u32_clamped, env_string_or_empty, trim_ascii, ascii_lower,
//          fixed_cstr_to_string, utf8_to_wide -- pure functions, no shared state.
// Thread:  none (pure).
// Input:   C strings / std::string / env var names.
// Output:  parsed numbers, trimmed or lowered strings, wide strings.
// Callers: viewer_args (parse_args), viewer_decoder_backend, startup env switches, control pong logs.
//
// Extracted verbatim from native_video_client_main.cpp (viewer split refactor Phase 0-2); header-only
// inline so no translation unit is added. Byte-identical copies live in host_args.hpp and
// host_string_util.hpp -- Phase 0-15 unifies them.

#include "viewer_common.hpp"

namespace remote60::native_poc::viewer {

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

inline std::string trim_ascii(std::string v) {
  size_t start = 0;
  while (start < v.size() && std::isspace(static_cast<unsigned char>(v[start])) != 0) {
    ++start;
  }
  size_t end = v.size();
  while (end > start && std::isspace(static_cast<unsigned char>(v[end - 1])) != 0) {
    --end;
  }
  return v.substr(start, end - start);
}

inline std::string ascii_lower(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return v;
}

inline std::string env_string_or_empty(const char* key) {
  if (!key) return std::string{};
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string{};
}

inline std::string fixed_cstr_to_string(const char* buf, size_t cap) {
  if (!buf || cap == 0) return std::string{};
  size_t n = 0;
  while (n < cap && buf[n] != '\0') ++n;
  return std::string(buf, buf + n);
}

inline std::wstring utf8_to_wide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring{};
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (n <= 1) return std::wstring{};
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
  return out;
}

}  // namespace remote60::native_poc::viewer
