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
//
// dxgi-wedge-fix r3 (Codex E2/E3) -- GNLINK_CHAIN_SCENARIO, after session 1's desktop is presented:
//   desktop     (default) the wedge -> reconnect -> the desktop again, as above.
//   monitor     the host (test seam) lists two screens; the SHIPPED viewer asks for the list itself (r4) and
//               its own toolbar shows the screen switch: pick the second screen through it -> "monitor"
//               reported -> wedge -> the reconnected viewer shows the picker with the reason and selects
//               nothing (a restore of the desktop is named as WRONG) -> a manual pick shows the stream.
//   nomonitors  the host does not advertise the monitor messages: no screen-list request, no switch.
//   presentfail the viewer is GNLinkViewerR3Test (GNLINK_CHAIN_VIEWER_EXE). Back to the picker
//               through the toolbar's 대상 선택; the fixture window B (GNLINK_CHAIN_B_TITLE, checked to be the
//               first listed window before anything is clicked) is picked with presents made to FAIL ->
//               B revealed, no B report, no restored -> the viewer gives up (43) -> the shell reconnects
//               with what was last SEEN (desktop) -> the desktop again.
//   presentok   the same, presents NOT failed while B is picked (the control): B reported as "window" ->
//               then presents fail (B is a WGC window; the DXGI wedge cannot end it) -> the reconnect carries
//               "window" -> the reconnected viewer shows the picker ("창은 자동으로 다시 고르지 않습니다").

#define wWinMain chain_product_entry
#include "client_shell_main.cpp"
#undef wWinMain
#include <shlwapi.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
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

