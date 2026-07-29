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

#include "native_socket.hpp"

namespace remote60::native_poc {

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

  /** Hands the socket to the caller, which becomes responsible for closing it. */
  SocketHandle Release();

  void Close();

 private:
  SocketHandle socket_ = kInvalidSocket;
};

}  // namespace remote60::native_poc
