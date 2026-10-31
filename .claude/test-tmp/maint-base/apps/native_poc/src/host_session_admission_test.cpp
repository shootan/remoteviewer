// prelogin-ui r9 (R3): what directory authentication allows a control dispatcher, while a newer session
// is being admitted.
//
// A dispatcher serves one session epoch. Before r9 a privileged request (remote restart, the SYSTEM input
// path) asked the shared "directory-authenticated" flag and whether the epoch was still its own -- and the
// UDP reader set that flag for a NEW directory Hello before it opened the new epoch. In between, the
// dispatcher of an older, unauthenticated LAN session saw "authenticated" and "my epoch" at once and could
// have a restart delegated on the newcomer's authority.
//
// This test drives the product's SessionState::AdmitHello (what both UDP admission sites call) and asks
// the product's DirectoryAuthenticatedFor at EVERY intermediate write of an admission (the `step` hook):
// the older dispatcher is never authorized, the new one is once admitted. Pure: no sockets are opened.

#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <string>

#include "host_session.hpp"

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("directory auth bound to the admitted epoch (prelogin-ui r9, R3)\n\n");
  remote60::native_poc::SessionState s;

  // A plain-LAN client (no capability) opens a session: its dispatcher serves that epoch.
  const uint64_t lan = s.AdmitHello(/*directoryAuth=*/false, /*startsSession=*/true);
  check("a LAN session is admitted, unauthenticated", !s.DirectoryAuthenticatedFor(lan), std::to_string(lan));

  // A directory-authenticated client now arrives; the LAN dispatcher (epoch `lan`) is still running and
  // looks at every point of the admission.
  std::string seen;
  bool lanEverAuthorized = false;
  const uint64_t dir = s.AdmitHello(true, true, [&](const char* step) {
    const bool lanAuth = s.DirectoryAuthenticatedFor(lan);
    seen += std::string(step) + (lanAuth ? "=LAN-AUTHORIZED " : "=no ");
    if (lanAuth) lanEverAuthorized = true;
  });
  check("R3: at no point of the new admission is the older LAN dispatcher authorized", !lanEverAuthorized, seen);
  check("...the new session is a new epoch", dir == lan + 1, std::to_string(dir));
  check("...and its own dispatcher is authorized once admitted", s.DirectoryAuthenticatedFor(dir));
  check("...the older one still is not (superseded)", !s.DirectoryAuthenticatedFor(lan));

  // The same client again (no new session): it keeps its standing; nobody else gains it.
  (void)s.AdmitHello(true, false);
  check("a re-Hello of the authenticated session keeps it authorized", s.DirectoryAuthenticatedFor(dir));

  // An authenticated session replaced by another: the old one loses it.
  const uint64_t dir2 = s.AdmitHello(true, true);
  check("an authenticated session replaced by a newer one: the old dispatcher is no longer authorized",
        !s.DirectoryAuthenticatedFor(dir) && s.DirectoryAuthenticatedFor(dir2));

  // An epoch opened by anything else than an admission (rollover) carries no auth of its own.
  const uint64_t rolled = s.BeginEpoch();
  check("an epoch opened without an admission is not authorized (fails closed)", !s.DirectoryAuthenticatedFor(rolled));

  // The first session of a process (the prelogin first-Hello site): no new epoch, the current one admitted.
  remote60::native_poc::SessionState first;
  const uint64_t e0 = first.epoch.load();
  (void)first.AdmitHello(true, false);
  check("the first authenticated Hello admits the current epoch", first.DirectoryAuthenticatedFor(e0), std::to_string(e0));

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
