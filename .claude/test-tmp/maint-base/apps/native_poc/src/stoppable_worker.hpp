#pragma once

// A long-running background thread WITH an owner (prelogin-ui r12, L1). Portable.
//
// r11 left two of them detached -- the streaming host's periodic Ready (a for(;;) holding a reference to the
// session's directory agent) and the pre-login worker's input-desktop watch (a for(;;) holding the capture
// state) -- so a normal shutdown, an early startup failure or an exception unwound and destroyed what they
// referenced while they still ran, and the Ready went on being sent after the host had stopped.
//
// The owner here is an object whose Stop() (and destructor) does, in this order: mark stopped -> wake any
// wait (Wait() returns at once) -> join, i.e. let the body's current step finish -- the body checks
// Stopping() between steps and passes it to any blocking call it can cancel -- before returning. So after
// Stop() returns the body runs no more, and the owner is declared so that it is stopped before whatever the
// body touches is destroyed.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace remote60::native_poc {

class StoppableWorker {
 public:
  StoppableWorker() = default;
  StoppableWorker(const StoppableWorker&) = delete;
  StoppableWorker& operator=(const StoppableWorker&) = delete;
  ~StoppableWorker() { Stop(); }

  // Runs `body(*this)` on a new thread (a running one is stopped and joined first).
  void Start(std::function<void(StoppableWorker&)> body) {
    Stop();
    {
      std::lock_guard<std::mutex> l(mu_);
      stop_ = false;
    }
    thread_ = std::thread([this, body = std::move(body)] { body(*this); });
  }

  // stop -> wake -> join. Idempotent. (Never called from the body itself.)
  void Stop() {
    {
      std::lock_guard<std::mutex> l(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
  }

  bool Stopping() const {
    std::lock_guard<std::mutex> l(mu_);
    return stop_;
  }

  // The body's sleep: false (at once) when stopped, true after `ms` otherwise.
  bool Wait(uint32_t ms) {
    std::unique_lock<std::mutex> l(mu_);
    cv_.wait_for(l, std::chrono::milliseconds(ms), [this] { return stop_; });
    return !stop_;
  }

  bool Running() const { return thread_.joinable(); }

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace remote60::native_poc
