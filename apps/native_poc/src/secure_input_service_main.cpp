#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <userenv.h>
#include <tlhelp32.h>
#include <sas.h>
#pragma comment(lib, "sas.lib")
#include <wtsapi32.h>
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Bcrypt.lib")

#include <thread>
#include <vector>

#include "poc_protocol.hpp"
#include "sealed_unlock.hpp"
#include "secure_unlock_ipc.hpp"

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "secure_input_inject.hpp"
#include "secure_input_mapping.hpp"
#include "secure_input_protocol.hpp"
#include "secure_input_session.hpp"
#include "secure_input_diag_budget.hpp"
#include "logon_ui_target.hpp"  // lockscreen-cred r1: the lock-screen agent's target and typing
#include "lock_input_policy.hpp"  // lockscreen-cred r1 (A6): no plaintext characters to a locked session
#include "lock_cred_channel.hpp"  // lockscreen-cred r1: the service <-> agent private pipe
#include "trusted_service_pipe.hpp"  // lockscreen-cred r1: the agent checks its server

// Prelogin remote access (prelogin-service): the supervisor + console observation + machine credential.
#include "prelogin_child_launch.hpp"
#include "prelogin_user_host_launch.hpp"
#include "secure_input_desktop_fence.hpp"
#include "prelogin_user_host_policy.hpp"
#include "prelogin_sas_policy.hpp"
#include "prelogin_uninstall_codes.hpp"
#include "prelogin_console_probe.hpp"
#include "prelogin_machine_record.hpp"
#include "prelogin_machine_store.hpp"
#include "prelogin_admin_pipe.hpp"
#include "prelogin_reconcile_pacer.hpp"
#include "secure_input_pipe_server.hpp"
#include "secure_unlock_service.hpp"
#include "prelogin_owner_floor.hpp"
#include "prelogin_supervisor.hpp"
#include "prelogin_worker_launch.hpp"
#include "prelogin_worker_log.hpp"
#ifdef REMOTE60_SIS_TEST_SEAM
// prelogin r21 E1: the lifecycle test build only (GNLinkInputServiceLifecycleTest.exe) -- see the header.
#include "secure_input_service_test_seam.hpp"
#define SIS_TEST_NOTE(...) remote60::native_poc::sis_test::Note(__VA_ARGS__)
#else
#define SIS_TEST_NOTE(...) ((void)0)
#endif

namespace {

using remote60::native_poc::DesktopRect;
using remote60::native_poc::map_client_point;
using remote60::native_poc::InjectPlan;
using remote60::native_poc::InjectRun;
using remote60::native_poc::plan_input_event;
using remote60::native_poc::run_input_event;
using remote60::native_poc::SecureInputKind;
using remote60::native_poc::SecureInputMessage;
using remote60::native_poc::kInvalidSessionId;
using remote60::native_poc::kSecureInputMagic;
using remote60::native_poc::kSecureInputPipeName;
using remote60::native_poc::kSecureInputServiceName;
using remote60::native_poc::resolve_target_session;
using remote60::native_poc::session_source_name;

// A service has no console, so everything printed here went nowhere -- which is how the input
// path came to be undiagnosable: the host counts every event as delivered because the injection
// result is discarded, and the only component that knows better could not say so.
//
// %ProgramData% rather than the install directory: the service runs as LocalSystem and the
// install directory is deliberately admin-only, so writing beside the binary would either fail
// or hand a SYSTEM-writable file to a place that is supposed to be read-only at runtime.
std::wstring diag_log_path() {
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) return remote60::native_poc::sis_test::Env(L"REMOTE60_SIS_TEST_DIAG");
#endif
  wchar_t base[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"ProgramData", base, MAX_PATH) == 0) return {};
  std::wstring dir = std::wstring(base) + L"\\GNLink";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\secure_input.log";
}

void diag(const char* format, ...) {
  static const std::wstring path = diag_log_path();
  if (path.empty()) return;
  char line[1024]{};
  va_list args;
  va_start(args, format);
  _vsnprintf_s(line, _TRUNCATE, format, args);
  va_end(args);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"a") != 0 || !file) return;
  // Truncate rather than grow without bound; this is a diagnostic, not an audit trail.
  if (_ftelli64(file) > 2 * 1024 * 1024) {
    fclose(file);
    if (_wfopen_s(&file, path.c_str(), L"w") != 0 || !file) return;
  }
  fprintf(file, "%02d:%02d:%02d.%03d %s\n", now.wHour, now.wMinute, now.wSecond,
          now.wMilliseconds, line);
  fclose(file);
}

SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
SERVICE_STATUS gStatus{};
std::atomic<bool> gRunning{true};
// r14 M1: every instance of the secure-input pipe (listening or serving), so STOP can end them all; and
// the one agent behind them, shared by the connection threads under this lock.
remote60::native_poc::PipeConnectionRegistry gSecureInputPipes;
std::mutex gAgentMu;

// Set by the SCM SESSIONCHANGE handler (the service control-dispatcher thread) and consumed by the
// agent-owning main thread in ensure_agent. The handler must never touch gAgent itself -- see the
// handler for why. Signalling through atomics keeps gAgent off the SCM thread (Codex review #363); r14 M1:
// the connection threads that own it take gAgentMu.
std::atomic<bool> gSessionChangePending{false};
std::atomic<uint32_t> gPendingSessionEvent{0};
std::atomic<uint32_t> gPendingSessionId{0};
// r11 U2: bumped on every console connect / disconnect (the console's attachment changed); each connection
// keeps the value it connected at (r14 M1). A pre-login message is delivered only within the attachment its
// worker connected in (secure_input_desktop_fence.hpp LogonOnlyTargetSession).
std::atomic<uint32_t> gConsoleGeneration{0};
// Bumped on every session-topology change. The unlock path snapshots it at challenge issue and
// re-checks it before decrypt and before WTSConnectSession, so a password can never be injected into
// a session that changed mid-flight -- the semantic fence d9a2444's lazy reset does not give ordinary
// input. (Codex review #365.)
std::atomic<uint32_t> gSessionTopologyGeneration{0};
std::atomic<uint32_t> gUnlockJobCounter{0};

struct AgentProcess {
  HANDLE process = nullptr;
  HANDLE writePipe = nullptr;
  DWORD sessionId = 0xffffffffu;
  // The desktop the agent was created on. A thread can move between desktops but the process's
  // association cannot, and SendInput refuses when they disagree -- so a change here means the
  // agent has to be replaced, not redirected.
  std::wstring desktop;
};

AgentProcess gAgent;

bool write_exact(HANDLE handle, const void* data, DWORD bytes) {
  const auto* cursor = static_cast<const uint8_t*>(data);
  DWORD total = 0;
  while (total < bytes) {
    DWORD written = 0;
    if (!WriteFile(handle, cursor + total, bytes - total, &written, nullptr) || written == 0) {
      return false;
    }
    total += written;
  }
  return true;
}

bool read_exact(HANDLE handle, void* data, DWORD bytes) {
  auto* cursor = static_cast<uint8_t*>(data);
  DWORD total = 0;
  while (total < bytes) {
    DWORD read = 0;
    if (!ReadFile(handle, cursor + total, bytes - total, &read, nullptr) || read == 0) {
      return false;
    }
    total += read;
  }
  return true;
}

void stop_agent() {
  SIS_TEST_NOTE("stop_agent");
  if (gAgent.writePipe) {
    SecureInputMessage shutdown{};
    shutdown.kind = static_cast<uint16_t>(SecureInputKind::Shutdown);
    (void)write_exact(gAgent.writePipe, &shutdown, sizeof(shutdown));
    CloseHandle(gAgent.writePipe);
  }
  gAgent.writePipe = nullptr;
  if (gAgent.process) {
    if (WaitForSingleObject(gAgent.process, 1500) == WAIT_TIMEOUT) {
      // This is a child created and owned by this service. It cannot be allowed to remain as
      // an orphaned SYSTEM input agent after the broker stops or the console session changes.
      (void)TerminateProcess(gAgent.process, 0);
      (void)WaitForSingleObject(gAgent.process, 500);
    }
    CloseHandle(gAgent.process);
  }
  gAgent.process = nullptr;
  gAgent.sessionId = 0xffffffffu;
  gAgent.desktop.clear();
}

std::wstring current_executable_path() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  path.resize(length);
  return path;
}

bool start_agent(DWORD sessionId, const std::wstring& desktopName) {
  stop_agent();
  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  SECURITY_ATTRIBUTES inherit{};
  inherit.nLength = sizeof(inherit);
  inherit.bInheritHandle = TRUE;
  if (!CreatePipe(&readPipe, &writePipe, &inherit, 0)) return false;
  (void)SetHandleInformation(writePipe, HANDLE_FLAG_INHERIT, 0);

  HANDLE serviceToken = nullptr;
  HANDLE agentToken = nullptr;
  bool ok = OpenProcessToken(GetCurrentProcess(),
                             TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                                 TOKEN_ADJUST_SESSIONID,
                             &serviceToken) &&
            DuplicateTokenEx(serviceToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                             TokenPrimary, &agentToken) &&
            SetTokenInformation(agentToken, TokenSessionId, &sessionId, sizeof(sessionId));
  if (serviceToken) CloseHandle(serviceToken);
  if (!ok) {
    if (agentToken) CloseHandle(agentToken);
    CloseHandle(readPipe);
    CloseHandle(writePipe);
    return false;
  }

  const std::wstring exe = current_executable_path();
  wchar_t command[32768]{};
  _snwprintf_s(command, _TRUNCATE, L"\"%s\" --agent %llu", exe.c_str(),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(readPipe)));
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  // Create the agent on the desktop it will inject into, rather than starting it on Default and
  // moving a thread there afterwards.
  //
  // Measured: with the agent created on Default and SetThreadDesktop'd onto Winlogon, attach
  // succeeded and SetCursorPos worked -- the cursor moved -- but every button event came back
  // from SendInput with ERROR_ACCESS_DENIED. Mouse moves appeared to work only because they
  // return after SetCursorPos without calling SendInput at all, which is what made the failures
  // look like they alternated.
  //
  // A thread can change desktops; the process's association is fixed when it is created, and
  // that is what the input path checks. So the desktop is chosen up front and the agent is
  // recreated when the input desktop changes.
  std::wstring desktopSpec = L"winsta0\\" + desktopName;
  startup.lpDesktop = desktopSpec.data();
  PROCESS_INFORMATION process{};
  ok = CreateProcessAsUserW(agentToken, nullptr, command, nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                            &startup, &process) != FALSE;
  CloseHandle(agentToken);
  CloseHandle(readPipe);
  if (!ok) {
    CloseHandle(writePipe);
    return false;
  }
  CloseHandle(process.hThread);
  gAgent.process = process.hProcess;
  gAgent.writePipe = writePipe;
  gAgent.sessionId = sessionId;
  gAgent.desktop = desktopName;
  return true;
}

// ------------------------------------------------------------------ lock-screen credential agent
// lockscreen-cred r1 (Codex A4 + codex_lockcred_agent_r1.md). The typing of a lock-screen credential is NOT a
// message on the input agent's pipe: the input pipe carries only InputEvent/InputText, and nothing a host
// sends there can become a credential job. The unlock thread starts a ONE-SHOT child of this service -- the
// installed executable named explicitly, the service's own SYSTEM token moved into the target session, on that
// session's Winlogon desktop, NO inherited handles, suspended, in a kill-on-close job, then resumed -- and
// hands it the job over a private one-shot named pipe this service created first (lock_cred_channel.hpp):
// sent only after the client is shown to be that very child (held process handle: pid, running, session,
// SYSTEM, its Hello), while the child talks only to a server that is the trusted service and its parent. The
// child checks the target (logon_ui_target.hpp), replaces the field's content once, answers, zeroes, exits.
// The password is never on a command line, in an environment, a file or a log; nothing falls back to them.
constexpr DWORD kLockCredConnectMs = 5000;   // the child's start and connect
constexpr DWORD kLockCredAnswerMs = 10000;   // its target wait (3 s) and the typing
constexpr DWORD kLockCredExitMs = 2000;

remote60::native_poc::UnlockStage type_lock_cred(uint32_t session, const uint16_t* units, uint16_t count,
                                                 uint32_t* win32) {
  using remote60::native_poc::UnlockStage;
  namespace lc = remote60::native_poc::lock_cred_channel;
  *win32 = 0;
  if (count > remote60::native_poc::sealed_unlock::kMaxPasswordUtf16) return UnlockStage::InjectFailed;
  const wchar_t* sddl = L"D:P(A;;GA;;;SY)";  // SYSTEM alone
  bool requireSystem = true;
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) {
    sddl = remote60::native_poc::sis_test::Sddl();  // TEST BUILD ONLY: this user too
    requireSystem = false;
  }
#endif
  std::wstring id;
  uint32_t pipeErr = 0;
  HANDLE pipe = lc::CreateServerPipe(sddl, &id, &pipeErr);
  if (pipe == INVALID_HANDLE_VALUE) {
    *win32 = pipeErr;
    diag("lock-cred: the private pipe could not be created (win32=%u) -> not typed", pipeErr);
    return UnlockStage::InjectFailed;
  }
  const std::wstring exe = current_executable_path();
  std::wstring cmd = L"\"" + exe + L"\" --lockcred-agent " + id;  // the mode and the pipe's id: nothing secret
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  std::wstring desktopSpec = L"winsta0\\Winlogon";
  PROCESS_INFORMATION process{};
  bool ok = false;
  DWORD childSession = session;
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) {
    // TEST BUILD ONLY: no SYSTEM token to move into a session -- the child runs as this user, in this session,
    // on this desktop; its target is the test's window by title (run_lock_cred_agent).
    ok = CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup,
                        &process) != FALSE;
    ProcessIdToSessionId(GetCurrentProcessId(), &childSession);
    SIS_TEST_NOTE("lock-cred agent started (test build: this user, this session, this desktop) ok=%d", ok ? 1 : 0);
  } else
