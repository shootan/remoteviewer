// prelogin_a0_probe.cpp -- A0 (isolated measurement) for the prelogin-service feature.
//
// Contract: .claude/plans/prelogin_service_2026-10-07.md (Codex r1 + verifier r2 + Codex r3).
// This probe is a DIAGNOSTIC harness, not a product unit test. It de-risks the single most novel
// mechanism the design introduces: a sealed (ECDH+AES-GCM) credential blob, once opened, being
// delivered as real Unicode keystrokes to an input target -- "crypto -> IPC -> input" (r3 section 3:
// "안전한 시험 창에 Unicode가 도달한 결과는 crypto->IPC->input 전달 증거다"). It runs fully in the
// current interactive session as a NON-elevated, test-owned window. It does NOT touch the product
// service, the install, SYSTEM/Winlogon, or any real credential.
//
// What is and is NOT proven here (kept honest per the contract):
//   PROVEN  : the real sealed_unlock primitives round-trip a credential string, and the opened
//             UTF-16 reaches a test-owned EDIT via the product's SendInput(KEYEVENTF_UNICODE) shape,
//             byte-for-byte; and a defocused negative control does NOT receive it.
//   NOT here: SYSTEM-side injection onto winsta0\Winlogon, UAC secure-desktop capture, cold LogonUI,
//             a real password provider, SendSAS policy, and the post-login handoff. Those are the
//             elevation/user-gated G1..G4 items (see .claude/a0_prelogin_measurements.md) and A1.
//
// Build: remote60_prelogin_a0_probe -> PreloginA0Probe.exe (links remote60_sealed_unlock, user32).

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
int gInconclusive = 0;

void ok(bool cond, const char* what) {
  std::printf(cond ? "  ok    %s\n" : "  FAIL  %s\n", what);
  if (!cond) ++gFailures;
}
void inconclusive(const char* what, const char* why) {
  std::printf("  SKIP? %s  [%s]\n", what, why);
  ++gInconclusive;
}

// Build the shared public context both sides serialize into AAD/KDF-info. Mirrors the product
// make_ctx shape; values are fixed test constants (no secrets).
UnlockContext make_ctx(const uint8_t hostPub[kPubKeyBytes], const uint8_t clientPub[kPubKeyBytes]) {
  UnlockContext c;
  c.protocolVersion = 1;
  c.hostId = 0x1122334455667788ull;
  c.clientSessionCookie = 0xA0A0A0A0u;
  for (int i = 0; i < static_cast<int>(kChallengeIdBytes); ++i) c.challengeId[i] = static_cast<uint8_t>(0x30 + i);
  c.requestId = 11;
  c.requesterSession = 2;
  c.consoleSession = 1;
  c.lockGeneration = 0;      // prelogin: no lock generation yet
  c.topologyGeneration = 5;
  c.issuedMs = 1000;
  c.expiresMs = 31000;
  c.accountId = 0xA0C0DE01ull;
  std::memcpy(c.hostPub, hostPub, kPubKeyBytes);
  std::memcpy(c.clientPub, clientPub, kPubKeyBytes);
  return c;
}

