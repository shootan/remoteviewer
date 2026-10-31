# -*- coding: utf-8 -*-
"""Shows why the connection failed, in the window that is already there.

The reason was known all along -- `connect failed 127.0.0.1:43999` went to stderr -- and the
window closed before anyone could read anything. Nothing about the connection changes here: the
success path returns exactly where it did, and the only new behaviour is on the way out.
"""
import io

# ---------------------------------------------------------------- declaration
p = 'apps/native_poc/src/viewer_startup.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''int connect_media_socket(ViewerContext& ctx);           // 4 / 5 / 6 (socket options, connect, UDP hello)'''
new = '''int connect_media_socket(ViewerContext& ctx);           // 4 / 5 / 6 (socket options, connect, UDP hello)

/**
 * Puts a failure on the screen and waits for the user to decide.
 *
 * Returns true when they asked to try again. Called only after a startup step has failed, and
 * only from the failure path -- a successful start never reaches it.
 *
 * It exists because the program used to know exactly why it could not connect, write that to
 * stderr, and close the window within about a tenth of a second. What the user got was a black
 * rectangle that vanished.
 */
bool show_startup_failure(ViewerContext& ctx, const std::string& reason);'''
assert d.count(old) == 1, 'hpp anchor'
d = d.replace(old, new, 1)
if '#include <string>' not in d:
    d = d.replace('#pragma once', '#pragma once\n\n#include <string>', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('hpp ok')

# ---------------------------------------------------------------- the screen
p = 'apps/native_poc/src/viewer_startup.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''int connect_media_socket(ViewerContext& ctx) {'''
new = '''namespace {

// Laid out from the client rect each paint, so the buttons are where they are drawn at any size
// and DPI. Two rects and a message; nothing here is a control in the Win32 sense.
struct FailureLayout {
  RECT text{};
  RECT retry{};
  RECT close{};
};

FailureLayout failure_layout(HWND hwnd) {
  RECT client{};
  GetClientRect(hwnd, &client);
  const int w = client.right - client.left;
  const int h = client.bottom - client.top;
  const int bw = 150;
  const int bh = 40;
  const int gap = 14;
  FailureLayout out;
  out.text = RECT{client.left, client.top + h / 2 - 70, client.right, client.top + h / 2 - 10};
  const int y = client.top + h / 2 + 20;
  out.retry = RECT{client.left + w / 2 - bw - gap / 2, y, client.left + w / 2 - gap / 2, y + bh};
  out.close = RECT{client.left + w / 2 + gap / 2, y, client.left + w / 2 + bw + gap / 2, y + bh};
  return out;
}

bool point_in(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

}  // namespace

bool show_startup_failure(ViewerContext& ctx, const std::string& reason) {
  HWND hwnd = ctx.session.hwnd;
  if (!hwnd) return false;   // nothing to show it in; the caller falls through to its exit code

  bool retry = false;
  bool done = false;

  // Painted directly rather than through the viewer's present path: there is no stream, no
  // swapchain and no decoder at this point, and routing through them to draw two rectangles
  // would tie a failure screen to the machinery that failed.
  auto paint = [&](HDC hdc) {
    const FailureLayout layout = failure_layout(hwnd);
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH back = CreateSolidBrush(RGB(15, 19, 25));
    FillRect(hdc, &client, back);
    DeleteObject(back);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(232, 234, 237));
    const std::wstring wide = remote60::native_poc::viewer::widen_utf8(reason);
    RECT text = layout.text;
    DrawTextW(hdc, wide.c_str(), -1, &text,
              DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL);

    auto button = [&](const RECT& r, const wchar_t* label, bool primary) {
      HBRUSH fill = CreateSolidBrush(primary ? RGB(59, 130, 246) : RGB(38, 46, 59));
      FillRect(hdc, &r, fill);
      DeleteObject(fill);
      SetTextColor(hdc, primary ? RGB(255, 255, 255) : RGB(200, 206, 216));
      RECT t = r;
      DrawTextW(hdc, label, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    button(layout.retry, L"다시 시도", true);
    button(layout.close, L"닫기", false);
  };

  // A loop of its own rather than a flag checked by the main pump: the main pump has not started
  // and the receiver threads do not exist yet.
  InvalidateRect(hwnd, nullptr, TRUE);
  MSG msg;
  while (!done) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        done = true;
        break;
      }
      if (msg.hwnd == hwnd && msg.message == WM_LBUTTONUP) {
        const FailureLayout layout = failure_layout(hwnd);
        const int x = GET_X_LPARAM(msg.lParam);
        const int y = GET_Y_LPARAM(msg.lParam);
        // Only when the user presses it. An automatic retry would overwrite the log line that
        // says what went wrong, which is the one thing worth keeping here.
        if (point_in(layout.retry, x, y)) {
          retry = true;
          done = true;
          break;
        }
        if (point_in(layout.close, x, y)) {
          done = true;
          break;
        }
      }
      if (msg.hwnd == hwnd && msg.message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        continue;
      }
      if (msg.hwnd == hwnd && (msg.message == WM_CLOSE || msg.message == WM_DESTROY)) {
        done = true;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (done) break;
    // The window proc may have swallowed WM_PAINT; draw straight to the DC so the screen is not
    // left blank while we wait.
    HDC hdc = GetDC(hwnd);
    if (hdc) {
      paint(hdc);
      ReleaseDC(hwnd, hdc);
    }
    Sleep(30);
  }
  return retry;
}

int connect_media_socket(ViewerContext& ctx) {'''
assert d.count(old) == 1, 'cpp anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('cpp ok')
