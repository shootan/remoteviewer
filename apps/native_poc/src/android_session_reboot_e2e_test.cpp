// The Android viewer's remote-reboot path end to end (prelogin-ui r1, item 8): the session controller
// the phone runs (ClientSessionController -- the same object native_bridge.cpp drives) against a real
// GNLinkStream on loopback. One confirm -> RequestReboot -> ControlRebootRequest (84) sent once -> the
// host's ControlRebootReply (85) parsed and handed back -- nothing re-sent.
//
// SAFETY: the host this test starts must not be able to restart this PC. It is launched with a
// restricted token from which SeShutdownPrivilege is DELETED (not just disabled -- it cannot be
// re-enabled), and the test verifies the child's token before anything is sent; without that it sends
// nothing. On loopback with no directory the session is also not directory-authenticated, so the
// host's gate answers NotAuthorized and never reaches its OS call; the deleted privilege is the
// backstop if that gate ever changed.
//
// Isolation as the sibling host e2e tests: staging directory inside the repository, ports picked at
// run time, a never-answering GNLinkCapture, a job object that takes the host with this process.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "native_video_client_session.hpp"
#include "reboot_wire.hpp"

namespace {

using remote60::native_poc::ClientSessionConnectArgs;
using remote60::native_poc::ClientSessionController;
using remote60::native_poc::ClientSessionState;

class NullSink : public remote60::native_poc::ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(remote60::native_poc::UdpH264AssembledFrame&& frame) override { (void)frame; }
  void OnVideoStreamReset() override {}
};

int gFailures = 0;
int gChecks = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

bool wait_until(const std::function<bool()>& done, int budgetMs) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  while (GetTickCount() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return done();
}

std::wstring self_path() {
  std::wstring path(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(n);
  return path;
}

std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

// A primary token like this process's, with SeShutdownPrivilege removed from it entirely.
HANDLE token_without_shutdown_privilege() {
  HANDLE self = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &self)) return nullptr;
  LUID_AND_ATTRIBUTES del{};
  if (!LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &del.Luid)) {
    CloseHandle(self);
    return nullptr;
  }
  HANDLE restricted = nullptr;
  const BOOL ok = CreateRestrictedToken(self, 0, 0, nullptr, 1, &del, 0, nullptr, &restricted);
  CloseHandle(self);
  return ok ? restricted : nullptr;
}

// Whether the process's token holds SeShutdownPrivilege at all (enabled or not). -1 = could not tell.
int process_has_shutdown_privilege(HANDLE process) {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &tok)) return -1;
  DWORD need = 0;
  GetTokenInformation(tok, TokenPrivileges, nullptr, 0, &need);
  std::vector<uint8_t> buf(need ? need : 1);
  int result = -1;
  if (GetTokenInformation(tok, TokenPrivileges, buf.data(), need, &need)) {
    LUID shutdown{};
    LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &shutdown);
    const auto* tp = reinterpret_cast<const TOKEN_PRIVILEGES*>(buf.data());
    result = 0;
    for (DWORD i = 0; i < tp->PrivilegeCount; ++i) {
      if (tp->Privileges[i].Luid.LowPart == shutdown.LowPart && tp->Privileges[i].Luid.HighPart == shutdown.HighPart) result = 1;
    }
  }
  CloseHandle(tok);
  return result;
}

int count_lines(const std::wstring& path, const std::string& needle) {
  std::ifstream in(path);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) n += line.find(needle) != std::string::npos ? 1 : 0;
  return n;
}

