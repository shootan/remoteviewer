#include "client_session_toolbar.hpp"

#include <windowsx.h>

#include <algorithm>
#include <mutex>
#include <string>

namespace remote60::native_poc {
namespace {

constexpr wchar_t kClassName[] = L"Remote60SessionToolbar";
// State arrives on the network thread; every window call below has to happen on the thread that
// owns the window, so the push turns into a posted message instead of a direct repaint.
constexpr UINT kMsgStatePushed = WM_APP + 1;

// Auto-hide. The bar blocks every remote-desktop pixel it covers, so it stays hidden until
// summoned and leaves when the mouse does. Dwell before expanding, so a click aimed at the
// top of the remote screen does not pop the bar into its own path; a delay before collapsing,
// so travelling from the trigger zone onto a button does not hide the target mid-way.
constexpr UINT_PTR kTimerExpand = 1;
constexpr UINT_PTR kTimerCollapse = 2;
constexpr UINT kExpandDwellMs = 250;
constexpr UINT kCollapseDelayMs = 800;
// Long enough to be seen and remembered on connect, short enough to get out of the way.
constexpr UINT kIntroMs = 2500;

enum ButtonId : int {
  kButtonNone = 0,
  kButtonTargets = 1,
  kButtonMacro = 2,
  kButtonMonitor = 3,
};

struct Button {
  RECT rect{};
  int id = kButtonNone;
  std::wstring label;
  bool active = false;
};

struct Toolbar {
  HWND hwnd = nullptr;
  HWND owner = nullptr;
  SessionToolbarCallbacks callbacks;
  SessionToolbarState state;
  std::vector<Button> buttons;
  int pressed = kButtonNone;
  int hovered = kButtonNone;
  bool tracking = false;
  bool wanted = false;    // what the session asked for, before owner visibility is considered
  bool expanded = false;  // whether the bar is currently summoned; hidden pixels block nothing
  bool menuOpen = false;  // the monitor menu is modal; collapsing under it would tear it down
  int dpi = 96;
  HFONT font = nullptr;
};

Toolbar g;
std::mutex gStateMu;
SessionToolbarState gPendingState;  // written by the pusher, drained by the window thread

int scaled(int value) { return MulDiv(value, g.dpi, 96); }

HBRUSH solid_brush(COLORREF color) {
  // Small and fixed: three shades for the bar, and one per button state.
  static COLORREF keys[8]{};
  static HBRUSH values[8]{};
  for (int i = 0; i < 8; ++i) {
    if (values[i] && keys[i] == color) return values[i];
  }
  for (int i = 0; i < 8; ++i) {
    if (!values[i]) {
      keys[i] = color;
      values[i] = CreateSolidBrush(color);
      return values[i];
    }
  }
  return values[0];
}

std::wstring status_text() {
  std::wstring text = g.state.connected ? L"연결됨" : L"연결 중";
  text += g.state.relay ? L" · 릴레이" : L" · 직접";
  if (!g.state.inputOn) text += L" · 입력 꺼짐";
  if (g.state.fps > 0) text += L" · " + std::to_wstring(g.state.fps) + L"fps";
  return text;
}

std::wstring monitor_label() {
  if (g.state.monitors.size() < 2) return std::wstring();
  for (size_t i = 0; i < g.state.monitors.size(); ++i) {
    if (g.state.monitors[i].id == g.state.selectedMonitorId) {
      return L"모니터 " + std::to_wstring(i + 1);
    }
  }
  return L"모니터";
}

int text_width(const std::wstring& text) {
  HDC hdc = GetDC(g.hwnd);
  if (!hdc) return scaled(70);
  HGDIOBJ old = g.font ? SelectObject(hdc, g.font) : nullptr;
  SIZE size{};
  GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
  if (old) SelectObject(hdc, old);
  ReleaseDC(g.hwnd, hdc);
  return size.cx;
}

/** Rebuilds the buttons and returns the size the bar needs for them. */
SIZE rebuild_layout() {
  g.buttons.clear();
  const int pad = scaled(6);
  const int gap = scaled(4);
  const int height = scaled(28);

  int x = pad;
  auto add = [&](int id, const std::wstring& label, bool active) {
    Button button;
    button.id = id;
    button.label = label;
    button.active = active;
    const int width = text_width(label) + scaled(22);
    button.rect = RECT{x, pad, x + width, pad + height};
    x += width + gap;
    g.buttons.push_back(std::move(button));
  };

  // No target picker on Windows: the product decision is desktop-only, and the picker screen
  // itself cannot render once the flip swap chain owns the window -- pressing the old button
  // left the session looking frozen. Monitors are picked from the dropdown instead.
  add(kButtonMacro, L"매크로", g.state.macroOpen);
  const std::wstring monitors = monitor_label();
  if (!monitors.empty()) add(kButtonMonitor, monitors + L" ▾", false);

  const int statusWidth = text_width(status_text());
  SIZE size{};
  size.cx = x + scaled(6) + statusWidth + pad;
  size.cy = height + pad * 2;
  return size;
}

void reposition() {
  if (!g.hwnd || !g.owner || !IsWindow(g.owner)) return;
  const bool showable =
      g.wanted && g.expanded && IsWindowVisible(g.owner) && !IsIconic(g.owner);
  if (!showable) {
    ShowWindow(g.hwnd, SW_HIDE);
    return;
  }
  const SIZE size = rebuild_layout();
  RECT client{};
  GetClientRect(g.owner, &client);
  POINT topLeft{client.left, client.top};
  ClientToScreen(g.owner, &topLeft);
  const int clientWidth = client.right - client.left;
  // Explicit template argument, because windows.h defines a max() macro that would otherwise
  // eat this call.
  const int x = topLeft.x + std::max<int>(0, (clientWidth - static_cast<int>(size.cx)) / 2);
  const int y = topLeft.y + scaled(10);
  SetWindowPos(g.hwnd, HWND_TOP, x, y, size.cx, size.cy,
               SWP_NOACTIVATE | (IsWindowVisible(g.hwnd) ? 0u : SWP_SHOWWINDOW));
  InvalidateRect(g.hwnd, nullptr, FALSE);
}

void paint(HDC target) {
  RECT client{};
  GetClientRect(g.hwnd, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;

  // Drawn off-screen first: the bar repaints on every hover and state push, and painting it
  // piece by piece straight to the window flickers over moving video.
  HDC hdc = CreateCompatibleDC(target);
  HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
  HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);

  FillRect(hdc, &client, solid_brush(RGB(20, 24, 31)));
  FrameRect(hdc, &client, solid_brush(RGB(48, 56, 68)));

  SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ oldFont = g.font ? SelectObject(hdc, g.font) : nullptr;

  int right = 0;
  for (const Button& button : g.buttons) {
    const bool down = g.pressed == button.id && g.hovered == button.id;
    COLORREF fill = RGB(31, 37, 47);
    if (button.active) fill = RGB(37, 72, 118);
    if (g.hovered == button.id) fill = button.active ? RGB(45, 87, 141) : RGB(43, 51, 64);
    if (down) fill = RGB(24, 29, 37);
    RECT rect = button.rect;
    FillRect(hdc, &rect, solid_brush(fill));
    FrameRect(hdc, &rect, solid_brush(button.active ? RGB(88, 140, 200) : RGB(56, 65, 79)));
    SetTextColor(hdc, RGB(232, 236, 242));
    DrawTextW(hdc, button.label.c_str(), -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    right = std::max<int>(right, button.rect.right);
  }

  RECT statusRect{right + scaled(10), 0, width - scaled(6), height};
  SetTextColor(hdc, g.state.relay ? RGB(242, 186, 106) : RGB(150, 158, 170));
  DrawTextW(hdc, status_text().c_str(), -1, &statusRect,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

  if (oldFont) SelectObject(hdc, oldFont);
  BitBlt(target, 0, 0, width, height, hdc, 0, 0, SRCCOPY);
  SelectObject(hdc, oldBitmap);
  DeleteObject(bitmap);
  DeleteDC(hdc);
}

int hit_test(int x, int y) {
  for (const Button& button : g.buttons) {
    if (x >= button.rect.left && x < button.rect.right && y >= button.rect.top &&
        y < button.rect.bottom) {
      return button.id;
    }
  }
  return kButtonNone;
}

void expand_toolbar() {
  KillTimer(g.hwnd, kTimerExpand);
  if (!g.expanded) {
    g.expanded = true;
    reposition();
  }
  KillTimer(g.hwnd, kTimerCollapse);
}

void collapse_toolbar() {
  KillTimer(g.hwnd, kTimerCollapse);
  if (g.menuOpen || g.pressed != kButtonNone || g.hovered != kButtonNone) return;
  if (g.expanded) {
    g.expanded = false;
    ShowWindow(g.hwnd, SW_HIDE);
  }
}

void show_monitor_menu(const Button& button) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  for (size_t i = 0; i < g.state.monitors.size(); ++i) {
    const SessionToolbarMonitor& monitor = g.state.monitors[i];
    std::wstring label = L"모니터 " + std::to_wstring(i + 1);
    if (monitor.width && monitor.height) {
      label += L"  (" + std::to_wstring(monitor.width) + L"×" + std::to_wstring(monitor.height) + L")";
    }
    if (monitor.primary) label += L"  주";
    const UINT flags =
        MF_STRING | (monitor.id == g.state.selectedMonitorId ? MF_CHECKED : MF_UNCHECKED);
    // Item ids are one-based so that zero can stay "nothing was picked".
    AppendMenuW(menu, flags, static_cast<UINT_PTR>(i + 1), label.c_str());
  }
  POINT origin{button.rect.left, button.rect.bottom};
  ClientToScreen(g.hwnd, &origin);
  g.menuOpen = true;
  const int picked = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                    origin.x, origin.y, 0, g.hwnd, nullptr);
  g.menuOpen = false;
  DestroyMenu(menu);
  if (picked <= 0 || static_cast<size_t>(picked) > g.state.monitors.size()) return;
  const uint32_t monitorId = g.state.monitors[static_cast<size_t>(picked - 1)].id;
  if (g.callbacks.onMonitor) g.callbacks.onMonitor(monitorId);
}

LRESULT CALLBACK toolbar_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    // Clicking the toolbar must not pull focus off the video window: that window is where
    // keystrokes are captured and forwarded, so activating this one would silently stop input.
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
    case WM_MOUSEMOVE: {
      // The mouse is on the bar; it must not vanish out from under the cursor.
      KillTimer(hwnd, kTimerCollapse);
      const int hovered = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (hovered != g.hovered) {
        g.hovered = hovered;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      if (!g.tracking) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        g.tracking = TrackMouseEvent(&track) != FALSE;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      g.tracking = false;
      if (g.hovered != kButtonNone) {
        g.hovered = kButtonNone;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      SetTimer(hwnd, kTimerCollapse, kCollapseDelayMs, nullptr);
      return 0;
    case WM_TIMER:
      if (wp == kTimerExpand) {
        expand_toolbar();
        return 0;
      }
      if (wp == kTimerCollapse) {
        collapse_toolbar();
        return 0;
      }
      break;
    case WM_LBUTTONDOWN:
      g.pressed = hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (g.pressed != kButtonNone) SetCapture(hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_LBUTTONUP: {
      const int pressed = g.pressed;
      g.pressed = kButtonNone;
      if (GetCapture() == hwnd) ReleaseCapture();
      InvalidateRect(hwnd, nullptr, FALSE);
      if (pressed == kButtonNone) return 0;
      if (hit_test(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)) != pressed) return 0;
      if (pressed == kButtonTargets) {
        if (g.callbacks.onTargets) g.callbacks.onTargets();
      } else if (pressed == kButtonMacro) {
        if (g.callbacks.onMacro) g.callbacks.onMacro();
      } else if (pressed == kButtonMonitor) {
        for (const Button& button : g.buttons) {
          if (button.id == kButtonMonitor) {
            show_monitor_menu(button);
            break;
          }
        }
      }
      return 0;
    }
    case kMsgStatePushed: {
      {
        std::lock_guard<std::mutex> lock(gStateMu);
        g.state = gPendingState;
      }
      reposition();
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

bool session_toolbar_create(HWND owner, SessionToolbarCallbacks callbacks) {
  if (g.hwnd) return true;
  if (!owner || !IsWindow(owner)) return false;

  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = toolbar_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

  g.owner = owner;
  g.callbacks = std::move(callbacks);
  g.dpi = static_cast<int>(GetDpiForWindow(owner));
  if (g.dpi <= 0) g.dpi = 96;

  g.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, kClassName,
                           L"", WS_POPUP, 0, 0, 10, 10, owner, nullptr, instance, nullptr);
  if (!g.hwnd) {
    g.owner = nullptr;
    return false;
  }
  SetLayeredWindowAttributes(g.hwnd, 0, 238, LWA_ALPHA);

  LOGFONTW lf{};
  lf.lfHeight = -MulDiv(12, g.dpi, 72);
  lf.lfWeight = FW_NORMAL;
  lstrcpynW(lf.lfFaceName, L"Malgun Gothic", LF_FACESIZE);
  g.font = CreateFontIndirectW(&lf);
  return true;
}

void session_toolbar_set_visible(bool visible) {
  if (!g.hwnd) return;
  g.wanted = visible;
  if (visible) {
    // Shown once on entry so the bar is discoverable, then it gets out of the way. After
    // that, only the top-center dwell brings it back.
    g.expanded = true;
    SetTimer(g.hwnd, kTimerCollapse, kIntroMs, nullptr);
  } else {
    g.expanded = false;
    KillTimer(g.hwnd, kTimerExpand);
    KillTimer(g.hwnd, kTimerCollapse);
  }
  reposition();
}

void session_toolbar_notify_mouse(int x, int y, int clientWidth) {
  if (!g.hwnd || !g.wanted) return;
  const int band = MulDiv(240, g.dpi, 96);
  const bool inZone =
      y >= 0 && y <= MulDiv(16, g.dpi, 96) && x >= clientWidth / 2 - band && x <= clientWidth / 2 + band;
  if (inZone) {
    if (g.expanded) {
      KillTimer(g.hwnd, kTimerCollapse);
    } else {
      // Restarted on every move: the dwell requires the mouse to SETTLE near the top, so a
      // click travelling through the zone on its way to a browser tab never summons the bar
      // into its own path.
      SetTimer(g.hwnd, kTimerExpand, kExpandDwellMs, nullptr);
    }
  } else {
    KillTimer(g.hwnd, kTimerExpand);
    if (g.expanded) SetTimer(g.hwnd, kTimerCollapse, kCollapseDelayMs, nullptr);
  }
}

void session_toolbar_update(const SessionToolbarState& state) {
  if (!g.hwnd) return;
  {
    std::lock_guard<std::mutex> lock(gStateMu);
    gPendingState = state;
  }
  PostMessageW(g.hwnd, kMsgStatePushed, 0, 0);
}

void session_toolbar_follow_owner() { reposition(); }

void session_toolbar_destroy() {
  if (g.font) {
    DeleteObject(g.font);
    g.font = nullptr;
  }
  if (g.hwnd) {
    DestroyWindow(g.hwnd);
    g.hwnd = nullptr;
  }
  g.owner = nullptr;
  g.buttons.clear();
}

}  // namespace remote60::native_poc
