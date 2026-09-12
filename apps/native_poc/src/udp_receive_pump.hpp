#pragma once
#include "native_socket.hpp"
#include "bounded_process_exit.hpp"
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace remote60::native_poc {
// The socket/ACK owner never calls a decoder or renderer. Video has a finite byte/packet budget;
// overflowing it drops media for the existing NACK/IDR machinery, never control acknowledgements.
class UdpReceivePump {
  SocketHandle socket_;
  std::function<bool(const void*, size_t)> control_;
  std::function<void()> tick_;
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> drops_{0};
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::vector<uint8_t>> queue_;
  size_t bytes_ = 0;
  int error_ = 0;
  std::thread worker_;
 public:
  UdpReceivePump(SocketHandle socket, std::function<bool(const void*, size_t)> control,
                  std::function<void()> tick)
      : socket_(socket), control_(std::move(control)), tick_(std::move(tick)), worker_([this] {
          try { Run(); } catch (...) {
            { std::lock_guard<std::mutex> lock(mu_); error_ = WSAENOBUFS; }
            cv_.notify_all();
          }
        }) {}
  void Run() {
    std::vector<uint8_t> buffer(65536);
    while (!stop_.load()) {
      const int count = recv(socket_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
      const int receiveError = count < 0 ? WSAGetLastError() : 0;
      if (stop_.load()) break;
      tick_();
      if (count < 0) {
        const int error = receiveError;
        if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK || error == WSAEINTR ||
            error == WSAECONNRESET || error == WSAEMSGSIZE) continue;
        { std::lock_guard<std::mutex> lock(mu_); error_ = error; }
        cv_.notify_all();
        return;
      }
      if (count == 0 || control_(buffer.data(), static_cast<size_t>(count))) continue;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (queue_.size() >= 1024 || bytes_ + count > 4u * 1024u * 1024u) { ++drops_; continue; }
        queue_.emplace_back(buffer.begin(), buffer.begin() + count);
        bytes_ += count;
      }
      cv_.notify_one();
    }
  }
  int Read(void* destination, size_t capacity, uint32_t timeoutMs = 25) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !queue_.empty() || error_ || stop_.load(); });
    if (queue_.empty()) { WSASetLastError(error_ ? error_ : WSAETIMEDOUT); return -1; }
    auto packet = std::move(queue_.front()); queue_.pop_front(); bytes_ -= packet.size();
    lock.unlock();
    if (packet.size() > capacity) { WSASetLastError(WSAEMSGSIZE); return -1; }
    std::memcpy(destination, packet.data(), packet.size());
    return static_cast<int>(packet.size());
  }
  uint64_t Drops() const { return drops_.load(); }
  ~UdpReceivePump() {
    stop_.store(true); cv_.notify_all();
    if (WaitForSingleObject(worker_.native_handle(), 3000) != WAIT_OBJECT_0) {
      const char text[] = "[viewer] UDP receive pump failed to stop\n";
      terminate_with_diagnostic(44, text, sizeof(text) - 1);
    }
    worker_.join();
  }
};
}
