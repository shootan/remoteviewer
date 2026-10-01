// See file_copy_helper_host.hpp.

#include "file_copy_helper_host.hpp"

#include <sddl.h>
#include <wtsapi32.h>
#include <bcrypt.h>

#include <cstdio>
#include <vector>

#include "file_copy_pipe_io.hpp"

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace remote60::native_poc::file_copy {

namespace {

bool token_user_sid(HANDLE token, std::wstring* sidOut) {
  std::vector<uint8_t> buf(256);
  DWORD len = 0;
  if (!GetTokenInformation(token, TokenUser, buf.data(), static_cast<DWORD>(buf.size()), &len)) {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    buf.resize(len);
    if (!GetTokenInformation(token, TokenUser, buf.data(), len, &len)) return false;
  }
  auto* user = reinterpret_cast<TOKEN_USER*>(buf.data());
  LPWSTR s = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &s)) return false;
  *sidOut = s;
  LocalFree(s);
  return true;
}

bool token_session(HANDLE token, DWORD* sessionOut) {
  DWORD len = 0;
  return GetTokenInformation(token, TokenSessionId, sessionOut, sizeof(*sessionOut), &len) != FALSE;
}

bool token_elevation_type(HANDLE token, TOKEN_ELEVATION_TYPE* out) {
  DWORD len = 0;
  return GetTokenInformation(token, TokenElevationType, out, sizeof(*out), &len) != FALSE;
}

bool random_bytes(uint8_t* out, size_t n) {
  return BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

}  // namespace

bool interactive_session_user_sid(DWORD sessionId, std::wstring* sidOut, DWORD* errorOut) {
  LPWSTR user = nullptr;
  LPWSTR domain = nullptr;
  DWORD bytes = 0;
  if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSUserName, &user, &bytes) || !user ||
      user[0] == 0) {
    if (errorOut) *errorOut = GetLastError();
    if (user) WTSFreeMemory(user);
    return false;
  }
  if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSDomainName, &domain, &bytes)) {
    domain = nullptr;
  }
  std::wstring account = user;
  std::wstring domainName = domain ? domain : L"";
  WTSFreeMemory(user);
  if (domain) WTSFreeMemory(domain);

  std::vector<uint8_t> sid(SECURITY_MAX_SID_SIZE);
  DWORD sidLen = static_cast<DWORD>(sid.size());
  wchar_t refDomain[256]{};
  DWORD refLen = 256;
  SID_NAME_USE use{};
  const std::wstring qualified = domainName.empty() ? account : domainName + L"\\" + account;
  if (!LookupAccountNameW(nullptr, qualified.c_str(), sid.data(), &sidLen, refDomain, &refLen, &use)) {
    if (errorOut) *errorOut = GetLastError();
    return false;
  }
  LPWSTR s = nullptr;
  if (!ConvertSidToStringSidW(sid.data(), &s)) {
    if (errorOut) *errorOut = GetLastError();
    return false;
  }
  *sidOut = s;
  LocalFree(s);
  return true;
}

bool current_process_user_sid(std::wstring* sidOut) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  const bool ok = token_user_sid(token, sidOut);
  CloseHandle(token);
  return ok;
}

DWORD token_integrity_rid(HANDLE token) {
  std::vector<uint8_t> buf(256);
  DWORD len = 0;
  if (!GetTokenInformation(token, TokenIntegrityLevel, buf.data(), static_cast<DWORD>(buf.size()), &len)) {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return 0;
    buf.resize(len);
    if (!GetTokenInformation(token, TokenIntegrityLevel, buf.data(), len, &len)) return 0;
  }
  auto* label = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf.data());
  const DWORD count = *GetSidSubAuthorityCount(label->Label.Sid);
  if (count == 0) return 0;
  return *GetSidSubAuthority(label->Label.Sid, count - 1);
}