#endif
  {
    HANDLE serviceToken = nullptr, agentToken = nullptr;
    DWORD sessionId = session;
    ok = OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY | TOKEN_ADJUST_SESSIONID, &serviceToken) &&
         DuplicateTokenEx(serviceToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &agentToken) &&
         SetTokenInformation(agentToken, TokenSessionId, &sessionId, sizeof(sessionId));
    if (serviceToken) CloseHandle(serviceToken);
    if (ok) {
      startup.lpDesktop = desktopSpec.data();
      ok = CreateProcessAsUserW(agentToken, exe.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                                CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                                &startup, &process) != FALSE;
    }
    if (agentToken) CloseHandle(agentToken);
  }
  if (!ok) {
    *win32 = GetLastError();
    CloseHandle(pipe);
    diag("lock-cred agent start failed win32=%u session=%u", *win32, session);
    return UnlockStage::InjectFailed;
  }
  // Into a kill-on-close job before it runs a single instruction -- or it does not run at all.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  const bool jobbed = job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
                      AssignProcessToJobObject(job, process.hProcess);
  if (!jobbed) {
    *win32 = GetLastError();
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, kLockCredExitMs);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job) CloseHandle(job);
    CloseHandle(pipe);
    diag("lock-cred agent not resumed: no job (win32=%u)", *win32);
    return UnlockStage::InjectFailed;
  }
  ResumeThread(process.hThread);
  CloseHandle(process.hThread);

  UnlockStage stage = UnlockStage::InjectFailed;
  const char* why = "";
  if (lc::AcceptOwnedChild(pipe, process.hProcess, process.dwProcessId, childSession, requireSystem, kLockCredConnectMs, &why)) {
    lc::Job jobMsg{};
    jobMsg.session = childSession;
    jobMsg.count = count;
    for (uint16_t i = 0; i < count; ++i) jobMsg.units[i] = units[i];
    const bool sent = lc::WriteBounded(pipe, &jobMsg, sizeof(jobMsg), kLockCredConnectMs);
    SecureZeroMemory(&jobMsg, sizeof(jobMsg));
    lc::Answer answer{};
    if (sent && lc::ReadBounded(pipe, &answer, sizeof(answer), kLockCredAnswerMs) && answer.magic == lc::kAnswerMagic &&
        answer.size == sizeof(answer)) {
      stage = static_cast<UnlockStage>(answer.stage);
      *win32 = answer.win32;
      if (stage != UnlockStage::Injected && stage != UnlockStage::TargetNotReady) stage = UnlockStage::InjectFailed;
    } else {
      diag("lock-cred agent gave no answer session=%u", session);
    }
  } else {
    diag("lock-cred: the pipe's client was refused (%s); nothing sent", why);
  }
  CloseHandle(pipe);
  // Its own exit, bounded; then the job closes, which ends it if it has not -- it types nothing after that.
  WaitForSingleObject(process.hProcess, kLockCredExitMs);
  CloseHandle(process.hProcess);
  CloseHandle(job);
  return stage;
}

// True when `session` is actively connected (WTSActive). *known=false when the state could not be
// queried -- the resolver then treats the requester as not-active so a valid console wins, which is
// the safe choice (never inject into a stale, disconnected session). The service runs as LocalSystem
// and is the session authority, so it asks WTS directly rather than trusting a value over the pipe.
// (Codex review #362.)
bool query_session_active(uint32_t session, bool* known) {
  *known = false;
  if (session == kInvalidSessionId || session == 0u) return false;
  LPWSTR buffer = nullptr;
  DWORD bytes = 0;
  bool active = false;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSConnectState, &buffer,
                                  &bytes) &&
      buffer && bytes >= sizeof(WTS_CONNECTSTATE_CLASS)) {
    const WTS_CONNECTSTATE_CLASS state = *reinterpret_cast<const WTS_CONNECTSTATE_CLASS*>(buffer);
    *known = true;
    active = (state == WTSActive);
  }
  if (buffer) WTSFreeMemory(buffer);
  return active;
}

// Resolves the session the agent belongs in and says why, once per change. Silence here is what
// let the wrong-session bug live: the write succeeded, so everything downstream looked healthy.
DWORD target_session(uint32_t requester) {  // r14 M1: the connection's own requester
  bool requesterKnown = false;
  const bool requesterActive = query_session_active(requester, &requesterKnown);
  const DWORD console = WTSGetActiveConsoleSessionId();
  const auto choice =
      resolve_target_session(requester, requesterKnown, requesterActive, console);
  static uint32_t reported = kInvalidSessionId;
  static remote60::native_poc::SessionSource reportedSource = remote60::native_poc::SessionSource::None;
  if (choice.sessionId != reported || choice.source != reportedSource) {
    reported = choice.sessionId;
    reportedSource = choice.source;
    diag("target session=%u source=%s (requester=%u known=%d active=%d console=%lu)",
         choice.sessionId, session_source_name(choice.source), requester,
         requesterKnown ? 1 : 0, requesterActive ? 1 : 0, console);
  }
  return choice.sessionId;
}

// The desktop currently receiving input -- "Default" ordinarily, "Winlogon" while a consent
// prompt or the lock screen is up. The agent has to be created on this one, so it is read here
// rather than left for the agent to discover after the fact.
//
// A failure to open it is itself the answer: only the secure desktop refuses, so that is what it
// must be. Guessing "Default" there would put the agent exactly where it cannot inject.
std::wstring current_input_desktop_name() {
  HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (!desktop) return L"Winlogon";
  wchar_t name[64]{};
  DWORD needed = 0;
  const bool ok = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed) != 0;
  CloseDesktop(desktop);
  return ok ? name : L"Default";
}

bool ensure_agent(DWORD sessionId) {
  // Consume any session change signalled by the SCM handler here, on the agent-owning thread. The
  // handler only sets a flag because it runs on the SCM dispatcher thread while this thread owns
  // gAgent; tearing the agent down there raced with our WriteFile/start_agent and use-after-closed
  // the handles once SERVICE_ACCEPT_SESSIONCHANGE was advertised. (Codex review #363.)
  if (gSessionChangePending.exchange(false, std::memory_order_acq_rel)) {
    diag("session change consumed event=%lu session=%lu (agent session=%u)",
         gPendingSessionEvent.load(std::memory_order_relaxed),
         gPendingSessionId.load(std::memory_order_relaxed), gAgent.sessionId);
    if (gAgent.process) stop_agent();
  }
  if (sessionId == kInvalidSessionId) return false;
  const std::wstring desktop = current_input_desktop_name();
  if (gAgent.process && gAgent.writePipe && gAgent.sessionId == sessionId &&
      gAgent.desktop == desktop && WaitForSingleObject(gAgent.process, 0) == WAIT_TIMEOUT) {
    return true;
  }
  if (gAgent.process && gAgent.desktop != desktop) {
    char narrowFrom[64]{};
    char narrowTo[64]{};
    size_t converted = 0;
    wcstombs_s(&converted, narrowFrom, gAgent.desktop.c_str(), _TRUNCATE);
    wcstombs_s(&converted, narrowTo, desktop.c_str(), _TRUNCATE);
    diag("input desktop changed %s -> %s, recreating the agent there", narrowFrom, narrowTo);
  }
  return start_agent(sessionId, desktop);
}

// r11 U2: a pre-login message's only possible target -- its worker's session, still the console, within
// the console attachment it connected in. No fallback.
DWORD logon_only_target_session(uint32_t requester, uint32_t consoleGenerationAtConnect) {
  const DWORD target = remote60::native_poc::LogonOnlyTargetSession(
      requester, WTSGetActiveConsoleSessionId(), consoleGenerationAtConnect,
      gConsoleGeneration.load(std::memory_order_acquire));
  return target;
}

// r14 M1: one connection's message -- its requester and console attachment as that connection learned them
// at connect. Called with gAgentMu held (the agent is shared by every connection).
remote60::native_poc::ForwardOutcome forward_to_agent(const SecureInputMessage& message, uint32_t requester,
                                                      uint32_t consoleGenerationAtConnect) {
  return remote60::native_poc::ForwardSecureInput(
      message, [requester] { return static_cast<uint32_t>(target_session(requester)); },
      [requester, consoleGenerationAtConnect] {
        return static_cast<uint32_t>(logon_only_target_session(requester, consoleGenerationAtConnect));
      },
      [](uint32_t session) { return ensure_agent(session); },
      [](uint32_t session) { return start_agent(session, current_input_desktop_name()); },
      [](const SecureInputMessage& m) { return write_exact(gAgent.writePipe, &m, sizeof(m)); });
}

void report_service_status(DWORD state, DWORD error = NO_ERROR) {
  gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  gStatus.dwCurrentState = state;
  gStatus.dwControlsAccepted =
      state == SERVICE_RUNNING ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE) : 0;
  gStatus.dwWin32ExitCode = error;
  gStatus.dwCheckPoint = 0;
  gStatus.dwWaitHint = 0;
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) {  // r21 E1: the SCM boundary -- every report is printed, none is sent
    remote60::native_poc::sis_test::Status(state, error);
    return;
  }
#endif
  if (gStatusHandle) SetServiceStatus(gStatusHandle, &gStatus);
}

DWORD WINAPI service_control(DWORD control, DWORD eventType, LPVOID eventData, LPVOID) {
  // Session changes matter because the agent lives inside one. A session that logs off or
  // disconnects takes the agent's desktop with it, and continuing to write to that agent is how
  // input silently goes nowhere.
  if (control == SERVICE_CONTROL_SESSIONCHANGE) {
    // HandlerEx contract: record and return immediately -- no blocking, no file I/O. Crucially, do
    // NOT touch gAgent here. gAgent is owned by the service's main thread (ensure_agent / forward /
    // stop_agent); calling CloseHandle/TerminateProcess from this SCM dispatcher thread while the
    // main thread is mid WriteFile or start_agent is a data race and a handle use-after-close. That
    // was the live bug b0d8d27 created by advertising SERVICE_ACCEPT_SESSIONCHANGE (before that the
    // handler never ran). Just flag it; ensure_agent consumes it on the owning thread and re-resolves
    // the target (a lock raises Winlogon, a reconnect moves the session; both need a re-resolve, and
    // target_session now reads the requester's connect state). (Codex review #362, #363.)
    if (eventType == WTS_SESSION_LOGOFF || eventType == WTS_SESSION_LOGON ||
        eventType == WTS_CONSOLE_CONNECT || eventType == WTS_CONSOLE_DISCONNECT ||
        eventType == WTS_REMOTE_CONNECT || eventType == WTS_REMOTE_DISCONNECT ||
        eventType == WTS_SESSION_LOCK || eventType == WTS_SESSION_UNLOCK) {
      const auto* notification = static_cast<const WTSSESSION_NOTIFICATION*>(eventData);
      gPendingSessionEvent.store(eventType, std::memory_order_relaxed);
      gPendingSessionId.store(notification ? notification->dwSessionId : 0u,
                              std::memory_order_relaxed);
      gSessionTopologyGeneration.fetch_add(1, std::memory_order_release);
      if (eventType == WTS_CONSOLE_CONNECT || eventType == WTS_CONSOLE_DISCONNECT) {
        gConsoleGeneration.fetch_add(1, std::memory_order_release);  // r11 U2
      }
      gSessionChangePending.store(true, std::memory_order_release);
    }
    return NO_ERROR;
  }
  if (control != SERVICE_CONTROL_STOP) return NO_ERROR;
  report_service_status(SERVICE_STOP_PENDING);
  gRunning.store(false, std::memory_order_release);
  // r14 M1: every secure-input pipe instance's I/O ends (each is closed by the thread that owns it).
  gSecureInputPipes.Stop();
  // The unlock worker is woken by service_main's shutdown (CancelSynchronousIo); the STOP handler
  // must stay short and must not touch the worker's pipe. (Codex #370 BLOCKER A.)
  return NO_ERROR;
}

// The process on the other end of the pipe is the streaming host, so its session is the one whose
// desktop the operator is actually looking at.
uint32_t requester_session_of(HANDLE pipe) {
  ULONG pid = 0;
  if (!GetNamedPipeClientProcessId(pipe, &pid) || pid == 0) return kInvalidSessionId;
  DWORD session = 0;
  if (!ProcessIdToSessionId(pid, &session)) return kInvalidSessionId;
  return session;
}

// r21 E1: the pipes' names and DACL, and whether OS effects are allowed -- the product's own, or the lifecycle
// test build's (secure_input_service_test_seam.hpp). Without REMOTE60_SIS_TEST_SEAM these are the constants.
const wchar_t* service_pipe_sddl() {
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) return remote60::native_poc::sis_test::Sddl();
#endif
  return L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
}
const wchar_t* secure_input_pipe_name() {
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) return remote60::native_poc::sis_test::SecureInputPipe();
#endif
  return kSecureInputPipeName;
}
const wchar_t* unlock_pipe_name() {
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) return remote60::native_poc::sis_test::UnlockPipe();
#endif
  return remote60::native_poc::kSecureUnlockPipeName;
}
const wchar_t* admin_pipe_name() {
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) return remote60::native_poc::sis_test::AdminPipe();
#endif
  return remote60::native_poc::prelogin::kPreloginAdminPipeName;
}
bool os_effects_blocked_for_test() {
#ifdef REMOTE60_SIS_TEST_SEAM
  return remote60::native_poc::sis_test::Active();
#else
  return false;
#endif
}

HANDLE create_secure_pipe(bool firstInstance) {
  // r14 M1: one instance per client (secure_input_pipe_server.hpp). secure-pipe-identity (r20 merge): the
  // server's first instance is FILE_FLAG_FIRST_PIPE_INSTANCE -- a name squatted while the service was stopped
  // fails the create and the service does not join it (fail closed); the SY/BA DACL + PIPE_REJECT_REMOTE_CLIENTS
  // are unchanged, and the server keeps one of its instances on the name for as long as it runs.
#ifdef REMOTE60_SIS_TEST_SEAM
  DWORD injected = NO_ERROR;
  if (remote60::native_poc::sis_test::Active() && remote60::native_poc::sis_test::FailThisCreate(&injected)) {
    SetLastError(injected);
    return INVALID_HANDLE_VALUE;
  }
#endif
  return remote60::native_poc::CreateSecureInputPipeInstance(secure_input_pipe_name(), service_pipe_sddl(), firstInstance);
}

