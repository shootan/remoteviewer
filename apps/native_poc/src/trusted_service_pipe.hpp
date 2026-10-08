#pragma once

// Trusted service-pipe client layer (secure-pipe-identity, 0.2.154 P1).
//
// The GNLinkSecureInput service hosts two local pipes -- \\.\pipe\GNLinkSecureInput (secure input) and
// \\.\pipe\GNLinkUnlock (sealed unlock). While the service is stopped, any local user process can create
// those names first; a client that trusts CreateFile success alone would then send secure input to it, or
// let a fake unlock server hand the viewer a challenge under the fake's own key (sealing the saved
// password to the attacker). This layer makes every client verify, BEFORE writing a byte, that the pipe's
// server is the installation's actually-running service, and makes CreateFile request only IDENTIFICATION
// impersonation. (Source-level defence; no field compromise is claimed.)
//
// The decision is a pure function of gathered facts (ClassifyServerIdentity) so it is testable off a
// machine and the "check removed" negative control is a real behavioural difference. The Win32 gatherer
// (GatherServerIdentity) binds three things: the pipe's server PID, the SCM's RUNNING pid for the service,
// and the server process image's FILE IDENTITY against the service's configured binary. Trust is valid
// only for that one open handle; a reconnect or a service restart re-verifies.

#include <cstdint>

namespace remote60::native_poc {

enum class TrustedPipeStatus {
  Trusted,      // the server is the installation's running service, bound by pid + image file identity
  Untrusted,    // the server is confirmed NOT the running service (different pid, or different image)
  Unavailable,  // identity could not be established (a lookup failed) -- fail closed, never trust
};

const char* trusted_pipe_status_name(TrustedPipeStatus s);

// Facts the Win32 layer gathers about one open client pipe handle vs the installed service.
struct ServerIdentityFacts {
  bool serverPidKnown = false;      // GetNamedPipeServerProcessId succeeded
  uint32_t serverPid = 0;
  bool serviceRunning = false;      // the SCM reports the service RUNNING with a process id
  uint32_t servicePid = 0;
  bool serverImageKnown = false;    // the server process was opened and its image file identity read
  bool serviceBinaryKnown = false;  // the service's configured binary file identity was read
  bool imageSameFile = false;       // the two are the SAME file (volume + file index)
};

// Pure verdict. Trusted ONLY when the server pid is the SCM's running-service pid AND the server
// process's image is the same file as the service binary. A missing pid/running/image fact is
// Unavailable (fail closed); a confirmed mismatch is Untrusted.
TrustedPipeStatus ClassifyServerIdentity(const ServerIdentityFacts& f);

// Win32: gather the facts for an already-open client pipe handle and a service name.
ServerIdentityFacts GatherServerIdentity(void* pipeHandle, const wchar_t* serviceName);

// A gatherer seam: production uses GatherServerIdentity; tests inject a fixture that returns synthetic
// facts for a test-owned server (so no real service/credential is touched). Never a shipping bypass:
// the default is the real gatherer and there is no env/option to skip it.
using ServerIdentityGatherer = ServerIdentityFacts (*)(void* pipeHandle, const wchar_t* serviceName);

// Open `name` as a client with SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, wait up to waitMs for the
// name, then verify the server. On Trusted, returns the handle (caller owns). On anything else, closes
// the handle it opened and returns INVALID (nullptr-equivalent via the out status); never falls back to
// an unverified server and never touches any other process. `gather` defaults to the production gatherer.
void* OpenVerifiedServicePipe(const wchar_t* name, uint32_t desiredAccess, uint32_t flagsAndAttributes,
                              const wchar_t* serviceName, uint32_t waitMs, TrustedPipeStatus* status,
                              ServerIdentityGatherer gather = nullptr);

}  // namespace remote60::native_poc