// ---- Stage A: real sealed exchange of a credential-shaped string ----------------------------------
// Returns the recovered UTF-16 units (empty on failure). Exercises the exact product path a prelogin
// credential-text delivery will use with the CURRENT v1 (password-only) sealed layout.
std::vector<uint16_t> sealed_roundtrip(const std::vector<uint16_t>& secret) {
  EcdhKeyPair host, client;
  if (!host.Generate() || !client.Generate()) { ok(false, "stageA: keypairs generate"); return {}; }
  uint8_t hostPub[kPubKeyBytes], clientPub[kPubKeyBytes];
  if (!host.ExportPublic(hostPub) || !client.ExportPublic(clientPub)) { ok(false, "stageA: export public"); return {}; }

  UnlockContext ctx = make_ctx(hostPub, clientPub);
  const auto info = BuildKdfInfo(ctx);
  const auto aad = BuildAad(ctx);
  uint8_t salt[kSaltBytes];
  if (!RandomBytes(salt, kSaltBytes)) { ok(false, "stageA: salt"); return {}; }

  uint8_t clientKey[kAesKeyBytes], hostKey[kAesKeyBytes];
  const bool derived = DeriveAesKey(client, hostPub, salt, info.data(), info.size(), clientKey) &&
                       DeriveAesKey(host, clientPub, salt, info.data(), info.size(), hostKey);
  ok(derived && std::memcmp(clientKey, hostKey, kAesKeyBytes) == 0, "stageA: both sides derive the same AES key");

  uint8_t plain[kPlaintextBytes];
  ok(PackPassword(secret.data(), static_cast<uint16_t>(secret.size()), plain), "stageA: pack credential");
  uint8_t nonce[kNonceBytes];
  RandomBytes(nonce, kNonceBytes);
  uint8_t cipher[kPlaintextBytes], tag[kTagBytes];
  ok(AesGcmSeal(clientKey, nonce, aad.data(), aad.size(), plain, kPlaintextBytes, cipher, tag), "stageA: client seals");

  uint8_t opened[kPlaintextBytes];
  ok(AesGcmOpen(hostKey, nonce, aad.data(), aad.size(), cipher, kPlaintextBytes, tag, opened), "stageA: host opens");

  uint16_t out[kMaxPasswordUtf16];
  uint16_t count = 0;
  const bool unpacked = UnpackPassword(opened, out, &count);
  ok(unpacked && count == secret.size() && std::memcmp(out, secret.data(), count * sizeof(uint16_t)) == 0,
     "stageA: opened credential matches the sealed one (byte-exact, incl. non-ASCII)");

  std::vector<uint16_t> recovered(out, out + (unpacked ? count : 0));
  // Scrub.
  SecureZero(clientKey, sizeof clientKey);
  SecureZero(hostKey, sizeof hostKey);
  SecureZero(plain, sizeof plain);
  SecureZero(opened, sizeof opened);
  SecureZero(out, sizeof out);
  return recovered;
}

// ---- Stage B/C: deliver recovered UTF-16 via SendInput to a test-owned EDIT ------------------------
// Mirrors the product inject_message() InputText path: per UTF-16 unit, a keydown+keyup INPUT with
// wVk=0, wScan=unit, dwFlags=KEYEVENTF_UNICODE (+KEYEVENTF_KEYUP). Nothing product-specific is linked;
// this is desktop-input mechanics on the current input desktop.

HWND g_edit = nullptr;
HWND g_main = nullptr;
HWND g_static = nullptr;  // a non-editable focus sink for the negative control

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

bool create_test_window() {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"PreloginA0ProbeWnd";
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  if (!RegisterClassExW(&wc)) return false;
  g_main = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"PreloginA0Probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 420, 200, nullptr, nullptr, wc.hInstance, nullptr);
  if (!g_main) return false;
  g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                           10, 10, 390, 28, g_main, reinterpret_cast<HMENU>(1), wc.hInstance, nullptr);
  g_static = CreateWindowExW(0, L"STATIC", L"(negative-control focus sink)", WS_CHILD | WS_VISIBLE,
                             10, 50, 390, 28, g_main, reinterpret_cast<HMENU>(2), wc.hInstance, nullptr);
  return g_edit && g_static;
}

// Force our window to the foreground and give `focusTarget` the keyboard focus, using the standard
// AttachThreadInput dance so the probe is deterministic rather than at the mercy of the foreground
// lock. Returns true only if our window actually became foreground.
bool force_foreground(HWND focusTarget) {
  HWND fg = GetForegroundWindow();
  DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
  DWORD me = GetCurrentThreadId();
  if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, TRUE);
  ShowWindow(g_main, SW_SHOW);
  BringWindowToTop(g_main);
  SetForegroundWindow(g_main);
  SetActiveWindow(g_main);
  SetFocus(focusTarget);
  if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, FALSE);
  // Pump so focus/activation settle.
  MSG msg;
  for (int i = 0; i < 50 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return GetForegroundWindow() == g_main;
}

