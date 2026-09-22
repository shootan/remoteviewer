// What the clients do when the directory says it has no address observation for them.
//
// The server answers 409 for that now, and it is a state either end can repair: send another
// observation and ask again. The dangerous shapes are the two either side of doing it right --
// treating it as an authentication failure (which signs the host out and makes it re-register,
// fixing nothing and losing the cached token) and retrying in a loop (which spins against a
// server refusing for a reason the client cannot fix, with a user waiting on it).
//
// Neither shape shows up in a passing suite unless something counts the requests. So this file
// stands up a directory -- http and udp, in this process, on ports the OS picks -- scripts what it
// answers, and counts what arrives. The assertions are about the number of requests as much as the
// outcome: "it worked" is also true of a client that tried forty times.
//
// The same fixture covers directory_observe_from_health(), which had no test at all: it is the
// route a viewer uses when it resumed from a stored session and so never saw a login response.
//
// THIS SUITE TAKES ABOUT 140 SECONDS, AND THAT IS NOT A HANG.
//
// Every wait here is bounded (the polls are 150 x 100ms and then fail), but the waits are real:
// the product floors the heartbeat interval at five seconds -- `if (cfg_.heartbeatSeconds < 5)
// cfg_.heartbeatSeconds = 5` -- so a test cannot ask for a faster cycle, and each HostAgent
// scenario has to sit through one. The only way to make this quicker is to let the interval be
// injected, the way the update tests inject their lock name.
//
// Recorded here because the tempting fix is to delete scenarios, and the scenarios are the point:
// what they assert is the number of requests, which is the only thing that separates "repaired it
// once" from "retried in a loop" or "signed out on the way to succeeding".

#ifndef NOMINMAX
#define NOMINMAX  // or windows.h's min/max macros eat the (std::min) in native_socket.hpp
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "directory_client.hpp"
#include "poc_protocol.hpp"
#include "directory_rendezvous.hpp"
#include "directory_session_bootstrap.hpp"
#include "directory_session_client.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

int gFailures = 0;

void check(const char* name, bool cond, const std::string& detail = {}) {
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ",
              detail.c_str());
  if (!cond) ++gFailures;
}

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

std::string candidateBody() {
  // One candidate that resolves and answers nothing. PunchAny falls back to the first candidate
  // rather than giving up, so the session still opens -- which is what lets this test end at the
  // directory exchange instead of needing a peer.
  return "{\"punchToken\":\"" + std::string(32, 'a') +
         "\",\"hostPublicIp\":\"127.0.0.1\",\"hostPublicUdpPort\":9,"
         "\"candidates\":[{\"ip\":\"127.0.0.1\",\"port\":9,\"kind\":\"public\"}]}";
}

/**
 * Runs a HostAgent against the fake directory for a few seconds.
 *
 * The agent never owns a socket -- the address the directory observes has to be the one media
 * arrives on -- so the test supplies one, forwards what the agent sends, and feeds the replies
 * back in through ConsumeUdpPacket. That is the same wiring the real host has.
 */
std::string exe_directory() {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string text(path);
  const size_t slash = text.find_last_of("\/");
  return slash == std::string::npos ? std::string(".") : text.substr(0, slash);
}

/**
 * Writes a host cache so the agent starts as a machine that has registered before.
 *
 * That is the state the field defect lived in: a cached token means EnsureRegistered() returns
 * immediately, so nothing ever tells the host where observations go.
 */
void SeedHostCache(const std::string& path, const std::string& url) {
  remote60::native_poc::directory::HostCache cache;
  cache.directoryUrl = url;
  cache.accountId = "tester";
  cache.machineId = remote60::native_poc::directory::machine_id();
  cache.hostName = "Cached PC";
  cache.hostId = "h-cached";
  cache.hostToken = std::string(32, 'e');
  remote60::native_poc::directory::save_host_cache(path, cache);
}

