// "Windows 로그인", end to end (prelogin-ui r3, item 5).
//
// The chain, with nothing in it written by the test except the user's own actions:
//
//   the session toolbar's "Windows 로그인" item (rendered by the product toolbar, clicked with a mouse
//   press/release on it) or Ctrl+Alt+U in the product's window procedure
//   -> request_prelogin_login -> the product's password window (typed into, character by character,
//   then its 입력 button clicked) -> the control thread (ControlClient) -> sealed under the PRELOGIN
//   purpose -> 40/41/42/43/44/45 over a directory-authenticated session -> a real host started here in
//   --prelogin mode -> PreloginCredReceiver opens it -> waits for the input target -> SendInput Unicode
//   -> the focused EDIT of a window THIS PROCESS owns, byte for byte -> the result window on the viewer.
//
// Negative controls in the same run:
//   * the input target is NOT the test window (another window of this process has the focus): the
//     host answers RejectedPolicy and NEITHER window received a character;
//   * the run with --not-prelogin starts the same host without --prelogin: no 0x2000 in the pong, no
//     toolbar item, Ctrl+Alt+U opens the existing lock-screen unlock prompt (not this one), and a forced
//     request is answered locally without one prelogin-cred message reaching the host.
// (An unlock-purpose blob refused by the receiver is in prelogin_cred_host_test.)
//
// What differs from the product, said once:
//   * The host is GNLinkStreamMigrationTest (the product's sources; REMOTE60_STREAM_TEST_SEAM swaps the
//     server for a loopback fake directory, and lets the receiver's "is the input target the logon UI"
//     question name a window title, GNLINK_STREAM_TEST_CRED_TARGET_TITLE, instead of LogonUI.exe). It
//     runs as this user on this desktop (Default), not as SYSTEM on Winlogon, with SeShutdownPrivilege
//     deleted from its token. The real LogonUI and the real password provider are the G check.
//   * The viewer is the product's window procedure, toolbar, prompt and ControlClient in this process;
//     the toolbar state is pushed when the pong arrives (the product pushes it on its own timer).
//   * The typing is real SendInput on this desktop: the window that receives it is this test's own,
//     brought to the foreground only for the moment of the test. REMOTE60_ALLOW_UI_TEST=1 is required
//     on top of REMOTE60_ALLOW_HOST_E2E=1. The secret is a test string, never a real password.
//   * LOCALAPPDATA of this process points into the scratch directory, so the viewer's credential store
//     (the existing unlock path's) is never the user's.

#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"
#include "directory_fake_server.hpp"

#include <fstream>
#include <memory>
#include <sstream>

#include "client_session_toolbar.hpp"
#include "control_resume.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_control_client.hpp"
#include "viewer_control_resume.hpp"
#include "viewer_picker.hpp"
#include "viewer_state.hpp"
#include "viewer_unlock.hpp"
#include "viewer_window_proc.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;
using remote60::native_poc::test_support::FakeDirectory;
using remote60::native_poc::test_support::Reply;
using remote60::native_poc::viewer::ViewerState;
using remote60::native_poc::viewer::kPreloginPromptClass;
using remote60::native_poc::viewer::kPreloginPromptTitle;
using remote60::native_poc::viewer::kPreloginResultTitle;
using remote60::native_poc::viewer::kPreloginResultClass;
using remote60::native_poc::viewer::kPreloginResultTextId;

// The one stand-in, and it is not in the chain: the macro window is a WebView2 window.
namespace remote60::native_poc {
bool macro_window_visible() { return false; }
void macro_window_toggle(HINSTANCE, HWND, const MacroWindowHooks&) {}
}  // namespace remote60::native_poc

namespace {

// A test string, not anybody's password: ASCII, a quote, a space, Latin-1 and a Hangul syllable.
const std::wstring kSecret = L"Pw-7Qz \"!ä가x";

std::string utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string hexw(const std::wstring& w) {
  std::string out;
  char b[8];
  for (wchar_t c : w) {
    std::snprintf(b, sizeof(b), "%04X ", static_cast<unsigned>(c));
    out += b;
  }
  return out;
}

HANDLE token_without_shutdown_privilege() {
  HANDLE self = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &self)) return nullptr;
  LUID_AND_ATTRIBUTES del{};
  LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &del.Luid);
  HANDLE restricted = nullptr;
  const BOOL ok = CreateRestrictedToken(self, 0, 0, nullptr, 1, &del, 0, nullptr, &restricted);
  CloseHandle(self);
  return ok ? restricted : nullptr;
}

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

// ------------------------------------------------------------------ the windows the typing may reach
// Two windows on their own thread: the one the host is told to type into (by title), and a decoy that
// holds the focus in the negative control. Both have a focused EDIT that records what it receives.
struct Targets {
  std::wstring title, decoyTitle;
  HWND main = nullptr, edit = nullptr, decoy = nullptr, decoyEdit = nullptr;
  std::thread t;
  std::atomic<bool> ready{false};
  DWORD tid = 0;

