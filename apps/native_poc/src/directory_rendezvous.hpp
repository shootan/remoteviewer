#pragma once

// Client side of the hole punch.
//
// The address a peer must aim at is a property of one specific socket: NAT maps each
// (local address, local port) to its own external port, so the socket that asks the directory
// "how do I look from outside?" has to be the very socket the media will arrive on. That is
// why this owns the socket and hands it to the session afterwards, rather than letting the
// session open its own.
//
// The HTTP half of the exchange lives in the app: only these two steps need the socket.

#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

#include "native_socket.hpp"

namespace remote60::native_poc {

/** One address the directory says the host may be reachable at. */
struct RendezvousCandidate {
  std::string ip;
  uint16_t port = 0;
  std::string kind;  // "private" | "public" | "public-alt" | "relay"; "relay" is held back in an early race
};

/**
 * np-impl r1 (Codex NP design, contract 3-4): how the race treats the relay.
 *
 * relayCommitMs == 0 is the race as it always was: the first valid answer from any candidate wins,
 * and the server holds the relay's answer back by its own grace. That is the only mode for a server
 * that did not accept relayEarly (an older one, or a connect it did not grant it to).
 *
 * relayCommitMs > 0 is for a connect the server told 'accepted': the relay answers at once, so the
 * wait is ours. T0 is the instant just before the first punch leaves; a valid direct answer before
 * T0 + relayCommitMs wins even when the relay already answered; at T0 + relayCommitMs a relay answer
 * that has arrived is chosen; with none yet the race goes on to the full budget and takes whichever
 * valid answer comes next. Nothing is chosen twice, and nothing chosen is replaced by a later one.
 */
struct PunchRaceOptions {
  uint32_t relayCommitMs = 0;
};

/** What one race saw -- counts and timings only, never packet contents or any token. */
struct PunchRaceReport {
  struct Stranger {
    uint32_t ipv4NetworkOrder = 0;
    uint16_t port = 0;  // host order
    uint32_t datagrams = 0;
    uint32_t validPunches = 0;  // of those, how many had the exact Punch shape
    int64_t firstMs = -1;       // after T0
    uint32_t firstBytes = 0;
  };
  static constexpr size_t kMaxStrangers = 8;
  uint32_t rounds = 0;
  uint32_t candidatesResolved = 0;
  int64_t firstDirectMs = -1;   // first valid answer from a non-relay candidate, after T0
  int64_t firstRelayMs = -1;    // first valid answer from the relay candidate, after T0
  int64_t chosenMs = -1;        // when the choice was made, after T0
  uint32_t invalidFromCandidate = 0;  // datagrams from a candidate tuple that failed the shape check
  uint32_t strangerDatagrams = 0;     // from no candidate at all (all of them, also past the table)
  uint32_t strangersUntracked = 0;    // stranger tuples that did not fit the table
  std::vector<Stranger> strangers;    // first kMaxStrangers distinct tuples
  bool relayHeld = false;             // early mode: a relay answer was kept aside at some point
  std::string decision;               // "direct" | "relay" | "relay-after-commit" | "none" | "cancelled" | "error"
};

/** The exact shape a punch answer has (the host's and the relay's alike): 49 B, magic, size, version, kind. */
bool punch_answer_is_valid(const char* data, int n);

#if defined(REMOTE60_RENDEZVOUS_TEST_SEAM)
// TEST BUILDS ONLY (hello-delay-fix r2): stands in for PunchAny's recvfrom -- returns what recvfrom would and
// sets *err to the socket error on a failure. nullptr restores the real one. Never in the product.
using PunchRecvHookForTest = int (*)(SocketHandle s, char* buf, int len, sockaddr_in* from, int* fromLen, int* err);
void SetPunchRecvHookForTest(PunchRecvHookForTest hook);
// np-impl r1, TEST BUILDS ONLY: the clock PunchAny reads (microseconds on the steady clock's scale) and the
// receive timeout it asked for last, so the commit point can be tested on a scripted clock. nullptr restores it.
using PunchClockHookForTest = int64_t (*)();
void SetPunchClockHookForTest(PunchClockHookForTest hook);
uint32_t PunchLastRecvTimeoutMsForTest();
#endif

class DirectoryRendezvous {
 public:
  ~DirectoryRendezvous();
  DirectoryRendezvous(const DirectoryRendezvous&) = delete;
  DirectoryRendezvous& operator=(const DirectoryRendezvous&) = delete;
  DirectoryRendezvous() = default;

  /**
   * Opens the media socket and asks the directory what address it presents to the outside.
   * outObserved receives "ip:port" on success. Any previously opened socket is discarded.
   */
  /**
   * `stop` is checked between attempts. Optional and defaulted, so existing callers are
   * unchanged: the two that matter are the viewer, which now has something to cancel with,
   * and the tests, which do not. Without it these loops run their full budget -- six waits of
   * 300ms here, four seconds in PunchAny -- and a viewer the user has already replaced spends
   * all of it before it can notice.
   */
  bool Observe(const std::string& directoryHost, int directoryUdpPort,
               const std::string& observeToken, std::string* outObserved,
               std::string* outError, const std::atomic<bool>* stop = nullptr);

  /**
   * Sends punch packets at the host until it answers or the budget runs out.
   *
   * Success here means our NAT now has a mapping towards the host; it does not prove the
   * reverse, which the ordinary hello handshake establishes straight afterwards. A punch that
   * times out is still worth continuing from: many NATs let the hello through anyway.
   */
  bool Punch(const std::string& hostIp, int hostPort, uint32_t budgetMs, std::string* outError);

  /**
   * Punches every candidate at once and keeps whichever answers first.
   *
   * Trying them in turn would be wrong twice over. It multiplies the wait by the number of
   * candidates when the first ones are the blocked ones, and the whole reason a list exists is
   * that the client cannot know which of them its own network permits -- a company Wi-Fi blocks
   * the high port outbound, a residential ISP blocks the well-known one inbound, and the client
   * sits somewhere it cannot determine from the inside.
   *
   * All punches leave the same socket, so whichever address answers is already mapped for the
   * media that follows. `outChosen` receives the winner; on failure every candidate was tried
   * for the full budget.
   */
  bool PunchAny(const std::vector<RendezvousCandidate>& candidates, uint32_t budgetMs,
                RendezvousCandidate* outChosen, std::string* outError,
                const std::atomic<bool>* stop = nullptr);

  /**
   * The same race with the relay treatment of `options` and, when asked, a report of what it saw.
   * The form above is this one with default options (the legacy race) and no report.
   *
   * A reply is an answer only when it has the exact Punch shape (punch_answer_is_valid) and comes
   * from a candidate's exact tuple. Reachability, nothing more: the Hello that follows is what
   * authenticates the session, as before.
   */
  bool PunchAny(const std::vector<RendezvousCandidate>& candidates, uint32_t budgetMs,
                const PunchRaceOptions& options, RendezvousCandidate* outChosen,
                PunchRaceReport* outReport, std::string* outError,
                const std::atomic<bool>* stop = nullptr);

  /** Hands the socket to the caller, which becomes responsible for closing it. */
  SocketHandle Release();

  void Close();

 private:
  SocketHandle socket_ = kInvalidSocket;
};

}  // namespace remote60::native_poc