std::string RunHost(FakeDirectory& dir, const char* label, int wantHeartbeats,
                    int wantRegisters, bool seedCache = false, uint16_t explicitPort = 0) {
  SOCKET media = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in bindAddr{};
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(media, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
  DWORD timeout = 200;
  setsockopt(media, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));

  remote60::native_poc::directory::HostAgent agent;
  std::atomic<bool> pumping{true};
  std::thread pump([&] {
    char buf[2048];
    while (pumping.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(media, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                             &fromLen);
      if (n > 0) agent.ConsumeUdpPacket(buf, static_cast<size_t>(n), from);
    }
  });

  remote60::native_poc::directory::HostAgentConfig cfg;
  cfg.url = dir.url();
  cfg.accountId = "tester";
  cfg.password = "test-pass-1234";
  cfg.hostName = "Fixture PC";
  // Beside this executable and unique per case, so no run reads another's cached token and
  // nothing outside the build tree is written.
  cfg.cachePath = exe_directory() + "\\retry-fixture-" + std::string(label) + ".json";
  // Nothing pinned by default: the point of most of these cases is what the agent works out for
  // itself. A case that pins one says so.
  cfg.observeUdpPort = explicitPort;
  cfg.heartbeatSeconds = 5;
  if (seedCache) {
    SeedHostCache(cfg.cachePath, cfg.url);
    // No password either. Registration is then impossible, so a heartbeat can only happen if the
    // cached token was used -- which is what puts the agent in the state under test.
    cfg.password.clear();
  }

  std::string error;
  const bool started = agent.Start(cfg, [&](const void* data, size_t len, const sockaddr_in& to) {
    sendto(media, static_cast<const char*>(data), static_cast<int>(len), 0,
           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  }, &error);
  check((std::string("the host agent starts (") + label + ")").c_str(), started, error);

  // Waits for what this case is about rather than for a fixed span: the repaired 409 retries
  // immediately, while a cleared token waits out a heartbeat cycle before registering again.
  // Bounded, so a client that never gets there fails rather than hangs.
  for (int i = 0; i < 150; ++i) {
    if (dir.Count("/api/host/heartbeat") >= wantHeartbeats &&
        dir.Count("/api/host/register") >= wantRegisters) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  const std::string status = agent.StatusLine();
  agent.Stop();
  pumping = false;
  closesocket(media);
  if (pump.joinable()) pump.join();
  DeleteFileA(cfg.cachePath.c_str());
  return status;
}

/**
 * A running HostAgent whose sends are recorded instead of reaching a socket.
 *
 * c1-lan-relay needs the real ConsumeUdpPacket and the real send_ seam -- the punch reply goes out
 * through the same function the outbound punches use, because it has to leave from the candidate
 * tuple the client is watching. Everything the agent does with the directory is real; only the
 * datagram's destination is a vector.
 */
struct RecordedSend {
  std::vector<uint8_t> bytes;
  sockaddr_in to{};
};

struct PunchHarness {
  remote60::native_poc::directory::HostAgent agent;
  std::mutex mu;
  std::vector<RecordedSend> sent;
  std::string cachePath;

  // A real socket, and a thread feeding what arrives back in. The recorder alone was not enough:
  // the agent's OBSERVE probe goes through the same seam, and with nowhere to go the observation
  // never came back, the cycle never reached the heartbeat, and no capability was ever collected.
  // Every punch then read reason=closed -- correctly, which is how the harness's own gap showed.
  SOCKET media = INVALID_SOCKET;
  uint16_t mediaPort = 0;  // what the client will use as the "private" candidate
  std::thread pump;
  std::atomic<bool> pumping{false};

  std::vector<RecordedSend> take() {
    std::lock_guard<std::mutex> lock(mu);
    std::vector<RecordedSend> out;
    out.swap(sent);
    return out;
  }
  size_t count() {
    std::lock_guard<std::mutex> lock(mu);
    return sent.size();
  }
  void Stop() {
    agent.Stop();
    pumping = false;
    if (media != INVALID_SOCKET) {
      closesocket(media);
      media = INVALID_SOCKET;
    }
    if (pump.joinable()) pump.join();
  }
  ~PunchHarness() {
    Stop();
    if (!cachePath.empty()) DeleteFileA(cachePath.c_str());
  }
};


/**
 * A stand-in for the relay: it answers, but only after the grace period.
 *
 * The server waits RELAY_GRACE_MS (2500) before answering a punch, which is what made it lose to
 * a host that answers immediately -- and win against one that never answers at all. That timing
 * is the whole contest, so the fake keeps it.
 */
struct LateRelay {
  SOCKET sock = INVALID_SOCKET;
  uint16_t port = 0;
  std::thread thread;
  std::atomic<bool> running{false};
  std::atomic<int> answered{0};

  bool Start(uint32_t graceMs) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    sockaddr_in bound{};
    int boundLen = sizeof(bound);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &boundLen) != 0) return false;
    port = ntohs(bound.sin_port);
    DWORD timeout = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    running = true;
    thread = std::thread([this, graceMs] {
      char buf[2048];
      bool scheduled = false;
      std::chrono::steady_clock::time_point answerAt;
      sockaddr_in peer{};
      while (running.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                               &fromLen);
        if (n > 0 && !scheduled) {
          scheduled = true;
          peer = from;
          answerAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
        }
        if (scheduled && std::chrono::steady_clock::now() >= answerAt) {
          remote60::native_poc::UdpHelloPacket reply{};
          reply.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Punch);
          sendto(sock, reinterpret_cast<const char*>(&reply), sizeof(reply), 0,
                 reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
          ++answered;
          scheduled = false;
        }
      }
    });
    return true;
  }
  void Stop() {
    running = false;
    if (sock != INVALID_SOCKET) {
      closesocket(sock);
      sock = INVALID_SOCKET;
    }
    if (thread.joinable()) thread.join();
  }
  ~LateRelay() { Stop(); }
};

