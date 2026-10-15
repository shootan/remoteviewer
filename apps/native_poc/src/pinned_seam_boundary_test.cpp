// keepalive r3 R3: the capture boundary of GNLinkStreamPinnedSelectTest, checked without a picture.
//
// Every host here runs on a private window station (no window of the user's is on its desktop) with
// REMOTE60_PINNED_TEST_CAPTURE_PROBE=1: at its first capture call it reports what it would capture and ends
// (66 admitted, 65 refused) before any capture object exists. So no case captures a screen, even one that
// would go wrong. The fixture windows are children of this test on the same private desktop.
//
// Cases: the admission (no args, unopenable pid, wrong title, no window), the startup refusal (pinned window
// admitted, capture criteria not met), the positive control, the lifetime watcher (window destroyed,
// retitled, recreated under the same title, process gone) -- and the negative control:
// REMOTE60_PINNED_TEST_PROBE_UNGATED=1 restores the 2abb8be decisions, and the same wrong-title / criteria
// cases must then reach a SCREEN capture (fallback=monitor) -- the defect, shown by the probe instead of a
// picture. A negative control that does not fail means the probe does not see the old path.
#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "native_video_client_session.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

void say(const std::string& s) { std::printf("%s\n", s.c_str()); }
std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

// ------------------------------------------------------------------------------ the fixture child
LRESULT CALLBACK fixture_proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
HWND fixture_window(const std::wstring& title) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = fixture_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60PinnedSeamFixture";
  RegisterClassExW(&wc);
  HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, title.c_str(), WS_POPUP | WS_VISIBLE, 0, 0,
                           320, 180, nullptr, nullptr, wc.hInstance, nullptr);
  if (h) ShowWindow(h, SW_SHOWNOACTIVATE);
  return h;
}
void pump_for(int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    Sleep(10);
  }
}
// --fixture <window|nowindow|destroy|retitle|recreate|exit> <title> : up 1.5 s, then the change, then 20 s.
int fixture_main(const std::wstring& mode, const std::wstring& title) {
  HWND h = mode == L"nowindow" ? nullptr : fixture_window(title);
  if (mode != L"nowindow" && !h) return 3;
  pump_for(1500);
  if (mode == L"destroy") DestroyWindow(h);
  if (mode == L"retitle") SetWindowTextW(h, L"another title");
  if (mode == L"recreate") {
    DestroyWindow(h);
    h = fixture_window(title);  // the same title, a new HWND
  }
  if (mode == L"exit") return 0;
  pump_for(20000);
  return 0;
}

// ------------------------------------------------------------------------------ one case
class NullSink : public ClientEncodedFrameSink {
 public:
  void OnEncodedH264Frame(UdpH264AssembledFrame&&) override {}
  void OnVideoStreamReset() override {}
};

struct Station {
  HWINSTA ws = nullptr;
  HDESK desk = nullptr;
  std::wstring desktop;
  bool Create() {
    ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!ws) return false;
    wchar_t name[256] = L"";
    DWORD len = 0;
    GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &len);
    HWINSTA orig = GetProcessWindowStation();
    SetProcessWindowStation(ws);
    desk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    SetProcessWindowStation(orig);
    desktop = std::wstring(name) + L"\\Default";
    return desk != nullptr;
  }
};

struct Case {
  std::string name;
  std::wstring fixtureMode;  // empty: no fixture
  enum class Pid { Fixture, None, Dead } pid = Pid::Fixture;
  bool rightTitle = true;
  std::wstring extraArgs;
  bool ungated = false;
  bool connect = true;    // a client connects (the host starts its capture only then)
  DWORD wantExit = 0;
  std::string wantLine;   // must be in the host's output
  bool wantScreen = false;  // the probe reported a screen (only the negative control)
};

struct Outcome {
  DWORD exit = 0;
  bool exited = false;
  std::string log;
};

