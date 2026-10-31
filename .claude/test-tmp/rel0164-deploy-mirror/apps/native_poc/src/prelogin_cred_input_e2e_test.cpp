// (e) end-to-end evidence for the "Windows 로그인" credential path (prelogin-service r3): a credential
// sealed for the PRELOGIN purpose (BuildPreloginCred* labels -- the viewer's run_unlock_exchange prelogin
// branch), opened on the host side, and delivered as real Unicode keystrokes to a TEST-OWNED window, byte
// for byte, with a defocused negative control. This joins the two halves already proven separately
// (prelogin_cred_seal_test = prelogin-purpose crypto + cross-purpose rejection; the A0 probe = sealed ->
// SendInput delivery) into the one chain the contract names for (e). It uses the product sealed_unlock
// primitives (no stub) and the product inject_message SendInput(UNICODE) shape. The live SYSTEM-side
// injection onto the console Winlogon/LogonUI (and the real Windows password provider) is the G2 check;
// this proves crypto(prelogin purpose) -> open -> Unicode-to-a-window, non-elevated, test-owned.
//
// env-gated (REMOTE60_ALLOW_UI_TEST) so a headless runner skips the focus-dependent stage.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sealed_unlock.hpp"

using namespace remote60::native_poc::sealed_unlock;

namespace {
int gFailures = 0;
int gSkipped = 0;
void ok(bool c, const char* what) {
  std::printf(c ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!c) ++gFailures;
}

UnlockContext ctx_with(const uint8_t hostPub[kPubKeyBytes], const uint8_t clientPub[kPubKeyBytes]) {
  UnlockContext c;
  c.protocolVersion = 1;
  c.hostId = 0x1122334455667788ull;
  c.clientSessionCookie = 0xA0A0A0A0u;
  for (int i = 0; i < static_cast<int>(kChallengeIdBytes); ++i) c.challengeId[i] = static_cast<uint8_t>(0x50 + i);
  c.requestId = 21;
  c.consoleSession = 1;
  c.accountId = 0xA0C0DE01ull;
  std::memcpy(c.hostPub, hostPub, kPubKeyBytes);
  std::memcpy(c.clientPub, clientPub, kPubKeyBytes);
  return c;
}

// --- test-owned input target (mirrors the A0 probe) ---
HWND g_main = nullptr, g_edit = nullptr, g_static = nullptr;
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
bool make_window() {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"PreloginCredE2EWnd";
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  if (!RegisterClassExW(&wc)) return false;
  g_main = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"PreloginCredE2E",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 420, 160,
                           nullptr, nullptr, wc.hInstance, nullptr);
  if (!g_main) return false;
  g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 10, 390,
                           26, g_main, reinterpret_cast<HMENU>(1), wc.hInstance, nullptr);
  g_static = CreateWindowExW(0, L"STATIC", L"(sink)", WS_CHILD | WS_VISIBLE, 10, 46, 390, 26, g_main,
                             reinterpret_cast<HMENU>(2), wc.hInstance, nullptr);
  return g_edit && g_static;
}
bool force_fg(HWND focus) {
  HWND fg = GetForegroundWindow();
  DWORD ft = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
  if (ft && ft != me) AttachThreadInput(me, ft, TRUE);
  ShowWindow(g_main, SW_SHOW);
  BringWindowToTop(g_main);
  SetForegroundWindow(g_main);
  SetFocus(focus);
  if (ft && ft != me) AttachThreadInput(me, ft, FALSE);
  MSG m;
  for (int i = 0; i < 50 && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++i) { TranslateMessage(&m); DispatchMessageW(&m); }
  return GetForegroundWindow() == g_main;
}
void pump() { MSG m; for (int i = 0; i < 200 && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++i) { TranslateMessage(&m); DispatchMessageW(&m); } }
void send_units(const std::vector<uint16_t>& u) {
  std::vector<INPUT> in;
  for (uint16_t c : u) {
    INPUT d = {}; d.type = INPUT_KEYBOARD; d.ki.wScan = c; d.ki.dwFlags = KEYEVENTF_UNICODE;
    INPUT up = d; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    in.push_back(d); in.push_back(up);
  }
  if (!in.empty()) SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
}
std::wstring edit_text() { wchar_t b[256] = {}; GetWindowTextW(g_edit, b, 255); return b; }
}  // namespace

