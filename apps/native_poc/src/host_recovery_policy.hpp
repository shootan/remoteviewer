#pragma once
#include <cstdint>

namespace remote60::native_poc {
// Parent-owned: survives the child that wedged. A healthy runtime restores the requested
// backend; short retries cannot repeatedly erase the diagnosis by resetting child-local state.
struct HostRecoveryPolicy {
  uint32_t dxgiFailures = 0;
  uint64_t fallbackUntilMs = 0;
  void OnExit(uint32_t code, uint64_t ranMs, uint64_t nowMs) {
    if (ranMs >= 300000) dxgiFailures = 0;
    if (code == 44) {
      if (dxgiFailures < 3) ++dxgiFailures;
      if (dxgiFailures >= 2) fallbackUntilMs = nowMs + 300000;
    }
  }
  bool UseWgc(uint64_t nowMs) const { return nowMs < fallbackUntilMs; }
};
}
