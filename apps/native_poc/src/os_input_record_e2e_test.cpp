// The record-only OS-effect boundary, end to end (2026-10-28, Codex codex_test_injection_finding.md Finding-2).
//
// GNLinkStreamInputRecordTest is GNLinkStream with REMOTE60_OS_INPUT_RECORD_ONLY (os_input_effects.hpp): its receipt,
// policy, routing and dispatch are the product's; every SendInput / input PostMessage / SetCursorPos / secure-broker
// hand-off is recorded on its stdout and never performed. This shows, with a real phone session controller:
//   * the host said "record-only" before anything connected (else the test stops: INVALID, nothing connected);
//   * the phone's Tab down/up, a pointer move, a left click and a short text each reach the dispatch
//     ("[input] injected seq=..") AND its record ("[os-input-record] api=..") -- the exact events, the text by its
//     length and hash only;
//   * the display wake at the stream start is recorded, not performed;
//   * this PC saw none of it: the cursor position and the foreground window are the same before and after.
// What it does not show: that Windows would have accepted any of it. That needs a real OS boundary (an approved
// isolated machine), not this PC.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "e2e_isolation.hpp"
#include "e2e_injection_guard.hpp"
#include "native_video_client_session.hpp"

namespace {

using remote60::native_poc::ClientSessionConnectArgs;
using remote60::native_poc::ClientSessionController;
using remote60::native_poc::ClientSessionState;
namespace e2e = remote60::native_poc::e2e;

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

int count(const std::string& text, const std::string& needle) {
  int n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++n;
  return n;
}

// the same hash os_input_effects.hpp records for a Unicode run
uint32_t fnv1a(const std::u16string& s) {
  uint32_t h = 2166136261u;
  for (char16_t u : s) {
    h = (h ^ (u & 0xFF)) * 16777619u;
    h = (h ^ (u >> 8)) * 16777619u;
  }
  return h;
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
    std::printf("SKIP  os_input_record_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return e2e::kE2eSkippedExit;
  }
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  const uint16_t mediaPort = e2e::e2e_pick_free_udp_port();
  if (mediaPort == 0) {
    std::printf("FAIL  no free UDP port\n");
    return 1;
  }
  e2e::StagingDir staging;
  if (!staging.Create(L"os_input_record")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  // the RECORD-ONLY test host, staged under the product's name (the product GNLinkStream.exe is never started here)
  const bool staged =
      CopyFileW((myDir + L"GNLinkStreamInputRecordTest.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("the record-only test host (GNLinkStreamInputRecordTest) and a never-answering helper are staged", staged);

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
  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged && hostLog != INVALID_HANDLE_VALUE) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", L"0");  // the user's clipboard stays out
    // --input-log-every 1: every dispatched event logged with its seq (the dispatch side of the evidence)
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                       L" --bind-port " + std::to_wstring(mediaPort) + L" --seconds 60 --input-log-every 1";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hostLog;
    si.hStdError = hostLog;
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e::e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e::e2e_command_is_isolated(cmd, dir, &isoWhy) &&
                       e2e::e2e_path_is_under(e2e::e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = isoOk && CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                                       dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  check("the host started", launched);
  auto log = [&] { return e2e::e2e_read_text_file(hostLogPath); };

  // fail closed: nothing connects before THIS launch said record-only
  std::string why;
  const bool recordOnly = launched && e2e::e2e_wait_os_record_only(log, 20000, &why);
  if (launched && !recordOnly) e2e::e2e_injection_setup_failed("os_input_record", why);
  check("the host said 'os input effects=record-only' before any client", recordOnly, why);

  POINT cursor0{};
  GetCursorPos(&cursor0);
  const HWND fg0 = GetForegroundWindow();

  NullSink sink;
  ClientSessionController phone;
  if (recordOnly) {
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
    phone.RequestStreamActive(true);
    const bool wakeRecorded = wait_until(
        [&] { return log().find("[os-input-record] api=SendInput n=2 mouse flags=0x1 dx=1") != std::string::npos; }, 15000);
    check("the display wake at the stream start: recorded (SendInput n=2, +1/-1), not performed", wakeRecorded);
    check("...and its monitor-power broadcast recorded (PostMessage HWND_BROADCAST WM_SYSCOMMAND)",
          log().find("[os-input-record] api=PostMessage") != std::string::npos && log().find("msg=0x112") != std::string::npos);

    const int disp0 = count(log(), "[native-video-host][input] injected seq=");
    // Tab down/up, a pointer move, a left click, a two-letter text
    const bool queued = phone.QueueInputEvent(5, 0, 0, 0, VK_TAB, 0) && phone.QueueInputEvent(6, 0, 0, 0, VK_TAB, 0) &&
                        phone.QueueInputEvent(1, 320, 240, 0, 0, 0) && phone.QueueInputEvent(2, 320, 240, 0, 1, 1) &&
                        phone.QueueInputEvent(3, 320, 240, 0, 1, 0);
    const std::u16string text = u"Ab";
    const bool textQueued = phone.QueueInputText(reinterpret_cast<const uint16_t*>(text.data()), text.size());
    check("the phone queued Tab down/up, a move, a click and a text", queued && textQueued);
    const bool dispatched = wait_until([&] { return count(log(), "[native-video-host][input] injected seq=") >= disp0 + 5; }, 8000);
    const std::string out = log();
    check("all five events reached the product's dispatch ([input] injected seq=..)", dispatched,
          std::to_string(count(out, "[native-video-host][input] injected seq=") - disp0));
    check("...Tab down and up recorded (SendInput vk=9), not sent",
          count(out, "key vk=9 down") >= 1 && count(out, "key vk=9 up") >= 1);
    check("...the pointer recorded (SetCursorPos), the cursor not moved", count(out, "api=SetCursorPos") >= 2);
    check("...the click recorded (SendInput mouse LEFTDOWN 0x2 / LEFTUP 0x4), not sent",
          out.find("mouse flags=0x2 ") != std::string::npos && out.find("mouse flags=0x4 ") != std::string::npos);
    char want[64];
    std::snprintf(want, sizeof(want), "unicode units=2 fnv=%08x", fnv1a(text));
    const bool textRecorded = wait_until([&] { return log().find(want) != std::string::npos; }, 5000);
    check("...the text recorded by its length and hash only (the characters themselves never printed)", textRecorded, want);
    phone.Disconnect();
  }
  POINT cursor1{};
  GetCursorPos(&cursor1);
  // (a person moving the mouse meanwhile would fail this check -- it can only fail toward "something moved")
  check("THIS PC saw none of it: the cursor is where it was", cursor0.x == cursor1.x && cursor0.y == cursor1.y,
        std::to_string(cursor0.x) + "," + std::to_string(cursor0.y) + " -> " + std::to_string(cursor1.x) + "," +
            std::to_string(cursor1.y));
  check("...and the foreground window is the same", GetForegroundWindow() == fg0);

  if (launched) {
    TerminateJobObject(job, 0);
    WaitForSingleObject(hostPi.hProcess, 10000);
    CloseHandle(hostPi.hProcess);
    CloseHandle(hostPi.hThread);
  }
  CloseHandle(job);
  staging.Remove();
  WSACleanup();
  std::printf("\n%d checks, %d failures\n%s\n", gChecks, gFailures, gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAIL");
  return gFailures == 0 ? 0 : 1;
}
