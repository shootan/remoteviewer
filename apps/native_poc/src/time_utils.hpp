#pragma once

#include <cstdint>
#include <windows.h>

namespace remote60::native_poc {

inline uint64_t qpc_now_us() {
  static LARGE_INTEGER freq = [] {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (freq.QuadPart <= 0 || now.QuadPart <= 0) return 0;
  const uint64_t freqTicks = static_cast<uint64_t>(freq.QuadPart);
  const uint64_t nowTicks = static_cast<uint64_t>(now.QuadPart);
  const uint64_t seconds = nowTicks / freqTicks;
  const uint64_t remainderTicks = nowTicks % freqTicks;
  return (seconds * 1000000ULL) + ((remainderTicks * 1000000ULL) / freqTicks);
}

}  // namespace remote60::native_poc
