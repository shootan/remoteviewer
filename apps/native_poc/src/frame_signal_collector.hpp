#pragma once

// Signal-only handoff from a framework callback to a worker this object owns. (encoder-rc-budget r12)
//
// Why: the fourth Home run's dumps (23 of 23) showed Windows.Graphics.Capture raising FrameArrived on
// a thread that holds the D3D11 device's critical section (d3d11!CDevice::CondObjectLock). Every
// std::mutex the handler then waited for that a D3D-calling thread held -- the readback ring's
// slotMu_ in Submit, the shared contextMu_ -- closed a lock-order cycle: the worker waited for the
// device CS inside GetData while holding those mutexes, the callback waited for the mutexes while
// holding the device CS. So the callback no longer touches capture state at all. It calls Signal(),
// which is an atomic load/add and SetEvent on an auto-reset event: no std::mutex, no condition
// variable, no allocation, no log line. ("Wait-free" here means exactly that -- it waits for no
// application lock; it is not a claim about SetEvent's real-time bound.) The worker thread owned
// here wakes and runs `work` (drain the frame pool, publish the latest frame); it may block on the
// host's mutexes because it holds no framework lock.
//
// Handoff contract:
//  - Bounded: one kernel event, no queue. Signal() cannot grow memory. Frames wait in the WGC pool
//    (a fixed buffer count chosen at pool creation) until the worker drains them. That the last
//    change is delivered is established by the lost-wake rule below and by the readback ring's
//    latest-wins publication (capture_readback_lock_test [6]), not by the pool's buffer policy.
//  - Lost wake: an auto-reset event set while `work` is running stays set, so the next wait returns
//    at once and `work` runs again. The last change is never left in the pool without a drain.
//  - Late callback: Signal() after Close() is a counted no-op. The delegate keeps this object alive
//    through a shared_ptr, so a callback that fires after Stop dereferences nothing destroyed.
//  - Start (r13, Codex C3): construction allocates nothing; Start() creates the event and the thread
//    and returns false when either fails -- the event handle is closed again if the thread could
//    not start -- and the owner must then NOT publish the attachment as started. A collector that
//    did not start ignores Signal() and stops trivially.
//  - Stop/join: Close() first (new signals ignored), then StopWithin(ms) wakes the worker and waits
//    for the thread with a bound. The caller must hold none of the mutexes `work` takes
//    (slotMu_, contextMu_, resourceMu, cadenceMu) and not the device CS.
//  - Memory order: closed_ is acquire/release; the event is the wake; counters are relaxed telemetry.
//
// This is not a general lock-free library: one event, one worker, one `work` function.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <system_error>
#include <thread>
#include <utility>

namespace remote60::native_poc {

class FrameSignalCollector {
 public:
  using WorkFn = std::function<void()>;
  // Failure injection for the tests that pin the start-failure contract. The product passes none.
  struct StartSeams {
    bool failEvent = false;   // behave as if CreateEventW returned nullptr
    bool failThread = false;  // behave as if std::thread's constructor threw
  };

  explicit FrameSignalCollector(WorkFn work, StartSeams seams = {}) : work_(std::move(work)), seams_(seams) {}
  ~FrameSignalCollector() {
    StopWithin(INFINITE);
    if (event_) CloseHandle(event_);
  }
  FrameSignalCollector(const FrameSignalCollector&) = delete;
  FrameSignalCollector& operator=(const FrameSignalCollector&) = delete;

  /**
   * Create the event and the worker thread. False = nothing is running and nothing leaked: the
   * caller must treat the attachment as not started (Codex C3). Idempotent on success.
   */
  bool Start() {
    if (started_) return true;
    event_ = seams_.failEvent ? nullptr : CreateEventW(nullptr, FALSE /* auto-reset */, FALSE, nullptr);
    if (!event_) {
      startError_ = "CreateEvent failed";
      return false;
    }
    try {
      if (seams_.failThread) throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again), "thread (injected)");
      worker_ = std::thread([this] { Loop(); });
    } catch (const std::exception& e) {
      startError_ = e.what();
      CloseHandle(event_);
      event_ = nullptr;
      return false;
    }
    started_ = true;
    return true;
  }

  /** True once Start() succeeded: Signal() wakes a worker. */
  bool ok() const { return started_; }
  const char* start_error() const { return startError_; }

  /**
   * Callback entry. Waits for no application lock: an atomic load/add and SetEvent. Returns false
   * when the collector is closed (late callback after detach) or never started -- the caller does
   * nothing else either way.
   */
  bool Signal() noexcept {
    signals_.fetch_add(1, std::memory_order_relaxed);
    if (closed_.load(std::memory_order_acquire) || !started_) {
      ignoredAfterClose_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    SetEvent(event_);
    return true;
  }

  /** New signals are ignored from here; the worker finishes the drain it is in and exits. */
  void Close() noexcept {
    closed_.store(true, std::memory_order_release);
    if (event_) SetEvent(event_);
  }

  /**
   * Close, wake, and wait for the worker up to `ms`. True = the thread has exited and is joined (or
   * never ran). False = still running after the bound (inside `work`); the caller decides what that
   * means -- the thread is left alone, and whatever `work` references must then stay alive.
   */
  bool StopWithin(DWORD ms) {
    Close();
    if (!worker_.joinable()) return true;
    const HANDLE h = static_cast<HANDLE>(worker_.native_handle());
    if (WaitForSingleObject(h, ms) != WAIT_OBJECT_0) return false;
    worker_.join();
    return true;
  }

  uint64_t Signals() const { return signals_.load(std::memory_order_relaxed); }
  uint64_t Drains() const { return drains_.load(std::memory_order_relaxed); }
  uint64_t IgnoredAfterClose() const { return ignoredAfterClose_.load(std::memory_order_relaxed); }
  bool closed() const { return closed_.load(std::memory_order_acquire); }

 private:
  void Loop() {
    for (;;) {
      WaitForSingleObject(event_, INFINITE);
      if (closed_.load(std::memory_order_acquire)) return;
      drains_.fetch_add(1, std::memory_order_relaxed);
      work_();
    }
  }

  WorkFn work_;
  StartSeams seams_;
  HANDLE event_ = nullptr;
  std::thread worker_;
  bool started_ = false;
  const char* startError_ = "not started";
  std::atomic<bool> closed_{false};
  std::atomic<uint64_t> signals_{0};
  std::atomic<uint64_t> drains_{0};
  std::atomic<uint64_t> ignoredAfterClose_{0};
};

}  // namespace remote60::native_poc