TokenFacts read_token_facts(HANDLE token) {
  TokenFacts f;
  auto note = [&] {
    if (f.error == 0) f.error = GetLastError();
  };
  DWORD len = 0;
  f.typeRead = GetTokenInformation(token, TokenType, &f.type, sizeof(f.type), &len) != FALSE;
  if (!f.typeRead) note();
  f.userRead = token_user_sid(token, &f.userSid);
  if (!f.userRead) note();
  f.sessionRead = token_session(token, &f.session);
  if (!f.sessionRead) note();
  f.integrityRid = token_integrity_rid(token);
  if (f.integrityRid == 0) note();
  TOKEN_ELEVATION elevation{};
  f.elevationRead = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &len) != FALSE;
  if (f.elevationRead) {
    f.elevated = elevation.TokenIsElevated != 0;
  } else {
    note();
  }
  f.elevationTypeRead = token_elevation_type(token, &f.elevationType);
  if (!f.elevationTypeRead) note();
  SetLastError(ERROR_SUCCESS);
  f.restricted = IsTokenRestricted(token) != FALSE;
  // IsTokenRestricted cannot tell "no" from "could not read": an error counts as restricted.
  if (!f.restricted && GetLastError() != ERROR_SUCCESS) {
    f.restricted = true;
    note();
  }
  return f;
}

TokenVerdict judge_helper_token(const TokenFacts& f, DWORD sessionId, const std::wstring& interactiveUserSid) {
  TokenVerdict v;
  v.error = f.error;
  if (!f.typeRead) v.why = "type-unreadable";
  else if (!f.userRead) v.why = "user-unreadable";
  else if (!f.sessionRead) v.why = "session-unreadable";
  else if (f.integrityRid == 0) v.why = "integrity-unreadable";
  else if (!f.elevationRead) v.why = "elevation-unreadable";
  else if (!f.elevationTypeRead) v.why = "elevation-type-unreadable";
  else if (f.type != TokenPrimary) v.why = "not-primary";
  else if (_wcsicmp(f.userSid.c_str(), interactiveUserSid.c_str()) != 0) v.why = "user-is-not-the-interactive-user";
  else if (f.session != sessionId) v.why = "session-differs";
  else if (f.integrityRid != SECURITY_MANDATORY_MEDIUM_RID) v.why = "not-medium";
  else if (f.elevated) v.why = "elevated";
  else if (f.elevationType == TokenElevationTypeFull) v.why = "full-elevation";
  else if (f.restricted) v.why = "restricted";
  else {
    v.ok = true;
    v.why = "ok";
    v.error = 0;
  }
  return v;
}

std::string describe_token_facts(const TokenFacts& f) {
  auto hex = [](DWORD x) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%lx", static_cast<unsigned long>(x));
    return std::string(b);
  };
  std::string s = "type=";
  s += !f.typeRead ? "?" : f.type == TokenPrimary ? "primary" : "impersonation";
  s += " il=" + (f.integrityRid ? hex(f.integrityRid) : std::string("?"));
  s += " elevated=" + (f.elevationRead ? std::string(f.elevated ? "1" : "0") : std::string("?"));
  s += " elevType=";
  if (!f.elevationTypeRead) s += "?";
  else if (f.elevationType == TokenElevationTypeDefault) s += "default";
  else if (f.elevationType == TokenElevationTypeLimited) s += "limited";
  else s += "full";
  s += " restricted=" + std::string(f.restricted ? "1" : "0");
  s += " session=" + (f.sessionRead ? std::to_string(f.session) : std::string("?"));
  return s;
}

const char* launch_failure_name(LaunchFailure f) {
  switch (f) {
    case LaunchFailure::None: return "none";
    case LaunchFailure::Missing: return "missing";
    case LaunchFailure::TokenRejected: return "token-rejected";
    case LaunchFailure::NoShell: return "no-shell";
    case LaunchFailure::SpawnFailed: return "spawn-failed";
    case LaunchFailure::HandshakeFailed: return "handshake-failed";
  }
  return "none";
}