Outcome run_case(const Case& c, const Station& st, const std::wstring& hostExe, const std::wstring& dir,
                 const std::wstring& outDir) {
  Outcome out;
  const std::wstring self = [] {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return std::wstring(p);
  }();
  const std::wstring title = e2e_unique_window_title(L"remote60 pinned seam fixture");
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  std::wstring desktop = st.desktop;
  PROCESS_INFORMATION fpi{};
  if (!c.fixtureMode.empty()) {
    std::wstring cmd = L"\"" + self + L"\" --fixture " + c.fixtureMode + L" \"" + title + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.lpDesktop = desktop.data();
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &fpi)) {
      AssignProcessToJobObject(job, fpi.hProcess);
    }
    Sleep(300);
  }
  DWORD pid = 0;
  if (c.pid == Case::Pid::Fixture) pid = fpi.dwProcessId;
  if (c.pid == Case::Pid::Dead) {
    // a process that has exited -- its pid opens nothing
    std::wstring cmd = L"cmd.exe /c exit 0";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION dpi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &dpi)) {
      WaitForSingleObject(dpi.hProcess, 5000);
      pid = dpi.dwProcessId;
      CloseHandle(dpi.hThread);
      CloseHandle(dpi.hProcess);  // the last handle: the pid is gone
    }
  }

  const uint16_t port = e2e_pick_free_udp_port();
  std::wstring cmd = L"\"" + hostExe + L"\" --transport udp --codec h264 --bind-address 127.0.0.1 --bind-port " +
                     std::to_wstring(port) + L" --fps 30 --bitrate 2000000 --seconds 30 --input-injection-mode none";
  if (c.pid != Case::Pid::None) {
    cmd += L" --capture-window-pid " + std::to_wstring(pid) + L" --capture-window-title \"" +
           (c.rightTitle ? title : std::wstring(L"remote60 no such window")) + L"\"";
  }
  cmd += c.extraArgs;
  SetEnvironmentVariableW(L"REMOTE60_PINNED_TEST_CAPTURE_PROBE", L"1");
  SetEnvironmentVariableW(L"REMOTE60_PINNED_TEST_PROBE_UNGATED", c.ungated ? L"1" : nullptr);
  SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", L"0");
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
  const std::wstring isoAppData = dir + L"localappdata";
  CreateDirectoryW(isoAppData.c_str(), nullptr);
  std::vector<wchar_t> env = e2e_isolated_environment(isoAppData);
  const std::wstring logPath = dir + std::wstring(c.name.begin(), c.name.end()) + L"_host.log";
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = log;
  si.hStdError = log;
  si.lpDesktop = desktop.data();  // the host on the private station too
  PROCESS_INFORMATION hpi{};
  const bool launched = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(),
                                       dir.c_str(), &si, &hpi) != 0;
  if (launched) {
    AssignProcessToJobObject(job, hpi.hProcess);
    ResumeThread(hpi.hThread);
    NullSink sink;
    ClientSessionController session;
    if (c.connect) {
      Sleep(500);
      ClientSessionConnectArgs a;
      a.host = "127.0.0.1";
      a.videoPort = port;
      a.controlPort = port;
      a.requireUdpHello = true;
      a.requireTcpControl = false;
      a.controlOverUdp = true;
      a.encodedFrameSink = &sink;
      (void)session.Connect(a);
    }
    out.exited = WaitForSingleObject(hpi.hProcess, 15000) == WAIT_OBJECT_0;
    if (!out.exited) TerminateProcess(hpi.hProcess, 0), WaitForSingleObject(hpi.hProcess, 5000);
    GetExitCodeProcess(hpi.hProcess, &out.exit);
    session.Disconnect();
    CloseHandle(hpi.hThread);
    CloseHandle(hpi.hProcess);
  }
  CloseHandle(log);
  if (fpi.hProcess) CloseHandle(fpi.hProcess), CloseHandle(fpi.hThread);
  CloseHandle(job);  // ends the fixture
  if (!outDir.empty()) CopyFileW(logPath.c_str(), (outDir + std::wstring(c.name.begin(), c.name.end()) + L"_host.log").c_str(), FALSE);
  std::ifstream f(logPath, std::ios::binary);
  out.log.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return out;
}

