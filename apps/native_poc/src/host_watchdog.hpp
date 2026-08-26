#pragma once

// Main-loop phase, watchdog exit codes and watchdog state (WatchdogState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <atomic>
#include <cstdint>

#include "time_utils.hpp"

namespace remote60::native_poc {

// Phase of the host's main capture/encode loop, published for the liveness watchdog. A permanent
// hang inside a GPU/MFT/driver call stops the loop WITHOUT crashing, so neither the in-loop
// self-heal (which never runs) nor the supervisor (which only relaunches on a crash) recovers it.
// The watchdog reads the last phase + how long the loop has been stuck to decide when to terminate.
enum class MainLoopPhase : uint32_t {
  Startup = 0,
  Loop,            // between iterations / ordinary work -- 10s hang threshold
  CaptureRestart,  // restart_capture_session: legitimately slow (device/pool rebuild) -- 20s
  EncodeCall,      // MFT encode of a frame -- the prime suspect for a driver/MFT wedge -- 10s
};
// Terminate exit code the watchdog uses; the supervisor treats it as "wedged, relaunch fast" and
// keeps it out of the crash streak / nv12 auto-disable (it is a recovery, not a crash).
constexpr unsigned int kExitMainLoopWatchdog = 43;
// The DXGI capture worker wedge watchdog terminates with this distinct code so the supervisor can
// tell a capture-thread hang apart from a main-loop hang, while giving both the same fast,
// no-crash-streak relaunch. (Codex-reviewed 2026-08-25.)
constexpr unsigned int kExitDxgiWorkerWedge = 44;

// Capture/encode liveness watchdogs (Phase 1-11 state struct): the GDI callback-stall watchdog
// (input push rate), the DXGI/WGC frozen-ring self-heal, the readback-throughput soft watchdog
// (per-1s-window deltas), the readback-slow log rate-limit, and the main-loop liveness stamps the
// independent watchdog thread polls. See the comment blocks in main() for each one's rationale.
// thread: main loop owns everything; the three mainLoop* atomics are read by the watchdog thread.
struct WatchdogState {
  // GDI callback-stall watchdog (REMOTE60_NATIVE_CAPTURE_INPUT_*).
  uint32_t inputMinPushPerSec = 0;
  uint32_t inputStallConsecutiveSec = 0;
  uint32_t inputStallWarmupSec = 0;
  uint32_t inputLowPushStreakSec = 0;
  uint64_t deadRestartCount = 0;        // callback-stall + frozen-ring restarts (see frozenRingRestartCount)
  uint64_t lastCaptureRestartUs = 0;
  // Frozen-ring self-heal (DXGI/WGC): streak guards a single slow poll; the last restart timestamp
  // lets a refreeze inside the window escalate to a full process restart.
  uint32_t frozenPollStreak = 0;
  uint64_t lastFrozenRestartUs = 0;
  uint64_t frozenRingRestartCount = 0;  // restarts driven specifically by the frozen ring
  uint64_t oldestGpuPendingPeakUs = 0;  // per-interval peak of the oldest GpuPending age
  uint32_t gpuPendingCountPeak = 0;     // per-interval peak GpuPending count
  // "readback slow" warn rate-limit: one line/sec with the window peak.
  uint64_t readbackSlowLastLogUs = 0;
  uint64_t readbackSlowWindowPeakUs = 0;
  // Readback-throughput soft watchdog over per-1s-window deltas of cumulative sources.
  uint64_t drainPrevAccepted = 0;
  uint64_t drainPrevBusyDrops = 0;
  uint64_t drainPrevSuperseded = 0;
  uint32_t drainConsecutiveSec = 0;
  uint64_t drainOldestPendingPeakUs = 0;  // per-1s window, reset every stats tick
  uint64_t lastDrainRestartUs = 0;
  uint64_t drainRestartCount = 0;
  bool drainPrevStreamActive = false;
  // cross-thread: main-loop liveness for the watchdog thread (phase + last progress stamp).
  std::atomic<uint32_t> mainLoopPhase{static_cast<uint32_t>(MainLoopPhase::Startup)};
  std::atomic<uint64_t> mainLoopProgressUs{0};
  std::atomic<uint64_t> mainLoopLastSeq{0};

  // --- behaviour (Phase 2-1: former main() lambdas enter_main_phase / mark_main_progress) ---
  // Publish the phase the main loop is about to enter; the watchdog thread picks the hang
  // threshold from it.
  void EnterMainPhase(MainLoopPhase p) {
    mainLoopPhase.store(static_cast<uint32_t>(p), std::memory_order_release);
  }
  // Stamp "the loop is alive" plus the phase; called once per iteration.
  void MarkMainProgress(MainLoopPhase p) {
    mainLoopProgressUs.store(qpc_now_us(), std::memory_order_release);
    mainLoopPhase.store(static_cast<uint32_t>(p), std::memory_order_release);
  }
};

}  // namespace remote60::native_poc