// ================= Sealed unlock (SYSTEM side) ==================================================
// Runs on its own thread over a dedicated DUPLEX pipe, separate from the fire-and-forget input pipe.
// This thread is the sole owner of the unlock crypto/challenge state and is where WTSConnectSession
// (which can block) runs -- never the SCM handler or the input path. (Codex review #365/#366.)

namespace {

uint64_t now_ms() { return GetTickCount64(); }

uint16_t stage_code(remote60::native_poc::UnlockStage s) { return static_cast<uint16_t>(s); }

// Stable-ish host id (computer name hash). Only needs to be consistent within a session pair; the
// client echoes it in the AAD.
uint64_t host_id() {
  wchar_t name[256]{};
  DWORD n = 256;
  uint64_t h = 1469598103934665603ull;
  if (GetComputerNameW(name, &n)) {
    for (DWORD i = 0; i < n; ++i) { h ^= static_cast<uint16_t>(name[i]); h *= 1099511628211ull; }
  }
  return h;
}

// FNV-1a over WTSUserName+WTSDomainName so a stored credential is bound to the account, not the
// session number.
uint64_t account_id_of(uint32_t session, bool* known) {
  if (known) *known = false;
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](LPWSTR str) {
    if (!str) return;
    for (LPWSTR p = str; *p; ++p) { h ^= static_cast<uint16_t>(*p); h *= 1099511628211ull; }
  };
  LPWSTR buf = nullptr;
  DWORD bytes = 0;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSUserName, &buf, &bytes) && buf) {
    if (buf[0] && known) *known = true;
    mix(buf);
    WTSFreeMemory(buf);
  }
  buf = nullptr;
  bytes = 0;
  h ^= 0x5eULL; h *= 1099511628211ull;  // separator between user and domain (Codex #370)
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSDomainName, &buf, &bytes) && buf) {
    mix(buf);
    WTSFreeMemory(buf);
  }
  return h;
}

// True when `session` is locked (secure LogonUI), distinct from a UAC/CAD secure desktop. Win11, so
// the historic Win7 reversed-flag quirk does not apply. *known=false if the query failed.
bool session_is_locked(uint32_t session, bool* known) {
  *known = false;
  if (session == kInvalidSessionId) return false;
#ifdef REMOTE60_SIS_TEST_SEAM
  // TEST BUILD ONLY (lockscreen-cred e2e): this user's session stands in for a locked console session --
  // "locked" until the test signals REMOTE60_SIS_TEST_UNLOCK_EVENT, which is then a session event (the topology
  // moves once) and "unlocked". Here, not on the unlock primitives alone, so the unlock path and the input
  // filter (A6) see the same answer. Connect state and console are the real ones.
  if (remote60::native_poc::sis_test::Active() && !remote60::native_poc::sis_test::Env(L"REMOTE60_SIS_TEST_FAKE_LOCK").empty()) {
    static HANDLE unlockEvent = [] {
      const std::wstring name = remote60::native_poc::sis_test::Env(L"REMOTE60_SIS_TEST_UNLOCK_EVENT");
      return name.empty() ? nullptr : CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    }();
    static std::atomic<bool> bumped{false};
    DWORD mine = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &mine);
    if (session == mine) {
      *known = true;
      const bool unlocked = unlockEvent && WaitForSingleObject(unlockEvent, 0) == WAIT_OBJECT_0;
      if (unlocked && !bumped.exchange(true)) gSessionTopologyGeneration.fetch_add(1, std::memory_order_release);
      return !unlocked;
    }
  }
#endif
  WTSINFOEXW* info = nullptr;
  DWORD bytes = 0;
  bool locked = false;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSSessionInfoEx,
                                  reinterpret_cast<LPWSTR*>(&info), &bytes) &&
      info && bytes >= sizeof(WTSINFOEXW) && info->Level == 1) {
    *known = true;
    locked = (info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK);
  }
  if (info) WTSFreeMemory(info);
  return locked;
}

// lockscreen-cred r1 (Codex A6): a plaintext character (text, or a non-navigation key down) to a session that
// is -- or must be treated as -- locked is not forwarded. Counted; said once per kind of refusal, then every
// 200th (no key code, no text).
std::atomic<uint64_t> gLockedPlaintextRefused{0};
bool refuse_locked_plaintext(const SecureInputMessage& message, uint32_t requester) {
  namespace li = remote60::native_poc::lock_input;
  const bool text = message.kind == static_cast<uint16_t>(SecureInputKind::InputText);
  if (!text && !li::IsCharacterKey(message.eventKind, message.keyCode)) return false;  // pointer, nav, key up
  bool known = false;
  const bool locked = session_is_locked(requester, &known);
  const bool defaultDesktop = _wcsicmp(current_input_desktop_name().c_str(), L"Default") == 0;
  const bool treat = li::TreatAsLocked(known, locked, defaultDesktop);
  if (!(text ? li::RefuseText(treat) : li::RefuseEvent(treat, message.eventKind, message.keyCode))) return false;
  const uint64_t n = gLockedPlaintextRefused.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n == 1 || n % 200 == 0) {
    diag("secure input: plaintext %s to a locked session refused (requester=%u lockKnown=%d locked=%d desktopDefault=%d total=%llu)",
         text ? "text" : "key", requester, known ? 1 : 0, locked ? 1 : 0, defaultDesktop ? 1 : 0,
         static_cast<unsigned long long>(n));
  }
  return true;
}


using remote60::native_poc::SecureUnlockChallengeRequest;
using remote60::native_poc::SecureUnlockChallengeResponse;
using remote60::native_poc::SecureUnlockSealedRequest;
using remote60::native_poc::SecureUnlockResult;
using remote60::native_poc::UnlockStage;
namespace su = remote60::native_poc::sealed_unlock;

HANDLE create_unlock_pipe() {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          service_pipe_sddl(), SDDL_REVISION_1, &descriptor, nullptr)) {
    return INVALID_HANDLE_VALUE;
  }
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.lpSecurityDescriptor = descriptor;
  // FILE_FLAG_FIRST_PIPE_INSTANCE (secure-pipe-identity): see create_secure_pipe. A preempted name fails
  // closed here rather than the service joining a squatter that could seal the viewer's password.
  HANDLE pipe = CreateNamedPipeW(
      unlock_pipe_name(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1, 8 * 1024, 8 * 1024, 0, &security);
  LocalFree(descriptor);
  return pipe;
}

// r15 R14-2: the sealed unlock's decisions live in secure_unlock_service.cpp; these are the real calls.
namespace sus = remote60::native_poc::sealed_unlock_service;

const sus::UnlockPrimitives& unlock_primitives() {
  static const sus::UnlockPrimitives p = [] {
    sus::UnlockPrimitives q;
    q.sessionIsLocked = [](uint32_t session, bool* known) { return session_is_locked(session, known); };
    q.sessionIsActive = [](uint32_t session, bool* known) { return query_session_active(session, known); };
    q.consoleSession = [] { return static_cast<uint32_t>(WTSGetActiveConsoleSessionId()); };
    q.accountIdOf = [](uint32_t session, bool* known) { return account_id_of(session, known); };
    q.hostId = [] { return host_id(); };
    q.topologyGeneration = [] { return gSessionTopologyGeneration.load(std::memory_order_acquire); };
    q.nowMs = [] { return now_ms(); };
    q.wtsConnect = [](uint32_t requester, uint32_t console, const wchar_t* pw, DWORD* gle) {
      SetLastError(0);
      const BOOL ok = WTSConnectSessionW(requester, console, const_cast<wchar_t*>(pw), FALSE);
      *gle = GetLastError();
      return ok != FALSE;
    };
    q.running = [] { return gRunning.load(std::memory_order_acquire); };
    q.sleep = [](uint32_t ms) { Sleep(ms); };
    q.log = [](const char* line) { diag("%s", line); };
    // lockscreen-cred r1: the lock-screen typing, by a one-shot child of this service (type_lock_cred).
    q.typeLockCred = [](uint32_t session, const uint16_t* units, uint16_t count, uint32_t* win32) {
      return type_lock_cred(session, units, count, win32);
    };
    return q;
  }();
  return p;
}

// The process on the other end of the unlock pipe: id, creation time, session -- read once at connect.
sus::UnlockCaller unlock_caller_of(HANDLE pipe) {
  sus::UnlockCaller c;
  ULONG pid = 0;
  if (!GetNamedPipeClientProcessId(pipe, &pid) || pid == 0) return c;
  DWORD session = 0;
  if (!ProcessIdToSessionId(pid, &session)) return c;
  HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) return c;
  FILETIME cr{}, ex{}, k{}, u{};
  const bool timed = GetProcessTimes(h, &cr, &ex, &k, &u) != FALSE;
  if (!timed) {
    CloseHandle(h);
    return c;
  }
  c.processId = pid;
  c.processStartTime = (static_cast<uint64_t>(cr.dwHighDateTime) << 32) | cr.dwLowDateTime;
  c.session = session;
  c.process = h;  // r16 R15-3: held for the connection's life (the caller closes it)
  return c;
}

void unlock_pipe_loop() {
  // ONE owner handle for the worker's life (secure-pipe-identity): created FIRST so a squatted name
  // fails closed HERE (unlock disabled) instead of the worker spinning to re-take a name someone else
  // owns, and reused across connections so the name is owned without a gap while the service runs.
  HANDLE pipe = create_unlock_pipe();
  if (pipe == INVALID_HANDLE_VALUE) {
    diag("unlock pipe create failed (name preempted or error) -> unlock disabled, fail closed");
    return;
  }
  while (gRunning.load(std::memory_order_acquire)) {
    // ConnectNamedPipe blocks; shutdown wakes it with CancelSynchronousIo on this thread, which
    // returns ERROR_OPERATION_ABORTED. Re-check gRunning after it returns, however it returned.
    const BOOL connected =
        ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    if (!gRunning.load(std::memory_order_acquire)) break;
    if (!connected) break;  // a connect error that is not shutdown -> stop serving (fail closed)
    if (connected) {
      sus::UnlockSession us;
      // r15 R14-2: this connection's caller, from the pipe -- and the same process must still be on it at
      // the irreversible step.
      us.caller = unlock_caller_of(pipe);
      // r16 R15-3: the very process connected at the start is still running (its own handle, held since)
      // and is still the one on this connection.
      // r17 R16-2: and the connection itself is not broken (a client that closed only its handle).
      us.callerStillThere = [pipe, &us] { return sus::UnlockConnectionAlive(pipe, us.caller); };
      const sus::Write write = [pipe](const void* d, size_t n) { (void)write_exact(pipe, d, static_cast<DWORD>(n)); };
      uint8_t buf[512];
      while (gRunning.load(std::memory_order_acquire)) {
        DWORD read = 0;
        if (!ReadFile(pipe, buf, sizeof(buf), &read, nullptr) || read < 8) break;
        uint32_t magic = 0;
        std::memcpy(&magic, buf, 4);
        if (magic != remote60::native_poc::kSecureUnlockMagic) break;
        uint16_t size = 0, kind = 0;
        std::memcpy(&size, buf + 4, 2);
        std::memcpy(&kind, buf + 6, 2);
        if (size != read) break;
        if (kind == static_cast<uint16_t>(remote60::native_poc::SecureUnlockKind::ChallengeRequest) &&
            read == sizeof(SecureUnlockChallengeRequest)) {
          SecureUnlockChallengeRequest req{};
          std::memcpy(&req, buf, sizeof(req));
          sus::HandleChallengeRequest(us, req, unlock_primitives(), write);
        } else if (kind == static_cast<uint16_t>(remote60::native_poc::SecureUnlockKind::LockCredChallengeRequest) &&
                   read == sizeof(SecureUnlockChallengeRequest)) {  // lockscreen-cred r1
          SecureUnlockChallengeRequest req{};
          std::memcpy(&req, buf, sizeof(req));
          sus::HandleLockCredChallengeRequest(us, req, unlock_primitives(), write);
        } else if (kind == static_cast<uint16_t>(remote60::native_poc::SecureUnlockKind::CapabilitiesRequest) &&
                   read == sizeof(remote60::native_poc::SecureUnlockQuery)) {  // lockscreen-cred r1
          remote60::native_poc::SecureUnlockQuery q{};
          std::memcpy(&q, buf, sizeof(q));
          sus::HandleCapabilities(q, unlock_primitives(), write);
        } else if (kind == static_cast<uint16_t>(remote60::native_poc::SecureUnlockKind::StatusRequest) &&
                   read == sizeof(remote60::native_poc::SecureUnlockQuery)) {  // lockscreen-cred r1
          remote60::native_poc::SecureUnlockQuery q{};
          std::memcpy(&q, buf, sizeof(q));
          sus::HandleStatusRequest(us, q, unlock_primitives(), write);
        } else if (kind == static_cast<uint16_t>(remote60::native_poc::SecureUnlockKind::SealedRequest) &&
                   read == sizeof(SecureUnlockSealedRequest)) {
          SecureUnlockSealedRequest req{};
          std::memcpy(&req, buf, sizeof(req));
          sus::HandleSealedRequest(us, req, gUnlockJobCounter.fetch_add(1, std::memory_order_relaxed) + 1,
                                   unlock_primitives(), write);
          su::SecureZero(&req, sizeof(req));
        } else {
          break;  // unknown/malformed
        }
      }
      (void)DisconnectNamedPipe(pipe);  // ready the SAME instance for the next client; name stays owned
      if (us.caller.process) CloseHandle(us.caller.process);  // r16 R15-3
    }
  }
  CloseHandle(pipe);  // the worker exclusively owns this pipe
}

}  // namespace

// ------------------------------------------------------------------ prelogin supervisor (feature-gated)
//
// This whole block is DORMANT unless the machine credential record exists and enables prelogin, so an
// existing deployment with the feature off sees exactly the previous secure-input behaviour. When on, a
// bounded reconcile thread observes the physical console and drives PreloginSupervisor: it launches the
// SYSTEM GNLinkStream --prelogin worker on the console's input desktop when no user is logged on, and
// hands off to the user's own GNLinkHost at login (replacing the elevated HKCU Run that never ran before
// login). The pure decision logic is unit-tested (prelogin_supervisor_test); the live WTS/token/child
// Win32 here is exercised under SYSTEM in the elevated G-phase.

