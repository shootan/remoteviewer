// prelogin-field D1 (0.2.160 field): GNLinkHost's tray icon must come back when the notification area was
// not there yet at start (the service starts the window at logon, before Explorer has made it), and when
// Explorer re-creates the notification area (TaskbarCreated).
//
// The window is the product's, in the asInvoker test build (GNLinkHostUiTest.exe; host_app_ui_test.cpp says
// what that build replaces), signed in from a fixture host.json. What is injected: the first N NIM_ADD calls
// fail (GNLINK_HOST_TEST_TRAY_ADD_FAILS, test build only). What is observed from outside, the way the shell
// knows it: Shell_NotifyIconGetRect for the window's icon (uID 1) -- S_OK only while the notification area
// holds it. The TaskbarCreated case: this test removes the icon (NIM_DELETE for that window and id, as an
// Explorer restart drops it) and posts the registered "TaskbarCreated" message to the window.
//
// What it does NOT show: requireAdministrator / UIPI (asInvoker build -- the product lets the broadcast through
// with ChangeWindowMessageFilterEx, not exercised here), a real Explorer restart, a real logon.

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
bool wait_until(const std::function<bool()>& done, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    Sleep(50);
  }
  return done();
}
HWND window_of(DWORD pid) {
  struct Find {
    DWORD pid;
    HWND hwnd;
  } f{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        auto* f = reinterpret_cast<Find*>(lp);
        DWORD p = 0;
        GetWindowThreadProcessId(h, &p);
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        if (p == f->pid && GetWindow(h, GW_OWNER) == nullptr && std::wstring(cls) == L"Remote60HostApp") {
          f->hwnd = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  return f.hwnd;
}
// The shell's own answer: does the notification area hold this window's icon 1?
bool icon_present(HWND hwnd) {
  NOTIFYICONIDENTIFIER id{};
  id.cbSize = sizeof(id);
  id.hWnd = hwnd;
  id.uID = 1;
  RECT rc{};
  return SUCCEEDED(Shell_NotifyIconGetRect(&id, &rc));
}
std::string read_all(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("GNLinkHost tray icon: retried at start, re-added on TaskbarCreated (prelogin-field D1)\n\n");
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_UI_TEST", nullptr, 0) == 0) {
    std::printf("SKIP  shows a window and a tray icon on this desktop; set REMOTE60_ALLOW_UI_TEST=1\nRESULT: SKIPPED\n");
    return 0;
  }
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  const std::filesystem::path exeDir = std::filesystem::path(self).parent_path();
  const std::filesystem::path hostExe = exeDir / L"GNLinkHostUiTest.exe";
  check("the GNLinkHost test build is beside this test", std::filesystem::exists(hostExe));
  std::filesystem::path repo = exeDir;
  while (!repo.empty() && !std::filesystem::exists(repo / L"AGENTS.md")) repo = repo.parent_path();
  const std::filesystem::path root = repo / L".claude" / L"test-tmp" / (L"tray-icon-" + std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(root / L"appdata" / L"remote60");
  std::ofstream(root / L".fixture") << "owned test profile";
  const std::string url = "http://127.0.0.1:9";
  {
    std::ofstream hj(root / L"appdata" / L"remote60" / L"host.json");
    hj << "{\n  \"directoryUrl\": \"" << url << "\",\n  \"accountId\": \"host-user\",\n  \"machineId\": \"ui-test-machine\",\n"
       << "  \"hostName\": \"UI-TEST-PC\",\n  \"hostId\": \"h-ui-1\",\n  \"hostToken\": \"tok-ui-fixture\"\n}\n";
  }
  std::wstring env;
  {
    LPWCH block = GetEnvironmentStringsW();
    for (LPWCH p = block; *p; p += wcslen(p) + 1) {
      const std::wstring kv = p;
      if (kv.rfind(L"LOCALAPPDATA=", 0) == 0 || kv.rfind(L"GNLINK_HOST_TEST_", 0) == 0) continue;
      env += kv;
      env.push_back(L'\0');
    }
    FreeEnvironmentStringsW(block);
    for (const std::wstring& kv :
         {L"GNLINK_HOST_TEST_ROOT=" + root.wstring(), L"LOCALAPPDATA=" + (root / L"appdata").wstring(),
          std::wstring(L"GNLINK_HOST_TEST_DIRECTORY=") + std::wstring(url.begin(), url.end()),
          L"GNLINK_HOST_TEST_PRELOGIN_PIPE=\\\\.\\pipe\\GNLinkTest-tray-" + std::to_wstring(GetCurrentProcessId()),
          std::wstring(L"GNLINK_HOST_TEST_PRELOGIN_SERVER_PID=0"),
          std::wstring(L"GNLINK_HOST_TEST_TRAY_ADD_FAILS=3")}) {
      env += kv;
      env.push_back(L'\0');
    }
    env.push_back(L'\0');
  }
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  std::wstring cmd = L"\"" + hostExe.wstring() + L"\" --tray";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  const bool started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                                      env.data(), root.wstring().c_str(), &si, &pi) != FALSE;
  if (started) {
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
  }
  check("GNLinkHost (test build) started with --tray, its first 3 NIM_ADDs failing", started);
  HWND hwnd = nullptr;
  wait_until([&] { return (hwnd = window_of(pi.dwProcessId)) != nullptr; }, 15000);
  check("its window exists", hwnd != nullptr);

  // 1. start: the icon is not there while the injected failures last, then the retry adds it.
  const bool absentAtFirst = hwnd && !icon_present(hwnd);
  const auto t0 = std::chrono::steady_clock::now();
  const bool cameBack = hwnd && wait_until([&] { return icon_present(hwnd); }, 20000);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  check("start: no icon while NIM_ADD fails ...", absentAtFirst);
  check("... then the retry timer adds it (the notification area holds it)", cameBack, std::to_string(ms) + " ms");

  // 2. TaskbarCreated: the icon is gone (as after an Explorer restart); the broadcast brings it back.
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = 1;
  const bool deleted = hwnd && Shell_NotifyIconW(NIM_DELETE, &nid) != FALSE;
  const bool gone = hwnd && wait_until([&] { return !icon_present(hwnd); }, 3000);
  check("the icon removed from the notification area (as an Explorer restart does)", deleted && gone);
  Sleep(1500);
  check("... and it stays away by itself (nothing re-adds it without the broadcast)", hwnd && !icon_present(hwnd));
  const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  PostMessageW(hwnd, taskbarCreated, 0, 0);
  const bool back = hwnd && wait_until([&] { return icon_present(hwnd); }, 5000);
  check("TaskbarCreated -> the window adds its icon again", back);

  // The window says what happened, in its own log.
  std::string log;
  for (const auto& e : std::filesystem::recursive_directory_iterator(root / L"appdata")) {
    if (e.is_regular_file() && e.path().filename().wstring().find(L"host_app") != std::wstring::npos) log += read_all(e.path());
  }
  check("its log has the start failure, the retry success and the TaskbarCreated re-add",
        log.find("tray icon not added (start") != std::string::npos && log.find("tray icon added (retry") != std::string::npos &&
            log.find("tray icon added (TaskbarCreated") != std::string::npos);

  if (started) {
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(job);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
