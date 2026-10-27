#pragma once

// keepalive-r2 ② (c, e): a standby lease's capability from the directory, through the LEASE'S OWN socket -- the product
// form of what the e2e first did by hand. The relay binds a standby lease to the endpoint its observation came from
// (contract §2.7), so the observation leaves from the lease's socket; then a standby connect; then the address to aim
// at: the relay's when this connect reserved one, else the host's -- the first private candidate (the LAN), else the
// first one listed. (Punching across NATs for the direct path is not done here: the lease's datagrams go straight to the
// address; a path that needs a punch is not reached and the lease stays unadmitted -- bounded, never "ready".)

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "directory_session_client.hpp"
#include "native_socket.hpp"
#include "standby_lease.hpp"

namespace remote60::native_poc {

// One observation from socket `s`: "OBSERVE <token>" to the directory's observe endpoint until an answer (the same
// datagram the rendezvous sends, on this socket). true = the directory recorded this endpoint for `token`.
inline bool standby_observe_on(SocketHandle s, const std::string& host, uint16_t port, const std::string& token) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &to.sin_addr) != 1) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    to.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
  }
  const std::string probe = "OBSERVE " + token;
  for (int attempt = 0; attempt < 6; ++attempt) {
    sendto(s, probe.data(), static_cast<int>(probe.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < until) {
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(s, &rd);
      timeval tv{0, 50000};
      if (select(static_cast<int>(s) + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
      char buf[512];
      sockaddr_in from{};
      socklen_t fl = sizeof(from);
      const int n = static_cast<int>(recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl));
      if (n > 0 && from.sin_addr.s_addr == to.sin_addr.s_addr && from.sin_port == to.sin_port) return true;
    }
  }
  return false;
}

struct StandbyDirectoryConfig {
  std::string url;                          // the directory (https in the product)
  std::function<std::string()> session;     // the current session token (it may be renewed)
  std::string hostId;
  std::string observeHost;                  // where observations go (the login / health metadata)
  uint16_t observePort = 0;
  std::function<std::string()> freshToken;  // a fresh observe token per attempt (random)
  // what the last attempt said, for the caller's log / UI: "reserved", a refusal reason, "observe failed", ...
  std::function<void(const std::string&)> note;
};

// The CapabilityFn of a lease for `cfg.hostId`.
inline StandbyLease::CapabilityFn standby_directory_capability(StandbyDirectoryConfig cfg) {
  return [cfg](SocketHandle s, StandbyCapability* out) {
    auto say = [&](const std::string& what) {
      if (cfg.note) cfg.note(what);
    };
    const std::string token = cfg.freshToken ? cfg.freshToken() : std::string();
    if (token.empty() || cfg.observePort == 0) {
      say("no observe endpoint");
      return false;
    }
    if (!standby_observe_on(s, cfg.observeHost, cfg.observePort, token)) {
      say("observe failed");
      return false;
    }
    DirectoryConnectTarget t;
    std::string error;
    uint32_t status = 0;
    if (!directory_connect(cfg.url, cfg.session ? cfg.session() : std::string(), cfg.hostId, token, &t, &error, &status,
                           /*standby=*/true)) {
      say("connect refused: " + error + " (" + std::to_string(status) + ")");
      return false;
    }
    out->token = t.punchToken;
    if (t.standbyRelay == "reserved" && t.relayPort != 0) {
      out->ip = t.relayIp;
      out->port = t.relayPort;
      say("reserved");
      return true;
    }
    // no standby relay for this connect (not eligible, relay off, a bound reached, an older server): the host itself
    const ConnectCandidate* pick = nullptr;
    for (const auto& c : t.candidates) {
      if (c.kind == CandidateKind::Private) {
        pick = &c;
        break;
      }
    }
    if (!pick && !t.candidates.empty()) pick = &t.candidates.front();
    if (!pick) {
      say("no candidate");
      return false;
    }
    out->ip = pick->ip;
    out->port = pick->port;
    say(t.standbyRelay.empty() ? "direct" : "direct (" + t.standbyRelay + ")");
    return true;
  };
}

}  // namespace remote60::native_poc
