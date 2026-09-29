#pragma once

// The host's side of the clipboard helper: which token it may run as, the pipe it is given, how it
// is started, and the handshake that proves the process on the other end is the one started.
// (file-copy-helper r1, plan §1)
//
// Role:    inspect_helper_token -- the elevated host's TokenLinkedToken, accepted ONLY when it is
//          the interactive session's user (same SID), Medium integrity, the same session, not
//          restricted. No fallback: if the linked token is not that, the feature is off.
//          HelperLink -- one pipe instance under a random name with a DACL for that user alone and
//          a Medium label, the 256-bit nonce, the launch (CreateProcessWithTokenW with the linked
//          token; or, for the Medium test launch, a plain CreateProcessW as the current user), the
//          Job (KILL_ON_JOB_CLOSE), and AwaitHello: the connecting client's PID must be the
//          launched PID and its Hello must carry the nonce -- otherwise the pipe is closed and the
//          feature is off.
// Thread:  the host's control thread (later, 작업용1's wiring); one link at a time.
// Callers: launch_file_copy_helper (the product path), the helper e2e test (the Medium launch).
//
// What the Medium test launch does NOT prove: CreateProcessWithTokenW under the elevated host's
// SeImpersonate, the real linked token, the real label on a pipe created by a High process. Those
// are the one prepared elevated run (UAC) the verifier asks the user for at the end.

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "file_copy_pipe.hpp"
#include "file_copy_pipe_io.hpp"

namespace remote60::native_poc::file_copy {

struct TokenVerdict {
  bool ok = false;
  const char* why = "";  // a short reason when !ok
  DWORD error = 0;       // GetLastError of the failing API, when there was one
};

/** The interactive user of `sessionId` as a string SID (WTS + LookupAccountName). */
bool interactive_session_user_sid(DWORD sessionId, std::wstring* sidOut, DWORD* errorOut);

/** This process's user as a string SID. */
bool current_process_user_sid(std::wstring* sidOut);

/** Integrity RID of a token (SECURITY_MANDATORY_*_RID), or 0 when it cannot be read. */
DWORD token_integrity_rid(HANDLE token);

/**
 * Examines `elevatedProcessToken`'s linked token for use as the helper's identity. On success
 * `*linkedPrimaryOut` is a PRIMARY token the caller owns (CloseHandle). The checks, in order:
 * the caller is a full elevation; a linked token exists; its user SID equals
 * `interactiveUserSid`; its session is `sessionId`; it is Medium integrity; it is not
 * restricted; it is a limited elevation. Every refusal names itself.
 */
TokenVerdict inspect_helper_token(HANDLE elevatedProcessToken, DWORD sessionId,
                                  const std::wstring& interactiveUserSid, HANDLE* linkedPrimaryOut);

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
 * prevent. The product path always passes the linked token; this guards a later wiring mistake.
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
   * Medium (no-write-up). Draws the nonce. `userSid` is the helper's user -- the linked token's
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

  DWORD helper_pid() const { return helperPid_; }
  HANDLE helper_process() const { return process_; }
  bool helper_alive() const;
  bool pipe_open() const { return pipe_ != INVALID_HANDLE_VALUE; }

 private:
  std::wstring pipeName_;
  std::array<uint8_t, kNonceBytes> nonce_{};
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE process_ = nullptr;
  HANDLE job_ = nullptr;
  DWORD helperPid_ = 0;
  bool handshaken_ = false;
  FrameReader reader_;
};

/**
 * The product path, in order: this process's token -> the interactive user of this session ->
 * inspect_helper_token -> pipe for that user -> CreateProcessWithTokenW -> AwaitHello. Any
 * refusal leaves `link` closed and names itself in `why`. Never falls back to another token.
 */
bool launch_file_copy_helper(const std::wstring& helperExe, HelperLink* link, std::string* why,
                             DWORD helloTimeoutMs = 10000);

/**
 * The viewer's path (R->P: remote files published on THIS PC's clipboard): the helper runs as this
 * process's own user -- the viewer is a Medium program, so nothing is raised or lowered. Refused
 * when this process is elevated (HelperLink::Launch). `desktop` null = this process's desktop; a
 * test names its private window station's.
 */
bool launch_file_copy_helper_as_self(const std::wstring& helperExe, const wchar_t* desktop, const std::wstring& extraArgs,
                                     HelperLink* link, std::string* why, DWORD helloTimeoutMs = 10000);

}  // namespace remote60::native_poc::file_copy
