// The clipboard-image transfer bar, end to end (3rd rate agreement ④, DECISIONS §5).
//
// What the user does: copies an image, sees "원격 PC로 이미지 보내는 중 N% ..." at the bottom of the
// viewer, presses 취소, and sees that it was cancelled -- and the remote side must have stopped
// too, ready for the next copy.
//
// The chain here, with nothing in it written by the test but the image and the two clicks:
//
//   ClipImageClient::SubmitSnapshot (the product method the viewer's WM_CLIPBOARDUPDATE calls --
//   the ONE replaced step: the image comes from this test, not from the user's clipboard, which
//   no test may touch) -> the product's package worker, offer, bulk serving (viewer_clip_image_
//   wiring.cpp, the product's own start) -> a real GNLinkStreamClipSink started here, isolated,
//   on a free port, input injection off -> pulls, chunks, status every 500 ms (the product's
//   ControlClient worker) -> the product's transfer bar (viewer_clip_transfer_bar.cpp) over a
//   window owned by the product's WndProc.
//
// Evidence written to --out: PNG images of the viewer window with the bar composited at their
// on-screen positions (PrintWindow of each, so a window of another program lying over them cannot
// leak into the image): while sending, after Cancel, after a small image was published.
//
// What differs from the shipped viewer, said once: the viewer's socket, Hello and receive loop are
// driven by this test (as viewer_mouse_xbutton_e2e_test does), no video is decoded (the window
// shows the product's own "연결하는 중…" line under the bar), the uplink cap is lowered to 1 Mbps
// by the product's own REMOTE60_CLIP_BULK_CAP_BPS so the 5 MiB transfer is still running when it
// is photographed and cancelled, and the clicks are posted window messages -- a physical mouse
// click is not used (it would move the user's real cursor).
//
// The user's clipboard is never read or written. Images ride the text clipboard sync (the product
// offers an image only to a host that advertises text sync too), so the host runs WITH its sync on
// -- on a private window station, whose clipboard is not the user's (as clip_image_clipboard_test's
// child). Unnamed stations are one per logon session, so this test's own clipboard child shares it:
// the child empties it before the host starts. On this side the two window messages that would touch this machine's clipboard -- the
// once-per-session push (kMsgPushClipboardNow, which reads it) and an incoming host text
// (kMsgApplyClipboard, which writes it) -- are held back from the product's window procedure by
// this test's message loop and counted. WinSta0's clipboard sequence number is compared before and
// after.
//
// The clipboard boundary itself (Codex review of 4528dc9 ②) is driven by a CHILD of this test on a
// private window station, whose clipboard is its own: real formats put there, the product's
// WndProc hearing WM_CLIPBOARDUPDATE, capture_local_clipboard reading it -- a new copy voids an
// older image still being encoded, the viewer's own echo does not, a copy over the size limit says
// it was not sent. No host is needed for that part.
//
// Scratch lives in the repository (test_scratch_dir: created by this run, reparse-checked, removed
// only after the host is known to have exited), never in %TEMP%.
//
//   remote60_viewer_clip_bar_e2e_test --out <dir>      (needs REMOTE60_ALLOW_HOST_E2E=1)

#include "e2e_isolation.hpp"
#include "control_resume_e2e_support.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
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
#include "viewer_state.hpp"
#include "viewer_udp_session.hpp"
#include "viewer_window_proc.hpp"
#include "test_scratch_dir.hpp"

namespace ts = remote60::native_poc::test_support;

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;
using remote60::native_poc::viewer::ViewerState;

// The one stand-in, and it is not in the chain: the macro window is a WebView2 window with its own
// loader, and nothing here opens it.
namespace remote60::native_poc {
bool macro_window_visible() { return false; }
void macro_window_toggle(HINSTANCE, HWND, const MacroWindowHooks&) {}
}  // namespace remote60::native_poc

namespace {

uint16_t kHostPort = 0;

/** A window of this process for the host to capture (so it never captures the user's screen). */
struct CaptureSource {
  HWND hwnd = nullptr;
  std::wstring title;
  static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(h, &ps);
      HBRUSH b = CreateSolidBrush(RGB(40, 90, 140));
      FillRect(dc, &ps.rcPaint, b);
      DeleteObject(b);
      EndPaint(h, &ps);
      return 0;
    }
    return DefWindowProcW(h, m, w, l);
  }
  bool Start() {
    title = e2e_unique_window_title(L"remote60 clip bar source");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60ClipBarSource";
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, title.c_str(), WS_POPUP, 0, 0, 320,
                           180, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return hwnd != nullptr;
  }
};

