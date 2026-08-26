#pragma once

// Small ASCII / UTF-8 / HRESULT string helpers shared by the host.
//
// Role:    pure string utilities (trim, lower, utf8<->wide, hr hex, csv split, base name).
// Thread:  none -- pure functions, no shared state.
// Input:   std::string / std::wstring / HRESULT arguments.
// Output:  transformed strings.
// Callers: native_video_host_main.cpp (and future host_* modules).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0). Header-only
// inline so no new translation unit is added and behavior is byte-identical.

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

namespace remote60::native_poc {

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

inline std::wstring utf8_to_wide(const std::string& utf8) {
  if (utf8.empty()) return std::wstring{};
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (n <= 1) return std::wstring{};
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
  return out;
}

inline std::string wide_to_utf8(const std::wstring& wide) {
  if (wide.empty()) return std::string{};
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return std::string{};
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n, nullptr, nullptr);
  return out;
}

inline std::wstring wide_lower(std::wstring v) {
  std::transform(v.begin(), v.end(), v.begin(), [](wchar_t c) {
    return static_cast<wchar_t>(std::towlower(c));
  });
  return v;
}

inline std::string hr_hex(HRESULT hr) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
  return std::string(buf);
}

inline std::vector<std::string> parse_csv_lower(const std::string& raw) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= raw.size()) {
    const size_t comma = raw.find(',', start);
    const size_t end = (comma == std::string::npos) ? raw.size() : comma;
    const std::string token = trim_ascii(raw.substr(start, end - start));
    if (!token.empty()) {
      out.push_back(ascii_lower(token));
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

inline std::string base_name_lower(std::string path) {
  if (path.empty()) return path;
  const size_t slashPos = path.find_last_of("\\/");
  if (slashPos != std::string::npos && slashPos + 1 < path.size()) {
    path = path.substr(slashPos + 1);
  }
  return ascii_lower(path);
}

}  // namespace remote60::native_poc
