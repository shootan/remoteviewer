#pragma once

// motion-settle r1 M0: the measurement fixture for "still -> motion -> still" on real text.
//
// Role:    a window of the test process that shows a web-page-like document drawn with the system's
//          real fonts (Malgun Gothic for Korean, Segoe UI for Latin, at the small sizes a browser uses),
//          and plays scripted scenes on it -- an app window minimised and restored over the page, a
//          1 s scroll, a long scroll, one typed character, scroll stop/start bursts, and a busy noise
//          region -- each followed by a long still. Every painted frame carries a 16-bit event id in a
//          marker strip (two rows of 40 px cells), so a decoded frame names the paint it shows; the
//          painted picture of each episode's LAST event (the one the screen settles on) is kept as the
//          quality reference for that episode.
// Thread:  the window runs its own thread (message loop, 60 Hz timer); the harness thread queues an
//          episode with Play() and reads the results after it ends.
// Input:   EpisodeSpec (scene, timings).
// Output:  EpisodeRecord: the paint QPC of every event id, the onset id, the final id, the final
//          picture's Y plane (BT.709 limited, the pipeline's own), whether the source stayed unchanged
//          after the final event.
// Callers: viewer_udp_recovery_test.cpp --motion-settle.
//
// Differences from the field, stated so nobody reads more into it: the minimise/restore is drawn by
// the fixture (a panel shrinking toward the taskbar and growing back), not DWM's own animation of a
// real top-level window; the page is drawn by GDI, not a browser engine (ClearType is off so the
// glyphs are the same every run). The window itself is the capture target (WGC), alpha 1/255 and
// click-through like the other e2e fixtures -- the user's screen is neither captured nor covered.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "time_utils.hpp"

namespace remote60::native_poc::e2etext {
using remote60::native_poc::qpc_now_us;

constexpr int kCell = 40;          // marker cell (px on the painted page)
constexpr int kMarkerRows = 2;     // row 0: sync + bits 0..7, row 1: sync + bits 8..15
constexpr int kMarkerH = kCell * kMarkerRows;

// Restart50/100/200 (motion-settle r2 F counter-example): one typed character, then a 1 s scroll that
// starts 170 ms (the settle quiet) + 50/100/200 ms later -- i.e. while a settle refinement and its
// lowered ceiling may be running. Their onset is the scroll's first frame.
enum class Scene { MinimizeRestore, Scroll1s, ScrollLong, TypeChar, StopStart, Noise, Restart50, Restart100, Restart200 };
constexpr int kSceneCount = 9;
inline const char* scene_name(Scene s) {
  switch (s) {
    case Scene::MinimizeRestore: return "minrestore";
    case Scene::Scroll1s: return "scroll1s";
    case Scene::ScrollLong: return "scrolllong";
    case Scene::TypeChar: return "typechar";
    case Scene::StopStart: return "stopstart";
    case Scene::Noise: return "noise";
    case Scene::Restart50: return "restart50";
    case Scene::Restart100: return "restart100";
    case Scene::Restart200: return "restart200";
  }
  return "?";
}

struct EpisodeSpec {
  Scene scene = Scene::Scroll1s;
  uint32_t preIdleMs = 2500;   // still before the action (the encoder settles on a still page)
  uint32_t holdMs = 6000;      // still after the final event
};

struct EpisodeRecord {
  Scene scene = Scene::Scroll1s;
  uint64_t startUs = 0;        // Play() began the pre-idle
  uint32_t preStillId = 0;     // the id on screen during the pre-idle
  uint64_t preStillUs = 0;
  uint32_t onsetId = 0;        // the first painted frame of the action
  uint64_t onsetUs = 0;
  uint32_t finalId = 0;        // the last painted frame (the still the screen settles on)
  uint64_t finalUs = 0;
  uint64_t endUs = 0;          // the hold ended
  bool sourceStillAfterFinal = false;  // no paint changed the picture between finalUs and endUs
  std::map<uint32_t, uint64_t> paints;  // id -> paint qpc, this episode
  int w = 0, h = 0;
  std::vector<uint8_t> finalY;   // the painted final picture, luma
  std::vector<uint8_t> preY;     // the painted pre-idle picture, luma (the "before" crop)
};

// ---------------------------------------------------------------------------------------------
// The document: drawn once into a tall page; the view is a window onto it.
// ---------------------------------------------------------------------------------------------
class TextPageTarget {
 public:
  std::wstring title;
  int width = 1920;
  int height = 1080;

