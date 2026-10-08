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

// Which step established or failed the identity -- kept for the local diagnostic (R3), never on the wire.
enum class PipeVerifyStage {
  None,
  Open,            // WaitNamedPipe / CreateFile to the name failed (absent / access denied / busy)
  ServerPid,       // GetNamedPipeServerProcessId
  ServiceStatus,   // SCM QueryServiceStatusEx (running + pid); not-installed vs stopped vs denied
  ServerImage,     // OpenProcess + QueryFullProcessImageName + file id of the server
  ServiceBinary,   // QueryServiceConfig binary + its file id
  ServerPidMismatch,  // pipe server pid != the SCM's running pid
  ImageMismatch,      // server image is a different file than the service binary
  FinalRecheck,       // end-of-collection: server still alive AND still the SCM's running pid
};
const char* pipe_verify_stage_name(PipeVerifyStage s);

// Facts the Win32 layer gathers about one open client pipe handle vs the installed service.
struct ServerIdentityFacts {
  bool serverPidKnown = false;      // GetNamedPipeServerProcessId succeeded
  uint32_t serverPid = 0;
  bool serviceRunning = false;      // the SCM reports the service RUNNING with a process id
  uint32_t servicePid = 0;
  bool serverImageKnown = false;    // the server process was opened and its image file identity read
  bool serviceBinaryKnown = false;  // the service's configured binary file identity was read
  bool imageSameFile = false;       // the two are the SAME file (volume + file index)
  // R2: at the END of collection, the SAME server process object was still alive AND the SCM still
  // reported that same pid RUNNING for the service. The default is FALSE so an un-collected facts
  // value (e.g. a CreateFile that never reached the gatherer) is never accidentally trusted; the Win32
  // gatherer sets it true only after an actual passing re-check.
  bool finalConsistent = false;
  // R3 diagnostic (local only): the first step that failed and its original Win32 error.
  PipeVerifyStage failStage = PipeVerifyStage::None;
  uint32_t failWin32 = 0;
};

// Pure verdict. Trusted ONLY when the server pid is the SCM's running-service pid, the server image is
// the same file as the service binary, AND the end-of-collection re-check held. A missing pid/running/
// image/recheck fact is Unavailable (fail closed); a confirmed mismatch is Untrusted.
TrustedPipeStatus ClassifyServerIdentity(const ServerIdentityFacts& f);

// Win32: gather the facts for an already-open client pipe handle and a service name. Holds the server
// process handle across the whole collection so the pid cannot be reused mid-check, and re-confirms the
// live pid/SCM pid at the end (R2). Records the first failing step + its Win32 error (R3).
ServerIdentityFacts GatherServerIdentity(void* pipeHandle, const wchar_t* serviceName);

// R3: record a one-line LOCAL diagnostic for a verification result, but only when it CHANGES for this
// `channel` (so a persistent squatter does not spam a line per input/connect). Keeps the failing stage,
// the original Win32 error, and the verdict. Nothing goes on the wire; nothing is read from credentials.
void LogTrustChange(const char* channel, TrustedPipeStatus status, const ServerIdentityFacts& facts);

// A gatherer seam: production uses GatherServerIdentity; tests inject a fixture that returns synthetic
// facts for a test-owned server (so no real service/credential is touched). Never a shipping bypass:
// the default is the real gatherer and there is no env/option to skip it.
using ServerIdentityGatherer = ServerIdentityFacts (*)(void* pipeHandle, const wchar_t* serviceName);

// Open `name` as a client with SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, wait up to waitMs for the
// name, then verify the server. On Trusted, returns the handle (caller owns). On anything else, closes
// the handle it opened and returns INVALID (nullptr-equivalent via the out status); never falls back to
// an unverified server and never touches any other process. `gather` defaults to the production gatherer.
// `waitMs` 0 means NMPWAIT_USE_DEFAULT_WAIT (NOT a non-blocking return) -- stated so a later caller
// (e.g. prelogin's fast-fail path) does not read 0 as "return immediately".
// `diagOut` (optional) receives the gathered facts incl. the failing stage + Win32 error, so the caller
// can keep a local diagnostic (R3) without re-querying anything.
void* OpenVerifiedServicePipe(const wchar_t* name, uint32_t desiredAccess, uint32_t flagsAndAttributes,
                              const wchar_t* serviceName, uint32_t waitMs, TrustedPipeStatus* status,
                              ServerIdentityGatherer gather = nullptr, ServerIdentityFacts* diagOut = nullptr);

}  // namespace remote60::native_poc
