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
#include <chrono>
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
  // Local port the shared media socket is bound to. Only used to notice that NAT did not
  // preserve it, which matters because a firewall-friendly bind port buys nothing once the
  // mapping lands somewhere else. 0 disables the check.
  uint16_t localUdpPort = 0;
  // A second port this host is also listening on, published so a client whose network filters
  // the first has something else to try. Zero when there is no second listener.
  uint16_t alternateUdpPort = 0;
  uint32_t heartbeatSeconds = 25;
};

/** %LOCALAPPDATA%\remote60\host.json */
std::string default_host_cache_path();

/** Stable per-machine identifier, so reinstalling does not create a duplicate host entry. */
std::string machine_id();

/**
 * What the host remembers between runs. The password is deliberately absent: a token can be
 * revoked from the server, a stored password cannot.
 */
struct HostCache {
  std::string directoryUrl;
  std::string accountId;
  std::string machineId;
  std::string hostName;
  std::string hostId;
  std::string hostToken;
};

bool load_host_cache(const std::string& path, HostCache* out);
bool save_host_cache(const std::string& path, const HostCache& cache);

/**
 * Exchanges an id and password for a host token. Also the only way to check credentials
 * without starting a session, which is what the sign-in window needs.
 */
/**
 * Where the server says address observations should be sent.
 *
 * The clients used to work this out themselves as httpPort + 1, which held only while the
 * directory was reached directly on its own two ports. Behind a TLS terminator on 443 that
 * becomes 444 -- nothing listens there, observation fails, and a host whose observation fails
 * skips its heartbeat and so never appears in the list at all. The relay cannot stand in for it
 * either: the relay address arrives in the /api/connect response, and the viewer gives up before
 * it ever calls connect.
 *
 * So the server tells us. `known` false means it did not, and the caller decides what that means
 * for the scheme it is using -- see observe_port_for().
 */
struct ObserveEndpoint {
  bool known = false;
  uint16_t port = 0;
  /** Empty means the directory host itself, which is the ordinary case. */
  std::string host;
  /**
   * True when a host WAS advertised and was thrown away for being unusable.
   *
   * Kept because the fallback is silent otherwise. The server does not validate this value -- it
   * trims it and sends it -- so a configuration slip arrives here looking like an address, and
   * dialling it produces a timeout that reads as a network fault. Falling back is right; falling
   * back without saying so leaves nobody able to find the actual mistake.
   */
  bool hostRejected = false;
};

/**
 * Whether an advertised observe host is safe to dial.
 *
 * A hostname or an IPv4 literal, and nothing else: no scheme, no port, no path, no spaces, no
 * control characters. Anything with those in it is a configuration mistake rather than an
 * address, and resolving it would either fail slowly or -- worse -- succeed against something
 * unintended.
 */
bool observe_host_is_usable(const std::string& host);

/**
 * Reads the optional `observe` metadata out of a login or register response.
 *
 * Returns false when the server said nothing, or said something unusable. A port outside
 * 1..65535 is treated as absent rather than clamped: a wrong port is worse than no port, because
 * no port leaves the caller on a documented fallback while a wrong one sends it somewhere that
 * will never answer and looks like a network fault.
 */
bool parse_observe_metadata(const std::string& json, ObserveEndpoint* out);

/**
 * The port to send observations to, or 0 when there is no safe answer.
 *
 * `secure` is whether the directory URL is https. The rules differ by scheme on purpose:
 *
 *   * The server said so -> use it, whatever the scheme.
 *   * Plain http and no metadata -> httpPort + 1, which is what every existing deployment does
 *     and what an unchanged server still expects.
 *   * https and no metadata -> 0. NOT 443 + 1. That address is not a fallback, it is a guess
 *     that cannot be right, and dialling it turns "this server needs configuring" into a silent
 *     timeout. The caller reports it instead.
 */
uint16_t observe_port_for(const ObserveEndpoint& advertised, uint16_t httpPort, bool secure);

bool register_host(const std::string& url, const std::string& accountId,
                   const std::string& password, const std::string& hostName,
                   const std::string& machineId, std::string* outHostId,
                   std::string* outHostToken, std::string* outError,
                   ObserveEndpoint* outObserve = nullptr);

/**
 * Creates an account, so a user can choose their own id and password rather than asking the
 * person who runs the server. The signup key is what stops an internet-facing server from
 * accepting registrations from anyone.
 */
bool create_account(const std::string& url, const std::string& accountId,
                    const std::string& password, const std::string& signupKey,
                    std::string* outError);

/** Splits http://host[:port] into its parts; rejects https, which is not supported yet. */
bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError);

/**
 * One POST, one connection, read to EOF.
 *
 * `extraHeaders` is appended verbatim and must already be CRLF terminated. Returns false when the
 * exchange did not complete at all; a completed exchange reports the server's status instead, so
 * a caller can tell "could not reach it" from "it said no".
 */
bool http_post(const std::string& host, uint16_t port, const std::string& path,
               const std::string& contentType, const std::string& extraHeaders,
               const std::string& body, uint32_t* outStatus, std::string* outResponse);

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

  /** Consumes a one-time /api/connect capability; the observed endpoint is advisory across NAT. */
  bool AuthorizePeer(const std::string& punchToken, const sockaddr_in& from);

  /** Human-readable one-liner for status output; safe to call from any thread. */
  std::string StatusLine() const;

 private:
  struct PunchTarget {
    uint32_t ipv4NetworkOrder = 0;
    uint16_t port = 0;
    std::string punchToken;
  };

  struct AuthorizedPeer {
    PunchTarget target;
    std::chrono::steady_clock::time_point expiresAt;
  };

  void Run();
  bool EnsureRegistered();
  bool RefreshObservedAddress();
  /** Aims the observe socket from the configured port, or what the server advertised. */
  bool ApplyObserveEndpoint();
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
  /**
   * What the server last said about the observe endpoint, and whether the socket is aimed there.
   *
   * Registration happens before the first observation on every cycle, so the answer is available
   * before it is needed. It is re-applied rather than assumed: a cached registration that skipped
   * the exchange would otherwise leave the socket pointed wherever startup guessed.
   */
  ObserveEndpoint observeAdvertised_{};
  bool observeAddrReady_ = false;
  /** Whether the directory URL is https, which changes what an absent advertisement means. */
  bool httpSecure_ = false;

  std::string machineId_;
  std::string hostId_;
  std::string hostToken_;
  std::string observeToken_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  // A peer punch means /api/connect has just queued a capability for this host. Wake the
  // heartbeat loop instead of making the controller wait for the ordinary 25-second poll.
  std::atomic<bool> refreshRequested_{false};
  bool portRewriteReported_ = false;  // guarded by mu_
  // The advertised-address line is printed once per run; the heartbeat that carries it repeats
  // every 25 seconds and would otherwise bury the log it was added to make readable.
  bool announcedCandidates_ = false;

  mutable std::mutex mu_;
  std::string status_ = "idle";
  bool observedReady_ = false;
  std::string observedIp_;
  uint16_t observedPort_ = 0;
  std::vector<AuthorizedPeer> authorizedPeers_;
};

}  // namespace remote60::native_poc::directory
