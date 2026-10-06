// Paste on demand (t-y4wj64jw), end to end.
//
// What the user does: copies something on this PC -- the remote PC's clipboard does not change.
// Presses Ctrl+V (or Shift+Insert) in the GNLink window -- what was copied goes to the remote PC, and
// only once the remote PC says it is ON ITS CLIPBOARD does the paste key go there. A failure sends no
// key and the bar says why, with Retry.
//
// The chain, nothing in it written by this test but the copies and the key messages:
//
//   a copy on the station clipboard -> the product's WndProc (WM_CLIPBOARDUPDATE: nothing sent)
//   key messages -> the product's WndProc (on_key_message: hotkeys, paste_intercept, forwarding)
//   -> the gate, the snapshot read on this thread -> ControlClient::pump_paste (80) / the image and
//   file clients -> a REAL GNLinkStream (its hub writes its clipboard and answers 81; its image
//   receiver publishes; its file helper publishes) -> the answer posted back (kMsgPasteResult) ->
//   the rebuilt key chord -> the input queue -> the worker -> the host's injection -> PostMessage
//   into a window of THIS process (the host is confined to it: --input-target-pid + title), which
//   records each key and, the instant the paste key lands, what the clipboard holds and who put it.
//
// What differs from the shipped viewer, said once:
//   * The whole test runs on a PRIVATE window station (a child of this process), so neither this
//     PC's nor anybody's real clipboard is read or written. Unnamed private stations are one per
//     logon session, so the viewer and the host SHARE that station's clipboard. "The remote PC got
//     the snapshot before the key" is therefore shown by the clipboard OWNER and SEQUENCE at the
//     moment the key lands (the host's process, a sequence newer than the gesture), not by the
//     content alone, which the viewer side already had.
//   * Because of that sharing: the viewer's incoming host text (kMsgApplyClipboard) is held back
//     from the window procedure and counted (the host would otherwise echo every local copy back),
//     except where a case delivers it on purpose; and the viewer's own R->P file helper is not
//     started (a CF_HDROP copy here would otherwise come back as a remote copy of files).
//   * Modifiers are set with SetKeyboardState (the product reads GetKeyState); key messages are
//     sent to the window, not typed -- no real input reaches any desktop.
//   * The host-IME (physical scan-code) path cannot be observed at the host: it injects scan codes
//     with SendInput behind a capture-focus gate. That case counts what the viewer SENT instead.
//   * --legacy-host runs the same viewer against the same host with REMOTE60_PASTE_ON_DEMAND=0: the
//     host withholds kCaptureFlagPasteOnDemandV1, which is what an older host looks like.
//
//   remote60_viewer_paste_e2e_test --out <dir>      (needs REMOTE60_ALLOW_HOST_E2E=1)

#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <shlobj.h>  // DROPFILES
#include <tlhelp32.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "clip_image_wic.hpp"
#include "control_resume.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "viewer_clip_image_wiring.hpp"
#include "viewer_clip_transfer_bar.hpp"
#include "viewer_constants.hpp"
#include "viewer_control_client.hpp"
#include "viewer_control_resume.hpp"
#include "viewer_paste_gate.hpp"
#include "viewer_paste_ui.hpp"
#include "viewer_state.hpp"
#include "viewer_udp_session.hpp"
#include "viewer_window_proc.hpp"
#include "test_scratch_dir.hpp"
#include "e2e_station_lock.hpp"

namespace ts = remote60::native_poc::test_support;

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;
using remote60::native_poc::viewer::ViewerState;

namespace remote60::native_poc {
bool macro_window_visible() { return false; }
void macro_window_toggle(HINSTANCE, HWND, const MacroWindowHooks&) {}
}  // namespace remote60::native_poc

namespace {

std::string narrow(const std::wstring& w) {
  if (w.empty()) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
std::string narrow16(const std::u16string& u) { return narrow(std::wstring(u.begin(), u.end())); }

// ------------------------------------------------------------------ the station clipboard ("the user")
bool station_open() {
  for (int i = 0; i < 50; ++i) {
    if (OpenClipboard(nullptr)) return true;
    Sleep(20);
  }
  return false;
}
bool station_put(UINT format, const std::vector<uint8_t>& bytes) {
  if (!station_open()) return false;
  EmptyClipboard();
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
  bool ok = g != nullptr;
  if (ok) {
    std::memcpy(GlobalLock(g), bytes.data(), bytes.size());
    GlobalUnlock(g);
    ok = SetClipboardData(format, g) != nullptr;
    if (!ok) GlobalFree(g);
  }
  CloseClipboard();
  return ok;
}
bool put_text(const std::u16string& s) {
  std::vector<uint8_t> b((s.size() + 1) * 2, 0);
  std::memcpy(b.data(), s.data(), s.size() * 2);
  return station_put(CF_UNICODETEXT, b);
}
bool put_files(const std::vector<std::wstring>& paths) {
  std::vector<uint8_t> b(sizeof(DROPFILES));
  DROPFILES df{};
  df.pFiles = sizeof(DROPFILES);
  df.fWide = TRUE;
  std::memcpy(b.data(), &df, sizeof(df));
  for (const std::wstring& p : paths) {
    const size_t at = b.size();
    b.resize(at + (p.size() + 1) * 2, 0);
    std::memcpy(b.data() + at, p.c_str(), p.size() * 2);
  }
  b.resize(b.size() + 2, 0);
  return station_put(CF_HDROP, b);
}
std::vector<uint8_t> noise_dibv5(uint32_t w, uint32_t h, uint32_t seed) {
  std::vector<uint8_t> bytes(sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4);
  BITMAPV5HEADER bh{};
  bh.bV5Size = sizeof(bh);
  bh.bV5Width = static_cast<LONG>(w);
  bh.bV5Height = static_cast<LONG>(h);
  bh.bV5Planes = 1;
  bh.bV5BitCount = 32;
  bh.bV5Compression = BI_BITFIELDS;
  bh.bV5RedMask = 0x00FF0000;
  bh.bV5GreenMask = 0x0000FF00;
  bh.bV5BlueMask = 0x000000FF;
  bh.bV5AlphaMask = 0xFF000000;
  bh.bV5CSType = LCS_sRGB;
  std::memcpy(bytes.data(), &bh, sizeof(bh));
  uint32_t x = seed;
  for (size_t o = sizeof(bh); o + 4 <= bytes.size(); o += 4) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const uint32_t px = x | 0xFF000000u;
    std::memcpy(bytes.data() + o, &px, 4);
  }
  return bytes;
}

// ------------------------------------------------------------------ the window the host types into
struct KeyEvent {
  UINT msg = 0;
  uint32_t vk = 0;
  // Sampled when the paste key (V / Insert) goes down -- that is, after the host decided to send it.
  bool sampled = false;
  DWORD clipSeq = 0;
  DWORD ownerPid = 0;
  std::u16string text;
  bool hasDib = false;
  bool hasVirtualFiles = false;
};

class PasteTarget {
 public:
  std::wstring title;
  bool Start() {
    thread_ = std::thread([this] { Run(); });
    return wait_until([this] { return ready_.load(); }, 5000) && hwnd_ != nullptr;
  }
  void Stop() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
  }
  std::vector<KeyEvent> Events() const {
    std::lock_guard<std::mutex> lock(mu_);
    return events_;
  }
  size_t Count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return events_.size();
  }
  /** Holds the station clipboard open on the target's thread for `ms` (a busy clipboard). */
  void HoldClipboard(DWORD ms) { PostMessageW(hwnd_, WM_APP + 1, ms, 0); }
  bool holding() const { return holding_.load(); }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PasteTarget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)) {
      KeyEvent e;
      e.msg = msg;
      e.vk = static_cast<uint32_t>(wp & 0xff);
      if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (e.vk == 'V' || e.vk == VK_INSERT)) self->Sample(hwnd, &e);
      std::lock_guard<std::mutex> lock(self->mu_);
      self->events_.push_back(std::move(e));
      return 0;
    }
    if (self && msg == WM_APP + 1) {
      if (station_open()) {
        self->holding_.store(true);
        Sleep(static_cast<DWORD>(wp));
        CloseClipboard();
        self->holding_.store(false);
      }
      return 0;
    }
    if (msg == WM_TIMER && self) {
      ++self->frame_;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    if (msg == WM_PAINT && self) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT c{};
      GetClientRect(hwnd, &c);
      HBRUSH b = CreateSolidBrush((self->frame_ & 1) ? RGB(20, 30, 40) : RGB(40, 30, 20));
      FillRect(dc, &c, b);
      DeleteObject(b);
      EndPaint(hwnd, &ps);
      return 0;
    }
    if (msg == WM_CLOSE) {
      DestroyWindow(hwnd);
      return 0;
    }
    if (msg == WM_DESTROY) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  void Sample(HWND hwnd, KeyEvent* e) {
    static const UINT kVirtualFiles = RegisterClipboardFormatW(L"FileGroupDescriptorW");
    e->sampled = true;
    e->clipSeq = GetClipboardSequenceNumber();
    HWND owner = GetClipboardOwner();
    if (owner) GetWindowThreadProcessId(owner, &e->ownerPid);
    e->hasDib = IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5);
    e->hasVirtualFiles = kVirtualFiles && IsClipboardFormatAvailable(kVirtualFiles);
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
      for (int i = 0; i < 50 && !OpenClipboard(hwnd); ++i) Sleep(10);
      if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* t = static_cast<const wchar_t*>(GlobalLock(h))) {
          const std::wstring w(t);
          e->text.assign(w.begin(), w.end());
          GlobalUnlock(h);
        }
      }
      CloseClipboard();
    }
  }
  void Run() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60PasteTarget";
    RegisterClassExW(&wc);
    title = e2e_unique_window_title(L"remote60 paste target");
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                            0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      SetTimer(hwnd_, 1, 50, nullptr);
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    hwnd_ = nullptr;
  }

  mutable std::mutex mu_;
  std::vector<KeyEvent> events_;
  uint64_t frame_ = 0;
  HWND hwnd_ = nullptr;
  std::atomic<bool> ready_{false};
  std::atomic<bool> holding_{false};
  std::thread thread_;
};

