// See viewer_clip_transfer_bar.hpp.

#include "viewer_clip_transfer_bar.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cstdio>

#include "bulk_pacer.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

namespace {

constexpr wchar_t kClassName[] = L"Remote60ClipTransferBar";
constexpr UINT_PTR kTimerPoll = 1;
constexpr UINT kPollMs = 250;
constexpr uint64_t kSlowAfterMs = 10000;  // past this, the line says the network is why

std::wstring megabytes(uint64_t bytes) {
  wchar_t buf[32];
  std::swprintf(buf, 32, L"%.1f", static_cast<double>(bytes) / (1024.0 * 1024.0));
  return buf;
}

std::wstring seconds(uint64_t ms) { return std::to_wstring((ms + 500) / 1000) + L"초"; }

std::wstring failure_reason(ClipImageReason r) {
  switch (r) {
    case ClipImageReason::Stalled: return L"원격 PC가 응답하지 않음";
    case ClipImageReason::Timeout: return L"시간 초과";
    case ClipImageReason::VerifyFailed: return L"받은 데이터 검증 실패";
    case ClipImageReason::DecodeFailed: return L"이미지를 읽지 못함";
    case ClipImageReason::SizeMismatch: return L"이미지 크기 불일치";
    case ClipImageReason::PublishFailed: return L"원격 클립보드에 쓰지 못함";
    case ClipImageReason::Session: return L"연결이 끊김";
    case ClipImageReason::Limit: return L"크기 제한 초과";
    case ClipImageReason::ColorProfile: return L"색 프로필이 들어 있는 이미지";
    default: return L"알 수 없는 이유";
  }
}

}  // namespace

std::wstring clip_transfer_bar_text(const ClipBarView& v) {
  if (v.phase == ClipBarPhase::Sending) {
    const uint64_t pct = v.bytesTotal ? (std::min<uint64_t>)(99, v.bytesDone * 100 / v.bytesTotal) : 0;
    std::wstring s = L"원격 PC로 이미지 보내는 중 " + std::to_wstring(pct) + L"% (" + megabytes(v.bytesDone) + L" / " +
                     megabytes(v.bytesTotal) + L" MB) · " + seconds(v.elapsedMs);
    if (v.elapsedMs >= kSlowAfterMs) s += L" — 네트워크가 느려 시간이 걸리고 있습니다";
    return s;
  }
  if (v.phase != ClipBarPhase::Result) return std::wstring();
  const auto st = static_cast<ClipImageState>(v.state);
  const auto why = static_cast<ClipImageReason>(v.reason);
  switch (st) {
    case ClipImageState::Published:
      return L"이미지를 원격 PC 클립보드에 넣었습니다 (" + megabytes(v.bytesTotal) + L" MB, " + seconds(v.elapsedMs) +
             L")";
    case ClipImageState::Cancelled:
      if (why == ClipImageReason::User) return L"이미지 보내기를 취소했습니다";
      if (why == ClipImageReason::Superseded) return L"새로 복사한 내용으로 바뀌어 이전 이미지는 보내지 않았습니다";
      if (why == ClipImageReason::Session) return L"연결이 끊겨 이미지 보내기가 중단됐습니다";
      return L"이미지 보내기가 취소됐습니다";
    case ClipImageState::Superseded:
      return L"원격 PC 클립보드가 먼저 바뀌어 이미지를 넣지 않았습니다";
    default:
      return L"이미지를 보내지 못했습니다 (" + failure_reason(why) + L")";
  }
}

ClipBarView clip_transfer_bar_view(const ClipImageClient::Progress& p, uint64_t nowUs, uint64_t* seenFinished,
                                   uint64_t* resultUntilUs) {
  ClipBarView v;
  if (*resultUntilUs == 1) {
    // The first poll after the bar is created: whatever ended before it existed is not news.
    *seenFinished = p.finished;
    *resultUntilUs = 0;
  }
  if (p.active) {
    v.phase = ClipBarPhase::Sending;
    v.bytesDone = p.bytesConfirmed;
    v.bytesTotal = p.bytesTotal;
    v.elapsedMs = p.elapsedMs;
    *seenFinished = p.finished;  // whatever ended before this one is not news any more
    *resultUntilUs = 0;
    return v;
  }
  if (p.finished != *seenFinished) {
    // A new outcome, including one that began and ended between two polls (a fast LAN copy).
    *seenFinished = p.finished;
    *resultUntilUs = nowUs + kClipBarResultUs;
  }
  if (*resultUntilUs > nowUs) {
    v.phase = ClipBarPhase::Result;
    v.bytesDone = p.bytesConfirmed;
    v.bytesTotal = p.bytesTotal;
    v.elapsedMs = p.elapsedMs;
    v.state = p.lastState;
    v.reason = p.lastReason;
  }
  return v;
}