  bool Start() {
    thread_ = std::thread([this] { Run(); });
    const uint64_t deadline = qpc_now_us() + 8000000ULL;
    while (!ready_.load() && qpc_now_us() < deadline) Sleep(10);
    return ready_.load() && hwnd_ != nullptr;
  }
  void Stop() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
  }
  HWND hwnd() const { return hwnd_; }

  // The running episode's pre-still and final ids (0 until painted), and their painted luma.
  void PeekIds(uint32_t* pre, uint32_t* fin) {
    std::lock_guard<std::mutex> lk(mu_);
    *pre = rec_.preStillId;
    *fin = rec_.finalId;
  }
  std::vector<uint8_t> PreY() {
    std::lock_guard<std::mutex> lk(mu_);
    return rec_.preY;
  }
  std::vector<uint8_t> FinalY() {
    std::lock_guard<std::mutex> lk(mu_);
    return rec_.finalY;
  }

  // Plays one episode and blocks until its hold ends. Returns its record.
  EpisodeRecord Play(const EpisodeSpec& spec) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      spec_ = spec;
      rec_ = EpisodeRecord{};
      rec_.scene = spec.scene;
      rec_.w = width;
      rec_.h = height;
      phase_ = Phase::PreIdle;
      phaseStartUs_ = 0;
      done_ = false;
    }
    PostMessageW(hwnd_, WM_APP + 1, 0, 0);  // the window thread starts the episode
    for (;;) {
      Sleep(20);
      std::lock_guard<std::mutex> lk(mu_);
      if (done_) return rec_;
    }
  }

 private:
  enum class Phase { Idle, PreIdle, Action, Hold };

  // ---- page --------------------------------------------------------------------------------
  int pageH_ = 0;
  HDC pageDc_ = nullptr;
  HBITMAP pageBmp_ = nullptr;
  uint32_t* pagePx_ = nullptr;
  HDC viewDc_ = nullptr;
  HBITMAP viewBmp_ = nullptr;
  uint32_t* viewPx_ = nullptr;
  int scrollY_ = 0;
  int typedChars_ = 0;

  static HFONT MakeFont(const wchar_t* face, int px, int weight = FW_NORMAL) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
  }
  HDC MakeDib(int w, int h, HBITMAP* bmp, uint32_t** px) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    *bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(px), nullptr, 0);
    ReleaseDC(nullptr, screen);
    SelectObject(dc, *bmp);
    return dc;
  }

  // A long article: a header bar, a headline, paragraphs mixing Korean and English at 13-15 px, a
  // link list at 12 px, a small table of figures -- what the user scrolled in the field.
  void PaintPage() {
    pageH_ = height * 5;
    pageDc_ = MakeDib(width, pageH_, &pageBmp_, &pagePx_);
    RECT all{0, 0, width, pageH_};
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(pageDc_, &all, bg);
    DeleteObject(bg);
    SetBkMode(pageDc_, TRANSPARENT);
    HFONT head = MakeFont(L"Malgun Gothic", 26, FW_BOLD);
    HFONT body = MakeFont(L"Malgun Gothic", 14);
    HFONT latin = MakeFont(L"Segoe UI", 13);
    HFONT smallFont = MakeFont(L"Malgun Gothic", 12);
    const wchar_t* ko[] = {
        L"원격 데스크톱에서 스크롤을 멈춘 뒤 작은 글자가 뭉개져 보이다가 몇 초가 지나야 선명해지는 현상을 재현하는 문서입니다.",
        L"화면이 정지하면 인코더는 같은 그림을 조금씩 다듬어 보내고, 움직임이 시작되면 첫 프레임이 커져서 전송이 밀립니다.",
        L"이 단락은 한글과 English 가 섞인 본문으로, 브라우저에서 흔히 보는 13~15 픽셀 크기의 글꼴을 그대로 사용합니다.",
        L"가나다라마바사아자차카타파하 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz ,.;:!?()[]{}",
        L"작은 글자의 획(ㄱ ㄴ ㄷ ㄹ ㅁ ㅂ ㅅ ㅇ ㅈ ㅊ ㅋ ㅌ ㅍ ㅎ)과 받침(값 닭 흙 읽 앉 밟)은 양자화에 가장 먼저 무너지는 부분입니다.",
    };
    const wchar_t* en[] = {
        L"The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs. Sphinx of black quartz, judge my vow.",
        L"Release notes: fixed a stall when the viewer resumed after a long idle; the encoder now refines a still screen faster.",
        L"Settings > Display > Advanced: 1920 x 1080 @ 60 Hz, HDR off, scaling 100%, color depth 8-bit, refresh rate 60.000 Hz.",
    };
    // Header bar
    RECT bar{0, 0, width, 64};
    HBRUSH barB = CreateSolidBrush(RGB(32, 33, 36));
    FillRect(pageDc_, &bar, barB);
    DeleteObject(barB);
    int y = 96;
    int para = 0;
    while (y < pageH_ - 60) {
      if (para % 7 == 0) {
        SelectObject(pageDc_, head);
        SetTextColor(pageDc_, RGB(20, 20, 20));
        std::wstring h = L"제 " + std::to_wstring(para / 7 + 1) + L" 장 — 정지 화면 화질 복구와 움직임 시작 응답 (Section " +
                         std::to_wstring(para / 7 + 1) + L")";
        TextOutW(pageDc_, 120, y, h.c_str(), static_cast<int>(h.size()));
        y += 46;
      }
      const bool isEn = (para % 3) == 2;
      SelectObject(pageDc_, isEn ? latin : body);
      SetTextColor(pageDc_, (para % 5 == 4) ? RGB(26, 13, 171) : RGB(32, 33, 36));
      for (int line = 0; line < 3 && y < pageH_ - 60; ++line) {
        const wchar_t* t = isEn ? en[(para + line) % 3] : ko[(para + line) % 5];
        TextOutW(pageDc_, 120, y, t, static_cast<int>(wcslen(t)));
        y += isEn ? 20 : 22;
      }
      if (para % 4 == 1) {
        SelectObject(pageDc_, smallFont);
        SetTextColor(pageDc_, RGB(95, 99, 104));
        for (int c = 0; c < 4; ++c) {
          wchar_t cell[96];
          swprintf(cell, 96, L"항목 %d-%d | 1,%03d원 | %02d:%02d | 상태: 정상", para, c, (para * 37 + c * 11) % 1000, c + 9,
                   (para * 7) % 60);
          TextOutW(pageDc_, 120 + c * 420, y, cell, static_cast<int>(wcslen(cell)));
        }
        y += 18;
      }
      y += 14;
      ++para;
    }
    DeleteObject(head);
    DeleteObject(body);
    DeleteObject(latin);
    DeleteObject(smallFont);
    GdiFlush();
  }

  // ---- the view the window shows ---------------------------------------------------------------
  void ComposeView(uint32_t id) {
    const int off = std::max(0, std::min(scrollY_, pageH_ - height));
    BitBlt(viewDc_, 0, 0, width, height, pageDc_, 0, off, SRCCOPY);
    if (typedChars_ > 0) {
      // The "typed" text: one more character each time, in the body font, on a fixed line.
      HFONT body = MakeFont(L"Malgun Gothic", 14);
      HGDIOBJ old = SelectObject(viewDc_, body);
      SetBkMode(viewDc_, OPAQUE);
      SetBkColor(viewDc_, RGB(255, 255, 255));
      SetTextColor(viewDc_, RGB(32, 33, 36));
      static const wchar_t kTyped[] = L"입력 중인 글자: 원격화면선명도측정ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
      const int n = std::min<int>(typedChars_, static_cast<int>(wcslen(kTyped)));
      std::wstring s(kTyped, kTyped + n);
      TextOutW(viewDc_, 120, height - 120, s.c_str(), static_cast<int>(s.size()));
      SelectObject(viewDc_, old);
      DeleteObject(body);
    }
    if (panel_ > 0.0) DrawPanel();
    if (noiseOn_) DrawNoise();
    GdiFlush();
    DrawMarker(id);
  }
  // A 900x600 "application window" over the page; panel_ in (0,1] scales it toward the taskbar.
  double panel_ = 1.0;
  void DrawPanel() {
    const int fullW = 900, fullH = 600;
    const int cx = width / 2, cy = height / 2 + 40;
    const int tx = width / 2, ty = height - 10;  // where a minimised window goes (the taskbar)
    const int w = static_cast<int>(fullW * panel_), h = static_cast<int>(fullH * panel_);
    const int x = static_cast<int>(tx + (cx - tx) * panel_) - w / 2;
    const int yv = static_cast<int>(ty + (cy - ty) * panel_) - h / 2;
    RECT body{x, yv, x + w, yv + h};
    HBRUSH b = CreateSolidBrush(RGB(243, 243, 243));
    FillRect(viewDc_, &body, b);
    DeleteObject(b);
    RECT cap{x, yv, x + w, yv + std::max(2, static_cast<int>(32 * panel_))};
    HBRUSH c = CreateSolidBrush(RGB(0, 95, 184));
    FillRect(viewDc_, &cap, c);
    DeleteObject(c);
    if (panel_ >= 0.999) {
      HFONT f = MakeFont(L"Malgun Gothic", 13);
      HGDIOBJ old = SelectObject(viewDc_, f);
      SetBkMode(viewDc_, TRANSPARENT);
      SetTextColor(viewDc_, RGB(255, 255, 255));
      TextOutW(viewDc_, x + 12, yv + 7, L"메모장 - 회의록.txt", 13);
      SetTextColor(viewDc_, RGB(20, 20, 20));
      for (int i = 0; i < 24; ++i) {
        wchar_t line[128];
        swprintf(line, 128, L"%02d. 회의 안건 %d: 원격 접속 화질 점검, latency %d ms, 담당자 확인 필요", i + 1, i + 1, 20 + i * 3);
        TextOutW(viewDc_, x + 16, yv + 44 + i * 22, line, static_cast<int>(wcslen(line)));
      }
      SelectObject(viewDc_, old);
      DeleteObject(f);
    }
  }
  bool noiseOn_ = false;
  uint32_t noiseSeed_ = 1;
  void DrawNoise() {
    const int nx = width - 760, ny = 220, nw = 640, nh = 360;
    uint32_t s = noiseSeed_ * 2654435761u + 1u;
    for (int yy = ny; yy < ny + nh && yy < height; ++yy) {
      uint32_t* row = viewPx_ + static_cast<size_t>(yy) * width;
      for (int xx = nx; xx < nx + nw && xx < width; ++xx) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        row[xx] = s & 0x00FFFFFFu;
      }
    }
  }
  void DrawMarker(uint32_t id) {
    auto cell = [&](int row, int c, bool white) {
      const uint32_t color = white ? 0x00FFFFFFu : 0x00000000u;
      for (int yy = row * kCell + 2; yy < row * kCell + kCell - 2; ++yy) {
        uint32_t* r = viewPx_ + static_cast<size_t>(yy) * width;
        for (int xx = c * kCell + 2; xx < c * kCell + kCell - 2; ++xx) r[xx] = color;
      }
    };
    // Black under the strip so the cells read the same on any page background.
    for (int yy = 0; yy < kMarkerH; ++yy) {
      uint32_t* r = viewPx_ + static_cast<size_t>(yy) * width;
      for (int xx = 0; xx < 9 * kCell; ++xx) r[xx] = 0;
    }
    for (int row = 0; row < kMarkerRows; ++row) {
      cell(row, 0, true);
      for (int b = 0; b < 8; ++b) cell(row, b + 1, ((id >> (row * 8 + b)) & 1u) != 0);
    }
  }
  static std::vector<uint8_t> Luma(const uint32_t* px, int w, int h) {
    std::vector<uint8_t> y(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < y.size(); ++i) {
      const uint32_t p = px[i];
      const int b = p & 0xFF, g = (p >> 8) & 0xFF, r = (p >> 16) & 0xFF;
      const int v = ((47 * r + 157 * g + 16 * b + 128) >> 8) + 16;
      y[i] = static_cast<uint8_t>(v > 255 ? 255 : v);
    }
    return y;
  }

  // ---- the episode state machine (window thread) ------------------------------------------------
  std::mutex mu_;
  EpisodeSpec spec_;
  EpisodeRecord rec_;
  Phase phase_ = Phase::Idle;
  uint64_t phaseStartUs_ = 0;
  uint32_t actionFrame_ = 0;
  bool done_ = false;
  uint32_t nextId_ = 1;
  uint32_t shownId_ = 0;
  uint64_t lastChangeUs_ = 0;

  // One painted frame with a new id (the picture changed).
  void PaintNew(uint64_t nowUs) {
    shownId_ = nextId_++;
    if (nextId_ > 0xFFFF) nextId_ = 1;
    ComposeView(shownId_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
    const uint64_t paintedUs = qpc_now_us();
    rec_.paints[shownId_] = paintedUs;
    lastChangeUs_ = paintedUs;
    (void)nowUs;
  }

  // What the action does at frame `f` of the 60 Hz timer: whether the picture changed, and whether
  // this is the action's last change (the picture the screen then settles on).
  struct Step { bool changed = false; bool last = false; bool onset = false; };
  Step ActionStep(uint32_t f) {
    switch (spec_.scene) {
      case Scene::MinimizeRestore:
        // 12 frames down to the taskbar, 40 frames gone, 12 frames back (~200 ms each way at 60 Hz).
        if (f < 12) { panel_ = 1.0 - (f + 1) / 12.0; return {true, false}; }
        if (f < 52) return {false, false};
        panel_ = (f - 51) / 12.0;
        return {true, f >= 63};
      case Scene::Scroll1s:
        scrollY_ += 12;
        return {true, f >= 59};
      case Scene::ScrollLong:
        scrollY_ += 8;
        return {true, f >= 299};
      case Scene::TypeChar:
        ++typedChars_;
        return {true, true};
      case Scene::StopStart:
        // 5 scroll bursts of 18 frames with 18 still frames between them.
        if ((f / 18) % 2 == 1) return {false, false};
        scrollY_ += 10;
        return {true, f >= 161};
      case Scene::Noise:
        if (f < 120) { noiseOn_ = true; ++noiseSeed_; return {true, false}; }
        noiseOn_ = false;
        return {true, true};
      case Scene::Restart50:
      case Scene::Restart100:
      case Scene::Restart200: {
        // Frame 0: one typed character. Then idle until 170 ms + the delay, then a 60-frame scroll.
        const uint32_t delayMs = spec_.scene == Scene::Restart50 ? 50 : spec_.scene == Scene::Restart100 ? 100 : 200;
        const uint32_t idleFrames = (170 + delayMs) * 60 / 1000;  // 60 Hz timer
        if (f == 0) { ++typedChars_; return {true, false}; }
        if (f < idleFrames) return {false, false};
        scrollY_ += 12;
        const uint32_t k = f - idleFrames;
        return {true, k >= 59, k == 0};
      }
    }
    return {true, true};
  }

  void Tick() {
    const uint64_t now = qpc_now_us();
    std::lock_guard<std::mutex> lk(mu_);
    switch (phase_) {
      case Phase::Idle:
        return;
      case Phase::PreIdle:
        if (phaseStartUs_ == 0) {
          // motion-settle r2 D: every episode of a scene starts from the same state, so its final picture
          // is the same picture every time and one long-still reference serves all of them (and the
          // baseline and every candidate alike).
          switch (spec_.scene) {
            case Scene::MinimizeRestore: scrollY_ = 600; typedChars_ = 0; break;
            case Scene::Noise: scrollY_ = 300; typedChars_ = 0; break;
            case Scene::TypeChar: scrollY_ = 0; typedChars_ = 10; break;
            case Scene::Restart50:
            case Scene::Restart100:
            case Scene::Restart200: scrollY_ = 0; typedChars_ = 20; break;
            default: scrollY_ = 0; typedChars_ = 0; break;
          }
          panel_ = spec_.scene == Scene::MinimizeRestore ? 1.0 : 0.0;
          noiseOn_ = false;
          noiseSeed_ = 1;
          phaseStartUs_ = now;
          rec_.startUs = now;
          PaintNew(now);
          rec_.preStillId = shownId_;
          rec_.preStillUs = rec_.paints[shownId_];
          rec_.preY = Luma(viewPx_, width, height);
          return;
        }
        if (now - phaseStartUs_ >= static_cast<uint64_t>(spec_.preIdleMs) * 1000ULL) {
          phase_ = Phase::Action;
          phaseStartUs_ = now;
          actionFrame_ = 0;
        }
        return;
      case Phase::Action: {
        const Step st = ActionStep(actionFrame_);
        ++actionFrame_;
        if (st.changed) {
          PaintNew(now);
          if (rec_.onsetId == 0 || st.onset) {  // a restart scene's onset is its scroll, not the character
            rec_.onsetId = shownId_;
            rec_.onsetUs = rec_.paints[shownId_];
          }
        }
        if (st.last) {
          rec_.finalId = shownId_;
          rec_.finalUs = rec_.paints[shownId_];
          rec_.finalY = Luma(viewPx_, width, height);
          phase_ = Phase::Hold;
          phaseStartUs_ = now;
        }
        return;
      }
      case Phase::Hold:
        if (now - phaseStartUs_ >= static_cast<uint64_t>(spec_.holdMs) * 1000ULL) {
          rec_.endUs = now;
          rec_.sourceStillAfterFinal = lastChangeUs_ <= rec_.finalUs && shownId_ == rec_.finalId;
          phase_ = Phase::Idle;
          done_ = true;
        }
        return;
    }
  }

  static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<TextPageTarget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_TIMER && self) {
      self->Tick();
      return 0;
    }
    if (msg == WM_APP + 1 && self) {
      self->Tick();
      return 0;
    }
    if (msg == WM_PAINT && self) {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd, &ps);
      BitBlt(dc, 0, 0, self->width, self->height, self->viewDc_, 0, 0, SRCCOPY);
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
    PaintPage();
    viewDc_ = MakeDib(width, height, &viewBmp_, &viewPx_);
    panel_ = 0.0;
    ComposeView(0);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Remote60MotionSettleTarget";
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT, wc.lpszClassName,
                            title.c_str(), WS_POPUP, 0, 0, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd_) {
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      timeBeginPeriod(1);
      SetTimer(hwnd_, 1, 16, nullptr);
    }
    ready_.store(true);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    timeEndPeriod(1);
    if (viewDc_) DeleteDC(viewDc_);
    if (viewBmp_) DeleteObject(viewBmp_);
    if (pageDc_) DeleteDC(pageDc_);
    if (pageBmp_) DeleteObject(pageBmp_);
    hwnd_ = nullptr;
  }

  std::thread thread_;
  std::atomic<bool> ready_{false};
  HWND hwnd_ = nullptr;
};

