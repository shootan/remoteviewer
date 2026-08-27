#pragma once

// Desktop capture backend policy state (DesktopBackendState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "host_capture_device.hpp"

namespace remote60::native_poc {

// Promotion retry backoff and the secure-desktop settle gate (formerly in host_main_loop.hpp).
constexpr uint64_t kDesktopBackendRetryMinUs = 3'000'000;
constexpr uint64_t kDesktopBackendRetryMaxUs = 30'000'000;
constexpr uint64_t kDesktopDefaultStableUs = 1'000'000;       // continuous default settle before promote
constexpr uint64_t kDesktopDefaultProbeIntervalUs = 200'000;  // OpenInputDesktop probe cadence

// Desktop capture backend policy (Phase 1-4 state struct): which backend the client asked for,
// which one is actually running after a demotion (DXGI -> WGC on UAC/lock/RDP), the exponential
// retry that climbs back, the secure-desktop stable gate that holds the climb until the DEFAULT
// desktop has been up for a while, and lifetime promotion telemetry. See the comment blocks at
// the assignments in main() for the full rationale.
// thread: main loop owns everything except the three req* atomics, which the control thread sets
// from ControlDesktopBackendRequest and the main loop consumes (cross-thread).
struct DesktopBackendState {
  // cross-thread: request from the control thread, consumed at the top of the main loop.
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

  // --- promotion gate / backoff (Phase 2-T4: the former stage_backend lines, pure on this state) ---
  // First sight of a demotion episode: remember when it began (for the promotion-wait metric).
  void NoteDemotionEpisode(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
    if (backend.demotionSinceUs == 0) backend.demotionSinceUs = nowUs;
  }
  // The bounded-cadence probe of the interactive desktop is due.
  bool DefaultProbeDue(uint64_t nowUs) const { return nowUs >= defaultProbeAtUs; }
  // One uncached probe result: schedules the next probe and runs the settle clock.
  void NoteDefaultProbe(uint64_t nowUs, bool isDefault) {
    DesktopBackendState& backend = *this;
    backend.defaultProbeAtUs = nowUs + kDesktopDefaultProbeIntervalUs;
    if (isDefault) {
      if (backend.defaultStableSinceUs == 0) backend.defaultStableSinceUs = nowUs;
    } else {
      backend.defaultStableSinceUs = 0;
      backend.secureProbeFalseTotal.fetch_add(1, std::memory_order_relaxed);
    }
  }
  // The default desktop has been up continuously for the whole settle window.
  bool DefaultStable(uint64_t nowUs) const {
    const DesktopBackendState& backend = *this;
    return backend.defaultStableSinceUs != 0 &&
           (nowUs - backend.defaultStableSinceUs) >= kDesktopDefaultStableUs;
  }
  // Retry deadline: the first call of an episode arms it (not due yet); afterwards, due once passed.
  bool RetryDue(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
    if (backend.retryAtUs == 0) {
      backend.retryAtUs = nowUs + kDesktopBackendRetryMinUs;
      return false;
    }
    return nowUs >= backend.retryAtUs;
  }
  // The final uncached check at the deadline saw a secure desktop: restart the settle clock.
  void NoteSecureAtDeadline() {
    DesktopBackendState& backend = *this;
    backend.defaultStableSinceUs = 0;  // secure again: restart the settle clock
    backend.secureProbeFalseTotal.fetch_add(1, std::memory_order_relaxed);
  }
  // Deferred by the secure gate; true the first time in this deadline episode (the caller logs once).
  bool NoteDeferredForSecure() {
    DesktopBackendState& backend = *this;
    if (backend.promotionDeferredForCurrentDeadline) return false;
    backend.promotionDeferredForCurrentDeadline = true;
    backend.promotionDeferredSecureTotal.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  // A promotion is being attempted now.
  void NotePromotionAttempt() {
    DesktopBackendState& backend = *this;
    backend.promotionDeferredForCurrentDeadline = false;
    backend.promotionAttempts.fetch_add(1, std::memory_order_relaxed);
  }
  // The requested backend is back: telemetry, and the retry / episode clocks reset.
  void NotePromotionSuccess(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
    backend.promotionSuccess.fetch_add(1, std::memory_order_relaxed);
    if (backend.demotionSinceUs != 0 && nowUs >= backend.demotionSinceUs) {
      backend.lastPromotionWaitUs.store(nowUs - backend.demotionSinceUs, std::memory_order_relaxed);
    }
    backend.retryAtUs = 0;
    backend.retryDelayUs = kDesktopBackendRetryMinUs;
    backend.demotionSinceUs = 0;
  }
  // Still unavailable with the default desktop up: exponential backoff up to the ceiling.
  void NotePromotionFailure(uint64_t nowUs) {
    DesktopBackendState& backend = *this;
    backend.promotionFail.fetch_add(1, std::memory_order_relaxed);
    backend.retryDelayUs =
        std::min<uint64_t>(backend.retryDelayUs * 2, kDesktopBackendRetryMaxUs);
    backend.retryAtUs = nowUs + backend.retryDelayUs;
  }
  // Any attempt consumes the stability evidence; the next one must gather fresh proof.
  void ConsumeStabilityEvidence() {
    DesktopBackendState& backend = *this;
    backend.defaultStableSinceUs = 0;
    backend.defaultProbeAtUs = 0;
  }
  // No demotion in progress: the whole gate back to idle.
  void ResetPromotionGate() {
    DesktopBackendState& backend = *this;
    backend.retryAtUs = 0;
    backend.retryDelayUs = kDesktopBackendRetryMinUs;
    backend.defaultStableSinceUs = 0;
    backend.defaultProbeAtUs = 0;
    backend.demotionSinceUs = 0;
    backend.promotionDeferredForCurrentDeadline = false;
  }
};

}  // namespace remote60::native_poc