LaunchFailure launch_failure_of(const std::string& why) {
  for (LaunchFailure f : {LaunchFailure::Missing, LaunchFailure::TokenRejected, LaunchFailure::NoShell,
                          LaunchFailure::SpawnFailed, LaunchFailure::HandshakeFailed}) {
    const std::string prefix = std::string(launch_failure_name(f)) + ":";
    if (why.compare(0, prefix.size(), prefix) == 0) return f;
  }
  return LaunchFailure::None;
}

TokenVerdict acquire_shell_token(DWORD sessionId, const std::wstring& interactiveUserSid, UniqueHandle* primaryOut,
                                 LaunchFailure* cls, std::string* diag) {
  primaryOut->reset();
  *cls = LaunchFailure::None;
  TokenVerdict v;
  auto refuse = [&](LaunchFailure c, const char* stage, const char* why, DWORD error) {
    *cls = c;
    v.ok = false;
    v.stage = stage;
    v.why = why;
    v.error = error;
    return v;
  };
  const HWND shell = GetShellWindow();
  if (!shell) return refuse(LaunchFailure::NoShell, "shell-window", "no-shell-window", GetLastError());
  DWORD pid = 0;
  GetWindowThreadProcessId(shell, &pid);
  if (pid == 0) return refuse(LaunchFailure::NoShell, "shell-window", "shell-pid-unknown", GetLastError());
  UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
  if (!process) return refuse(LaunchFailure::NoShell, "shell-process", "shell-process-open", GetLastError());
  UniqueHandle shellToken;
  if (!OpenProcessToken(process.get(), TOKEN_QUERY | TOKEN_DUPLICATE, shellToken.put())) {
    return refuse(LaunchFailure::TokenRejected, "shell-token", "shell-token-open", GetLastError());
  }
  const TokenFacts facts = read_token_facts(shellToken.get());
  if (diag) *diag = "shell: " + describe_token_facts(facts);
  const TokenVerdict shellVerdict = judge_helper_token(facts, sessionId, interactiveUserSid);
  if (!shellVerdict.ok) return refuse(LaunchFailure::TokenRejected, "shell-token", shellVerdict.why, shellVerdict.error);
  UniqueHandle primary;
  if (!DuplicateTokenEx(shellToken.get(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY, nullptr,
                        SecurityImpersonation, TokenPrimary, primary.put())) {
    return refuse(LaunchFailure::TokenRejected, "duplicate", "duplicate-failed", GetLastError());
  }
  // The token launched is the duplicate: it is judged too, not assumed to be its source.
  const TokenVerdict finalVerdict = judge_helper_token(read_token_facts(primary.get()), sessionId, interactiveUserSid);
  if (!finalVerdict.ok) return refuse(LaunchFailure::TokenRejected, "duplicate", finalVerdict.why, finalVerdict.error);
  *primaryOut = std::move(primary);
  v.ok = true;
  v.why = "ok";
  return v;
}

HelloCheck verify_hello(const Hello& hello, const std::array<uint8_t, kNonceBytes>& expectedNonce,
                        uint32_t clientPid, uint32_t launchedPid) {
  HelloCheck c;
  if (launchedPid == 0 || clientPid != launchedPid) {
    c.why = "client-pid-is-not-the-launched-helper";
    return c;
  }
  if (hello.pid != clientPid) {
    c.why = "hello-pid-differs-from-pipe-client";
    return c;
  }
  if (hello.version != kPipeVersion) {
    c.why = "hello-version";
    return c;
  }
  // Constant-time compare: the nonce is the helper's proof, not a checksum.
  uint8_t diff = 0;
  for (size_t i = 0; i < kNonceBytes; ++i) diff |= static_cast<uint8_t>(hello.nonce[i] ^ expectedNonce[i]);
  if (diff != 0) {
    c.why = "nonce-mismatch";
    return c;
  }
  c.ok = true;
  c.why = "ok";
  return c;
}

bool medium_launch_allowed(TOKEN_ELEVATION_TYPE type, DWORD integrityRid) {
  if (type == TokenElevationTypeFull) return false;
  return integrityRid < SECURITY_MANDATORY_HIGH_RID;
}

namespace detail {

std::function<void()>& before_connect_cancel_hook() {
  static std::function<void()> hook;
  return hook;
}

std::atomic<uint32_t>& connect_attempts() {
  static std::atomic<uint32_t> n{0};
  return n;
}

ConnectOutcome begin_connect(HANDLE pipe, std::unique_ptr<PendingConnect>* pending, DWORD* error) {
  pending->reset();
  *error = ERROR_SUCCESS;
  // Admission before anything else (r4): a connection wait counts against the same bound as a
  // read or a write, so at kMaxOrphanedIo no event is created and ConnectNamedPipe is not called.
  // Without this a link that kept timing out and getting stuck could leave storage without end.
  if (!file_copy::detail::reserve_io()) {
    *error = ERROR_NO_SYSTEM_RESOURCES;
    return ConnectOutcome::Failed;
  }
  auto p = std::make_unique<PendingConnect>();
  p->io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!p->io.hEvent) {
    *error = GetLastError();
    file_copy::detail::release_io();
    return ConnectOutcome::Failed;
  }
  connect_attempts().fetch_add(1);
  const BOOL connected = ConnectNamedPipe(pipe, &p->io);
  const DWORD e = connected ? ERROR_SUCCESS : GetLastError();
  if (connected || e == ERROR_PIPE_CONNECTED) {
    CloseHandle(p->io.hEvent);
    file_copy::detail::release_io();
    return ConnectOutcome::Connected;
  }
  if (e == ERROR_IO_PENDING) {
    *pending = std::move(p);  // the reservation travels with the wait; settle_connect frees it
    return ConnectOutcome::Pending;
  }
  CloseHandle(p->io.hEvent);
  file_copy::detail::release_io();
  *error = e;
  return ConnectOutcome::Failed;
}

ConnectOutcome settle_connect(HANDLE pipe, std::unique_ptr<PendingConnect>& pending, DWORD timeoutMs, HANDLE process,
                              DWORD* error) {
  *error = ERROR_SUCCESS;
  if (!pending) return ConnectOutcome::Failed;
  HANDLE waits[2] = {pending->io.hEvent, process};
  const DWORD w = WaitForMultipleObjects(process ? 2 : 1, waits, FALSE, timeoutMs);
  ConnectOutcome early = ConnectOutcome::Connected;
  if (w != WAIT_OBJECT_0) {
    early = (w == WAIT_OBJECT_0 + 1) ? ConnectOutcome::ProcessExited : ConnectOutcome::Timeout;
    if (before_connect_cancel_hook()) before_connect_cancel_hook()();
    CancelIoEx(pipe, &pending->io);
    if (file_copy::detail::simulate_stuck_cancel() ||
        WaitForSingleObject(pending->io.hEvent, file_copy::detail::cancel_wait_ms()) != WAIT_OBJECT_0) {
      // Not finished: the OS may still complete into this storage. Leaked, never freed -- and its
      // reservation is kept, which is how the bound counts it.
      (void)pending.release();
      file_copy::detail::orphan_counter().fetch_add(1);
      return ConnectOutcome::Stuck;
    }
  }
  DWORD moved = 0;
  const BOOL ok = GetOverlappedResult(pipe, &pending->io, &moved, FALSE);
  const DWORD e = ok ? ERROR_SUCCESS : GetLastError();
  CloseHandle(pending->io.hEvent);
  pending.reset();
  file_copy::detail::release_io();  // the OS is done with the storage: the wait no longer counts
  if (ok) return ConnectOutcome::Connected;  // including a connection that beat the cancellation
  if (e == ERROR_OPERATION_ABORTED && early != ConnectOutcome::Connected) return early;
  *error = e;
  return ConnectOutcome::Failed;
}

}  // namespace detail