int main() {
  std::printf("prelogin credential input e2e (prelogin-purpose sealed -> Unicode)\n");

  // ---- crypto: prelogin-purpose sealed exchange (always runs; no display needed) ----
  EcdhKeyPair host, client;
  ok(host.Generate() && client.Generate(), "keypairs");
  uint8_t hostPub[kPubKeyBytes], clientPub[kPubKeyBytes];
  host.ExportPublic(hostPub);
  client.ExportPublic(clientPub);
  UnlockContext ctx = ctx_with(hostPub, clientPub);
  const auto info = BuildPreloginCredKdfInfo(ctx);  // PRELOGIN purpose labels
  const auto aad = BuildPreloginCredAad(ctx);
  uint8_t salt[kSaltBytes];
  RandomBytes(salt, kSaltBytes);
  uint8_t ck[kAesKeyBytes], hk[kAesKeyBytes];
  ok(DeriveAesKey(client, hostPub, salt, info.data(), info.size(), ck) &&
         DeriveAesKey(host, clientPub, salt, info.data(), info.size(), hk) &&
         std::memcmp(ck, hk, kAesKeyBytes) == 0,
     "prelogin-purpose key agreement");

  const std::vector<uint16_t> secret = {L'W', L'i', L'n', 0x00E4 /*ä*/, 0xAC00 /*가*/, L'9', L'!'};
  uint8_t plain[kPlaintextBytes];
  ok(PackPassword(secret.data(), static_cast<uint16_t>(secret.size()), plain), "pack credential");
  uint8_t nonce[kNonceBytes];
  RandomBytes(nonce, kNonceBytes);
  uint8_t cipher[kPlaintextBytes], tag[kTagBytes];
  ok(AesGcmSeal(ck, nonce, aad.data(), aad.size(), plain, kPlaintextBytes, cipher, tag), "client seals (prelogin)");

  uint8_t opened[kPlaintextBytes];
  ok(AesGcmOpen(hk, nonce, aad.data(), aad.size(), cipher, kPlaintextBytes, tag, opened), "host opens (prelogin)");
  uint16_t out[kMaxPasswordUtf16];
  uint16_t count = 0;
  const bool unpacked = UnpackPassword(opened, out, &count);
  ok(unpacked && count == secret.size() && std::memcmp(out, secret.data(), count * sizeof(uint16_t)) == 0,
     "host recovers the credential byte-exact");
  std::vector<uint16_t> recovered(out, out + (unpacked ? count : 0));
  SecureZero(ck, sizeof ck); SecureZero(hk, sizeof hk); SecureZero(plain, sizeof plain); SecureZero(opened, sizeof opened);

  // ---- delivery: the opened credential typed as Unicode into a test-owned window ----
  if (GetEnvironmentVariableW(L"REMOTE60_ALLOW_UI_TEST", nullptr, 0) == 0) {
    std::printf("  SKIP (set REMOTE60_ALLOW_UI_TEST=1 for the SendInput delivery stage)\n");
    ++gSkipped;
  } else if (recovered.empty()) {
    ok(false, "no credential to deliver");
  } else if (!make_window()) {
    std::printf("  SKIP (no interactive window station)\n");
    ++gSkipped;
  } else {
    if (force_fg(g_edit)) {
      SetWindowTextW(g_edit, L"");
      pump();
      send_units(recovered);
      for (int i = 0; i < 40; ++i) { pump(); Sleep(10); }
      std::wstring got = edit_text(), want(recovered.begin(), recovered.end());
      ok(got == want, "opened credential reached the focused EDIT via SendInput(UNICODE), byte-exact");
      // negative control: defocused -> nothing arrives.
      SetWindowTextW(g_edit, L"");
      force_fg(g_static);
      pump();
      send_units(recovered);
      for (int i = 0; i < 40; ++i) { pump(); Sleep(10); }
      ok(edit_text().empty(), "NEGATIVE: with the EDIT defocused the same SendInput does not fill it");
    } else {
      std::printf("  SKIP (could not acquire foreground; display dependency)\n");
      ++gSkipped;
    }
    DestroyWindow(g_main);
  }

  std::printf("\nsummary: failures=%d skipped=%d\n", gFailures, gSkipped);
  if (gFailures != 0) { std::printf("prelogin_cred_input_e2e_test: FAIL\n"); return 1; }
  std::printf("prelogin_cred_input_e2e_test: PASS\n");
  return 0;
}
