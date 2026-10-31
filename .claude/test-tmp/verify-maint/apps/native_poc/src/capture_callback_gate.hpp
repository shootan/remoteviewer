#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace remote60::native_poc {
// A new gate belongs to each WGC attachment. The delegate retains it by value, so even a late
// invocation after unsubscribe can refuse entry without dereferencing destroyed capture state.
class CaptureCallbackGate {
  std::mutex mu_;
  std::condition_variable cv_;
  bool closed_ = false;
  unsigned active_ = 0;
 public:
  bool Enter() {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) return false;
    ++active_; return true;
  }
  void Leave() {
    { std::lock_guard<std::mutex> lock(mu_); --active_; }
    cv_.notify_all();
  }
  void Close() { std::lock_guard<std::mutex> lock(mu_); closed_ = true; }
  bool Drain(std::chrono::milliseconds budget) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, budget, [&] { return active_ == 0; });
  }
};
struct CaptureCallbackLease {
  CaptureCallbackGate* gate = nullptr;
  explicit CaptureCallbackLease(CaptureCallbackGate* candidate) {
    if (candidate && candidate->Enter()) gate = candidate;
  }
  CaptureCallbackLease(const CaptureCallbackLease&) = delete;
  CaptureCallbackLease& operator=(const CaptureCallbackLease&) = delete;
  ~CaptureCallbackLease() { if (gate) gate->Leave(); }
  explicit operator bool() const { return gate != nullptr; }
};
}