/** A heartbeat body that hands the host one capability for a PUBLIC tuple. */
std::string heartbeatWithCapability(const std::string& token, const char* ip, uint16_t port) {
  return "{\"ok\":true,\"pendingPunch\":[{\"ip\":\"" + std::string(ip) + "\",\"port\":" +
         std::to_string(port) + ",\"punchToken\":\"" + token + "\"}]}";
}

sockaddr_in addrOf(const char* ip, uint16_t port) {
  sockaddr_in out{};
  out.sin_family = AF_INET;
  out.sin_port = htons(port);
  inet_pton(AF_INET, ip, &out.sin_addr);
  return out;
}

remote60::native_poc::UdpHelloPacket punchPacket() {
  remote60::native_poc::UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Punch);
  return packet;
}

/** Did anything get sent to this address? The outbound punch is the only thing that would. */
bool SentTo(PunchHarness* harness, const sockaddr_in& want) {
  std::lock_guard<std::mutex> lock(harness->mu);
  for (const RecordedSend& record : harness->sent) {
    if (record.to.sin_addr.s_addr == want.sin_addr.s_addr &&
        record.to.sin_port == want.sin_port) {
      return true;
    }
  }
  return false;
}

/**
 * Starts an agent against `dir` and waits for the state the case needs.
 *
 * `capabilityTarget`, when set, is the tuple the heartbeat hands out: the wait ends when a
 * datagram has actually gone there, which is the only thing that proves Punch() ran and so that
 * the reply window is open. Waiting for "any datagram" is not enough -- the observe probe goes
 * through the same seam, and waiting on it let the first version of this test run its punches
 * against a host that had collected nothing.
 */
