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

#include "probe_serve.hpp"
#include "trusted_service_pipe.hpp"

using namespace remote60::native_poc;

namespace {
const wchar_t kTestPipe[] = L"\\\\.\\pipe\\GNLinkTspTestPipe";
const wchar_t kTestService[] = L"GNLinkTspTestSvc";

SERVICE_STATUS_HANDLE g_status = nullptr;
HANDLE g_stop = nullptr;  // manual-reset STOP latch: set once by the handler, observed by every wait

void set_state(DWORD state, DWORD exitCode = NO_ERROR) {
  SERVICE_STATUS s{};
  s.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  s.dwCurrentState = state;
  s.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
  s.dwWin32ExitCode = exitCode;
  if (g_status) SetServiceStatus(g_status, &s);
}

void WINAPI svc_ctrl(DWORD c) {
  if (c == SERVICE_CONTROL_STOP) {
    set_state(SERVICE_STOP_PENDING);
    if (g_stop) SetEvent(g_stop);  // a latch, not a one-shot CancelIoEx: every blocking wait sees it
  }
}

void WINAPI svc_main(DWORD, wchar_t**) {
  g_status = RegisterServiceCtrlHandlerW(kTestService, svc_ctrl);
  if (!g_status) return;
  set_state(SERVICE_START_PENDING);
  g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual-reset
  if (!g_stop) { set_state(SERVICE_STOPPED, GetLastError()); return; }  // C4: a failed CreateEvent fails closed

  // Build the DACL + the pipe BEFORE reporting RUNNING: a failure here reports STOPPED with an error,
  // so the probe never trusts a "RUNNING" fixture that has no pipe.
  PSECURITY_DESCRIPTOR sd = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
                                                            SDDL_REVISION_1, &sd, nullptr)) {
    set_state(SERVICE_STOPPED, GetLastError());
    if (g_stop) CloseHandle(g_stop);
    return;
  }
  SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
  HANDLE pipe = CreateNamedPipeW(
      kTestPipe, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096,
      0, &sa);
  const DWORD pipeErr = pipe == INVALID_HANDLE_VALUE ? GetLastError() : 0;  // r6 D4: before LocalFree
  if (sd) LocalFree(sd);
  if (pipe == INVALID_HANDLE_VALUE) {
    set_state(SERVICE_STOPPED, pipeErr);  // name squatted or create failed -> fail closed
    if (g_stop) CloseHandle(g_stop);
    return;
  }
  // r6 D4: the loop's I/O event exists BEFORE RUNNING is reported (a fixture that cannot serve never says
  // it runs).
  HANDLE io = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!io) {
    const DWORD ioErr = GetLastError();
    CloseHandle(pipe);
    CloseHandle(g_stop);
    set_state(SERVICE_STOPPED, ioErr);
    return;
  }

  set_state(SERVICE_RUNNING);
  // The 4-stop-point overlapped loop is the shared ProbeServeRun (exercised non-elevated in the fixture
  // test). A wait / completion / connect failure ends it fail-closed and its error is kept: the service
  // stops with that error as its exit code (sc query shows it), not as a clean stop.
  const ProbeServeResult r = ProbeServeRun(pipe, g_stop, io);

  CloseHandle(io);
  CloseHandle(pipe);
  CloseHandle(g_stop);
  const bool stopped = r.exit == ProbeServeExit::StopBeforeConnect || r.exit == ProbeServeExit::StopDuringConnect ||
                       r.exit == ProbeServeExit::StopBetween || r.exit == ProbeServeExit::StopDuringRead;
  set_state(SERVICE_STOPPED, stopped ? NO_ERROR : (r.win32 != 0 ? r.win32 : ERROR_GEN_FAILURE));
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
