#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "bounded_process_exit.hpp"

namespace remote60::native_poc {
// Process-owned workers. Completed tasks are reaped; shutdown stops admission, cancels Windows
// synchronous pipe I/O and joins before their referenced UI/auth state can be destroyed.
class AsyncWorkerGroup {
  struct Worker { std::thread thread; std::shared_ptr<std::atomic<bool>> done; };
  std::mutex mutex_;
  std::vector<Worker> workers_;
  std::atomic<bool> stopping_{false};
  std::function<void()> onFailure_;
 public:
  explicit AsyncWorkerGroup(std::function<void()> onFailure = {}) : onFailure_(std::move(onFailure)) {}
  bool Stopping() const { return stopping_.load(std::memory_order_acquire); }
  template<class Fn> bool Launch(Fn&& fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Stopping()) return false;
    for (auto it = workers_.begin(); it != workers_.end();) {
      if (it->done->load(std::memory_order_acquire)) { it->thread.join(); it = workers_.erase(it); }
      else ++it;
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    workers_.reserve(workers_.size() + 1);  // allocate before creating a joinable thread
    workers_.push_back({std::thread([done, work = std::forward<Fn>(fn), failed = onFailure_]() mutable {
      try { work(); } catch (...) {
        if (failed) failed();
        else {
          const char message[] = "[async] worker failed unexpectedly\n";
          terminate_with_diagnostic(48, message, sizeof(message) - 1);
        }
      }
      done->store(true, std::memory_order_release);
    }), done});
    return true;
  }
  void Shutdown() {
    std::vector<Worker> pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_.store(true, std::memory_order_release);
      pending.swap(workers_);
    }
    const ULONGLONG deadline = GetTickCount64() + 15000;
    for (auto& worker : pending) CancelSynchronousIo(worker.thread.native_handle());
    for (auto& worker : pending) {
      const ULONGLONG now = GetTickCount64();
      const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
      if (WaitForSingleObject(worker.thread.native_handle(), remaining) != WAIT_OBJECT_0) {
        const char message[] = "[async] worker shutdown deadline; process exiting before shared state teardown\n";
        terminate_with_diagnostic(48, message, sizeof(message) - 1);
      }
      worker.thread.join();
    }
  }
  ~AsyncWorkerGroup() { Shutdown(); }
};
}
