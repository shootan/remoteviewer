#pragma once

// Everything between "the user picked a PC" and "there is a socket the media will arrive on".
//
// The steps have to happen in this order and on this socket, and getting either wrong produces a
// session that connects and then receives nothing:
//
//   1. open the media socket and ask the directory how it looks from outside (Observe)
//   2. tell the directory which host to reach, quoting that observation (connect)
//   3. punch every candidate it returned at once, keep whichever answers (PunchAny)
//
// Step 1 must use the socket the video will arrive on, because NAT maps each socket to its own
// external port. Step 3 must be a race rather than a sequence: the client cannot tell from the
// inside which of its own network's restrictions apply, and trying candidates in turn multiplies
// the wait by the number of dead ones ahead of the live one.
//
// The relay, when the server offers it, is simply the last candidate. It answers late on purpose,
// so a working direct path always wins -- which matters because relay traffic is billed.

#include <cstdint>
#include <string>

#include "connect_candidates.hpp"
#include "native_socket.hpp"

namespace remote60::native_poc {

struct DirectorySessionRequest {
  std::string url;              // http://host[:port] of the directory
  std::string sessionToken;     // from directory_login
  std::string hostId;
  uint16_t directoryUdpPort = 8081;
  // The client gives up on the handshake soon after this, so a longer budget buys nothing.
  uint32_t punchBudgetMs = 4000;
};

struct DirectorySessionResult {
  SocketHandle socket = kInvalidSocket;  // prepared and unconnected; the caller owns it
  ConnectCandidate chosen;
  // False when no candidate answered and `chosen` is the first one tried anyway. Worth
  // surfacing: some NATs pass the hello even after dropping the punch, so this is a warning
  // rather than a failure, and it explains a slow start when it happens.
  bool answered = false;
  std::string punchToken;
};

/**
 * Runs the three steps and hands back a socket ready for the hello handshake.
 *
 * On failure nothing is left open. On success the caller owns `result->socket` and must close it.
 */
bool directory_session_open(const DirectorySessionRequest& request, DirectorySessionResult* result,
                            std::string* outError);

}  // namespace remote60::native_poc
