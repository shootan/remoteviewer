#pragma once

// Prelogin ingress authentication policy (prelogin-service r6 G2).
//
// Codex G2: the first UDP Hello accepted a token-less peer (tokenLen==0 simply skipped authorization),
// and a SEPARATE unauthenticated TCP control link / LAN direct-dial listener could borrow the shared
// directoryAuthenticated+epoch state. In prelogin the login screen is exposed, so every ingress MUST be
// authenticated BEFORE the peer is adopted or any frame is served, and there must be no side channel that
// bypasses that. The concrete closures:
//   - PreloginAdmitFirstHello: in prelogin a first Hello is admitted ONLY if it authenticated.
//   - the separate TCP control listener and the LAN direct-dial listener are NOT opened in prelogin, so
//     the only ingress is the authenticated UDP session (there is no unauthenticated link to borrow auth).
//   - a directory failure/absence means OFFLINE, never a LAN fallback.

namespace remote60::native_poc::prelogin {

// A first Hello is admitted when the host is not in prelogin mode (unchanged LAN behaviour), or -- in
// prelogin -- only when it presented a valid directory capability (authenticated). A token-less prelogin
// Hello is refused before any peer/media state is adopted.
inline bool PreloginAdmitFirstHello(bool preloginMode, bool authenticated) {
  return !preloginMode || authenticated;
}

// Whether the host should open a listener/channel that is NOT the authenticated directory UDP session.
// In prelogin these side channels are disabled so there is no unauthenticated ingress.
inline bool PreloginAllowsSideChannel(bool preloginMode) { return !preloginMode; }

}  // namespace remote60::native_poc::prelogin