bool StartPunchHarness(FakeDirectory& dir, PunchHarness* harness, const char* label,
                       const sockaddr_in* capabilityTarget) {
  harness->cachePath = exe_directory() + "\\punch-fixture-" + std::string(label) + ".json";

  harness->media = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in bindAddr{};
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(harness->media, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
  sockaddr_in bound{};
  int boundLen = sizeof(bound);
  if (getsockname(harness->media, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) {
    harness->mediaPort = ntohs(bound.sin_port);
  }
  DWORD timeout = 200;
  setsockopt(harness->media, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  harness->pumping = true;
  harness->pump = std::thread([harness] {
    char buf[2048];
    while (harness->pumping.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(harness->media, buf, sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (n > 0) harness->agent.ConsumeUdpPacket(buf, static_cast<size_t>(n), from);
    }
  });
  remote60::native_poc::directory::HostAgentConfig cfg;
  cfg.url = dir.url();
  cfg.accountId = "tester";
  cfg.password = "test-pass-1234";
  cfg.hostName = "Punch PC";
  cfg.cachePath = harness->cachePath;
  cfg.heartbeatSeconds = 5;

  std::string error;
  if (!harness->agent.Start(cfg, [harness](const void* data, size_t len, const sockaddr_in& to) {
        RecordedSend record;
        record.bytes.assign(static_cast<const uint8_t*>(data),
                            static_cast<const uint8_t*>(data) + len);
        record.to = to;
        {
          std::lock_guard<std::mutex> lock(harness->mu);
          harness->sent.push_back(record);
        }
        // Recorded AND sent: the directory has to see the observe probe for the cycle to get as
        // far as a heartbeat, and the reply has to leave from this socket for the same reason the
        // product needs it to -- the candidate tuple is this socket's address.
        sendto(harness->media, static_cast<const char*>(data), static_cast<int>(len), 0,
               reinterpret_cast<const sockaddr*>(&to), sizeof(to));
      }, &error)) {
    return false;
  }
  for (int i = 0; i < 300; ++i) {
    const bool ready = capabilityTarget ? SentTo(harness, *capabilityTarget)
                                        : dir.Count("/api/host/heartbeat") >= 1;
    if (ready) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return capabilityTarget ? SentTo(harness, *capabilityTarget)
                          : dir.Count("/api/host/heartbeat") >= 1;
}

}  // namespace

int main() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL  winsock did not start\n");
    return 1;
  }

  using namespace remote60::native_poc;


  // ============================================ c1-lan-relay: the host answers a punch, bounded
  //
  // The field defect: a viewer on the same LAN punched the host's private address twenty times,
  // every punch arrived, and the host sent nothing back -- so the viewer's PunchAny never saw the
  // private candidate answer and the relay's 2500 ms grace won. These run the REAL path:
  // ConsumeUdpPacket deciding, and the real send_ seam carrying the reply out of the same socket
  // the candidate tuple names.
  {
    FakeDirectory dir;
    check("the fake directory starts (punch reply)", dir.Start());
    // The observe endpoint has to come from somewhere or the cycle never reaches a heartbeat:
    // step=health ok=0 then step=observe ok=0, forever, and no capability is ever collected.
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    const std::string token(32, 'c');
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-1\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    // Script's last reply repeats, so every heartbeat keeps handing out the capability.
    dir.Script("/api/host/heartbeat",
               {Reply{200, heartbeatWithCapability(token, "211.218.222.1", 60420)}});

    PunchHarness harness;
    const sockaddr_in capabilityTarget = addrOf("211.218.222.1", 60420);
    check("the host agent starts and collects a capability",
          StartPunchHarness(dir, &harness, "reply", &capabilityTarget));

    // Everything sent while starting up is the outbound punch to the public tuple. The reply is
    // what happens next, so the record starts clean here.
    harness.take();

    const sockaddr_in client = addrOf("192.168.20.16", 60420);
    const remote60::native_poc::UdpHelloPacket punch = punchPacket();
    harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);

    std::vector<RecordedSend> replies = harness.take();
    check("a punch from a LAN client is answered", replies.size() == 1,
          std::to_string(replies.size()) + " datagrams");
    if (replies.size() == 1) {
      check("...with one datagram of exactly the same size",
            replies[0].bytes.size() == sizeof(remote60::native_poc::UdpHelloPacket),
            std::to_string(replies[0].bytes.size()) + " bytes");
      check("...addressed back to the source that punched",
            replies[0].to.sin_addr.s_addr == client.sin_addr.s_addr &&
                replies[0].to.sin_port == client.sin_port);
      const auto* asHello =
          reinterpret_cast<const remote60::native_poc::UdpHelloPacket*>(replies[0].bytes.data());
      check("...and it is a punch, carrying nothing",
            asHello->kind == static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Punch) &&
                asHello->authToken[0] == '\0');
    }

    // The budget, through the product path. A client sends 25 punches in an attempt; the 26th
    // and everything after it gets nothing, and the host does not start a conversation.
    for (int i = 0; i < 40; ++i) harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    const size_t afterFlood = harness.take().size();
    check("one source is answered at most its allowance",
          afterFlood + 1 <= remote60::native_poc::kPunchReplyPerSource,
          std::to_string(afterFlood + 1) + " of " +
              std::to_string(remote60::native_poc::kPunchReplyPerSource));

    // A flood must not turn into a flood of HTTP either: the refresh flag is one per cycle and
    // punches do not get to re-arm it while it is up.
    const int heartbeatsBefore = dir.Count("/api/host/heartbeat");
    for (int i = 0; i < 200; ++i) harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const int heartbeatsAfter = dir.Count("/api/host/heartbeat");
    check("two hundred punches do not become two hundred heartbeats",
          heartbeatsAfter - heartbeatsBefore <= 2,
          std::to_string(heartbeatsAfter - heartbeatsBefore) + " heartbeats");

    // AuthorizePeer, unchanged: the capability was issued against the PUBLIC tuple and the Hello
    // arrives from the PRIVATE one, which is exactly what a direct LAN connection looks like.
    // The rule is not being modified here -- it is being pinned, because the whole fix depends
    // on it staying true.
    remote60::native_poc::directory::HostAgent::PeerAuthDiag diag;
    const bool authorized = harness.agent.AuthorizePeer(token, client, &diag);
    check("a capability issued for the public tuple is accepted from the private one", authorized);
    check("...and is reported as an endpoint that moved", diag.endpointMoved);
    check("...and the capability is single use", !harness.agent.AuthorizePeer(token, client, &diag));
    check("...and a token nobody issued is refused",
          !harness.agent.AuthorizePeer(std::string(32, 'z'), client, &diag));
  }

  {
    // No capability, no window. A host nobody has asked about answers nothing at all -- which is
    // what keeps this from being an open reflector for anyone who finds the port.
    FakeDirectory dir;
    check("the fake directory starts (no window)", dir.Start());
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-2\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    PunchHarness harness;
    check("the host agent starts without a capability",
          StartPunchHarness(dir, &harness, "nowindow", nullptr));
    harness.take();

    const sockaddr_in client = addrOf("192.168.20.16", 60420);
    const remote60::native_poc::UdpHelloPacket punch = punchPacket();
    for (int i = 0; i < 20; ++i) harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    check("a host with no connection in progress answers nothing", harness.take().empty());

    // And the punches themselves must not open the window. Twenty arrived above; if receiving
    // one were enough to arm the host, the twenty-first would be answered.
    harness.agent.ConsumeUdpPacket(&punch, sizeof(punch), client);
    check("...and receiving punches does not arm it", harness.take().empty());
  }


  // ================= c1-lan-relay, end to end: which candidate the real client actually picks
  //
  // The two halves joined. The host side is the product path -- ConsumeUdpPacket deciding and
  // send_ carrying the reply out of the very socket the private candidate names. The client side
  // is the product's own DirectoryRendezvous::PunchAny, given the same shape of candidate list
  // the directory hands out: private first, then a relay that answers on the 2500 ms grace.
  //
  // What this does NOT cover, and is not claimed: the Hello/HelloAck that follows. AuthorizePeer
  // accepting a private-source Hello against a public-tuple capability is pinned above, at unit
  // level, but the session handshake itself is not driven here.
  {
    FakeDirectory dir;
    check("the fake directory starts (e2e)", dir.Start());
    const std::string token(32, 'd');
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-e2e\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    dir.Script("/api/host/heartbeat",
               {Reply{200, heartbeatWithCapability(token, "211.218.222.1", 60420)}});

    PunchHarness harness;
    const sockaddr_in capabilityTarget = addrOf("211.218.222.1", 60420);
    check("the host is up with a capability (e2e)",
          StartPunchHarness(dir, &harness, "e2e", &capabilityTarget));
    check("...and the host socket has a port for the client to punch", harness.mediaPort != 0);

    LateRelay relay;
    check("the late-answering relay starts", relay.Start(2500));

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself through the directory",
          rv.Observe("127.0.0.1", dir.udpPort(), "e2e-token", &observed, &error), error);

    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", harness.mediaPort, "private"});
    candidates.push_back({"127.0.0.1", relay.port, "relay"});

    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    const bool picked = rv.PunchAny(candidates, 4000, &chosen, &error);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();

    check("the client picks a candidate", picked, error);
    check("...and it is the private one, not the relay", chosen.kind == "private",
          chosen.kind + " at " + std::to_string(elapsedMs) + "ms");
    // The number that matters: the relay answers at 2500ms, so anything under that means the
    // host's own reply is what was seen. Recorded rather than asserted tightly -- the claim is
    // the ORDER, not a latency figure from a loopback test.
    check("...well before the relay's grace period", elapsedMs < 2500,
          std::to_string(elapsedMs) + "ms of the relay's 2500ms");
    rv.Close();
    harness.Stop();
    relay.Stop();
  }

  {
    // The same wiring with the host NOT armed -- no capability, so no window, so no reply. This
    // is the state the field was in, and the relay wins exactly as it did there. It is also the
    // control for the case above: without it, "private won" could be an artefact of the harness.
    FakeDirectory dir;
    check("the fake directory starts (e2e control)", dir.Start());
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                        std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/register", {Reply{200, "{\"ok\":true,\"hostId\":\"h-e2e2\",\"hostToken\":\"" +
                                             std::string(32, 'e') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[]}"}});

    PunchHarness harness;
    check("the host is up without a capability (e2e control)",
          StartPunchHarness(dir, &harness, "e2e-silent", nullptr));
    check("...and its socket has a port", harness.mediaPort != 0);

    LateRelay relay;
    check("the late-answering relay starts (control)", relay.Start(2500));

    remote60::native_poc::DirectoryRendezvous rv;
    std::string observed, error;
    check("the client observes itself (control)",
          rv.Observe("127.0.0.1", dir.udpPort(), "e2e-token-2", &observed, &error), error);

    std::vector<remote60::native_poc::RendezvousCandidate> candidates;
    candidates.push_back({"127.0.0.1", harness.mediaPort, "private"});
    candidates.push_back({"127.0.0.1", relay.port, "relay"});

    remote60::native_poc::RendezvousCandidate chosen;
    const auto began = std::chrono::steady_clock::now();
    const bool picked = rv.PunchAny(candidates, 5000, &chosen, &error);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began).count();

    check("a silent host loses to the relay", picked && chosen.kind == "relay",
          chosen.kind + " at " + std::to_string(elapsedMs) + "ms");
    check("...and it takes the relay's grace period to get there", elapsedMs >= 2400,
          std::to_string(elapsedMs) + "ms");
    rv.Close();
    harness.Stop();
    relay.Stop();
  }

  // ------------------------------------------------- directory_observe_from_health(), untested
  //
  // The route a viewer takes when it resumed from a stored session: it never saw a login
  // response, so this is the only place the observe endpoint can come from. On an https
  // directory, getting nothing here means refusing to observe -- so a reconnect would fail where
  // a fresh sign-in works, and the difference would look like nothing at all.
  {
    FakeDirectory dir;
    check("the fake directory starts", dir.Start());

    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":29181}}"}});
    directory::ObserveEndpoint got;
    std::string error;
    check("the health route carries the observe endpoint",
          directory_observe_from_health(dir.url(), &got, &error), error);
    check("...with the port the server named", got.known && got.port == 29181,
          std::to_string(got.port));

    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":29181,\"host\":\"obs.example\"}}"}});
    got = directory::ObserveEndpoint{};
    check("...and the host when there is one",
          directory_observe_from_health(dir.url(), &got, &error) && got.host == "obs.example",
          got.host);

    // An older directory says nothing. That is a documented state, not a failure of this call --
    // the port rule has an answer for absence, and reporting it as an error would turn every
    // older server into a broken one.
    dir.Script("/healthz", {Reply{200, "{\"ok\":true}"}});
    got = directory::ObserveEndpoint{};
    error.clear();
    const bool silent = directory_observe_from_health(dir.url(), &got, &error);
    check("a directory that says nothing leaves the endpoint unknown", !silent && !got.known);
    check("...and does not invent a port", got.port == 0, std::to_string(got.port));

    dir.Script("/healthz", {Reply{500, "{\"error\":\"broken\"}"}});
    got = directory::ObserveEndpoint{};
    error.clear();
    check("a server error is an error", !directory_observe_from_health(dir.url(), &got, &error));
    check("...and it says something", !error.empty(), error);

    const std::string deadUrl = dir.url();
    dir.Stop();
    got = directory::ObserveEndpoint{};
    error.clear();
    check("a directory that is not there is reported as unreachable",
          !directory_observe_from_health(deadUrl, &got, &error) &&
              error.find("cannot reach") != std::string::npos,
          error);
  }

  // ------------------------------------------------------- the viewer's retry, counted
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/connect", {Reply{409, "{\"error\":\"observation_required\"}"},
                                Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    const bool opened = directory_session_open(request, &session, &error);
    check("a 409 on connect is repaired rather than reported", opened, error);
    check("...by asking exactly twice", dir.Count("/api/connect") == 2,
          std::to_string(dir.Count("/api/connect")));
    check("...and by observing again first", dir.ObserveProbes() >= 2,
          std::to_string(dir.ObserveProbes()));
    check("...and without signing in again", dir.Count("/api/login") == 0,
          std::to_string(dir.Count("/api/login")));
    if (session.socket != kInvalidSocket) closesocket(session.socket);
    dir.Stop();
  }

  {
    // Refused twice: the client must give up, not keep going. This is the assertion that a loop
    // would fail -- the outcome is the same either way, only the count differs.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/connect", {Reply{409, "{\"error\":\"observation_required\"}"}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    check("a directory that keeps refusing is not retried forever",
          !directory_session_open(request, &session, &error), error);
    check("...and it stopped at two attempts", dir.Count("/api/connect") == 2,
          std::to_string(dir.Count("/api/connect")));
    dir.Stop();
  }

  {
    // Every other refusal is not repairable from here, so it must not cost a second round trip.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/connect", {Reply{404, "{\"error\":\"host not found\"}"}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    check("a 404 is not treated as a missing observation",
          !directory_session_open(request, &session, &error), error);
    check("...and is asked once", dir.Count("/api/connect") == 1,
          std::to_string(dir.Count("/api/connect")));
    dir.Stop();
  }

  // ------------------------------------------------------------- the host's retry, end to end
  //
  // Through HostAgent itself, not a piece of it: register, observe, heartbeat, and the 409 that
  // arrives in the middle. Two things must be true and only one of them is about the outcome.
  //
  // The heartbeat has to be sent twice -- once refused, once accepted -- and the host must NOT
  // register again. A 401 clears the cached token and re-registers, which is right for a token
  // the server has forgotten and wrong for this: nothing is wrong with the token, and dropping it
  // would lose the one thing that lets an unattended PC come back without someone walking to it.
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'b') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{409, "{\"error\":\"observation_required\"}"},
                                       Reply{200, "{\"ok\":true}"}});
    RunHost(dir, "409-then-200", 2, 1, false, dir.udpPort());

    check("the host heartbeats again after a 409", dir.Count("/api/host/heartbeat") == 2,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...and does not register again, so the cached token survives",
          dir.Count("/api/host/register") == 1,
          std::to_string(dir.Count("/api/host/register")));
    check("...and it observed more than once", dir.ObserveProbes() >= 2,
          std::to_string(dir.ObserveProbes()));
    dir.Stop();
  }

  {
    // The contrast that gives the assertion above its meaning: 401 IS the token being gone, and
    // it does re-register. If both statuses took the same path, the test above would pass for a
    // client that treated every refusal as a lost token.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'b') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{401, "{\"error\":\"unknown host token\"}"},
                                       Reply{200, "{\"ok\":true}"}});
    RunHost(dir, "401", 1, 2, false, dir.udpPort());

    check("a 401 does register again", dir.Count("/api/host/register") >= 2,
          std::to_string(dir.Count("/api/host/register")));
    dir.Stop();
  }

  // ---------------------------------------- a port that is not a port must not become one
  //
  // The reply to the address probe is the host's own public port: it is what gets published, and
  // it decides whether anyone can reach this machine. The reader here took the digits in front of
  // a '.' and had no upper bound -- only zero was refused -- so 65537 became **1** on the way
  // through uint16_t. That is not a value anything downstream rejects. It is a plausible port,
  // and a host nobody can reach looks like a network fault rather than a parse.
  //
  // Asserted through the status line, which is where the observation surfaces: no "public=" means
  // nothing was published, which is the correct outcome for every one of these.
  {
    const char* bad[] = {
        "{\"ip\":\"1.2.3.4\",\"port\":65537}",    // truncated to 1 before
        "{\"ip\":\"1.2.3.4\",\"port\":65536}",    // one past the top
        "{\"ip\":\"1.2.3.4\",\"port\":0}",        // refused before too
        "{\"ip\":\"1.2.3.4\",\"port\":29181.5}",  // read as 29181 before
        "{\"ip\":\"1.2.3.4\",\"port\":-1}",
        "{\"ip\":\"1.2.3.4\",\"port\":\"29181\"}",
        "{\"ip\":\"1.2.3.4\",\"port\":99999999999999}",
        "{\"ip\":\"\",\"port\":29181}",           // an address that is not one
    };
    for (const char* reply : bad) {
      FakeDirectory dir;
      dir.Start();
      dir.ObserveReply(reply);
      dir.Script("/api/host/register",
                 {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'c') + "\"}"}});
      dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
      const std::string status = RunHost(dir, "bad-port", 1, 1, false, dir.udpPort());
      check((std::string("nothing is published for ") + reply).c_str(),
            status.find("public=") == std::string::npos, status);
      dir.Stop();
    }
  }

  {
    // The boundary that must still work. A rule that refuses 65535 as well would be a different
    // defect wearing the same fix.
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\"ip\":\"1.2.3.4\",\"port\":65535}");
    dir.Script("/api/host/register",
               {Reply{200, "{\"hostId\":\"h1\",\"hostToken\":\"" + std::string(32, 'd') + "\"}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    const std::string status = RunHost(dir, "port-65535", 1, 1, false, dir.udpPort());
    check("65535 is a port and is published", status.find("public=1.2.3.4:65535") != std::string::npos,
          status);
    dir.Stop();
  }

  // ---------------------------------------- and the same reply on the viewer's path
  //
  // The viewer and the phone share one parser (directory_rendezvous.cpp), so this covers both.
  // A refused reply must leave the probe unanswered rather than dialling a number it invented.
  {
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\"ip\":\"1.2.3.4\",\"port\":65537}");
    dir.Script("/api/connect", {Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    const bool opened = directory_session_open(request, &session, &error);
    check("the viewer refuses an out-of-range observed port", !opened, error);
    check("...and never asks to connect on it", dir.Count("/api/connect") == 0,
          std::to_string(dir.Count("/api/connect")));
    dir.Stop();
  }

  {
    FakeDirectory dir;
    dir.Start();
    dir.ObserveReply("{\"ip\":\"1.2.3.4\",\"port\":29181.5}");
    dir.Script("/api/connect", {Reply{200, candidateBody()}});

    DirectorySessionRequest request{};
    request.url = dir.url();
    request.sessionToken = "session";
    request.hostId = "host";
    request.directoryUdpPort = dir.udpPort();
    request.punchBudgetMs = 300;
    DirectorySessionResult session{};
    std::string error;
    check("a fractional observed port is not a port on the viewer's side either",
          !directory_session_open(request, &session, &error), error);
    dir.Stop();
  }

  // ------------------------------------ a host that resumed from a cache still has to be told
  //
  // The field defect, reproduced without TLS. EnsureRegistered() returns immediately when a token
  // is cached, and the advertisement only ever arrived on the registration response -- so a host
  // that had registered successfully once never learned where observations go again. On https
  // there is no default to fall back to, so it could not observe, could not heartbeat, and never
  // appeared in anyone's list. The better the last run went, the more certainly the next one was
  // stuck.
  //
  // Here the fake directory's observe port is an ephemeral one the OS picked, which is NOT the
  // http port plus one. So the legacy default cannot reach it: a heartbeat proves the agent asked
  // the health route and used the answer.
  {
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" +
                                           std::to_string(dir.udpPort()) + "}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

    const std::string status = RunHost(dir, "cached-advertised", 1, 0, true);
    check("a cached host asks the health route", dir.Count("/healthz") >= 1,
          std::to_string(dir.Count("/healthz")));
    check("...and heartbeats using the advertised port",
          dir.Count("/api/host/heartbeat") >= 1,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...without registering again", dir.Count("/api/host/register") == 0,
          std::to_string(dir.Count("/api/host/register")));
    check("...and reports itself online", status.find("public=") != std::string::npos, status);
    dir.Stop();
  }

  {
    // The same, with a directory that says nothing. It must refuse rather than dial something it
    // made up -- and it must not sit in a loop asking. The count is the assertion: a poll would
    // show one request per cycle.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

    const std::string status = RunHost(dir, "cached-silent", 99, 99, true);
    check("a silent directory means nothing is published",
          status.find("public=") == std::string::npos, status);
    check("...and no heartbeat is sent", dir.Count("/api/host/heartbeat") == 0,
          std::to_string(dir.Count("/api/host/heartbeat")));
    check("...and the health route is asked, but not on a loop",
          dir.Count("/healthz") >= 1 && dir.Count("/healthz") <= 6,
          std::to_string(dir.Count("/healthz")));
    check("...and the reason is about the server, not a timeout",
          status.find("observations") != std::string::npos, status);
    dir.Stop();
  }

  {
    // A pinned port is the operator's decision and outranks the advertisement -- and the agent
    // must not ask the health route at all when it already has an answer.
    FakeDirectory dir;
    dir.Start();
    dir.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":9}}"}});
    dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
    dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

    const std::string status = RunHost(dir, "cached-pinned", 1, 0, true, dir.udpPort());
    check("a pinned port is used and the advertisement is not needed",
          dir.Count("/api/host/heartbeat") >= 1 && status.find("public=") != std::string::npos,
          status);
    check("...so the health route is not asked at all", dir.Count("/healthz") == 0,
          std::to_string(dir.Count("/healthz")));
    dir.Stop();
  }

  // ------------------------------------ asking must not cost anything where the old rule worked
  //
  // The health request is new, and new questions break old answers. On http the observe port has
  // always been derivable (httpPort + 1), and every currently deployed directory is laid out that
  // way -- so a health route that 500s, or that answers without saying anything, must leave that
  // path exactly as it was. If this pair ever goes red, the fix has made the common case worse
  // than the bug it repaired.
  {
    const struct {
      const char* label;
      const char* what;
      Reply health;
    } rows[] = {
        {"legacy-health-500", "a health route that fails",
         Reply{500, "{\"error\":\"no health route here\"}"}},
        {"legacy-health-silent", "a health route that says nothing",
         Reply{200, "{\"ok\":true}"}},
    };
    for (const auto& row : rows) {
      FakeDirectory dir;
      if (!dir.StartLegacyAdjacent()) {
        // Never silently skipped: an unavailable adjacent port would otherwise turn this into a
        // case that passes by not running.
        check((std::string(row.what) + ": could not lay out the fixture").c_str(), false,
              "the adjacent udp port was not free after 32 tries");
        continue;
      }
      dir.Script("/healthz", {row.health});
      dir.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true}"}});
      dir.Script("/api/host/register", {Reply{500, "{\"error\":\"must not be called\"}"}});

      const std::string status = RunHost(dir, row.label, 1, 0, true);
      check((std::string(row.what) + " leaves the http fallback working").c_str(),
            dir.Count("/api/host/heartbeat") >= 1,
            std::to_string(dir.Count("/api/host/heartbeat")));
      check("...and the observation really went to httpPort+1", dir.ObserveProbes() >= 1,
            std::to_string(dir.ObserveProbes()));
      check("...without registering again", dir.Count("/api/host/register") == 0,
            std::to_string(dir.Count("/api/host/register")));
      check("...and the address is published", status.find("public=") != std::string::npos,
            status);
      dir.Stop();
    }
  }

  WSACleanup();
  std::printf(gFailures == 0 ? "\nall retry checks passed\n" : "\n%d FAILED\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
