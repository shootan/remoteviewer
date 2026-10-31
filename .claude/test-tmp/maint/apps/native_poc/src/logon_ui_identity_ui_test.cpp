// lockscreen-cred r2 (Codex L4): the password field's identity and the replacing typing, against REAL edit
// controls through UI Automation (logon_ui_target.cpp -- the code the lock-screen agent and the pre-login worker
// run). One window holds two password fields (A, B), a read-only password field and a user-name field. What it
// pins:
//   - A and B of the SAME window are different targets (the element is part of the identity); a non-password
//     field is no target;
//   - ReplaceWithUnicode for A while the focus is on B types nothing (kNotTypedError) into either;
//   - ReplaceWithUnicode for A with an earlier value in A leaves exactly the new value (never appended);
//   - a read-only password field: nothing typed (the refused clear is not "no pattern").
// It brings its own window to the front and sends real input to it (REMOTE60_ALLOW_UI_TEST=1); test strings only.

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

#include "logon_ui_target.hpp"

namespace lu = remote60::native_poc::logon_ui;

namespace {
int gChecks = 0, gFailures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

struct Win {
  HWND main = nullptr, a = nullptr, b = nullptr, ro = nullptr, user = nullptr;
  DWORD tid = 0;
  std::atomic<bool> ready{false};
  std::thread t;
  static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) return 0;
    return DefWindowProcW(h, m, w, l);
  }
  void Start() {
    t = std::thread([this] {
      tid = GetCurrentThreadId();
      WNDCLASSW wc{};
      wc.lpfnWndProc = Proc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = L"Remote60LogonUiIdentityTest";
      wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
      RegisterClassW(&wc);
      main = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"remote60 logon ui identity test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             60, 60, 420, 200, nullptr, nullptr, wc.hInstance, nullptr);
      const DWORD edit = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP;
      user = CreateWindowExW(0, L"EDIT", L"", edit, 10, 6, 380, 24, main, nullptr, wc.hInstance, nullptr);
      a = CreateWindowExW(0, L"EDIT", L"", edit | ES_PASSWORD, 10, 36, 380, 24, main, nullptr, wc.hInstance, nullptr);
      b = CreateWindowExW(0, L"EDIT", L"", edit | ES_PASSWORD, 10, 66, 380, 24, main, nullptr, wc.hInstance, nullptr);
      ro = CreateWindowExW(0, L"EDIT", L"", edit | ES_PASSWORD | ES_READONLY, 10, 96, 380, 24, main, nullptr, wc.hInstance, nullptr);
      ready = true;
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      DestroyWindow(main);
    });
    while (!ready.load()) Sleep(10);
  }
  // Brings the window to the front with `field` focused (the window's thread does the focusing).
  bool Focus(HWND field) {
    for (int i = 0; i < 30; ++i) {
      const DWORD me = GetCurrentThreadId();
      HWND fg = GetForegroundWindow();
      const DWORD ft = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
      if (ft && ft != me) AttachThreadInput(me, ft, TRUE);
      AttachThreadInput(me, tid, TRUE);
      ShowWindow(main, SW_SHOWNORMAL);
      BringWindowToTop(main);
      SetForegroundWindow(main);
      SetFocus(field);
      AttachThreadInput(me, tid, FALSE);
      if (ft && ft != me) AttachThreadInput(me, ft, FALSE);
      Sleep(80);
      GUITHREADINFO gi{};
      gi.cbSize = sizeof(gi);
      if (GetForegroundWindow() == main && GetGUIThreadInfo(tid, &gi) && gi.hwndFocus == field) return true;
    }
    return false;
  }
  static std::wstring Text(HWND e) {
    wchar_t buf[256] = {};
    GetWindowTextW(e, buf, 255);
    return buf;
  }
  void Stop() {
    PostThreadMessageW(tid, WM_QUIT, 0, 0);
    if (t.joinable()) t.join();
  }
};
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_UI_TEST", nullptr, 0) == 0) {
    std::printf("SKIP  logon_ui_identity_ui_test (brings a window to the front and sends input to it)\n"
                "      Set REMOTE60_ALLOW_UI_TEST=1 to run it.\n\nRESULT: SKIPPED\n");
    return 0;
  }
  std::printf("the password field's identity and the replacing typing, on real edit controls (lockscreen-cred r2, L4)\n\n");
  Win w;
  w.Start();
  const DWORD pid = GetCurrentProcessId();
  const std::u16string secret = u"New-pw-ä가";
  const std::vector<uint16_t> units(secret.begin(), secret.end());

  const bool fa = w.Focus(w.a);
  const uint64_t idA = lu::FocusedPasswordFieldIdentity(w.main, pid);
  const bool fb = w.Focus(w.b);
  const uint64_t idB = lu::FocusedPasswordFieldIdentity(w.main, pid);
  const bool fu = w.Focus(w.user);
  const uint64_t idU = lu::FocusedPasswordFieldIdentity(w.main, pid);
  check("focus set on A, B and the user-name field in turn", fa && fb && fu);
  check("A and B (two password fields of ONE window) are different targets; both nonzero, process id in the high half",
        idA != 0 && idB != 0 && idA != idB && (idA >> 32) == pid && (idB >> 32) == pid);
  check("the user-name field (not a password field) is no target", idU == 0);
  w.Focus(w.a);
  check("A's identity is stable while it keeps the focus", lu::FocusedPasswordFieldIdentity(w.main, pid) == idA);

  // Replace for A while the focus is on B: nothing typed anywhere.
  SetWindowTextW(w.a, L"");
  SetWindowTextW(w.b, L"");
  w.Focus(w.b);
  uint32_t err = 0;
  const bool wrong = lu::ReplaceWithUnicode(idA, units.data(), static_cast<uint16_t>(units.size()), &err);
  Sleep(200);
  check("NEGATIVE: replace for A while the focus is on B (same window): refused, kNotTypedError, nothing typed",
        !wrong && err == lu::kNotTypedError && Win::Text(w.a).empty() && Win::Text(w.b).empty(),
        "err=" + std::to_string(err));

  // Replace for A with an earlier value in A: exactly the new value.
  SetWindowTextW(w.a, L"earlier-attempt");
  w.Focus(w.a);
  err = 0;
  const bool replaced = lu::ReplaceWithUnicode(idA, units.data(), static_cast<uint16_t>(units.size()), &err);
  Sleep(300);
  const std::wstring gotA = Win::Text(w.a);
  check("replace for A (focused, holding an earlier value): A holds exactly the new value -- not appended",
        replaced && gotA == std::wstring(secret.begin(), secret.end()), "err=" + std::to_string(err) + " len=" + std::to_string(gotA.size()));
  check("...and nothing went into B or the user-name field", Win::Text(w.b).empty() && Win::Text(w.user).empty());

  // A read-only password field: the clear is refused -> nothing typed (not treated as "no pattern").
  SetWindowTextW(w.ro, L"fixed");
  const bool fro = w.Focus(w.ro);
  const uint64_t idRo = lu::FocusedPasswordFieldIdentity(w.main, pid);
  err = 0;
  const bool roTyped = idRo != 0 && lu::ReplaceWithUnicode(idRo, units.data(), static_cast<uint16_t>(units.size()), &err);
  Sleep(200);
  check("NEGATIVE: a read-only password field: refused (kNotTypedError), its value untouched, nothing typed elsewhere",
        fro && idRo != 0 && !roTyped && err == lu::kNotTypedError && Win::Text(w.ro) == L"fixed" && Win::Text(w.b).empty(),
        "idRo=" + std::to_string(idRo != 0) + " err=" + std::to_string(err) + " ro=" + std::string(Win::Text(w.ro).size(), '*'));

  w.Stop();
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