namespace {
std::atomic<uint32_t> gPipesCreated{0}, gPipesClosed{0};
std::function<void(DWORD)>& receive_probe() {
  static std::function<void(DWORD)> probe;
  return probe;
}
}  // namespace

uint32_t HelperLink::pipes_created() { return gPipesCreated.load(); }
uint32_t HelperLink::pipes_closed() { return gPipesClosed.load(); }
void HelperLink::SetReceiveProbeForTest(std::function<void(DWORD)> probe) {
  receive_probe() = std::move(probe);
  OutputDebugStringA("TEST PROBE receive crossing installed");
}

HelperLink::~HelperLink() { Close(); }

bool HelperLink::CreateServerPipe(const std::wstring& userSid, std::string* why) {
  Close();
  std::array<uint8_t, kPipeNameRandomBytes> random{};
  if (!random_bytes(random.data(), random.size()) || !random_bytes(nonce_.data(), nonce_.size())) {
    if (why) *why = "random";
    return false;
  }
  pipeName_ = pipe_name_for(random);
  // DACL: that user, full access, nothing else (protected: nothing inherited). Label: Medium,
  // no-write-up, so a Medium client may open what a High creator made.
  const std::wstring sddl = L"D:P(A;;GA;;;" + userSid + L")S:(ML;;NW;;;ME)";
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
    if (why) *why = "security-descriptor err=" + std::to_string(GetLastError());
    return false;
  }
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = sd;
  sa.bInheritHandle = FALSE;
  pipe_ = CreateNamedPipeW(pipeName_.c_str(),
                           PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                           64 * 1024, 64 * 1024, 0, &sa);
  const DWORD err = GetLastError();
  LocalFree(sd);
  if (pipe_ == INVALID_HANDLE_VALUE) {
    if (why) *why = "CreateNamedPipe err=" + std::to_string(err);
    return false;
  }
  ++gPipesCreated;
  return true;
}