// The 16-bit id out of a decoded NV12 Y plane; 0 when no marker (sync cells not white). visW scales
// the cell pitch when the host encoded below the painted size.
inline uint32_t read_marker16(const uint8_t* y, uint32_t codedW, uint32_t codedH, uint32_t visLeft, uint32_t visTop,
                              uint32_t visW, uint32_t visH, int paintedW, int paintedH) {
  if (!y || codedW == 0 || visW == 0 || visH == 0) return 0;
  const double cx = static_cast<double>(kCell) * visW / paintedW;
  const double cy = static_cast<double>(kCell) * visH / paintedH;
  if (cx < 4.0 || cy < 4.0) return 0;
  auto sample = [&](int row, int c) -> int {
    const uint32_t px = visLeft + static_cast<uint32_t>(c * cx + cx / 2);
    const uint32_t py = visTop + static_cast<uint32_t>(row * cy + cy / 2);
    if (px >= codedW || py >= codedH) return -1;
    return y[static_cast<size_t>(py) * codedW + px];
  };
  uint32_t id = 0;
  for (int row = 0; row < kMarkerRows; ++row) {
    if (sample(row, 0) < 160) return 0;
    for (int b = 0; b < 8; ++b) {
      const int v = sample(row, b + 1);
      if (v < 0) return 0;
      if (v > 128) id |= (1u << (row * 8 + b));
    }
  }
  return id;
}

