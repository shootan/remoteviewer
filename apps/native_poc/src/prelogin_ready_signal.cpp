// Win32 implementation of the prelogin handoff readiness signal (prelogin_ready_signal.hpp).

#include "prelogin_ready_signal.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace remote60::native_poc::prelogin {

namespace {
// Manual-reset so a set stays observable until the service explicitly resets it for the next handoff.
constexpr wchar_t kEventBase[] = L"Global\\GNLinkUserHostReady";
constexpr wchar_t kNonceEnv[] = L"REMOTE60_PRELOGIN_READY_NONCE";

// Bind the object name to the per-launch nonce (G9): only the host the service started for THIS launch
// shares the nonce, so a foreign/stale host cannot satisfy the handoff. Empty nonce -> legacy base name.
std::wstring event_name(const std::wstring& nonce) {
  if (nonce.empty()) return kEventBase;
  return std::wstring(kEventBase) + L"_" + nonce;
}
}  // namespace

std::wstring ReadReadyNonceFromEnv() {
  wchar_t buf[128]{};
  const DWORD n = GetEnvironmentVariableW(kNonceEnv, buf, static_cast<DWORD>(std::size(buf)));
  if (n == 0 || n >= std::size(buf)) return std::wstring();  // unset, or implausibly long -> treat as none
  return std::wstring(buf, n);
}

bool SignalUserHostReady() {
  // Create + HOLD the handle for the process lifetime (a function-local static): the named kernel object
  // then lives exactly as long as this user Host is alive, so the service observing it gets "a ready
  // Host is still up", and the object vanishing when the Host exits turns the signal off for free. The
  // name carries the launch nonce the service injected, so a different install process cannot set it.
  static HANDLE held = nullptr;
  if (!held) {
    const std::wstring name = event_name(ReadReadyNonceFromEnv());
    held = CreateEventW(nullptr, /*manualReset=*/TRUE, /*initial=*/FALSE, name.c_str());
  }
  if (!held) return false;
  return SetEvent(held) != FALSE;
}

bool UserHostReadySignaled(const std::wstring& nonce) {
  const std::wstring name = event_name(nonce);
  HANDLE ev = OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
  if (!ev) return false;  // not created yet -> not ready
  const DWORD w = WaitForSingleObject(ev, 0);
  CloseHandle(ev);
  return w == WAIT_OBJECT_0;
}

void ResetUserHostReady(const std::wstring& nonce) {
  // If a previous Host still holds the object, clear its signal so it cannot be mistaken for the new
  // Host's readiness. If no one holds it (the previous Host already exited), there is nothing to reset.
  const std::wstring name = event_name(nonce);
  HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
  if (!ev) return;
  ResetEvent(ev);
  CloseHandle(ev);
}

}  // namespace remote60::native_poc::prelogin