// ---- r3 helpers: the viewer's toolbar and its picker cards, driven with posted mouse messages ----------
HWND toolbar_of_pid(DWORD pid) {
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
        if (owner == f->pid && std::wstring(cls) == L"Remote60SessionToolbar") {
          f->found = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  return f.found;
}

// Sweeps the toolbar with posted presses (each released OUTSIDE every button, so nothing is clicked) and
// reads the toolbar's own "[toolbar] down id=N x=X" lines: button id -> an x on it. Id 1 = 대상 선택,
// 3 = the screen switch (shown only for two or more screens).
std::map<int, int> sweep_toolbar(HWND toolbar, DWORD pid, int* height) {
  std::map<int, int> xs;
  RECT rc{};
  GetClientRect(toolbar, &rc);
  const int w = std::max<long>(rc.right, 600), y = std::max<long>(rc.bottom / 2, 12);
  if (height) *height = y;
  const size_t before = lines_of_pid(viewer_log_text(), pid).size();
  for (int x = 4; x < w; x += 10) {
    PostMessageW(toolbar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    PostMessageW(toolbar, WM_LBUTTONUP, 0, MAKELPARAM(30000, 30000));
    chain_pump(15);
  }
  chain_pump(800);
  const std::string lines = lines_of_pid(viewer_log_text(), pid).substr(before);
  std::istringstream in(lines);
  for (std::string l; std::getline(in, l);) {
    const size_t at = l.find("[toolbar] down id=");
    if (at == std::string::npos) continue;
    int id = 0, x = 0, yy = 0;
    if (std::sscanf(l.c_str() + at, "[toolbar] down id=%d x=%d y=%d", &id, &x, &yy) == 3 && id > 0 && !xs.count(id))
      xs[id] = x;
  }
  return xs;
}

void click_toolbar(HWND toolbar, int x, int y) {
  PostMessageW(toolbar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
  chain_pump(60);
  PostMessageW(toolbar, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
}

HWND menu_of_pid(DWORD pid) {
  struct Find {
    DWORD pid;
    HWND found;
  } f{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        auto* f = reinterpret_cast<Find*>(lp);
        DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner);
        wchar_t cls[16]{};
        GetClassNameW(h, cls, 16);
        if (owner == f->pid && std::wstring(cls) == L"#32768" && IsWindowVisible(h)) {
          f->found = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  return f.found;
}

// The picker's card for the first listed window: slot 1 of the grid whose slot 0 (the desktop card) the
// picker logged -- the same arithmetic as card_rect_for_slot (gap 14 px at 96 dpi).
RECT window_card(const RECT& desktop, HWND viewer, const RECT& client) {
  const int dpi = static_cast<int>(GetDpiForWindow(viewer));
  const int gap = MulDiv(14, dpi > 0 ? dpi : 96, 96);
  const int cw = desktop.right - desktop.left, ch = desktop.bottom - desktop.top;
  RECT r{desktop.right + gap, desktop.top, desktop.right + gap + cw, desktop.bottom};
  if (r.right > client.right) r = RECT{desktop.left, desktop.bottom + gap, desktop.right, desktop.bottom + gap + ch};
  return r;
}

void click_viewer(HWND viewer, const RECT& card) {
  const LPARAM at = MAKELPARAM((card.left + card.right) / 2, (card.top + card.bottom) / 2);
  PostMessageW(viewer, WM_MOUSEMOVE, 0, at);
  PostMessageW(viewer, WM_LBUTTONDOWN, MK_LBUTTON, at);
  chain_pump(80);
  PostMessageW(viewer, WM_LBUTTONUP, 0, at);
}

// The newest "window-list ... firstTitle=<title>" of this viewer.
std::string first_listed_title(const std::string& viewerLines) {
  const size_t at = viewerLines.rfind("firstTitle=");
  if (at == std::string::npos) return {};
  std::string t = viewerLines.substr(at + 11);
  // The title runs to the first field the log appends after it.
  for (const char* field : {" eventQpcUs=", " viewerSession="}) {
    const size_t end = t.find(field);
    if (end != std::string::npos) t.resize(end);
  }
  while (!t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ')) t.pop_back();
  return t;
}

}  // namespace


namespace {

// Waits for the shell's next viewer (not `previous`) and returns its pid.
DWORD next_viewer(DWORD previous) {
  DWORD pid = 0;
  chain_wait([&] {
    for (const auto& e : gViewerByHost)
      if (e.second.pid != previous) pid = e.second.pid;
    return pid != 0;
  }, 20000);
  return pid;
}

void run_r3_scenario(const std::string& scenario, DWORD pid1, HWND viewer1, const Shot& pickerShot,
                     const std::wstring& shots, const std::wstring& wedgeFile, const std::wstring& presentFailFile,
                     const std::string& bTitle) {
  HWND toolbar = nullptr;
  chain_wait([&] { return (toolbar = toolbar_of_pid(pid1)) != nullptr; }, 10000);
  require(toolbar != nullptr, "the viewer's session toolbar window exists");
  int ty = 0;
  const std::map<int, int> buttons = sweep_toolbar(toolbar, pid1, &ty);
  std::string ids;
  for (const auto& [id, x] : buttons) ids += std::to_string(id) + "@" + std::to_string(x) + " ";
  std::printf("      (toolbar buttons found by the sweep: %s)\n", ids.c_str());

  if (scenario == "nomonitors") {
    // r4: a host that does not advertise the monitor messages (test seam "nomonitors", an older host): the
    // shipped viewer must never ask it for a screen list, and so shows no screen switch.
    chain_pump(3000);
    const std::string v1 = lines_of_pid(viewer_log_text(), pid1);
    check(v1.find("monitor-list requested") == std::string::npos, "r4: no screen-list request to a host without the monitor messages");
    check(!buttons.count(3), "r4: ...and no screen switch on the toolbar");
    return;
  }
  if (scenario == "monitor") {
    const std::string v1 = lines_of_pid(viewer_log_text(), pid1);
    std::printf("      (screen-list requests by this viewer: %zu)\n", count_of(v1, "monitor-list requested"));
    check(count_of(v1, "monitor-list requested") == 1, "r4: the SHIPPED viewer asked the host for its screen list, once");
    check(v1.find("[viewer-restore] target=monitor") == std::string::npos,
          "r4: the list alone selected nothing and did not change what is reported (still the desktop)");
    if (!buttons.count(3)) {
      check(false, "E2: the toolbar offers a screen switch while the host lists two screens",
            "FOUND: no screen-switch button (id 3) -- the viewer has no screen list");
      return;
    }
    check(true, "E2: the toolbar offers a screen switch (host lists two screens)");
    click_toolbar(toolbar, buttons.at(3), ty);
    HWND menu = nullptr;
    chain_wait([&] { return (menu = menu_of_pid(pid1)) != nullptr; }, 5000);
    require(menu != nullptr, "E2: the toolbar's screen menu opened");
    for (int key : {VK_DOWN, VK_DOWN, VK_RETURN}) {
      PostMessageW(menu, WM_KEYDOWN, key, 0);
      chain_pump(150);
    }
    require(chain_wait([&] { return lines_of_pid(viewer_log_text(), pid1).find("[viewer-restore] target=monitor") !=
                                    std::string::npos; }, 15000),
            "E2: the second screen picked through the toolbar -> \"monitor\" reported to the shell");
    { std::ofstream(wedgeFile) << "wedge\n"; }
    require(chain_wait([&] { return shell_log_text().find("restore=monitor") != std::string::npos; }, 60000),
            "E2: the session died; the shell's reconnect carries restore=monitor");
    const DWORD pid2 = next_viewer(pid1);
    require(pid2 != 0, "E2: the shell started a new viewer on its own");
    // Either outcome is waited for and named: the picker with its reason (right), or a restore of the desktop
    // (wrong -- what the MONITOR_AS_DESKTOP mutant does). A viewer that dies or never decides is INVALID.
    std::string outcome;
    chain_wait([&] {
      const std::string v = lines_of_pid(viewer_log_text(), pid2);
      if (v.find("result=picker reason=monitor_not_restored") != std::string::npos) outcome = "picker";
      else if (v.find("[restore] requested") != std::string::npos || v.find("result=restored") != std::string::npos)
        outcome = "restored";
      return !outcome.empty();
    }, 60000);
    if (outcome.empty()) {
      std::printf("chain: INVALID the new viewer neither restored nor showed the picker reason (exited or stuck)\n");
      std::fflush(stdout);
      TerminateProcess(GetCurrentProcess(), 3);
    }
    require(outcome == "picker", "E2: the new viewer did NOT restore it: the picker, reason monitor_not_restored",
            outcome == "restored" ? "WRONG: it restored the desktop on its own for an explicitly chosen screen" : outcome);
    chain_pump(4000);
    const std::string v2 = lines_of_pid(viewer_log_text(), pid2);
    check(v2.find("[restore] requested") == std::string::npos && v2.find("select_request") == std::string::npos &&
              v2.find("result=restored") == std::string::npos,
          "E2: ...and selected nothing on its own (no restore request, no select, no restored)");
    HWND viewer2 = window_of_pid(pid2);
    const Shot pick2 = screenshot(viewer2, shots + L"\\E2-1-picker-with-reason.bmp");
    check(pick2.ok && pick2.pickerColorShare > 0.15, "E2: screenshot: the reconnected window is the picker",
          shot_text(pick2));
    RECT card{};
    require(desktop_card(v2, &card), "E2: the picker's desktop card is known");
    click_viewer(viewer2, card);
    require(chain_wait([&] { return lines_of_pid(viewer_log_text(), pid2).find("[viewer-restore] target=desktop") !=
                                    std::string::npos; }, 30000),
            "E2: a MANUAL pick of the desktop card is presented and reported");
    chain_pump(1500);
    const Shot s2 = screenshot(viewer2, shots + L"\\E2-2-after-manual-pick.bmp");
    check(s2.ok && s2.pickerColorShare < 0.05, "E2: screenshot: the stream after the manual pick", shot_text(s2));
    return;
  }

  // presentfail / presentok
  const bool fail = scenario == "presentfail";
  require(buttons.count(1) != 0, "E3: the toolbar's 대상 선택 is there");
  const size_t before1 = lines_of_pid(viewer_log_text(), pid1).size();
  click_toolbar(toolbar, buttons.at(1), ty);
  RECT card{};
  require(chain_wait([&] {
            const std::string fresh = lines_of_pid(viewer_log_text(), pid1).substr(before1);
            return desktop_card(fresh, &card);
          }, 20000),
          "E3: back on the picker through the toolbar (a fresh picker paint)");
  // The picker shows the newest window list this viewer has (a reopened picker does not fetch a new one).
  const std::string first = first_listed_title(lines_of_pid(viewer_log_text(), pid1));
  if (first != bTitle) {
    // Never click a window of this PC that is not the fixture's.
    std::printf("chain: INVALID first listed window is \"%s\", not the fixture window \"%s\"\n", first.c_str(),
                bTitle.c_str());
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 3);
  }
  check(true, "E3: the first listed window is the fixture window B", first);
  RECT client{};
  GetClientRect(viewer1, &client);
  const RECT bCard = window_card(card, viewer1, client);
  chain_pump(800);  // the picker guard: shown for at least 300 ms before a pick counts
  if (fail) { std::ofstream(presentFailFile) << "fail\n"; }
  const size_t reportsBefore = count_of(lines_of_pid(viewer_log_text(), pid1), "[viewer-restore] target=");
  const size_t revealsBefore = count_of(lines_of_pid(viewer_log_text(), pid1), "[restore] revealed gen=");
  click_viewer(viewer1, bCard);
  require(chain_wait([&] { return count_of(lines_of_pid(viewer_log_text(), pid1), "[restore] revealed gen=") > revealsBefore; },
                     30000),
          "E3: B was confirmed by the host and its first frame decoded (revealed)");
  if (fail) {
    require(chain_wait([&] { return shell_log_text().find("viewer exit operation=") != std::string::npos; }, 30000),
            "E3: B's presents failed; the viewer gave up on its own");
    DeleteFileW(presentFailFile.c_str());  // the reconnected viewer presents normally
    const std::string v1 = lines_of_pid(viewer_log_text(), pid1);
    check(count_of(v1, "[viewer-restore] target=") == reportsBefore,
          "E3: B was NOT reported (the shell still holds A, the desktop)",
          "reports before/after: " + std::to_string(reportsBefore) + "/" + std::to_string(count_of(v1, "[viewer-restore] target=")));
    const std::string sl = shell_log_text();
    const size_t at = sl.rfind("viewer exit operation=");
    std::printf("      (%s)\n", sl.substr(at, sl.find('\n', at) - at).c_str());
    check(sl.find("restore=desktop", at) != std::string::npos, "E3: the reconnect carries restore=desktop (A)");
    const DWORD pid2 = next_viewer(pid1);
    require(pid2 != 0, "E3: the shell started a new viewer on its own");
    require(chain_wait([&] { return lines_of_pid(viewer_log_text(), pid2).find("result=restored target=desktop") !=
                                    std::string::npos; }, 60000),
            "E3: the new viewer went back to A (the desktop) without a pick");
    chain_pump(1500);
    const Shot s = screenshot(window_of_pid(pid2), shots + L"\\E3-fail-restored-A.bmp");
    check(s.ok && s.pickerColorShare < 0.05, "E3: screenshot: the desktop stream again", shot_text(s));
  } else {
    require(chain_wait([&] { return lines_of_pid(viewer_log_text(), pid1).find("[viewer-restore] target=window") !=
                                    std::string::npos; }, 30000),
            "E3 control: B presented -> \"window\" reported (the shell now holds B)");
    chain_pump(1500);
    // B is a window: it is captured through WGC, so the DXGI wedge cannot end this session. It is ended the
    // same way as in presentfail -- presents failing from now on -- AFTER B was presented and reported.
    { std::ofstream(presentFailFile) << "fail\n"; }
    require(chain_wait([&] { return shell_log_text().find("viewer exit operation=") != std::string::npos; }, 30000),
            "E3 control: the session ends (presents failing after B was shown)");
    DeleteFileW(presentFailFile.c_str());
    require(chain_wait([&] { return shell_log_text().find("restore=window") != std::string::npos; }, 60000),
            "E3 control: the reconnect carries restore=window (the shell now holds B)");
    const DWORD pid2 = next_viewer(pid1);
    require(pid2 != 0, "E3 control: a new viewer");
    require(chain_wait([&] { return lines_of_pid(viewer_log_text(), pid2).find("result=picker reason=window_not_restored") !=
                                    std::string::npos; }, 60000),
            "E3 control: the new viewer shows the picker for B (a window is not restored automatically)");
  }
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
  const std::string scenario = envs("GNLINK_CHAIN_SCENARIO").empty() ? "desktop" : envs("GNLINK_CHAIN_SCENARIO");
  if (!envs("GNLINK_CHAIN_VIEWER_EXE").empty()) gShellTestViewerExe = widen(envs("GNLINK_CHAIN_VIEWER_EXE"));
  const std::wstring presentFailFile = widen(envs("GNLINK_VIEWER_TEST_PRESENT_FAIL_FILE"));
  const std::string bTitle = envs("GNLINK_CHAIN_B_TITLE");
  std::printf("      (scenario: %s)\n", scenario.c_str());

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
            "the desktop card pressed: confirmed by the host, its frame PRESENTED, reported to the shell");
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

    if (scenario != "desktop") {
      run_r3_scenario(scenario, pid1, viewer1, pickerShot, shots, wedgeFile, presentFailFile, bTitle);
      throw std::runtime_error("__r3_done__");
    }

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
    if (std::string(e.what()) != "__r3_done__") {
      std::printf("chain: FAIL at \"%s\"\n", e.what());
      result = 1;
    }
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