ClipSnapshot noise_dib(uint32_t w, uint32_t h, uint64_t sequence, uint32_t seed) {
  ClipSnapshot s;
  s.kind = ClipSnapshotKind::Dib;
  s.sequence = sequence;
  s.bytes.resize(sizeof(BITMAPV5HEADER) + static_cast<size_t>(w) * h * 4);
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
  std::memcpy(s.bytes.data(), &bh, sizeof(bh));
  uint32_t x = seed;
  for (size_t o = sizeof(bh); o + 4 <= s.bytes.size(); o += 4) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const uint32_t px = x | 0xFF000000u;  // opaque: noise does not compress, which is the point
    std::memcpy(s.bytes.data() + o, &px, 4);
  }
  return s;
}

/**
 * The viewer window and the bar as they are on screen, one image: each rendered with PrintWindow
 * (its own pixels, whatever lies over it) and placed at its screen position relative to the viewer.
 */
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
  bi.bmiHeader.biHeight = h;  // bottom-up, as a clipboard DIB
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
    px |= 0xFF000000u;  // GDI leaves alpha 0: the PNG would be transparent
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
    *detail = std::to_string(w) + "x" + std::to_string(h) + " printViewer=" + std::to_string(okViewer) +
              " bar=" + (barDrawn ? "drawn" : "not visible") + " printBar=" + std::to_string(okBar) +
              " png=" + std::to_string(png.size()) + " bytes";
  }
  return okViewer && okBar && written;
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

int count_files(const std::wstring& dir) {
  WIN32_FIND_DATAW fd{};
  HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (f == INVALID_HANDLE_VALUE) return 0;
  int n = 0;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n;
  } while (FindNextFileW(f, &fd));
  FindClose(f);
  return n;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return std::string();
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

}  // namespace

// ------------------------------------------------------------------ the clipboard-boundary child
// Runs on a private window station (its clipboard is not the user's). Everything below the product
// window is the product's: WndProc -> capture_local_clipboard -> ClipImageClient.
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
std::vector<uint8_t> text_bytes(const std::u16string& s) {
  std::vector<uint8_t> b((s.size() + 1) * 2, 0);
  std::memcpy(b.data(), s.data(), s.size() * 2);
  return b;
}

int run_clipboard_child(const wchar_t* resultFile) {
  FILE* out = _wfreopen(resultFile, L"w", stdout);
  if (!out) return 3;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ViewerState ctx;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;  // the product's
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60ClipBarStationViewer";
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"station viewer", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, nullptr,
                              nullptr, wc.hInstance, &ctx);
  check("the product's window procedure owns a window on the private station", hwnd != nullptr);
  ctx.control.clipboard.enabled.store(true);
  ctx.control.clipboard.hostSupports.store(true);
  auto& image = ctx.control.clipImage;
  image.Start([](const void*, size_t) { return true; }, [] { return uint64_t{0}; }, [] { return false; }, 1200,
              BulkRateConfig{}, [](const std::string& l) { std::printf("      client: %s\n", l.c_str()); });
  image.SetBulkNegotiated(true);
  image.SetHostSupports(true);
  const UINT kDibv5 = CF_DIBV5;
  const auto dib_bytes = [](uint32_t w, uint32_t h, uint32_t seed) { return noise_dib(w, h, seed, seed).bytes; };
  const auto packaged = [&] { return image.GetCounters().packaged; };
  const auto settle = [&] {
    for (int i = 0; i < 300; ++i) {  // until the package worker has nothing left (<= 3 s)
      Sleep(10);
    }
  };

  // 1. An ordinary image copy reaches the image client through the product's path.
  check("an image copy is put on the private clipboard", station_put(kDibv5, dib_bytes(64, 64, 1)));
  SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);
  settle();
  check("WM_CLIPBOARDUPDATE -> capture_local_clipboard -> the client packages it",
        image.GetCounters().submitted == 1 && packaged() == 1);

  // 2. A genuine new copy (text) while an older image is still being encoded: the older one is void.
  const uint64_t p2 = packaged();
  station_put(kDibv5, dib_bytes(2048, 2048, 2));
  SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);  // encoding starts (a 2048 x 2048 noise PNG: ~100 ms+)
  station_put(CF_UNICODETEXT, text_bytes(u"a newer copy by the user"));
  SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);  // the user copied something else meanwhile
  settle();
  check("a newer copy makes the image still being encoded void (nothing of it is offered)", packaged() == p2);

  // 2b. The case the old code missed: the new copy is an image the clipboard read refuses (over the
  //     snapshot limit before anything is copied). It cancelled nothing, so the older image still went.
  {
    const uint64_t p2b = packaged();
    station_put(kDibv5, dib_bytes(2048, 2048, 5));
    SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);  // the older image starts encoding
    std::vector<uint8_t> huge(65u * 1024u * 1024u + 4096u, 0);  // over kMaxDibSnapshotBytes
    BITMAPV5HEADER bh{};
    bh.bV5Size = sizeof(bh);
    bh.bV5Width = 4096;
    bh.bV5Height = 4097;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_RGB;
    std::memcpy(huge.data(), &bh, sizeof(bh));
    check("an image copy over the read limit is put on the private clipboard", station_put(kDibv5, huge));
    SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);
    settle();
    check("a new image the read refuses still makes the older image void", packaged() == p2b);
    const auto pr2 = image.GetProgress();
    check("...and says the new one was not sent (too large)",
          pr2.outcome == ClipOutcome::NotSent && pr2.detail == static_cast<uint8_t>(ClipPackageResult::TooLarge));
  }

  // 3. The viewer's own echo -- the host's text it just wrote -- is NOT a new copy.
  const uint64_t p3 = packaged();
  station_put(kDibv5, dib_bytes(2048, 2048, 3));
  SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);
  SendMessageW(hwnd, remote60::native_poc::viewer::kMsgApplyClipboard, 0,
               reinterpret_cast<LPARAM>(new std::u16string(u"text from the host")));  // the product's apply
  SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);  // what that write provokes
  settle();
  check("the echo of the viewer's own write does not void the image being encoded", packaged() == p3 + 1);

  // 4. A copy over the size limit: the older one is void and the user is told this one did not go.
  station_put(kDibv5, dib_bytes(8193, 1, 4));
  SendMessageW(hwnd, WM_CLIPBOARDUPDATE, 0, 0);
  settle();
  const auto pr = image.GetProgress();
  check("an image over the size limit ends with 'not sent: too large'",
        pr.outcome == ClipOutcome::NotSent && pr.detail == static_cast<uint8_t>(ClipPackageResult::TooLarge));

  image.Stop();
  DestroyWindow(hwnd);
  // Unnamed private stations are one per logon session (clip_image_winsta_test): the host started
  // next lives on this same station, and must find its clipboard empty, not this child's text.
  if (station_open()) {
    EmptyClipboard();
    CloseClipboard();
  }
  std::printf("CHILD RESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "PASSED", gChecks, gFailures);
  std::fflush(stdout);
  return gFailures ? 1 : 0;
}