using namespace remote60::native_poc::prelogin;

HANDLE gPreloginWorker = nullptr;  // GNLinkStream.exe --prelogin, SYSTEM, console input desktop
DWORD gPreloginWorkerSession = 0xFFFFFFFFu;  // the console session that worker was launched for (S5 identity)
HANDLE gUserHostProcess = nullptr; // GNLinkHost.exe --tray, launched for the logged-on user at handoff
DWORD gUserHostSession = 0xFFFFFFFFu;        // the session that host was launched for (S5 identity)
// prelogin-ui r2 (item 7) + r6 G9, combined in the r5 merge: the user Host's ready arrives over the admin
// pipe from this install's GNLinkStream (image checked by the server), with its session, process start
// time and the per-launch nonce this service injected into the Host it started. It completes the handoff
// only for THIS launch: the console session, a host started after the launch, and the launch's nonce --
// not a named event any admin process could set (prelogin_admin_pipe.hpp UserHostReadyTracker).
remote60::native_poc::prelogin::UserHostReadyTracker gUserHostReady;
std::string narrow_nonce(const std::wstring& w) { return std::string(w.begin(), w.end()); }
std::atomic<uint64_t> gUserHostLaunchTime{0};
// Per-launch readiness nonce (G9): the service injects this into the user Host it starts, and ONLY that
// host can signal the ready event the service waits on -- a foreign/stale GNLinkStream cannot.
std::wstring gUserHostReadyNonce;
// All launched children join this Job with KILL_ON_JOB_CLOSE, so a SYSTEM worker can never outlive the
// service: if the service process dies for any reason, closing its last handle to the job kills them
// (Codex S5 lifetime boundary). Created lazily on the first launch.
HANDLE gChildJob = nullptr;

HANDLE ensure_child_job() {
  if (gChildJob) return gChildJob;
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) return nullptr;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
  info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
    CloseHandle(job);
    return nullptr;
  }
  gChildJob = job;
  return gChildJob;
}

std::wstring sibling_exe_path(const wchar_t* name) {
  const std::wstring self = current_executable_path();
  const size_t slash = self.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  return self.substr(0, slash + 1) + name;
}

// ASCII, to match how the directory client stores machineId in the record (RegGetValueA MachineGuid).
std::string current_machine_guid() {
  char buf[128] = {};
  DWORD cb = sizeof(buf);
  if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", "MachineGuid",
                   RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &cb) != ERROR_SUCCESS) {
    return {};
  }
  return std::string(buf);
}

std::wstring machine_record_path() {
  wchar_t programData[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH) == 0) return {};
  return std::wstring(programData) + L"\\GNLink\\credential\\machine.cred";
}

// The durable owner-generation FLOOR, kept OUTSIDE the record (HKLM, SYSTEM/Admins) so it cannot be
// rolled back by restoring an old on-disk blob (Codex S2). Disable/revoke bumps it above the current
// record's generation; a restored older-generation record then validates as Revoked. Default 0.
// The read primitive (with error-vs-absent distinction) is shared with the --prelogin worker in
// prelogin_owner_floor.hpp (G6); kOwnerFloorKey/kOwnerFloorValue come from there.

// Raise the floor to `floor` (monotonic; never lowers). Called by the disable/revoke path so a later
// restore of an old enabled blob is rejected. Returns true on success.
bool raise_prelogin_owner_floor(uint64_t floor) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kOwnerFloorKey, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr,
                      &key, nullptr) != ERROR_SUCCESS) {
    return false;
  }
  uint64_t cur = 0;
  DWORD cb = sizeof(cur);
  RegGetValueW(HKEY_LOCAL_MACHINE, kOwnerFloorKey, kOwnerFloorValue, RRF_RT_REG_QWORD, nullptr, &cur, &cb);
  bool ok = true;
  if (floor > cur) {
    ok = RegSetValueExW(key, kOwnerFloorValue, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&floor),
                        sizeof(floor)) == ERROR_SUCCESS;
  }
  RegCloseKey(key);
  return ok;
}

// Reads + validates the machine record to decide whether prelogin is enabled on this box RIGHT NOW. The
// revoke floor comes from the durable external store, NOT the record itself, so an OFF/revoked state and
// a restored old blob are both rejected. Returns false (feature off) on any failure -- never fails open.
bool prelogin_feature_enabled() {
  if (os_effects_blocked_for_test()) return false;  // r21 E1 test build: the machine's record is not read
  // r9 R1/R2: a revoke still owed (or a mark that cannot be read) means off, whatever the record says.
  if (remote60::native_poc::prelogin::ReadPreloginRevokePending() != 0) return false;
  const std::wstring path = machine_record_path();
  if (path.empty()) return false;
  MachineRecord rec;
  uint32_t win32 = 0;
  if (ReadAndOpenMachineRecord(path, &rec, &win32) != MachineStoreStatus::Ok) return false;
  uint64_t floor = 0;
  // G6: a floor that cannot be read is NOT floor 0 -- fail safe (feature off) rather than fail open.
  if (!ReadPreloginOwnerFloor(&floor)) return false;
  BindingContext ctx;
  ctx.currentMachineId = current_machine_guid();
  ctx.expectedAccountId = 0;        // any account enrolled on this machine
  ctx.originAllowed = true;         // origin binding verified at register time; not re-litigated here
  ctx.minOwnerGeneration = floor;   // EXTERNAL durable floor (S2): record cannot self-pass
  ctx.featureGateEnabled = true;
  return ValidateMachineRecord(rec, ctx) == RecordVerdict::Usable;
}

// r9 R5: Ctrl+Alt+Del for the console session's logon screen. The caller (the --prelogin worker) must be in
// the console session, with nobody logged on there; the local policy SoftwareSASGeneration must let services
// generate it (read, never changed). SendSAS(FALSE) is made while impersonating this service's token moved
// into that session. It returns nothing: "Sent" is that the call was made.
uint8_t request_sas_for(uint32_t callerSession, uint32_t* win32, bool* cadNotRequired) {
  using remote60::native_poc::PreloginSasResult;
  *win32 = 0;
  // DisableCAD = 1: this PC's logon does not ask for Ctrl+Alt+Del at all (said to the viewer).
  DWORD cad = 0;
  DWORD cadCb = sizeof(cad);
  *cadNotRequired = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                                 L"DisableCAD", RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &cad, &cadCb) ==
                        ERROR_SUCCESS &&
                    cad != 0;
  const DWORD console = WTSGetActiveConsoleSessionId();
  // r10 T3: the console's user as ClassifyTokenObservation reads it (only ERROR_NO_TOKEN is "nobody"), and
  // the policy as Missing / Value / Error -- a failed read is neither "nobody logged on" nor "not set".
  HANDLE userToken = nullptr;
  const bool tokenOk = console != 0xFFFFFFFFu && WTSQueryUserToken(console, &userToken);
  const DWORD tokenErr = tokenOk ? 0 : GetLastError();
  if (userToken) CloseHandle(userToken);
  const auto user = remote60::native_poc::prelogin::ClassifyTokenObservation(tokenOk, tokenErr);
  const auto policy = remote60::native_poc::prelogin::ReadSasPolicy(
      HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", L"SoftwareSASGeneration");
  const auto decision = remote60::native_poc::prelogin::DecideSas(callerSession == console, user, tokenErr, policy);
  const PreloginSasResult d = decision.result;
  diag("prelogin: SAS asked by session %lu (console %lu, user %d err %lu, policy state %d value %lu err %lu) -> %d",
       static_cast<unsigned long>(callerSession), static_cast<unsigned long>(console), static_cast<int>(user),
       static_cast<unsigned long>(tokenErr), static_cast<int>(policy.state), static_cast<unsigned long>(policy.value),
       static_cast<unsigned long>(policy.error), static_cast<int>(d));
  *win32 = decision.win32;
  if (d != PreloginSasResult::Sent && d != PreloginSasResult::SentUnconfirmed) return static_cast<uint8_t>(d);
  HANDLE self = nullptr;
  HANDLE inSession = nullptr;
  DWORD session = console;
  const bool ready = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE, &self) &&
                     DuplicateTokenEx(self, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenImpersonation, &inSession) &&
                     SetTokenInformation(inSession, TokenSessionId, &session, sizeof(session)) &&
                     SetThreadToken(nullptr, inSession);
  if (!ready) *win32 = GetLastError();
  if (self) CloseHandle(self);
  if (!ready) {
    if (inSession) CloseHandle(inSession);
    return static_cast<uint8_t>(PreloginSasResult::Failed);
  }
  SendSAS(FALSE);
  RevertToSelf();
  CloseHandle(inSession);
  return static_cast<uint8_t>(d);  // Sent, or SentUnconfirmed when the policy is not set
}

// The admin pipe's primitives as this service binds them (the running service and --uninstall-service).
void bind_service_admin_primitives(remote60::native_poc::prelogin::AdminPrimitives* p) {
  remote60::native_poc::prelogin::BindProductStartType(p, kSecureInputServiceName);
  remote60::native_poc::prelogin::BindProductMachineStore(p);
  p->recordPath = machine_record_path;
  p->machineId = current_machine_guid;
  p->readFloor = [](uint64_t* out) { return ReadPreloginOwnerFloor(out); };  // r6 G6: fallible
  p->raiseFloor = raise_prelogin_owner_floor;
  // r11 U1: the issued-generation bound, next to the floor.
  p->readIssued = [](uint64_t* out) {
    return remote60::native_poc::prelogin::ReadIssuedGenerationAt(HKEY_LOCAL_MACHINE, kOwnerFloorKey, out);
  };
  p->raiseIssued = [](uint64_t g) {
    return remote60::native_poc::prelogin::RaiseIssuedGenerationAt(HKEY_LOCAL_MACHINE, kOwnerFloorKey, g);
  };
  p->featureEnabled = prelogin_feature_enabled;
  // r9 R1/R2: the owed-revoke mark, and one writer in this process (admin server + reconcile).
  p->revokePending = [] { return remote60::native_poc::prelogin::ReadPreloginRevokePending(); };
  p->markRevokePending = [] {  // r11 U1: a revoke that cannot be proven stays owed
    return remote60::native_poc::prelogin::WriteRevokePendingAt(HKEY_LOCAL_MACHINE,
                                                                remote60::native_poc::prelogin::kOwnerFloorKey, true);
  };
  p->clearRevokePending = [] {
    return remote60::native_poc::prelogin::WriteRevokePendingAt(HKEY_LOCAL_MACHINE,
                                                                remote60::native_poc::prelogin::kOwnerFloorKey, false);
  };
  static const std::shared_ptr<std::mutex> transaction = std::make_shared<std::mutex>();
  p->transaction = transaction;
  p->requestSas = request_sas_for;  // r9 R5
  // r6 V7: the only server a record may name -- the product's.
  p->allowedDirectoryUrl = remote60::native_poc::prelogin::PreloginAllowedDirectoryUrl();
}


// Launch a SYSTEM child in `sessionId` on `desktopName` (winsta0\<desktop>). Mirrors start_agent's token
// handling but takes no inherited pipe; the child runs the full product engine. Returns the process
// handle (caller owns) or nullptr.
HANDLE launch_system_child_in_session(DWORD sessionId, const std::wstring& desktopName,
                                      const std::wstring& exe, const wchar_t* args, const wchar_t* environment) {
  if (exe.empty()) return nullptr;
  HANDLE serviceToken = nullptr;
  HANDLE childToken = nullptr;
  bool ok = OpenProcessToken(GetCurrentProcess(),
                             TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY | TOKEN_ADJUST_SESSIONID,
                             &serviceToken) &&
            DuplicateTokenEx(serviceToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary,
                             &childToken) &&
            SetTokenInformation(childToken, TokenSessionId, &sessionId, sizeof(sessionId));
  if (serviceToken) CloseHandle(serviceToken);
  if (!ok) {
    if (childToken) CloseHandle(childToken);
    return nullptr;
  }
  wchar_t command[32768]{};
  _snwprintf_s(command, _TRUNCATE, L"\"%s\" %s", exe.c_str(), args);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  std::wstring desktopSpec = L"winsta0\\" + desktopName;
  startup.lpDesktop = desktopSpec.data();
  PROCESS_INFORMATION proc{};
  // CREATE_SUSPENDED so the child is assigned to the kill-on-close Job BEFORE it runs (S5): it can never
  // escape the job or do anything before the lifetime boundary is in place.
  ok = CreateProcessAsUserW(childToken, nullptr, command, nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                            const_cast<wchar_t*>(environment), nullptr, &startup, &proc) != FALSE;
  CloseHandle(childToken);
  if (!ok) return nullptr;
  // G9: NEVER resume a child we could not bind to the kill-on-close Job. ensure_child_job() or the
  // assignment failing means the lifetime boundary is absent -- an un-jobbed SYSTEM worker would outlive
  // the service. Terminate the still-suspended child and report failure instead of adopting it.
  HANDLE job = ensure_child_job();
  const bool jobAssigned = (job != nullptr) && (AssignProcessToJobObject(job, proc.hProcess) != FALSE);
  const bool resumed = jobAssigned && (ResumeThread(proc.hThread) != static_cast<DWORD>(-1));
  const ChildLaunchDisposition disp = DecideChildAdoption(/*createdSuspended=*/true, jobAssigned, resumed);
  CloseHandle(proc.hThread);
  if (disp != ChildLaunchDisposition::Adopt) {
    (void)TerminateProcess(proc.hProcess, 0);
    (void)WaitForSingleObject(proc.hProcess, 1000);
    CloseHandle(proc.hProcess);
    diag("prelogin: child launch aborted (job=%d resume=%d) -> terminated, not adopted", jobAssigned, resumed);
    return nullptr;
  }
  return proc.hProcess;
}

bool child_alive(HANDLE h) { return h && WaitForSingleObject(h, 0) == WAIT_TIMEOUT; }

void stop_child(HANDLE* h) {
  if (*h) {
    if (child_alive(*h)) {
      (void)TerminateProcess(*h, 0);
      (void)WaitForSingleObject(*h, 1000);
    }
    CloseHandle(*h);
    *h = nullptr;
  }
}

