#pragma once

#include <winsock2.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include "bounded_process_exit.hpp"

namespace remote60::native_poc::viewer {

// Match the shared socket retry contract; an oversized UDP datagram is also nonterminal.
inline bool udp_ingress_retryable_error(int error) {
  return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK || error == WSAEINTR ||
         error == WSAEMSGSIZE;
}

// One socket owner. Control traffic is delivered immediately even while the video consumer is
// decoding/rebuilding. Only raw video packets queue; dropping under overload preserves the
// assembler's ordinary loss/IDR rules rather than silently dropping reference AUs after decode.
class UdpIngress {
 public:
  using Control = std::function<bool(const uint8_t*, size_t)>;
  UdpIngress(SOCKET socket, Control control, std::function<void()> tick)
      : socket_(socket), control_(std::move(control)), tick_(std::move(tick)),
        worker_([this] {
          try { run(); } catch (...) {
            { std::lock_guard<std::mutex> lock(mu_); closed_ = true; }
            ready_.notify_all();
          }
        }) {}
  ~UdpIngress() {
    stop_.store(true);
    // select's bounded wait lets us join without closing a socket owned by ViewerState.
    if (worker_.joinable()) {
      if (WaitForSingleObject(worker_.native_handle(), 3000) != WAIT_OBJECT_0) {
        const char message[] = "[viewer] UDP ingress shutdown deadline\n";
        remote60::native_poc::terminate_with_diagnostic(44, message, sizeof(message) - 1);
      }
      worker_.join();
    }
  }
  UdpIngress(const UdpIngress&) = delete;
  UdpIngress& operator=(const UdpIngress&) = delete;

  // -1 = terminal receive error, 0 = maintenance timeout, positive = datagram bytes.
  int Pop(uint8_t* dst, size_t capacity) {
    std::unique_lock<std::mutex> lock(mu_);
    ready_.wait_for(lock, std::chrono::milliseconds(25), [&] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) return closed_ ? -1 : 0;
    Packet p = std::move(queue_.front());
    queue_.pop_front();
    if (capacity < p.size) { ++dropped_; return 0; }
    std::memcpy(dst, p.data.data(), p.size);
    return static_cast<int>(p.size);
  }
  uint64_t dropped() const { return dropped_.load(); }

 private:
  struct Packet { std::array<uint8_t, 1600> data{}; size_t size = 0; };
  void run() {
    while (!stop_.load()) {
      tick_();
      fd_set readSet;
      FD_ZERO(&readSet);
      FD_SET(socket_, &readSet);
      timeval timeout{0, 25000};
      const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
      if (ready == 0) continue;
      if (ready < 0) {
        if (udp_ingress_retryable_error(WSAGetLastError())) continue;
        break;
      }
      Packet p;
      const int n = recv(socket_, reinterpret_cast<char*>(p.data.data()), static_cast<int>(p.data.size()), 0);
      if (n <= 0) {
        const int error = WSAGetLastError();
        if (n == 0 || udp_ingress_retryable_error(error)) continue;
        break;
      }
      p.size = static_cast<size_t>(n);
      if (control_(p.data.data(), p.size)) continue;
      {
        std::lock_guard<std::mutex> lock(mu_);
        // ~1.6 MB, a hard per-session bound. Keep the newest packets on overload; the gap is
        // visible to the existing assembler and the local discard counter is reported separately.
        if (queue_.size() == 1024) { queue_.pop_front(); ++dropped_; }
        queue_.push_back(std::move(p));
      }
      ready_.notify_one();
    }
    { std::lock_guard<std::mutex> lock(mu_); closed_ = true; }
    ready_.notify_all();
  }
  SOCKET socket_;
  Control control_;
  std::function<void()> tick_;
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> dropped_{0};
  std::mutex mu_;
  std::condition_variable ready_;
  std::deque<Packet> queue_;
  bool closed_ = false;
  std::thread worker_;
};
}  // namespace remote60::native_poc::viewer
