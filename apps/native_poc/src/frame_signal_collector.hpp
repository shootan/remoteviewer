#pragma once

// Signal-only handoff from a framework callback to a worker this object owns. (encoder-rc-budget r12)
//
// Why: the fourth Home run's dumps (23 of 23) showed Windows.Graphics.Capture raising FrameArrived on
// a thread that holds the D3D11 device's critical section (d3d11!CDevice::CondObjectLock). Every
// std::mutex the handler then waited for that a D3D-calling thread held -- the readback ring's
// slotMu_ in Submit, the shared contextMu_ -- closed a lock-order cycle: the worker waited for the
// device CS inside GetData while holding those mutexes, the callback waited for the mutexes while
// holding the device CS. So the callback no longer touches capture state at all. It calls Signal(),
// which is an atomic store and SetEvent on an auto-reset event: no std::mutex, no condition
// variable, no allocation, no log line. The worker thread owned here wakes and runs `work` (drain
// the frame pool, publish the latest frame); it may block on the host's mutexes because it holds
// no framework lock.
//
// Handoff contract:
//  - Bounded: one kernel event, no queue. Signal() cannot grow memory. Frames wait in the WGC pool
//    (a fixed buffer count chosen at pool creation) until the worker drains them -- the memory is
//    the pool's, and a pool that is full simply reuses its oldest buffer, as WGC always did.
//  - Lost wake: an auto-reset event set while `work` is running stays set, so the next wait returns
//    at once and `work` runs again. The last change is never left in the pool without a drain.
//  - Late callback: Signal() after Close() is a counted no-op. The delegate keeps this object alive
//    through a shared_ptr, so a callback that fires after Stop dereferences nothing destroyed.
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
#include <thread>
#include <utility>

namespace remote60::native_poc {

class FrameSignalCollector {
 public:
  using WorkFn = std::function<void()>;

  explicit FrameSignalCollector(WorkFn work) : work_(std::move(work)) {
    event_ = CreateEventW(nullptr, FALSE /* auto-reset */, FALSE, nullptr);
    if (event_) worker_ = std::thread([this] { Loop(); });
  }
  ~FrameSignalCollector() {
    StopWithin(INFINITE);
    if (event_) CloseHandle(event_);
  }
  FrameSignalCollector(const FrameSignalCollector&) = delete;
  FrameSignalCollector& operator=(const FrameSignalCollector&) = delete;

  /** False when the event could not be created: Signal() then wakes nothing. */
  bool ok() const { return event_ != nullptr; }

  /**
   * Callback entry. Wait-free: an atomic load/add and SetEvent. Returns false when the collector is
   * closed (late callback after detach) -- the caller does nothing else either way.
   */
  bool Signal() noexcept {
    signals_.fetch_add(1, std::memory_order_relaxed);
    if (closed_.load(std::memory_order_acquire)) {
      ignoredAfterClose_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    if (event_) SetEvent(event_);
    return true;
  }

  /** New signals are ignored from here; the worker finishes the drain it is in and exits. */
  void Close() noexcept {
    closed_.store(true, std::memory_order_release);
    if (event_) SetEvent(event_);
  }

  /**
   * Close, wake, and wait for the worker up to `ms`. True = the thread has exited and is joined.
   * False = still running after the bound (inside `work`); the caller decides what that means --
   * the thread is left alone, and whatever `work` references must then stay alive.
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
  HANDLE event_ = nullptr;
  std::thread worker_;
  std::atomic<bool> closed_{false};
  std::atomic<uint64_t> signals_{0};
  std::atomic<uint64_t> drains_{0};
  std::atomic<uint64_t> ignoredAfterClose_{0};
};

}  // namespace remote60::native_poc