// Text-ROI PSNR of a decoded picture against the painted luma: everything below the marker strip,
// the painted reference box-averaged down when the host encoded below the painted size.
inline double text_roi_psnr(const uint8_t* y, uint32_t codedW, uint32_t visLeft, uint32_t visTop, uint32_t visW,
                            uint32_t visH, const std::vector<uint8_t>& ref, int refW, int refH) {
  if (!y || ref.empty() || visW == 0 || visH == 0) return 0.0;
  const uint32_t top = static_cast<uint32_t>((static_cast<uint64_t>(kMarkerH) * visH) / refH) + 2;
  double se = 0;
  uint64_t n = 0;
  for (uint32_t yy = top; yy < visH; ++yy) {
    const int ry0 = static_cast<int>(static_cast<uint64_t>(yy) * refH / visH);
    const int ry1 = std::max(ry0 + 1, static_cast<int>(static_cast<uint64_t>(yy + 1) * refH / visH));
    const uint8_t* row = y + static_cast<size_t>(visTop + yy) * codedW + visLeft;
    for (uint32_t xx = 0; xx < visW; ++xx) {
      double refv;
      if (static_cast<int>(visW) == refW && static_cast<int>(visH) == refH) {
        refv = ref[static_cast<size_t>(yy) * refW + xx];
      } else {
        const int rx0 = static_cast<int>(static_cast<uint64_t>(xx) * refW / visW);
        const int rx1 = std::max(rx0 + 1, static_cast<int>(static_cast<uint64_t>(xx + 1) * refW / visW));
        uint32_t sum = 0, cnt = 0;
        for (int ry = ry0; ry < ry1 && ry < refH; ++ry)
          for (int rx = rx0; rx < rx1 && rx < refW; ++rx) { sum += ref[static_cast<size_t>(ry) * refW + rx]; ++cnt; }
        refv = cnt ? static_cast<double>(sum) / cnt : 0.0;
      }
      const double d = static_cast<double>(row[xx]) - refv;
      se += d * d;
      ++n;
    }
  }
  if (n == 0) return 0.0;
  const double mse = se / static_cast<double>(n);
  return mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// The glyph pixels of a painted page: every pixel within 1 px of ink (luma below 180 on the light
// page), below the marker strip. Text sharpness lives here; the page's white space, which a full
// ROI average is dominated by, says nothing about it.
inline std::vector<uint8_t> make_ink_mask(const std::vector<uint8_t>& ref, int w, int h) {
  std::vector<uint8_t> m(ref.size(), 0);
  if (ref.empty()) return m;
  for (int y = kMarkerH + 2; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (ref[static_cast<size_t>(y) * w + x] >= 180) continue;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int yy = y + dy, xx = x + dx;
          if (yy > kMarkerH + 1 && yy < h && xx >= 0 && xx < w) m[static_cast<size_t>(yy) * w + xx] = 1;
        }
    }
  return m;
}

// PSNR over the ink mask, at the painted size only (a picture the host encoded smaller is compared
// through text_roi_psnr's box filter instead; the ink figure is then -1).
inline double ink_psnr(const uint8_t* y, uint32_t codedW, uint32_t visLeft, uint32_t visTop, uint32_t visW, uint32_t visH,
                       const std::vector<uint8_t>& ref, const std::vector<uint8_t>& mask, int refW, int refH) {
  if (!y || ref.empty() || mask.size() != ref.size()) return -1.0;
  if (static_cast<int>(visW) != refW || static_cast<int>(visH) != refH) return -1.0;
  double se = 0;
  uint64_t n = 0;
  for (int yy = 0; yy < refH; ++yy) {
    const uint8_t* row = y + static_cast<size_t>(visTop + yy) * codedW + visLeft;
    const size_t base = static_cast<size_t>(yy) * refW;
    for (int xx = 0; xx < refW; ++xx) {
      if (!mask[base + xx]) continue;
      const double d = static_cast<double>(row[xx]) - ref[base + xx];
      se += d * d;
      ++n;
    }
  }
  if (n == 0) return -1.0;
  const double mse = se / static_cast<double>(n);
  return mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

}  // namespace remote60::native_poc::e2etext
