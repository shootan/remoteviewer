// quality r1, end to end: what ABR does with a client that never sends decode metrics, with one
// that sends them the way the Android APK does (present* only, decode fields 0), and with one that
// reports properly and then goes silent. A real isolated host, the product control scheduler and
// serializer on this side; no phone.
//
// The field case (2026-09-27): this PC -> APK 0.2.21, bitrate 6000 or 3000 at 30 fps, fell to the
// lowest rung within 5-11 s ("high_to_mid_severe" then "mid_to_low_severe") with
// clientDecodedFps=0 clientAvgLatUs=0 clientMbps=0 and nothing wrong on the host. The APK sends
// ControlClientMetrics every second with only the present* block (native_video_client_session.cpp
// fills nothing else), and the host read its zero decode fps as a relay collapse.
//
// Modes (one host run each, --mode):
//   none     never sends ControlClientMetrics           -> must not demote
//   present  sends it APK-style, present* only            -> must not demote
//   stop     sends full Windows-style metrics, then stops -> must demote on stale_active (P7 kept)
//   recover  full metrics, 12 s of silence, then full again -> demotes, then climbs back to high
//            through the ABR's own hold times (r2: DesktopText restores fps and the 1080p floor)
// All modes also check the r1 instrumentation is live: [keyframe] lines with reasons, and the
// per-flow byte keys on the stats line.
//
// THE CADENCE. ABR only judges seconds in which the host sent a real cadence (>= fps/2 real
// frames); a sparse second holds. So this capture target is a window of this process that IS
// composed -- on screen, alpha 1/255, click-through, never activated, repainted at 60 Hz -- and the
// test counts real (non-synthetic) frames per second itself. If the host did not send a real
// cadence, the ABR checks are NOT JUDGED rather than passed.
//
// quality r2 adds the encode-size question: with --desktop the host captures the monitor instead of
// the test window (DesktopText priority applies), --bitrate sets the user's budget, --text-off sets
// the rollback switch, and --expect-size WxH asserts what the host actually encoded (its own stats
// line and [keyframe] lines). Nothing captured is kept: only sizes and byte counts are read.
//
//   remote60_abr_client_evidence_e2e_test --mode none|present|stop [--host <GNLinkStream.exe>]
//       [--desktop [--monitor N]] [--bitrate N] [--text-off] [--expect-size WxH]
//   --select-at SEC:window|desktop (r3, repeatable): at second SEC select this test's window or the
//   desktop with the product's ControlWindowSelect, the way a viewer does at runtime (a host started
//   with --capture-window-title refuses selection, so pair it with --desktop --cadence-window).
//   --tune-at SEC:BITRATE (r3, repeatable): a runtime tune (bitrate, fps kept at 30) through the
//   product scheduler, as the viewer's quality control sends it.
//   --request-key-at SEC (r4, repeatable): the viewer asks for a keyframe (product KeyframeRequestState).
//   Every one must be answered by a [keyframe] carrying reason "viewer" -- the recovery IDR the
//   r4 cadence change must not take away. --run-sec N overrides the run length.
//   --expect-final WxH asserts the size on the LAST stats line: the box re-chosen for the last capture.
//   --monitor selects the host's monitor N (primary first, then left to right) with the product's
//   ControlMonitorSelect, before the measured run -- a 4K panel that is not the primary.
//   quality r5, rate control: --fps N, --keyint N (frames, as the viewer sends it), --max-qp N (0 =
//   the ceiling off; unset = the host default), --pattern pan|noise and --window WxH turn the target
//   into a high-motion source -- pan: a large detailed texture panned a few pixels a frame (camera
//   motion over a busy scene, like video); noise: fresh noise every frame (the worst case). The
//   run ends with one R5MEASURE line: payload / parity / header Mbps over the seconds after warmup,
//   payload against the target, and the IDRs in that span, all from the host's own stats lines.
//   (REMOTE60_ALLOW_HOST_E2E=1)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>


#include "e2e_isolation.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"
#include "udp_control_channel.hpp"
#include "control_resume_e2e_support.hpp"

#include <mmsystem.h>  // timeBeginPeriod (r5); after the project headers, which bring winsock2/windows

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;