void ensure_prelogin_worker(DWORD consoleSession) {
  if (os_effects_blocked_for_test()) {
    SIS_TEST_NOTE("pre-login worker start blocked (test build)");
    return;
  }
  // Reuse the existing worker ONLY if it is alive AND was launched for THIS console session (Codex S5):
  // after a fast-user-switch / console change the old child must never be mistaken for the new session's.
  if (child_alive(gPreloginWorker) && gPreloginWorkerSession == consoleSession) return;
  // r6 V1: a worker that ended is said out loud (its exit code, how long it ran), and one that keeps
  // dying is retried less and less often (prelogin_worker_launch.hpp), not every second for ever.
  static remote60::native_poc::prelogin::WorkerRestartBackoff backoff;
  static ULONGLONG launchedAt = 0;
  const ULONGLONG now = GetTickCount64();
  if (gPreloginWorker && !child_alive(gPreloginWorker)) {
    DWORD rc = 0;
    GetExitCodeProcess(gPreloginWorker, &rc);
    const uint64_t wait = backoff.OnExit(launchedAt, now);
    diag("prelogin: worker exited rc=%lu after %llu ms; next start in %llu ms", rc,
         static_cast<unsigned long long>(now - launchedAt), static_cast<unsigned long long>(wait));
  }
  stop_child(&gPreloginWorker);  // dead handle, or a different-session child -> never reuse
  if (!backoff.MayLaunch(now)) return;
  // r6 V1: the worker gets the product's streaming switches in an explicit environment (the same
  // values GNLinkHost gives its child) -- the service's own environment has none of them, and without
  // the encoder switch GNLinkStream refuses h264 and exits rc=11.
  wchar_t* own = GetEnvironmentStringsW();
  const std::vector<wchar_t> environment = remote60::native_poc::prelogin::PreloginWorkerEnvironment(own);
  if (own) FreeEnvironmentStringsW(own);
  const std::wstring args = remote60::native_poc::prelogin::PreloginWorkerArgs();
  {
    // prelogin-field D3: the worker opens its own log (a handle cannot cross into its session); the service makes
    // sure the folder is there and trusted, and says so when that changes. The worker checks it again itself.
    static DWORD lastLogFolder = 0xFFFFFFFFu;
    remote60::native_poc::prelogin::HeldChain logChain;
    const std::wstring logPath = remote60::native_poc::prelogin::ProductWorkerLogPath();
    const DWORD lf = logPath.empty() ? ERROR_PATH_NOT_FOUND
                                     : remote60::native_poc::prelogin::HoldRecordChain(
                                           logPath, /*create=*/true, remote60::native_poc::prelogin::OwnerIsSystemOrAdministrators,
                                           &logChain);
    if (lf != lastLogFolder) {
      diag("prelogin: worker log folder %s (win32 %lu%s%ls)", lf == 0 ? "ok" : "REFUSED", static_cast<unsigned long>(lf),
           lf == 0 ? "" : " at ", lf == 0 ? L"" : logChain.failedAt.c_str());
      lastLogFolder = lf;
    }
  }
  gPreloginWorker = launch_system_child_in_session(consoleSession, L"Winlogon", sibling_exe_path(L"GNLinkStream.exe"),
                                                    args.c_str(), environment.data());
  gPreloginWorkerSession = gPreloginWorker ? consoleSession : 0xFFFFFFFFu;
  launchedAt = now;
  if (!gPreloginWorker) backoff.OnLaunchFailed(now);
  diag("prelogin: worker launch on console session=%u -> %s", consoleSession,
       gPreloginWorker ? "ok" : "FAILED");
}

// Per-launch readiness nonce (G9): unguessable so only the host THIS launch started can satisfy the
// handoff. Random preferred; a tick+counter fallback still yields a per-launch unique value.
std::wstring make_launch_nonce() {
  unsigned char b[8]{};
  if (BCryptGenRandom(nullptr, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
    wchar_t hex[17]{};
    for (int i = 0; i < 8; ++i) _snwprintf_s(hex + i * 2, 3, _TRUNCATE, L"%02x", b[i]);
    return hex;
  }
  static std::atomic<uint32_t> ctr{0};
  wchar_t buf[32]{};
  _snwprintf_s(buf, _TRUNCATE, L"%08llx%08x", static_cast<unsigned long long>(GetTickCount64()),
               ctr.fetch_add(1, std::memory_order_relaxed));
  return buf;
}

// Launch the logged-on user's own Host with the verified user/linked-Full token (the service-owned
// handoff that replaces the elevated HKCU Run). The token matrix is resolved by the console probe
// classifiers; here we obtain the primary token and, for a Limited token, its linked Full.
void launch_user_host(DWORD consoleSession) {
  if (os_effects_blocked_for_test()) {
    SIS_TEST_NOTE("user Host start blocked (test build)");
    return;
  }
  // Reuse only an alive host launched for THIS session (Codex S5 identity).
  if (child_alive(gUserHostProcess) && gUserHostSession == consoleSession) return;
  stop_child(&gUserHostProcess);
  gUserHostSession = 0xFFFFFFFFu;
  gUserHostReady.Reset();  // this launch's ready only: and record when it began (an older host is stale)
  {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    gUserHostLaunchTime.store((static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime);
  }
  HANDLE userToken = nullptr;
  if (!WTSQueryUserToken(consoleSession, &userToken)) {
    diag("prelogin: handoff WTSQueryUserToken failed err=%lu", GetLastError());
    return;
  }
  HANDLE launchToken = userToken;
  HANDLE linked = nullptr;
  TOKEN_ELEVATION_TYPE et = TokenElevationTypeDefault;
  DWORD ret = 0;
  if (GetTokenInformation(userToken, TokenElevationType, &et, sizeof(et), &ret) &&
      et == TokenElevationTypeLimited) {
    TOKEN_LINKED_TOKEN lt = {};
    if (GetTokenInformation(userToken, TokenLinkedToken, &lt, sizeof(lt), &ret) && lt.LinkedToken) {
      linked = lt.LinkedToken;
      launchToken = linked;  // use the Full linked token (Q4 Limited+linked path)
    }
  }
  // G9: re-judge the FINAL launch token at the instant of launch, not just at the earlier observation.
  // If the token drifted to a non-admin / unresolvable state, abort the handoff rather than start a Host
  // with the wrong authority.
  const PrivilegeFacts facts = GatherPrivilegeFactsFromToken(reinterpret_cast<void*>(launchToken));
  const UserPrivilege priv = ClassifyPrivilege(facts);
  if (!LaunchTokenAcceptable(priv)) {
    diag("prelogin: handoff aborted -- launch token not a supported admin at launch time");
    if (linked) CloseHandle(linked);
    CloseHandle(userToken);
    return;
  }
  // A fresh per-launch nonce binds the ready to THIS host (the ready itself comes over the admin pipe;
  // r14 L5: the Global\ event it once used, and its reset here, are gone).
  const std::wstring nonce = make_launch_nonce();
  // r7 H1: the user's own environment with this launch's nonce -- or no launch at all (a Host without its
  // nonce could never complete the handoff). The start itself is the one call the H1 test also makes.
  std::vector<wchar_t> env;
  DWORD envErr = 0;
  if (!remote60::native_poc::prelogin::BuildUserHostEnvironment(launchToken, nonce, &env, &envErr)) {
    diag("prelogin: handoff aborted -- the user's environment could not be built err=%lu", envErr);
    if (linked) CloseHandle(linked);
    CloseHandle(userToken);
    return;
  }
  const std::wstring exe = sibling_exe_path(L"GNLinkHost.exe");
  PROCESS_INFORMATION proc{};
  DWORD startErr = 0;
  // r14 H3 / L3: started and resumed OUTSIDE the kill-on-close job (that job is the SYSTEM worker's): the
  // user's Host outlives a service stop / restart / crash, and its in-app update can break away.
  const bool created =
      remote60::native_poc::prelogin::LaunchUserHost(launchToken, exe, env, L"winsta0\\Default", &proc, &startErr);
  if (!created) diag("prelogin: user host start failed err=%lu", startErr);
  bool adopted = false;
  if (created) {
    CloseHandle(proc.hThread);
    gUserHostProcess = proc.hProcess;
    gUserHostSession = consoleSession;   // S5 identity
    gUserHostReadyNonce = nonce;         // the service now waits ONLY on this launch's ready
    adopted = true;
  }
  diag("prelogin: user host handoff -> %s", adopted ? "launched" : "FAILED");
  if (linked) CloseHandle(linked);
  CloseHandle(userToken);
}

// r9 R4/W1: the user's Host after logon -- started by this service, or the one the user already runs
// (adopted: watched, never stopped by the service). prelogin_user_host_policy.hpp decides.
remote60::native_poc::prelogin::UserHostLaunchPolicy gUserHostPolicy;
HANDLE gAdoptedHost = nullptr;
DWORD gAdoptedHostPid = 0;
DWORD gAdoptedSession = 0xFFFFFFFFu;  // r10 N1: the session the adopted Host runs in

// This logon's identity: the console session and its logon time (a lock/unlock is the same logon). r10: a
// query that fails keeps the last key known for that session -- a failure is not a new logon (it would
// forget that the user closed their Host).
uint64_t console_logon_key(DWORD session) {
  static DWORD lastSession = 0xFFFFFFFFu;
  static uint64_t lastKey = 0;
  WTSINFOW* info = nullptr;
  DWORD bytes = 0;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSSessionInfo, reinterpret_cast<LPWSTR*>(&info),
                                  &bytes) &&
      info) {
    const uint64_t logonTime = static_cast<uint64_t>(info->LogonTime.QuadPart);
    WTSFreeMemory(info);
    lastSession = session;
    lastKey = (logonTime << 8) ^ session;
    return lastKey;
  }
  if (session == lastSession) return lastKey;
  return (static_cast<uint64_t>(0xFFFFFFFFu) << 8) ^ session;  // never queried: one stable key per session
}

// A GNLinkHost of THIS install running in `session` (its image is this install's GNLinkHost.exe): its pid.
DWORD find_user_host_in_session(DWORD session) {
  const std::wstring image = sibling_exe_path(L"GNLinkHost.exe");
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  DWORD found = 0;
  for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
    if (_wcsicmp(pe.szExeFile, L"GNLinkHost.exe") != 0) continue;
    DWORD sid = 0xFFFFFFFFu;
    if (!ProcessIdToSessionId(pe.th32ProcessID, &sid) || sid != session) continue;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
    if (!p) continue;
    wchar_t path[MAX_PATH * 2] = {};
    DWORD n = MAX_PATH * 2;
    if (QueryFullProcessImageNameW(p, 0, path, &n) && _wcsicmp(path, image.c_str()) == 0) found = pe.th32ProcessID;
    CloseHandle(p);
  }
  CloseHandle(snap);
  return found;
}

// r11 U3: first thing every tick, whichever branch follows (WatchedHostOnTick): a watched Host of another
// session is let go -- it is that user's, still logged on there -- and only a Host this service started in
// the console session, which has no user any more, is stopped.
void settle_watched_hosts(DWORD console, bool preloginWorkerTick) {
  using remote60::native_poc::prelogin::WatchedHostStep;
  if (gUserHostProcess) {
    const WatchedHostStep step =
        remote60::native_poc::prelogin::WatchedHostOnTick(gUserHostSession, console, preloginWorkerTick, true);
    if (step == WatchedHostStep::LetGo) {
      diag("prelogin: the started user host belongs to session %lu, the console is %lu: no longer watched",
           static_cast<unsigned long>(gUserHostSession), static_cast<unsigned long>(console));
      CloseHandle(gUserHostProcess);  // not stopped: it is that user's
      gUserHostProcess = nullptr;
    } else if (step == WatchedHostStep::Stop) {
      stop_child(&gUserHostProcess);
    }
  }
  if (gAdoptedHost &&
      remote60::native_poc::prelogin::WatchedHostOnTick(gAdoptedSession, console, preloginWorkerTick, false) !=
          WatchedHostStep::Keep) {
    CloseHandle(gAdoptedHost);  // the user's own: never stopped
    gAdoptedHost = nullptr;
    gAdoptedHostPid = 0;
  }
}

// One tick of the user-Host side of the handoff.
void reconcile_user_host(DWORD console) {
  using remote60::native_poc::prelogin::UserHostDecision;
  remote60::native_poc::prelogin::UserHostObservation o;
  o.logon = console_logon_key(console);
  o.nowMs = GetTickCount64();
  // (r10 N1 / r11 U3: a Host of another session was already let go this tick -- settle_watched_hosts.)
  HANDLE* watched = gUserHostProcess ? &gUserHostProcess : (gAdoptedHost ? &gAdoptedHost : nullptr);
  if (watched) {
    if (WaitForSingleObject(*watched, 0) == WAIT_OBJECT_0) {
      DWORD code = 0;
      GetExitCodeProcess(*watched, &code);
      o.watchedExited = true;
      o.watchedExitCode = code;
      diag("prelogin: user host %s ended exit=%lu", watched == &gAdoptedHost ? "(adopted)" : "(started)",
           static_cast<unsigned long>(code));
      CloseHandle(*watched);
      *watched = nullptr;
      if (watched == &gAdoptedHost) gAdoptedHostPid = 0;
    } else {
      o.watchedAlive = true;
    }
  }
  DWORD otherPid = 0;
  if (!o.watchedAlive) {
    otherPid = find_user_host_in_session(console);
    o.otherRunning = otherPid != 0;
  }
  switch (gUserHostPolicy.Decide(o)) {
    case UserHostDecision::Adopt:
      gAdoptedHost = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, otherPid);
      gAdoptedHostPid = gAdoptedHost ? otherPid : 0;
      gAdoptedSession = console;
      diag("prelogin: the user's GNLinkHost is already running (pid %lu): adopted, none started",
           static_cast<unsigned long>(otherPid));
      break;
    case UserHostDecision::Launch:
      launch_user_host(console);
      if (!gUserHostProcess) gUserHostPolicy.OnLaunchFailed(GetTickCount64());
      break;
    case UserHostDecision::None:
      break;
  }
}

