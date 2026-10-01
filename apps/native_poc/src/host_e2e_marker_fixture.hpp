#pragma once

// Shared e2e fixture for the bitrate-hard-cap verification tests (r8): the invisible content window
// that paints desktop-like scenes and an event-ID marker, the event log (paint QPC per id), and the
// marker reader for a decoded NV12 Y plane. Used by both fec_single_chunk_host_e2e_test (component
// smoke + paired latency) and viewer_udp_recovery_test (real VideoReceiver smoke). No gate logic here.

#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <string>

#include "e2e_isolation.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc::e2emarker {
using remote60::native_poc::qpc_now_us;

// A small poll-until (same as control_resume_e2e_support.hpp's), kept local so this header needs no
// e2e-support dependency.
inline bool wait_until(const std::function<bool()>& done, int budgetMs) {
  const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
  while (GetTickCount() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return done();
}

constexpr int kWinW = 640;
constexpr int kWinH = 360;

// StaticText/LowMotion/Video are the original synthetic cases. TextScroll/WindowDrag/PartialVideo are
// the r4-addendum "real desktop-like" cases the verifier asked for: a structured text page that
// scrolls, a static page with an opaque window dragged across it, and a static page with a small
// localised video (noise) region -- the mostly-static-plus-local-motion profile of actual desktop use,
// which exercises the cap's quality trade-off far more realistically than full-frame noise.
// ... plus r5 measurement cases: SingleChange (a static page with ONE localised change every ~2 s
// after idle -- for input->screen present latency, measured on the change frame, not an average that a
// backlog inflates) and FullTransition (the whole page content swaps every ~2 s -- a full-screen change).
enum class Content {
  StaticText, LowMotion, Video, TextScroll, WindowDrag, PartialVideo, SingleChange, FullTransition
};
inline const char* content_name(Content c) {
  switch (c) {
    case Content::StaticText: return "static";
    case Content::LowMotion: return "lowmotion";
    case Content::Video: return "video";
    case Content::TextScroll: return "textscroll";
    case Content::WindowDrag: return "windowdrag";
    case Content::PartialVideo: return "partialvideo";
    case Content::SingleChange: return "singlechange";
    case Content::FullTransition: return "fulltransition";
  }
  return "?";
}

// ----------------------------------------------------------------------- event-ID latency (r6 H3)
//
// To measure the REAL change->decoded-picture response (not "max capture->decode over all AUs", which
// an idle refresh frame or a mis-mapped async output inflates), the discrete-change fixtures stamp an
// event-ID marker into the painted pixels and record that event's paint QPC here. The decoder output's
// Y plane is read back, the marker decoded, and the FIRST decoded frame carrying event E is matched to
// its paint QPC -- so the latency is paint(E) -> decoded-picture(E), event-matched, provenance-robust.
// 9 high-contrast 40 px cells at the top-left: cell 0 a sync (always white), cells 1..8 the id bits.
constexpr int kEvCell = 40;
constexpr int kEvCells = 9;  // 1 sync + 8 bits
struct EventLog {
  std::mutex mu;
  std::map<uint32_t, uint64_t> paintQpc;  // eventId -> paint QPC (same clock as the decoder)
  uint32_t next = 1;
  uint32_t Allocate(uint64_t qpc) {
    std::lock_guard<std::mutex> lk(mu);
    const uint32_t id = next++;
    paintQpc[id] = qpc;
    return id;
  }
  uint64_t PaintQpc(uint32_t id) {
    std::lock_guard<std::mutex> lk(mu);
    auto it = paintQpc.find(id);
    return it == paintQpc.end() ? 0 : it->second;
  }
  uint32_t Count() {
    std::lock_guard<std::mutex> lk(mu);
    return next - 1;
  }
};

// Read the marker out of a decoded NV12 Y plane (coded stride = width; visible origin applied). Returns
// 0 if the sync cell is not clearly white (no valid marker in this frame). H264 preserves the large
// high-contrast cells; a center sample per cell thresholded at mid-luma survives both rates.
inline uint32_t decode_event_marker(const uint8_t* y, uint32_t codedW, uint32_t codedH, uint32_t visLeft,
                                    uint32_t visTop) {
  if (!y || codedW == 0 || codedH == 0) return 0;
  const uint32_t cy = visTop + kEvCell / 2;
  auto sample = [&](int cell) -> int {
    const uint32_t cx = visLeft + static_cast<uint32_t>(cell) * kEvCell + kEvCell / 2;
    if (cx >= codedW || cy >= codedH) return -1;
    return y[static_cast<size_t>(cy) * codedW + cx];
  };
  const int sync = sample(0);
  if (sync < 160) return 0;  // sync cell must be clearly white -> a marker is present
  uint32_t id = 0;
  for (int b = 0; b < 8; ++b) {
    const int v = sample(b + 1);
    if (v < 0) return 0;
    if (v > 128) id |= (1u << b);
  }
  return id;
}

// ---------------------------------------------------------------------------- the target

// An on-screen window the user cannot see or hit: alpha 1/255, WS_EX_TRANSPARENT, never activated,
// tool window (not in the host's window list, found by title). On screen because an off-screen
// window is never composed and the host then sends only synthetic refresh frames.
class Target {
 public:
  Content content = Content::StaticText;
  std::wstring title;
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
  static uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  // The page: light background, lines of dark glyph-like cells. Identical every frame.
  void PaintPage(HDC dc) {
    RECT client{0, 0, kWinW, kWinH};
    HBRUSH back = CreateSolidBrush(RGB(236, 236, 230));
    FillRect(dc, &client, back);
    DeleteObject(back);
    uint32_t s = 0x1234567u;
    for (int line = 0; line < 22; ++line) {
      const int top = 12 + line * 15;
      for (int col = 0; col < 76; ++col) {
        const uint32_t r = xorshift(s);
        if ((r & 7u) == 0) continue;
        const int left = 10 + col * 8;
        for (int yy = top; yy < top + 10; ++yy) {
          for (int xx = left; xx < left + 6; ++xx) {
            const bool ink = ((r >> ((yy - top) % 8)) & 1u) || ((xx - left) == 2);
            if (ink) SetPixelV(dc, xx, yy, RGB(30, 30, 40));
          }
        }
      }
    }
  }
  void Paint(HDC dc) {
    EnsureSurface(dc);
    if (!memDc_) return;
    if (!pagePainted_) {
      PaintPage(memDc_);
      pagePainted_ = true;
    }
    if (content == Content::LowMotion) {
      // The caret cell and the block's track are repainted from the page, then the moving bits.
      HBRUSH back = CreateSolidBrush(RGB(236, 236, 230));
      RECT caret{620, 12, 623, 24};
      RECT track{0, kWinH - 20, kWinW, kWinH - 4};
      FillRect(memDc_, &caret, back);
      FillRect(memDc_, &track, back);
      DeleteObject(back);
      if ((frame_ / 15) % 2 == 0) {  // blinks every half second at 30 Hz
        HBRUSH ink = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(memDc_, &caret, ink);
        DeleteObject(ink);
      }
      const int bx = static_cast<int>((frame_ * 2) % (kWinW - 16));
      RECT block{bx, kWinH - 20, bx + 16, kWinH - 4};
      HBRUSH grey = CreateSolidBrush(RGB(90, 90, 90));
      FillRect(memDc_, &block, grey);
      DeleteObject(grey);
    } else if (content == Content::Video) {
      uint32_t state = static_cast<uint32_t>(frame_ * 2654435761u + 1u);
      for (int y = kWinH / 4; y < kWinH * 3 / 4; ++y) {
        uint32_t* row = pixels_ + static_cast<size_t>(y) * kWinW;
        for (int x = 0; x < kWinW; ++x) {
          state ^= state << 13;
          state ^= state >> 17;
          state ^= state << 5;
          row[x] = state & 0x00FFFFFFu;
        }
      }
      const int barX = static_cast<int>((frame_ * 12) % (kWinW - 32));
      for (int y = 0; y < kWinH / 4; ++y) {
        uint32_t* row = pixels_ + static_cast<size_t>(y) * kWinW;
        for (int x = 0; x < kWinW; ++x) row[x] = (x >= barX && x < barX + 32) ? 0x00F0F0F0u : 0x00ECECE6u;
      }
    } else if (content == Content::TextScroll) {
      // The whole structured page scrolls vertically -- a full-frame change, but of real text, not
      // noise. Composited by blitting the cached page at a wrapping offset (fast, no per-pixel work).
      EnsurePageCache(dc);
      const int off = static_cast<int>((frame_ * 4) % static_cast<uint64_t>(kWinH));
      BitBlt(memDc_, 0, 0, kWinW, kWinH - off, pageDc_, 0, off, SRCCOPY);
      if (off > 0) BitBlt(memDc_, 0, kWinH - off, kWinW, off, pageDc_, 0, 0, SRCCOPY);
    } else if (content == Content::WindowDrag) {
      // A static text page with one opaque "window" (title bar + body) dragged diagonally across it --
      // mostly static, a localised moving rectangle. Restore the clean page first (blit), then draw it.
      EnsurePageCache(dc);
      BitBlt(memDc_, 0, 0, kWinW, kWinH, pageDc_, 0, 0, SRCCOPY);
      const int span = kWinW - 360;
      int wx = static_cast<int>((frame_ * 6) % static_cast<uint64_t>(span * 2));
      if (wx >= span) wx = span * 2 - wx;  // bounce
      const int wy = 40 + (wx / 3);
      RECT body{wx, wy, wx + 340, wy + 200};
      HBRUSH bodyB = CreateSolidBrush(RGB(250, 250, 252));
      FillRect(memDc_, &body, bodyB);
      DeleteObject(bodyB);
      RECT title{wx, wy, wx + 340, wy + 24};
      HBRUSH titleB = CreateSolidBrush(RGB(48, 96, 180));
      FillRect(memDc_, &title, titleB);
      DeleteObject(titleB);
      RECT frameTop{wx, wy, wx + 340, wy + 1};
      HBRUSH edge = CreateSolidBrush(RGB(20, 20, 30));
      FrameRect(memDc_, &body, edge);
      FillRect(memDc_, &frameTop, edge);
      DeleteObject(edge);
    } else if (content == Content::PartialVideo) {
      // A static text page with a small localised video region (noise) -- e.g. a video playing in a
      // corner of a document. Restore the clean page (blit), then repaint only the small noise rect.
      EnsurePageCache(dc);
      BitBlt(memDc_, 0, 0, kWinW, kWinH, pageDc_, 0, 0, SRCCOPY);
      GdiFlush();  // commit the restore blit before writing the noise region into memDc_'s DIB bits
      const int vx = kWinW - 340, vy = kWinH - 260, vw = 320, vh = 240;
      uint32_t state = static_cast<uint32_t>(frame_ * 2654435761u + 1u);
      for (int y = vy; y < vy + vh; ++y) {
        uint32_t* row = pixels_ + static_cast<size_t>(y) * kWinW;
        for (int x = vx; x < vx + vw; ++x) {
          state ^= state << 13;
          state ^= state >> 17;
          state ^= state << 5;
          row[x] = state & 0x00FFFFFFu;
        }
      }
    } else if (content == Content::SingleChange) {
      // A static page; each transition step is ONE change -- an event-ID marker that increments. The
      // marker (r6 H3) is the single localised change, carrying the event id into the pixels so the
      // decoder output can be matched to the paint QPC.
      EnsurePageCache(dc);
      BitBlt(memDc_, 0, 0, kWinW, kWinH, pageDc_, 0, 0, SRCCOPY);
      GdiFlush();
      DrawEventMarker();
    } else if (content == Content::FullTransition) {
      // The whole content swaps between two very different pages every ~2 s: a full-screen change,
      // overlaid with the same event-ID marker.
      EnsurePageCache(dc);
      if ((frame_ / 60) % 2 == 0) {
        BitBlt(memDc_, 0, 0, kWinW, kWinH, pageDc_, 0, 0, SRCCOPY);  // the text page
      } else {
        RECT all{0, 0, kWinW, kWinH};
        HBRUSH bg = CreateSolidBrush(RGB(24, 28, 40));
        FillRect(memDc_, &all, bg);
        DeleteObject(bg);
        HBRUSH fg = CreateSolidBrush(RGB(220, 210, 160));
        for (int by = 20; by < kWinH - 20; by += 40)
          for (int bx = 20; bx < kWinW - 20; bx += 60) {
            RECT b{bx, by, bx + 44, by + 26};
            FillRect(memDc_, &b, fg);
          }
        DeleteObject(fg);
      }
      GdiFlush();
      DrawEventMarker();
    }
    BitBlt(dc, 0, 0, kWinW, kWinH, memDc_, 0, 0, SRCCOPY);
  }

  // Draw the event-ID marker into the DIB (r6 H3). A new id is allocated once per transition STEP
  // (frame_/60), recording the paint QPC; the marker persists (redrawn) until the next step.
  void DrawEventMarker() {
    if (!events || !pixels_) return;
    const int step = static_cast<int>(frame_ / 60);
    if (step != markerStep_ || markerId_ == 0) {
      markerStep_ = step;
      markerId_ = events->Allocate(static_cast<uint64_t>(qpc_now_us()));
    }
    auto fillCell = [&](int cell, bool white) {
      const uint32_t color = white ? 0x00FFFFFFu : 0x00000000u;
      for (int yy = 2; yy < kEvCell - 2; ++yy) {
        uint32_t* row = pixels_ + static_cast<size_t>(yy) * kWinW;
        for (int xx = cell * kEvCell + 2; xx < cell * kEvCell + kEvCell - 2 && xx < kWinW; ++xx) row[xx] = color;
      }
    };
    fillCell(0, true);  // sync cell
    for (int b = 0; b < 8; ++b) fillCell(b + 1, (markerId_ >> b) & 1u);
  }
  void EnsureSurface(HDC dc) {
    if (memDc_) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kWinW;
    bi.bmiHeader.biHeight = -kWinH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    memDc_ = CreateCompatibleDC(dc);
    bitmap_ = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
    SelectObject(memDc_, bitmap_);
  }
  // A cached clean text page the real-desktop content types composite from each frame (scroll blit,
  // window-drag restore, partial-video restore) -- painting the page is per-pixel and far too slow to
  // redo every frame, so it is rendered once here and only blitted after.
  void EnsurePageCache(HDC dc) {
    if (pageDc_) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kWinW;
    bi.bmiHeader.biHeight = -kWinH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    pageDc_ = CreateCompatibleDC(dc);
    pageBitmap_ = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, nullptr, nullptr, 0);
    SelectObject(pageDc_, pageBitmap_);
    PaintPage(pageDc_);
    GdiFlush();
  }
  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Target*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_TIMER && self) {
      if (self->content != Content::StaticText) {
        ++self->frame_;
        // Continuous-motion content repaints every tick; the measurement cases (SingleChange,
        // FullTransition) stay idle and repaint ONLY at a transition boundary (~2 s), so the host
        // sees genuine idle between discrete changes -- that is what makes the present-latency of the
        // change frame a real input->screen measure rather than a steady cadence.
        const bool discrete =
            self->content == Content::SingleChange || self->content == Content::FullTransition;
        const bool transitionTick = (self->frame_ % 60) == 0;  // ~2 s at the 33 ms timer
        if (!discrete || transitionTick) InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    if (msg == WM_PAINT && self) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      self->Paint(dc);
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
    wc.lpszClassName = L"Remote60FecSingleChunkTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            wc.lpszClassName, title.c_str(), WS_POPUP, 0, 0, kWinW, kWinH, nullptr, nullptr,
                            wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      timeBeginPeriod(1);
      SetTimer(hwnd_, 1, 33, nullptr);  // 30 Hz, the host's capture rate
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    if (memDc_) DeleteDC(memDc_);
    if (bitmap_) DeleteObject(bitmap_);
    if (pageDc_) DeleteDC(pageDc_);
    if (pageBitmap_) DeleteObject(pageBitmap_);
    hwnd_ = nullptr;
  }
  std::thread thread_;
  std::atomic<bool> ready_{false};
  uint64_t frame_ = 0;
  bool pagePainted_ = false;
  HWND hwnd_ = nullptr;
  HDC memDc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  uint32_t* pixels_ = nullptr;
  HDC pageDc_ = nullptr;      // cached clean page for the real-desktop content types
  HBITMAP pageBitmap_ = nullptr;

 public:
  EventLog* events = nullptr;  // r6 H3: where DrawEventMarker records (eventId -> paint QPC)
 private:
  int markerStep_ = -1;
  uint32_t markerId_ = 0;
};

