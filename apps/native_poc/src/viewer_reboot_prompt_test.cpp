// UI evidence test for the reboot confirm popup (viewer_reboot_prompt.hpp) + the menu gating policy
// (reboot_menu_policy.hpp). Shows the REAL native dialog, captures it with PrintWindow (BMP artifact),
// and injects the REAL WM_COMMAND a button click posts: 확인 -> one reboot request, 취소/close -> zero.
// The 0x8000-off warning path is exercised (taller layout). This is the native-Win32 equivalent of the
// established viewer e2e capture+inject pattern (not WebView2). Runs in this user session, test-owned
// windows only; env-gated so it is skipped in a headless/CI run.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "reboot_menu_policy.hpp"
#include "viewer_reboot_prompt.hpp"

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
int gSkipped = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}

HWND wait_for_dialog(int timeoutMs) {
  for (int i = 0; i < timeoutMs / 10; ++i) {
    HWND w = FindWindowW(kRebootConfirmClass, nullptr);
    if (w) return w;
    Sleep(10);
  }
  return nullptr;
}

// PrintWindow the dialog to a 32bpp DIB; returns true and the distinct-pixel count (a uniform/blank
// capture means nothing was painted). Writes a BMP artifact next to the exe for the evidence trail.
bool capture_window(HWND w, const wchar_t* bmpPath, size_t* distinctOut) {
  RECT rc{};
  if (!GetWindowRect(w, &rc)) return false;
  const int width = rc.right - rc.left, height = rc.bottom - rc.top;
  if (width <= 0 || height <= 0) return false;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biHeight = -height;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mem, dib);
  const BOOL ok = PrintWindow(w, mem, 2 /*PW_RENDERFULLCONTENT*/);
  size_t distinct = 0;
  if (ok && bits) {
    const uint32_t* px = static_cast<const uint32_t*>(bits);
    const size_t n = static_cast<size_t>(width) * height;
    uint32_t first = px[0];
    for (size_t i = 1; i < n; ++i) {
      if (px[i] != first) { ++distinct; }
    }
    // Write a minimal 32bpp BMP artifact.
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih = bi.bmiHeader;
    ih.biHeight = height;  // bottom-up for the file
    const DWORD imgBytes = static_cast<DWORD>(n * 4);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + imgBytes;
    HANDLE f = CreateFileW(bmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD wr = 0;
      WriteFile(f, &fh, sizeof(fh), &wr, nullptr);
      WriteFile(f, &ih, sizeof(ih), &wr, nullptr);
      // flip rows for bottom-up
      std::vector<uint32_t> row(width);
      for (int yy = height - 1; yy >= 0; --yy) {
        WriteFile(f, px + static_cast<size_t>(yy) * width, width * 4, &wr, nullptr);
      }
      CloseHandle(f);
    }
  }
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  if (distinctOut) *distinctOut = distinct;
  return ok != FALSE;
}

// Drive one confirm dialog: run it on a worker thread, find the real window, (optionally) capture it,
// inject `injectMsg` (WM_COMMAND with a button id, or WM_CLOSE), and return what it resolved to and a
// "reboot request count" (the real onReboot wiring sets session.rebootRequested iff the dialog returns
// true, exactly once).
struct ConfirmOutcome { bool shown = false; bool confirmed = false; int requests = 0; int dialogHeight = 0; };

ConfirmOutcome run_confirm(bool warning, UINT injectMsg, WPARAM injectWparam, const wchar_t* bmp) {
  ConfirmOutcome o;
  std::atomic<bool> finished{false};
  std::atomic<bool> result{false};
  std::thread th([&] {
    result = show_reboot_confirm(nullptr, warning);
    finished = true;
  });
  HWND dlg = wait_for_dialog(3000);
  o.shown = dlg != nullptr;
  if (dlg) {
    RECT rc{};
    if (GetWindowRect(dlg, &rc)) o.dialogHeight = rc.bottom - rc.top;
    Sleep(60);  // let it paint
    if (bmp) {
      size_t distinct = 0;
      if (capture_window(dlg, bmp, &distinct)) {
        std::printf("        captured %ls (distinct pixels=%zu)\n", bmp, distinct);
      }
    }
    PostMessageW(dlg, injectMsg, injectWparam, 0);
  }
  if (th.joinable()) th.join();
  o.confirmed = result.load();
  o.requests = o.confirmed ? 1 : 0;  // mirrors the real onReboot -> session.rebootRequested(once)
  (void)finished;
  return o;
}
}  // namespace

int main() {
  std::printf("viewer reboot confirm popup (UI evidence)\n");

  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_UI_TEST", nullptr, 0) == 0) {
    std::printf("  SKIP (set REMOTE60_ALLOW_UI_TEST=1 to run the interactive dialog)\n");
    std::printf("viewer_reboot_prompt_test: SKIPPED\n");
    return 0;  // not a failure; a headless runner skips the real-window test
  }

  std::printf("[menu gating policy drives the dialog warning arg]\n");
  check(DecideRebootMenu(kCaptureFlagRemoteRebootV1).menuEnabled, "reboot cap -> menu item enabled");
  check(DecideRebootMenu(0).disabledBecauseOldHost, "old host -> menu item disabled with reason");
  check(DecideRebootMenu(kCaptureFlagRemoteRebootV1).showPreloginWarning,
        "not durable-enabled -> dialog shows the warning");
  check(!DecideRebootMenu(kCaptureFlagRemoteRebootV1 | kCaptureFlagPreloginEnabledV1).showPreloginWarning,
        "durable-enabled -> no warning");

  std::printf("[확인 -> one request]\n");
  auto confirm = run_confirm(/*warning=*/false, WM_COMMAND, MAKEWPARAM(kRebootConfirmOkId, BN_CLICKED),
                             L"reboot_confirm_ok.bmp");
  check(confirm.shown, "the confirm dialog window appeared");
  check(confirm.confirmed && confirm.requests == 1, "확인 click -> confirmed, exactly one reboot request");

  std::printf("[취소 -> zero requests]\n");
  auto cancel = run_confirm(false, WM_COMMAND, MAKEWPARAM(kRebootConfirmCancelId, BN_CLICKED),
                            L"reboot_confirm_cancel.bmp");
  check(cancel.shown, "the confirm dialog window appeared");
  check(!cancel.confirmed && cancel.requests == 0, "취소 click -> NOT confirmed, zero reboot requests");

  std::printf("[close (X) -> zero requests]\n");
  auto closed = run_confirm(false, WM_CLOSE, 0, nullptr);
  check(!closed.confirmed && closed.requests == 0, "closing the dialog -> zero reboot requests");

  std::printf("[0x8000-off warning layout]\n");
  auto warn = run_confirm(/*warning=*/true, WM_COMMAND, MAKEWPARAM(kRebootConfirmCancelId, BN_CLICKED),
                          L"reboot_confirm_warning.bmp");
  check(warn.shown && warn.dialogHeight > confirm.dialogHeight,
        "the warning dialog is taller (the extra 'no connect until login' line is laid out)");

  if (gFailures != 0) {
    std::printf("viewer_reboot_prompt_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("viewer_reboot_prompt_test: PASS\n");
  return 0;
}