// r9 W2: once a user is logged on, the SYSTEM pre-login worker may stay only for the logon transition (it
// stops itself the moment the input desktop is the user's -- host_startup_config's desktop guard); if the
// handoff has not completed within this, it is stopped here regardless.
constexpr uint64_t kWorkerAfterLogonMs = 30000;
uint64_t gLogonSeenMs = 0;

// Observe the console for ONE reconcile tick. `consoleOut` returns the exact console session this
// observation was taken against, so the reconcile uses the SAME session for any launch (Codex S5: do not
// re-query WTSGetActiveConsoleSessionId and risk launching on a different session). Feature-enabled is
// re-evaluated EVERY tick from the durable record+floor (Codex S2: a live OFF/revoke takes effect without
// a service restart, and an enable is honoured because the loop always runs).
SupervisorObservation observe_console(DWORD* consoleOut, bool consoleChanged) {
  SupervisorObservation obs;
  const bool enabled = prelogin_feature_enabled();
  obs.featureEnabled = enabled;
  obs.machineCredentialValid = enabled;  // the same durable record+floor gates both
  // consoleChanged comes from the monotonic topology generation tracked by the loop -- NOT from
  // exchanging gSessionChangePending, which is ensure_agent's to consume (Codex S5 event-race).
  obs.consoleChanged = consoleChanged;
  const DWORD console = WTSGetActiveConsoleSessionId();
  if (consoleOut) *consoleOut = console;
  obs.consoleSessionValid = (console != 0xFFFFFFFFu);
  if (!obs.consoleSessionValid) return obs;
  HANDLE userToken = nullptr;
  if (WTSQueryUserToken(console, &userToken)) {
    obs.userToken = UserTokenObservation::HasUser;
    const PrivilegeFacts facts = GatherPrivilegeFactsFromToken(reinterpret_cast<void*>(userToken));
    obs.privilege = ClassifyPrivilege(facts);
    CloseHandle(userToken);
    // Real ready signal (r4 #7): the handoff completes only when the launched user Host is BOTH alive
    // AND has signalled it reached its ready point (directory-registered + serving), never on the bare
    // LOGON or on mere process existence.
    // The host's ready over the admin pipe, for THIS session, launch and launch nonce (r2 item 7 + r6 G9).
    obs.userHostReady = (child_alive(gUserHostProcess) &&
                         gUserHostReady.Completes(console, gUserHostLaunchTime.load(), narrow_nonce(gUserHostReadyNonce))) ||
                        // r10: or the adopted Host (session + image checked) -- its own streaming child's ready.
                        (gAdoptedHost && gAdoptedSession == console && WaitForSingleObject(gAdoptedHost, 0) == WAIT_TIMEOUT &&
                         gUserHostReady.CompletesAdopted(console, gAdoptedHostPid, [](uint32_t pid, uint64_t start) {
                           return remote60::native_poc::prelogin::ParentProcessOf(pid, start);
                         }));
  } else {
    obs.userToken = ClassifyTokenObservation(false, GetLastError());
  }
  return obs;
}

void prelogin_reconcile_loop() {
  PreloginSupervisor sup;
  uint32_t lastTopoGen = gSessionTopologyGeneration.load(std::memory_order_acquire);
  // r9 R1/R2: a revoke owed by a sign-out or an uninstall is carried out here (first thing after a start,
  // then every tick until it worked) -- under the same writer lock as the admin server.
  remote60::native_poc::prelogin::AdminPrimitives revokePrimitives;
  bind_service_admin_primitives(&revokePrimitives);
  int lastRevokeStatus = -1;
  while (gRunning.load(std::memory_order_acquire)) {
    if (!os_effects_blocked_for_test() && remote60::native_poc::prelogin::ReadPreloginRevokePending() != 0) {
      const auto owed = remote60::native_poc::prelogin::CompletePendingRevoke(revokePrimitives);
      if (static_cast<int>(owed.status) != lastRevokeStatus) {
        diag("prelogin: owed revoke -> status=%d win32=%lu", static_cast<int>(owed.status),
             static_cast<unsigned long>(owed.win32));
        lastRevokeStatus = static_cast<int>(owed.status);
      }
    }
    // Track the monotonic topology generation as OUR own last-seen (Codex S5): a session change bumps
    // it, and we react once, without racing ensure_agent for the pending flag.
    const uint32_t topoGen = gSessionTopologyGeneration.load(std::memory_order_acquire);
    const bool consoleChanged = (topoGen != lastTopoGen);
    lastTopoGen = topoGen;
    DWORD console = 0xFFFFFFFFu;
    SupervisorObservation obs = observe_console(&console, consoleChanged);
    obs.nowMs = GetTickCount64();  // r14 H1: the transient budget is a span of time, not a count of turns  // console = this obs's snapshot
    const PreloginSupervisor::Outcome out = sup.Reconcile(obs);
    settle_watched_hosts(console, out.action == SupervisorAction::RunPreloginWorker);  // r11 U3
    switch (out.action) {
      case SupervisorAction::RunPreloginWorker:
        gLogonSeenMs = 0;
        if (obs.consoleSessionValid) ensure_prelogin_worker(console);
        break;
      case SupervisorAction::StartUserHostFull:
      case SupervisorAction::StartUserHostLinkedFull:
      case SupervisorAction::StartUserHostDefault:
        // r9 R4/W1: start the user's Host only as the policy says (adopt one already running, never one the
        // user closed, a crashed one with backoff) -- not on every tick until a ready.
        if (obs.consoleSessionValid) reconcile_user_host(console);
        // r9 W2: the pre-login worker serves the logon transition only, bounded.
        if (gLogonSeenMs == 0) gLogonSeenMs = GetTickCount64();
        if (gPreloginWorker && GetTickCount64() - gLogonSeenMs >= kWorkerAfterLogonMs) {
          diag("prelogin: logged on %llus without a handoff: the pre-login worker is stopped",
               static_cast<unsigned long long>(kWorkerAfterLogonMs / 1000));
          stop_child(&gPreloginWorker);
        }
        break;
      case SupervisorAction::None:
        if (out.handoff == HandoffAction::CompleteHandoff) stop_child(&gPreloginWorker);
        break;
      case SupervisorAction::StopAll:
      case SupervisorAction::EnterUnsupportedUser:
      case SupervisorAction::EnterFault:
      case SupervisorAction::HoldNoCredential:
      case SupervisorAction::HoldNoConsole:
        stop_child(&gPreloginWorker);
        break;
      case SupervisorAction::RetryTransient:
        break;  // keep any healthy child
    }
    // r14 H1: bounded cadence on what this loop OWNS -- the topology generation this tick began with (a
    // later session event ends the wait once). gSessionChangePending is ensure_agent's: it stayed set until
    // the next secure input and made every wait return at once (the loop spun at 100% CPU).
    (void)remote60::native_poc::prelogin::WaitReconcileTick(
        topoGen, [] { return gRunning.load(std::memory_order_acquire); },
        [] { return gSessionTopologyGeneration.load(std::memory_order_acquire); }, [](uint32_t ms) { Sleep(ms); });
  }
  stop_child(&gPreloginWorker);
  // r14 L3: the service stopping does not stop the user's Host -- it is only no longer watched (a new
  // service adopts it, r11 U4).
  if (gUserHostProcess) {
    CloseHandle(gUserHostProcess);
    gUserHostProcess = nullptr;
  }
}

void WINAPI service_main(DWORD, wchar_t**) {
  // Ex rather than the plain handler so SERVICE_CONTROL_SESSIONCHANGE can be delivered; the
  // plain form cannot receive it, and without it the agent outlives the session it was made for.
#ifdef REMOTE60_SIS_TEST_SEAM
  if (remote60::native_poc::sis_test::Active()) {  // r21 E1: no SCM; the test's STOP is this service's own handler
    gStatusHandle = reinterpret_cast<SERVICE_STATUS_HANDLE>(static_cast<uintptr_t>(1));
    remote60::native_poc::sis_test::StartStopWatcher(
        [] { (void)service_control(SERVICE_CONTROL_STOP, 0, nullptr, nullptr); });
  } else {
    gStatusHandle = RegisterServiceCtrlHandlerExW(kSecureInputServiceName, service_control, nullptr);
  }
#else
  gStatusHandle =
      RegisterServiceCtrlHandlerExW(kSecureInputServiceName, service_control, nullptr);
#endif
  if (!gStatusHandle) return;
  report_service_status(SERVICE_START_PENDING);
  gRunning.store(true, std::memory_order_release);
  report_service_status(SERVICE_RUNNING);

  // The sealed-unlock exchange lives on its own thread + duplex pipe so its request/response (and a
  // possibly-blocking WTSConnectSession) never touch the input path. (Codex #365/#366.)
  std::thread unlockThread(unlock_pipe_loop);

  // Prelogin supervisor: ALWAYS runs (Codex S2). It re-reads the durable record+floor every tick, so a
  // live enable is honoured without a service restart and a live OFF/revoke takes effect immediately.
  // When the feature is off the supervisor simply decides Disabled -> StopAll each tick (no child, no
  // screen/input), so a feature-off deployment still sees no prelogin behaviour -- just a 1s reconcile
  // that does nothing. (A missing record makes prelogin_feature_enabled return fast.)
  diag("prelogin: starting supervisor reconcile loop (feature re-checked each tick)");
  std::thread preloginThread(prelogin_reconcile_loop);

  // prelogin-ui r1 (item 4): GNLinkHost's "로그인 전에도 원격 접속 허용" reaches this service -- the single
  // writer of the machine record and of its own start mode -- over its own admin-only pipe
  // (prelogin_admin_pipe.cpp). The primitives are this file's own.
  remote60::native_poc::prelogin::AdminPrimitives adminPrimitives;
  bind_service_admin_primitives(&adminPrimitives);
  // prelogin-ui r2: the restart the streaming host's gate admitted (item 6) and the user Host's ready
  // (item 7) -- both only from this install's GNLinkStream, elevated or SYSTEM.
  remote60::native_poc::prelogin::BindProductReboot(&adminPrimitives);
  adminPrimitives.noteUserHostReady = [](uint32_t session, uint64_t started, const std::string& nonce, uint32_t pid) {
    gUserHostReady.Note(session, started, nonce, pid);
  };
  static remote60::native_poc::prelogin::RebootLedger rebootLedger;
  const std::wstring hostImage = sibling_exe_path(L"GNLinkStream.exe");
  std::thread adminThread([&adminPrimitives, hostImage] {
    remote60::native_poc::prelogin::RunPreloginAdminServer(
        adminPrimitives, [] { return gRunning.load(std::memory_order_acquire); },
        admin_pipe_name(), service_pipe_sddl(),
        remote60::native_poc::prelogin::ClientIsElevatedAdministrator,
        [hostImage](void* pipe) { return remote60::native_poc::prelogin::ClientImageIs(pipe, hostImage); }, &rebootLedger);
  });

  DWORD servingError = NO_ERROR;  // r20: the last pipe create error, for the service's exit code
  // r14 M1: every client on its own instance and thread (a disconnected user's streaming host no longer holds
  // the pipe from the console's worker or the new user's host).
  remote60::native_poc::RunMultiClientPipeServer(
      [] { return gRunning.load(std::memory_order_acquire); },
      [&servingError](bool first) {
        HANDLE h = create_secure_pipe(first);
        if (h == INVALID_HANDLE_VALUE) {
          const DWORD e = GetLastError();
          servingError = e != NO_ERROR ? e : ERROR_PIPE_BUSY;
          // r20 merge (Codex Q2): the first instance is never retried without FILE_FLAG_FIRST_PIPE_INSTANCE.
          if (first) diag("secure input pipe create failed (name preempted or error, gle=%lu) -> fail closed", e);
          else diag("secure input pipe: next listener create failed (gle=%lu)", e);
        }
        return h;
      },
      [](HANDLE pipe, HANDLE stop) {
        // Identify the requester before reading anything from it. This is the only point at which the
        // host's session can be learned, and everything downstream depends on it -- for THIS connection.
        const uint32_t requester = requester_session_of(pipe);
        const uint32_t consoleGenerationAtConnect = gConsoleGeneration.load(std::memory_order_acquire);
        // r15 R14-2: nothing process-wide is published -- the unlock path binds its own caller, and an input
        // connection coming or going is not a session-topology change.
        diag("control pipe connected, requester session=%u", requester);
        while (gRunning.load(std::memory_order_acquire)) {
          SecureInputMessage message{};
          // r15 R14-1: overlapped, woken by the server's stop event (secure_input_pipe_server.hpp).
          if (!remote60::native_poc::ReadPipeMessage(pipe, &message, sizeof(message), stop)) break;
          if (message.magic != kSecureInputMagic || message.size != sizeof(message)) break;
          // lockscreen-cred r1: this pipe carries input and nothing else -- a host cannot make the agent stop
          // (Shutdown is the service's own) or carry anything new.
          if (message.kind != static_cast<uint16_t>(SecureInputKind::InputEvent) &&
              message.kind != static_cast<uint16_t>(SecureInputKind::InputText)) {
            diag("secure input: a message of kind %u from requester session=%u was refused", message.kind, requester);
            continue;
          }
          // lockscreen-cred r1 (Codex A6): the last boundary before Winlogon -- no plaintext character to a
          // locked session, whatever the host or its viewer believed (lock_input_policy.hpp).
          if (refuse_locked_plaintext(message, requester)) continue;
          remote60::native_poc::ForwardOutcome outcome;
          {
            std::lock_guard<std::mutex> agent(gAgentMu);
            outcome = forward_to_agent(message, requester, consoleGenerationAtConnect);
          }
          if (outcome == remote60::native_poc::ForwardOutcome::LogonBindingLost) {
            // r11 U2: the worker's session is no longer the console it connected at -- this message and
            // whatever it queued behind it are dropped (the pipe goes; a worker still serving reconnects).
            diag("pre-login input dropped: requester session=%u is not the console it connected at", requester);
            break;
          }
        }
      },
      &gSecureInputPipes, [](const char* line) { diag("%s", line); });
  // r20 merge (Codex Q2): the server returned while the service was still meant to run -- the first instance
  // could not be had (a squatted name) or serving was stopped on purpose. Secure input is NOT being provided:
  // the service goes down the way secure-pipe's single-owner loop did (agent stopped, workers joined,
  // SERVICE_STOPPED) -- now with the error -- instead of staying RUNNING without input. The next start
  // re-checks the name with FILE_FLAG_FIRST_PIPE_INSTANCE.
  DWORD exitError = NO_ERROR;
  if (gRunning.load(std::memory_order_acquire)) {
    exitError = servingError != NO_ERROR ? servingError : ERROR_PIPE_BUSY;
    diag("secure input serving ended while running (error=%lu) -> the service stops, fail closed", exitError);
    gRunning.store(false, std::memory_order_release);
    SIS_TEST_NOTE("serving ended while running: error=%lu, running cleared", exitError);
  }
  {
    std::lock_guard<std::mutex> agent(gAgentMu);
    stop_agent();
  }
  if (preloginThread.joinable()) preloginThread.join();
  SIS_TEST_NOTE("joined: prelogin supervisor");
  if (adminThread.joinable()) adminThread.join();  // its connect wait checks gRunning every 250 ms
  SIS_TEST_NOTE("joined: admin pipe server");
  // Wake the unlock worker out of any blocking ConnectNamedPipe/ReadFile and wait for it to exit.
  // CancelSynchronousIo only acts while the thread is inside a synchronous wait, so loop until the
  // thread actually ends. (Codex #370 BLOCKER A.)
  if (unlockThread.joinable()) {
    HANDLE h = static_cast<HANDLE>(unlockThread.native_handle());
    while (WaitForSingleObject(h, 50) == WAIT_TIMEOUT) {
      (void)CancelSynchronousIo(h);
    }
    unlockThread.join();
  }
  SIS_TEST_NOTE("joined: unlock worker");
  report_service_status(SERVICE_STOPPED, exitError);
}

