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
  if (freq.QuadPart <= 0) return 0;
  return static_cast<uint64_t>((now.QuadPart * 1000000LL) / freq.QuadPart);
}

}  // namespace remote60::native_poc
