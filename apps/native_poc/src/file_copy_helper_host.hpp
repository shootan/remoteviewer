#pragma once

// The host's side of the clipboard helper: which token it may run as, the pipe it is given, how it
// is started, and the handshake that proves the process on the other end is the one started.
// (file-copy-helper r1, plan §1)
//
// Role:    acquire_shell_token -- the helper's identity is the CURRENT SHELL PROCESS's primary
//          token (GetShellWindow -> its PID -> OpenProcessToken), accepted ONLY when judge_helper_token
//          says it is a primary token of the interactive session's user (same SID), in the host's
//          session, exactly Medium, not elevated, not a full elevation, not restricted -- checked on
//          the shell's token and again on the duplicate that is launched. No fallback: no shell, or a
//          shell token that is not that, and the feature is off. (helper-shell-token r1: the old
//          source, TokenLinkedToken, is an Identification-level token for a caller without TCB and
//          cannot become a primary token -- DuplicateTokenEx failed with 1346 on every elevated host.)
//          HelperLink -- one pipe instance under a random name with a DACL for that user alone and
//          a Medium label, the 256-bit nonce, the launch (CreateProcessWithTokenW with the shell
//          token; or, for the Medium test launch, a plain CreateProcessW as the current user), the
//          Job (KILL_ON_JOB_CLOSE), and AwaitHello: the connecting client's PID must be the
//          launched PID and its Hello must carry the nonce -- otherwise the pipe is closed and the
//          feature is off.
// Thread:  the host's control thread (later, 작업용1's wiring); one link at a time.
// Callers: launch_file_copy_helper (the product path), the helper e2e test (the Medium launch).
//
// What the Medium test launch does NOT prove: CreateProcessWithTokenW under the elevated host's
// SeImpersonate, the shell token seen from a High process, the real label on a pipe created by a
// High process. Those are the one prepared elevated run (UAC) the verifier asks the user for.

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "file_copy_pipe.hpp"
#include "file_copy_pipe_io.hpp"

