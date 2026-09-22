#pragma once

/**
 * A directory that answers what a test tells it to, and remembers what it was asked.
 *
 * TEST SUPPORT. Nothing in the product includes this.
 *
 * Lifted out of directory_retry_test.cpp when a second test needed the same fake. Both halves
 * are here because the flow needs both: the UDP probe is what makes an observation, and the
 * HTTP call is where the server's opinion of that observation comes back.
 *
 * Scripted, not simulated: `Script(path, replies)` queues answers and the last one repeats,
 * which is how a test says "every heartbeat from here on hands out this capability".
 */

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace remote60::native_poc::test_support {

struct Reply {
  int status = 200;
  std::string body;
};

/**
 * A directory that answers what the test tells it to, and remembers what it was asked.
 *
 * Both halves, because the flow needs both: the UDP probe is what makes an observation, and the
 * HTTP call is where the server's opinion of that observation comes back.
 */
class FakeDirectory {
 public:
  bool Start() {
    http_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (http_ == INVALID_SOCKET || udp_ == INVALID_SOCKET) return false;
    if (!Bind(http_, &httpPort_) || !Bind(udp_, &udpPort_)) return false;
    if (::listen(http_, 8) != 0) return false;
    httpThread_ = std::thread([this] { ServeHttp(); });
    udpThread_ = std::thread([this] { ServeUdp(); });
    return true;
  }

  /**
   * Starts with the UDP socket at httpPort + 1 -- the layout a deployed http directory has, and
   * the only one the legacy fallback can reach.
   *
   * The other Start() deliberately takes an ephemeral observe port so that a heartbeat proves the
   * advertisement was used. This one is the opposite case: it proves the advertisement is not
   * REQUIRED where a derivable port already worked. Loopback only, never a fixed well-known
   * number, and retried because the OS may hand out the adjacent port to someone else.
   */
  bool StartLegacyAdjacent() {
    for (int attempt = 0; attempt < 32; ++attempt) {
      http_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      udp_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (http_ == INVALID_SOCKET || udp_ == INVALID_SOCKET) return false;
      if (!Bind(http_, &httpPort_) || httpPort_ >= 65535) {
        CloseSockets();
        continue;
      }
      if (!BindAt(udp_, static_cast<uint16_t>(httpPort_ + 1))) {
        CloseSockets();
        continue;
      }
      udpPort_ = static_cast<uint16_t>(httpPort_ + 1);
      if (::listen(http_, 8) != 0) return false;
      httpThread_ = std::thread([this] { ServeHttp(); });
      udpThread_ = std::thread([this] { ServeUdp(); });
      return true;
    }
    return false;
  }

  void Stop() {
    stopping_ = true;
    if (http_ != INVALID_SOCKET) { closesocket(http_); http_ = INVALID_SOCKET; }
    if (udp_ != INVALID_SOCKET) { closesocket(udp_); udp_ = INVALID_SOCKET; }
    if (httpThread_.joinable()) httpThread_.join();
    if (udpThread_.joinable()) udpThread_.join();
  }

  ~FakeDirectory() { Stop(); }

  /** Queued answers for a path. The last one repeats once the queue runs out. */
  void Script(const std::string& path, std::vector<Reply> replies) {
    std::lock_guard<std::mutex> lk(mu_);
    scripts_[path] = std::move(replies);
    served_[path] = 0;
  }

  int Count(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    return counts_[path];
  }

  int ObserveProbes() const { return probes_.load(); }

  /** What to answer an OBSERVE probe with. Empty = the real thing (the sender's own address). */
  void ObserveReply(const std::string& json) {
    std::lock_guard<std::mutex> lk(mu_);
    observeReply_ = json;
  }

  std::string url() const { return "http://127.0.0.1:" + std::to_string(httpPort_); }
  uint16_t udpPort() const { return udpPort_; }

 private:
  void CloseSockets() {
    if (http_ != INVALID_SOCKET) {
      closesocket(http_);
      http_ = INVALID_SOCKET;
    }
    if (udp_ != INVALID_SOCKET) {
      closesocket(udp_);
      udp_ = INVALID_SOCKET;
    }
  }

  static bool BindAt(SOCKET s, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only
    addr.sin_port = htons(port);
    return bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
  }

  static bool Bind(SOCKET s, uint16_t* outPort) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only
    addr.sin_port = 0;                              // the OS picks
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    *outPort = ntohs(addr.sin_port);
    return true;
  }

  Reply Next(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    ++counts_[path];
    auto it = scripts_.find(path);
    if (it == scripts_.end() || it->second.empty()) return Reply{404, "{\"error\":\"no script\"}"};
    size_t& at = served_[path];
    const Reply reply = it->second[at < it->second.size() ? at : it->second.size() - 1];
    ++at;
    return reply;
  }

  void ServeHttp() {
    while (!stopping_) {
      SOCKET c = accept(http_, nullptr, nullptr);
      if (c == INVALID_SOCKET) return;
      std::string raw;
      char buf[4096];
      size_t headEnd = std::string::npos;
      size_t want = 0;
      for (;;) {
        if (headEnd == std::string::npos) {
          headEnd = raw.find("\r\n\r\n");
          if (headEnd != std::string::npos) {
            const size_t at = raw.find("Content-Length:");
            want = at == std::string::npos ? 0 : strtoul(raw.c_str() + at + 15, nullptr, 10);
            if (raw.size() >= headEnd + 4 + want) break;
          }
        } else if (raw.size() >= headEnd + 4 + want) {
          break;
        }
        const int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
      }

      // "POST /api/connect HTTP/1.1" -> "/api/connect"
      std::string path;
      const size_t firstSpace = raw.find(' ');
      const size_t secondSpace = firstSpace == std::string::npos
                                     ? std::string::npos
                                     : raw.find(' ', firstSpace + 1);
      if (firstSpace != std::string::npos && secondSpace != std::string::npos) {
        path = raw.substr(firstSpace + 1, secondSpace - firstSpace - 1);
      }
      const Reply reply = Next(path);
      const std::string out = "HTTP/1.1 " + std::to_string(reply.status) + " X\r\nContent-Length: " +
                              std::to_string(reply.body.size()) +
                              "\r\nConnection: close\r\n\r\n" + reply.body;
      size_t sent = 0;
      while (sent < out.size()) {
        const int n = send(c, out.data() + sent, static_cast<int>(out.size() - sent), 0);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
      }
      shutdown(c, SD_SEND);
      closesocket(c);
    }
  }

  void ServeUdp() {
    char buf[512];
    while (!stopping_) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(udp_, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from),
                            &fromLen);
      if (n <= 0) return;
      buf[n] = '\0';
      if (std::string(buf, static_cast<size_t>(n)).rfind("OBSERVE ", 0) != 0) continue;
      ++probes_;
      char ip[INET_ADDRSTRLEN] = {};
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
      std::string scripted;
      {
        std::lock_guard<std::mutex> lk(mu_);
        scripted = observeReply_;
      }
      const std::string reply =
          scripted.empty() ? std::string("{\"ip\":\"") + ip + "\",\"port\":" +
                                 std::to_string(ntohs(from.sin_port)) + "}"
                           : scripted;
      sendto(udp_, reply.data(), static_cast<int>(reply.size()), 0,
             reinterpret_cast<sockaddr*>(&from), fromLen);
    }
  }

  SOCKET http_ = INVALID_SOCKET;
  SOCKET udp_ = INVALID_SOCKET;
  uint16_t httpPort_ = 0;
  uint16_t udpPort_ = 0;
  std::map<std::string, std::vector<Reply>> scripts_;
  std::map<std::string, size_t> served_;
  std::map<std::string, int> counts_;
  std::atomic<int> probes_{0};
  std::string observeReply_;
  std::mutex mu_;
  std::thread httpThread_;
  std::thread udpThread_;
  std::atomic<bool> stopping_{false};
};

}  // namespace remote60::native_poc::test_support
