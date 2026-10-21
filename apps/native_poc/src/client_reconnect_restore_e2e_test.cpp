// dxgi-wedge-fix r1 (Codex B7): the whole chain, as processes, against a private directory.
//
//   the product shell (this exe: client_shell_main.cpp with only its entry renamed, REMOTE60_SHELL_TEST_SEAM)
//     signs in to the fixture directory (DOM), picks the PC (DOM) -> the REAL GNLinkViewer.exe
//   the viewer's picker: its desktop card pressed with the window messages a mouse produces
//     -> the host's answer -> the first frame -> "[viewer-restore] target=..." -> the shell's pump
//   the runner makes the streaming child's DXGI worker stop inside its frame handler; the PRODUCT
//     watchdog ends it (exit 44); the REAL supervisor (GNLinkHostUiTest) relaunches it on WGC
//   the viewer finds its session dead and closes (43) -> the shell reconnects on its own with
//     --reconnect --restore-target -> the new viewer goes back to the desktop without a pick
//     -> a NEW frame is rendered in its window
//
// Started by apps/directory/test/dxgi_wedge_reconnect_runner.js, which owns the directory, the host and
// the wedge; this process reports PASS/FAIL lines, timings and screenshots (BMP, of the viewer window as
// the screen shows it -- the swapchain is composited, so this is what a person sees). Nothing is sent to
// the window that has the foreground; the viewer's picker is pressed with posted WM_LBUTTONDOWN/UP, and
// input injection is OFF on the host (the runner's GNLINK_HOST_TEST_STREAM_PORT path).
//
// Environment (all set by the runner): GNLINK_RECOVERY_TEST_ROOT (fixture with .fixture and profile =
// TEMP), GNLINK_CHAIN_DIRECTORY, GNLINK_CHAIN_ACCOUNT, GNLINK_CHAIN_PASSWORD (the fixture's test account),
// GNLINK_CHAIN_HOSTNAME, GNLINK_CHAIN_WEDGE_FILE, GNLINK_CHAIN_SHOTS (folder for the screenshots).

#define wWinMain chain_product_entry
#include "client_shell_main.cpp"
#undef wWinMain
#include <shlwapi.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace {

uint64_t gT0 = 0;
double secs() { return (GetTickCount64() - gT0) / 1000.0; }