namespace {

uint16_t kHostPort = 0;  // picked at run time (e2e_pick_free_udp_port): tests run side by side
uint32_t kFps = 30;  // --fps (r5); 30 for every earlier mode

// r5: the source the host captures. Default is the small cadence animation of r1-r4.
enum class Pattern { Default, Pan, Noise };
Pattern gPattern = Pattern::Default;
int gWinW = 480;
int gWinH = 270;
int gStillAfterMs = 0;
double gMeasureFromSec = 8.0;  // r5 --measure-from SEC: the measured span starts SEC after the first stats line  // r5 --still-after SEC: the motion stops (the picture freezes) after SEC
int gBlock = 4;  // r5 --block: pan texture detail, px per random block (4 = very busy, 16 = video-like)
constexpr uint32_t kDefaultBitrate = 6000000;
std::wstring kTargetTitle;  // unique to this process, set in wmain (e2e_unique_window_title)

// An on-screen window the user cannot see or hit: alpha 1/255, WS_EX_TRANSPARENT, never activated.
// On screen because an off-screen window is never composed and the host then sends only synthetic
// refresh frames (C0 measured about one a second), which ABR treats as sparse and ignores.
class CadenceTarget {
 public:
  // selectable: leave out WS_EX_TOOLWINDOW, which the host's window list excludes -- needed only
  // when the test selects this window at runtime (it then shows a taskbar button for the run).
  bool selectable = false;
  bool Start() {
    thread_ = std::thread([this] { Run(); });
    return wait_until([this] { return ready_.load(); }, 5000) && hwnd_ != nullptr;
  }
  void Stop() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
  }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<CadenceTarget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_TIMER && self) {
      if (gStillAfterMs > 0 && self->startTick_ == 0) self->startTick_ = GetTickCount64();
      if (gStillAfterMs > 0 && GetTickCount64() - self->startTick_ > static_cast<uint64_t>(gStillAfterMs)) {
        return 0;  // frozen: nothing changes, nothing repaints
      }
      ++self->frame_;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    if (msg == WM_PAINT && self && gPattern != Pattern::Default) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      self->PaintMotion(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    if (msg == WM_PAINT && self) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      RECT client{};
      GetClientRect(hwnd, &client);
      const int phase = static_cast<int>(self->frame_ % 32);
      HBRUSH back = CreateSolidBrush((self->frame_ & 1) ? RGB(20, 20, 30) : RGB(40, 25, 25));
      FillRect(dc, &client, back);
      DeleteObject(back);
      RECT block{phase * 12, 30 + (phase % 7) * 10, phase * 12 + 140, 200};
      HBRUSH fore = CreateSolidBrush(RGB(200, 60 + phase * 5, 40 + phase * 3));
      FillRect(dc, &block, fore);
      DeleteObject(fore);
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
  void Run() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60AbrEvidenceTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW((selectable ? 0 : WS_EX_TOOLWINDOW) | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            wc.lpszClassName, kTargetTitle.c_str(), WS_POPUP, 0, 0, gWinW, gWinH, nullptr,
                            nullptr, wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      // r5: a high-motion source must change at least as often as the host samples it.
      if (gPattern != Pattern::Default) timeBeginPeriod(1);
      SetTimer(hwnd_, 1, gPattern != Pattern::Default ? 8 : 16, nullptr);
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    hwnd_ = nullptr;
  }
  // r5: pan -- a texture larger than the window by kPanMargin, blitted at an offset that moves
  // 7 px right and 3 px down a frame; busy at every scale (4 px blocks of random colour over
  // gradients and stripes), so it is hard to code but motion-predictable, like camera video.
  // noise -- every pixel fresh every frame.
  static constexpr int kPanMargin = 512;
  void EnsureSurface(HDC dc) {
    if (memDc_) return;
    const int w = gWinW + (gPattern == Pattern::Pan ? kPanMargin : 0);
    const int h = gWinH + (gPattern == Pattern::Pan ? kPanMargin : 0);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    memDc_ = CreateCompatibleDC(dc);
    bitmap_ = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
    SelectObject(memDc_, bitmap_);
    surfW_ = w;
    surfH_ = h;
    if (gPattern == Pattern::Pan) {
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          const uint32_t block = Rand(static_cast<uint32_t>((x / gBlock) * 7919 + (y / gBlock) * 104729));
          const uint32_t r = ((block & 0xFF) + (x * 255 / w)) / 2;
          const uint32_t g = (((block >> 8) & 0xFF) + (y * 255 / h)) / 2;
          const uint32_t b = ((((x + y) / 9) & 1) ? 200u : 40u) ^ ((block >> 16) & 0x3F);
          pixels_[static_cast<size_t>(y) * w + x] = (r << 16) | (g << 8) | b;
        }
      }
    }
  }
  static uint32_t Rand(uint32_t v) {
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return v * 2654435761u;
  }
  void PaintMotion(HDC dc) {
    EnsureSurface(dc);
    if (!pixels_) return;
    int sx = 0, sy = 0;
    if (gPattern == Pattern::Pan) {
      sx = static_cast<int>((frame_ * 7) % kPanMargin);
      sy = static_cast<int>((frame_ * 3) % kPanMargin);
    } else {
      uint32_t state = static_cast<uint32_t>(frame_ * 2654435761u + 1u);
      const size_t n = static_cast<size_t>(surfW_) * surfH_;
      for (size_t i = 0; i < n; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        pixels_[i] = state & 0x00FFFFFFu;
      }
    }
    BitBlt(dc, 0, 0, gWinW, gWinH, memDc_, sx, sy, SRCCOPY);
  }
  std::thread thread_;
  std::atomic<bool> ready_{false};
  uint64_t frame_ = 0;
  uint64_t startTick_ = 0;
  HWND hwnd_ = nullptr;
  HDC memDc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  uint32_t* pixels_ = nullptr;
  int surfW_ = 0;
  int surfH_ = 0;
};

// r5: "HH:MM:SS.mmm" at the start of a host log line, in seconds since midnight; -1 if absent.
double log_time_sec(const std::string& line) {
  if (line.size() < 18 || line[8] != ':' || line[11] != ':' || line[14] != '.') return -1.0;
  return std::stoi(line.substr(6, 2)) * 3600.0 + std::stoi(line.substr(9, 2)) * 60.0 +
         std::stoi(line.substr(12, 2)) + std::stoi(line.substr(15, 3)) / 1000.0;
}

