#pragma once

// The private channel between the SYSTEM service and its one-shot lock-screen agent (lockscreen-cred r1, Codex
// codex_lockcred_agent_r1.md). Handle inheritance cannot cross sessions (the service is in session 0, the agent
// in the console session; CreateProcessAsUserW's bInheritHandles contract), so the job is not handed over an
// inherited pipe. Instead:
//
//   1. the service creates a ONE-SHOT named pipe FIRST: an unpredictable name (128 random bits), the first and
//      only instance (FILE_FLAG_FIRST_PIPE_INSTANCE -- a name someone already holds is a failure, not shared),
//      local clients only (PIPE_REJECT_REMOTE_CLIENTS), a DACL for SYSTEM alone. The name is not a secret and
//      nothing is authenticated by it.
//   2. it starts the agent (the protected installed executable, bInheritHandles FALSE, suspended, in a
//      kill-on-close job, then resumed) with only the mode and that name's id on its command line.
//   3. it sends the job only to the client that IS that child: the pipe's client process id is the one
//      CreateProcess returned (the service holds that process's handle, so the id cannot have been reused), the
//      child is still running, its session is the target one, its token is SYSTEM -- and the child's first
//      message repeats its id. Anything else connecting is refused and gets nothing.
//   4. the agent, for its part, talks only to a server that IS the trusted service: the pipe's server is the
//      installed GNLinkSecureInput service's process (the existing trusted-pipe check) and that process is the
//      agent's own parent.
//
// Never in argv / environment / a file / stdout / a log: the password, the sealed job, a key. If the channel
// cannot be set up, nothing falls back to any of those -- the job simply fails.
//
// Wire (bounded, fixed, versioned): agent -> service Hello, service -> agent Job, then (r2, Codex L2) the
// commit fence: the agent waits for its target and says Ready; the service asks its own fence again -- the
// request still valid in every respect it was issued for -- and answers Go(allow) or Go(deny); only on allow
// does the agent touch the field. Then agent -> service Answer. A Ready never answered within the bound is a
// deny (the service ends the child).

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

#include "sealed_unlock.hpp"

namespace remote60::native_poc::lock_cred_channel {

constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\GNLinkLockCred-";
constexpr uint32_t kHelloMagic = 0x4C434848u;   // "LCHH"
constexpr uint32_t kJobMagic = 0x4C434A42u;     // "LCJB"
constexpr uint32_t kAnswerMagic = 0x4C434A41u;  // "LCJA"
constexpr uint32_t kReadyMagic = 0x4C435244u;   // "LCRD"
constexpr uint32_t kGoMagic = 0x4C43474Fu;      // "LCGO"
constexpr uint16_t kVersion = 2;

#pragma pack(push, 1)
struct Hello {
  uint32_t magic = kHelloMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(Hello));
  uint16_t version = kVersion;
  uint32_t pid = 0;  // the agent's own process id
};
struct Job {
  uint32_t magic = kJobMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(Job));
  uint16_t version = kVersion;
  uint32_t session = 0;  // the session it must type in (the agent checks it is its own)
  uint32_t requestId = 0;  // r2: the request it is for (Ready / Go / Answer repeat it)
  uint16_t count = 0;
  uint16_t reserved = 0;
  uint16_t units[sealed_unlock::kMaxPasswordUtf16] = {};
};
// agent -> service: Ready (target found, waiting for Go) or Answer (final) -- one shape, told apart by magic.
struct AgentMessage {
  uint32_t magic = kAnswerMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(AgentMessage));
  uint16_t stage = 0;  // Answer: UnlockStage Injected / TargetNotReady / InjectFailed
  uint32_t requestId = 0;
  uint32_t win32 = 0;
};
using Answer = AgentMessage;
// service -> agent: the commit fence's answer.
struct Go {
  uint32_t magic = kGoMagic;
  uint16_t size = static_cast<uint16_t>(sizeof(Go));
  uint16_t allow = 0;  // 1: type now; 0: type nothing
  uint32_t requestId = 0;
};
#pragma pack(pop)

// An id is exactly 32 lowercase hex digits (what CreateServerPipe makes); anything else is refused.
bool IdIsWellFormed(const std::wstring& id);
std::wstring PipeNameFor(const std::wstring& id);

// 1. The service's pipe, under a fresh random id (or `fixedId`, for a test that must collide). Overlapped,
// duplex, byte mode, one instance. INVALID_HANDLE_VALUE with *err on failure (a taken name included).
HANDLE CreateServerPipe(const wchar_t* sddl, std::wstring* idOut, uint32_t* err, const std::wstring& fixedId = std::wstring());

// 3. Waits (bounded) for a client, then accepts it only if it is `child` -- see the top -- and its Hello says
// so. `requireSystem`: the client's token must be LocalSystem (off only for a same-user test build). False
// with *why naming the first check that failed; the caller then sends nothing and ends the child.
bool AcceptOwnedChild(HANDLE pipe, HANDLE child, DWORD childPid, DWORD expectSession, bool requireSystem, DWORD timeoutMs,
                      const char** why);

// Bounded overlapped I/O on the server's pipe: all `n` bytes within `timeoutMs`, or false (the I/O cancelled).
bool WriteBounded(HANDLE pipe, const void* data, DWORD n, DWORD timeoutMs);
bool ReadBounded(HANDLE pipe, void* data, DWORD n, DWORD timeoutMs);

// 4. The agent's side: opens the pipe for `id` and keeps it only if its server is `expectedServerPid` (its
// parent, read by the caller) AND passes `verifyServer` (the trusted-service check). INVALID_HANDLE_VALUE with
// *why otherwise.
using ServerCheck = bool (*)(HANDLE pipe);
HANDLE OpenFromAgent(const std::wstring& id, DWORD expectedServerPid, ServerCheck verifyServer, const char** why);

// This process's parent process id (0 if it cannot be read).
DWORD ParentProcessId();

// r2 (Codex L2): the service's side of one job after AcceptOwnedChild -- send it, then serve the commit fence:
// on the agent's Ready, ask `fence` and answer Go(allow); then read its Answer. Bounded throughout.
enum class JobOutcome { Typed, TargetNotReady, InjectFailed, Denied, NoAnswer };
JobOutcome RunJob(HANDLE pipe, const Job& job, const std::function<bool()>& fence, DWORD readyMs, DWORD answerMs,
                  uint32_t* win32);
// r2: the agent's side -- say Ready for `requestId` and read the service's Go. True only on an allow for it.
bool AgentAskGo(HANDLE pipe, uint32_t requestId);
// The agent's final word.
void AgentAnswer(HANDLE pipe, uint32_t requestId, uint16_t stage, uint32_t win32);

}  // namespace remote60::native_poc::lock_cred_channel