void drain_messages(int budget = 200) {
  MSG msg;
  for (int i = 0; i < budget && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

void send_unicode_units(const std::vector<uint16_t>& units) {
  std::vector<INPUT> in;
  in.reserve(units.size() * 2);
  for (uint16_t u : units) {
    INPUT down = {}; down.type = INPUT_KEYBOARD; down.ki.wVk = 0; down.ki.wScan = u; down.ki.dwFlags = KEYEVENTF_UNICODE;
    INPUT up = down; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    in.push_back(down); in.push_back(up);
  }
  if (!in.empty()) SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
}

std::wstring read_edit_text() {
  wchar_t buf[512] = {};
  GetWindowTextW(g_edit, buf, 511);
  return std::wstring(buf);
}

void stage_input_delivery(const std::vector<uint16_t>& recovered) {
  if (!create_test_window()) { inconclusive("stageB/C: create test window", "no interactive window station"); return; }

  // Positive: edit focused -> the Unicode must arrive byte-for-byte.
  if (!force_foreground(g_edit)) {
    inconclusive("stageB: focus test EDIT", "could not acquire foreground (display-capture/session dependency)");
    DestroyWindow(g_main);
    return;
  }
  SetWindowTextW(g_edit, L"");
  drain_messages();
  send_unicode_units(recovered);
  // SendInput is async; give the queue time to flush into the focused control.
  for (int i = 0; i < 40; ++i) { drain_messages(); Sleep(10); }
  std::wstring got = read_edit_text();
  std::wstring want(recovered.begin(), recovered.end());
  ok(got == want, "stageB(positive): opened credential reached the focused EDIT via SendInput(UNICODE), byte-exact");
  if (got != want) std::printf("        (want %zu units, got %zu)\n", want.size(), got.size());

  // Negative control: move focus to a non-editable STATIC, clear, send the SAME input -> EDIT stays empty.
  // If this "negative" also filled the EDIT, the positive above would be meaningless.
  SetWindowTextW(g_edit, L"");
  force_foreground(g_static);
  drain_messages();
  send_unicode_units(recovered);
  for (int i = 0; i < 40; ++i) { drain_messages(); Sleep(10); }
  std::wstring neg = read_edit_text();
  ok(neg.empty(), "stageC(negative): with the EDIT defocused, the same SendInput does NOT fill it");
  if (!neg.empty()) std::printf("        (negative control leaked %zu units)\n", neg.size());

  DestroyWindow(g_main);
}

}  // namespace

int main() {
  std::printf("prelogin_a0_probe (A0 isolated: sealed crypto -> Unicode input delivery)\n");

  // A credential-shaped secret: ASCII + Latin-1 + Hangul + digit + symbol. Proves Unicode fidelity end
  // to end. This is a throwaway test string, never a real password.
  const std::vector<uint16_t> secret = {L'P', L'w', 0x00E4 /*ä*/, 0x00DF /*ß*/, 0xAC00 /*가*/, L'7', L'!'};

  std::printf("\n[Stage A] real sealed_unlock ECDH+AES-GCM exchange of a credential string\n");
  std::vector<uint16_t> recovered = sealed_roundtrip(secret);

  std::printf("\n[Stage B/C] deliver the opened credential as Unicode keystrokes to a test-owned EDIT\n");
  if (recovered.empty()) {
    ok(false, "stageB/C: skipped because stageA did not recover the credential");
  } else {
    stage_input_delivery(recovered);
  }

  std::printf("\nsummary: failures=%d inconclusive=%d\n", gFailures, gInconclusive);
  if (gFailures != 0) { std::printf("prelogin_a0_probe: FAIL\n"); return 1; }
  if (gInconclusive != 0) { std::printf("prelogin_a0_probe: PASS-with-SKIPS (env-gated stage not run)\n"); return 2; }
  std::printf("prelogin_a0_probe: PASS\n");
  return 0;
}