  static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SETFOCUS || (m == WM_ACTIVATE && LOWORD(w) != WA_INACTIVE)) {
      HWND child = GetWindow(h, GW_CHILD);
      if (child) SetFocus(child);
      return 0;
    }
    if (m == WM_CLOSE) return 0;  // only Stop() ends them
    return DefWindowProcW(h, m, w, l);
  }
  bool Start() {
    title = e2e_unique_window_title(L"remote60 prelogin login target");
    decoyTitle = e2e_unique_window_title(L"remote60 prelogin login decoy");
    t = std::thread([this] {
      tid = GetCurrentThreadId();
      WNDCLASSEXW wc{sizeof(wc)};
      wc.lpfnWndProc = Proc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = L"Remote60PreloginLoginTarget";
      wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
      RegisterClassExW(&wc);
      main = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 420, 110,
                             nullptr, nullptr, wc.hInstance, nullptr);
      edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 10, 380, 26, main,
                             nullptr, wc.hInstance, nullptr);
      decoy = CreateWindowExW(0, wc.lpszClassName, decoyTitle.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 170, 420,
                              110, nullptr, nullptr, wc.hInstance, nullptr);
      decoyEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 10, 380, 26,
                                  decoy, nullptr, wc.hInstance, nullptr);
      ready = true;
      MSG m;
      while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
      }
      DestroyWindow(decoy);
      DestroyWindow(main);
    });
    return wait_until([this] { return ready.load(); }, 5000) && main && edit && decoy && decoyEdit;
  }
  static std::wstring text(HWND e) {
    wchar_t b[256] = {};
    GetWindowTextW(e, b, 255);
    return b;
  }
  void Clear() {
    SetWindowTextW(edit, L"");
    SetWindowTextW(decoyEdit, L"");
  }
  void Stop() {
    if (tid) PostThreadMessageW(tid, WM_QUIT, 0, 0);
    if (t.joinable()) t.join();
  }
};

// Brings `w` to the front with its EDIT focused (this process is the foreground one at that moment:
// its own prompt just closed). True when it is the foreground window.
bool bring_front(HWND w) {
  for (int i = 0; i < 20; ++i) {
    HWND fg = GetForegroundWindow();
    const DWORD ft = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD wt = GetWindowThreadProcessId(w, nullptr);
    const DWORD me = GetCurrentThreadId();
    if (ft && ft != me) AttachThreadInput(me, ft, TRUE);
    if (wt != me) AttachThreadInput(me, wt, TRUE);
    ShowWindow(w, SW_SHOWNORMAL);
    BringWindowToTop(w);
    SetForegroundWindow(w);
    SetFocus(GetWindow(w, GW_CHILD));
    if (wt != me) AttachThreadInput(me, wt, FALSE);
    if (ft && ft != me) AttachThreadInput(me, ft, FALSE);
    if (GetForegroundWindow() == w) return true;
    Sleep(50);
  }
  return GetForegroundWindow() == w;
}

// ------------------------------------------------------------------------------ evidence pictures
std::wstring gCaptureDir;
bool save_capture(HWND w, const std::wstring& name) {
  if (gCaptureDir.empty() || !w) return false;
  RECT r{};
  GetWindowRect(w, &r);
  const int cw = r.right - r.left, ch = r.bottom - r.top;
  if (cw <= 0 || ch <= 0) return false;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = cw;
  bi.bmiHeader.biHeight = -ch;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, bmp);
  const BOOL printed = PrintWindow(w, mem, PW_RENDERFULLCONTENT);
  bool ok = false;
  if (printed && bits) {
    const DWORD imageBytes = static_cast<DWORD>(cw) * static_cast<DWORD>(ch) * 4;
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + imageBytes;
    BITMAPINFOHEADER ih = bi.bmiHeader;
    ih.biHeight = -ch;
    std::ofstream out(gCaptureDir + name + L".bmp", std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    out.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
    out.write(static_cast<const char*>(bits), imageBytes);
    ok = out.good();
  }
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  return ok;
}

// ----------------------------------------------------------------------------------- the host
struct Host {
  PROCESS_INFORMATION pi{};
  HANDLE job = nullptr;
  HANDLE readEnd = nullptr;
  std::thread reader;
  std::mutex mu;
  std::string output;
  std::wstring dir;
  std::string log() {
    std::lock_guard<std::mutex> l(mu);
    return output;
  }
  int Count(const std::string& marker) {
    const std::string all = log();
    int n = 0;
    for (size_t at = all.find(marker); at != std::string::npos; at = all.find(marker, at + marker.size())) ++n;
    return n;
  }
  void Stop() {
    if (job) {
      CloseHandle(job);
      job = nullptr;
    }
    if (pi.hProcess) {
      WaitForSingleObject(pi.hProcess, 20000);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      pi = PROCESS_INFORMATION{};
    }
    if (reader.joinable()) reader.join();
    if (readEnd) {
      CloseHandle(readEnd);
      readEnd = nullptr;
    }
  }
};

