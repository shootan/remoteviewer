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

#include "directory_observe.hpp"

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
  // pc2-connect-diag: the server's label for this attempt, so the client, viewer and server
  // logs can be read as one story. Empty against a server that predates it.
  std::string connectId;
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
/**
 * Asks the directory where observations go, without holding a session.
 *
 * For the path where a session came from cache and no login happened, so the login response --
 * where this normally rides -- was never seen. Without it a cached session on an https directory
 * would fall into "nothing was advertised" and refuse to observe, which would make reconnecting
 * worse than connecting fresh.
 *
 * Returns false when the server says nothing usable; that is a state, not an error.
 */
bool directory_observe_from_health(const std::string& url, directory::ObserveEndpoint* outObserve,
                                   std::string* outError);

bool directory_login(const std::string& url, const std::string& accountId,
                     const std::string& password, std::string* outSessionToken,
                     std::string* outError,
                     directory::ObserveEndpoint* outObserve = nullptr);

/**
 * The PCs registered to the signed-in account, online ones first.
 *
 * `outStatus` carries the http status when the exchange happened at all: a 401 means the
 * session is no longer one, which a client holding a device credential can repair by itself.
 */
bool directory_list_hosts(const std::string& url, const std::string& sessionToken,
                          std::vector<DirectoryHostEntry>* outHosts, std::string* outError,
                          uint32_t* outStatus = nullptr);

// ---------------------------------------------------------------- staying signed in
//
// A sign-in can ask the directory for a device credential: something that gets a new session
// later without the password. See apps/directory/README.md ("Staying signed in").

/** What the directory handed out. `revokeToken` comes with a sign-in, never with a refresh. */
struct DeviceSignIn {
  std::string sessionToken;
  std::string deviceId;
  std::string deviceCredential;
  std::string revokeToken;
};

/**
 * What became of a call, in the terms the caller has to act on.
 *
 * They are kept apart because each one means something different for the stored credential:
 * only Rejected is an answer about the credential itself.
 */
enum class SessionCall {
  Ok,
  Rejected,     // 401: the credential (or password) is not accepted. Erase, ask to sign in.
  Unsupported,  // 404/405: a directory from before device credentials. Keep, do without.
  Limited,      // 429: asked too often. Keep, try later.
  Failed,       // anything else that answered -- 5xx, a redirect, a body that makes no sense
  Unreachable,  // nothing answered
};
const char* session_call_name(SessionCall call);

/**
 * Signs in and asks for a device credential for this device.
 *
 * Against a directory that does not issue them the sign-in still succeeds: `out->sessionToken`
 * is set and the device fields are empty. That is a state, not an error.
 */
SessionCall directory_login_with_device(const std::string& url, const std::string& accountId,
                                        const std::string& password, const std::string& kind,
                                        const std::string& label, DeviceSignIn* out,
                                        std::string* outError);

/** Exchanges the credential for a session and the next credential. */
SessionCall directory_session_refresh(const std::string& url, const std::string& deviceId,
                                      const std::string& deviceCredential, DeviceSignIn* out,
                                      std::string* outError);

/**
 * Ends the device and every session it issued.
 *
 * With the revoke token, a session, or both; the revoke token is what a client that signed out
 * while offline still has. Ending something already ended is Ok.
 */
SessionCall directory_session_logout(const std::string& url, const std::string& deviceId,
                                     const std::string& revokeToken,
                                     const std::string& sessionToken, std::string* outError);

/**
 * Ask to reach one host.
 *
 * `observeToken` must be the one just used with DirectoryRendezvous::Observe on the socket the
 * media will arrive on -- that is how the directory knows which address to tell the host to
 * punch towards. Passing a token from a different socket produces a session that connects and
 * then receives nothing.
 */
/**
 * `outStatus` carries the http status when the exchange happened at all, so a caller can tell a
 * 409 -- the directory has no address observation for this client, which is repairable here by
 * sending one -- from every other refusal, which is not.
 */
bool directory_connect(const std::string& url, const std::string& sessionToken,
                       const std::string& hostId, const std::string& observeToken,
                       DirectoryConnectTarget* outTarget, std::string* outError,
                       uint32_t* outStatus = nullptr);

}  // namespace remote60::native_poc