// A self-contained isolated-host launcher that captures the marker Target window (r8). Used by the
// real-VideoReceiver smoke; the fec e2e test keeps its own inline spawn. Same isolation guarantees as
// e2e_isolation.hpp (isolated LOCALAPPDATA, no input injection, kill-on-close job).
struct MarkerHost {
  Target target;
  EventLog eventLog;
  HANDLE job = nullptr;
  PROCESS_INFORMATION pi{};
  HANDLE hostLog = INVALID_HANDLE_VALUE;
  std::wstring hostLogPath;
  HWND targetHwnd = nullptr;

  bool Start(const std::wstring& hostExe, const std::wstring& runDir, uint16_t port, int seconds,
             uint32_t bitrate, uint32_t fps, Content content, bool wireCapOn, const std::wstring& selfExe) {
    CreateDirectoryW(runDir.c_str(), nullptr);
    if (!CopyFileW(hostExe.c_str(), (runDir + L"GNLinkStream.exe").c_str(), FALSE) ||
        !CopyFileW(selfExe.c_str(), (runDir + L"GNLinkCapture.exe").c_str(), FALSE))
      return false;
    target.events = &eventLog;
    target.content = content;
    target.title = L"remote60 marker target " + std::to_wstring(GetCurrentProcessId()) + L" " +
                   std::to_wstring(static_cast<int>(content)) + L" " + std::to_wstring(port);
    if (!target.Start()) return false;
    targetHwnd = target.hwnd();
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    hostLogPath = runDir + L"host.log";
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC", L"1");
    SetEnvironmentVariableW(L"REMOTE60_NATIVE_WIRE_CAP", wireCapOn ? L"1" : L"0");
    const std::wstring cmd = L"\"" + runDir + L"GNLinkStream.exe\" --transport udp --codec h264" +
                             L" --bind-address 127.0.0.1 --bind-port " + std::to_wstring(port) + L" --fps " +
                             std::to_wstring(fps) + L" --bitrate " + std::to_wstring(bitrate) + L" --seconds " +
                             std::to_wstring(seconds + 30) + L" --input-injection-mode none" +
                             remote60::native_poc::e2e::e2e_capture_window_args(target.title);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hostLog = CreateFileW(hostLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hostLog != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hostLog;
      si.hStdError = hostLog;
    }
    const std::wstring isoAppData = runDir + L"localappdata";
    CreateDirectoryW(isoAppData.c_str(), nullptr);
    std::vector<wchar_t> isoEnv = remote60::native_poc::e2e::e2e_isolated_environment(isoAppData);
    const bool launched = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr,
                                         hostLog != INVALID_HANDLE_VALUE,
                                         CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                         isoEnv.data(), runDir.c_str(), &si, &pi) != 0;
    if (launched) {
      AssignProcessToJobObject(job, pi.hProcess);
      ResumeThread(pi.hThread);
    }
    return launched;
  }
  void Stop() {
    target.Stop();
    if (pi.hProcess) {
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hProcess);
      pi.hProcess = nullptr;
    }
    if (pi.hThread) {
      CloseHandle(pi.hThread);
      pi.hThread = nullptr;
    }
    if (hostLog != INVALID_HANDLE_VALUE) {
      CloseHandle(hostLog);
      hostLog = INVALID_HANDLE_VALUE;
    }
    if (job) {
      CloseHandle(job);
      job = nullptr;
    }
  }
};

}  // namespace remote60::native_poc::e2emarker
