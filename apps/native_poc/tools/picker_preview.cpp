// Draws the real picker and the real toolbar, without a host.
//
// Both are GDI, so the WebView preview cannot reach them, and until now the only way to see
// either was to have a live session -- which meant they were never looked at. The picker's model
// is fed by `WindowPanel::ApplyWindowList`, which takes a plain struct: no socket, no negotiation,
// nothing from the wire path. The drawing is `draw_overlay`, the same function the product calls
// from its paint.
//
// A tool, not a test. It asserts nothing; what it produces is pictures for a person to look at.
// The wording contracts are asserted in session_toolbar_click_test.
//
// What this does NOT show: that a real host produces these lists, that thumbnails arrive, or that
// clicking a card selects anything. Those need a session.

// winsock2 before windows.h: the viewer headers pull in socket types, and windows.h would
// otherwise drag in the older winsock and redefine them.
#include <winsock2.h>

#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "client_session_toolbar.hpp"
#include "poc_protocol.hpp"
#include "viewer_gdi_util.hpp"
#include "viewer_overlay_draw.hpp"
#include "viewer_state.hpp"

namespace {

using remote60::native_poc::viewer::ViewerState;

std::wstring executable_dir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring full(path);
  const size_t slash = full.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

std::wstring preview_dir() {
  const wchar_t* ups[] = {L"\\..\\..\\..\\..\\.claude\\ui-preview",
                          L"\\..\\..\\..\\.claude\\ui-preview"};
  for (const wchar_t* up : ups) {
    wchar_t full[MAX_PATH]{};
    if (PathCanonicalizeW(full, (executable_dir() + up).c_str())) {
      CreateDirectoryW(full, nullptr);
      if (PathFileExistsW(full)) return full;
    }
  }
  return executable_dir();
}

int png_encoder(CLSID* out) {
  UINT count = 0;
  UINT bytes = 0;
  Gdiplus::GetImageEncodersSize(&count, &bytes);
  if (bytes == 0) return -1;
  std::vector<char> buffer(bytes);
  auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
  Gdiplus::GetImageEncoders(count, bytes, codecs);
  for (UINT i = 0; i < count; ++i) {
    if (std::wstring(codecs[i].MimeType) == L"image/png") {
      *out = codecs[i].Clsid;
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool save_png(HBITMAP bitmap, const std::wstring& path) {
  CLSID clsid{};
  if (png_encoder(&clsid) < 0) return false;
  Gdiplus::Bitmap image(bitmap, nullptr);
  return image.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

/** One entry, as the host would have sent it. */
remote60::native_poc::ControlWindowEntry entry(uint64_t id, const char* title, uint32_t w,
                                               uint32_t h) {
  remote60::native_poc::ControlWindowEntry e;
  e.id = id;
  e.pid = static_cast<uint32_t>(1000 + id);
  e.width = w;
  e.height = h;
  std::snprintf(e.title, sizeof(e.title), "%s", title);
  return e;
}

}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  Gdiplus::GdiplusStartupInput gdiplusInput;
  ULONG_PTR gdiplusToken = 0;
  if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
    std::printf("GDI+ would not start\n");
    return 1;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"Remote60PickerPreview";
  RegisterClassExW(&wc);
  // Off screen: the picker is full-window, and nobody at this machine should see it flash past.
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"picker preview", WS_OVERLAPPEDWINDOW, -4000,
                              -4000, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);
  if (!hwnd) {
    std::printf("no window\n");
    return 1;
  }
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);

  const std::wstring outDir = preview_dir();
  std::printf("out  %ls\n", outDir.c_str());

  struct Shot {
    const wchar_t* file;
    const char* what;
    int count;          // how many windows the host reported
    bool connected;
    bool locked;
    uint64_t selected;
    const char* status; // overrides the derived line when not empty
  };
  const Shot shots[] = {
      {L"picker-connecting.png", "not connected yet", 0, false, false, 0, ""},
      {L"picker-empty.png", "connected, no shareable windows", 0, true, false, 0, ""},
      {L"picker-list.png", "connected, four windows", 4, true, false, 0, ""},
      {L"picker-selected.png", "a window is the current target", 4, true, false, 2, ""},
      {L"picker-locked.png", "target fixed by host config", 4, true, true, 0, ""},
      {L"picker-error.png", "the host refused the list", 0, true, false, 0,
       "화면 목록을 가져오지 못했습니다."},
      // The boundaries the grid rule turns on: one card, a few, the point where growing stops,
      // and a list too long to fit at the preferred width.
      {L"picker-count-01.png", "one window", 1, true, false, 0, ""},
      {L"picker-count-04.png", "four windows", 4, true, false, 0, ""},
      {L"picker-count-12.png", "twelve windows", 12, true, false, 0, ""},
      {L"picker-count-30.png", "thirty windows (scrolls)", 30, true, false, 0, ""},
  };

  for (const Shot& shot : shots) {
    // A fresh state per shot: a picker that carried the previous shot's list would photograph
    // something no single moment produces.
    ViewerState ctx;
    ctx.session.hwnd = hwnd;
    remote60::native_poc::viewer::ensure_ui_font(ctx, hwnd);
    ctx.control.connected.store(shot.connected, std::memory_order_relaxed);
    ctx.picker.visible.store(true, std::memory_order_relaxed);

    if (shot.connected) {
      remote60::native_poc::ControlWindowListMessage msg;
      msg.seq = 1;
      msg.flags = shot.locked ? 1u : 0u;
      msg.selectedWindowId = shot.selected;
      const char* titles[] = {"메모장 - 회의록.txt", "Visual Studio Code", "Chrome - GNLink",
                              "탐색기"};
      const uint32_t sizes[][2] = {{1280, 720}, {1920, 1080}, {1600, 900}, {1024, 768}};
      const int count =
          std::min(shot.count, static_cast<int>(remote60::native_poc::kControlWindowListMaxEntries));
      msg.itemCount = static_cast<uint32_t>(count);
      for (int i = 0; i < count; ++i) {
        char title[96];
        std::snprintf(title, sizeof(title), "%s", titles[i % 4]);
        msg.items[i] = entry(static_cast<uint64_t>(i + 1), title, sizes[i % 4][0], sizes[i % 4][1]);
      }
      ctx.picker.windowPanel.ApplyWindowList(msg, 8);
    }
    if (shot.status && *shot.status) ctx.picker.windowPanel.SetStatus(shot.status);

    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old = SelectObject(mem, bitmap);

    remote60::native_poc::viewer::draw_overlay(ctx, mem);

    SelectObject(mem, old);
    const std::wstring out = outDir + L"\\" + shot.file;
    std::printf("  %-26ls %s  %s\n", shot.file, save_png(bitmap, out) ? "ok  " : "FAIL",
                shot.what);

    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    remote60::native_poc::viewer::destroy_cached_gdi_objects(ctx);
  }

  // ------------------------------------------------------------------ the toolbar, same window
  //
  // Its pixels had never been looked at either: the bar is created by the session and disappears
  // with it. Here it is built against the same off-screen owner and photographed in each of the
  // three path states -- the ones that used to be two.
  {
    remote60::native_poc::SessionToolbarCallbacks callbacks;
    callbacks.onTargets = [] {};
    callbacks.onMacro = [] {};
    remote60::native_poc::session_toolbar_create(hwnd, std::move(callbacks));
    remote60::native_poc::session_toolbar_set_visible(true);
    remote60::native_poc::session_toolbar_follow_owner();

    struct Bar {
      const wchar_t* file;
      bool connected;
      bool known;
      bool relay;
      const char* what;
    };
    const Bar bars[] = {
        {L"toolbar-path-unknown.png", false, false, false, "connecting, path not yet decided"},
        {L"toolbar-path-direct.png", true, true, false, "connected, direct"},
        {L"toolbar-path-relay.png", true, true, true, "connected, relay"},
    };
    HWND bar = FindWindowExW(nullptr, nullptr, L"Remote60SessionToolbar", nullptr);
    for (const Bar& shot : bars) {
      remote60::native_poc::SessionToolbarState state;
      state.connected = shot.connected;
      state.inputOn = true;
      state.pathKnown = shot.known;
      state.relay = shot.relay;
      state.fps = shot.connected ? 60 : 0;
      remote60::native_poc::session_toolbar_update(state);

      MSG msg;
      const DWORD until = GetTickCount() + 200;
      while (GetTickCount() < until) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
        Sleep(10);
      }

      bool saved = false;
      if (bar) {
        RECT r{};
        GetWindowRect(bar, &r);
        const int w = r.right - r.left;
        const int h = r.bottom - r.top;
        if (w > 0 && h > 0) {
          HDC screen = GetDC(nullptr);
          HDC mem = CreateCompatibleDC(screen);
          HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
          HGDIOBJ old = SelectObject(mem, bitmap);
          // PW_RENDERFULLCONTENT: the bar is a layered window, and the plain form comes back
          // empty for those.
          if (PrintWindow(bar, mem, 2) || PrintWindow(bar, mem, 0)) {
            SelectObject(mem, old);
            saved = save_png(bitmap, outDir + L"\\" + shot.file);
          } else {
            SelectObject(mem, old);
          }
          DeleteObject(bitmap);
          DeleteDC(mem);
          ReleaseDC(nullptr, screen);
        }
      }
      std::printf("  %-26ls %s  %s\n", shot.file, saved ? "ok  " : "FAIL", shot.what);
    }
    remote60::native_poc::session_toolbar_destroy();
  }

  DestroyWindow(hwnd);
  Gdiplus::GdiplusShutdown(gdiplusToken);
  std::printf("\nLIMIT: the lists here were handed to the panel directly. That a real host\n"
              "       produces them, that thumbnails arrive, and that clicking a card selects\n"
              "       anything all need a session and are not shown.\n");
  return 0;
}
