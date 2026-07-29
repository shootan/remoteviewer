#pragma once

// Directory-service client for the host.
//
// The host announces itself to a small rendezvous server so a phone can find it by name
// instead of by IP, and so the two can meet even when neither side accepts inbound
// connections. Everything here is out-of-band bookkeeping: once the two peers have each
// other's public address the existing UDP media protocol runs exactly as before.
//
// The protocol and the staging plan live in the account/host-registration/hole-punching
// design note under docs/.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace remote60::native_poc::directory {

struct HostAgentConfig {
  // Base URL of the directory service, e.g. http://example.org:8080. Only http:// is
  // understood today; https:// is rejected rather than silently downgraded.
  std::string url;
  std::string accountId;
  // Used once to obtain a host token. Never written to disk and never logged.
  std::string password;
  std::string hostName;
  // Where the host token is cached. Empty means default_host_cache_path().
  std::string cachePath;
  // Port of the UDP address-observation endpoint. 0 means the HTTP port + 1, which is the
  // server's own default relationship between the two.
  uint16_t observeUdpPort = 0;
  uint32_t heartbeatSeconds = 25;
};

/** %LOCALAPPDATA%\remote60\host.json */
std::string default_host_cache_path();

/** Stable per-machine identifier, so reinstalling does not create a duplicate host entry. */
std::string machine_id();

/**
 * Keeps the host registered and reachable.
 *
 * Runs one background thread that registers, refreshes its public address, heartbeats, and
 * performs the outbound UDP punch when a client asks to connect. It never owns a socket: all
 * UDP goes through the media socket supplied by the caller, because the address the directory
 * observes must be the address the media stream will actually arrive on.
 */
class HostAgent {
 public:
  using SendFn = std::function<void(const void* data, size_t len, const sockaddr_in& to)>;

  HostAgent() = default;
  ~HostAgent();
  HostAgent(const HostAgent&) = delete;
  HostAgent& operator=(const HostAgent&) = delete;

  bool Start(const HostAgentConfig& cfg, SendFn send, std::string* outError);
  void Stop();

  /**
   * Offer a UDP datagram that was not recognised by the media protocol.
   * Returns true when it belonged to the directory flow and was consumed.
   */
  bool ConsumeUdpPacket(const void* data, size_t len, const sockaddr_in& from);

  /** Human-readable one-liner for status output; safe to call from any thread. */
  std::string StatusLine() const;

 private:
  struct PunchTarget {
    uint32_t ipv4NetworkOrder = 0;
    uint16_t port = 0;
  };

  void Run();
  bool EnsureRegistered();
  bool RefreshObservedAddress();
  bool Heartbeat(std::vector<PunchTarget>* outPunch);
  void Punch(const std::vector<PunchTarget>& targets);
  void SetStatus(const std::string& status);

  bool HttpPostJson(const std::string& path, const std::string& body, uint32_t* outStatus,
                    std::string* outResponse);

  bool LoadCache();
  void SaveCache() const;

  HostAgentConfig cfg_;
  SendFn send_;

  std::string httpHost_;
  uint16_t httpPort_ = 0;
  sockaddr_in observeAddr_{};

  std::string machineId_;
  std::string hostId_;
  std::string hostToken_;
  std::string observeToken_;

  std::thread thread_;
  std::atomic<bool> running_{false};

  mutable std::mutex mu_;
  std::string status_ = "idle";
  bool observedReady_ = false;
  std::string observedIp_;
  uint16_t observedPort_ = 0;
};

}  // namespace remote60::native_poc::directory