/** Runs the child above on a private window station; its PASS/FAIL lines count here. */
void run_clipboard_boundary() {
  std::cout << "\n--- the clipboard boundary, on a private window station (the product's WndProc) ---\n";
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
  check("a private window station for the clipboard child", ws != nullptr && dk != nullptr);
  const std::wstring result = ts::scratch_path(L"clipboard_child.txt");
  std::wstring desktop = std::wstring(name) + L"\\Default";
  std::wstring cmd = L"\"" + self_path() + L"\" --clipboard-child \"" + result + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.lpDesktop = desktop.data();
  PROCESS_INFORMATION pi{};
  DWORD code = 99;
  if (ws && dk && CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, 120000);
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
  in.close();
  DeleteFileW(result.c_str());
  check("the clipboard child exited 0", code == 0, std::to_string(code));
  if (dk) CloseDesktop(dk);
  if (ws) CloseWindowStation(ws);
}

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  SetConsoleOutputCP(CP_UTF8);
  std::wstring outDir;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--thumbnail") {  // staged as GNLinkCapture.exe: a helper that never answers
      Sleep(300000);
      return 0;
    }
    if (a == L"--out" && i + 1 < argc) outDir = argv[++i];
    if (a == L"--clipboard-child" && i + 1 < argc) return run_clipboard_child(argv[i + 1]);
  }
  if (!host_e2e_allowed()) {
    std::printf("SKIP  viewer_clip_bar_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  if (outDir.empty()) outDir = directory_of(self_path()) + L"clip_bar_shots";
  const DWORD interactiveClipBefore = GetClipboardSequenceNumber();
  CreateDirectoryW(outDir.c_str(), nullptr);
  std::cout << "screenshots: " << narrow(outDir) << "\n";

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  kHostPort = e2e_pick_free_udp_port();
  if (kHostPort == 0) {
    std::printf("FAIL  no free UDP port for the host\n");
    return 1;
  }
  std::printf("host port %u (picked at run time)\n", kHostPort);

  // Scratch in the repository (test_scratch_dir), never %TEMP%.
  const std::wstring scratch = ts::make_scratch_dir(L"clip_bar");
  check("a scratch directory inside the repository", !scratch.empty(), ts::scratch_root_problem());
  if (scratch.empty()) {
    std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
    return 1;
  }
  const std::wstring dir = scratch + L"\\";
  run_clipboard_boundary();
  const std::wstring me = self_path();
  const std::wstring myDir = directory_of(me);
  // The TEST build of the host: received images go to a folder, never to a clipboard.
  const bool staged =
      CopyFileW((myDir + L"GNLinkStreamClipSink.exe").c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
      CopyFileW(me.c_str(), (dir + L"GNLinkCapture.exe").c_str(), FALSE) &&
      CopyFileW((myDir + L"GNLinkClipHelper.exe").c_str(), (dir + L"GNLinkClipHelper.exe").c_str(), FALSE);
  check("the test host (ClipSink), a never-answering helper and the clipboard helper could be staged", staged);

  CaptureSource source;
  check("a window of this process is up for the host to capture", source.Start());

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

  std::wstring hostDesktopName;  // the host's private station (its consumers paste there)
  const std::wstring hostLogPath = dir + L"host.log";
  const std::wstring sinkDir = dir + L"sink";
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION hostPi{};
  bool launched = false;
  if (staged) {
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_CLIPBOARD_SYNC", nullptr);  // on: images need the text sync
    CreateDirectoryW(sinkDir.c_str(), nullptr);
    SetEnvironmentVariableW(L"REMOTE60_CLIP_IMAGE_TEST_SINK_DIR", sinkDir.c_str());
    // File copy: the test build's helper runs as this user on the host's private station.
    SetEnvironmentVariableW(L"REMOTE60_FILE_COPY_TEST_HELPER_AS_SELF", L"1");
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(kHostPort) +
                       L" --fps 30 --seconds 300 --input-injection-mode none" + e2e_capture_window_args(source.title);
    const bool noInjection = cmd.find(L"--input-injection-mode none") != std::wstring::npos;
    check("the host is started with input injection off", noInjection);
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
    // A private window station: the host's clipboard is its own, not the user's.
    HWINSTA ws = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);  // unnamed: medium may not name one
    wchar_t wsName[256] = L"";
    DWORD wsLen = 0;
    if (ws) GetUserObjectInformationW(ws, UOI_NAME, wsName, sizeof(wsName), &wsLen);
    HWINSTA origWs = GetProcessWindowStation();
    HDESK privateDesk = nullptr;
    if (ws) {
      SetProcessWindowStation(ws);
      privateDesk = CreateDesktopW(L"Default", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
      SetProcessWindowStation(origWs);
    }
    static std::wstring hostDesktop;
    hostDesktop = std::wstring(wsName) + L"\\Default";
    hostDesktopName = hostDesktop;
    check("the host gets a private window station (its own clipboard)", ws != nullptr && privateDesk != nullptr,
          narrow(hostDesktop));
    si.lpDesktop = hostDesktop.data();
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = ws != nullptr && privateDesk != nullptr && e2e_command_is_isolated(cmd, dir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = noInjection && isoOk &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hostLog != INVALID_HANDLE_VALUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, isoEnv.data(),
                              dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  // ------------------------------------------------------------------ the viewer, the product's
  // The product's own cap switch (clip_bulk_rate_config_from_env, read by the CRT's getenv): the
  // 5 MiB image must still be on its way when it is photographed and cancelled.
  _putenv_s("REMOTE60_CLIP_BULK_CAP_BPS", "1000000");
  ViewerState ctx;
  SOCKET sock = INVALID_SOCKET;
  std::thread worker;
  std::thread ingress;
  std::atomic<bool> ingressStop{false};
  std::atomic<uint64_t> media{0};
  bool connected = false;
  uint32_t nextResumeId = 0x7400;
  uint64_t generation = 1;
  HWND viewerWindow = nullptr;
  if (launched) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;  // the product's, not a stand-in
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60ClipBarViewer";
    RegisterClassExW(&wc);
    viewerWindow = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"GNLink (clip bar e2e)", WS_OVERLAPPEDWINDOW,
                                   380, 120, 900, 560, nullptr, nullptr, wc.hInstance, &ctx);
    check("the product's window procedure owns a window", viewerWindow != nullptr);
    if (viewerWindow) ShowWindow(viewerWindow, SW_SHOWNOACTIVATE);
    ctx.session.hwnd = viewerWindow;
    ctx.session.running.store(true);
    ctx.session.controlRequired = true;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(kHostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 50;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 8 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    ctx.session.sock = sock;

    // The product's Hello (asks for the bulk channel) and the product's reading of the ack.
    UdpHelloOptions hello = remote60::native_poc::viewer::viewer_udp_hello_options(std::string(), false);
    std::string helloError;
    uint32_t ackFeatures = 0;
    check("the viewer's Hello is answered", udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr),
          helloError);
    remote60::native_poc::viewer::viewer_apply_udp_hello_ack(ackFeatures, false, ctx.session);
    check("the host acknowledged the bulk channel", ctx.session.bulkChannelNegotiated);

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

    // The product's image-client start and transfer bar (viewer_clip_image_wiring.cpp).
    remote60::native_poc::viewer::start_clip_image_client(ctx, 1200);
    remote60::native_poc::viewer::create_clip_transfer_bar(ctx);
    check("the transfer bar exists (hidden until an image goes)",
          clip_transfer_bar_window() != nullptr && !IsWindowVisible(clip_transfer_bar_window()));

    ingress = std::thread([&] {
      std::vector<uint8_t> buf(2048);
      while (!ingressStop.load()) {
        ctx.control.udpControl.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n <= 0) continue;
        // The product's receive order (viewer_video_receiver.cpp): the bulk streams first -- a file
        // paste's, then an image's.
        if (ctx.session.bulkChannelNegotiated && ctx.control.fileCopy.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
        if (ctx.session.bulkChannelNegotiated && ctx.control.clipImage.OnDatagram(buf.data(), static_cast<size_t>(n))) continue;
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

  int heldPush = 0, heldApply = 0;
  const auto pump_until = [&](const std::function<bool()>& done, int budgetMs) {
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
    for (;;) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        // Held back: these two would read / write THIS machine's clipboard (see the header).
        if (msg.message == remote60::native_poc::viewer::kMsgPushClipboardNow) {
          ++heldPush;
          continue;
        }
        if (msg.message == remote60::native_poc::viewer::kMsgApplyClipboard) {
          ++heldApply;
          continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (done()) return true;
      if (GetTickCount() >= deadline) return done();
      Sleep(10);
    }
  };
  const auto click_bar = [&](int x, int y) {
    const HWND bar = clip_transfer_bar_window();
    const LPARAM at = MAKELPARAM(x, y);
    PostMessageW(bar, WM_LBUTTONDOWN, MK_LBUTTON, at);
    PostMessageW(bar, WM_LBUTTONUP, 0, at);
    pump_until([] { return false; }, 300);
  };

  const bool ready = connected &&
                     pump_until([&] { return ctx.control.clipImage.Usable(); }, 25000);
  check("the host says it takes images (Pong) and the bulk channel is up", ready);

  if (ready) {
    // ---------------------------------------------------------------- 1. sending, then Cancel
    std::cout << "\n--- 5 MiB image: the bar while sending ---\n";
    ctx.control.clipImage.SubmitSnapshot(noise_dib(512, 2560, 2001, 0x2468ACE1u));
    const bool sending = pump_until(
        [&] {
          const ClipBarView v = clip_transfer_bar_current();
          return v.phase == ClipBarPhase::Sending && v.bytesDone > 0 && v.elapsedMs >= 4000;
        },
        30000);
    const HWND bar = clip_transfer_bar_window();
    const ClipBarView v1 = clip_transfer_bar_current();
    const std::wstring t1 = clip_transfer_bar_text(v1);
    check("the bar shows the transfer while it runs", sending && IsWindowVisible(bar), narrow(t1));
    check("...with a percentage, sizes and seconds",
          t1.find(L"원격 PC로 이미지 보내는 중") == 0 && t1.find(L"%") != std::wstring::npos &&
              t1.find(L"MB") != std::wstring::npos && t1.find(L"초") != std::wstring::npos);
    const RECT cancel = clip_transfer_bar_cancel_rect();
    RECT barClient{};
    GetClientRect(bar, &barClient);
    check("...and a Cancel button inside the bar",
          cancel.right > cancel.left && cancel.left >= 0 && cancel.right <= barClient.right && cancel.top >= 0 &&
              cancel.bottom <= barClient.bottom,
          std::to_string(cancel.left) + "," + std::to_string(cancel.top) + "-" + std::to_string(cancel.right) + "," +
              std::to_string(cancel.bottom));
    {
      RECT br{}, vc{};
      GetWindowRect(bar, &br);
      GetClientRect(viewerWindow, &vc);
      POINT o{0, 0};
      ClientToScreen(viewerWindow, &o);
      const bool inside = br.left >= o.x && br.right <= o.x + vc.right && br.top >= o.y && br.bottom <= o.y + vc.bottom;
      check("the bar lies inside the viewer's client area (not off-window)", inside);
      bool aboveOwner = false;
      for (HWND h = GetWindow(bar, GW_HWNDNEXT); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (h == viewerWindow) {
          aboveOwner = true;
          break;
        }
      }
      check("the bar is above the viewer in z-order (the viewer cannot cover it)", aboveOwner);
      check("the bar is enabled and takes the mouse", IsWindowEnabled(bar) != FALSE);
      const POINT c{br.left + (cancel.left + cancel.right) / 2, br.top + (cancel.top + cancel.bottom) / 2};
      const HWND atPoint = WindowFromPoint(c);
      std::cout << "      info: the window at the Cancel button's screen point is "
                << (atPoint == bar ? "the bar" : "another window (this desktop's own z-order)") << "\n";
    }
    std::string shot;
    check("screenshot while sending", save_windows_png(viewerWindow, bar, outDir + L"\\01_sending.png", &shot), shot);

    // Negative control first: a press on the text, not the button, must cancel nothing.
    const uint64_t beforeCancelled = ctx.control.clipImage.GetCounters().cancelled;
    click_bar(cancel.left / 2, (cancel.top + cancel.bottom) / 2);
    pump_until([] { return false; }, 1500);
    check("a click beside the button cancels nothing (negative control)",
          ctx.control.clipImage.Active() && ctx.control.clipImage.GetCounters().cancelled == beforeCancelled);

    std::cout << "\n--- 취소 ---\n";
    const uint64_t cancelAt = qpc_now_us();
    click_bar((cancel.left + cancel.right) / 2, (cancel.top + cancel.bottom) / 2);
    const bool stopped = pump_until([&] { return !ctx.control.clipImage.Active(); }, 10000);
    check("Cancel stops the sending on this side at once", stopped,
          std::to_string((qpc_now_us() - cancelAt) / 1000) + " ms after the click");
    const bool settled = pump_until([&] { return !ctx.control.clipImage.GetProgress().cancelling; }, 20000);
    const auto p1 = ctx.control.clipImage.GetProgress();
    const auto c1 = ctx.control.clipImage.GetCounters();
    check("...and the HOST's answer settles it: cancelled, by the user",
          settled && p1.outcome == ClipOutcome::Cancelled && p1.detail == static_cast<uint8_t>(ClipImageReason::User),
          "outcome=" + std::to_string(static_cast<int>(p1.outcome)) + " detail=" + std::to_string(p1.detail) + " after " +
              std::to_string((qpc_now_us() - cancelAt) / 1000) + " ms");
    pump_until([&] { return clip_transfer_bar_current().phase == ClipBarPhase::Result; }, 3000);
    const std::wstring t2 = clip_transfer_bar_text(clip_transfer_bar_current());
    check("the bar says it was cancelled, without a Cancel button",
          t2 == L"이미지 보내기를 취소했습니다" && clip_transfer_bar_cancel_rect().right == 0 && IsWindowVisible(bar),
          narrow(t2));
    check("screenshot after Cancel", save_windows_png(viewerWindow, bar, outDir + L"\\02_cancelled.png", &shot), shot);

    // ---------------------------------------------------------------- 2. the host is idle again
    std::cout << "\n--- a small image after the cancel: the host takes it ---\n";
    const uint64_t publishedBefore = c1.published;
    ctx.control.clipImage.SubmitSnapshot(noise_dib(256, 256, 2002, 0x13579BDFu));
    const bool published = pump_until([&] { return ctx.control.clipImage.GetCounters().published > publishedBefore; }, 30000);
    check("the next copy is published (both sides went idle after the cancel)", published);
    pump_until([&] {
      const ClipBarView v = clip_transfer_bar_current();
      return v.phase == ClipBarPhase::Result && v.outcome == ClipOutcome::Published;
    }, 3000);
    const std::wstring t3 = clip_transfer_bar_text(clip_transfer_bar_current());
    check("the bar says where it went", t3.find(L"이미지를 원격 PC 클립보드에 넣었습니다") == 0, narrow(t3));
    check("screenshot after a published image",
          save_windows_png(viewerWindow, bar, outDir + L"\\03_published.png", &shot), shot);
    const bool hidden = pump_until([&] { return !IsWindowVisible(bar); }, 8000);
    check("the result line goes away by itself (about 5 s)", hidden);

    // ---------------------------------------------------------------- 3. files (t-zdmsd4gb D6)
    // The one replaced step: the files come from this test (SubmitLocalFiles is the product method
    // the window procedure calls on a CF_HDROP copy), not from the user's clipboard. The paste is a
    // real consumer (Explorer's copy engine) on the host's private station.
    std::cout << "\n--- files: offered, sending, Cancel, a completed paste ---\n";
    const bool filesUsable = pump_until([&] { return ctx.control.fileCopy.Usable(); }, 15000);
    check("the host says it takes files (Pong 0x800)", filesUsable);
    const std::wstring filesDir = dir + L"files";
    CreateDirectoryW(filesDir.c_str(), nullptr);
    const std::wstring bigPath = filesDir + L"\\big file.bin";
    {
      std::vector<uint8_t> big(16u * 1024u * 1024u);
      uint32_t x = 0x1234567u;
      for (auto& b : big) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = static_cast<uint8_t>(x);
      }
      std::ofstream(bigPath, std::ios::binary).write(reinterpret_cast<const char*>(big.data()), static_cast<std::streamsize>(big.size()));
    }
    ctx.control.fileCopy.SubmitLocalFiles({bigPath}, 9001);
    const bool offered = pump_until([&] {
      const ClipBarView v = clip_transfer_bar_current();
      return v.isFile && clip_transfer_bar_text(v).find(L"붙여넣을 수 있습니다") != std::wstring::npos;
    }, 20000);
    const std::wstring f0 = clip_transfer_bar_text(clip_transfer_bar_current());
    check("the bar says the copied file can be pasted on the remote PC (an offer, not a transfer)",
          offered && f0 == L"복사한 파일 1개를 원격 PC에서 붙여넣을 수 있습니다" && IsWindowVisible(bar), narrow(f0));
    check("screenshot: offered", save_windows_png(viewerWindow, bar, outDir + L"\\04_file_offered.png", &shot), shot);

    const std::wstring consumerExe = myDir + L"remote60_file_copy_helper_e2e_test.exe";
    const auto start_consumer = [&](const std::wstring& dest, uint64_t expect, PROCESS_INFORMATION* pi) {
      CreateDirectoryW(dest.c_str(), nullptr);
      std::wstring cmd = L"\"" + consumerExe + L"\" --consumer --mode drop --dest \"" + dest + L"\" --result \"" + dest +
                         L".txt\" --expect-bytes " + std::to_wstring(expect) + L" --wait-sec 60";
      std::vector<wchar_t> c(cmd.begin(), cmd.end());
      c.push_back(0);
      STARTUPINFOW csi{};
      csi.cb = sizeof(csi);
      std::wstring d = hostDesktopName;
      csi.lpDesktop = d.data();
      const bool ok = CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &csi, pi) != 0;
      if (ok) AssignProcessToJobObject(job, pi->hProcess);
      return ok;
    };
    PROCESS_INFORMATION bigPi{};
    check("a paste consumer starts on the host's private station", start_consumer(dir + L"destBig", 16u * 1024u * 1024u, &bigPi));
    const bool fileSending = pump_until([&] {
      const ClipBarView v = clip_transfer_bar_current();
      return v.isFile && v.phase == ClipBarPhase::Sending && v.bytesDone > 0 && v.elapsedMs >= 3000;
    }, 40000);
    const std::wstring f1 = clip_transfer_bar_text(clip_transfer_bar_current());
    check("the bar shows the file going out while it runs", fileSending && IsWindowVisible(bar), narrow(f1));
    check("...with file count, a percentage, sizes and seconds",
          f1.find(L"원격 PC로 파일 1개 보내는 중") == 0 && f1.find(L"%") != std::wstring::npos && f1.find(L"MB") != std::wstring::npos);
    const RECT fc = clip_transfer_bar_cancel_rect();
    check("...and a Cancel button", fc.right > fc.left);
    check("screenshot: file sending", save_windows_png(viewerWindow, bar, outDir + L"\\05_file_sending.png", &shot), shot);
    const auto served0 = ctx.control.fileCopy.GetCounters().bytesServed;
    click_bar(fc.left / 2, (fc.top + fc.bottom) / 2);
    pump_until([] { return false; }, 1000);
    check("a click beside the button cancels nothing (negative control)",
          ctx.control.fileCopy.PasteActive() && ctx.control.fileCopy.GetCounters().bytesServed > served0);
    click_bar((fc.left + fc.right) / 2, (fc.top + fc.bottom) / 2);
    const bool fileCancelled = pump_until([&] {
      const ClipBarView v = clip_transfer_bar_current();
      return v.isFile && v.phase == ClipBarPhase::Result && !ctx.control.fileCopy.PasteActive();
    }, 20000);
    const std::wstring f2 = clip_transfer_bar_text(clip_transfer_bar_current());
    check("Cancel ends the paste, confirmed by the host: the bar says it was cancelled, without a Cancel button",
          fileCancelled && f2 == L"파일 붙여넣기를 취소했습니다" + std::wstring(L" — 받는 쪽에 일부만 저장된 파일이 남았을 수 있습니다") &&
              clip_transfer_bar_cancel_rect().right == 0,
          narrow(f2));
    check("screenshot: file cancelled", save_windows_png(viewerWindow, bar, outDir + L"\\06_file_cancelled.png", &shot), shot);
    if (bigPi.hProcess) {
      WaitForSingleObject(bigPi.hProcess, 70000);
      CloseHandle(bigPi.hProcess);
      CloseHandle(bigPi.hThread);
    }
    const std::wstring smallPath = filesDir + L"\\small.txt";
    std::ofstream(smallPath, std::ios::binary) << std::string(40000, 'x');
    ctx.control.fileCopy.SubmitLocalFiles({smallPath}, 9002);
    pump_until([&] { return ctx.control.fileCopy.GetCounters().offersAccepted >= 2; }, 20000);
    PROCESS_INFORMATION smallPi{};
    start_consumer(dir + L"destSmall", 40000, &smallPi);
    const bool completed = pump_until([&] {
      return clip_transfer_bar_text(clip_transfer_bar_current()).find(L"붙여넣기 완료") != std::wstring::npos;
    }, 40000);
    const std::wstring f3 = clip_transfer_bar_text(clip_transfer_bar_current());
    check("a paste the consumer completes: the bar says it was completed on the remote PC",
          completed && f3.find(L"원격 PC에서 파일 1개 붙여넣기 완료") == 0, narrow(f3));
    check("screenshot: file completed", save_windows_png(viewerWindow, bar, outDir + L"\\07_file_completed.png", &shot), shot);
    if (smallPi.hProcess) {
      WaitForSingleObject(smallPi.hProcess, 30000);
      CloseHandle(smallPi.hProcess);
      CloseHandle(smallPi.hThread);
    }
    std::ifstream smallLanded(dir + L"destSmall\\small.txt", std::ios::binary | std::ios::ate);
    check("...and the file is there, whole", smallLanded && smallLanded.tellg() == 40000);
    check("this viewer never started a clipboard helper of its own (nothing was put on this PC's clipboard)",
          ctx.control.fileCopy.GetCounters().helperLaunches == 0);
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
  check("the bar is destroyed with the viewer window", clip_transfer_bar_window() == nullptr);
  if (source.hwnd) DestroyWindow(source.hwnd);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);
  bool hostGone = true;
  if (hostPi.hProcess) {
    hostGone = WaitForSingleObject(hostPi.hProcess, 20000) == WAIT_OBJECT_0;
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  check("the host is gone when the job closes", hostGone);

  // The host's own account: the cancel arrived and ended its transfer; only the small image landed.
  if (launched && ready) {
    const int cancelEnds = count_lines(hostLogPath, "[clip-image] end state=cancelled reason=9");
    const int publishedEnds = count_lines(hostLogPath, "[clip-image] end state=published");
    // Files: the host's own account of the two pastes -- the cancelled one (state 4 = failed,
    // reason 4 = cancelled) and the completed one (state 3 = ended, reason 1 = completed).
    const int fileCancelEnds = count_lines(hostLogPath, "[file-copy] paste ended state=4 reason=4");
    const int fileCompletedEnds = count_lines(hostLogPath, "[file-copy] paste ended state=3 reason=1");
    check("host log: the big file's paste ended cancelled on the host", fileCancelEnds == 1, std::to_string(fileCancelEnds));
    check("host log: the small file's paste ended completed on the host", fileCompletedEnds == 1,
          std::to_string(fileCompletedEnds));
    check("host log: the transfer ended as cancelled by the USER (reason 9) on the host", cancelEnds == 1,
          std::to_string(cancelEnds));
    check("host log: exactly one image published (the small one)", publishedEnds == 1, std::to_string(publishedEnds));
    const int sunk = count_files(sinkDir);
    check("the test sink holds the published image and nothing of the cancelled one", sunk >= 1, std::to_string(sunk) + " files");
    {
      std::ifstream in(hostLogPath, std::ios::binary);
      std::string line;
      std::cout << "      host clip-image lines:\n";
      while (std::getline(in, line)) {
        if (line.find("[clip-image]") != std::string::npos) std::cout << "        " << line << "\n";
      }
    }
    // Kept beside the screenshots for the report (the scratch directory goes).
    CopyFileW(hostLogPath.c_str(), (outDir + L"\\host.log").c_str(), FALSE);
  }

  // Removed only once the host is known to have exited (above): a directory that will not go
  // usually means a process that has not stopped, and that is reported, not retried around.
  check("the scratch run directory is removed (the host had exited: " + std::string(hostGone ? "yes" : "NO") + ")",
        hostGone && ts::remove_scratch_run_dir());
  std::cout << "      held back from the window procedure: " << heldPush << " clipboard push(es), " << heldApply
            << " host text apply(s)\n";
  check("no host text arrived to apply (the station clipboard was emptied before the host started)", heldApply == 0);
  const DWORD interactiveClipAfter = GetClipboardSequenceNumber();
  check("the user's clipboard (WinSta0) did not move: " + std::to_string(interactiveClipBefore) + " -> " +
            std::to_string(interactiveClipAfter),
        interactiveClipBefore == interactiveClipAfter);
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
