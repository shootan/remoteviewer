#pragma once

// Handing a credential to a process that is about to be elevated, without writing it anywhere.
//
// The update route wants a session or a host token. The process that holds one is the signed-in
// application; the process that fetches the manifest is an elevated worker two launches away. Every
// obvious way to bridge that is a way to leave the credential somewhere: a command line is visible
// to anything that can list processes, an environment block is inherited and dumped by crash
// reporters, a file has to be deleted by someone, and a log is written precisely so it can be read
// later.
//
// So it goes over a named pipe that exists only for one attempt, and the sender proves who it is
// talking to before writing anything. The rules here are all one rule stated in several places:
// *nothing is sent until the receiver is known to be the process we started*.
//
//   * the pipe is created BEFORE the child, so there is no window in which the name exists and
//     nobody owns it
//   * FILE_FLAG_FIRST_PIPE_INSTANCE, so a name squatted in advance fails creation instead of
//     silently handing us somebody else's endpoint
//   * nMaxInstances = 1, so the second connection has nowhere to land
//   * PIPE_REJECT_REMOTE_CLIENTS, because this is between two processes on one machine and a
//     remote client is by definition not one of them
//   * an explicit DACL -- the current user, SYSTEM, Administrators -- rather than whatever the
//     default token happens to grant
//   * GetNamedPipeClientProcessId() compared against the PID of the handle we hold from
//     CreateProcessW / ShellExecuteExW, and the handle is kept open for the whole exchange so that
//     PID cannot be recycled onto a different process underneath the check
//   * a deadline on every wait, and nothing sent after it expires -- a late arrival is refused
//     rather than served, because "it turned up eventually" does not establish who it is
//
// Two hops, because the launch has two: the application starts a bootstrap updater, which copies
// itself and starts the working copy. The bootstrap must therefore keep the working copy's process
// handle rather than closing it -- otherwise its own check would be comparing against a handle it
// no longer holds.
//
// What this file does NOT do: decide whether a credential should be sent at all. That is
// update_endpoint.hpp's job, and it is decided before anything here is called.

#include <windows.h>

#include <cstdint>
#include <string>

namespace remote60::native_poc::update {

/** Names the credential pipe for one attempt. Distinct from the ready event: a client that never
 *  waits for readiness still needs an identity for this channel. */
std::wstring make_credential_pipe_name(uint32_t pid, uint64_t tick);

/**
 * The framing, kept small and explicit so a partial read cannot look like a short payload.
 *
 * [magic][version][length][payload]. A frame whose length is impossible is refused before any
 * allocation, and a read that stops early is a failure rather than a truncated credential.
 */
constexpr uint32_t kCredentialFrameMagic = 0x474e4c43;  // 'GNLC'
constexpr uint32_t kCredentialFrameVersion = 1;
constexpr uint32_t kCredentialMaxPayload = 4096;

std::string encode_credential_frame(const std::string& payload);

/** False when the bytes are not a complete, well-formed frame of a version we know. */
bool decode_credential_frame(const std::string& frame, std::string* payload, std::string* error);

/**
 * The sending half. Created before the child starts; served once the child's handle is known.
 *
 * Serve() writes nothing unless the connected client's process id matches `expectedProcess`, and
 * returns false without sending on any mismatch, timeout or short write.
 */
class CredentialServer {
 public:
  CredentialServer() = default;
  ~CredentialServer();
  CredentialServer(const CredentialServer&) = delete;
  CredentialServer& operator=(const CredentialServer&) = delete;

  /** Creates the pipe. Must be called before the child process is started. */
  bool Create(const std::wstring& pipeName, std::string* error);

  /**
   * Waits for the child, checks who it is, sends, and waits for its acknowledgement.
   *
   * `payload` is cleared on return whatever the outcome -- the caller's copy is the caller's
   * problem, but this one does not outlive the call.
   */
  bool Serve(HANDLE expectedProcess, std::string& payload, uint32_t deadlineMs,
             std::string* error);

  void Close();

 private:
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
};

/**
 * The receiving half. Connects, reads one frame, acknowledges.
 *
 * A failure here is not a reason to continue without a credential: the caller decides that, and
 * for the updater the answer is to fail the attempt rather than fetch anonymously and get a 401
 * it cannot explain.
 */
bool receive_credential(const std::wstring& pipeName, uint32_t deadlineMs, std::string* payload,
                        std::string* error);

}  // namespace remote60::native_poc::update
