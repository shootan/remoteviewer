// UAC-1-time real-SCM positive bundle for secure-pipe-identity (NOT shipped; a test tool).
//
// Isolated from the installed GNLinkSecureInput: this uses a TEST service name + TEST pipe name. The
// elevated runner (see report) does, once:
//   sc create GNLinkTspTestSvc  binPath= "<thisexe> --test-service"  type= own  start= demand
//   sc start  GNLinkTspTestSvc
//   <thisexe> --probe            (asserts the REAL GatherServerIdentity returns Trusted end to end)
//   sc stop   GNLinkTspTestSvc ; sc delete GNLinkTspTestSvc
//
// --test-service: run as a service, own \\.\pipe\GNLinkTspTestPipe (SY/BA DACL, local only), idle until
//   SCM STOP. Its own process is the running service the SCM reports -> the exact binding under test.
// --probe: OpenVerifiedServicePipe(testPipe, ..., testServiceName) with the PRODUCTION gatherer. Prints
//   the verdict + diagnostic; exit 0 iff Trusted. No credential, no unlock, no real-service contact.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "trusted_service_pipe.hpp"

using namespace remote60::native_poc;

namespace {
const wchar_t kTestPipe[] = L"\\\\.\\pipe\\GNLinkTspTestPipe";
const wchar_t kTestService[] = L"GNLinkTspTestSvc";

SERVICE_STATUS_HANDLE g_status = nullptr;
std::atomic<bool> g_run{true};
HANDLE g_pipe = INVALID_HANDLE_VALUE;

void set_state(DWORD state) {
  SERVICE_STATUS s{};
  s.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  s.dwCurrentState = state;
  s.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
  if (g_status) SetServiceStatus(g_status, &s);
}

void WINAPI svc_ctrl(DWORD c) {
  if (c == SERVICE_CONTROL_STOP) {
    g_run.store(false);
    set_state(SERVICE_STOP_PENDING);
    if (g_pipe != INVALID_HANDLE_VALUE) CancelIoEx(g_pipe, nullptr);
  }
}

void WINAPI svc_main(DWORD, wchar_t**) {
  g_status = RegisterServiceCtrlHandlerW(kTestService, svc_ctrl);
  if (!g_status) return;
  set_state(SERVICE_START_PENDING);
  PSECURITY_DESCRIPTOR sd = nullptr;
  ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1,
                                                       &sd, nullptr);
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  g_pipe = CreateNamedPipeW(kTestPipe, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                                PIPE_REJECT_REMOTE_CLIENTS,
                            1, 4096, 4096, 0, &sa);
  if (sd) LocalFree(sd);
  set_state(SERVICE_RUNNING);
  while (g_run.load() && g_pipe != INVALID_HANDLE_VALUE) {
    const BOOL c = ConnectNamedPipe(g_pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    if (!g_run.load()) break;
    if (!c) break;
    char buf[64];
    DWORD got = 0;
    (void)ReadFile(g_pipe, buf, sizeof(buf), &got, nullptr);  // drain; a probe sends nothing
    DisconnectNamedPipe(g_pipe);
  }
  if (g_pipe != INVALID_HANDLE_VALUE) CloseHandle(g_pipe);
  set_state(SERVICE_STOPPED);
}

int run_probe() {
  TrustedPipeStatus st = TrustedPipeStatus::Unavailable;
  ServerIdentityFacts diag;
  HANDLE h = static_cast<HANDLE>(OpenVerifiedServicePipe(kTestPipe, GENERIC_READ | GENERIC_WRITE,
                                                         FILE_ATTRIBUTE_NORMAL, kTestService, 2000, &st,
                                                         nullptr, &diag));
  std::printf("probe: status=%s stage=%s win32=%lu serverPid=%lu svcPid=%lu imageSame=%d finalConsistent=%d\n",
              trusted_pipe_status_name(st), pipe_verify_stage_name(diag.failStage),
              static_cast<unsigned long>(diag.failWin32), static_cast<unsigned long>(diag.serverPid),
              static_cast<unsigned long>(diag.servicePid), diag.imageSameFile ? 1 : 0,
              diag.finalConsistent ? 1 : 0);
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  if (st == TrustedPipeStatus::Trusted) {
    std::printf("probe: PASS (real gatherer returned Trusted for the running test service)\n");
    return 0;
  }
  std::printf("probe: FAIL (not Trusted)\n");
  return 1;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--probe") return run_probe();
  }
  // Default: run as a service (SCM calls svc_main). If launched interactively it simply fails to
  // connect to the SCM dispatcher, which is harmless.
  SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t*>(kTestService), svc_main}, {nullptr, nullptr}};
  if (!StartServiceCtrlDispatcherW(table)) {
    std::printf("not started by the SCM (run elevated: sc create %ls binPath= \"<exe> --test-service\")\n",
                kTestService);
    return 2;
  }
  return 0;
}