std::wstring HelperLink::helper_command_line(const std::wstring& exe, const std::wstring& extraArgs) const {
  std::wstring cmd = L"\"" + exe + L"\" --pipe " + pipeName_ + L" --nonce " + hex_encode(nonce_.data(), nonce_.size()) +
                     L" --host-pid " + std::to_wstring(GetCurrentProcessId());
  if (!extraArgs.empty()) cmd += L" " + extraArgs;
  return cmd;
}

bool HelperLink::Launch(const std::wstring& exe, HANDLE token, const wchar_t* desktop, const std::wstring& extraArgs,
                        std::string* why) {
  lastError_ = 0;
  if (pipe_ == INVALID_HANDLE_VALUE) {
    if (why) *why = "no-pipe";
    return false;
  }
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    lastError_ = GetLastError();
    if (why) *why = "helper-exe-missing";
    return false;
  }
  if (!token) {
    // Starting the helper as THIS process is only for a process that is already the plain user.
    // An elevated caller would hand it administrator rights: refused, whatever asked. (r2)
    HANDLE self = nullptr;
    TOKEN_ELEVATION_TYPE type{};
    DWORD rid = SECURITY_MANDATORY_HIGH_RID;  // unreadable counts as elevated
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &self)) {
      DWORD len = 0;
      if (!GetTokenInformation(self, TokenElevationType, &type, sizeof(type), &len)) type = TokenElevationTypeFull;
      rid = token_integrity_rid(self);
      if (rid == 0) rid = SECURITY_MANDATORY_HIGH_RID;
      CloseHandle(self);
    } else {
      type = TokenElevationTypeFull;
    }
    if (!medium_launch_allowed(type, rid)) {
      if (why) *why = "medium-launch-refused-caller-elevated";
      return false;
    }
  }
  job_ = CreateJobObjectW(nullptr, nullptr);
  if (!job_) {
    lastError_ = GetLastError();
    if (why) *why = "CreateJobObject err=" + std::to_string(lastError_);
    return false;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    lastError_ = GetLastError();
    if (why) *why = "SetInformationJobObject err=" + std::to_string(lastError_);
    return false;
  }
  std::wstring cmd = helper_command_line(exe, extraArgs);
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  std::wstring desktopSpec = desktop ? desktop : L"";
  if (!desktopSpec.empty()) si.lpDesktop = desktopSpec.data();
  PROCESS_INFORMATION pi{};
  BOOL ok;
  const DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
  if (token) {
    ok = CreateProcessWithTokenW(token, 0, nullptr, mutableCmd.data(), flags, nullptr, nullptr, &si, &pi);
  } else {
    ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi);
  }
  if (!ok) {
    lastError_ = GetLastError();
    if (why) *why = std::string(token ? "CreateProcessWithTokenW" : "CreateProcessW") + " err=" + std::to_string(lastError_);
    return false;
  }
  if (!AssignProcessToJobObject(job_, pi.hProcess)) {
    // A helper outside the Job would outlive this object; that is not a helper we run.
    lastError_ = GetLastError();
    if (why) *why = "AssignProcessToJobObject err=" + std::to_string(lastError_);
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return false;
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  process_ = pi.hProcess;
  helperPid_ = pi.dwProcessId;
  return true;
}