int downs_of(const std::vector<KeyEvent>& ev, size_t from, uint32_t vk) {
  int n = 0;
  for (size_t i = from; i < ev.size(); ++i) n += (ev[i].vk == vk && (ev[i].msg == WM_KEYDOWN || ev[i].msg == WM_SYSKEYDOWN));
  return n;
}
int ups_of(const std::vector<KeyEvent>& ev, size_t from, uint32_t vk) {
  int n = 0;
  for (size_t i = from; i < ev.size(); ++i) n += (ev[i].vk == vk && (ev[i].msg == WM_KEYUP || ev[i].msg == WM_SYSKEYUP));
  return n;
}
/** The first down of `vk` at or after `from` (npos when none). */
size_t first_down(const std::vector<KeyEvent>& ev, size_t from, uint32_t vk, int nth = 1) {
  for (size_t i = from; i < ev.size(); ++i) {
    if (ev[i].vk == vk && (ev[i].msg == WM_KEYDOWN || ev[i].msg == WM_SYSKEYDOWN) && --nth == 0) return i;
  }
  return std::string::npos;
}
/** Whether `mod` is held on the target just before index `at` (its last edge before it is a down). */
bool held_before(const std::vector<KeyEvent>& ev, size_t from, size_t at, uint32_t mod) {
  bool held = false;
  for (size_t i = from; i < at && i < ev.size(); ++i) {
    if (ev[i].vk != mod) continue;
    held = ev[i].msg == WM_KEYDOWN || ev[i].msg == WM_SYSKEYDOWN;
  }
  return held;
}
std::string trace(const std::vector<KeyEvent>& ev, size_t from) {
  std::string s;
  for (size_t i = from; i < ev.size(); ++i) {
    const bool down = ev[i].msg == WM_KEYDOWN || ev[i].msg == WM_SYSKEYDOWN;
    char b[16];
    std::snprintf(b, sizeof(b), "%02X%s ", ev[i].vk, down ? "v" : "^");
    s += b;
  }
  return s;
}

LPARAM key_lp(UINT vk, bool up, bool repeat = false) {
  UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  bool ext = false;
  if (vk == VK_INSERT) {
    scan = 0x52;
    ext = true;
  }
  LPARAM lp = 1 | (static_cast<LPARAM>(scan & 0xff) << 16);
  if (ext) lp |= (1ll << 24);
  if (up) lp |= (1ll << 30) | (1ll << 31);
  else if (repeat) lp |= (1ll << 30);
  return lp;
}

void set_mods(bool ctrl, bool shift) {
  BYTE ks[256]{};
  GetKeyboardState(ks);
  ks[VK_CONTROL] = ks[VK_LCONTROL] = ctrl ? 0x80 : 0;
  ks[VK_SHIFT] = ks[VK_LSHIFT] = shift ? 0x80 : 0;
  // Every other modifier explicitly up: a held Alt / Win / right-hand key would make it no paste.
  for (const int vk : {VK_RCONTROL, VK_RSHIFT, VK_MENU, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN}) ks[vk] = 0;
  SetKeyboardState(ks);
}

bool set_process_suspended(DWORD pid, bool suspend) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  int n = 0;
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID != pid) continue;
      if (HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID)) {
        if (suspend) SuspendThread(t);
        else ResumeThread(t);
        CloseHandle(t);
        ++n;
      }
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  return n > 0;
}

std::wstring process_path(DWORD pid) {
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return std::wstring();
  wchar_t buf[MAX_PATH * 2] = L"";
  DWORD len = MAX_PATH * 2;
  std::wstring out;
  if (QueryFullProcessImageNameW(p, 0, buf, &len)) out.assign(buf, len);
  CloseHandle(p);
  return out;
}

/** The viewer window and the bar as one image (PrintWindow of each: nothing else can leak in). */
bool save_windows_png(HWND viewerWnd, HWND bar, const std::wstring& path, std::string* detail) {
  RECT vr{};
  GetWindowRect(viewerWnd, &vr);
  const int w = vr.right - vr.left;
  const int h = vr.bottom - vr.top;
  if (w <= 0 || h <= 0) return false;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, dib);
  const BOOL okViewer = PrintWindow(viewerWnd, mem, PW_RENDERFULLCONTENT);
  BOOL okBar = TRUE;
  bool barDrawn = false;
  if (bar && IsWindowVisible(bar)) {
    RECT br{};
    GetWindowRect(bar, &br);
    const int bw = br.right - br.left;
    const int bh = br.bottom - br.top;
    HDC barDc = CreateCompatibleDC(screen);
    HBITMAP barBmp = CreateCompatibleBitmap(screen, bw, bh);
    HGDIOBJ oldBar = SelectObject(barDc, barBmp);
    okBar = PrintWindow(bar, barDc, 0);
    BitBlt(mem, br.left - vr.left, br.top - vr.top, bw, bh, barDc, 0, 0, SRCCOPY);
    SelectObject(barDc, oldBar);
    DeleteObject(barBmp);
    DeleteDC(barDc);
    barDrawn = true;
  }
  GdiFlush();
  std::vector<uint8_t> dibBytes(sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4);
  BITMAPV5HEADER v5{};
  v5.bV5Size = sizeof(v5);
  v5.bV5Width = w;
  v5.bV5Height = h;
  v5.bV5Planes = 1;
  v5.bV5BitCount = 32;
  v5.bV5Compression = BI_BITFIELDS;
  v5.bV5RedMask = 0x00FF0000;
  v5.bV5GreenMask = 0x0000FF00;
  v5.bV5BlueMask = 0x000000FF;
  v5.bV5AlphaMask = 0xFF000000;
  v5.bV5CSType = LCS_sRGB;
  std::memcpy(dibBytes.data(), &v5, sizeof(v5));
  const uint8_t* src = static_cast<const uint8_t*>(bits);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    uint32_t px;
    std::memcpy(&px, src + i * 4, 4);
    px |= 0xFF000000u;
    std::memcpy(dibBytes.data() + sizeof(v5) + i * 4, &px, 4);
  }
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  std::vector<uint8_t> png;
  uint32_t pw = 0, ph = 0;
  const bool enc = clip_dib_to_png(dibBytes.data(), dibBytes.size(), &png, &pw, &ph) == ClipWicResult::Ok;
  bool written = false;
  if (enc) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    written = out.good();
  }
  if (detail) {
    *detail = std::to_string(w) + "x" + std::to_string(h) + " bar=" + (barDrawn ? "drawn" : "not visible") +
              " png=" + std::to_string(png.size()) + " bytes";
  }
  return okViewer && okBar && written;
}