namespace {

struct Bar {
  HWND hwnd = nullptr;
  HWND owner = nullptr;
  ClipTransferBarHooks hooks;
  ClipBarView view;
  uint64_t seenFinished = 0;
  uint64_t resultUntilUs = 1;  // 1 = "nothing polled yet" (see clip_transfer_bar_view)
  RECT cancel{};
  bool pressed = false;
  int dpi = 96;
  HFONT font = nullptr;
  HBRUSH background = nullptr;
  HBRUSH border = nullptr;
  HBRUSH button = nullptr;
  HBRUSH buttonDown = nullptr;
};

Bar g;

int scaled(int v) { return MulDiv(v, g.dpi, 96); }

SIZE measure(const std::wstring& text) {
  HDC hdc = GetDC(g.hwnd);
  HGDIOBJ old = g.font ? SelectObject(hdc, g.font) : nullptr;
  SIZE s{};
  GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &s);
  if (old) SelectObject(hdc, old);
  ReleaseDC(g.hwnd, hdc);
  return s;
}

void reposition() {
  if (!g.hwnd || !g.owner || !IsWindow(g.owner)) return;
  const bool showable = g.view.phase != ClipBarPhase::Hidden && IsWindowVisible(g.owner) && !IsIconic(g.owner);
  if (!showable) {
    ShowWindow(g.hwnd, SW_HIDE);
    return;
  }
  const std::wstring text = clip_transfer_bar_text(g.view);
  const SIZE t = measure(text);
  const int pad = scaled(10);
  const int height = t.cy + pad * 2;
  int width = t.cx + pad * 2;
  g.cancel = RECT{};
  if (clip_transfer_bar_has_cancel(g.view)) {
    const SIZE b = measure(L"취소");
    const int bw = b.cx + scaled(28);
    g.cancel = RECT{width, scaled(5), width + bw, height - scaled(5)};
    width += bw + pad;
  }
  RECT client{};
  GetClientRect(g.owner, &client);
  POINT origin{client.left, client.top};
  ClientToScreen(g.owner, &origin);
  const int clientW = client.right - client.left;
  const int clientH = client.bottom - client.top;
  width = (std::min)(width, (std::max)(clientW - scaled(16), scaled(120)));
  if (g.cancel.right > width - pad) {  // narrow window: the button keeps its place at the right edge
    const int bw = g.cancel.right - g.cancel.left;
    g.cancel.right = width - pad;
    g.cancel.left = g.cancel.right - bw;
  }
  const int x = origin.x + (std::max)(0, (clientW - width) / 2);
  const int y = origin.y + (std::max)(0, clientH - height - scaled(16));
  SetWindowPos(g.hwnd, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | (IsWindowVisible(g.hwnd) ? 0u : SWP_SHOWWINDOW));
  InvalidateRect(g.hwnd, nullptr, FALSE);
}

void poll() {
  if (!g.hooks.progress) return;
  const ClipBarView before = g.view;
  g.view = clip_transfer_bar_view(g.hooks.progress(), BulkPacer::NowUs(), &g.seenFinished, &g.resultUntilUs);
  if (g.view.phase != before.phase && g.hooks.onLog) {
    std::string line = "[clip-bar] phase=" + std::to_string(static_cast<int>(g.view.phase));
    if (g.view.phase == ClipBarPhase::Result) {
      line += " state=" + std::to_string(g.view.state) + " reason=" + std::to_string(g.view.reason);
    }
    g.hooks.onLog(line);
  }
  const bool changed = g.view.phase != before.phase || g.view.bytesDone != before.bytesDone ||
                       g.view.elapsedMs / 1000 != before.elapsedMs / 1000 || g.view.state != before.state;
  if (changed) reposition();
}