bool host_e2e_allowed() {
  wchar_t value[8]{};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_ALLOW_HOST_E2E", value, 8);
  return n > 0 && value[0] == L'1';
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--thumbnail") {  // staged as GNLinkCapture.exe: never answers
      Sleep(120000);
      return 0;
    }
  }
  if (!host_e2e_allowed()) {
    std::printf("SKIP  android_session_reboot_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return remote60::native_poc::e2e::kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  NullSink sink;
  ClientSessionController phone;
  check("not connected: a reboot request is not taken (no id)", phone.RequestReboot() == 0);

  const uint16_t mediaPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  if (mediaPort == 0) {
    std::printf("FAIL  no free UDP port\n");
    return 1;
  }
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"phone_reboot")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged = CopyFileW((myDir + L"GNLinkStream.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
                      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper are staged", staged);

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  const std::wstring hostLogPath = dir + L"host.log";
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE restricted = token_without_shutdown_privilege();
  check("a token without SeShutdownPrivilege is made for the host", restricted != nullptr);

  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged && restricted && hostLog != INVALID_HANDLE_VALUE) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                       L" --bind-port " + std::to_wstring(mediaPort) + L" --seconds 90";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hostLog;
    si.hStdError = hostLog;
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = remote60::native_poc::e2e::e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = remote60::native_poc::e2e::e2e_command_is_isolated(cmd, dir, &isoWhy) &&
                       remote60::native_poc::e2e::e2e_path_is_under(
                           remote60::native_poc::e2e::e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = isoOk && CreateProcessAsUserW(restricted, nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                             isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started (restricted token)", launched, launched ? "" : std::to_string(GetLastError()));
  const int hostHasShutdown = launched ? process_has_shutdown_privilege(hostPi.hProcess) : -1;
  // The safety precondition: nothing is sent unless the host provably cannot restart this PC.
  const bool safe = hostHasShutdown == 0;
  check("SAFETY: the host's token holds no SeShutdownPrivilege at all (it cannot restart this PC)", safe,
        std::to_string(hostHasShutdown));

  if (launched && safe) {
    ClientSessionConnectArgs args;
    args.host = "127.0.0.1";
    args.videoPort = mediaPort;
    args.controlPort = mediaPort;
    args.requireUdpHello = true;
    args.requireTcpControl = false;
    args.controlOverUdp = true;
    args.controlIntervalMs = 200;
    args.encodedFrameSink = &sink;
    check("the phone's session connects", phone.Connect(args));
    const bool up = wait_until([&] { return phone.Snapshot().state == ClientSessionState::Connected; }, 15000);
    check("...and reaches Connected", up, phone.Snapshot().status);
    const bool flags = wait_until(
        [&] { return (phone.HostCaptureFlags() & remote60::native_poc::kCaptureFlagRemoteRebootV1) != 0; }, 8000);
    check("the host advertises remote reboot (Pong 0x4000), read by the phone's session", flags,
          std::to_string(phone.HostCaptureFlags()));

    const uint32_t id = phone.RequestReboot();
    check("one confirm: a request id (never 0)", id != 0, std::to_string(id));
    check("a second confirm while it is out: not taken", phone.RequestReboot() == 0);
    uint32_t gotId = 0, win32 = 0;
    int outcome = -1;
    const bool answered = wait_until([&] { return phone.TakeRebootOutcome(&gotId, &outcome, &win32); }, 8000);
    check("the host's answer (85) comes back for that request", answered && gotId == id,
          std::to_string(gotId) + " outcome " + std::to_string(outcome));
    check("...NotAuthorized: a loopback session without the directory is not allowed to restart",
          outcome == static_cast<int>(remote60::native_poc::RebootResult::NotAuthorized), std::to_string(outcome));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    uint32_t againId = 0, againWin32 = 0;
    int againOutcome = -1;
    check("nothing more comes (one answer per request, none re-sent)", !phone.TakeRebootOutcome(&againId, &againOutcome, &againWin32));
    check("the session is still up after a refused request", phone.Snapshot().state == ClientSessionState::Connected,
          phone.Snapshot().status);
    phone.Disconnect();
  }

  if (launched) {
    TerminateProcess(hostPi.hProcess, 0);
    WaitForSingleObject(hostPi.hProcess, 10000);
  }
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  if (launched) {
    const int hostLines = count_lines(hostLogPath, "[native-video-host][control][reboot] requestId=");
    check("the host saw exactly one reboot request", hostLines == 1, std::to_string(hostLines));
    check("...and answered it not authorized (auth=0 result=2)",
          count_lines(hostLogPath, "[native-video-host][control][reboot] requestId=") == 1 &&
              count_lines(hostLogPath, " auth=0 result=2 ") == 1);
    CloseHandle(hostPi.hProcess);
    CloseHandle(hostPi.hThread);
  }
  if (restricted) CloseHandle(restricted);
  CloseHandle(job);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