/** Local time as the host's log writes it: "MM-DD HH:MM:SS.mmm". */
std::string local_time_str() {
  SYSTEMTIME t{};
  GetLocalTime(&t);
  char b[32];
  std::snprintf(b, sizeof(b), "%02u-%02u %02u:%02u:%02u.%03u", t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds);
  return b;
}

int count_lines(const std::wstring& path, const std::string& needle) {
  std::ifstream in(path, std::ios::binary);
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    if (line.find(needle) != std::string::npos) ++n;
  }
  return n;
}

}  // namespace

// ------------------------------------------------------------------ the child: everything, on a private station
int run_paste_child(const std::wstring& resultFile, const std::wstring& outDir, bool legacy) {
  FILE* out = _wfreopen(resultFile.c_str(), L"w", stdout);
  if (!out) return 3;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  const char* tag = legacy ? "legacy" : "paste";
  const uint16_t port = e2e_pick_free_udp_port();
  check("a free UDP port for the host", port != 0, std::to_string(port));

  const std::wstring scratch = ts::make_scratch_dir(legacy ? L"paste_legacy" : L"paste");
  check("a scratch directory inside the repository", !scratch.empty(), ts::scratch_root_problem());
  if (scratch.empty() || port == 0) return 1;
  const std::wstring dir = scratch + L"\\";
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  const bool staged = CopyFileW((myDir + L"GNLinkStreamClipSink.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
                      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE) &&
                      CopyFileW((myDir + L"GNLinkClipHelper.exe").c_str(), (dir + L"GNLinkClipHelper.exe").c_str(), FALSE);
  check("the product host, a never-answering helper and the clipboard helper are staged", staged);

  // The station's clipboard starts empty: whatever another test left is not this run's copy.
  if (station_open()) {
    EmptyClipboard();
    CloseClipboard();
  }

  PasteTarget target;
  check("a window of this process is up for the host to type into", target.Start());
  std::cout << "      diag: target visible=" << IsWindowVisible(target.hwnd()) << "\n";

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  const std::wstring hostLogPath = dir + L"host.log";
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_FRAME_GATING_DISABLE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATIC_SCENE_FPS", L"15");
    SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", nullptr);  // on: the host's clipboard hub runs
    SetEnvironmentVariableW(L"REMOTE60_FILE_COPY_TEST_HELPER_AS_SELF", L"1");
    CreateDirectoryW((dir + L"sink").c_str(), nullptr);
    SetEnvironmentVariableW(L"REMOTE60_CLIP_IMAGE_TEST_SINK_DIR", (dir + L"sink").c_str());
    SetEnvironmentVariableW(L"REMOTE60_PASTE_ON_DEMAND", legacy ? L"0" : nullptr);
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" + L" --bind-address 127.0.0.1 --bind-port " +
                       std::to_wstring(port) + L" --fps 30 --seconds 300 --enable-input-injection" +
                       L" --input-injection-mode background_message --input-target-pid " +
                       std::to_wstring(GetCurrentProcessId()) + L" --input-target-title \"" + target.title + L"\"" +
                       e2e_capture_window_args(target.title);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    // lpDesktop left empty: the host inherits THIS process's private station, and its clipboard.
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, dir, &isoWhy) && e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the host is isolated from the user's files", isoOk, isoWhy);
    launched = isoOk && CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                                       dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);
  const DWORD hostPid = hostPi.dwProcessId;

  ViewerState ctx;
  SOCKET sock = INVALID_SOCKET;
  std::thread worker;
  std::thread ingress;
  std::atomic<bool> ingressStop{false};
  uint32_t nextResumeId = 0x7600;
  uint64_t generation = 1;
  HWND viewerWindow = nullptr;
  bool controlUp = false;
  if (launched) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;  // the product's
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60PasteViewer";
    RegisterClassExW(&wc);
    viewerWindow = CreateWindowExW(0, wc.lpszClassName, L"GNLink (paste e2e)", WS_OVERLAPPEDWINDOW, 40, 40, 900, 560,
                                   nullptr, nullptr, wc.hInstance, &ctx);
    check("the product's window procedure owns a window", viewerWindow != nullptr);
    if (viewerWindow) ShowWindow(viewerWindow, SW_SHOW);
    std::cout << "      diag: viewer visible=" << IsWindowVisible(viewerWindow) << " iconic=" << IsIconic(viewerWindow)
              << " style=0x" << std::hex << GetWindowLongPtrW(viewerWindow, GWL_STYLE) << std::dec << "\n";
    // As create_window does: this window hears every change of the clipboard it lives with.
    AddClipboardFormatListener(viewerWindow);
    ctx.session.hwnd = viewerWindow;
    ctx.session.running.store(true);
    ctx.session.inputEnabled.store(true);
    ctx.session.controlRequired = true;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(port);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 50;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 8 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    ctx.session.sock = sock;
    UdpHelloOptions hello = remote60::native_poc::viewer::viewer_udp_hello_options(std::string(), false);
    std::string helloError;
    uint32_t ackFeatures = 0;
    check("the viewer's Hello is answered", udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr), helloError);
    remote60::native_poc::viewer::viewer_apply_udp_hello_ack(ackFeatures, false, ctx.session);
    ctx.control.udpControl.Configure(
        [sock](const void* data, size_t len) { return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0; },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    ctx.control.overUdp.store(true, std::memory_order_release);
    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    getpeername(sock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    ViewerControlResume::Config cfg;
    cfg.decide.giveUpAfterUs = remote60::native_poc::viewer::SessionLivenessConfig{}.controlGoneWithVideoUs;
    ctx.control.resume.Configure(
        &ctx.control.udpControl,
        [sock](const void* data, size_t len) { return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0; },
        [](const std::string& line) { std::cout << "    " << line << "\n"; }, [&nextResumeId]() { return ++nextResumeId; },
        [&generation]() { return generation; }, cfg, (ackFeatures & kUdpFeatureControlResume) != 0, peer.sin_addr.s_addr,
        peer.sin_port);
    remote60::native_poc::viewer::start_clip_image_client(ctx, 1200);
    // The test difference (header): no R->P helper here -- on a shared station this PC's own CF_HDROP
    // copy would come back as the remote PC's files. Set before any pong, so nothing has used it.
    ctx.control.fileCopy.SetHelperLauncher(nullptr);
    remote60::native_poc::viewer::create_clip_transfer_bar(ctx);
    ingress = std::thread([&] {
      std::vector<uint8_t> buf(2048);
      while (!ingressStop.load()) {
        ctx.control.udpControl.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        if (ctx.session.bulkChannelNegotiated && ctx.control.fileCopy.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (ctx.session.bulkChannelNegotiated && ctx.control.clipImage.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (ctx.control.resume.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (ctx.control.udpControl.OnPacket(buf.data(), static_cast<size_t>(n))) continue;
        ctx.recvLive.lastPublishUs.store(qpc_now_us(), std::memory_order_relaxed);
        ctx.recvLive.lastDatagramUs.store(qpc_now_us(), std::memory_order_relaxed);
      }
    });
    static remote60::native_poc::viewer::Args args;
    args.inputLogEvery = 0;
    static remote60::native_poc::viewer::ControlClient control(ctx, args, /*startInPicker=*/false);
    ctx.control.scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
    ctx.control.connected.store(true, std::memory_order_relaxed);
    worker = std::thread([&] { control.Run(); });
  }

  int heldApply = 0;
  const auto pump_until = [&](const std::function<bool()>& done, int budgetMs) {
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
    for (;;) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == remote60::native_poc::viewer::kMsgApplyClipboard) {  // see the header
          delete reinterpret_cast<std::u16string*>(msg.lParam);
          ++heldApply;
          continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (done()) return true;
      if (GetTickCount() >= deadline) return done();
      Sleep(5);
    }
  };
  const auto idle = [&](int ms) { pump_until([] { return false; }, ms); };
  const auto key = [&](UINT msg, UINT vk, bool repeat = false) {
    const bool up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
    SendMessageW(viewerWindow, msg, vk, key_lp(vk, up, repeat));
  };
  // A paste exactly as typed: the modifier, the key, the key let go, the modifier let go.
  const auto ctrl_v = [&] {
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'V');
    key(WM_KEYUP, 'V');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
  };
  const auto text_sends = [&] {
    std::lock_guard<std::mutex> lock(ctx.control.clipboard.mu);
    return ctx.control.clipboard.nextSeq;
  };
  const auto bar_text = [&] { return clip_transfer_bar_text(clip_transfer_bar_current()); };
  std::string shot;

  if (launched) {
    controlUp = pump_until([&] { return ctx.control.lastRttUs.load() > 0 && ctx.control.clipboard.hostSupports.load(); }, 25000);
    check("control is up and the host has clipboard sync (Pong 0x100)", controlUp);
    if (controlUp) {
      const bool bitAsExpected = pump_until(
          [&] { return ctx.control.clipboard.hostPasteOnDemand.load() == !legacy; }, 5000);
      check(legacy ? "the legacy host does NOT advertise paste on demand (Pong 0x1000 clear)"
                   : "the host advertises paste on demand (Pong 0x1000)",
            bitAsExpected);
      if (!legacy) {
        pump_until([&] { return ctx.control.clipImage.Usable() && ctx.control.fileCopy.Usable(); }, 15000);
        check("images and files are usable too (bulk + 0x200 + 0x800)",
              ctx.control.clipImage.Usable() && ctx.control.fileCopy.Usable());
      }
      idle(500);  // the session-start message (kMsgPushClipboardNow) has run
    }
  }

  // What each case expects of the HOST, in its own log's order (evaluated at the end: the host's
  // stdout reaches its file only when it exits). Tokens: d / u = a key edge (input event 5 / 6),
  // m = a mouse edge, A = paste-apply written (result 0), F = paste-apply failed, I = an image
  // published, P = files published by the helper, R = a file offer refused, U = a copy-time text
  // update (51, never expected). The paste chord is "dduu"; the release of the modifier that reached
  // the host before the gesture is the "u" before the request.
  struct Expect {
    std::string name;
    std::string at;  // local time "MM-DD HH:MM:SS.mmm", the host log's format
    std::function<bool(const std::string&)> ok;
    std::string want;
  };
  std::vector<Expect> expects;
  const auto mark = [&](const std::string& name, const std::string& want, std::function<bool(const std::string&)> ok = nullptr) {
    Expect e;
    e.name = name;
    e.at = local_time_str();
    e.want = want;
    e.ok = ok ? ok : [want](const std::string& got) { return got == want; };
    expects.push_back(std::move(e));
  };
  const auto station_text = [&](DWORD* owner) {
    std::u16string t;
    *owner = 0;
    if (HWND o = GetClipboardOwner()) GetWindowThreadProcessId(o, owner);
    if (station_open()) {
      if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
          const std::wstring s(w);
          t.assign(s.begin(), s.end());
          GlobalUnlock(h);
        }
      }
      CloseClipboard();
    }
    return t;
  };
  // A paste's answer has been handled on this thread (its log line is the viewer's own).
  const auto answered = [&](uint32_t sendsBefore) {
    return pump_until([&] { return text_sends() > sendsBefore && !remote60::native_poc::viewer::paste_bar_view(ctx).active; }, 6000);
  };
  const auto view = [] { return clip_transfer_bar_current(); };

  if (controlUp && legacy) {
    std::cout << "\n--- legacy host: a copy here, Ctrl+V: no key, the bar says update ---\n";
    put_text(u"legacy local copy");
    idle(300);
    mark("legacy: Ctrl+V on this PC's copy -> Ctrl as typed, nothing written, no V", "du");
    const uint32_t s0 = text_sends();
    ctrl_v();
    idle(1500);
    check("nothing was sent to the remote clipboard", text_sends() == s0);
    const ClipBarView v = view();
    check("the bar says the remote GNLink must be updated", v.isPaste && v.pasteText.find(L"업데이트") != std::wstring::npos,
          narrow(v.pasteText));
    check("...with no Retry (it would fail the same way)", !v.pasteRetry);
    std::cout << "\n--- legacy host: a copy made on the remote PC (Ctrl+C there), then Ctrl+V: its own paste ---\n";
    mark("legacy: after Ctrl+C there, Ctrl+V goes at once as typed", "dduduu");
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'C');
    key(WM_KEYUP, 'C');
    key(WM_KEYDOWN, 'V');
    key(WM_KEYUP, 'V');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
    idle(1500);
    check("...and nothing was sent", text_sends() == s0);
    mark("legacy: end", "");
  }

  if (controlUp && !legacy) {
    // ---------------------------------------------------------------- A. a copy here sends nothing
    std::cout << "\n--- A. copies on this PC: text, an image, files -- nothing goes to the remote PC ---\n";
    const std::wstring filesDir = dir + L"files";
    CreateDirectoryW(filesDir.c_str(), nullptr);
    const std::wstring fileA = filesDir + L"\\paste a.txt";
    std::ofstream(fileA, std::ios::binary) << std::string(3000, 'a');
    const uint32_t a0 = text_sends();
    const auto ic0 = ctx.control.clipImage.GetCounters();
    const auto fc0 = ctx.control.fileCopy.GetCounters();
    mark("A: copies here (text, image, files) reach nothing on the host", "");
    check("a text copy is made here", put_text(u"copied here only"));
    idle(1600);
    check("an image copy is made here", station_put(CF_DIBV5, noise_dibv5(32, 32, 7)));
    idle(1600);
    check("a copy of files is made here", put_files({fileA}));
    idle(1600);
    check("no text went to the remote PC (no 51, no 80)", text_sends() == a0,
          std::to_string(a0) + " -> " + std::to_string(text_sends()));
    check("no image was offered", ctx.control.clipImage.GetCounters().submitted == ic0.submitted &&
                                     ctx.control.clipImage.GetCounters().offered == ic0.offered);
    check("no files were offered", ctx.control.fileCopy.GetCounters().offersSent == fc0.offersSent);

    // ---------------------------------------------------------------- B. text Ctrl+V
    std::cout << "\n--- B. text: Ctrl+V sends it, the key follows the host's answer ---\n";
    const std::u16string t1 = u"붙여넣기 on demand #1";
    put_text(t1);
    idle(300);
    mark("B: Ctrl+V -> Ctrl released, written, THEN the chord", "duAdduu");
    const uint32_t sB = text_sends();
    const DWORD seqB = GetClipboardSequenceNumber();
    const uint64_t sentB = ctx.session.inputEventsSent.load();
    ctrl_v();
    answered(sB);
    idle(400);
    DWORD ownerB = 0;
    const std::u16string atB = station_text(&ownerB);
    check("one text request went out", text_sends() == sB + 1);
    check("the remote clipboard holds what was copied, written by the host after the gesture",
          atB == t1 && ownerB == hostPid && GetClipboardSequenceNumber() > seqB,
          narrow16(atB) + " owner " + std::to_string(ownerB) + " host " + std::to_string(hostPid));
    check("six input edges went out: Ctrl down, its release, the four-edge chord",
          ctx.session.inputEventsSent.load() - sentB == 6, std::to_string(ctx.session.inputEventsSent.load() - sentB));
    check("the bar has nothing to say about a paste that worked", !remote60::native_poc::viewer::paste_bar_view(ctx).active);

    // ---------------------------------------------------------------- C. Shift+Insert
    std::cout << "\n--- C. text: Shift+Insert ---\n";
    const std::u16string t2 = u"shift insert paste";
    put_text(t2);
    idle(300);
    mark("C: Shift+Insert -> Shift released, written, then Shift+Insert", "duAdduu");
    const uint32_t sC = text_sends();
    set_mods(false, true);
    key(WM_KEYDOWN, VK_SHIFT);
    key(WM_KEYDOWN, VK_INSERT);
    key(WM_KEYUP, VK_INSERT);
    set_mods(false, false);
    key(WM_KEYUP, VK_SHIFT);
    answered(sC);
    idle(400);
    DWORD ownerC = 0;
    check("Shift+Insert: the host's clipboard holds the copied text", station_text(&ownerC) == t2 && ownerC == hostPid);

    // ---------------------------------------------------------------- D. the same copy again
    std::cout << "\n--- D. the same copy pasted again: sent and confirmed again, not skipped ---\n";
    mark("D: the same copy again -> written again, then the chord", "duAdduu");
    const uint32_t sD = text_sends();
    const DWORD seqD = GetClipboardSequenceNumber();
    ctrl_v();
    answered(sD);
    idle(400);
    check("the second paste is sent again (no 'already sent' shortcut)", text_sends() == sD + 1);
    check("...and the host wrote it again", GetClipboardSequenceNumber() > seqD);

    // ---------------------------------------------------------------- E. Ctrl kept held
    std::cout << "\n--- E. Ctrl kept held after the paste: Ctrl+A is still Ctrl+A ---\n";
    mark("E: Ctrl kept held -> chord, then Ctrl down again before A, up at the end", "duAdduudduu");
    const uint32_t sE = text_sends();
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'V');
    key(WM_KEYUP, 'V');
    answered(sE);
    key(WM_KEYDOWN, 'A');
    key(WM_KEYUP, 'A');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
    idle(600);

    // ---------------------------------------------------------------- F. fast keys while waiting
    std::cout << "\n--- F. V pressed twice, its repeat, and Ctrl+B typed while waiting ---\n";
    put_text(u"fast keys");
    idle(300);
    mark("F: two pastes in order, the repeat none, Ctrl+B after both (as Ctrl+B)", "duAdduuAdduudduu");
    const uint32_t sF = text_sends();
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'V');
    key(WM_KEYDOWN, 'V', /*repeat=*/true);  // auto-repeat: not another paste
    key(WM_KEYUP, 'V');
    key(WM_KEYDOWN, 'V');                   // a second, real press: waits behind the first
    key(WM_KEYUP, 'V');
    key(WM_KEYDOWN, 'B');                   // Ctrl+B, typed before any answer came
    key(WM_KEYUP, 'B');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
    pump_until([&] { return text_sends() == sF + 2 && !remote60::native_poc::viewer::paste_bar_view(ctx).active; }, 8000);
    idle(600);
    check("two text requests (the repeat is not a paste)", text_sends() == sF + 2);

    // ---------------------------------------------------------------- G/H. focus or target changes
    std::cout << "\n--- G. focus leaves while waiting: no key ---\n";
    mark("G: focus left -> Ctrl released, perhaps written, NO chord", "du[A]",
         [](const std::string& g) { return g == "du" || g == "duA"; });
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'V');
    SendMessageW(viewerWindow, WM_KILLFOCUS, 0, 0);  // before any answer can have been dispatched
    key(WM_KEYUP, 'V');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
    idle(1500);
    check("...and the bar says nothing (logged only)", !remote60::native_poc::viewer::paste_bar_view(ctx).active, narrow(bar_text()));

    std::cout << "\n--- H. the target changes while waiting: no key ---\n";
    mark("H: target changed -> written, NO chord", "duA");
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'V');
    ctx.sel.epoch.fetch_add(1);  // what a window / monitor selection does
    key(WM_KEYUP, 'V');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
    idle(1500);
    check("...and nothing shown", !remote60::native_poc::viewer::paste_bar_view(ctx).active);

    std::cout << "\n--- H2. the connection changes while waiting: no key ---\n";
    mark("H2: an answer from the previous connection -> written, NO chord", "duA");
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'V');
    ctx.control.clipboard.connGen.fetch_add(1);  // what a reconnect does (the first pong of a new session)
    key(WM_KEYUP, 'V');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
    idle(1500);
    check("...and nothing shown (logged only)", !remote60::native_poc::viewer::paste_bar_view(ctx).active);

    // ---------------------------------------------------------------- I. waiting: Esc, mouse, timeout, retry
    std::cout << "\n--- I1. waiting (host paused): the bar says so; Esc cancels ---\n";
    put_text(u"waiting text");
    idle(300);
    // The request had not left (the worker was still waiting on the paused host for the Ctrl edges),
    // so the cancel withdraws it: nothing is written at all, which is the better of the two outcomes.
    mark("I1: Esc while waiting -> the request withdrawn, NO write, NO chord, no Esc", "du");
    check("the host is paused (its threads suspended)", set_process_suspended(hostPid, true));
    ctrl_v();
    idle(300);
    ClipBarView w = view();
    check("while waiting the bar says the copy is being sent, with Cancel",
          w.isPaste && w.pasteText == L"붙여넣을 내용을 원격 PC에 보내는 중…" && w.pasteCancel && !w.pasteRetry, narrow(w.pasteText));
    key(WM_KEYDOWN, VK_ESCAPE);
    key(WM_KEYUP, VK_ESCAPE);
    idle(300);
    w = view();
    check("Esc: the bar says the paste was cancelled (no Retry)",
          w.isPaste && w.pasteText == L"붙여넣기를 취소했습니다" && !w.pasteRetry && !w.pasteCancel, narrow(w.pasteText));
    set_process_suspended(hostPid, false);
    idle(2500);  // the host answers now -- late, for a paste that is over

    std::cout << "\n--- I2. waiting: a mouse click cancels ---\n";
    // The click lands outside any video (none is decoded here), so it is not forwarded either.
    mark("I2: a click while waiting -> the request withdrawn, NO write, NO chord", "du");
    set_process_suspended(hostPid, true);
    ctrl_v();
    idle(200);
    SendMessageW(viewerWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, 200));
    SendMessageW(viewerWindow, WM_LBUTTONUP, 0, MAKELPARAM(200, 200));
    idle(200);
    w = view();
    check("a click: the bar says the paste was cancelled", w.isPaste && w.pasteText == L"붙여넣기를 취소했습니다", narrow(w.pasteText));
    set_process_suspended(hostPid, false);
    idle(2500);

    std::cout << "\n--- I3. no answer in time: no key, the reason, Retry ---\n";
    mark("I3: timeout -> the request withdrawn, NO chord; Retry -> written, THEN the chord", "duAdduu");
    set_process_suspended(hostPid, true);
    const uint64_t t0 = qpc_now_us();
    ctrl_v();
    pump_until([&] { return view().pasteText.find(L"시간 초과") != std::wstring::npos; }, 6000);
    const uint64_t timeoutMs = (qpc_now_us() - t0) / 1000;
    w = view();
    check("after the text deadline the bar says it timed out, the paste not run",
          w.pasteText.find(L"전송하지 못해 붙여넣기를 실행하지 않았습니다") == 0 && w.pasteText.find(L"시간 초과") != std::wstring::npos,
          narrow(w.pasteText) + " after " + std::to_string(timeoutMs) + " ms");
    check("...within the deadline's reach (3 s + a tick)", timeoutMs >= 2900 && timeoutMs <= 4000, std::to_string(timeoutMs));
    check("...with a Retry button", w.pasteRetry && !w.pasteCancel);
    set_process_suspended(hostPid, false);
    idle(2500);
    const uint32_t sR = text_sends();
    remote60::native_poc::viewer::paste_retry_from_bar(ctx);  // what the bar's Retry button calls
    answered(sR);
    idle(400);
    check("Retry sent the paste again", text_sends() == sR + 1);

    std::cout << "\n--- I5. an answer naming another paste arrives while one waits: ignored ---\n";
    put_text(u"the real one");
    idle(300);
    mark("I5: a stray answer is ignored; the real one -> written, THEN one chord", "duAdduu");
    set_process_suspended(hostPid, true);
    ctrl_v();
    idle(200);
    {
      // Posted to the product's handler exactly as the worker posts one -- for a request nobody made.
      auto* stray = new remote60::native_poc::viewer::PasteAnswer();
      stray->id = 0x5157A7A7A7A7A7ULL;
      stray->applied = true;
      stray->format = remote60::native_poc::viewer::PasteFormat::Text;
      PostMessageW(viewerWindow, remote60::native_poc::viewer::kMsgPasteResult, 0, reinterpret_cast<LPARAM>(stray));
    }
    idle(300);
    check("after a stray answer the paste is still waiting (no key, the bar still says sending)",
          view().isPaste && view().pasteText == L"붙여넣을 내용을 원격 PC에 보내는 중…", narrow(view().pasteText));
    const uint32_t s5 = text_sends();
    set_process_suspended(hostPid, false);
    answered(s5);
    idle(500);

    std::cout << "\n--- I4. the host cannot write its clipboard: no key, the reason ---\n";
    put_text(u"busy clipboard");
    idle(300);
    mark("I4: the host's clipboard is busy -> the write fails, NO chord", "duF");
    set_process_suspended(hostPid, true);
    ctrl_v();  // the snapshot is read now, while the clipboard is free
    target.HoldClipboard(1500);
    pump_until([&] { return target.holding(); }, 1000);
    set_process_suspended(hostPid, false);
    pump_until([&] { return view().pasteText.find(L"쓰지 못함") != std::wstring::npos; }, 4000);
    w = view();
    check("the bar says the remote clipboard could not be written, the paste not run",
          w.pasteText.find(L"원격 PC 클립보드에 쓰지 못함") != std::wstring::npos && w.pasteRetry, narrow(w.pasteText));
    pump_until([&] { return !target.holding(); }, 3000);
    idle(800);

    // ---------------------------------------------------------------- J. an image
    std::cout << "\n--- J. an image: Ctrl+V after the host published it ---\n";
    station_put(CF_DIBV5, noise_dibv5(96, 64, 0x5151u));
    idle(300);
    mark("J: an image -> published, THEN the chord", "duIdduu");
    const uint64_t pubJ = ctx.control.clipImage.GetCounters().published;
    ctrl_v();
    pump_until([&] { return ctx.control.clipImage.GetCounters().published > pubJ && !remote60::native_poc::viewer::paste_bar_view(ctx).active; }, 30000);
    idle(600);
    check("the image was published on the host", ctx.control.clipImage.GetCounters().published == pubJ + 1);

    // ---------------------------------------------------------------- K. files, and a helper restart
    std::cout << "\n--- K. files: Ctrl+V after the host's helper published them ---\n";
    put_files({fileA});
    idle(300);
    mark("K: files -> the helper published them, THEN the chord", "duPdduu");
    const uint64_t accK = ctx.control.fileCopy.GetCounters().offersAccepted;
    ctrl_v();
    pump_until([&] { return ctx.control.fileCopy.GetCounters().offersAccepted > accK && !remote60::native_poc::viewer::paste_bar_view(ctx).active; }, 15000);
    idle(600);
    static const UINT kVirtualFiles = RegisterClipboardFormatW(L"FileGroupDescriptorW");
    DWORD helperPid = 0;
    if (HWND o = GetClipboardOwner()) GetWindowThreadProcessId(o, &helperPid);
    const std::wstring helperPath = helperPid ? process_path(helperPid) : std::wstring();
    check("the host's clipboard now names the files (virtual files), put there by this run's helper",
          IsClipboardFormatAvailable(kVirtualFiles) && helperPid != 0 && helperPid != hostPid && e2e_path_is_under(helperPath, dir) &&
              helperPath.find(L"GNLinkClipHelper.exe") != std::wstring::npos,
          narrow(helperPath));
    std::cout << "\n--- K2. the host's helper goes away; the next paste starts a new one ---\n";
    bool killed = false;
    if (helperPid && e2e_path_is_under(helperPath, dir)) {
      if (HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, helperPid)) {
        killed = TerminateProcess(h, 9) && WaitForSingleObject(h, 5000) == WAIT_OBJECT_0;
        CloseHandle(h);
      }
    }
    check("this run's helper (identified by the clipboard it owned and its path) is stopped", killed);
    idle(1000);
    put_files({fileA});  // the same files copied again: on a shared station the helper's own publish was the last copy
    idle(300);
    mark("K2: after the helper died -> a new helper published them, THEN the chord", "duPdduu");
    const uint64_t accK2 = ctx.control.fileCopy.GetCounters().offersAccepted;
    ctrl_v();
    pump_until([&] { return ctx.control.fileCopy.GetCounters().offersAccepted > accK2 && !remote60::native_poc::viewer::paste_bar_view(ctx).active; }, 15000);
    idle(600);
    DWORD helper2 = 0;
    if (HWND o = GetClipboardOwner()) GetWindowThreadProcessId(o, &helper2);
    check("...a NEW helper owns the remote clipboard", helper2 != 0 && helper2 != helperPid && helper2 != hostPid,
          std::to_string(helper2) + " was " + std::to_string(helperPid));

    // ---------------------------------------------------------------- L. the remote PC's copy is the newest
    std::cout << "\n--- L. a copy made on the remote PC (Ctrl+C there): Ctrl+V pastes its own ---\n";
    put_text(u"this PC's older copy");
    idle(300);
    mark("L: Ctrl+C there, then Ctrl+V -> the keys as typed, nothing written", "dduduu");
    const uint32_t sL = text_sends();
    set_mods(true, false);
    key(WM_KEYDOWN, VK_CONTROL);
    key(WM_KEYDOWN, 'C');
    key(WM_KEYUP, 'C');
    key(WM_KEYDOWN, 'V');
    key(WM_KEYUP, 'V');
    set_mods(false, false);
    key(WM_KEYUP, VK_CONTROL);
    idle(1000);
    check("nothing was sent", text_sends() == sL);
    std::cout << "\n--- L2. the remote PC's text arriving here (R->P) is the newest copy too ---\n";
    put_text(u"this PC again");
    idle(300);
    SendMessageW(viewerWindow, remote60::native_poc::viewer::kMsgApplyClipboard, 0,
                 reinterpret_cast<LPARAM>(new std::u16string(u"text copied on the remote PC")));  // the product's apply
    idle(300);
    mark("L2: after R->P text, Ctrl+V -> the keys as typed, nothing written", "dduu");
    ctrl_v();
    idle(1000);
    check("nothing was sent", text_sends() == sL);

    // ---------------------------------------------------------------- M. the host-IME (physical) path
    std::cout << "\n--- M. host-IME mode: the same admission, the chord as scan codes ---\n";
    SendMessageW(viewerWindow, remote60::native_poc::viewer::kMsgHostImeActivate, 0, 0);
    put_text(u"physical path text");
    idle(300);
    mark("M: physical path -> written (the scan-code edges are not input events)", "A");
    // The worker logs every physical-key exchange it completes ("actionKind=12", PhysicalKey) to
    // this child's stdout, which is the result file: counted there.
    const auto physical_sent = [&] { return count_lines(resultFile, "actionKind=12 "); };
    const uint32_t sM = text_sends();
    const int physM = physical_sent();
    ctrl_v();
    pump_until([&] { return text_sends() == sM + 1 && physical_sent() - physM >= 6; }, 5000);
    idle(500);
    check("the text was sent and confirmed first", text_sends() == sM + 1);
    check("six physical-key edges went out: Ctrl down, its release, the chord",
          physical_sent() - physM == 6, std::to_string(physical_sent() - physM));
    SendMessageW(viewerWindow, remote60::native_poc::viewer::kMsgHostImeDeactivate, 0, 0);
    idle(300);
    mark("end", "");
  }

  // ------------------------------------------------------------------------------- teardown
  ctx.session.running.store(false);
  if (worker.joinable()) worker.join();
  ingressStop.store(true);
  if (ingress.joinable()) ingress.join();
  ctx.control.clipImage.Stop();
  ctx.control.fileCopy.Stop();
  if (sock != INVALID_SOCKET) closesocket(sock);
  if (viewerWindow) DestroyWindow(viewerWindow);
  target.Stop();
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  bool hostGone = true;
  if (hostPi.hProcess) {
    hostGone = WaitForSingleObject(hostPi.hProcess, 20000) == WAIT_OBJECT_0;
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  check("the host is gone when the job closes", hostGone);
  if (launched && controlUp) {
    // The host's own order of events, per case window (see Expect above).
    struct Tok {
      std::string at;
      char c;
    };
    std::vector<Tok> toks;
    {
      std::ifstream in(hostLogPath, std::ios::binary);
      std::string line;
      while (std::getline(in, line)) {
        if (line.size() < 19) continue;
        const std::string at = line.substr(0, 18);
        char c = 0;
        if (line.find("[input-timing] seq=") != std::string::npos) {
          if (line.find(" eventKind=5 ") != std::string::npos) c = 'd';
          else if (line.find(" eventKind=6 ") != std::string::npos) c = 'u';
          else if (line.find(" eventKind=2 ") != std::string::npos || line.find(" eventKind=3 ") != std::string::npos) c = 'm';
        } else if (line.find("[clipboard] paste-apply id=") != std::string::npos) {
          c = line.find(" result=0 ") != std::string::npos ? 'A' : 'F';
        } else if (line.find("[clipboard] update applied") != std::string::npos) {
          c = 'U';
        } else if (line.find("[clip-image] end state=published") != std::string::npos) {
          c = 'I';
        } else if (line.find("[file-copy] offer published") != std::string::npos) {
          c = 'P';
        } else if (line.find("[file-copy] offer refused") != std::string::npos) {
          c = 'R';
        }
        if (c) toks.push_back(Tok{at, c});
      }
    }
    for (size_t i = 0; i < expects.size(); ++i) {
      const Expect& e = expects[i];
      if (e.name == "end" || e.name == "legacy: end") break;
      const std::string to = i + 1 < expects.size() ? expects[i + 1].at : std::string("99");
      std::string got;
      for (const Tok& t : toks) {
        if (t.at >= e.at && t.at < to) got.push_back(t.c);
      }
      check("host order, " + e.name, e.ok(got), "host saw \"" + got + "\", wanted \"" + e.want + "\"");
    }
    const int applies = count_lines(hostLogPath, "[clipboard] paste-apply id=");
    const int failed = count_lines(hostLogPath, "[clipboard] paste-apply id=") - count_lines(hostLogPath, " result=0 stage=0");
    const int legacyUpdates = count_lines(hostLogPath, "[clipboard] update applied");
    std::cout << "      host: paste-apply lines " << applies << " (not applied " << failed << "), text v1 updates "
              << legacyUpdates << ", held-back host texts " << heldApply << "\n";
    check("host log: no copy-time text update (51) at all", legacyUpdates == 0);
    if (legacy) {
      check("host log: the legacy host was never asked to paste", applies == 0);
    } else {
      check("host log: one paste-apply line per text request the viewer sent", applies == static_cast<int>(text_sends()),
            std::to_string(applies) + " vs " + std::to_string(text_sends()));
      check("host log: exactly one write failed (the busy clipboard, at OpenClipboard)",
            failed == 1 && count_lines(hostLogPath, " result=1 stage=1 ") == 1, std::to_string(failed));
    }
    CopyFileW(hostLogPath.c_str(), (outDir + L"\\host_" + (legacy ? L"legacy" : L"paste") + L".log").c_str(), FALSE);
  }
  check("the scratch run directory is removed", hostGone && ts::remove_scratch_run_dir());
  std::printf("CHILD RESULT: %s  (%d checks, %d failed) [%s]\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures, tag);
  std::fflush(stdout);
  return gFailures ? 1 : 0;
}

namespace {
/**
 * The bar as the user sees it, on THIS desktop (a private station's windows are never visible, so
 * the bar there cannot show). No host: the viewer is told the host has clipboard sync but cannot
 * confirm a paste -- the older-host answer, which reads NO clipboard content: WM_CLIPBOARDUPDATE
 * asks only which formats exist, and the update-needed route never takes a snapshot.
 */
void run_bar_on_visible_desktop(const std::wstring& outDir) {
  std::cout << "\n=== the paste line on this desktop (product window and bar; no clipboard content read) ===\n";
  static const UINT kVirtualFiles = RegisterClipboardFormatW(L"FileGroupDescriptorW");
  if (kVirtualFiles && IsClipboardFormatAvailable(kVirtualFiles) && !IsClipboardFormatAvailable(CF_HDROP)) {
    std::printf("SKIP  the visible bar: this desktop's clipboard holds virtual files, which count as a remote copy\n");
    return;
  }
  ViewerState ctx;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60PasteBarViewer";
  RegisterClassExW(&wc);
  HWND w = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"GNLink (paste bar e2e)", WS_OVERLAPPEDWINDOW, 380, 120, 900,
                           560, nullptr, nullptr, wc.hInstance, &ctx);
  check("the product's window procedure owns a window on this desktop", w != nullptr);
  if (!w) return;
  ShowWindow(w, SW_SHOWNOACTIVATE);
  ctx.session.hwnd = w;
  ctx.session.inputEnabled.store(true);
  ctx.control.clipboard.enabled.store(true);
  ctx.control.clipboard.hostSupports.store(true);
  ctx.control.clipboard.hostPasteOnDemand.store(false);
  remote60::native_poc::viewer::create_clip_transfer_bar(ctx);
  SendMessageW(w, WM_CLIPBOARDUPDATE, 0, 0);  // this PC's copy is the newest
  // This thread's key state on the user's desktop is not ours alone: GetKeyboardState can hand back
  // a modifier the user is holding, and Alt / Win / Shift with V is not a paste (classify_paste_key).
  // So every modifier is set explicitly for the gesture, and the state found is put back after.
  BYTE original[256]{};
  GetKeyboardState(original);
  const auto show = [](const char* when) {
    std::printf("      diag(%s): ctrl=%d shift=%d alt=%d lwin=%d rwin=%d (GetKeyState high bit)\n", when,
                GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0, GetKeyState(VK_MENU) < 0,
                GetKeyState(VK_LWIN) < 0, GetKeyState(VK_RWIN) < 0);
  };
  show("found");
  std::printf("      diag: inputEnabled=%d sync=%d hostSupports=%d hostConfirms=%d\n", ctx.session.inputEnabled.load() ? 1 : 0,
              ctx.control.clipboard.enabled.load() ? 1 : 0, ctx.control.clipboard.hostSupports.load() ? 1 : 0,
              ctx.control.clipboard.hostPasteOnDemand.load() ? 1 : 0);
  BYTE ks[256];
  std::memcpy(ks, original, sizeof(ks));
  for (const int vk : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_MENU, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN}) ks[vk] = 0;
  ks[VK_CONTROL] = ks[VK_LCONTROL] = 0x80;
  ks[VK_RCONTROL] = 0;
  SetKeyboardState(ks);
  show("at the gesture");
  SendMessageW(w, WM_KEYDOWN, VK_CONTROL, key_lp(VK_CONTROL, false));
  SendMessageW(w, WM_KEYDOWN, 'V', key_lp('V', false));
  SendMessageW(w, WM_KEYUP, 'V', key_lp('V', true));
  ks[VK_CONTROL] = ks[VK_LCONTROL] = 0;
  SetKeyboardState(ks);
  SendMessageW(w, WM_KEYUP, VK_CONTROL, key_lp(VK_CONTROL, true));
  SetKeyboardState(original);
  const DWORD until = GetTickCount() + 600;
  while (GetTickCount() < until) {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    Sleep(10);
  }
  const HWND bar = clip_transfer_bar_window();
  const ClipBarView v = clip_transfer_bar_current();
  check("the bar is shown over the viewer", bar && IsWindowVisible(bar));
  check("...with the paste line: the remote GNLink must be updated",
        v.isPaste && clip_transfer_bar_text(v).find(L"업데이트") != std::wstring::npos, narrow(clip_transfer_bar_text(v)));
  check("...and no Retry or Cancel button for it", clip_transfer_bar_retry_rect().right == 0 && clip_transfer_bar_cancel_rect().right == 0);
  RECT br{}, vc{};
  GetWindowRect(bar, &br);
  GetClientRect(w, &vc);
  POINT o{0, 0};
  ClientToScreen(w, &o);
  check("...inside the viewer's client area", br.left >= o.x && br.right <= o.x + vc.right && br.top >= o.y && br.bottom <= o.y + vc.bottom);
  std::string shot;
  check("screenshot: the paste line on this desktop", save_windows_png(w, bar, outDir + L"\\bar_update_needed.png", &shot), shot);
  DestroyWindow(w);
  check("the bar goes with the window", clip_transfer_bar_window() == nullptr);
}