bool HelperLink::AwaitHello(DWORD timeoutMs, std::string* why) {
  lastError_ = 0;
  if (pipe_ == INVALID_HANDLE_VALUE || helperPid_ == 0) {
    if (why) *why = "not-launched";
    return false;
  }
  auto refuse = [&](const std::string& reason) {
    if (why) *why = reason;
    ClosePipe();  // the one way a pipe handle is closed (nothing is in flight yet: no reader exists)
    return false;
  };
  std::unique_ptr<detail::PendingConnect> pending;
  DWORD err = 0;
  detail::ConnectOutcome outcome = detail::begin_connect(pipe_, &pending, &err);
  if (outcome == detail::ConnectOutcome::Pending) outcome = detail::settle_connect(pipe_, pending, timeoutMs, process_, &err);
  switch (outcome) {
    case detail::ConnectOutcome::Connected:
      break;
    case detail::ConnectOutcome::Timeout:
      return refuse("connect-timeout");
    case detail::ConnectOutcome::ProcessExited:
      return refuse("helper-exited-before-connecting");
    case detail::ConnectOutcome::Stuck:
      return refuse("connect-cancel-stuck");  // the storage is orphaned; this link is over
    default:
      lastError_ = err;
      return refuse("ConnectNamedPipe err=" + std::to_string(err));
  }
  ULONG clientPid = 0;
  if (!GetNamedPipeClientProcessId(pipe_, &clientPid)) return refuse("client-pid-unreadable");
  PipeFrame frame;
  reader_ = FrameReader{};
  if (!reader_.Receive(pipe_, &frame, timeoutMs)) return refuse("no-hello");
  Hello hello;
  if (!decode(frame, &hello)) return refuse("hello-malformed");
  const HelloCheck check = verify_hello(hello, nonce_, clientPid, helperPid_);
  if (!check.ok) return refuse(check.why);
  if (!pipe_send_frame(pipe_, encode_hello_ack(), 5000)) return refuse("hello-ack-send");
  handshaken_ = true;
  return true;
}

bool HelperLink::Enter(HANDLE* h) {
  std::lock_guard<std::mutex> lock(hmu_);
  if (pipe_ == INVALID_HANDLE_VALUE || closeRequested_) return false;
  ++ioInFlight_;
  *h = pipe_;
  return true;
}

void HelperLink::Leave(bool failed) {
  std::lock_guard<std::mutex> lock(hmu_);
  --ioInFlight_;
  if (failed) closeRequested_ = true;  // broken, aborted, not a frame, or a partial send: the link is over
  if (closeRequested_ && ioInFlight_ == 0) CloseNowLocked();
}

void HelperLink::CloseNowLocked() {
  if (pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
    ++gPipesClosed;
  }
  closeRequested_ = false;
  handshaken_ = false;
}