// Access the agent needs on the input desktop just to see it and follow switches.
constexpr DWORD kDesktopBaseAccess =
    DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_SWITCHDESKTOP;

// SendInput refuses unless BOTH hold: the calling thread is on the current input desktop, and the
// thread's desktop was opened with DESKTOP_JOURNALPLAYBACK. Either failure returns the same
// ERROR_ACCESS_DENIED, which is why the two were indistinguishable for so long.
//
// Measured on 0.2.11: the agent ran as SYSTEM, was created on Winlogon, attached successfully, and
// SetCursorPos moved the cursor on the live consent prompt -- so the desktop was already correct --
// yet every button event came back err=5. The mask above simply never asked for journal playback.
// SetCursorPos does not need it; SendInput does. That single missing bit is the whole reason UAC
// was unclickable, and it is why the driver/registry routes looked like the only options left.
//
// The retry without journal playback is deliberate. A desktop whose DACL withholds that right
// should still get cursor movement rather than lose input entirely, and the caller records which
// of the two it got so a future err=5 cannot be misread as this bug returning.
bool attach_to_input_desktop(HDESK* ownedDesktop, bool* journalPlayback) {
  bool playback = true;
  HDESK next = OpenInputDesktop(0, FALSE, kDesktopBaseAccess | DESKTOP_JOURNALPLAYBACK);
  if (!next) {
    playback = false;
    next = OpenInputDesktop(0, FALSE, kDesktopBaseAccess);
  }
  if (!next) return false;
  if (!SetThreadDesktop(next)) {
    CloseDesktop(next);
    return false;
  }
  if (*ownedDesktop) CloseDesktop(*ownedDesktop);
  *ownedDesktop = next;
  if (journalPlayback) *journalPlayback = playback;
  return true;
}

POINT map_point(const SecureInputMessage& message) {
  // Where the client's pixels live on the desktop. The host sends it when it knows; otherwise
  // fall back to the virtual screen, which is what full-desktop capture covers and what
  // SetCursorPos addresses. The old fallback was the primary monitor alone, which made a prompt
  // on any other display unreachable.
  DesktopRect target{message.targetOriginX, message.targetOriginY,
                     static_cast<int32_t>(message.targetWidth),
                     static_cast<int32_t>(message.targetHeight)};
  if (!target.valid()) {
    target.originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    target.originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    target.width = std::max<int32_t>(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    target.height = std::max<int32_t>(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
  }
  const auto mapped =
      map_client_point(message.x, message.y, message.inputWidth, message.inputHeight, target);
  POINT point{};
  point.x = mapped.x;
  point.y = mapped.y;
  return point;
}

// The button edge -> SendInput mapping is mouse_button_map.hpp, shared with the host; what this
// agent does for one event is planned in secure_input_inject.hpp before any call is made. The copy
// that lived here made any key that was not right or middle a LEFT click. (mouse-xbutton r1 / r2)

const char* gDpiAwarenessApplied = "none";

// Every other component that touches screen coordinates declares DPI awareness -- the capture
// worker, the host window, the viewer. The input agent was the one that did not, and it is the
// one that converts a client coordinate into a desktop point.
//
// It matters because the host captures and reports PHYSICAL pixels. A DPI-unaware process is
// handed a virtualised coordinate space instead, so SetCursorPos(x, y) is scaled up by the
// display's factor before it reaches the cursor -- putting every click down and to the right of
// the intended spot, by more the further from the origin it is. At 100% scaling the two spaces
// coincide and this call changes nothing, which is why it can be added without risking the
// machines that already work.
//
// Called before any coordinate is touched; awareness cannot be changed once the process has
// begun interacting with the desktop.
void apply_dpi_awareness() {
  if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    gDpiAwarenessApplied = "per-monitor-v2";
    return;
  }
  if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) {
    gDpiAwarenessApplied = "system";
    return;
  }
  // Pre-1703 fallback. Equivalent to system awareness and enough to stop the virtualisation.
  gDpiAwarenessApplied = SetProcessDPIAware() ? "legacy-system" : "none";
}

// Records the first few failures in full. "inject failed" is not actionable: SetCursorPos and
// SendInput fail for entirely different reasons, and the event kind decides which of them even
// runs. Bounded because a broken session would otherwise write a line per click forever.
void diag_inject_failure(const char* where, const SecureInputMessage& message, DWORD error,
                         long mapped_x, long mapped_y) {
  static std::atomic<int> remaining{40};
  if (remaining.fetch_sub(1, std::memory_order_relaxed) <= 0) return;
  diag("inject FAILED at %s: kind=%u eventKind=%u buttons=%u key=%u in=(%d,%d)/%ux%u "
       "target=(%d,%d)/%ux%u mapped=(%ld,%ld) err=%lu",
       where, message.kind, message.eventKind, message.buttons, message.keyCode, message.x,
       message.y, message.inputWidth, message.inputHeight, message.targetOriginX,
       message.targetOriginY, message.targetWidth, message.targetHeight, mapped_x, mapped_y,
       error);
}

// Records where a click actually landed, for the first few button events of each episode.
//
// Until injection started working there was nothing to record: the only coordinate log ran on
// failure. Now that clicks land, "they land in the wrong place" has three candidate causes that
// the mapped point alone cannot separate -- a wrong target rect from the host, a wrong input
// domain from the client, or DPI virtualisation rewriting the coordinate after we hand it over.
//
// Reading the cursor straight back is what separates them. If GetCursorPos returns the point we
// asked for, the coordinate arithmetic is sound and the inputs to it are what is wrong; if it
// returns something else, the offset is being applied below us. `virt` is logged alongside
// because the agent's own view of the virtual screen is what the fallback path would have used.
void diag_inject_landing(const SecureInputMessage& message, long mapped_x, long mapped_y) {
  // Per episode, not per process: a 2 s quiet gap refills the 12 lines, 60 per minute at most
  // (secure_input_diag_budget.hpp). The agent outlives a UAC prompt -- it is recreated only when
  // the desktop name changes -- so the old one-shot budget recorded the first prompt of the day
  // and nothing after it (2026-09-07 15:15 left no line). Single message thread.
  static remote60::native_poc::DiagLandingBudget budget;
  if (!remote60::native_poc::diag_landing_budget_take(budget, now_ms() * 1000ULL)) return;
  POINT actual{};
  const bool got = GetCursorPos(&actual) != FALSE;
  diag("inject landed: eventKind=%u in=(%d,%d)/%ux%u target=(%d,%d)/%ux%u mapped=(%ld,%ld) "
       "cursor=%s(%ld,%ld) virt=(%d,%d)/%dx%d dpiAwareness=%s",
       message.eventKind, message.x, message.y, message.inputWidth, message.inputHeight,
       message.targetOriginX, message.targetOriginY, message.targetWidth, message.targetHeight,
       mapped_x, mapped_y, got ? "" : "FAILED", got ? actual.x : 0, got ? actual.y : 0,
       GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
       GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
       gDpiAwarenessApplied);
}

bool inject_message(const SecureInputMessage& message) {
  if (message.kind == static_cast<uint16_t>(SecureInputKind::InputText)) {
    const uint16_t count = std::min<uint16_t>(message.textCount,
                                              remote60::native_poc::kSecureInputTextMax);
    for (uint16_t i = 0; i < count; ++i) {
      INPUT input[2]{};
      input[0].type = INPUT_KEYBOARD;
      input[0].ki.wScan = message.text[i];
      input[0].ki.dwFlags = KEYEVENTF_UNICODE;
      input[1] = input[0];
      input[1].ki.dwFlags |= KEYEVENTF_KEYUP;
      if (SendInput(2, input, sizeof(INPUT)) != 2) {
        diag_inject_failure("SendInput-text", message, GetLastError(), 0, 0);
        return false;
      }
    }
    return true;
  }
  if (message.kind != static_cast<uint16_t>(SecureInputKind::InputEvent)) {
    diag_inject_failure("kind-not-input-event", message, 0, 0, 0);
    return false;
  }
  const POINT point = map_point(message);
  // Decided in full before anything is touched (secure_input_inject.hpp): an event this agent
  // does not understand -- a button edge whose key is not a button, an unknown kind -- moves
  // nothing and sends nothing. Until r2 the cursor was placed first and the refusal came after.
  const InjectPlan plan = plan_input_event(message);
  if (!plan.valid) {
    diag_inject_failure(plan.why, message, 0, point.x, point.y);
    return false;
  }
  // Position with SetCursorPos and send the button separately. Carrying absolute coordinates on
  // the button event is theoretically tidier -- it removes the window in which something else
  // could move the cursor between the two calls -- but the 0..65535 virtual-desktop mapping
  // relies on metrics this SYSTEM agent does not reliably see, and getting them wrong throws
  // every click off-screen, which reads as input being completely dead. Keep the proven path.
  const InjectRun run = run_input_event(
      plan, point, [](long x, long y) { return SetCursorPos(x, y) != FALSE; },
      [](INPUT& input) { return SendInput(1, &input, sizeof(INPUT)) == 1; });
  if (run == InjectRun::SetCursorPosFailed) {
    diag_inject_failure("SetCursorPos", message, GetLastError(), point.x, point.y);
    return false;
  }
  if (run == InjectRun::SendInputFailed) {
    diag_inject_failure("SendInput", message, GetLastError(), point.x, point.y);
    return false;
  }
  // Button and key events only. A move sends no INPUT, and there are enough of them to exhaust
  // the budget before a single click is recorded -- and it is the click whose position is in
  // question.
  if (plan.sendInput) diag_inject_landing(message, point.x, point.y);
  return true;
}

// Reports what the agent is actually doing, once per change rather than per event.
//
// This is the only place that knows whether an injection landed, and it used to discard the
// answer -- so the host counted every event as delivered and a click that went nowhere was
// indistinguishable from one that worked. Recording the desktop name matters most: "Winlogon"
// proves the agent followed the switch, "Default" while a consent prompt is up proves it did not.
// `journal` is reported because it is the difference between "SendInput can work here" and
// "SendInput will return err=5 no matter what else is right". Without it in the log, a future
// failure on a desktop that withholds the right would look identical to the 0.2.11 bug.
void report_agent_state(const wchar_t* desktopName, bool attached, bool journal, bool injected) {
  static std::wstring lastDesktop;
  static bool lastAttached = true;
  static bool lastJournal = true;
  static bool lastInjected = true;
  const std::wstring current = desktopName ? desktopName : L"?";
  if (current == lastDesktop && attached == lastAttached && journal == lastJournal &&
      injected == lastInjected) {
    return;
  }
  lastDesktop = current;
  lastAttached = attached;
  lastJournal = journal;
  lastInjected = injected;
  char narrow[128]{};
  size_t converted = 0;
  wcstombs_s(&converted, narrow, current.c_str(), _TRUNCATE);
  diag("agent desktop=%s attach=%s journal=%s inject=%s", narrow, attached ? "ok" : "FAILED",
       journal ? "ok" : "DENIED", injected ? "ok" : "FAILED");
}

std::wstring current_desktop_name() {
  HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
  if (!desktop) return L"?";
  wchar_t name[128]{};
  DWORD needed = 0;
  if (!GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed)) return L"?";
  return name;
}

int run_agent(HANDLE readPipe) {
  apply_dpi_awareness();
  HDESK ownedDesktop = nullptr;
  // The desktop this process was CREATED on, before any attach. Distinct from the one it later
  // attaches to, and the only way to confirm from outside which of the two the input path is
  // actually judging.
  {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    char created[128] = "?";
    HDESK own = GetThreadDesktop(GetCurrentThreadId());
    if (own) {
      wchar_t wide[64]{};
      DWORD needed = 0;
      if (GetUserObjectInformationW(own, UOI_NAME, wide, sizeof(wide), &needed)) {
        size_t converted = 0;
        wcstombs_s(&converted, created, wide, _TRUNCATE);
      }
    }
    diag("agent started in session %lu, created on desktop=%s dpiAwareness=%s", session, created,
         gDpiAwarenessApplied);
  }
  for (;;) {
    SecureInputMessage message{};
    if (!read_exact(readPipe, &message, sizeof(message))) break;
    if (message.magic != kSecureInputMagic || message.size != sizeof(message)) break;
    if (message.kind == static_cast<uint16_t>(SecureInputKind::Shutdown)) break;
    // Re-attach every message on purpose: the desktop can change between any two events, and a
    // handle kept from the previous one points at the desktop that is no longer receiving input.
    // r10 T2: then the fence -- a pre-login message is injected only if that desktop is the logon one.
    bool journalPlayback = false;
    const auto step = remote60::native_poc::AgentHandleMessage(
        message, [&] { return attach_to_input_desktop(&ownedDesktop, &journalPlayback); },
        [] { return current_desktop_name(); }, [](const SecureInputMessage& m) { return inject_message(m); });
    if (step == remote60::native_poc::AgentStep::AttachFailed) {
      report_agent_state(L"(attach failed)", false, false, false);
      continue;
    }
    if (step == remote60::native_poc::AgentStep::RefusedNotLogonDesktop) {
      diag("agent: a pre-login message was not injected -- the input desktop is no longer the logon desktop");
    }
    report_agent_state(current_desktop_name().c_str(), true, journalPlayback,
                       step == remote60::native_poc::AgentStep::Injected);
  }
  diag("agent exiting");
  if (ownedDesktop) CloseDesktop(ownedDesktop);
  CloseHandle(readPipe);
  return 0;
}