/** Runs a child on a private window station; its PASS/FAIL lines count here. */
void run_child_on_private_station(const std::wstring& outDir, bool legacy) {
  std::cout << "\n=== child on a private window station" << (legacy ? " (legacy host)" : "") << " ===\n";
  HWINSTA ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
  wchar_t name[256] = L"";
  DWORD len = 0;
  if (ws) GetUserObjectInformationW(ws, UOI_NAME, name, sizeof(name), &len);
  HWINSTA orig = GetProcessWindowStation();
  HDESK dk = nullptr;
  if (ws) {
    SetProcessWindowStation(ws);
    dk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    SetProcessWindowStation(orig);
  }
  check("a private window station for the child", ws != nullptr && dk != nullptr);
  const std::wstring result = outDir + (legacy ? L"\\child_legacy.txt" : L"\\child_paste.txt");
  std::wstring desktop = std::wstring(name) + L"\\Default";
  std::wstring cmd = L"\"" + self_path() + L"\" --paste-child \"" + result + L"\" --out \"" + outDir + L"\"" +
                     (legacy ? L" --legacy-host" : L"");
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.lpDesktop = desktop.data();
  PROCESS_INFORMATION pi{};
  DWORD code = 99;
  if (ws && dk && CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, 600000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  std::ifstream in(result);
  std::string line;
  while (std::getline(in, line)) {
    std::cout << "  child: " << line << "\n";
    if (line.rfind("PASS", 0) == 0) ++gChecks;
    if (line.rfind("FAIL", 0) == 0) {
      ++gChecks;
      ++gFailures;
    }
  }
  check(std::string("the child exited 0") + (legacy ? " (legacy host)" : ""), code == 0, std::to_string(code));
  if (dk) CloseDesktop(dk);
  if (ws) CloseWindowStation(ws);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  SetConsoleOutputCP(CP_UTF8);
  std::wstring outDir, childResult;
  bool legacy = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--thumbnail") {  // staged as GNLinkCapture.exe: a helper that never answers
      Sleep(300000);
      return 0;
    }
    if (a == L"--out" && i + 1 < argc) outDir = argv[++i];
    if (a == L"--paste-child" && i + 1 < argc) childResult = argv[++i];
    if (a == L"--legacy-host") legacy = true;
  }
  if (!childResult.empty()) return run_paste_child(childResult, outDir, legacy);
  if (!host_e2e_allowed()) {
    std::printf("SKIP  viewer_paste_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  if (outDir.empty()) outDir = directory_of(self_path()) + L"paste_shots";
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC, for the screenshot's PNG
  CreateDirectoryW(outDir.c_str(), nullptr);
  remote60::native_poc::e2e::StationLock stationLock;  // TEST ONLY: one clipboard e2e at a time
  if (!stationLock.Acquire("viewer_paste_e2e_test")) return remote60::native_poc::e2e::StationLock::Busy("viewer_paste_e2e_test");
  const DWORD interactiveClipBefore = GetClipboardSequenceNumber();
  std::cout << "screenshots: " << narrow(outDir) << "\n";
  // Where the user's clipboard moved, if it did, and whose it is then: the process that owns it
  // (name only -- nothing on it is read). This test never writes it; a move is somebody else's copy.
  const auto clip_owner = [] {
    DWORD pid = 0;
    if (HWND o = GetClipboardOwner()) GetWindowThreadProcessId(o, &pid);
    const std::wstring path = pid ? process_path(pid) : std::wstring();
    return "owner pid " + std::to_string(pid) + " " + narrow(path.substr(path.find_last_of(L'\\') + 1));
  };
  const auto phase_seq = [&](const char* phase) {
    std::printf("      WinSta0 clipboard after %s: seq %lu, %s\n", phase, GetClipboardSequenceNumber(), clip_owner().c_str());
  };
  std::printf("      WinSta0 clipboard at start: seq %lu, %s\n", interactiveClipBefore, clip_owner().c_str());
  run_child_on_private_station(outDir, false);
  phase_seq("the private-station run");
  run_child_on_private_station(outDir, true);
  phase_seq("the legacy-host run");
  run_bar_on_visible_desktop(outDir);
  phase_seq("the visible-desktop bar");
  const DWORD interactiveClipAfter = GetClipboardSequenceNumber();
  check("the user's clipboard (WinSta0) did not move: " + std::to_string(interactiveClipBefore) + " -> " +
            std::to_string(interactiveClipAfter),
        interactiveClipBefore == interactiveClipAfter, clip_owner());
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
