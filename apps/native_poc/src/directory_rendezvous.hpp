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
#include <string>
#include <vector>

#include "native_socket.hpp"

namespace remote60::native_poc {

/** One address the directory says the host may be reachable at. */
struct RendezvousCandidate {
  std::string ip;
  uint16_t port = 0;
  std::string kind;  // "private" | "public" | "public-alt"; for logging only
};

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
  bool Observe(const std::string& directoryHost, int directoryUdpPort,
               const std::string& observeToken, std::string* outObserved, std::string* outError);

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
                RendezvousCandidate* outChosen, std::string* outError);

  /** Hands the socket to the caller, which becomes responsible for closing it. */
  SocketHandle Release();

  void Close();

 private:
  SocketHandle socket_ = kInvalidSocket;
};

}  // namespace remote60::native_poc