// lockscreen-cred r1: the one-shot lock-screen typing (see type_lock_cred). Talks only to the trusted service
// that is its parent, over the pipe named by `id`; says who it is; takes one job; types it into the lock
// screen's password field if -- and only if -- that is the input target (replacing what the field held);
// answers; zeroes; exits. Nothing it reads is logged. Exit codes: 0 answered, 2 bad id, 3 the server was not
// the trusted service / its parent, 4 no job.
namespace {
bool agent_trusts_server(HANDLE pipe) {
#ifdef REMOTE60_SIS_TEST_SEAM
  // TEST BUILD ONLY (its parent is the lifecycle test service, which no SCM knows): the parent check stands.
  if (!remote60::native_poc::sis_test::Env(L"GNLINK_SIS_TEST_CRED_TARGET_TITLE").empty()) return true;
#endif
  return remote60::native_poc::ClassifyServerIdentity(
             remote60::native_poc::GatherServerIdentity(pipe, remote60::native_poc::kSecureInputServiceName)) ==
         remote60::native_poc::TrustedPipeStatus::Trusted;
}
}  // namespace

int run_lock_cred_agent(const std::wstring& id) {
  using remote60::native_poc::UnlockStage;
  namespace lu = remote60::native_poc::logon_ui;
  namespace lc = remote60::native_poc::lock_cred_channel;
  if (!lc::IdIsWellFormed(id)) return 2;
  const char* why = "";
  HANDLE pipe = lc::OpenFromAgent(id, lc::ParentProcessId(), agent_trusts_server, &why);
  if (pipe == INVALID_HANDLE_VALUE) return 3;
  lc::Hello hello{};
  hello.pid = GetCurrentProcessId();
  DWORD n = 0;
  lc::Job job{};
  const bool got = WriteFile(pipe, &hello, sizeof(hello), &n, nullptr) && n == sizeof(hello) &&
                   read_exact(pipe, &job, sizeof(job)) && job.magic == lc::kJobMagic && job.size == sizeof(job) &&
                   job.version == lc::kVersion && job.count <= remote60::native_poc::sealed_unlock::kMaxPasswordUtf16;
  if (!got) {
    SecureZeroMemory(&job, sizeof(job));
    CloseHandle(pipe);
    return 4;
  }
  lc::Answer answer{};
  answer.stage = static_cast<uint16_t>(UnlockStage::InjectFailed);
  DWORD mySession = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &mySession) || mySession != job.session) {
    answer.stage = static_cast<uint16_t>(UnlockStage::TargetNotReady);  // not the session it was meant for
  } else {
    // Attach to the desktop receiving input -- the lock screen's -- so the UIA and focus questions and the
    // typing are asked of it (this process was created there; the thread follows it explicitly).
    HDESK input = OpenInputDesktop(0, FALSE, kDesktopBaseAccess | DESKTOP_JOURNALPLAYBACK);
    if (!input) input = OpenInputDesktop(0, FALSE, kDesktopBaseAccess);
    if (input) (void)SetThreadDesktop(input);
    lu::TargetFn target = [] { return lu::LockScreenTarget(lu::InputDesktopName, lu::PasswordFieldIdentity); };
#ifdef REMOTE60_SIS_TEST_SEAM
    // TEST BUILD ONLY (GNLinkInputServiceLifecycleTest's child): the test's window by its title, on this
    // desktop, with the same password-field rule.
    const std::wstring title = remote60::native_poc::sis_test::Env(L"GNLINK_SIS_TEST_CRED_TARGET_TITLE");
    if (!title.empty()) {
      target = [title]() -> uint64_t {
        HWND fg = GetForegroundWindow();
        wchar_t now[256] = {};
        DWORD pid = 0;
        if (!fg || GetWindowTextW(fg, now, 256) <= 0 || title != now || !GetWindowThreadProcessId(fg, &pid)) return 0;
        return lu::FocusedPasswordFieldIdentity(fg, pid);
      };
    }
#endif
    uint32_t err = 0;
    // Codex Q1 (b): the field's content is REPLACED (a retry is never appended to an earlier attempt) -- in the
    // field of the target just verified (its process id is in the identity), right before the typing.
    const lu::InjectFn replace = [&target](const uint16_t* units, uint16_t count, uint32_t* e) {
      const uint64_t tid = target();
      if (tid == 0) {
        *e = ERROR_INVALID_WINDOW_HANDLE;
        return false;
      }
      return lu::ReplaceWithUnicode(static_cast<uint32_t>(tid >> 32), units, count, e);
    };
    const UnlockStage stage = lu::TypeIntoTarget(job.units, job.count, target, replace, [](uint32_t ms) { Sleep(ms); },
                                                 3000, &err);
    answer.stage = static_cast<uint16_t>(stage);
    answer.win32 = err;
    if (input) CloseDesktop(input);
  }
  SecureZeroMemory(&job, sizeof(job));
  (void)write_exact(pipe, &answer, sizeof(answer));
  CloseHandle(pipe);
  return 0;
}


}  // namespace

// Registration lives here, invoked by the elevated installer, so the streaming host never
// needs the rights to create or repoint a LocalSystem service. The image path is whatever this
// binary already is: an installer that put it under Program Files therefore registers an
// admin-only path, and there is no code left that can walk it back out to a writable folder.
// `preloginEnabled` raises the service to AUTO_START so it comes up before any user logs in (the
// prelogin-service feature). Start-mode discipline (prelogin-service §1 / Codex r3): a re-install or
// update must NEVER silently downgrade an existing AUTO back to DEMAND, and installing without the
// feature must not force a feature-on box off. So on an EXISTING service we only ever RAISE to AUTO when
// the feature is explicitly enabled; otherwise we leave the start type untouched (SERVICE_NO_CHANGE). A
// fresh install defaults to DEMAND (feature off) unless prelogin is enabled at install time. The runtime
// feature toggle flips the mode through the admin pipe's Enable/Disable, not through a re-install.
int install_service(bool preloginEnabled) {
  const std::wstring exe = current_executable_path();
  if (exe.empty()) return 1;
  const std::wstring quoted = L"\"" + exe + L"\"";
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
  if (!manager) return static_cast<int>(GetLastError());
  SC_HANDLE service = OpenServiceW(manager, kSecureInputServiceName, SERVICE_CHANGE_CONFIG);
  if (service) {
    // Preserve the current start type unless the feature is explicitly on (then raise to AUTO). Never
    // force DEMAND here -- that is the downgrade the contract forbids.
    const DWORD startType = preloginEnabled ? SERVICE_AUTO_START : SERVICE_NO_CHANGE;
    (void)ChangeServiceConfigW(service, SERVICE_WIN32_OWN_PROCESS, startType, SERVICE_ERROR_NORMAL,
                               quoted.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  } else {
    const DWORD startType = preloginEnabled ? SERVICE_AUTO_START : SERVICE_DEMAND_START;
    service = CreateServiceW(manager, kSecureInputServiceName, L"GNLink secure desktop input",
                             SERVICE_CHANGE_CONFIG, SERVICE_WIN32_OWN_PROCESS,
                             startType, SERVICE_ERROR_NORMAL, quoted.c_str(),
                             nullptr, nullptr, nullptr, nullptr, nullptr);
  }
  if (!service) {
    const DWORD err = GetLastError();
    CloseServiceHandle(manager);
    return static_cast<int>(err);
  }
  SERVICE_DESCRIPTIONW description{};
  description.lpDescription =
      const_cast<wchar_t*>(L"Delivers GNLink remote input to elevated and secure desktops.");
  (void)ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);
  CloseServiceHandle(service);
  CloseServiceHandle(manager);
  return 0;
}

// The service stopped (or absent): true once the SCM says STOPPED within ~10 s.
bool stop_service_and_wait(SC_HANDLE service) {
  if (!service) return true;  // not installed: nothing runs that could write
  SERVICE_STATUS status{};
  (void)ControlService(service, SERVICE_CONTROL_STOP, &status);
  for (int i = 0; i < 100; ++i) {
    SERVICE_STATUS_PROCESS live{};
    DWORD bytes = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&live), sizeof(live), &bytes)) {
      return false;
    }
    if (live.dwCurrentState == SERVICE_STOPPED) return true;
    Sleep(100);
  }
  return false;
}

int uninstall_service() {
  namespace pl = remote60::native_poc::prelogin;
  // r10 T1: the mark goes down before anything that can fail -- an SCM that cannot be opened must not
  // leave the withdrawal unrecorded.
  const bool markedFirst = pl::WriteRevokePendingAt(HKEY_LOCAL_MACHINE, pl::kOwnerFloorKey, true);
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) {
    diag("uninstall: SCM not opened err=%lu; mark=%d", GetLastError(), markedFirst ? 1 : 0);
    return markedFirst ? pl::kUninstallRevokeOwed : pl::kUninstallRevokeUnconfirmed;
  }
  SC_HANDLE service = OpenServiceW(manager, kSecureInputServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
  const DWORD openErr = service ? ERROR_SUCCESS : GetLastError();
  if (!service && openErr != ERROR_SERVICE_DOES_NOT_EXIST) {
    CloseServiceHandle(manager);
    diag("uninstall: service not opened err=%lu; mark=%d", openErr, markedFirst ? 1 : 0);
    return markedFirst ? pl::kUninstallRevokeOwed : pl::kUninstallRevokeUnconfirmed;
  }
  // r6 V4 + r9 R2: removing GNLink withdraws pre-login access as turning it off does, as ONE writer: the mark
  // first (durable -- whatever happens next, any later service start finishes the revoke before it serves),
  // then the service stops (its admin server with it: no Enable between its floor check and its publish),
  // then the Disable decision here. The floor stays (r8 V4). Success only if the record is really gone.
  remote60::native_poc::prelogin::AdminPrimitives p;
  bind_service_admin_primitives(&p);
  const auto revoke = remote60::native_poc::prelogin::RevokeForUninstall(
      p,
      [markedFirst] {
        return markedFirst || remote60::native_poc::prelogin::WriteRevokePendingAt(
                                  HKEY_LOCAL_MACHINE, remote60::native_poc::prelogin::kOwnerFloorKey, true);
      },
      [service] { return stop_service_and_wait(service); });
  diag("uninstall: pre-login access revoked=%d marked=%d stopped=%d floor=%d remove=%lu record=%d status=%d win32=%lu",
       revoke.revoked ? 1 : 0, revoke.marked ? 1 : 0, revoke.serviceStopped ? 1 : 0, revoke.floorRaised ? 1 : 0,
       static_cast<unsigned long>(revoke.removeError), revoke.recordState, static_cast<int>(revoke.reply.status),
       static_cast<unsigned long>(revoke.reply.win32));
  DWORD err = ERROR_SUCCESS;
  if (service) {
    const BOOL deleted = DeleteService(service);
    err = deleted ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(service);
    // Marked-for-delete means it disappears once the last handle closes, which just happened.
    if (!deleted && err == ERROR_SERVICE_MARKED_FOR_DELETE) err = ERROR_SUCCESS;
  }
  CloseServiceHandle(manager);
  // r10 T1: what is left, most serious first -- access not known to be off, then access off but the
  // withdrawal still owed, then the service alone not removed.
  if (!revoke.revoked) return revoke.marked ? pl::kUninstallRevokeOwed : pl::kUninstallRevokeUnconfirmed;
  if (err != ERROR_SUCCESS) {
    diag("uninstall: DeleteService failed err=%lu", static_cast<unsigned long>(err));
    return pl::kUninstallServiceNotRemoved;
  }
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  if (argc == 3 && std::wstring(argv[1]) == L"--agent") {
    const uintptr_t value = static_cast<uintptr_t>(_wcstoui64(argv[2], nullptr, 10));
    return run_agent(reinterpret_cast<HANDLE>(value));
  }
  if (argc == 3 && std::wstring(argv[1]) == L"--lockcred-agent") {  // lockscreen-cred r1: one job, then exit
    return run_lock_cred_agent(argv[2]);  // the private pipe's id -- nothing secret is ever on this line
  }
  // r6 V7: the start type moves only through the admin pipe's Enable/Disable (the record, the start type
  // and the floor in their one order). The install / toggle switches that moved it on their own --
  // --install-service-prelogin, --prelogin-autostart, --prelogin-demandstart -- had no caller and are gone.
  if (argc == 2 && std::wstring(argv[1]) == L"--install-service") return install_service(/*preloginEnabled=*/false);
  if (argc == 2 && std::wstring(argv[1]) == L"--uninstall-service") return uninstall_service();
#ifdef REMOTE60_SIS_TEST_SEAM
  if (argc == 2 && std::wstring(argv[1]) == L"--lifecycle-test") {
    remote60::native_poc::sis_test::Active() = true;
    SIS_TEST_NOTE("lifecycle test: the product service_main on this thread, no SCM");
    service_main(0, nullptr);
    SIS_TEST_NOTE("service_main returned");
    return 0;
  }
#endif
  SERVICE_TABLE_ENTRYW table[] = {
      {const_cast<wchar_t*>(kSecureInputServiceName), service_main},
      {nullptr, nullptr},
  };
  return StartServiceCtrlDispatcherW(table) ? 0 : static_cast<int>(GetLastError());
}
