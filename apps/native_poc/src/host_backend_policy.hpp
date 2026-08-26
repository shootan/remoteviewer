#pragma once

// Desktop capture backend policy state (DesktopBackendState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <atomic>
#include <cstdint>

#include "host_capture_device.hpp"

namespace remote60::native_poc {

// Desktop capture backend policy (Phase 1-4 state struct): which backend the client asked for,
// which one is actually running after a demotion (DXGI -> WGC on UAC/lock/RDP), the exponential
// retry that climbs back, the secure-desktop stable gate that holds the climb until the DEFAULT
// desktop has been up for a while, and lifetime promotion telemetry. See the comment blocks at
// the assignments in main() for the full rationale.
// thread: main loop owns everything except the three req* atomics, which the control thread sets
// from ControlDesktopBackendRequest and the main loop consumes (cross-thread).
struct DesktopBackendState {
  // cross-thread: request from the control thread, consumed at the top of the main loop.
  std::atomic<bool> reqPending{false};
  std::atomic<uint32_t> reqSeq{0};
  std::atomic<uint16_t> reqValue{0};
  DesktopCaptureBackend requested = DesktopCaptureBackend::Dxgi;
  DesktopCaptureBackend active = DesktopCaptureBackend::Dxgi;
  // Exponential backoff for climbing back to the requested backend.
  uint64_t retryAtUs = 0;
  uint64_t retryDelayUs = 0;
  // Secure-desktop stable gate.
  uint64_t defaultStableSinceUs = 0;  // when the default desktop last became continuously up (0=not)
  uint64_t defaultProbeAtUs = 0;      // next uncached secure-desktop probe
  uint64_t demotionSinceUs = 0;       // when this WGC demotion began (for the promotion-wait metric)
  bool promotionDeferredForCurrentDeadline = false;  // episode latch so the deferred counter can't per-loop spin
  // Lifetime promotion telemetry (read by the stats line; atomics because the control thread
  // reads them for the window-list/status replies).
  std::atomic<uint64_t> promotionAttempts{0};
  std::atomic<uint64_t> promotionSuccess{0};
  std::atomic<uint64_t> promotionFail{0};
  std::atomic<uint64_t> promotionDeferredSecureTotal{0};  // deadlines held off by the secure gate (per episode)
  std::atomic<uint64_t> secureProbeFalseTotal{0};         // uncached probes that saw a secure desktop
  std::atomic<uint64_t> lastPromotionWaitUs{0};           // demotion -> successful promotion
};

}  // namespace remote60::native_poc
