#pragma once

// keepalive r3 R2-a: who may still adopt a native connection.
//
// The app numbers its connect operations (SessionPark.ConnectOwner) and tells the native side which one is
// current; a sign-out, an exit, a stop, a disconnect or another connect makes the old one stale. The native
// connect path (native_bridge.cpp: observe -> punch -> socket hand-over -> Connect) asks this gate at every
// step, and the last step -- the session's Connect -- runs inside Adopt:
//   - a stale operation does not start it (its prepared socket is closed instead);
//   - it is serialised against any other adoption (one Connect at a time on the one session);
//   - an operation that went stale WHILE Connect ran is not adopted: the connection it just made is
//     disconnected before Adopt returns, and 0 comes back -- I/O already started is not undone, but nothing
//     of it is ever handed to the app as its connection;
//   - otherwise Adopt returns the generation that Connect made (read inside the serialised section, so it
//     is this call's and not a later one's).
// Cancelling never waits for a running Connect (it only changes the current number).

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace remote60::native_poc {

class ConnectOpGate {
 public:
  void SetCurrent(int64_t op) { current_.store(op, std::memory_order_release); }
  bool IsCurrent(int64_t op) const { return op != 0 && current_.load(std::memory_order_acquire) == op; }

  struct Steps {
    std::function<bool()> connect;          // the session's Connect
    std::function<uint64_t()> generation;   // the session's connection generation now
    std::function<void()> disconnect;       // the session's Disconnect
    std::function<void()> closePrepared;    // drop the prepared socket of a connect that does not start
    std::function<void(const char*)> stage; // test hook: called at each stage boundary (may cancel there)
  };

  // 0: not adopted (stale before or during, or Connect failed). Otherwise the generation Connect made.
  uint64_t Adopt(int64_t op, const Steps& s) {
    std::lock_guard<std::mutex> lk(adoptMu_);
    if (s.stage) s.stage("before_adopt");
    if (!IsCurrent(op)) {
      if (s.closePrepared) s.closePrepared();
      staleRefused_.fetch_add(1, std::memory_order_relaxed);
      return 0;
    }
    const bool ok = s.connect && s.connect();
    if (s.stage) s.stage("after_connect");
    const uint64_t gen = ok && s.generation ? s.generation() : 0;
    if (!IsCurrent(op)) {
      if (ok && s.disconnect) s.disconnect();  // made by a stale operation: never handed over
      staleRefused_.fetch_add(1, std::memory_order_relaxed);
      return 0;
    }
    return gen;
  }

  uint32_t StaleRefused() const { return staleRefused_.load(std::memory_order_relaxed); }

 private:
  std::atomic<int64_t> current_{0};
  std::mutex adoptMu_;
  std::atomic<uint32_t> staleRefused_{0};
};

}  // namespace remote60::native_poc