void paint(HDC target) {
  RECT client{};
  GetClientRect(g.hwnd, &client);
  const int w = client.right - client.left;
  const int h = client.bottom - client.top;
  HDC hdc = CreateCompatibleDC(target);  // off-screen, then one blit: no flicker over moving video
  HBITMAP bmp = CreateCompatibleBitmap(target, w, h);
  HGDIOBJ oldBmp = SelectObject(hdc, bmp);
  FillRect(hdc, &client, g.background);
  FrameRect(hdc, &client, g.border);
  SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ oldFont = g.font ? SelectObject(hdc, g.font) : nullptr;
  const int pad = scaled(10);
  RECT textRect{pad, 0, (g.cancel.right > 0 ? g.cancel.left : w) - pad / 2, h};
  const bool failed = g.view.phase == ClipBarPhase::Result &&
                      static_cast<ClipImageState>(g.view.state) != ClipImageState::Published &&
                      static_cast<ClipImageState>(g.view.state) != ClipImageState::Cancelled;
  SetTextColor(hdc, failed ? RGB(242, 150, 140) : RGB(232, 236, 242));
  const std::wstring text = clip_transfer_bar_text(g.view);
  DrawTextW(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  if (g.cancel.right > 0) {
    RECT b = g.cancel;
    FillRect(hdc, &b, g.pressed ? g.buttonDown : g.button);
    FrameRect(hdc, &b, g.border);
    SetTextColor(hdc, RGB(232, 236, 242));
    DrawTextW(hdc, L"취소", -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  if (oldFont) SelectObject(hdc, oldFont);
  BitBlt(target, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
  SelectObject(hdc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(hdc);
}

bool in_cancel(int x, int y) {
  return g.cancel.right > 0 && x >= g.cancel.left && x < g.cancel.right && y >= g.cancel.top && y < g.cancel.bottom;
}

LRESULT CALLBACK bar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    // Clicking the bar must not take focus from the video window: keystrokes are captured there.
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd, &ps);
      paint(hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      if (wp == kTimerPoll) {
        poll();
        return 0;
      }
      break;
    case WM_LBUTTONDOWN:
      g.pressed = in_cancel(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (g.pressed) SetCapture(hwnd);
      if (g.hooks.onLog) {
        g.hooks.onLog("[clip-bar] down x=" + std::to_string(GET_X_LPARAM(lp)) + " y=" + std::to_string(GET_Y_LPARAM(lp)) +
                      " onCancel=" + (g.pressed ? "1" : "0"));
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_LBUTTONUP: {
      const bool was = g.pressed;
      g.pressed = false;
      if (GetCapture() == hwnd) ReleaseCapture();
      InvalidateRect(hwnd, nullptr, FALSE);
      if (!was) return 0;
      if (!in_cancel(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) {
        if (g.hooks.onLog) g.hooks.onLog("[clip-bar] up ignored: released off Cancel");
        return 0;
      }
      if (!clip_transfer_bar_has_cancel(g.view)) {
        if (g.hooks.onLog) g.hooks.onLog("[clip-bar] up ignored: nothing is being sent any more");
        return 0;
      }
      if (g.hooks.onLog) {
        g.hooks.onLog(std::string("[clip-bar] cancel clicked, callback ") + (g.hooks.onCancel ? "present" : "MISSING"));
      }
      if (g.hooks.onCancel) g.hooks.onCancel();
      poll();
      return 0;
    }
    case WM_DPICHANGED:
      g.dpi = HIWORD(wp);
      reposition();
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool clip_transfer_bar_create(HWND owner, ClipTransferBarHooks hooks) {
  if (g.hwnd) return true;
  if (!owner || !IsWindow(owner)) return false;
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = bar_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
  g.owner = owner;
  g.hooks = std::move(hooks);
  g.dpi = static_cast<int>(GetDpiForWindow(owner));
  if (g.dpi <= 0) g.dpi = 96;
  g.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 10, 10, owner,
                           nullptr, instance, nullptr);
  if (!g.hwnd) {
    g.owner = nullptr;
    return false;
  }
  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(12, g.dpi, 72);
  lf.lfWeight = FW_NORMAL;
  lstrcpynW(lf.lfFaceName, L"Malgun Gothic", LF_FACESIZE);
  g.font = CreateFontIndirectW(&lf);
  g.background = CreateSolidBrush(RGB(20, 24, 31));
  g.border = CreateSolidBrush(RGB(64, 74, 90));
  g.button = CreateSolidBrush(RGB(43, 51, 64));
  g.buttonDown = CreateSolidBrush(RGB(24, 29, 37));
  SetTimer(g.hwnd, kTimerPoll, kPollMs, nullptr);
  if (g.hooks.onLog) g.hooks.onLog("[clip-bar] created");
  return true;
}

void clip_transfer_bar_follow_owner() { reposition(); }

void clip_transfer_bar_destroy() {
  if (g.hwnd) {
    KillTimer(g.hwnd, kTimerPoll);
    DestroyWindow(g.hwnd);
  }
  for (HGDIOBJ* o : {reinterpret_cast<HGDIOBJ*>(&g.font), reinterpret_cast<HGDIOBJ*>(&g.background),
                     reinterpret_cast<HGDIOBJ*>(&g.border), reinterpret_cast<HGDIOBJ*>(&g.button),
                     reinterpret_cast<HGDIOBJ*>(&g.buttonDown)}) {
    if (*o) DeleteObject(*o);
    *o = nullptr;
  }
  g = Bar{};
}

HWND clip_transfer_bar_window() { return g.hwnd; }
RECT clip_transfer_bar_cancel_rect() { return g.cancel; }
ClipBarView clip_transfer_bar_current() { return g.view; }

}  // namespace remote60::native_poc
