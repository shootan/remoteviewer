#include "directory_rendezvous.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "poc_protocol.hpp"

namespace remote60::native_poc {
namespace {

// hello-delay-fix r1: how many receives one punch round may take before it waits out the rest of the
// round, and the step of that wait -- the bound on a loop fed by errors that come back at once.
constexpr uint32_t kPunchReadsPerRound = 256;
constexpr uint32_t kPunchPauseStepMs = 25;

// A receive result PunchAny can wait through: a datagram, an empty one, a timeout, and on Windows the
// per-datagram truncation and the ICMP-driven reset an earlier punch to an unreachable candidate earns
// (classify_udp_recv). POSIX reports that ICMP as ECONNREFUSED, which is not terminal here either.
bool punch_receive_is_transient(int n, int err) {
  switch (classify_udp_recv(n, err)) {
    case UdpRecvClass::Terminal:
#if !defined(_WIN32)
      return err == ECONNREFUSED;
#else
      return false;
#endif
    default:
      return true;
  }
}

constexpr uint32_t kObserveAttempts = 6;
constexpr uint32_t kObserveWaitMs = 300;
constexpr uint32_t kPunchIntervalMs = 150;

bool resolve_udp(const std::string& host, int port, sockaddr_in* out) {
  addrinfo* results = nullptr;
  std::string error;
  if (!resolve_endpoint(host, port, SOCK_DGRAM, IPPROTO_UDP, &results, &error)) return false;
  bool found = false;
  for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
    if (it->ai_family != AF_INET) continue;
    std::memcpy(out, it->ai_addr, sizeof(sockaddr_in));
    found = true;
    break;
  }
  freeaddrinfo(results);
  return found;
}

/** Extracts a value from the directory's tiny {"ip":"..","port":N} reply. */
bool parse_observed(const std::string& json, std::string* outIp, int* outPort) {
  const size_t ipKey = json.find("\"ip\"");
  const size_t portKey = json.find("\"port\"");
  if (ipKey == std::string::npos || portKey == std::string::npos) return false;
  const size_t ipStart = json.find('"', json.find(':', ipKey) + 1);
  if (ipStart == std::string::npos) return false;
  const size_t ipEnd = json.find('"', ipStart + 1);
  if (ipEnd == std::string::npos) return false;
  *outIp = json.substr(ipStart + 1, ipEnd - ipStart - 1);

  size_t cursor = json.find(':', portKey);
  if (cursor == std::string::npos) return false;
  ++cursor;
  while (cursor < json.size() && (json[cursor] == ' ' || json[cursor] == '\t')) ++cursor;
  const size_t digitsBegin = cursor;
  long port = 0;
  while (cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9') {
    port = port * 10 + (json[cursor] - '0');
    // Stop accumulating well before an int can wrap. Anything this long is already out of range,
    // and signed overflow would be undefined rather than merely wrong.
    if (port > 65535) return false;
    ++cursor;
  }
  if (cursor == digitsBegin) return false;
  // What follows has to end the value. Without this, "29181.5" read as 29181 -- digits taken from
  // in front of a '.' -- which is a plausible port that nothing downstream refuses.
  if (cursor < json.size()) {
    const char after = json[cursor];
    if (after != ',' && after != '}' && after != ' ' && after != '\t' && after != '\r' &&
        after != '\n') {
      return false;
    }
  }
  if (port <= 0 || port > 65535) return false;
  *outPort = static_cast<int>(port);
  return !outIp->empty();
}

}  // namespace

DirectoryRendezvous::~DirectoryRendezvous() { Close(); }

void DirectoryRendezvous::Close() { close_socket(&socket_); }

SocketHandle DirectoryRendezvous::Release() {
  const SocketHandle handle = socket_;
  socket_ = kInvalidSocket;
  return handle;
}