void chain_pump(unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    Sleep(5);
  } while (GetTickCount64() < end);
}
bool chain_wait(const std::function<bool()>& predicate, unsigned ms) {
  const uint64_t end = GetTickCount64() + ms;
  while (GetTickCount64() < end) {
    if (predicate()) return true;
    chain_pump(50);
  }
  return predicate();
}
std::string chain_eval(const std::wstring& script) {
  struct Reply {
    bool done = false;
    std::string text;
  };
  auto reply = std::make_shared<Reply>();
  if (!gWebView) return {};
  gWebView->ExecuteScript(script.c_str(), Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                              [reply](HRESULT hr, LPCWSTR value) -> HRESULT {
                                                if (SUCCEEDED(hr) && value) reply->text = narrow(value);
                                                reply->done = true;
                                                return S_OK;
                                              })
                                              .Get());
  chain_wait([&] { return reply->done; }, 3000);
  return reply->text;
}
bool chain_dom(const std::wstring& script, unsigned timeout = 15000) {
  return chain_wait([&] { return chain_eval(script) == "true"; }, timeout);
}
int gFailures = 0;
void check(bool ok, const std::string& label, const std::string& detail = {}) {
  std::printf("%s [%6.1fs] %s%s%s\n", ok ? "PASS" : "FAIL", secs(), label.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
  std::fflush(stdout);
  if (!ok) ++gFailures;
}
void require(bool ok, const std::string& label, const std::string& detail = {}) {
  check(ok, label, detail);
  if (!ok) throw std::runtime_error(label);
}
std::string envs(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

// The viewer's own log: the shell's pump writes every viewer line, with its context, here.
std::string viewer_log_text() {
  std::string all;
  for (const wchar_t* name : {L"\\viewer.log.1", L"\\viewer.log"}) {
    std::ifstream in(log_dir_path() + name, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    all += s.str();
  }
  return all;
}
std::string shell_log_text() {
  std::ifstream in(log_dir_path() + L"\\client.log", std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}
size_t count_of(const std::string& text, const std::string& needle) {
  size_t n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++n;
  return n;
}
// Lines of `text` from viewer process `pid` (the pump appends " viewerPid=<pid>").
std::string lines_of_pid(const std::string& text, DWORD pid) {
  const std::string tag = " viewerPid=" + std::to_string(pid) + " ";
  std::string out;
  std::istringstream in(text);
  for (std::string l; std::getline(in, l);)
    if (l.find(tag) != std::string::npos) out += l + "\n";
  return out;
}

HWND window_of_pid(DWORD pid) {
  struct Find {
    DWORD pid;
    HWND found;
  } f{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        auto* f = reinterpret_cast<Find*>(lp);
        DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner);
        wchar_t cls[64]{};
        GetClassNameW(h, cls, 64);
        if (owner == f->pid && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) {
          RECT r{};
          GetClientRect(h, &r);
          if (r.right - r.left > 200) {
            f->found = h;
            return FALSE;
          }
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  return f.found;
}

struct Shot {
  bool ok = false;
  double pickerColorShare = 1.0;  // share of client pixels that are exactly the picker background
  size_t distinctColors = 0;
};
// The viewer's client area as the screen shows it (BitBlt from the screen DC), saved as a BMP.
Shot screenshot(HWND hwnd, const std::wstring& path) {
  Shot s;
  if (!hwnd) return s;
  ShowWindow(hwnd, SW_RESTORE);
  SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  chain_pump(150);
  RECT rc{};
  GetClientRect(hwnd, &rc);
  POINT tl{0, 0};
  ClientToScreen(hwnd, &tl);
  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) return s;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, w, h, screen, tl.x, tl.y, SRCCOPY | CAPTUREBLT);
  SelectObject(mem, old);
  const auto* px = static_cast<const uint32_t*>(bits);
  size_t picker = 0;
  std::set<uint32_t> colors;
  for (int i = 0; i < w * h; ++i) {
    const uint32_t c = px[i] & 0xFFFFFF;
    if (c == 0x0D0F14) ++picker;  // RGB(13,15,20), BGRA in memory -> 0x140F0D; checked both ways below
    if (c == 0x140F0D) ++picker;
    if (colors.size() < 5000) colors.insert(c);
  }
  s.pickerColorShare = static_cast<double>(picker) / (static_cast<double>(w) * h);
  s.distinctColors = colors.size();
  // BMP file
  BITMAPFILEHEADER fh{};
  const DWORD imageBytes = static_cast<DWORD>(w) * h * 4;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
  fh.bfSize = fh.bfOffBits + imageBytes;
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  out.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
  out.write(static_cast<const char*>(bits), imageBytes);
  s.ok = out.good();
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  return s;
}

std::string shot_text(const Shot& s) {
  char b[96];
  std::snprintf(b, sizeof(b), "pickerBg=%.1f%% colors=%zu", s.pickerColorShare * 100.0, s.distinctColors);
  return b;
}

// "[picker] first paint after show, client=WxH desktopCard=l,t,r,b" -- the newest one of this viewer.
bool desktop_card(const std::string& viewerLines, RECT* out) {
  const size_t at = viewerLines.rfind("desktopCard=");
  if (at == std::string::npos) return false;
  long l = 0, t = 0, r = 0, b = 0;
  if (std::sscanf(viewerLines.c_str() + at, "desktopCard=%ld,%ld,%ld,%ld", &l, &t, &r, &b) != 4) return false;
  *out = RECT{l, t, r, b};
  return r > l && b > t;
}

}  // namespace

int main() {
  gT0 = GetTickCount64();
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const char* fixture = std::getenv("GNLINK_RECOVERY_TEST_ROOT");
  wchar_t temporary[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temporary);
  const std::string directory = envs("GNLINK_CHAIN_DIRECTORY");
  if (!fixture || !std::filesystem::exists(std::filesystem::path(fixture) / ".fixture") ||
      std::filesystem::weakly_canonical(temporary) !=
          std::filesystem::weakly_canonical(std::filesystem::path(fixture) / "profile") ||
      directory.rfind("http://127.0.0.1:", 0) != 0) {
    std::puts("FAIL isolated fixture/profile and a loopback directory required");
    return 2;
  }
  gShellTestDirectoryUrl = directory;
  std::filesystem::create_directories(std::filesystem::path(fixture) / "shelldata");
  gShellTestDataDir = (std::filesystem::path(fixture) / "shelldata").wstring();
  const std::wstring shots = widen(envs("GNLINK_CHAIN_SHOTS"));
  const std::wstring wedgeFile = widen(envs("GNLINK_CHAIN_WEDGE_FILE"));
  const std::string hostName = envs("GNLINK_CHAIN_HOSTNAME");

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  std::string socketError;
  if (!initialize_sockets(&socketError)) return 2;
  LogUploadShutdown uploader;
  int result = 0;
  try {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    RegisterClassExW(&wc);
    gWindow = CreateWindowExW(0, kWindowClass, L"GNLink chain test", WS_OVERLAPPEDWINDOW, 20, 20, 520, 700, nullptr,
                              nullptr, wc.hInstance, nullptr);
    ShowWindow(gWindow, SW_SHOWNOACTIVATE);
    require(SUCCEEDED(create_shell_webview()), "the product shell's WebView starts");
    require(chain_dom(L"!!document.getElementById('signIn')&&!document.getElementById('signIn').disabled"),
            "the sign-in form is up");
    chain_eval(L"document.getElementById('account').value='" + widen(envs("GNLINK_CHAIN_ACCOUNT")) +
               L"';document.getElementById('password').value='" + widen(envs("GNLINK_CHAIN_PASSWORD")) +
               L"';document.getElementById('signIn').click();true");
    require(chain_dom(L"!document.getElementById('hostsCard').classList.contains('hidden')"),
            "signed in to the fixture directory: the PC list is shown");
    const std::wstring hostSel = L"Array.from(document.querySelectorAll('.host')).find(function(h){return h.textContent.toLowerCase().indexOf('" +
                                 widen(hostName) + L"'.toLowerCase())>=0;})";
    const bool listed = chain_dom(L"(function(){var h=" + hostSel + L";return !!h&&!h.disabled;})()", 60000);
    if (!listed) {
      std::printf("      (hosts shown: %s)\n",
                  chain_eval(L"Array.from(document.querySelectorAll('.host')).map(function(h){return h.textContent+(h.disabled?' [disabled]':'');}).join(' | ')").c_str());
    }
    require(listed, "the test host is listed and can be pressed");
    chain_eval(L"(function(){" + hostSel + L".click();return true;})()");

    // ---- session 1: the user picks the desktop in the picker -------------------------------------
    DWORD pid1 = 0;
    require(chain_wait([&] {
              for (const auto& e : gViewerByHost) pid1 = e.second.pid;
              return pid1 != 0;
            }, 15000),
            "the shell started the real GNLinkViewer", "pid=" + std::to_string(pid1));
    RECT card{};
    require(chain_wait([&] { return desktop_card(lines_of_pid(viewer_log_text(), pid1), &card); }, 60000),
            "the viewer connected through the directory and painted its picker");
    HWND viewer1 = window_of_pid(pid1);
    require(viewer1 != nullptr, "the viewer window is found by its process");
    chain_pump(800);  // the picker guard: shown for at least 300 ms before a pick counts
    const Shot pickerShot = screenshot(viewer1, shots + L"\\1-picker.bmp");
    check(pickerShot.ok, "screenshot 1: the picker (before any pick)", shot_text(pickerShot));
    const LPARAM at = MAKELPARAM((card.left + card.right) / 2, (card.top + card.bottom) / 2);
    PostMessageW(viewer1, WM_MOUSEMOVE, 0, at);
    PostMessageW(viewer1, WM_LBUTTONDOWN, MK_LBUTTON, at);
    chain_pump(80);
    PostMessageW(viewer1, WM_LBUTTONUP, 0, at);
    require(chain_wait([&] { return lines_of_pid(viewer_log_text(), pid1).find("[viewer-restore] target=desktop") !=
                                    std::string::npos; }, 30000),
            "the desktop card pressed: confirmed by the host, first frame decoded, reported to the shell");
    const std::string reported = [&] {
      const std::string v = lines_of_pid(viewer_log_text(), pid1);
      const size_t a = v.rfind("[viewer-restore] target=");
      return v.substr(a + 24, v.find(' ', a + 24) - (a + 24));
    }();
    std::printf("      (reported: %s)\n", reported.c_str());
    chain_pump(1500);
    const Shot streamShot = screenshot(viewer1, shots + L"\\2-session1-stream.bmp");
    check(streamShot.ok && streamShot.pickerColorShare < 0.05 && pickerShot.pickerColorShare > 0.15,
          "screenshot 2: session 1 shows the stream, not the picker (negative control: screenshot 1 is the picker)",
          "picker: " + shot_text(pickerShot) + " / stream: " + shot_text(streamShot));

    // ---- the wedge --------------------------------------------------------------------------------
    { std::ofstream(wedgeFile) << "wedge\n"; }
    const double tWedge = secs();
    std::printf("      (wedge file created at %.1fs)\n", tWedge);
    require(chain_wait([&] { return shell_log_text().find("viewer exit operation=") != std::string::npos &&
                                    shell_log_text().find("reconnect=scheduled") != std::string::npos; }, 60000),
            "the viewer's session died and the shell scheduled the reconnect");
    const double tViewerExit = secs();
    {
      const std::string log = shell_log_text();
      const size_t a = log.find("viewer exit operation=");
      std::printf("      (%s)\n", log.substr(a, log.find('\n', a) - a).c_str());
      check(log.find("restore=" + reported) != std::string::npos,
            "the reconnect carries the reported target (restore=" + reported + ")");
    }
    DWORD pid2 = 0;
    require(chain_wait([&] {
              for (const auto& e : gViewerByHost)
                if (e.second.pid != pid1) pid2 = e.second.pid;
              return pid2 != 0;
            }, 15000),
            "the shell started a NEW viewer on its own", "pid=" + std::to_string(pid2));
    const double tRelaunch = secs();
    // While it goes back: what the window says (the picker overlay, "재연결 중 ...").
    HWND viewer2 = nullptr;
    chain_wait([&] { return (viewer2 = window_of_pid(pid2)) != nullptr; }, 15000);
    int interim = 0;
    const bool restored = chain_wait([&] {
      if (viewer2 && interim < 6) screenshot(viewer2, shots + L"\\3-reconnecting-" + std::to_wstring(interim++) + L".bmp");
      return lines_of_pid(viewer_log_text(), pid2).find("[restore] result=restored") != std::string::npos;
    }, 60000);
    const std::string v2 = lines_of_pid(viewer_log_text(), pid2);
    check(v2.find("[restore] waiting target=" + reported) != std::string::npos,
          "the new viewer was started to restore exactly that target");
    require(restored, "the new viewer went back to it without a pick ([restore] result=restored)");
    const double tRestored = secs();
    check(v2.find("[picker] select source=") == std::string::npos, "...no picker click in the new viewer");
    // A NEW frame on screen: the stream is rendered, and it keeps moving.
    chain_pump(1500);
    const Shot after1 = screenshot(viewer2, shots + L"\\4-restored.bmp");
    check(after1.ok && after1.pickerColorShare < 0.05 && pickerShot.pickerColorShare > 0.15,
          "screenshot 4: the reconnected window shows the stream, not the picker", shot_text(after1));
    const size_t decodedBefore = count_of(v2, "decodedFrames=");
    chain_pump(3000);
    const std::string v2later = lines_of_pid(viewer_log_text(), pid2);
    check(count_of(v2later, "decodedFrames=") > decodedBefore && v2later.find("decodedFrames=0 ") == std::string::npos,
          "...and frames keep arriving and decoding after the restore");
    std::printf("\n      timeline: wedge %.1fs -> viewer exit seen %.1fs (+%.1f) -> new viewer %.1fs (+%.1f) -> "
                "restored %.1fs (+%.1f); wedge -> restored %.1f s\n",
                tWedge, tViewerExit, tViewerExit - tWedge, tRelaunch, tRelaunch - tViewerExit, tRestored,
                tRestored - tRelaunch, tRestored - tWedge);
  } catch (const std::exception& e) {
    std::printf("chain: FAIL at \"%s\"\n", e.what());
    result = 1;
  }
  // Every viewer this run started ends here: asked to close (the window's own WM_CLOSE), and -- test build
  // only -- through the terminate handle taken from its launch handle (never a pid lookup).
  for (auto& entry : gViewerByHost) {
    if (HWND w = window_of_pid(entry.second.pid)) PostMessageW(w, WM_CLOSE, 0, 0);
  }
  chain_pump(1500);
  for (auto& entry : gViewerByHost) {
    if (entry.second.testTerminate) TerminateProcess(entry.second.testTerminate, 0);
  }
  chain_pump(1500);
  if (gFailures) result = 1;
  std::printf("%s\n", result == 0 ? "client_reconnect_restore_e2e: ALL PASS" : "client_reconnect_restore_e2e: FAIL");
  std::fflush(stdout);
  TerminateProcess(GetCurrentProcess(), static_cast<UINT>(result));
  return result;
}
