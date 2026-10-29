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

// A receive result PunchAny waits through, decided HERE and not by the session's classify_udp_recv
// (hello-delay-fix r2, Codex N2): punching several candidates from one unconnected socket, a failed
// receive usually speaks about ONE of those paths, not about the socket -- and ending the whole punch on
// it gives up the candidates that would still answer. So: a datagram, an empty one and a timeout; and
// the per-datagram/path reports a UDP recvfrom documents -- Windows WSAECONNRESET (an earlier send's ICMP
// Port Unreachable), WSAENETRESET (its time to live expired), WSAEMSGSIZE (truncated); POSIX ECONNREFUSED
// (the ICMP report there). Anything else -- a socket that is not one, an invalid or shut-down socket, the
// network subsystem gone -- ends it: punching longer does not bring that back. Undocumented codes are not
// assumed to be per-path.
bool punch_receive_is_transient(int n, int err) {
  if (n >= 0) return true;
#if defined(REMOTE60_PUNCH_TEST_MUTANT_NETRESET_ENDS)
  if (err == WSAENETRESET) return false;  // TEST BUILD ONLY (punch_any mutant target): the r1 rule
#endif
#if defined(_WIN32)
  switch (err) {
    case WSAEWOULDBLOCK:
    case WSAETIMEDOUT:
    case WSAEINTR:
    case WSAECONNRESET:
    case WSAENETRESET:
    case WSAEMSGSIZE:
      return true;
    default:
      return false;
  }
#else
  switch (err) {
    case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case ETIMEDOUT:
    case EINTR:
    case ECONNREFUSED:
      return true;
    default:
      return false;
  }
#endif
}

#if defined(REMOTE60_RENDEZVOUS_TEST_SEAM)
// TEST BUILDS ONLY (punch_any_test): the receive PunchAny makes, so a test can hand it errors no loopback
// socket produces on demand (a TTL expiry). Never compiled into the product.
PunchRecvHookForTest gPunchRecvHook = nullptr;
// np-impl r1: the clock PunchAny reads, and the receive timeout it last asked for, so a test can run the
// commit-point boundaries (999 / 1000 / 1001 ms) on a scripted clock instead of the scheduler's.
PunchClockHookForTest gPunchClockHook = nullptr;
uint32_t gPunchLastRecvTimeoutMs = 0;
#endif

std::chrono::steady_clock::time_point punch_now() {
#if defined(REMOTE60_RENDEZVOUS_TEST_SEAM)
  if (gPunchClockHook) return std::chrono::steady_clock::time_point(std::chrono::microseconds(gPunchClockHook()));
#endif
  return std::chrono::steady_clock::now();
}

