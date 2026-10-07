#pragma once

// What a VIEWER may send to a host at the pre-login screen (prelogin-ui r5, G8).
//
// The contract (prelogin-service plans, Codex r3 section 2): before login, plaintext is for navigating
// the logon screen only; a password's characters travel only the sealed "Windows 로그인" path. The host
// already drops a non-navigation key it is sent (prelogin_input_filter.hpp) -- but by then the key has
// crossed the wire. So the viewer does not send it at all: while the host says it is at the logon
// screen (Pong kCaptureFlagPreloginV1), a key event goes only for a navigation key (the host's own
// list), and text (typed, IME-composed, pasted) does not go. Mouse and wheel are unaffected.
//
// The PC viewer (viewer_input_forward.cpp) and the shared session the Android app drives
// (native_video_client_session.cpp) both ask here, so the two cannot disagree.

#include <cstdint>

#include "poc_protocol.hpp"
#include "prelogin_input_filter.hpp"

namespace remote60::native_poc::prelogin {

constexpr uint16_t kInputKindKeyDown = 5;  // ControlInputEvent kinds 5/6 (as the host reads them)
constexpr uint16_t kInputKindKeyUp = 6;

inline bool HostAtLogonScreen(uint32_t hostCaptureFlags) {
  return (hostCaptureFlags & kCaptureFlagPreloginV1) != 0;
}

// May this ControlInputEvent be sent to a host with these capability bits?
inline bool ViewerMaySendInputEvent(uint32_t hostCaptureFlags, uint16_t kind, uint32_t keyCode) {
  if (!HostAtLogonScreen(hostCaptureFlags)) return true;
  if (kind != kInputKindKeyDown && kind != kInputKindKeyUp) return true;  // pointer / wheel
  return keyCode <= 0xFFFF && IsPreloginNavKeyAllowed(static_cast<uint16_t>(keyCode));
}

// May text (ControlInputText, a physical scan key for the host IME) be sent at all?
inline bool ViewerMaySendText(uint32_t hostCaptureFlags) { return !HostAtLogonScreen(hostCaptureFlags); }

// A host-IME physical key (by its virtual key).
inline bool ViewerMaySendPhysicalKey(uint32_t hostCaptureFlags, uint16_t vk) {
  return !HostAtLogonScreen(hostCaptureFlags) || IsPreloginNavKeyAllowed(vk);
}

}  // namespace remote60::native_poc::prelogin
