#pragma once
#include <cstdint>

namespace remote60::native_poc {
// Main's policy: one DXGI wedge quarantines DXGI for the entire supervisor run.
// Parent-owned, so a fresh child or a long healthy run cannot erase the diagnosis.
struct HostRecoveryPolicy {
  bool quarantineDxgi = false;
  void OnExit(uint32_t code, uint64_t, uint64_t) {
    if (code == 44) quarantineDxgi = true;
  }
  bool UseWgc(uint64_t) const { return quarantineDxgi; }
};
}