int punch_recvfrom(SocketHandle s, char* buf, int len, sockaddr_in* from, int* fromLen, int* err) {
#if defined(REMOTE60_RENDEZVOUS_TEST_SEAM)
  if (gPunchRecvHook) return gPunchRecvHook(s, buf, len, from, fromLen, err);
#endif
#if defined(_WIN32)
  int fl = *fromLen;
  const int n = recvfrom(s, buf, len, 0, reinterpret_cast<sockaddr*>(from), &fl);
#else
  socklen_t fl = static_cast<socklen_t>(*fromLen);
  const int n = static_cast<int>(recvfrom(s, buf, static_cast<size_t>(len), 0, reinterpret_cast<sockaddr*>(from), &fl));
#endif
  *fromLen = static_cast<int>(fl);
  *err = n < 0 ? socket_last_error() : 0;
  return n;
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

#if defined(REMOTE60_RENDEZVOUS_TEST_SEAM)
void SetPunchRecvHookForTest(PunchRecvHookForTest hook) { gPunchRecvHook = hook; }
void SetPunchClockHookForTest(PunchClockHookForTest hook) { gPunchClockHook = hook; }
uint32_t PunchLastRecvTimeoutMsForTest() { return gPunchLastRecvTimeoutMs; }
#endif

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

bool punch_answer_is_valid(const char* data, int n) {
  if (!data || n != static_cast<int>(sizeof(UdpHelloPacket))) return false;
  UdpHelloPacket p{};
  std::memcpy(&p, data, sizeof(p));
  return p.magic == kMagic && p.size == sizeof(UdpHelloPacket) && p.version >= kUdpProtocolVersion &&
         p.kind == static_cast<uint16_t>(UdpPacketKind::Punch);
}

bool DirectoryRendezvous::PunchAny(const std::vector<RendezvousCandidate>& candidates,
                                   uint32_t budgetMs, RendezvousCandidate* outChosen,
                                   std::string* outError,
                                   const std::atomic<bool>* stop) {
  return PunchAny(candidates, budgetMs, PunchRaceOptions{}, outChosen, nullptr, outError, stop);
}

bool DirectoryRendezvous::PunchAny(const std::vector<RendezvousCandidate>& candidates,
                                   uint32_t budgetMs, const PunchRaceOptions& options,
                                   RendezvousCandidate* outChosen, PunchRaceReport* outReport,
                                   std::string* outError,
                                   const std::atomic<bool>* stop) {
  PunchRaceReport localReport;
  PunchRaceReport& report = outReport ? *outReport : localReport;
  report = PunchRaceReport{};
  report.decision = "error";
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
  report.candidatesResolved = static_cast<uint32_t>(targets.size());
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
  // np-impl r1 (contract 3): T0 is taken just before the first punch leaves, and the commit point
  // and every reported time count from it.
  const auto t0 = punch_now();
  const auto deadline = t0 + std::chrono::milliseconds(budgetMs);
  const bool early = options.relayCommitMs > 0;
  const auto commitAt = t0 + std::chrono::milliseconds(options.relayCommitMs);
  const auto sinceT0 = [t0](Clock::time_point at) {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(at - t0).count());
  };
  int heldRelay = -1;  // early mode: index of the relay answer kept aside
  auto nextSend = t0;
  uint32_t readsThisRound = 0;
  char scratch[512];
  const auto stopped = [stop]() { return stop && stop->load(std::memory_order_acquire); };
  // The one place a choice is made, so it is made once, and only if nobody has asked to stop first
  // (contract 4: a cancel outranks any choice).
  const auto choose = [&](size_t i, const char* decision) {
    if (stopped()) {
      report.decision = "cancelled";
      if (outError) *outError = "cancelled";
      return false;
    }
    report.chosenMs = sinceT0(punch_now());
    report.decision = decision;
    if (outChosen) *outChosen = *resolved[i];
    return true;
  };
  const auto noteStranger = [&](const sockaddr_in& from, int n, bool validPunch, Clock::time_point at) {
    ++report.strangerDatagrams;
    for (auto& s : report.strangers) {
      if (s.ipv4NetworkOrder == from.sin_addr.s_addr && s.port == ntohs(from.sin_port)) {
        ++s.datagrams;
        if (validPunch) ++s.validPunches;
        return;
      }
    }
    if (report.strangers.size() >= PunchRaceReport::kMaxStrangers) {
      ++report.strangersUntracked;
      return;
    }
    PunchRaceReport::Stranger s;
    s.ipv4NetworkOrder = from.sin_addr.s_addr;
    s.port = ntohs(from.sin_port);
    s.datagrams = 1;
    s.validPunches = validPunch ? 1 : 0;
    s.firstMs = sinceT0(at);
    s.firstBytes = static_cast<uint32_t>(n);
    report.strangers.push_back(s);
  };
  for (;;) {
    const auto now = punch_now();
    // Checked before every send and every wait, and no wait is longer than one interval: a cancel
    // is noticed as soon as it was before.
    if (stopped()) {
      report.decision = "cancelled";
      if (outError) *outError = "cancelled";
      return false;
    }
    // np-impl r1 (contract 4): at the commit point a relay answer already in hand is taken. Before it,
    // a direct answer still wins over it; with none in hand the race simply goes on.
    if (heldRelay >= 0 && now >= commitAt) return choose(static_cast<size_t>(heldRelay), "relay");
    if (now >= deadline) break;
    if (now >= nextSend) {
      // One round hits every candidate before waiting, so a reply from any of them is caught by
      // the same wait rather than after the others have had their turn.
      for (const sockaddr_in& target : targets) {
        (void)sendto(socket_, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
                     reinterpret_cast<const sockaddr*>(&target), sizeof(target));
      }
      ++report.rounds;
      // From now, not from the missed slot: a late round never turns into a burst.
      nextSend = now + std::chrono::milliseconds(kPunchIntervalMs);
      readsThisRound = 0;
    }
    // The wait ends at the next round, the budget -- and, while a relay answer is held, the commit
    // point, so the choice is made on time rather than up to one interval late.
    auto waitUntil = (std::min)(nextSend, deadline);
    if (heldRelay >= 0 && commitAt < waitUntil) waitUntil = commitAt;
    if (readsThisRound >= kPunchReadsPerRound) {
      // A round's worth of receives already came back at once: wait out the rest of it rather
      // than spin (in steps, so a stop is still seen within one).
      const auto pause = (std::min)(waitUntil - punch_now(), std::chrono::duration_cast<Clock::duration>(
                                                                  std::chrono::milliseconds(kPunchPauseStepMs)));
      if (pause > Clock::duration::zero()) std::this_thread::sleep_for(pause);
      continue;
    }
    const auto waitMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil - punch_now()).count();
    const uint32_t recvTimeoutMs = static_cast<uint32_t>((std::max)(static_cast<long long>(1), waitMs));
    (void)set_recv_timeout(socket_, recvTimeoutMs);
#if defined(REMOTE60_RENDEZVOUS_TEST_SEAM)
    gPunchLastRecvTimeoutMs = recvTimeoutMs;
#endif

    sockaddr_in from{};
    int fromLen = static_cast<int>(sizeof(from));
    int err = 0;
    const int n = punch_recvfrom(socket_, scratch, static_cast<int>(sizeof(scratch)), &from, &fromLen, &err);
    ++readsThisRound;
    if (n > 0) {
      const auto at = punch_now();
      // The commit point comes first: an answer read at or after it, with a relay answer already held,
      // does not undo the commit -- "before T0 + relayCommitMs" is strict (contract 3-4).
      if (heldRelay >= 0 && at >= commitAt) return choose(static_cast<size_t>(heldRelay), "relay");
      const bool valid = punch_answer_is_valid(scratch, n);
      size_t match = targets.size();
      for (size_t i = 0; i < targets.size(); ++i) {
        if (from.sin_addr.s_addr == targets[i].sin_addr.s_addr && from.sin_port == targets[i].sin_port) {
          match = i;
          break;
        }
      }
      if (match == targets.size()) {
        // np-impl r1 (contract 1): not from a candidate. Counted, never answered, never chosen.
        noteStranger(from, n, valid, at);
        continue;
      }
      if (!valid) {
        // From a candidate, but not a punch answer: garbage, a stray, a wrong version -- not "ready".
        ++report.invalidFromCandidate;
        continue;
      }
      const bool isRelay = resolved[match]->kind == "relay";
      if (isRelay) {
        if (report.firstRelayMs < 0) report.firstRelayMs = sinceT0(at);
        if (early && at < commitAt) {
          // Kept aside: a direct answer before the commit point still wins over it.
          if (heldRelay < 0) heldRelay = static_cast<int>(match);
          report.relayHeld = true;
          continue;
        }
        return choose(match, early ? "relay-after-commit" : "relay");
      }
      if (report.firstDirectMs < 0) report.firstDirectMs = sinceT0(at);
      return choose(match, "direct");
    }
    if (!punch_receive_is_transient(n, err)) {
      // A socket that cannot receive at all does not come back by punching faster or longer.
      if (outError) *outError = "punch receive failed (error " + std::to_string(err) + ")";
      return false;
    }
  }

  // The budget ran out. A relay answer held in early mode cannot be here (the commit point lies inside
  // the budget whenever relayCommitMs < budgetMs; when it does not, a held answer is still taken now).
  if (heldRelay >= 0) return choose(static_cast<size_t>(heldRelay), "relay");
  report.decision = "none";
  if (outError) *outError = "no candidate answered";
  return false;
}

}  // namespace remote60::native_poc