bool start_host(Host* host, const std::wstring& dir, const std::string& url, uint16_t port, bool prelogin,
                const std::wstring& targetTitle, HANDLE restricted) {
  host->dir = dir;
  const std::wstring myDir = directory_of(self_path());
  if (!CopyFileW((myDir + L"GNLinkStreamMigrationTest.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) ||
      !CopyFileW(self_path().c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE)) {
    return false;
  }
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE writeEnd = nullptr;
  if (!CreatePipe(&host->readEnd, &writeEnd, &sa, 1 << 20)) return false;
  SetHandleInformation(host->readEnd, HANDLE_FLAG_INHERIT, 0);
  host->job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(host->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
  SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATIC_SCENE_FPS", L"15");
  const std::wstring wurl(url.begin(), url.end());
  std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(port) + L" --directory-url " + wurl +
                     L" --directory-id tester --directory-pw e2e-pass-1234 --seconds 150" + L" --directory-cache \"" + dir +
                     L"host_cache.json\"" + (prelogin ? L" --prelogin" : L"");
  std::string why;
  const bool isolated = e2e_command_is_isolated(cmd, dir, &why);
  check("the host command is isolated from the user's files", isolated, why);
  if (!isolated) return false;
  const std::wstring appData = dir + L"host_localappdata";
  CreateDirectoryW(appData.c_str(), nullptr);
  std::vector<wchar_t> env = e2e_isolated_environment(appData);
  env.pop_back();
  for (const std::wstring& kv :
       {L"GNLINK_STREAM_TEST_DIRECTORY=" + wurl, L"GNLINK_STREAM_TEST_CRED_TARGET_TITLE=" + targetTitle,
        // a test pipe nobody serves: the host never reaches this PC's installed service
        L"GNLINK_STREAM_TEST_ADMIN_PIPE=\\\\.\\pipe\\GNLinkTest-prelogin-login-" + std::to_wstring(GetCurrentProcessId())}) {
    env.insert(env.end(), kv.begin(), kv.end());
    env.push_back(L'\0');
  }
  env.push_back(L'\0');
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writeEnd;
  si.hStdError = writeEnd;
  const bool launched = CreateProcessAsUserW(restricted, nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                                             CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(),
                                             dir.c_str(), &si, &host->pi) != FALSE;
  CloseHandle(writeEnd);
  if (!launched) return false;
  AssignProcessToJobObject(host->job, host->pi.hProcess);
  // Nothing runs until the token is known to be unable to restart this PC.
  const int priv = process_has_shutdown_privilege(host->pi.hProcess);
  check("SAFETY: the host's token holds no SeShutdownPrivilege at all", priv == 0, std::to_string(priv));
  if (priv != 0) return false;
  ResumeThread(host->pi.hThread);
  host->reader = std::thread([host] {
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(host->readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) {
      std::lock_guard<std::mutex> l(host->mu);
      host->output.append(buf, got);
    }
  });
  return true;
}

// Everything the viewer prints, kept, so "the password is in no log" can be checked.
struct Tee : std::streambuf {
  std::streambuf* real;
  std::mutex mu;
  std::string kept;
  explicit Tee(std::streambuf* r) : real(r) {}
  int overflow(int c) override {
    if (c == EOF) return 0;
    {
      std::lock_guard<std::mutex> l(mu);
      kept.push_back(static_cast<char>(c));
    }
    return real->sputc(static_cast<char>(c));
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    {
      std::lock_guard<std::mutex> l(mu);
      kept.append(s, static_cast<size_t>(n));
    }
    return real->sputn(s, n);
  }
  std::string all() {
    std::lock_guard<std::mutex> l(mu);
    return kept;
  }
};

bool file_contains(const std::wstring& path, const std::string& needle) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str().find(needle) != std::string::npos;
}

// Any file under `root` holding the secret, as UTF-8 or as UTF-16LE.
std::wstring find_secret_on_disk(const std::wstring& root) {
  const std::string u8 = utf8(kSecret);
  const std::string u16(reinterpret_cast<const char*>(kSecret.data()), kSecret.size() * sizeof(wchar_t));
  std::vector<std::wstring> stack{root};
  while (!stack.empty()) {
    const std::wstring d = stack.back();
    stack.pop_back();
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((d + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      const std::wstring n = fd.cFileName;
      if (n == L"." || n == L"..") continue;
      if (n == L"GNLinkCapture.exe") continue;  // the staged copy of THIS test's image, which holds the test string
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        stack.push_back(d + n + L"\\");
      } else if (file_contains(d + n, u8) || file_contains(d + n, u16)) {
        FindClose(h);
        return d + n;
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  return {};
}

// The result window the viewer shows (owned by the viewer window): its text, once it has one.
std::wstring result_box_text(HWND box) {
  wchar_t b[1024] = {};
  wait_until([&] {
    HWND s = GetDlgItem(box, kPreloginResultTextId);
    return s && GetWindowTextW(s, b, 1023) > 0;
  }, 3000);
  Sleep(300);  // painted before anyone takes its picture
  return b;
}

// Presses its 확인 button until it is gone.
void close_result(HWND box) {
  for (int i = 0; i < 30 && IsWindow(box); ++i) {
    PostMessageW(GetDlgItem(box, 1), BM_CLICK, 0, 0);
    Sleep(100);
  }
}

HWND wait_window(const wchar_t* cls, const wchar_t* title, int ms) {
  HWND w = nullptr;
  wait_until([&] { return (w = FindWindowW(cls, title)) != nullptr && IsWindowVisible(w); }, ms);
  return w;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  bool notPrelogin = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--thumbnail") {  // staged as GNLinkCapture.exe: a helper that never answers
      Sleep(300000);
      return 0;
    }
    if (a == L"--not-prelogin") notPrelogin = true;
  }
  if (!host_e2e_allowed() || GetEnvironmentVariableW(L"REMOTE60_ALLOW_UI_TEST", nullptr, 0) == 0) {
    std::printf("SKIP  viewer_prelogin_login_e2e_test (starts a listening host and types into its own window)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 and REMOTE60_ALLOW_UI_TEST=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  Tee tee(std::cout.rdbuf());
  std::cout.rdbuf(&tee);
  std::cout.setf(std::ios::unitbuf);
  std::cout << "mode: " << (notPrelogin ? "host WITHOUT --prelogin (negative: the existing unlock path)"
                                        : "host in --prelogin (\"Windows 로그인\")")
            << "\n";
  {
    wchar_t cap[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_CAPTURE_DIR", cap, MAX_PATH) > 0) {
      gCaptureDir = cap;
      if (gCaptureDir.back() != L'\\') gCaptureDir += L'\\';
      CreateDirectoryW(gCaptureDir.c_str(), nullptr);
    }
  }

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

  StagingDir staging;
  if (!staging.Create(L"prelogin_login")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();
  // The viewer's credential store (the existing unlock path's) lives here for this run.
  const std::wstring viewerAppData = dir + L"viewer_localappdata";
  CreateDirectoryW(viewerAppData.c_str(), nullptr);
  SetEnvironmentVariableW(L"LOCALAPPDATA", viewerAppData.c_str());

  // The directory-authenticated client socket: the capability is issued for exactly this tuple.
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  InetPtonW(AF_INET, L"127.0.0.1", &local.sin_addr);
  bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local));
  int len = sizeof(local);
  getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len);
  const uint16_t clientPort = ntohs(local.sin_port);
  const std::string token(32, 'p');

  FakeDirectory directory;
  check("the fake directory starts", directory.Start());
  directory.Script("/healthz", {Reply{200, "{\"ok\":true,\"observe\":{\"port\":" + std::to_string(directory.udpPort()) + "}}"}});
  directory.Script("/api/host/register",
                   {Reply{200, "{\"ok\":true,\"hostId\":\"h-prelogin\",\"hostToken\":\"" + std::string(32, 'e') + "\"}"}});
  directory.Script("/api/host/heartbeat", {Reply{200, "{\"ok\":true,\"pendingPunch\":[{\"ip\":\"127.0.0.1\",\"port\":" +
                                                          std::to_string(clientPort) + ",\"punchToken\":\"" + token + "\"}]}"}});
  SetEnvironmentVariableW(L"REMOTE60_DIRECTORY_HEARTBEAT_SEC", L"120");

  Targets targets;
  check("the test's own target and decoy windows are up", targets.Start());

  HANDLE restricted = token_without_shutdown_privilege();
  Host host;
  const uint16_t hostPort = e2e_pick_free_udp_port();
  const bool started = restricted && hostPort != 0 &&
                       start_host(&host, dir, directory.url(), hostPort, !notPrelogin, targets.title, restricted);
  check("the host (test build, staged as GNLinkStream.exe) started", started);
  const bool punched =
      started && wait_until([&] { return host.log().find("directory punch ->") != std::string::npos; }, 30000);
  check("the host registered and collected the viewer's capability from its heartbeat", punched);

  // ------------------------------------------------------------------ the viewer, for real
  ViewerState ctx;
  std::thread worker, ingress;
  std::atomic<bool> ingressStop{false};
  std::atomic<uint64_t> media{0};
  uint32_t nextResumeId = 0x7500;
  uint64_t generation = 1;
  HWND viewerWindow = nullptr;
  bool connected = false;
  std::vector<std::string> toolbarLines;
  std::mutex toolbarMu;
  if (punched) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;  // the product's
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60PreloginLoginViewer";
    RegisterClassExW(&wc);
    viewerWindow = CreateWindowExW(0, wc.lpszClassName, L"remote60 prelogin login viewer", WS_OVERLAPPEDWINDOW, 500, 40,
                                   800, 500, nullptr, nullptr, wc.hInstance, &ctx);
    check("the product's window procedure owns a window", viewerWindow != nullptr);
    ShowWindow(viewerWindow, SW_SHOWNOACTIVATE);
    ctx.session.hwnd = viewerWindow;
    ctx.session.running.store(true);
    ctx.session.inputEnabled.store(true);
    ctx.session.controlRequired = true;

    // The product's toolbar, with the product's callback for the new item (viewer_startup.cpp's).
    SessionToolbarCallbacks cb;
    cb.onLog = [&](const std::string& line) {
      std::lock_guard<std::mutex> l(toolbarMu);
      toolbarLines.push_back(line);
    };
    cb.onPreloginLogin = [&ctx] { remote60::native_poc::viewer::request_prelogin_login(ctx, ctx.session.hwnd); };
    check("the session toolbar is created", session_toolbar_create(viewerWindow, std::move(cb)));
    session_toolbar_set_visible(true);

    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(hostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    // Whatever the host's punch left on the socket is not the session's.
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);
    char drain[2048];
    while (recv(sock, drain, sizeof(drain), 0) > 0) {
    }
    nb = 0;
    ioctlsocket(sock, FIONBIO, &nb);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 50;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    ctx.session.sock = sock;

    UdpHelloOptions hello;
    hello.authToken = token;  // the directory's capability: a prelogin host takes no other session
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    std::string helloError;
    uint32_t ackFeatures = 0;
    check("the viewer's directory-authenticated Hello is answered",
          udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr), helloError);
    ctx.control.udpControl.Configure(
        [s = sock](const void* data, size_t n) { return send(s, static_cast<const char*>(data), static_cast<int>(n), 0) > 0; },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    ctx.control.overUdp.store(true, std::memory_order_release);
    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    getpeername(sock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    ViewerControlResume::Config cfg;
    cfg.decide.giveUpAfterUs = remote60::native_poc::viewer::SessionLivenessConfig{}.controlGoneWithVideoUs;
    ctx.control.resume.Configure(
        &ctx.control.udpControl,
        [s = sock](const void* data, size_t n) { return send(s, static_cast<const char*>(data), static_cast<int>(n), 0) > 0; },
        [](const std::string& line) { std::cout << "    " << line << "\n"; }, [&nextResumeId]() { return ++nextResumeId; },
        [&generation]() { return generation; }, cfg, (ackFeatures & kUdpFeatureControlResume) != 0, peer.sin_addr.s_addr,
        peer.sin_port);
    ingress = std::thread([&] {
      std::vector<uint8_t> buf(2048);
      while (!ingressStop.load()) {
        ctx.control.udpControl.Tick();
        const int n = recv(ctx.session.sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        if (ctx.control.resume.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (ctx.control.udpControl.OnPacket(buf.data(), static_cast<size_t>(n))) continue;
        media.fetch_add(1);
        ctx.recvLive.lastPublishUs.store(qpc_now_us(), std::memory_order_relaxed);
        ctx.recvLive.lastDatagramUs.store(qpc_now_us(), std::memory_order_relaxed);
      }
    });
    connected = wait_until([&] { return media.load() > 0; }, 20000);
    check("video is arriving", connected, std::to_string(media.load()) + " media datagrams");
    static remote60::native_poc::viewer::Args args;
    args.inputLogEvery = 0;
    static remote60::native_poc::viewer::ControlClient control(ctx, args, /*startInPicker=*/false);
    ctx.control.scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
    ctx.control.connected.store(true, std::memory_order_relaxed);
    worker = std::thread([&] { control.Run(); });
  }

  const auto pump_until = [&](const std::function<bool()>& done, int budgetMs) {
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
    for (;;) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (done()) return true;
      if (GetTickCount() >= deadline) return done();
      Sleep(10);
    }
  };
  HWND bar = nullptr;
  // The x of the toolbar item with this id, found by pressing along the bar (the toolbar logs the id
  // under the press). A press there and a release elsewhere fires nothing, so the sweep is inert.
  const auto find_item = [&](const std::string& id) {
    RECT rc{};
    GetClientRect(bar, &rc);
    const int midY = (rc.bottom - rc.top) / 2;
    for (int x = 4; x < rc.right - 4; x += 5) {
      {
        std::lock_guard<std::mutex> l(toolbarMu);
        toolbarLines.clear();
      }
      SendMessageW(bar, WM_LBUTTONDOWN, 0, MAKELPARAM(x, midY));
      SendMessageW(bar, WM_LBUTTONUP, 0, MAKELPARAM(rc.right + 50, midY));  // released off it: no click
      std::lock_guard<std::mutex> l(toolbarMu);
      for (const std::string& line : toolbarLines) {
        const size_t p = line.find("down id=");
        if (p != std::string::npos && line.substr(p + 8, line.find(' ', p + 8) - (p + 8)) == id) return x;
      }
    }
    return -1;
  };

  // The operator: what a user does with the password window, from another thread (the window is
  // modal on the viewer's UI thread): type the secret into its edit key by key, then press 입력.
  struct OperatorRun {
    bool sawPrompt = false, typedAll = false, captured = false;
    std::wstring promptTitle;
  };
  const auto operate_prompt = [&](OperatorRun* run, const wchar_t* cls, bool cancel) {
    HWND prompt = nullptr;
    HWND wrong = nullptr;  // the other prompt: a wrong path must fail here, not hang the run in a modal
    const wchar_t* other = (std::wstring(cls) == L"GNLinkUnlockPrompt") ? kPreloginPromptClass : L"GNLinkUnlockPrompt";
    wait_until([&] {
      prompt = FindWindowW(cls, nullptr);
      wrong = FindWindowW(other, nullptr);
      return (prompt && IsWindowVisible(prompt)) || (wrong && IsWindowVisible(wrong));
    }, 8000);
    if (!prompt && wrong) {
      wchar_t t[128] = {};
      GetWindowTextW(wrong, t, 127);
      run->promptTitle = t;  // what opened instead
      save_capture(wrong, L"wrong_prompt_opened");
      PostMessageW(GetDlgItem(wrong, 2), BM_CLICK, 0, 0);  // 취소
      wait_until([&] { return !IsWindow(wrong); }, 3000);
      return;
    }
    run->sawPrompt = prompt != nullptr;
    if (!prompt) return;
    wchar_t t[128] = {};
    GetWindowTextW(prompt, t, 127);
    run->promptTitle = t;
    HWND edit = FindWindowExW(prompt, nullptr, L"EDIT", nullptr);
    if (!cancel && edit) {
      for (wchar_t c : kSecret) PostMessageW(edit, WM_CHAR, c, 1);
      run->typedAll = wait_until([&] { return GetWindowTextLengthW(edit) == static_cast<int>(kSecret.size()); }, 3000);
    }
    Sleep(150);
    run->captured = save_capture(prompt, cancel ? L"unlock_prompt_old_path" : L"prelogin_prompt_typed");
    HWND button = GetDlgItem(prompt, cancel ? 2 : 1);
    PostMessageW(button, BM_CLICK, 0, 0);
    wait_until([&] { return !IsWindow(prompt); }, 3000);
  };

  const bool ready = connected && pump_until([&] { return ctx.control.lastRttUs.load() > 0; }, 25000);
  check("the worker completes a round trip with the host", ready);
  const auto flags = [&] { return ctx.session.hostCaptureFlags.load(); };
  if (ready) {
    bar = FindWindowExW(nullptr, nullptr, L"Remote60SessionToolbar", nullptr);
    check("the toolbar window exists", bar != nullptr);
  }

  if (ready && bar && !notPrelogin) {
    std::cout << "\n--- the host is at the pre-login screen ---\n";
    check("THE PONG ADVERTISES kCaptureFlagPreloginV1 (0x2000)", pump_until([&] { return (flags() & kCaptureFlagPreloginV1) != 0; }, 8000),
          std::to_string(flags()));
    remote60::native_poc::viewer::push_session_toolbar_state(ctx);
    session_toolbar_follow_owner();
    pump_until([] { return false; }, 300);
    const int x = find_item("7");
    check("THE TOOLBAR LAYS OUT \"Windows 로그인\" (item 7)", x >= 0, std::to_string(x));
    save_capture(bar, L"toolbar_prelogin_item");

    // ---- 1. the toolbar item -> prompt -> typed into the test window, byte for byte
    std::cout << "\n--- 1. the toolbar item, the password typed, 입력 ---\n";
    targets.Clear();
    OperatorRun run;
    std::atomic<bool> fronted{false};
    std::wstring boxText;
    bool boxCaptured = false;
    std::thread op([&] {
      operate_prompt(&run, kPreloginPromptClass, false);
      Sleep(300);  // the prompt hands the foreground back to the viewer as it closes; then the user's window
      fronted = bring_front(targets.main);
      HWND box = wait_window(kPreloginResultClass, nullptr, 15000);
      if (box) {
        boxText = result_box_text(box);
        boxCaptured = save_capture(box, L"result_injected");
        close_result(box);
      }
    });
    RECT rc{};
    GetClientRect(bar, &rc);
    const int midY = (rc.bottom - rc.top) / 2;
    PostMessageW(bar, WM_LBUTTONDOWN, 0, MAKELPARAM(x, midY));
    PostMessageW(bar, WM_LBUTTONUP, 0, MAKELPARAM(x, midY));
    pump_until([&] { return !boxText.empty(); }, 25000);
    op.join();
    check("the click opened the product's password window", run.sawPrompt, utf8(run.promptTitle));
    check("...titled \"GNLink — Windows 로그인\"", run.promptTitle == kPreloginPromptTitle);
    check("...took every character typed into it", run.typedAll);
    check("the test window came to the front after 입력", fronted.load());
    const std::wstring got = Targets::text(targets.edit);
    save_capture(targets.main, L"target_window_received");
    check("THE HOST TYPED THE SECRET INTO THE TEST WINDOW, BYTE FOR BYTE", got == kSecret, hexw(got));
    check("...and nothing into the decoy", Targets::text(targets.decoyEdit).empty());
    check("THE RESULT WINDOW SAYS \"typed\", NOT \"logged in\"",
          boxText.find(L"입력했습니다") != std::wstring::npos && boxText.find(L"로그인 성공이 아닙니다") != std::wstring::npos,
          utf8(boxText));
    check("the host's result line: stage 8 (Injected), once", host.Count(" stage=8 win32=0") == 1);
    check("the result window was captured", boxCaptured || gCaptureDir.empty());

    // ---- 2. Ctrl+Alt+U in the viewer window: the same path, while the host is at the logon screen
    std::cout << "\n--- 2. Ctrl+Alt+U ---\n";
    Sleep(600);  // the host's challenge rate limit
    targets.Clear();
    OperatorRun run2;
    std::wstring box2;
    std::thread op2([&] {
      operate_prompt(&run2, kPreloginPromptClass, false);
      Sleep(300);
      bring_front(targets.main);
      HWND box = wait_window(kPreloginResultClass, nullptr, 15000);
      if (box) {
        box2 = result_box_text(box);
        close_result(box);
      }
    });
    BYTE keys[256] = {};
    GetKeyboardState(keys);
    BYTE held[256];
    std::memcpy(held, keys, sizeof(held));
    held[VK_CONTROL] = held[VK_LCONTROL] = 0x80;
    held[VK_MENU] = held[VK_LMENU] = 0x80;
    SetKeyboardState(held);  // this thread's view of the modifiers only (what GetKeyState reads)
    SendMessageW(viewerWindow, WM_KEYDOWN, 'U', 1);  // not posted: no TranslateMessage, no WM_CHAR
    pump_until([&] { return run2.sawPrompt || !box2.empty(); }, 8000);
    SetKeyboardState(keys);
    pump_until([&] { return !box2.empty(); }, 25000);
    op2.join();
    check("Ctrl+Alt+U opened the \"Windows 로그인\" window (not the unlock prompt)", run2.sawPrompt && run2.promptTitle == kPreloginPromptTitle,
          utf8(run2.promptTitle));
    check("THE SECRET ARRIVED AGAIN, BYTE FOR BYTE", Targets::text(targets.edit) == kSecret, hexw(Targets::text(targets.edit)));
    check("...the result said typed", box2.find(L"입력했습니다") != std::wstring::npos, utf8(box2));

    // ---- 3. negative: the focus is on another window -> nothing is typed anywhere
    std::cout << "\n--- 3. NEGATIVE: the test window does not have the focus ---\n";
    Sleep(600);
    targets.Clear();
    OperatorRun run3;
    std::wstring box3;
    bool box3Captured = false;
    std::thread op3([&] {
      operate_prompt(&run3, kPreloginPromptClass, false);
      Sleep(300);
      bring_front(targets.decoy);  // the user is somewhere else
      HWND box = wait_window(kPreloginResultClass, nullptr, 20000);
      if (box) {
        box3 = result_box_text(box);
        box3Captured = save_capture(box, L"result_not_at_target");
        close_result(box);
      }
    });
    PostMessageW(bar, WM_LBUTTONDOWN, 0, MAKELPARAM(x, midY));
    PostMessageW(bar, WM_LBUTTONUP, 0, MAKELPARAM(x, midY));
    pump_until([&] { return !box3.empty(); }, 30000);
    op3.join();
    (void)box3Captured;
    check("the prompt was used", run3.sawPrompt && run3.typedAll);
    check("NEGATIVE: NOTHING reached the test window", Targets::text(targets.edit).empty(), hexw(Targets::text(targets.edit)));
    check("NEGATIVE: and NOTHING was typed into the window that had the focus",
          Targets::text(targets.decoyEdit).empty(), hexw(Targets::text(targets.decoyEdit)));
    check("the result says it was not typed (the target was not the logon screen)",
          box3.find(L"입력하지 않았습니다") != std::wstring::npos, utf8(box3));
    check("the host's result line: stage 1 (RejectedPolicy)", host.Count(" stage=1 win32=0") == 1);

    // ---- typed is not logged in: no "logged in" while the host stays at the logon screen
    pump_until([] { return false; }, 1500);
    check("no \"logged in\" window appeared (the host is still at the logon screen)",
          FindWindowW(kPreloginResultClass, nullptr) == nullptr && (flags() & kCaptureFlagPreloginV1) != 0);
  }

  if (ready && bar && notPrelogin) {
    std::cout << "\n--- the host is NOT at the pre-login screen ---\n";
    pump_until([&] { return flags() != 0; }, 8000);
    check("THE PONG DOES NOT ADVERTISE 0x2000", flags() != 0 && (flags() & kCaptureFlagPreloginV1) == 0, std::to_string(flags()));
    remote60::native_poc::viewer::push_session_toolbar_state(ctx);
    session_toolbar_follow_owner();
    pump_until([] { return false; }, 300);
    check("NEGATIVE: the toolbar lays out NO \"Windows 로그인\" item", find_item("7") < 0);
    save_capture(bar, L"toolbar_not_prelogin");

    std::cout << "\n--- Ctrl+Alt+U takes the existing unlock path ---\n";
    OperatorRun run;
    std::thread op([&] { operate_prompt(&run, L"GNLinkUnlockPrompt", /*cancel=*/true); });
    BYTE keys[256] = {};
    GetKeyboardState(keys);
    BYTE held[256];
    std::memcpy(held, keys, sizeof(held));
    held[VK_CONTROL] = held[VK_LCONTROL] = 0x80;
    held[VK_MENU] = held[VK_LMENU] = 0x80;
    SetKeyboardState(held);
    SendMessageW(viewerWindow, WM_KEYDOWN, 'U', 1);  // not posted: no TranslateMessage, no WM_CHAR
    pump_until([&] { return run.sawPrompt; }, 8000);
    SetKeyboardState(keys);
    pump_until([] { return false; }, 1000);
    op.join();
    check("Ctrl+Alt+U opened the EXISTING unlock prompt (GNLinkUnlockPrompt), as before", run.sawPrompt,
          utf8(run.promptTitle));
    check("NEGATIVE: no \"Windows 로그인\" window exists", FindWindowW(kPreloginPromptClass, nullptr) == nullptr);

    std::cout << "\n--- the entry itself, and a request that slipped through ---\n";
    remote60::native_poc::viewer::request_prelogin_login(ctx, viewerWindow);
    pump_until([] { return false; }, 500);
    check("NEGATIVE: request_prelogin_login opens nothing at a host that is not at the logon screen",
          FindWindowW(kPreloginPromptClass, nullptr) == nullptr && !ctx.session.preloginCredRequested.load());
    // A request queued while the host was at the logon screen, drained after it left it.
    {
      std::lock_guard<std::mutex> l(ctx.session.preloginCredMu);
      ctx.session.preloginCredPassword = kSecret;
    }
    std::wstring box;
    std::thread op2([&] {
      HWND b = wait_window(kPreloginResultClass, nullptr, 10000);
      if (b) {
        box = result_box_text(b);
        save_capture(b, L"result_not_at_logon");
        close_result(b);
      }
    });
    ctx.session.preloginCredRequested.store(true);
    pump_until([&] { return !box.empty(); }, 12000);
    op2.join();
    check("...is answered locally: \"not at the logon screen\"", box.find(L"로그인 화면 상태가 아니") != std::wstring::npos, utf8(box));
    check("NEGATIVE: the prelogin path reached the host 0 times", host.Count("[native-video-host][prelogin-cred]") == 0);
    check("...and the queued password is gone from the viewer", ctx.session.preloginCredPassword.empty());

    std::cout << "\n--- \"logged in\" is the host leaving the logon screen after a typed password ---\n";
    std::wstring logon;
    std::thread op3([&] {
      HWND b = wait_window(kPreloginResultClass, nullptr, 10000);
      if (b) {
        logon = result_box_text(b);
        save_capture(b, L"result_logged_in");
        close_result(b);
      }
    });
    // As if this viewer had just been told "typed" by a pre-login host: the next pong from a host that
    // is not at the logon screen is the logon. (The handoff to the user's host is the G check.)
    ctx.session.preloginInjectedAtUs.store(qpc_now_us());
    pump_until([&] { return !logon.empty(); }, 12000);
    op3.join();
    check("A POSTED \"logged in\" WINDOW, DIFFERENT FROM \"typed\"", logon.find(L"로그인되었습니다") != std::wstring::npos &&
                                                                     logon.find(L"입력했습니다") == std::wstring::npos,
          utf8(logon));
  }

  // ------------------------------------------------------------------------------- teardown
  ctx.session.running.store(false);
  if (worker.joinable()) worker.join();
  ingressStop.store(true);
  if (ingress.joinable()) ingress.join();
  if (ctx.session.sock != INVALID_SOCKET) closesocket(ctx.session.sock);
  session_toolbar_destroy();
  if (viewerWindow) DestroyWindow(viewerWindow);
  targets.Stop();
  host.Stop();
  if (restricted) CloseHandle(restricted);

  std::cout << "\n--- nothing kept ---\n";
  const std::string secret8 = utf8(kSecret);
  check("the password is in none of the viewer's output", tee.all().find(secret8) == std::string::npos &&
                                                            tee.all().find("Pw-7Qz") == std::string::npos);
  check("...nor in the host's", host.log().find("Pw-7Qz") == std::string::npos);
  const std::wstring onDisk = find_secret_on_disk(dir);
  check("...nor in any file of this run (the viewer's credential store included)", onDisk.empty(), utf8(onDisk));
  WIN32_FIND_DATAW fd{};
  HANDLE f = FindFirstFileW((viewerAppData + L"\\GNLink\\unlock_*.cred").c_str(), &fd);
  check("no credential file was written by the viewer", f == INVALID_HANDLE_VALUE);
  if (f != INVALID_HANDLE_VALUE) FindClose(f);

  std::cout.rdbuf(tee.real);
  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  WSACleanup();
  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks << " checks, "
            << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
