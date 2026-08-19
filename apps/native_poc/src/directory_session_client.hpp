#pragma once

// The HTTP half of reaching a host through the directory, from the viewer's side.
//
// DirectoryRendezvous deliberately owns only the UDP socket, because the address a peer must aim
// at belongs to one specific socket. Everything else -- signing in, listing the PCs on the
// account, asking for a capability to reach one of them -- is plain request/response, and it
// lives here so both the Windows client and any future viewer share one implementation of it.
//
// The host side has its own copy of this exchange in directory_client.hpp. They are separate on
// purpose: a host registers and heartbeats, a viewer signs in and connects, and the only thing
// they have in common is the wire format.

#include <cstdint>
#include <string>
#include <vector>

#include "connect_candidates.hpp"

namespace remote60::native_poc {

/** One PC on the account, as the directory reports it. */
struct DirectoryHostEntry {
  std::string hostId;
  std::string hostName;
  bool online = false;
  uint64_t lastSeenMs = 0;
};

/** What /api/connect hands back: where to aim, and the one-time capability to prove it. */
struct DirectoryConnectTarget {
  std::vector<ConnectCandidate> candidates;
  std::string punchToken;
  // Kept for the ordering older clients used, and as the fallback when the candidate list is
  // empty because the host predates it.
  std::string hostPublicIp;
  uint16_t hostPublicUdpPort = 0;
};

/**
 * Sign in with the account credentials and receive a session token.
 *
 * The token is what every later call carries; the password is not stored and is not needed
 * again. `outError` receives the server's own message when it rejects, because "wrong password"
 * and "too many attempts, retry in 40s" need different reactions from the person reading it.
 */
bool directory_login(const std::string& url, const std::string& accountId,
                     const std::string& password, std::string* outSessionToken,
                     std::string* outError);

/** The PCs registered to the signed-in account, online ones first. */
bool directory_list_hosts(const std::string& url, const std::string& sessionToken,
                          std::vector<DirectoryHostEntry>* outHosts, std::string* outError);

/**
 * Ask to reach one host.
 *
 * `observeToken` must be the one just used with DirectoryRendezvous::Observe on the socket the
 * media will arrive on -- that is how the directory knows which address to tell the host to
 * punch towards. Passing a token from a different socket produces a session that connects and
 * then receives nothing.
 */
bool directory_connect(const std::string& url, const std::string& sessionToken,
                       const std::string& hostId, const std::string& observeToken,
                       DirectoryConnectTarget* outTarget, std::string* outError);

}  // namespace remote60::native_poc