std::string line_with(const std::string& log, const std::string& needle) {
  const size_t at = log.find(needle);
  if (at == std::string::npos) return {};
  const size_t from = log.rfind('\n', at);
  const size_t to = log.find('\n', at);
  return log.substr(from == std::string::npos ? 0 : from + 1, (to == std::string::npos ? log.size() : to) -
                                                                   (from == std::string::npos ? 0 : from + 1));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc >= 4 && std::wstring(argv[1]) == L"--fixture") return fixture_main(argv[2], argv[3]);
  std::wstring outDir;  // --out DIR: the hosts' logs kept there
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::wstring(argv[i]) == L"--out") outDir = std::wstring(argv[i + 1]) + L"\\";
  }
  if (!outDir.empty()) CreateDirectoryW(outDir.c_str(), nullptr);
  if (!host_e2e_allowed()) {
    say("SKIPPED  launches the pinned test host. Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED");
    return kE2eSkippedExit;
  }
  wchar_t me[MAX_PATH];
  GetModuleFileNameW(nullptr, me, MAX_PATH);
  std::wstring myDir(me);
  myDir.resize(myDir.find_last_of(L'\\') + 1);
  StagingDir staging;
  if (!staging.Create(L"pinned_seam")) {
    say("FAIL  " + staging.why());
    return 1;
  }
  const std::wstring dir = staging.path();
  const std::wstring hostExe = dir + L"GNLinkStream.exe";
  check("the pinned test host is staged", CopyFileW((myDir + L"GNLinkStreamPinnedSelectTest.exe").c_str(),
                                                     hostExe.c_str(), FALSE) != 0);
  Station st;
  check("a private window station for the host and its fixture", st.Create(), narrow(st.desktop));
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  const std::string kRefusedStart = "[pinned-select-test] refused to start";
  const std::string kProbe = "[pinned-select-test] capture-probe";
  const std::string kGone = "refused: the pinned window is gone";
  std::vector<Case> cases;
  cases.push_back({"no_args", L"", Case::Pid::None, true, L"", false, false, 64, kRefusedStart});
  cases.push_back({"dead_pid", L"", Case::Pid::Dead, true, L"", false, false, 64, "cannot be opened"});
  cases.push_back({"wrong_title", L"window", Case::Pid::Fixture, false, L"", false, true, 64,
                   "no window of the pinned process"});
  cases.push_back({"no_window", L"nowindow", Case::Pid::Fixture, true, L"", false, true, 64,
                   "no window of the pinned process"});
  cases.push_back({"criteria_unmet", L"window", Case::Pid::Fixture, true,
                   L" --capture-window-process remote60-no-such.exe", false, true, 65,
                   "refused: startup: the pinned window was not found"});
  cases.push_back({"positive", L"window", Case::Pid::Fixture, true, L"", false, true, 66,
                   "target=window"});
  cases.push_back({"gone_destroy", L"destroy", Case::Pid::Fixture, true, L"", false, false, 65, kGone});
  cases.push_back({"gone_retitle", L"retitle", Case::Pid::Fixture, true, L"", false, false, 65, kGone});
  cases.push_back({"gone_recreate", L"recreate", Case::Pid::Fixture, true, L"", false, false, 65, kGone});
  cases.push_back({"gone_exit", L"exit", Case::Pid::Fixture, true, L"", false, false, 65, kGone});
  // negative controls: the 2abb8be decisions -- these MUST reach a screen
  Case nc1{"ungated_wrong_title", L"window", Case::Pid::Fixture, false, L"", true, true, 66, "target=screen"};
  nc1.wantScreen = true;
  Case nc2{"ungated_criteria_unmet", L"window", Case::Pid::Fixture, true,
           L" --capture-window-process remote60-no-such.exe", true, true, 66, "target=screen"};
  nc2.wantScreen = true;
  cases.push_back(nc1);
  cases.push_back(nc2);

  for (const Case& c : cases) {
    const Outcome o = run_case(c, st, hostExe, dir, outDir);
    const std::string probe = line_with(o.log, kProbe);
    const bool fellBack = o.log.find("fallback=monitor") != std::string::npos;
    const bool screen = probe.find("target=screen") != std::string::npos;
    const bool anyCapture = o.log.find("capture-started=1") != std::string::npos ||
                            o.log.find("capture item source=") != std::string::npos;
    say("CASE " + c.name + " exit=" + std::to_string(o.exit) + " exited=" + std::to_string(o.exited) +
        " fallback=" + std::to_string(fellBack) + " probe='" + probe + "'");
    check("[" + c.name + "] the host ends with " + std::to_string(c.wantExit), o.exited && o.exit == c.wantExit,
          std::to_string(o.exit));
    check("[" + c.name + "] it says why", o.log.find(c.wantLine) != std::string::npos, c.wantLine);
    check("[" + c.name + "] no capture object was ever made", !anyCapture);
    if (c.wantScreen) {
      check("[" + c.name + "] NEGATIVE CONTROL: the old decisions go to a SCREEN", screen && fellBack);
    } else {
      check("[" + c.name + "] no screen: no fallback=monitor, no screen probe", !fellBack && !screen);
    }
    if (c.name == "positive") {
      check("[positive] the probe saw the pinned window, admitted", probe.find("pinned=1 admitted=1") != std::string::npos);
    }
  }
  WSACleanup();
  if (!staging.Remove()) say("WARN  " + staging.why());
  say(std::string("\npinned_seam_boundary_test: ") + (gFailures == 0 ? "PASS" : "FAIL") +
      "\nRESULT: " + (gFailures == 0 ? "PASS" : "FAIL"));
  return gFailures == 0 ? 0 : 1;
}
