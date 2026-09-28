#pragma once

// Test support: a UDP impairment proxy on loopback -- one-way delay, random loss, a rate cap (a
// bottleneck queue) -- between a viewer-side socket and a host. Both directions, one thread pair
// each. Test code only; nothing in a product links it.
//
// Used by clip_image_e2e_test (in-process host + client) and abr_client_evidence_e2e_test (a real
// isolated host process, --proxy).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace remote60::native_poc::test {

inline uint64_t impair_now_us() {
  static const uint64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return static_cast<uint64_t>(f.QuadPart);
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return static_cast<uint64_t>(c.QuadPart) / freq * 1000000ull +
         static_cast<uint64_t>(c.QuadPart) % freq * 1000000ull / freq;
}
struct Impair {
  uint64_t oneWayDelayUs = 0;
  uint32_t lossPerMille = 0;
  uint64_t rateBps = 0;  // 0 = unlimited
  // The bottleneck's buffer, as queueing delay: a datagram that would wait longer is dropped
  // (drop-tail). A real rate limit has a finite buffer; an unbounded one turns any overshoot into
  // a delay that never drains.
  uint64_t maxQueueUs = 200000;
  uint32_t seed = 1;  // the loss pattern
};

class Proxy {
 public:
  // Client talks to `front`; the host talks to `back`.
  bool Start(const sockaddr_in& host, const Impair& im) {
    host_ = host;
    im_ = im;
    front_ = bind_any(&frontAddr);
    back_ = bind_any(&backAddr);
    if (front_ == INVALID_SOCKET || back_ == INVALID_SOCKET) return false;
    running_ = true;
    threads_.emplace_back([this] { Pump(front_, true); });
    threads_.emplace_back([this] { Pump(back_, false); });
    threads_.emplace_back([this] { Deliver(up_, true); });
    threads_.emplace_back([this] { Deliver(down_, false); });
    return true;
  }
  void Stop() {
    running_ = false;
    closesocket(front_);
    closesocket(back_);
    for (auto& t : threads_) t.join();
    threads_.clear();
  }
  sockaddr_in frontAddr{}, backAddr{};
  std::atomic<uint64_t> dropped{0};
  std::atomic<uint64_t> queueDropped{0};

 private:
  struct Pkt {
    uint64_t dueUs;
    std::vector<uint8_t> b;
  };
  struct Lane {
    std::mutex mu;
    std::deque<Pkt> q;
    uint64_t lastDueUs = 0;
  };
  static SOCKET bind_any(sockaddr_in* out) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    int len = sizeof(*out);
    getsockname(s, reinterpret_cast<sockaddr*>(out), &len);
    int buf = 8 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    DWORD to = 100;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    return s;
  }
  void Pump(SOCKET s, bool up) {
    std::mt19937 rng(im_.seed * 2 + (up ? 1 : 0));  // fixed per seed: runs are repeatable, seeds differ
    uint8_t buf[2048];
    while (running_) {
      sockaddr_in from{};
      int fl = sizeof(from);
      const int n = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
      if (n <= 0) continue;
      if (up) client_ = from;
      if (im_.lossPerMille && rng() % 1000 < im_.lossPerMille) {
        ++dropped;
        continue;
      }
      Lane& lane = up ? up_ : down_;
      std::lock_guard<std::mutex> l(lane.mu);
      const uint64_t now = impair_now_us();
      uint64_t due = now + im_.oneWayDelayUs;
      if (im_.rateBps) {  // a bottleneck: each datagram leaves after the previous one has
        const uint64_t ser = static_cast<uint64_t>(n) * 8ull * 1000000ull / im_.rateBps;
        due = std::max(due, lane.lastDueUs + ser);
        if (im_.maxQueueUs && due > now + im_.oneWayDelayUs + im_.maxQueueUs) {
          ++queueDropped;
          continue;
        }
      }
      lane.lastDueUs = due;
      lane.q.push_back({due, std::vector<uint8_t>(buf, buf + n)});
    }
  }
  void Deliver(Lane& lane, bool up) {
    while (running_) {
      Pkt p;
      bool have = false;
      {
        std::lock_guard<std::mutex> l(lane.mu);
        if (!lane.q.empty() && lane.q.front().dueUs <= impair_now_us()) {
          p = std::move(lane.q.front());
          lane.q.pop_front();
          have = true;
        }
      }
      if (!have) {
        Sleep(1);
        continue;
      }
      if (up) {
        sendto(back_, reinterpret_cast<const char*>(p.b.data()), static_cast<int>(p.b.size()), 0,
               reinterpret_cast<const sockaddr*>(&host_), sizeof(host_));
      } else {
        sendto(front_, reinterpret_cast<const char*>(p.b.data()), static_cast<int>(p.b.size()), 0,
               reinterpret_cast<const sockaddr*>(&client_), sizeof(client_));
      }
    }
  }
  sockaddr_in host_{}, client_{};
  Impair im_;
  SOCKET front_ = INVALID_SOCKET, back_ = INVALID_SOCKET;
  std::atomic<bool> running_{false};
  Lane up_, down_;
  std::vector<std::thread> threads_;
};

}  // namespace remote60::native_poc::test
