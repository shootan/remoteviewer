#pragma once

// The one rule for "may a credential be typed here, now" -- the logon UI's password field -- and the one
// typing (lockscreen-cred r1, Codex A4; the rule itself is prelogin-ui r7 H5's, moved here unchanged so the
// pre-login worker and the lock-screen agent cannot disagree).
//
// The target: the input desktop is Winlogon; its foreground window belongs to %SystemRoot%\System32\
// LogonUI.exe (that full path, not just a process called so) running as LocalSystem in THIS process's
// session; and the element holding the keyboard focus is a password field (UI Automation IsPassword) of that
// same process -- the user-name box of the same window is not a target. Its identity (window + process) is
// what is compared before and after the typing.
//
// What is NOT here: the pre-login worker's "another desktop seen = this worker has left" latch
// (prelogin_logon_desktop.hpp). A lock-screen agent that finds Default instead simply has no target now.
//
// The typing: the whole credential as Unicode key down/up pairs in ONE SendInput (the system does not
// interleave other input into one call), the INPUT buffer zeroed after; a partial send is a failure.
//
// TypeIntoTarget is the commit sequence both callers use, with the OS questions as parameters so it runs
// against stand-ins: wait (bounded) for a target; then, in one step, take the target again -- the same one ->
// type once -> the same one still. A target that changed before is not typed into (TargetNotReady); one that
// changed during, or a typing that failed, is InjectFailed. Never retried.

#include <cstdint>
#include <functional>
#include <string>

#include "poc_protocol.hpp"

namespace remote60::native_poc::logon_ui {

// The name of the desktop receiving input now ("" when it cannot be read).
std::wstring InputDesktopName();
// The identity of the logon UI password field holding the focus in this process's session, or 0.
uint64_t PasswordFieldIdentity();
// The identity of `window` (an HWND) of process `pid` when the keyboard focus is a password field of that
// process (UI Automation IsPassword), else 0. PasswordFieldIdentity is this for the system's LogonUI.
uint64_t FocusedPasswordFieldIdentity(void* window, uint32_t pid);
// The product's typing (one SendInput); false with *err when the OS refused or sent only part.
bool TypeUnicode(const uint16_t* units, uint16_t count, uint32_t* err);
// lockscreen-cred r1 (Codex Q1 b): the typing that REPLACES what the field holds, for the lock-screen agent --
// a retry must not be appended to an earlier attempt. The focused password field of process `pid` (the one
// just verified) is emptied through its writable UI Automation ValuePattern when it has one; otherwise
// Ctrl+A goes first in the SAME SendInput as the units (select-all, then the typing replaces the selection).
// Never a second attempt by another way; false with *err if either step failed.
bool ReplaceWithUnicode(uint32_t pid, const uint16_t* units, uint16_t count, uint32_t* err);

using TargetFn = std::function<uint64_t()>;
using InjectFn = std::function<bool(const uint16_t* units, uint16_t count, uint32_t* err)>;
using SleepFn = std::function<void(uint32_t ms)>;
using StillValidFn = std::function<bool()>;  // the job is still wanted (not cancelled / stale)

// The commit sequence (see the top). `target` returns 0 while there is none. Returns Injected,
// TargetNotReady (nothing typed) or InjectFailed (*err set).
inline UnlockStage TypeIntoTarget(const uint16_t* units, uint16_t count, const TargetFn& target, const InjectFn& inject,
                                  const SleepFn& sleep, uint32_t waitMs, uint32_t* err,
                                  const StillValidFn& stillValid = nullptr) {
  *err = 0;
  uint64_t seen = 0;
  for (uint32_t waited = 0;; waited += 100) {
    if (stillValid && !stillValid()) return UnlockStage::TargetNotReady;
    seen = target ? target() : 0;
    if (seen != 0 || waited >= waitMs) break;
    if (sleep) sleep(100);
  }
  if (seen == 0) return UnlockStage::TargetNotReady;
  if (stillValid && !stillValid()) return UnlockStage::TargetNotReady;
  if ((target ? target() : 0) != seen) return UnlockStage::TargetNotReady;  // changed before: nothing typed
  if (!inject || !inject(units, count, err)) {
    if (*err == 0) *err = 31;  // ERROR_GEN_FAILURE
    return UnlockStage::InjectFailed;
  }
  if ((target ? target() : 0) != seen) {
    *err = 1400;  // ERROR_INVALID_WINDOW_HANDLE: the target changed while it was typed
    return UnlockStage::InjectFailed;
  }
  return UnlockStage::Injected;
}

// The lock-screen agent's target: on the Winlogon desktop, the password field above; otherwise none.
inline uint64_t LockScreenTarget(const std::function<std::wstring()>& desktopName, const TargetFn& passwordField) {
  const std::wstring now = desktopName ? desktopName() : std::wstring();
  if (now.empty()) return 0;
  // Case-insensitive compare of the desktop name against "Winlogon".
  static const wchar_t kWinlogon[] = L"winlogon";
  if (now.size() != 8) return 0;
  for (size_t i = 0; i < 8; ++i) {
    wchar_t c = now[i];
    if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    if (c != kWinlogon[i]) return 0;
  }
  return passwordField ? passwordField() : 0;
}

}  // namespace remote60::native_poc::logon_ui