bool DirectoryRendezvous::Observe(const std::string& directoryHost, int directoryUdpPort,
                                  const std::string& observeToken, std::string* outObserved,
                                  std::string* outError,
                                  const std::atomic<bool>* stop) {
  Close();
  if (!initialize_sockets(outError)) return false;

  sockaddr_in directory{};
  if (!resolve_udp(directoryHost, directoryUdpPort, &directory)) {
    // Named, because the observe endpoint is no longer always the directory's own host and port:
    // the server can advertise a different one. "cannot resolve directory host" on its own sent
    // whoever read it to look at the url, which may be the one thing here that is correct.
    if (outError) {
      *outError = "cannot resolve directory observe endpoint '" + directoryHost + ":" +
                  std::to_string(directoryUdpPort) + "'";
    }
    return false;
  }

  SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == kInvalidSocket) {
    if (outError) *outError = "udp socket create failed";
    return false;
  }
  // Matches what the session would have set: a keyframe burst overruns a default-sized buffer.
  {
    int recvBuf = 4 * 1024 * 1024;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf),
                   sizeof(recvBuf)) != 0) {
      recvBuf = 1024 * 1024;
      (void)setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf),
                       sizeof(recvBuf));
    }
  }
  (void)set_recv_timeout(sock, kObserveWaitMs);

  const std::string probe = "OBSERVE " + observeToken;
  char reply[256];
  const auto stopped = [stop]() { return stop && stop->load(std::memory_order_acquire); };
  for (uint32_t attempt = 0; attempt < kObserveAttempts; ++attempt) {
    // Between attempts, not inside the wait: the wait is one 300ms recv timeout, so the
    // worst a cancel waits here is that, not the whole 1.8 seconds.
    if (stopped()) {
      // r5 T1: not handed over yet -- this call's to close (it is not socket_, so Close() never sees it).
      close_socket(&sock);
      if (outError) *outError = "cancelled";
      return false;
    }
    (void)sendto(sock, probe.data(), static_cast<int>(probe.size()), 0,
                 reinterpret_cast<const sockaddr*>(&directory), sizeof(directory));

    sockaddr_in from{};
#if defined(_WIN32)
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif
    const int n = recvfrom(sock, reply, sizeof(reply) - 1, 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n <= 0) continue;
    reply[n] = '\0';

    std::string ip;
    int port = 0;
    if (!parse_observed(std::string(reply, static_cast<size_t>(n)), &ip, &port)) continue;
    if (outObserved) *outObserved = ip + ":" + std::to_string(port);
    socket_ = sock;
    return true;
  }

  close_socket(&sock);
  if (outError) *outError = "directory did not answer the address probe";
  return false;
}

