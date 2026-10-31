#pragma once

#include "directory_observe.hpp"
#include "update_endpoint.hpp"

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
  // Base URL of the directory service, e.g. http://example.org:8080 or https://example.org.
  // The scheme picks the transport and the default port, and it is decided once, by
  // parse_directory_url.
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

/**
 * Splits http://host[:port] or https://host[:port] into its parts.
 *
 * `outSecure` is how the caller learns which one it was, and it is the only place that answer
 * should come from: whoever dials has to use the same answer the parser used to pick the default
 * port, or the two disagree about a url neither of them rejected. The scheme is compared without
 * case, which is what a scheme is -- `HTTP://` used to be read as no scheme at all.
 *
 * The default port follows the scheme: 80 for http, 443 for https.
 */
bool parse_directory_url(const std::string& url, std::string* outHost, uint16_t* outPort,
                         std::string* outError, bool* outSecure = nullptr);

/**
 * "scheme://host:port" for deciding whether two spellings mean the same server.
 *
 * Whether a cached host token still belongs where it was issued used to be a string comparison
 * against the url as typed. So `http://rem.example:8080/` and `http://rem.example:8080` were two
 * servers, and so were `http://rem.example` and `http://rem.example:80` -- and each mismatch
 * threw the token away and re-registered. Nothing was unsafe about it; it just made a trailing
 * slash cost an unattended PC its cached credentials.
 *
 * The port is always written out and the host is lowercased, so the defaults and the case cannot
 * make two names of one server look different. The scheme stays in it: http and https are not the
 * same origin, and a token issued to one has no business being sent to the other.
 *
 * This is for comparison only. What the user typed is what stays stored and shown -- rewriting
 * that under them is a different kind of surprise.
 */
std::string directory_origin_key(const std::string& url);

/**
 * Where update manifests live, derived from the directory the user already signed in to.
 *
 * Until now the manifest url only came from an environment variable, so a machine configured with
 * nothing but a server address could never check for updates -- the address was there, the route
 * was there, and nothing joined them.
 *
 * Built from the origin, not by pasting strings: `directory_origin_key()` decides the scheme,
 * host and port so this cannot disagree with the client about which server it is, and the path
 * and the platform query are appended whole. The server hands back the document and its detached
 * signature in one response, so there is no second `.sig` request to get wrong.
 *
 * Empty when there is nothing to derive from, and **empty for an http directory**. This is not a
 * place to guess: upgrading the caller to https on its behalf would invent a server nobody
 * configured, and following the directory down to http would fetch an administrator-privileged
 * artifact over a hop anyone can rewrite. An http deployment that wants updates says so with an
 * explicit https override.
 */
std::string directory_update_manifest_url(const std::string& directoryUrl,
                                          const std::string& platform);

/**
 * Builds the snapshot an update fetch runs from (see update_endpoint.hpp).
 *
 * The override wins and gets no credential; otherwise the url is derived from the directory and
 * carries one. `credentialHeader` is what this process holds -- `x-host-token: ...` on a host, an
 * `Authorization: Bearer ...` session elsewhere. Passing an empty one is normal: a client that is
 * not signed in has nothing to send, and the server will say so with a 401 that means "the update
 * check failed", not "sign in again".
 */
update::UpdateEndpoint update_endpoint_for(const std::string& override_,
                                           const std::string& directoryUrl,
                                           const std::string& platform,
                                           const std::string& credentialHeader = {},
                                           const std::string& ownerKey = {},
                                           uint64_t ownerEpoch = 0);

/**
 * The explicit override when there is one, otherwise the derived url.
 *
 * The override keeps working exactly as before -- a deployment that set it is not moved onto a
 * different server by this change.
 */
std::string update_manifest_url_for(const std::string& override_, const std::string& directoryUrl,
                                    const std::string& platform);

/**
 * One POST, one connection, read to EOF.
 *
 * `extraHeaders` is appended verbatim and must already be CRLF terminated. Returns false when the
 * exchange did not complete at all; a completed exchange reports the server's status instead, so
 * a caller can tell "could not reach it" from "it said no".
 */
/**
 * Asks a directory where address observations go, over its health route.
 *
 * Needs no session, which is the point: a host that resumed from a cached token never registered
 * and so never saw the response that carries the advertisement. Returns false when the server
 * could not be reached (`outError` says why) and also when it simply said nothing -- absence is a
 * documented state, not a failure, and the caller tells them apart by whether `outError` is set.
 */
bool observe_endpoint_from_health(const std::string& url, ObserveEndpoint* out,
                                  std::string* outError);

/** A GET over the same two transports as http_post. */
bool http_get(const std::string& host, uint16_t port, bool secure, const std::string& path,
              uint32_t* outStatus, std::string* outResponse);

bool http_post(const std::string& host, uint16_t port, bool secure, const std::string& path,
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
  /**
   * Gets the observe endpoint from the directory when registration did not supply one.
   *
   * A host that starts with a cached token never registers, so it never saw the login response
   * that carries the advertisement -- and on https there is no default to fall back to, so it
   * could not observe, could not heartbeat, and never appeared in anyone's list. The cache
   * holding a perfectly good token was what made this permanent: the more successful the last
   * run, the more certainly the next one was stuck.
   *
   * Asked over the same origin's health route, which needs no session. Bounded: a server that
   * does not advertise yet may start later, so this retries with backoff and then stops asking.
   *
   * The cooldown counts CYCLES of Run(), not seconds: 2, 4, 6 ... at the default heartbeat
   * interval of 25 s. Writing it as "2, 4, 6" without the unit reads like seconds and is out by
   * more than an order of magnitude.
   *
   * WARNING: LIMIT, stated rather than implied: after kMaxFetchAttempts the asking stops for the rest of
   * this process's life. A directory that is given an observe port LATER will not be picked up,
   * and the host has to be restarted to see it. That is a deliberate bound -- an omission on the
   * server should not turn into a machine that talks to it forever -- but it is not permanent
   * self-recovery, and it should not be described as such.
   */
  bool FetchObserveEndpointFromHealth();
  bool Heartbeat(std::vector<PunchTarget>* outPunch);
  // One attempt, reporting the status and the server's error name so the caller can tell a
  // missing observation apart from every other refusal. Heartbeat() is the policy on top.
  bool HeartbeatAttempt(std::vector<PunchTarget>* outPunch, uint32_t* outStatus,
                        std::string* outServerError);
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
  /** How many times the health route has been asked, so the asking is bounded. */
  int observeFetchAttempts_ = 0;
  /** Cycles left to wait before asking again. Grows, so a silent server is not polled forever. */
  int observeFetchCooldown_ = 0;
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
