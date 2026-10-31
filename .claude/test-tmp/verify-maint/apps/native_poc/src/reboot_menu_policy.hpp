#pragma once

// Viewer-side policy for the remote-reboot menu item and the prelogin credential-entry affordance
// (prelogin-service r3). Pure decision logic over the host's advertised Pong capability flags, so the
// PC and Android viewers share exactly one rule and it is unit-testable without any UI.
//
// Two subtle contract points are pinned here:
//   - R1: the reboot confirm popup's "...you cannot connect until someone logs in" warning depends on
//     whether prelogin is DURABLE-ENABLED (reachable after a reboot, kCaptureFlagPreloginEnabledV1), NOT
//     on whether the host happens to be in prelogin right now (kCaptureFlagPreloginV1). A logged-in,
//     feature-on host must NOT be warned.
//   - PC-first: in v1 only the PC viewer can send the sealed "Windows 로그인" credential; the Android
//     viewer shows an "available after an app update" note and never a plaintext fallback.

#include <cstdint>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

enum class ViewerPlatform { Pc, Android };

struct RebootMenuPolicy {
  bool menuEnabled = false;         // the in-menu reboot item is actionable
  bool disabledBecauseOldHost = false;  // disabled specifically because the host lacks the capability
  bool showPreloginWarning = false; // append the "no connect until login after restart" warning
};

inline RebootMenuPolicy DecideRebootMenu(uint32_t hostCaptureFlags) {
  const bool canReboot = (hostCaptureFlags & kCaptureFlagRemoteRebootV1) != 0;
  const bool durableEnabled = (hostCaptureFlags & kCaptureFlagPreloginEnabledV1) != 0;
  RebootMenuPolicy p;
  p.menuEnabled = canReboot;
  p.disabledBecauseOldHost = !canReboot;
  // R1: reachability AFTER the restart depends on durable-enabled, not on the momentary prelogin state.
  p.showPreloginWarning = canReboot && !durableEnabled;
  return p;
}

// lockscreen-cred r1: which logon screen the host is at, if any -- the one decision the toolbar button, the
// menu and Ctrl+Alt+U all take ("Windows 로그인" is the same entry for both screens; what differs is the
// purpose the password is sealed under and who types it).
enum class LogonScreenRole { None, Prelogin, LockScreen };

struct PreloginCredPolicy {
  bool sealedEntryAvailable = false;  // the viewer may send the sealed credential to the LogonUI
  bool showAndroidUpdateNote = false; // Android v1: guidance only, never a plaintext fallback
  LogonScreenRole role = LogonScreenRole::None;
};

inline LogonScreenRole DecideLogonScreenRole(uint32_t hostCaptureFlags) {
  if ((hostCaptureFlags & kCaptureFlagPreloginV1) != 0) return LogonScreenRole::Prelogin;  // the worker's
  if ((hostCaptureFlags & kCaptureFlagLockCredV1) != 0) return LogonScreenRole::LockScreen;  // a logged-on host's
  return LogonScreenRole::None;
}

inline PreloginCredPolicy DecidePreloginCred(ViewerPlatform platform, uint32_t hostCaptureFlags) {
  PreloginCredPolicy p;
  p.role = DecideLogonScreenRole(hostCaptureFlags);
  const bool atLogonScreen = p.role != LogonScreenRole::None;
  p.sealedEntryAvailable = atLogonScreen && platform == ViewerPlatform::Pc;
  p.showAndroidUpdateNote = atLogonScreen && platform == ViewerPlatform::Android;
  return p;
}

}  // namespace remote60::native_poc