bool DirectoryRendezvous::Punch(const std::string& hostIp, int hostPort, uint32_t budgetMs,
                                std::string* outError) {
  if (socket_ == kInvalidSocket) {
    if (outError) *outError = "no prepared socket; observe first";
    return false;
  }

  sockaddr_in host{};
  if (!resolve_udp(hostIp, hostPort, &host)) {
    if (outError) *outError = "cannot resolve host address";
    return false;
  }

  (void)set_recv_timeout(socket_, kPunchIntervalMs);
  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
  char scratch[512];
  while (std::chrono::steady_clock::now() < deadline) {
    (void)sendto(socket_, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
                 reinterpret_cast<const sockaddr*>(&host), sizeof(host));

    sockaddr_in from{};
#if defined(_WIN32)
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif
    const int n = recvfrom(socket_, scratch, sizeof(scratch), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
    // Anything at all from the host proves the path is open in this direction.
    if (n > 0 && from.sin_addr.s_addr == host.sin_addr.s_addr && from.sin_port == host.sin_port) {
      return true;
    }
  }

  if (outError) *outError = "punch timed out";
  return false;
}

bool DirectoryRendezvous::PunchAny(const std::vector<RendezvousCandidate>& candidates,
                                   uint32_t budgetMs, RendezvousCandidate* outChosen,
                                   std::string* outError,
                                   const std::atomic<bool>* stop) {
  if (socket_ == kInvalidSocket) {
    if (outError) *outError = "no prepared socket; observe first";
    return false;
  }
  if (candidates.empty()) {
    if (outError) *outError = "no candidates";
    return false;
  }

  // Resolve once. A candidate we cannot resolve is dropped rather than failing the whole
  // attempt: one bad entry must not cost the others their chance.
  std::vector<sockaddr_in> targets;
  std::vector<const RendezvousCandidate*> resolved;
  for (const RendezvousCandidate& candidate : candidates) {
    sockaddr_in addr{};
    if (!resolve_udp(candidate.ip, candidate.port, &addr)) continue;
    targets.push_back(addr);
    resolved.push_back(&candidate);
  }
  if (targets.empty()) {
    if (outError) *outError = "no candidate address could be resolved";
    return false;
  }

  UdpHelloPacket packet{};
  packet.kind = static_cast<uint16_t>(UdpPacketKind::Punch);

  // hello-delay-fix r1: the send clock is separate from what the receive returns. A round goes out
  // at most once per kPunchIntervalMs, however the wait in between ends. Before, a receive that
  // failed AT ONCE -- on Windows the ICMP Port Unreachable an earlier punch to an unreachable
  // candidate earned comes back as WSAECONNRESET on the next recvfrom -- started the next round
  // immediately; that round's punches earned more of them, and the loop fed itself: ~290 rounds in
  // a few milliseconds were measured, and the replies and errors it left queued on the socket then
  // held up the hello's ack for seconds (reconnect-hello-delay r1). An answer from a candidate is
  // still taken the moment it arrives.
  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::milliseconds(budgetMs);
  auto nextSend = Clock::now();
  uint32_t readsThisRound = 0;
  char scratch[512];
  const auto stopped = [stop]() { return stop && stop->load(std::memory_order_acquire); };
  for (;;) {
    const auto now = Clock::now();
    if (now >= deadline) break;
    // Checked before every send and every wait, and no wait is longer than one interval: a cancel
    // is noticed as soon as it was before.
    if (stopped()) {
      if (outError) *outError = "cancelled";
      return false;
    }
    if (now >= nextSend) {
      // One round hits every candidate before waiting, so a reply from any of them is caught by
      // the same wait rather than after the others have had their turn.
      for (const sockaddr_in& target : targets) {
        (void)sendto(socket_, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
                     reinterpret_cast<const sockaddr*>(&target), sizeof(target));
      }
      // From now, not from the missed slot: a late round never turns into a burst.
      nextSend = now + std::chrono::milliseconds(kPunchIntervalMs);
      readsThisRound = 0;
    }
    const auto waitUntil = (std::min)(nextSend, deadline);
    if (readsThisRound >= kPunchReadsPerRound) {
      // A round's worth of receives already came back at once: wait out the rest of it rather
      // than spin (in steps, so a stop is still seen within one).
      const auto pause = (std::min)(waitUntil - Clock::now(), std::chrono::duration_cast<Clock::duration>(
                                                                  std::chrono::milliseconds(kPunchPauseStepMs)));
      if (pause > Clock::duration::zero()) std::this_thread::sleep_for(pause);
      continue;
    }
    const auto waitMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil - Clock::now()).count();
    (void)set_recv_timeout(socket_, static_cast<uint32_t>((std::max)(static_cast<long long>(1), waitMs)));

    sockaddr_in from{};
#if defined(_WIN32)
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif
    const int n = recvfrom(socket_, scratch, sizeof(scratch), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
    const int err = n < 0 ? socket_last_error() : 0;
    ++readsThisRound;
    if (n > 0) {
      for (size_t i = 0; i < targets.size(); ++i) {
        if (from.sin_addr.s_addr != targets[i].sin_addr.s_addr ||
            from.sin_port != targets[i].sin_port) {
          continue;
        }
        if (outChosen) *outChosen = *resolved[i];
        return true;
      }
      continue;  // not from a candidate: keep waiting for this round
    }
    if (!punch_receive_is_transient(n, err)) {
      // A socket that cannot receive at all does not come back by punching faster or longer.
      if (outError) *outError = "punch receive failed (error " + std::to_string(err) + ")";
      return false;
    }
  }

  if (outError) *outError = "no candidate answered";
  return false;
}

}  // namespace remote60::native_poc
