// See file_copy_helper_host.hpp.

#include "file_copy_helper_host.hpp"

#include <sddl.h>
#include <wtsapi32.h>
#include <bcrypt.h>

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

TokenVerdict inspect_helper_token(HANDLE elevatedProcessToken, DWORD sessionId,
                                  const std::wstring& interactiveUserSid, HANDLE* linkedPrimaryOut) {
  TokenVerdict v;
  if (linkedPrimaryOut) *linkedPrimaryOut = nullptr;
  TOKEN_ELEVATION_TYPE type{};
  if (!token_elevation_type(elevatedProcessToken, &type)) {
    v.why = "elevation-type-unreadable";
    v.error = GetLastError();
    return v;
  }
  if (type != TokenElevationTypeFull) {
    // Not an elevated process (UAC off, or never elevated): its "linked token" is the OTHER half
    // -- the full admin one -- which is exactly what must never run the helper.
    v.why = type == TokenElevationTypeLimited ? "caller-not-elevated" : "caller-no-split-token";
    return v;
  }
  TOKEN_LINKED_TOKEN linked{};
  DWORD len = 0;
  if (!GetTokenInformation(elevatedProcessToken, TokenLinkedToken, &linked, sizeof(linked), &len) || !linked.LinkedToken) {
    v.why = "no-linked-token";
    v.error = GetLastError();
    return v;
  }
  HANDLE t = linked.LinkedToken;
  auto refuse = [&](const char* why) {
    v.why = why;
    v.error = GetLastError();
    CloseHandle(t);
    return v;
  };
  std::wstring user;
  if (!token_user_sid(t, &user)) return refuse("linked-user-unreadable");
  if (_wcsicmp(user.c_str(), interactiveUserSid.c_str()) != 0) return refuse("linked-user-is-not-the-interactive-user");
  DWORD session = 0;
  if (!token_session(t, &session)) return refuse("linked-session-unreadable");
  if (session != sessionId) return refuse("linked-session-differs");
  if (token_integrity_rid(t) != SECURITY_MANDATORY_MEDIUM_RID) return refuse("linked-token-not-medium");
  if (IsTokenRestricted(t)) return refuse("linked-token-restricted");
  TOKEN_ELEVATION_TYPE linkedType{};
  if (!token_elevation_type(t, &linkedType) || linkedType != TokenElevationTypeLimited) return refuse("linked-token-not-limited");
  HANDLE primary = nullptr;
  if (!DuplicateTokenEx(t, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primary)) {
    return refuse("linked-token-duplicate-failed");
  }
  CloseHandle(t);
  if (linkedPrimaryOut) {
    *linkedPrimaryOut = primary;
  } else {
    CloseHandle(primary);
  }
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

ConnectOutcome begin_connect(HANDLE pipe, std::unique_ptr<PendingConnect>* pending, DWORD* error) {
  pending->reset();
  *error = ERROR_SUCCESS;
  auto p = std::make_unique<PendingConnect>();
  p->io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!p->io.hEvent) {
    *error = GetLastError();
    return ConnectOutcome::Failed;
  }
  const BOOL connected = ConnectNamedPipe(pipe, &p->io);
  const DWORD e = connected ? ERROR_SUCCESS : GetLastError();
  if (connected || e == ERROR_PIPE_CONNECTED) {
    CloseHandle(p->io.hEvent);
    return ConnectOutcome::Connected;
  }
  if (e == ERROR_IO_PENDING) {
    *pending = std::move(p);
    return ConnectOutcome::Pending;
  }
  CloseHandle(p->io.hEvent);
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
      // Not finished: the OS may still complete into this storage. Leaked, never freed.
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
  if (ok) return ConnectOutcome::Connected;  // including a connection that beat the cancellation
  if (e == ERROR_OPERATION_ABORTED && early != ConnectOutcome::Connected) return early;
  *error = e;
  return ConnectOutcome::Failed;
}

}  // namespace detail

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
  if (pipe_ == INVALID_HANDLE_VALUE) {
    if (why) *why = "no-pipe";
    return false;
  }
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
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
    if (why) *why = "CreateJobObject err=" + std::to_string(GetLastError());
    return false;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    if (why) *why = "SetInformationJobObject err=" + std::to_string(GetLastError());
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
    if (why) *why = std::string(token ? "CreateProcessWithTokenW" : "CreateProcessW") + " err=" + std::to_string(GetLastError());
    return false;
  }
  if (!AssignProcessToJobObject(job_, pi.hProcess)) {
    // A helper outside the Job would outlive this object; that is not a helper we run.
    if (why) *why = "AssignProcessToJobObject err=" + std::to_string(GetLastError());
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
  if (pipe_ == INVALID_HANDLE_VALUE || helperPid_ == 0) {
    if (why) *why = "not-launched";
    return false;
  }
  auto refuse = [&](const std::string& reason) {
    if (why) *why = reason;
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
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

bool HelperLink::Send(const PipeFrame& frame, DWORD timeoutMs) {
  if (pipe_ == INVALID_HANDLE_VALUE) return false;
  if (pipe_send_frame(pipe_, frame, timeoutMs)) return true;
  const DWORD error = GetLastError();
  ClosePipe();  // part of a frame may be on the wire: nothing sent after it could be framed
  SetLastError(error);
  return false;
}

bool HelperLink::Receive(PipeFrame* frame, DWORD timeoutMs) {
  if (pipe_ == INVALID_HANDLE_VALUE) {
    SetLastError(ERROR_PIPE_NOT_CONNECTED);
    return false;
  }
  if (reader_.Receive(pipe_, frame, timeoutMs)) return true;
  const DWORD error = GetLastError();
  if (error != WAIT_TIMEOUT) ClosePipe();  // broken, aborted, or not a frame: the link is over
  SetLastError(error);
  return false;
}

void HelperLink::ClosePipe() {
  if (pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
  handshaken_ = false;
}

void HelperLink::Close() {
  ClosePipe();
  if (job_) {
    CloseHandle(job_);  // KILL_ON_JOB_CLOSE: the helper goes with it
    job_ = nullptr;
  }
  if (process_) {
    CloseHandle(process_);
    process_ = nullptr;
  }
  helperPid_ = 0;
  handshaken_ = false;
}

bool HelperLink::helper_alive() const {
  return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

bool launch_file_copy_helper(const std::wstring& helperExe, HelperLink* link, std::string* why, DWORD helloTimeoutMs) {
  HANDLE self = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &self)) {
    if (why) *why = "OpenProcessToken err=" + std::to_string(GetLastError());
    return false;
  }
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
    CloseHandle(self);
    if (why) *why = "ProcessIdToSessionId err=" + std::to_string(GetLastError());
    return false;
  }
  std::wstring interactiveUser;
  DWORD err = 0;
  if (!interactive_session_user_sid(session, &interactiveUser, &err)) {
    CloseHandle(self);
    if (why) *why = "interactive-user-unknown err=" + std::to_string(err);
    return false;
  }
  HANDLE linked = nullptr;
  const TokenVerdict verdict = inspect_helper_token(self, session, interactiveUser, &linked);
  CloseHandle(self);
  if (!verdict.ok) {
    if (why) *why = std::string("token: ") + verdict.why + " err=" + std::to_string(verdict.error);
    return false;
  }
  std::wstring linkedUser;
  bool ok = token_user_sid(linked, &linkedUser) && link->CreateServerPipe(linkedUser, why) &&
            link->Launch(helperExe, linked, nullptr, L"", why) && link->AwaitHello(helloTimeoutMs, why);
  CloseHandle(linked);
  if (!ok) link->Close();
  return ok;
}

}  // namespace remote60::native_poc::file_copy
