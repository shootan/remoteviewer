// The viewer must not vanish when it cannot connect.
//
// Before this, pointing the viewer at a port nobody is listening on produced a black window that
// closed itself within about a tenth of a second. The reason was known -- it went to stderr --
// and the user saw nothing. This runs the real GNLinkViewer.exe against a dead port and checks
// that it is still there afterwards.
//
// Process-level on purpose: the thing being guarded is the process's lifetime, and a unit test of
// the drawing code cannot see that. The wording and the buttons are evidenced by the screenshot
// in .claude/ui-preview; what is asserted here is the behaviour that made the screenshot
// possible at all.

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <string>

namespace {

int gPass = 0;
int gFail = 0;

void ok(bool cond, const std::string& what, const std::string& detail = {}) {
  if (cond) {
    ++gPass;
    std::printf("PASS  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  } else {
    ++gFail;
    std::printf("FAIL  %s%s%s\n", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  }
}

std::wstring executable_dir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring full(path);
  const size_t slash = full.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

/** A port nobody should be on. Bound briefly to prove it, then released. */
bool port_is_free(unsigned short port) {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  bool free_ = false;
  if (s != INVALID_SOCKET) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    free_ = bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    closesocket(s);
  }
  WSACleanup();
  return free_;
}

struct Found {
  HWND hwnd = nullptr;
  DWORD pid = 0;
};

BOOL CALLBACK find_by_pid(HWND hwnd, LPARAM param) {
  auto* found = reinterpret_cast<Found*>(param);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == found->pid && IsWindowVisible(hwnd)) {
    found->hwnd = hwnd;
    return FALSE;
  }
  return TRUE;
}

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const std::wstring exe = executable_dir() + L"\\GNLinkViewer.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::printf("SKIP  GNLinkViewer.exe is not beside this test\n");
    std::printf("viewer_startup_failure_test: PASS (skipped)\n");
    return 0;
  }

  const unsigned short deadPort = 43999;
  ok(port_is_free(deadPort), "the port used for the test really is free",
     std::to_string(deadPort));

  // Off-screen so nothing appears in front of whoever is at the machine, and pointed at loopback
  // so no packet leaves this host.
  std::wstring cmd = L"\"" + exe + L"\" --host 127.0.0.1 --port " + std::to_wstring(deadPort);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWMINNOACTIVE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      nullptr, &si, &pi)) {
    ok(false, "the viewer could be started");
    std::printf("viewer_startup_failure_test: FAIL (%d passed, %d failed)\n", gPass, gFail);
    return 1;
  }

  // Two and a half seconds. The old behaviour left within about 120ms, and the longest wait seen
  // for a refused connect on this machine was well under a second.
  const DWORD waited = WaitForSingleObject(pi.hProcess, 2500);
  ok(waited == WAIT_TIMEOUT,
     "a viewer that cannot connect is still running, not gone",
     waited == WAIT_TIMEOUT ? std::string("alive after 2500ms")
                            : std::string("exited early"));

  if (waited == WAIT_TIMEOUT) {
    Found found;
    found.pid = pi.dwProcessId;
    EnumWindows(find_by_pid, reinterpret_cast<LPARAM>(&found));
    ok(found.hwnd != nullptr, "and it still owns a window to say why in");

    // Closing has to work, or the improvement is a window the user cannot get rid of.
    if (found.hwnd) PostMessageW(found.hwnd, WM_CLOSE, 0, 0);
    const DWORD closed = WaitForSingleObject(pi.hProcess, 5000);
    ok(closed == WAIT_OBJECT_0, "and closing the window ends it",
       closed == WAIT_OBJECT_0 ? std::string() : std::string("still running after WM_CLOSE"));
  }

  DWORD code = 0;
  if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) {
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 2000);
  }
  GetExitCodeProcess(pi.hProcess, &code);
  std::printf("exit  %lu\n", code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  std::printf("viewer_startup_failure_test: %s (%d passed, %d failed)\n",
              gFail == 0 ? "PASS" : "FAIL", gPass, gFail);
  return gFail == 0 ? 0 : 1;
}
