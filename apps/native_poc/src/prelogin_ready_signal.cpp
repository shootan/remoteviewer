// Win32 implementation of the prelogin handoff readiness signal (prelogin_ready_signal.hpp).

#include "prelogin_ready_signal.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace remote60::native_poc::prelogin {

namespace {
// Manual-reset so a set stays observable until the service explicitly resets it for the next handoff.
constexpr wchar_t kEventName[] = L"Global\\GNLinkUserHostReady";
}  // namespace

bool SignalUserHostReady() {
  // Create + HOLD the handle for the process lifetime (a function-local static): the named kernel object
  // then lives exactly as long as this user Host is alive, so the service observing it gets "a ready
  // Host is still up", and the object vanishing when the Host exits turns the signal off for free.
  static HANDLE held = nullptr;
  if (!held) held = CreateEventW(nullptr, /*manualReset=*/TRUE, /*initial=*/FALSE, kEventName);
  if (!held) return false;
  return SetEvent(held) != FALSE;
}

bool UserHostReadySignaled() {
  HANDLE ev = OpenEventW(SYNCHRONIZE, FALSE, kEventName);
  if (!ev) return false;  // not created yet -> not ready
  const DWORD w = WaitForSingleObject(ev, 0);
  CloseHandle(ev);
  return w == WAIT_OBJECT_0;
}

void ResetUserHostReady() {
  // If a previous Host still holds the object, clear its signal so it cannot be mistaken for the new
  // Host's readiness. If no one holds it (the previous Host already exited), there is nothing to reset.
  HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, kEventName);
  if (!ev) return;
  ResetEvent(ev);
  CloseHandle(ev);
}

}  // namespace remote60::native_poc::prelogin