bool HelperLink::Send(const PipeFrame& frame, DWORD timeoutMs) {
  HANDLE h = INVALID_HANDLE_VALUE;
  if (!Enter(&h)) {
    SetLastError(ERROR_PIPE_NOT_CONNECTED);
    return false;
  }
  const bool ok = pipe_send_frame(h, frame, timeoutMs);
  const DWORD error = GetLastError();
  Leave(!ok);  // part of a frame may be on the wire: nothing sent after it could be framed
  SetLastError(error);
  return ok;
}

bool HelperLink::Receive(PipeFrame* frame, DWORD timeoutMs) {
  HANDLE h = INVALID_HANDLE_VALUE;
  if (!Enter(&h)) {
    SetLastError(ERROR_PIPE_NOT_CONNECTED);
    return false;
  }
  if (auto& probe = receive_probe()) probe(helperPid_);
  const bool ok = reader_.Receive(h, frame, timeoutMs);
  const DWORD error = GetLastError();
  Leave(!ok && error != WAIT_TIMEOUT);  // a timeout keeps the link; anything else ends it
  SetLastError(error);
  return ok;
}

void HelperLink::ClosePipe() {
  std::lock_guard<std::mutex> lock(hmu_);
  handshaken_ = false;
  if (pipe_ == INVALID_HANDLE_VALUE) return;
  if (ioInFlight_ > 0) {
    // Somebody is reading or writing on it: the handle stays open for them. Their I/O is cancelled
    // and they close the handle on their way out (Leave). No second close, no I/O on a closed one.
    closeRequested_ = true;
    CancelIoEx(pipe_, nullptr);
    return;
  }
  CloseNowLocked();
}

void HelperLink::Close() {
  ClosePipe();
  HANDLE job = nullptr, process = nullptr;
  {
    std::lock_guard<std::mutex> lock(hmu_);
    job = job_;
    process = process_;
    job_ = nullptr;
    process_ = nullptr;
    helperPid_ = 0;
    handshaken_ = false;
  }
  if (job) CloseHandle(job);  // KILL_ON_JOB_CLOSE: the helper goes with it
  if (process) CloseHandle(process);
}

bool HelperLink::helper_alive() const {
  std::lock_guard<std::mutex> lock(hmu_);
  return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

bool HelperLink::pipe_open() const {
  std::lock_guard<std::mutex> lock(hmu_);
  return pipe_ != INVALID_HANDLE_VALUE && !closeRequested_;
}

namespace {

// "<class>: stage=<step> err=<win32> (<detail>)" -- launch_failure_of reads the class back.
std::string launch_why(LaunchFailure cls, const char* stage, DWORD error, const std::string& detail) {
  std::string s = std::string(launch_failure_name(cls)) + ": stage=" + stage + " err=" + std::to_string(error);
  if (!detail.empty()) s += " (" + detail + ")";
  return s;
}

// What every launch does once it knows the helper's user: the pipe, the start (Job, suspended,
// resume), the handshake -- each refusal classified, with the Win32 error the link recorded.
bool start_and_greet(HelperLink* link, const std::wstring& userSid, const std::wstring& helperExe, HANDLE token,
                     const wchar_t* desktop, const std::wstring& extraArgs, DWORD helloTimeoutMs, std::string* why) {
  std::string detail;
  if (!link->CreateServerPipe(userSid, &detail)) {
    if (why) *why = launch_why(LaunchFailure::SpawnFailed, "pipe", 0, detail);
    return false;
  }
  if (!link->Launch(helperExe, token, desktop, extraArgs, &detail)) {
    const LaunchFailure cls = detail == "helper-exe-missing" ? LaunchFailure::Missing : LaunchFailure::SpawnFailed;
    if (why) *why = launch_why(cls, "launch", link->last_error(), detail);
    return false;
  }
  if (!link->AwaitHello(helloTimeoutMs, &detail)) {
    if (why) *why = launch_why(LaunchFailure::HandshakeFailed, "hello", link->last_error(), detail);
    return false;
  }
  return true;
}

// For the log only: what this process's TokenLinkedToken is (the old source: 1346 on duplication).
// Its type and impersonation level, nothing else; the handle is closed at once.
std::string linked_token_note() {
  UniqueHandle self;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, self.put())) return "linked: ?";
  TOKEN_LINKED_TOKEN linked{};
  DWORD len = 0;
  if (!GetTokenInformation(self.get(), TokenLinkedToken, &linked, sizeof(linked), &len) || !linked.LinkedToken) {
    return "linked: none err=" + std::to_string(GetLastError());
  }
  UniqueHandle t(linked.LinkedToken);
  TOKEN_TYPE type{};
  if (!GetTokenInformation(t.get(), TokenType, &type, sizeof(type), &len)) return "linked: type=?";
  if (type == TokenPrimary) return "linked: type=primary";
  SECURITY_IMPERSONATION_LEVEL level{};
  if (!GetTokenInformation(t.get(), TokenImpersonationLevel, &level, sizeof(level), &len)) {
    return "linked: type=impersonation level=?";
  }
  static const char* const kLevels[] = {"anonymous", "identification", "impersonation", "delegation"};
  return std::string("linked: type=impersonation level=") + (level >= 0 && level <= 3 ? kLevels[level] : "?");
}

}  // namespace