namespace remote60::native_poc::file_copy {

struct TokenVerdict {
  bool ok = false;
  const char* why = "";  // a short reason when !ok
  const char* stage = "";  // where it was refused (acquire_shell_token: shell-window / shell-process / shell-token / duplicate)
  DWORD error = 0;       // GetLastError of the failing API, when there was one
};

/** The interactive user of `sessionId` as a string SID (WTS + LookupAccountName). */
bool interactive_session_user_sid(DWORD sessionId, std::wstring* sidOut, DWORD* errorOut);

/** This process's user as a string SID. */
bool current_process_user_sid(std::wstring* sidOut);

/** Integrity RID of a token (SECURITY_MANDATORY_*_RID), or 0 when it cannot be read. */
DWORD token_integrity_rid(HANDLE token);

/** One owner, one CloseHandle (process, token, duplicate). Null and INVALID_HANDLE_VALUE are "none". */
class UniqueHandle {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE h) : h_(h) {}
  ~UniqueHandle() { reset(); }
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  UniqueHandle(UniqueHandle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
  UniqueHandle& operator=(UniqueHandle&& o) noexcept {
    if (this != &o) {
      reset();
      h_ = o.h_;
      o.h_ = nullptr;
    }
    return *this;
  }
  HANDLE get() const { return h_; }
  HANDLE* put() {
    reset();
    return &h_;
  }
  explicit operator bool() const { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
  void reset() {
    if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = nullptr;
  }

 private:
  HANDLE h_ = nullptr;
};

/**
 * What a token says about itself, as read (read_token_facts). Each field has its own "read"
 * flag: a field that could not be read is a refusal, never a default. Tests build these by hand
 * to drive judge_helper_token through cases no real token on this PC can show (High, elevated).
 */
struct TokenFacts {
  bool typeRead = false;
  TOKEN_TYPE type = TokenImpersonation;
  bool userRead = false;
  std::wstring userSid;
  bool sessionRead = false;
  DWORD session = 0;
  DWORD integrityRid = 0;  // 0 = unreadable
  bool elevationRead = false;
  bool elevated = true;  // TokenElevation.TokenIsElevated
  bool elevationTypeRead = false;
  TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeFull;
  bool restricted = true;  // IsTokenRestricted
  DWORD error = 0;         // GetLastError of the first field that could not be read
};

TokenFacts read_token_facts(HANDLE token);

/**
 * Pure: may a token with these facts run the helper? The contract, in order: every field read;
 * a PRIMARY token; user SID == `interactiveUserSid`; session == `sessionId`; integrity exactly
 * Medium; TokenIsElevated false; elevation type Limited or Default (Default: no linked token --
 * a standard user, not something to refuse; Full is refused); not restricted. Each refusal names
 * itself (the caller adds the stage when it logs it).
 */
TokenVerdict judge_helper_token(const TokenFacts& facts, DWORD sessionId, const std::wstring& interactiveUserSid);

/** "type=primary il=0x2000 elevated=0 elevType=limited restricted=0 session=1" -- no SID, no handle values. */
std::string describe_token_facts(const TokenFacts& facts);

/** Why the helper did not start, as a class (helper-shell-token r1 A4). Logged; not on the wire. */
enum class LaunchFailure : uint8_t {
  None = 0,
  Missing,          // the helper executable is not there (an install / update problem)
  TokenRejected,    // a token could not be read, or is not the interactive user's plain Medium one
  NoShell,          // no shell window / its process is gone or cannot be opened
  SpawnFailed,      // the pipe, the Job or CreateProcess*
  HandshakeFailed,  // started, but did not connect / prove itself
};
const char* launch_failure_name(LaunchFailure f);
/** The class a launcher's `why` begins with ("missing: ...") -- None when it names none. */
LaunchFailure launch_failure_of(const std::string& why);

/**
 * The shell's identity for the helper: GetShellWindow -> PID -> OpenProcess(QUERY_LIMITED) ->
 * OpenProcessToken(QUERY|DUPLICATE) -> judge -> DuplicateTokenEx(QUERY|DUPLICATE|ASSIGN_PRIMARY,
 * primary) -> judge again on the duplicate. Never enumerates processes by name, never another
 * session's or user's token, never SYSTEM. On success `*primaryOut` owns the duplicate. `*cls`
 * is NoShell or TokenRejected on refusal; the verdict's `stage` names the step. `*diag`
 * (optional) gets the shell token's facts.
 */
TokenVerdict acquire_shell_token(DWORD sessionId, const std::wstring& interactiveUserSid, UniqueHandle* primaryOut,
                                 LaunchFailure* cls, std::string* diag);

struct HelloCheck {
  bool ok = false;
  const char* why = "";
};

/** Pure: the handshake rule. The client PID must be the launched PID and the nonce must match. */
HelloCheck verify_hello(const Hello& hello, const std::array<uint8_t, kNonceBytes>& expectedNonce,
                        uint32_t clientPid, uint32_t launchedPid);

/**
 * Pure: whether a process with this elevation type and integrity may start the helper as ITSELF
 * (HelperLink::Launch with token == nullptr, the Medium test launch). An elevated caller may not:
 * the helper would inherit administrator rights, which is the one thing this design exists to
 * prevent. The product path always passes the shell token; this guards a later wiring mistake.
 */
bool medium_launch_allowed(TOKEN_ELEVATION_TYPE type, DWORD integrityRid);

namespace detail {

/**
 * The pieces of AwaitHello's connection wait, exposed so the cases that cannot be timed from
 * outside -- a connection completing between the wait timing out and its cancellation, a
 * cancellation that never completes -- can be driven deterministically. (r3 ①)
 */
enum class ConnectOutcome : uint8_t {
  Connected = 0,
  Pending,        // begin_connect only: the wait is on
  Timeout,
  ProcessExited,  // the launched helper ended before connecting
  Failed,         // an API error (*error)
  Stuck,          // the cancellation never completed: the storage was orphaned (leaked, counted)
};

/** Heap-owned storage of a pending ConnectNamedPipe, freed only once the OS says it is over. */
struct PendingConnect {
  OVERLAPPED io{};
};

/**
 * Starts ConnectNamedPipe. Connected / Failed at once, or Pending with `*pending` to settle.
 * Under the process-wide I/O bound (file_copy_pipe_io.hpp, r4): at kMaxOrphanedIo it makes no
 * OS call at all and returns Failed with `*error` = ERROR_NO_SYSTEM_RESOURCES. A Pending wait
 * holds its reservation until settle_connect returns; Stuck keeps it (the orphan).
 */
ConnectOutcome begin_connect(HANDLE pipe, std::unique_ptr<PendingConnect>* pending, DWORD* error);

/** Telemetry: ConnectNamedPipe calls made by this process (the bound test reads it). */
std::atomic<uint32_t>& connect_attempts();

/**
 * Waits up to `timeoutMs` for a pending connection (ending early if `process` ends), cancels it at
 * the bound, and then asks the OS what actually happened: a connection that completed before the
 * cancellation took effect is Connected, not lost. Stuck leaks the storage and counts it.
 */
ConnectOutcome settle_connect(HANDLE pipe, std::unique_ptr<PendingConnect>& pending, DWORD timeoutMs, HANDLE process,
                              DWORD* error);

/** Test seam: runs after a connection wait times out and before it is cancelled. */
std::function<void()>& before_connect_cancel_hook();

}  // namespace detail

class HelperLink {
 public:
  HelperLink() = default;
  ~HelperLink();
  HelperLink(const HelperLink&) = delete;
  HelperLink& operator=(const HelperLink&) = delete;

  /**
   * Creates the pipe: `\\.\pipe\GNLinkClip-<128-bit hex>`, one instance, byte mode, overlapped,
   * remote clients rejected, DACL = `userSid` full access and nobody else, integrity label
   * Medium (no-write-up). Draws the nonce. `userSid` is the helper's user -- the shell token's
   * user on the product path, this process's user for the Medium test launch.
   */
  bool CreateServerPipe(const std::wstring& userSid, std::string* why);

  const std::wstring& pipe_name() const { return pipeName_; }
  const std::array<uint8_t, kNonceBytes>& nonce() const { return nonce_; }

  /** `"exe" --pipe <name> --nonce <hex> --host-pid <pid> [extraArgs]` */
  std::wstring helper_command_line(const std::wstring& exe, const std::wstring& extraArgs) const;

  /**
   * Starts the helper, suspended, puts it in a Job that kills it when the Job (this object) goes,
   * then resumes it. `token` non-null: CreateProcessWithTokenW(token) -- the product path, needs
   * SeImpersonate (an elevated host has it). `token` null: CreateProcessW as this process's user
   * -- the Medium test launch, REFUSED when this process is elevated (medium_launch_allowed).
   * `desktop` (optional) is the `winsta\desktop` the helper is put on.
   */
  bool Launch(const std::wstring& exe, HANDLE token, const wchar_t* desktop, const std::wstring& extraArgs,
              std::string* why);

  /**
   * Waits for the helper to connect (bounded), refuses a client whose PID is not the launched
   * one, reads its Hello and refuses a wrong nonce, answers HelloAck. On refusal the pipe is
   * closed: there is no second chance on this instance.
   */
  bool AwaitHello(DWORD timeoutMs, std::string* why);

  /**
   * The pipe handle has ONE owner, this object, and is closed ONCE (r7): Send and Receive register
   * as "in flight" on it while they use it; ClosePipe / Close while something is in flight only
   * cancel that I/O (CancelIoEx) and mark the handle for closing -- the last operation to leave
   * closes it. Nothing ever does I/O on a closed or reused handle, and no handle is closed twice.
   * A pending I/O's storage is owned by the I/O layer (file_copy_pipe_io.hpp): it is released
   * only when the OS reports the cancelled operation complete, or orphaned by its policy.
   */
  /** False closes the pipe: a frame that could not be written whole leaves the link unusable. */
  bool Send(const PipeFrame& frame, DWORD timeoutMs = 5000);
  /**
   * False with GetLastError() == WAIT_TIMEOUT means "nothing complete yet" (a half-received frame
   * is kept for the next call); any other error closes the pipe.
   */
  bool Receive(PipeFrame* frame, DWORD timeoutMs);

  /** Closes the pipe, the process handle and the Job (which ends the helper). */
  void Close();

  /**
   * Drops only the pipe: the helper is expected to notice, clear the clipboard and exit on its
   * own (its contract on disconnect). The Job stays, so it still cannot outlive this object.
   */
  void ClosePipe();

  /** The Win32 error of the last Launch / AwaitHello refusal (0 when it had none). */
  DWORD last_error() const { return lastError_; }
  DWORD helper_pid() const { return helperPid_; }
  HANDLE helper_process() const { return process_; }
  bool helper_alive() const;
  bool pipe_open() const;

  /**
   * Diagnostics (tests): pipe handles created / closed by every HelperLink of this process. Each
   * handle is closed exactly once, so once every link is closed the two agree.
   */
  static uint32_t pipes_created();
  static uint32_t pipes_closed();
  /**
   * TEST ONLY -- called on every Receive once it is registered as in flight, before the read, with
   * the pid of the helper on the other end, so a test can cross ONE link's read with ClosePipe on
   * purpose. No product code calls this; the build gate checks the shipped executables do not
   * carry it.
   */
  static void SetReceiveProbeForTest(std::function<void(DWORD helperPid)> probe);

 private:
  std::wstring pipeName_;
  std::array<uint8_t, kNonceBytes> nonce_{};
  bool Enter(HANDLE* h);       // registers an I/O on pipe_ (false: closed or closing)
  void Leave(bool failed);     // unregisters; closes the handle if asked to and nobody is left
  void CloseNowLocked();       // hmu_ held, nothing in flight: the one CloseHandle

  HANDLE pipe_ = INVALID_HANDLE_VALUE;  // under hmu_
  mutable std::mutex hmu_;              // pipe_, ioInFlight_, closeRequested_, process_, job_
  int ioInFlight_ = 0;                  // Sends / Receives using pipe_ right now
  bool closeRequested_ = false;         // ClosePipe came while I/O was in flight: the last one out closes
  HANDLE process_ = nullptr;
  HANDLE job_ = nullptr;
  DWORD helperPid_ = 0;
  DWORD lastError_ = 0;
  bool handshaken_ = false;
  FrameReader reader_;
};

/**
 * The product path, in order: the helper executable exists -> this process's session -> its
 * interactive user -> acquire_shell_token -> pipe for that user -> CreateProcessWithTokenW (Job,
 * suspended) -> AwaitHello. Any refusal leaves `link` closed and `why` =
 * "<class>: stage=<step> err=<win32> (<detail>)" with the class from launch_failure_name, plus the
 * token diagnostics when they were read. Never falls back to another token. `*diag` (optional)
 * gets the token diagnostics line on success too: the shell token's facts and, for comparison,
 * the type / impersonation level of this process's TokenLinkedToken (no longer used to launch).
 */
bool launch_file_copy_helper(const std::wstring& helperExe, HelperLink* link, std::string* why,
                             DWORD helloTimeoutMs = 10000, std::string* diag = nullptr);

/**
 * The viewer's path (R->P: remote files published on THIS PC's clipboard): the helper runs as this
 * process's own user -- the viewer is a Medium program, so nothing is raised or lowered. Refused
 * when this process is elevated (HelperLink::Launch). `desktop` null = this process's desktop; a
 * test names its private window station's.
 */
bool launch_file_copy_helper_as_self(const std::wstring& helperExe, const wchar_t* desktop, const std::wstring& extraArgs,
                                     HelperLink* link, std::string* why, DWORD helloTimeoutMs = 10000);

}  // namespace remote60::native_poc::file_copy