std::string value_of(const std::string& line, const std::string& key) {
  const std::string needle = " " + key + "=";
  const size_t at = line.find(needle);
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  const size_t end = line.find(' ', start);
  return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::cout.setf(std::ios::unitbuf);
  if (!host_e2e_allowed()) {
    std::printf("SKIP  abr_client_evidence_e2e_test (starts a listening host)\n");
    std::printf("      Set REMOTE60_ALLOW_HOST_E2E=1 to run it.\n\nRESULT: SKIPPED\n");
    return kE2eSkippedExit;
  }
  std::string mode;
  std::wstring hostExe;
  bool desktop = false;
  bool textOff = false;
  uint32_t bitrate = kDefaultBitrate;
  std::string expectSize;
  int monitor = -1;
  std::wstring keepLog;
  bool cadenceWindow = false;
  std::map<int, std::string> selectAt;  // second -> "window" | "desktop"
  std::string expectFinal;
  std::map<int, uint32_t> tuneAt;  // second -> bitrate
  std::set<int> requestKeyAt;
  int runSec = 0;
  uint32_t keyint = 0;     // r5: 0 = the host's default
  std::wstring maxQpArg;   // r5: empty = the host's default
  std::string patternName = "default";
  bool gdi = false;  // r3: force the GDI desktop backend, staging the real GNLinkCapture.exe worker  // with --desktop: keep the invisible animated window up anyway
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--thumbnail") {  // staged as GNLinkCapture.exe
      Sleep(300000);
      return 0;
    }
    if (a == L"--mode" && i + 1 < argc) {
      const std::wstring m = argv[++i];
      mode.assign(m.begin(), m.end());
    } else if (a == L"--host" && i + 1 < argc) {
      hostExe = argv[++i];
    } else if (a == L"--desktop") {
      desktop = true;
    } else if (a == L"--text-off") {
      textOff = true;
    } else if (a == L"--bitrate" && i + 1 < argc) {
      bitrate = static_cast<uint32_t>(std::wcstoul(argv[++i], nullptr, 10));
    } else if (a == L"--select-at" && i + 1 < argc) {
      const std::wstring v = argv[++i];
      const size_t colon = v.find(L':');
      if (colon != std::wstring::npos) {
        const std::wstring what = v.substr(colon + 1);
        selectAt[static_cast<int>(std::wcstol(v.substr(0, colon).c_str(), nullptr, 10))] =
            std::string(what.begin(), what.end());
      }
    } else if (a == L"--request-key-at" && i + 1 < argc) {
      requestKeyAt.insert(static_cast<int>(std::wcstol(argv[++i], nullptr, 10)));
    } else if (a == L"--run-sec" && i + 1 < argc) {
      runSec = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
    } else if (a == L"--fps" && i + 1 < argc) {
      kFps = static_cast<uint32_t>(std::wcstoul(argv[++i], nullptr, 10));
    } else if (a == L"--keyint" && i + 1 < argc) {
      keyint = static_cast<uint32_t>(std::wcstoul(argv[++i], nullptr, 10));
    } else if (a == L"--max-qp" && i + 1 < argc) {
      maxQpArg = argv[++i];
    } else if (a == L"--pattern" && i + 1 < argc) {
      const std::wstring v = argv[++i];
      patternName.assign(v.begin(), v.end());
      gPattern = v == L"pan" ? Pattern::Pan : (v == L"noise" ? Pattern::Noise : Pattern::Default);
    } else if (a == L"--measure-from" && i + 1 < argc) {
      gMeasureFromSec = std::wcstod(argv[++i], nullptr);
    } else if (a == L"--still-after" && i + 1 < argc) {
      gStillAfterMs = static_cast<int>(std::wcstol(argv[++i], nullptr, 10)) * 1000;
    } else if (a == L"--block" && i + 1 < argc) {
      gBlock = std::max(1, static_cast<int>(std::wcstol(argv[++i], nullptr, 10)));
    } else if (a == L"--window" && i + 1 < argc) {
      const std::wstring v = argv[++i];
      const size_t x = v.find(L'x');
      if (x != std::wstring::npos) {
        gWinW = static_cast<int>(std::wcstol(v.substr(0, x).c_str(), nullptr, 10));
        gWinH = static_cast<int>(std::wcstol(v.substr(x + 1).c_str(), nullptr, 10));
      }
    } else if (a == L"--gdi") {
      gdi = true;
    } else if (a == L"--tune-at" && i + 1 < argc) {
      const std::wstring v = argv[++i];
      const size_t colon = v.find(L':');
      if (colon != std::wstring::npos) {
        tuneAt[static_cast<int>(std::wcstol(v.substr(0, colon).c_str(), nullptr, 10))] =
            static_cast<uint32_t>(std::wcstoul(v.substr(colon + 1).c_str(), nullptr, 10));
      }
    } else if (a == L"--expect-final" && i + 1 < argc) {
      const std::wstring e = argv[++i];
      expectFinal.assign(e.begin(), e.end());
    } else if (a == L"--cadence-window") {
      cadenceWindow = true;
    } else if (a == L"--keep-log" && i + 1 < argc) {
      keepLog = argv[++i];
    } else if (a == L"--monitor" && i + 1 < argc) {
      monitor = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
    } else if (a == L"--expect-size" && i + 1 < argc) {
      const std::wstring e = argv[++i];
      expectSize.assign(e.begin(), e.end());
    }
  }
  if (mode != "none" && mode != "present" && mode != "stop" && mode != "recover") {
    std::printf("usage: --mode none|present|stop|recover [--host <GNLinkStream.exe>]\n");
    return 2;
  }
  std::cout << "mode: " << mode << " capture=" << (desktop ? "desktop" : "test-window")
            << " bitrate=" << bitrate << " textPriority=" << (textOff ? "off" : "on") << "\n";

  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
  kHostPort = remote60::native_poc::e2e::e2e_pick_free_udp_port();
  if (kHostPort == 0) {
    std::printf("FAIL  no free UDP port for the host\n");
    return 1;
  }
  std::printf("host port %u (picked at run time)\n", kHostPort);
  kTargetTitle = remote60::native_poc::e2e::e2e_unique_window_title(L"remote60 abr evidence target");
  // Staged inside the repository's test scratch root, never %TEMP%; removed when main returns,
  // whichever way it returns (RV-20 r2). Only the directory this process created is ever removed.
  remote60::native_poc::e2e::StagingDir staging;
  if (!staging.Create(L"abr_ev")) {
    std::printf("FAIL  %s\n", staging.why().c_str());
    return 1;
  }
  const std::wstring dir = staging.path();
  const std::wstring me = self_path();
  if (hostExe.empty()) hostExe = directory_of(me) + L"GNLinkStream.exe";
  const bool staged = CopyFileW(hostExe.c_str(), (dir + L"GNLinkStream.exe").c_str(), FALSE) &&
                      CopyFileW(gdi ? (directory_of(me) + L"GNLinkCapture.exe").c_str() : me.c_str(),
                                (dir + L"GNLinkCapture.exe").c_str(), FALSE);
  check("a host and a never-answering helper could be staged", staged,
        std::string(hostExe.begin(), hostExe.end()));

  CadenceTarget target;
  target.selectable = !selectAt.empty();
  if (!desktop || cadenceWindow) {
    check("an on-screen, invisible, click-through window is up for the host to capture", target.Start());
  }
  const HWND targetHwnd = target.hwnd();

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
    if (textOff) SetEnvironmentVariableW(L"REMOTE60_NATIVE_TEXT_PRIORITY_DISABLE", L"1");
    if (gdi) SetEnvironmentVariableW(L"REMOTE60_DESKTOP_CAPTURE_BACKEND", L"gdi");
    if (!maxQpArg.empty()) SetEnvironmentVariableW(L"REMOTE60_NATIVE_MAX_QP", maxQpArg.c_str());
    // r5: a stats line every second, so the measured span has one reading per second.
    if (gPattern != Pattern::Default) SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", L"1");
    std::wstring cmd = L"\"" + dir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                       L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(kHostPort) +
                       L" --fps " + std::to_wstring(kFps) + L" --bitrate " + std::to_wstring(bitrate) +
                       (keyint > 0 ? L" --keyint " + std::to_wstring(keyint) : std::wstring()) +
                       L" --seconds " + std::to_wstring(std::max(120, runSec + 30)) +
                       L" --input-injection-mode none" +
                       (desktop ? std::wstring() : remote60::native_poc::e2e::e2e_capture_window_args(kTargetTitle));
    const bool noInjection = cmd.find(L"--input-injection-mode none") != std::wstring::npos;
    check("the host is started with input injection off", noInjection);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = dir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = e2e_isolated_environment(isoAppData);
    std::string isoWhy;
    const bool isoOk = e2e_command_is_isolated(cmd, dir, &isoWhy) &&
                       e2e_path_is_under(e2e_block_localappdata(isoEnv), dir);
    check("the launched process is isolated from the user's files", isoOk, isoWhy);
    launched = noInjection && isoOk &&
               CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                              hostLog != INVALID_HANDLE_VALUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                              isoEnv.data(), dir.c_str(), &si, &hostPi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, hostPi.hProcess);
      ResumeThread(hostPi.hThread);
    }
  }
  check("the host started", launched);

  // Per second of the run: real (non-synthetic) frames completed at this end.
  std::vector<uint32_t> realFramesPerSec;
  std::vector<std::string> selectionLog;
  uint64_t metricsStoppedAtSec = 0;
  if (launched) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in hostAddr{};
    hostAddr.sin_family = AF_INET;
    hostAddr.sin_port = htons(kHostPort);
    InetPtonW(AF_INET, L"127.0.0.1", &hostAddr.sin_addr);
    connect(sock, reinterpret_cast<const sockaddr*>(&hostAddr), sizeof(hostAddr));
    DWORD rcvTimeout = 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
    int rcvBuf = 4 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));

    UdpHelloOptions hello;
    hello.budgetMs = 20000;
    hello.sliceMaxMs = 250;
    hello.retrySleepMs = 50;
    std::string helloError;
    uint32_t ackFeatures = 0;
    const bool handshake = udp_hello_handshake(sock, hello, nullptr, &helloError, &ackFeatures, nullptr);
    check("the viewer's Hello is answered", handshake, helloError);

    UdpControlChannel control;
    control.Configure(
        [&sock](const void* data, size_t len) {
          return send(sock, static_cast<const char*>(data), static_cast<int>(len), 0) > 0;
        },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, 1200);
    UdpControlLink link(&control, 12000);

    std::atomic<bool> stop{false};
    std::mutex fmu;
    std::set<uint32_t> realSeqThisSec;
    // r6: frame age per real frame (same machine, same QPC clock): capture -> encode done, and
    // capture -> received here, for the frames of the measured span.
    std::atomic<bool> latencyOn{false};
    std::vector<uint64_t> capToEncUs, capToRecvUs;
    std::thread ingress([&] {
      std::vector<uint8_t> buf(2048);
      while (!stop.load()) {
        control.Tick();
        const int n = recv(sock, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        if (n > 0 && !control.OnPacket(buf.data(), static_cast<size_t>(n)) &&
            n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
          UdpVideoChunkHeader h{};
          std::memcpy(&h, buf.data(), sizeof(h));
          if (h.magic == kMagic && h.kind == static_cast<uint16_t>(UdpPacketKind::VideoChunk) &&
              h.size == sizeof(UdpVideoChunkHeader) && (h.flags & 0x10u) == 0 &&
              (h.flags & 0x40u) == 0 && (h.flags & 0x4u) != 0) {  // last chunk of a real frame
            std::lock_guard<std::mutex> lk(fmu);
            realSeqThisSec.insert(h.seq);
            if (latencyOn.load() && h.captureQpcUs > 0) {
              const uint64_t now = qpc_now_us();
              if (h.encodeEndQpcUs >= h.captureQpcUs) capToEncUs.push_back(h.encodeEndQpcUs - h.captureQpcUs);
              if (now >= h.captureQpcUs) capToRecvUs.push_back(now - h.captureQpcUs);
            }
          }
        }
      }
    });

    if (desktop && monitor >= 0) {
      // The product message, sent once before the control thread starts, answered with the list.
      ControlOutboundAction select{};
      select.kind = ControlOutboundActionKind::MonitorSelect;
      select.expectedResponseType = MessageType::ControlMonitorList;
      select.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlMonitorListMessage));
      select.monitorSelect.header.magic = kMagic;
      select.monitorSelect.header.type = static_cast<uint16_t>(MessageType::ControlMonitorSelect);
      select.monitorSelect.header.size = static_cast<uint16_t>(sizeof(select.monitorSelect));
      select.monitorSelect.seq = 1;
      select.monitorSelect.monitorId = static_cast<uint32_t>(monitor);
      select.monitorSelect.clientSendQpcUs = qpc_now_us();
      TcpControlResponse response;
      const bool selected = execute_control_action(link, select, &response);
      check("monitor " + std::to_string(monitor) + " was selected", selected);
      std::this_thread::sleep_for(std::chrono::seconds(2));  // the reattach and its keyframe
    }

    // What this viewer reports, per mode, set once a second by the loop below.
    std::mutex mmu;
    ClientControlMetricsSnapshot pendingMetrics{};
    bool pendingSelectSet = false;
    uint32_t pendingTune = 0;
    bool pendingKeyRequest = false;
    uint64_t pendingSelect = 0;
    std::string pendingSelectKind;  // what the run asked for, not what the id happens to mean
    std::mutex selMu;
    std::vector<std::string> selections;  // what the host answered, in order
    std::atomic<bool> controlStop{false};
    std::thread controlThread([&] {
      ClientControlScheduler scheduler;
      WindowPanelStateModel windowPanel;
      StreamStateControl streamState;
      CaptureModeRequestState captureMode;
      KeyframeRequestState keyframe(120000, 300000, 3);
      RuntimeTuneState runtimeTune(300000, 30000000, 250000, 1, 240);
      ClientInputQueue inputQueue;
      scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      while (!controlStop.load()) {
        ClientControlMetricsSnapshot metrics;
        bool selectNow = false;
        uint64_t selectId = 0;
        std::string selectKind;
        uint32_t tuneNow = 0;
        bool keyNow = false;
        {
          std::lock_guard<std::mutex> lk(mmu);
          metrics = pendingMetrics;
          tuneNow = pendingTune;
          pendingTune = 0;
          keyNow = pendingKeyRequest;
          pendingKeyRequest = false;
          selectNow = pendingSelectSet;
          selectId = pendingSelect;
          selectKind = pendingSelectKind;
          pendingSelectSet = false;
        }
        if (keyNow) (void)keyframe.Request(2, qpc_now_us());
        if (tuneNow != 0) {
          runtimeTune.SetEnabled(true);
          runtimeTune.SetTargets(tuneNow, 0, kFps);
        }
        if (selectNow) {
          // The product message a viewer sends when the user picks a window (its id) or the
          // desktop (windowId 0).
          ControlOutboundAction select{};
          select.kind = ControlOutboundActionKind::WindowSelect;
          select.expectedResponseType = MessageType::ControlWindowSelected;
          select.expectedResponseSize = static_cast<uint16_t>(sizeof(ControlWindowSelectedMessage));
          select.windowSelect.header.magic = kMagic;
          select.windowSelect.header.type = static_cast<uint16_t>(MessageType::ControlWindowSelect);
          select.windowSelect.header.size = static_cast<uint16_t>(sizeof(select.windowSelect));
          select.windowSelect.seq = 9001;
          select.windowSelect.windowId = selectId;
          select.windowSelect.clientSendQpcUs = qpc_now_us();
          TcpControlResponse response;
          const bool ok = execute_control_action(link, select, &response);
          const auto& w = response.windowSelected;
          std::lock_guard<std::mutex> lk(selMu);
          selections.push_back(selectKind + " sent=" +
                               (ok ? "1" : "0") + " flags=" + std::to_string(w.flags) +
                               " asked=" + std::to_string(selectId) + " answered=" +
                               std::to_string(w.windowId) + " reason=" +
                               std::string(w.reason, strnlen(w.reason, sizeof(w.reason))));
        }
        ControlOutboundAction action{};
        const uint64_t now = qpc_now_us();
        if (scheduler.NextAction(now, metrics, &windowPanel, &streamState, &captureMode, &keyframe,
                                 &runtimeTune, &inputQueue, &action, nullptr, nullptr)) {
          TcpControlResponse response;
          if (execute_control_action(link, action, &response) &&
              action.kind == ControlOutboundActionKind::Ping) {
            scheduler.OnPingCompleted(qpc_now_us());
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      }
    });

    // The run: 36 seconds. 'stop' reports fully for the first 14 and is silent after.
    const int kRunSec = runSec > 0 ? runSec : (mode == "recover" ? 72 : 36);
    constexpr int kStopAfterSec = 14;
    constexpr int kResumeAtSec = 26;  // recover: silent from 14 to 26
    for (int s = 0; s < kRunSec; ++s) {
      const uint64_t now = qpc_now_us();
      ClientControlMetricsSnapshot m{};
      if (mode == "present") {
        // Exactly what the APK's session fills: the present* block and nothing else.
        m.message.presentTargetIntervalUs = 1000000 / kFps;
        m.message.presentFpsX100 = kFps * 100;
        m.message.presentGapP50Us = 1000000 / kFps;
        m.message.presentGapP95Us = 1000000 / kFps + 4000;
        m.message.presentGapMaxUs = 1000000 / kFps + 9000;
        m.message.presentSampleCount = kFps;
        m.message.presentDisplayedCount = kFps;
        m.updatedQpcUs = now;
      } else if ((mode == "stop" && s < kStopAfterSec) ||
                 (mode == "recover" && (s < kStopAfterSec || s >= kResumeAtSec))) {
        // A healthy Windows-style report: decode fields present and fine.
        m.message.width = 480;
        m.message.height = 270;
        m.message.recvFpsX100 = kFps * 100;
        m.message.decodedFpsX100 = kFps * 100;
        m.message.recvMbpsX1000 = 3000;
        m.message.avgLatencyUs = 30000;
        m.message.maxLatencyUs = 45000;
        m.message.avgDecodeTailUs = 8000;
        m.message.maxDecodeTailUs = 15000;
        m.updatedQpcUs = now;
      }
      if (mode == "stop" && s == kStopAfterSec) metricsStoppedAtSec = static_cast<uint64_t>(s);
      if (requestKeyAt.count(s) != 0) {
        std::lock_guard<std::mutex> lk(mmu);
        pendingKeyRequest = true;
      }
      if (tuneAt.count(s) != 0) {
        std::lock_guard<std::mutex> lk(mmu);
        pendingTune = tuneAt[s];
      }
      if (selectAt.count(s) != 0) {
        std::lock_guard<std::mutex> lk(mmu);
        pendingSelect = selectAt[s] == "window" ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(target.hwnd()))
                                                : 0ull;
        pendingSelectKind = selectAt[s];
        pendingSelectSet = true;
      }
      {
        std::lock_guard<std::mutex> lk(mmu);
        pendingMetrics = m;
      }
      if (s + 1 >= static_cast<int>(gMeasureFromSec)) latencyOn.store(true);
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::lock_guard<std::mutex> lk(fmu);
      realFramesPerSec.push_back(static_cast<uint32_t>(realSeqThisSec.size()));
      realSeqThisSec.clear();
    }

    controlStop.store(true);
    if (controlThread.joinable()) controlThread.join();
    if (gPattern != Pattern::Default) {
      std::lock_guard<std::mutex> lk(fmu);
      auto stat = [](std::vector<uint64_t> v, const char* name) {
        if (v.empty()) return std::string(name) + "=none";
        std::sort(v.begin(), v.end());
        uint64_t sum = 0;
        for (uint64_t x : v) sum += x;
        char b[160];
        std::snprintf(b, sizeof(b), "%sAvgMs=%.1f %sP95Ms=%.1f", name, sum / 1000.0 / v.size(), name,
                      v[v.size() * 95 / 100] / 1000.0);
        return std::string(b);
      };
      std::cout << "R6LAT frames=" << capToRecvUs.size() << " " << stat(capToEncUs, "capToEnc") << " "
                << stat(capToRecvUs, "capToRecv") << "\n";
    }
    {
      std::lock_guard<std::mutex> lk(selMu);
      selectionLog = selections;
    }
    stop.store(true);
    if (ingress.joinable()) ingress.join();
    closesocket(sock);
  }

  if (!desktop || cadenceWindow) target.Stop();
  if (hostPi.hProcess) {
    TerminateProcess(hostPi.hProcess, 0);
    WaitForSingleObject(hostPi.hProcess, 5000);
    CloseHandle(hostPi.hProcess);
  }
  if (hostPi.hThread) CloseHandle(hostPi.hThread);
  if (hostLog != INVALID_HANDLE_VALUE) CloseHandle(hostLog);
  CloseHandle(job);

  if (launched && !desktop) {
    std::string captureLine;
    const bool own = remote60::native_poc::e2e::e2e_host_captured_window(hostLogPath, targetHwnd, &captureLine);
    std::printf("capture: %s (this pid %lu)\n", captureLine.c_str(), static_cast<unsigned long>(GetCurrentProcessId()));
    check("the host captured the window this test paints (its hwnd, this pid), not another test's", own, captureLine);
  }

  // ------------------------------------------------------------------ the host's own account
  std::vector<std::string> abrLines, keyLines;
  std::string lastStats;
  {
    std::ifstream in(hostLogPath);
    std::string line;
    while (std::getline(in, line)) {
      if (line.find("[abr] profile=") != std::string::npos) abrLines.push_back(line);
      if (line.find("[keyframe] seq=") != std::string::npos) keyLines.push_back(line);
      if (line.find(" udpTxWireEstBytes=") != std::string::npos) lastStats = line;
    }
  }

  std::string perSec;
  uint32_t cadenceSeconds = 0;
  for (size_t i = 0; i < realFramesPerSec.size(); ++i) {
    perSec += std::to_string(realFramesPerSec[i]) + " ";
    if (i >= 4 && realFramesPerSec[i] >= kFps / 2) ++cadenceSeconds;
  }
  std::cout << "real frames per second at this end: " << perSec << "\n";
  const size_t judged = realFramesPerSec.size() > 4 ? realFramesPerSec.size() - 4 : 0;
  const bool cadenceOk = launched && judged > 0 && cadenceSeconds * 10 >= judged * 8;
  std::cout << "seconds after warmup with a real cadence (>= " << kFps / 2 << "): " << cadenceSeconds
            << " of " << judged << "\n";

  std::cout << "\n--- host [abr] lines (" << abrLines.size() << ") ---\n";
  for (const auto& l : abrLines) std::cout << "  " << l << "\n";
  std::cout << "\n--- host [keyframe] lines (" << keyLines.size() << ") ---\n";
  for (const auto& l : keyLines) std::cout << "  " << l << "\n";
  if (!lastStats.empty()) {
    std::cout << "\n--- per-flow bytes, last stats line ---\n";
    for (const char* k : {"udpTxBytes", "udpTxParityBytes", "udpTxChunkHdrBytes", "udpTxVideoDatagrams",
                          "udpTxNackBytes", "udpTxNackDatagrams", "ctlTxBytes", "ctlTxDatagrams",
                          "udpTxIpUdpHdrEstBytes", "udpTxWireEstBytes"}) {
      std::cout << "  " << k << "=" << value_of(lastStats, k) << "\n";
    }
  }

  // quality r2: what the host actually encoded -- its stats line (size=) and every [keyframe] line.
  std::string encodedSize = value_of(lastStats, "size");
  std::set<std::string> keySizes;
  for (const auto& l : keyLines) keySizes.insert(value_of(l, "size"));
  std::string keySizeList;
  for (const auto& k : keySizes) keySizeList += k + " ";
  std::cout << "\nencoded size (last stats line): " << encodedSize << "  keyframe sizes: " << keySizeList << "\n";
  if (launched && !expectSize.empty()) {
    check("the host encoded " + expectSize + " (stats line and every keyframe)",
          encodedSize == expectSize && keySizes.size() == 1 && *keySizes.begin() == expectSize,
          "stats=" + encodedSize + " keys=" + keySizeList);
  }

  if (launched && !selectAt.empty()) {
    bool allOk = selectionLog.size() == selectAt.size();
    for (const auto& l : selectionLog) {
      std::cout << "selection: " << l << "\n";
      allOk = allOk && l.find("sent=1 flags=1 ") != std::string::npos;
      // r4 (r3 review): a "window" selection must really name this test's window -- without
      // --cadence-window it would be sent as id 0, the desktop, and pass.
      if (l.rfind("window ", 0) == 0) {
        const std::string asked = value_of(" " + l, "asked");
        allOk = allOk && asked != "0" && !asked.empty() && value_of(" " + l, "answered") == asked;
      }
    }
    check("every selection was accepted by the host (flags bit0)", allOk,
          std::to_string(selectionLog.size()) + " of " + std::to_string(selectAt.size()));
  }
  // r4: keyframes by reason (a key with several reasons counts under each).
  {
    std::map<std::string, int> byReason;
    uint64_t keyBytes = 0;
    for (const auto& l : keyLines) {
      std::string r = value_of(l, "reasons");
      const std::string b = value_of(l, "bytes");
      if (!b.empty()) keyBytes += std::stoull(b);
      size_t start = 0;
      while (start <= r.size()) {
        const size_t bar = r.find('|', start);
        byReason[r.substr(start, bar == std::string::npos ? std::string::npos : bar - start)] += 1;
        if (bar == std::string::npos) break;
        start = bar + 1;
      }
    }
    std::cout << "keyframes: " << keyLines.size() << " total, " << keyBytes << " bytes;";
    for (const auto& [reason, n] : byReason) std::cout << " " << reason << "=" << n;
    std::cout << "\n";
    if (launched && !requestKeyAt.empty()) {
      const int viewerKeys = byReason.count("viewer") ? byReason["viewer"] : 0;
      check("every viewer keyframe request was answered with an IDR (" +
                std::to_string(requestKeyAt.size()) + " asked)",
            viewerKeys >= static_cast<int>(requestKeyAt.size()),
            "viewer-reason keys=" + std::to_string(viewerKeys));
    }
  }
  // r5: the rate the encoder actually produced, from the host's own cumulative counters, over the
  // stats lines after an 8 s warmup (encoder start, first IDR, ramp).
  if (launched) {
    std::vector<std::string> statsLines;
    std::string rcLine, gopLine;
    {
      std::ifstream in(hostLogPath);
      std::string line;
      while (std::getline(in, line)) {
        if (line.find(" udpTxWireEstBytes=") != std::string::npos) statsLines.push_back(line);
        if (line.find("h264 rate-control") != std::string::npos) rcLine = line;
        if (line.find("h264 gop-config") != std::string::npos) gopLine = line;
      }
    }
    const double t0 = statsLines.empty() ? -1.0 : log_time_sec(statsLines.front());
    size_t a = 0;
    while (a < statsLines.size() && log_time_sec(statsLines[a]) < t0 + gMeasureFromSec) ++a;
    if (a + 1 < statsLines.size()) {
      const std::string& A = statsLines[a];
      const std::string& B = statsLines.back();
      const double span = log_time_sec(B) - log_time_sec(A);
      auto mbps = [&](const char* key) {
        const std::string x = value_of(A, key), y = value_of(B, key);
        if (x.empty() || y.empty() || span <= 0) return -1.0;
        return (std::stod(y) - std::stod(x)) * 8.0 / span / 1e6;
      };
      int keysInSpan = 0;
      std::map<std::string, int> spanReasons;
      for (const auto& l : keyLines) {
        const double t = log_time_sec(l);
        if (t > log_time_sec(A) && t <= log_time_sec(B)) {
          ++keysInSpan;
          spanReasons[value_of(l, "reasons")] += 1;
        }
      }
      std::string reasons;
      for (const auto& [r, n] : spanReasons) reasons += r + ":" + std::to_string(n) + ",";
      const double payload = mbps("udpTxBytes");
      std::cout << std::fixed;
      std::cout.precision(2);
      std::cout << "R5MEASURE pattern=" << patternName << " block=" << gBlock << " fps=" << kFps << " keyint=" << keyint
                << " maxQpArg=" << std::string(maxQpArg.begin(), maxQpArg.end())
                << " encode=" << value_of(B, "size") << " span=" << span
                << " targetMbps=" << bitrate / 1e6 << " payloadMbps=" << payload
                << " ratio=" << (payload / (bitrate / 1e6))
                << " parityMbps=" << mbps("udpTxParityBytes")
                << " chunkHdrMbps=" << mbps("udpTxChunkHdrBytes")
                << " ipUdpHdrMbps=" << mbps("udpTxIpUdpHdrEstBytes")
                << " nackMbps=" << mbps("udpTxNackBytes")
                << " wireMbps=" << mbps("udpTxWireEstBytes")
                << " keys=" << keysInSpan << " keysPerSec=" << (span > 0 ? keysInSpan / span : 0.0)
                << " keyReasons=" << reasons << "\n";
      auto tail = [](const std::string& l) {
        const size_t at = l.find("h264");
        return at == std::string::npos ? std::string("(missing)") : l.substr(at);
      };
      std::cout << "R5RC " << tail(rcLine) << "\n";
      std::cout << "R5GOP " << tail(gopLine) << "\n";
    } else {
      std::cout << "R5MEASURE none: not enough stats lines after warmup\n";
    }
  }

  if (launched && !expectFinal.empty()) {
    check("after the last selection the host encodes " + expectFinal + " (last stats line)",
          encodedSize == expectFinal, "stats=" + encodedSize);
  }

  if (launched) {
    // Instrumentation (A), independent of the cadence.
    bool keyReasoned = !keyLines.empty();
    for (const auto& l : keyLines) keyReasoned = keyReasoned && !value_of(l, "reasons").empty();
    check("every [keyframe] line names its reasons", keyReasoned, std::to_string(keyLines.size()) + " lines");
    const std::string parity = value_of(lastStats, "udpTxParityBytes");
    const std::string wire = value_of(lastStats, "udpTxWireEstBytes");
    const std::string payload = value_of(lastStats, "udpTxBytes");
    const bool flows = !parity.empty() && !wire.empty() && !payload.empty() &&
                       std::stoull(parity) > 0 && std::stoull(wire) > std::stoull(payload);
    check("the stats line carries the per-flow bytes, and the wire estimate exceeds the payload", flows,
          "payload=" + payload + " parity=" + parity + " wire=" + wire);

    if (!cadenceOk) {
      skip("ABR verdict for mode " + mode, "the host did not send a real cadence -- NOT JUDGED");
    } else if (mode == "none" || mode == "present") {
      check("ABR did not demote a client whose decode evidence " +
                std::string(mode == "none" ? "never arrived" : "was never reported"),
            abrLines.empty(), abrLines.empty() ? "" : abrLines.front());
    } else if (mode == "recover") {
      bool demoted = false;
      bool recoveredHigh = false;
      std::string last;
      for (const auto& l : abrLines) {
        if (value_of(l, "reason").find("severe") != std::string::npos) demoted = true;
        if (demoted && value_of(l, "profile") == "high") recoveredHigh = true;
        last = l;
      }
      check("ABR demoted during the silence and climbed back to high after it", demoted && recoveredHigh,
            std::to_string(abrLines.size()) + " [abr] lines");
      if (!expectSize.empty()) {
        check("...and the high profile it returned to is " + expectSize + " at " + std::to_string(kFps) + " fps",
              value_of(last, "encode") == expectSize && value_of(last, "fps") == std::to_string(kFps),
              last);
      }
    } else {
      bool staleDemote = false;
      for (const auto& l : abrLines) {
        if (value_of(l, "evidence").find("stale_active") != std::string::npos &&
            value_of(l, "reason").find("severe") != std::string::npos) {
          staleDemote = true;
        }
      }
      check("ABR demoted a client that reported and then went silent (stale_active, P7 kept)",
            staleDemote, std::to_string(abrLines.size()) + " [abr] lines");
    }
  }
  (void)metricsStoppedAtSec;

  if (!keepLog.empty()) CopyFileW(hostLogPath.c_str(), keepLog.c_str(), FALSE);
  check("the scratch directory is cleaned up", staging.Remove(), staging.why());
  WSACleanup();
  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