bool launch_file_copy_helper(const std::wstring& helperExe, HelperLink* link, std::string* why, DWORD helloTimeoutMs,
                             std::string* diag) {
  if (diag) diag->clear();
  // The file first: a missing helper is an install problem, and no token step may hide it (the
  // 0.2.146 field log showed only the token failure while the executable was not even there).
  if (GetFileAttributesW(helperExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (why) *why = launch_why(LaunchFailure::Missing, "helper-exe", GetLastError(), "helper-exe-missing");
    return false;
  }
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
    if (why) *why = launch_why(LaunchFailure::TokenRejected, "host-session", GetLastError(), "");
    return false;
  }
  std::wstring interactiveUser;
  DWORD err = 0;
  if (!interactive_session_user_sid(session, &interactiveUser, &err)) {
    if (why) *why = launch_why(LaunchFailure::TokenRejected, "interactive-user", err, "interactive-user-unknown");
    return false;
  }
  UniqueHandle token;
  LaunchFailure cls = LaunchFailure::None;
  std::string shellNote;
  const TokenVerdict verdict = acquire_shell_token(session, interactiveUser, &token, &cls, &shellNote);
  const std::string tokens = (shellNote.empty() ? std::string("shell: ?") : shellNote) + "; " + linked_token_note();
  if (diag) *diag = tokens;
  if (!verdict.ok) {
    if (why) *why = launch_why(cls, verdict.stage, verdict.error, std::string(verdict.why) + "; " + tokens);
    return false;
  }
  // The pipe is for the user the token was judged to be: the interactive user.
  const bool ok = start_and_greet(link, interactiveUser, helperExe, token.get(), nullptr, L"", helloTimeoutMs, why);
  if (!ok) {
    if (why) *why += "; " + tokens;
    link->Close();
  }
  return ok;
}

bool launch_file_copy_helper_as_self(const std::wstring& helperExe, const wchar_t* desktop, const std::wstring& extraArgs,
                                     HelperLink* link, std::string* why, DWORD helloTimeoutMs) {
  if (GetFileAttributesW(helperExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (why) *why = launch_why(LaunchFailure::Missing, "helper-exe", GetLastError(), "helper-exe-missing");
    return false;
  }
  std::wstring sid;
  if (!current_process_user_sid(&sid)) {
    if (why) *why = launch_why(LaunchFailure::TokenRejected, "self-user", GetLastError(), "no user SID");
    return false;
  }
  const bool ok = start_and_greet(link, sid, helperExe, nullptr, desktop, extraArgs, helloTimeoutMs, why);
  if (!ok) link->Close();
  return ok;
}

}  // namespace remote60::native_poc::file_copy
