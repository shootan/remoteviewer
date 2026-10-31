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

// lockscreen-cred r1 (Codex A5): the lock screen of a logged-on host (kCaptureFlagLockCredV1) is a logon
// screen for this purpose too -- characters are held back there and pointed at "Windows 로그인". (The host and
// the service refuse them as well; this is the viewer not sending them, and saying why.)
inline bool HostAtLogonScreen(uint32_t hostCaptureFlags) {
  return (hostCaptureFlags & (kCaptureFlagPreloginV1 | kCaptureFlagLockCredV1)) != 0;
}

// r7 H6: before the host's first pong a viewer does not know which it is talking to -- and an unknown
// role is treated as the logon screen (characters held back), not as a logged-in desktop. What the gates
// below are given is this.
inline uint32_t EffectiveHostFlags(bool roleKnown, uint32_t hostCaptureFlags) {
  return roleKnown ? hostCaptureFlags : (hostCaptureFlags | kCaptureFlagPreloginV1);
}

// May this ControlInputEvent be sent to a host with these capability bits?
inline bool ViewerMaySendInputEvent(uint32_t hostCaptureFlags, uint16_t kind, uint32_t keyCode) {
  if (!HostAtLogonScreen(hostCaptureFlags)) return true;
  if (kind != kInputKindKeyDown && kind != kInputKindKeyUp) return true;  // pointer / wheel
  // lockscreen-cred r1 (Codex A6): a key UP goes -- it releases a key whose down reached the host before the
  // screen changed (the viewer sends an up only for a down it sent); holding it back would leave it stuck.
  if (kind == kInputKindKeyUp) return true;
  return keyCode <= 0xFFFF && IsPreloginNavKeyAllowed(static_cast<uint16_t>(keyCode));
}

// May text (ControlInputText, a physical scan key for the host IME) be sent at all?
inline bool ViewerMaySendText(uint32_t hostCaptureFlags) { return !HostAtLogonScreen(hostCaptureFlags); }

// A host-IME physical key (by its virtual key).
inline bool ViewerMaySendPhysicalKey(uint32_t hostCaptureFlags, uint16_t vk) {
  return !HostAtLogonScreen(hostCaptureFlags) || IsPreloginNavKeyAllowed(vk);
}

// r7 H6: the same rules on an outbound control action, right before it is sent (a ControlOutboundAction
// of the shared client core; a template so this header stays free of it). Anything not input passes.
template <typename Action>
inline bool ViewerMaySendAction(uint32_t hostCaptureFlags, const Action& a) {
  using Kind = decltype(a.kind);
  if (a.kind == Kind::InputEvent) return ViewerMaySendInputEvent(hostCaptureFlags, a.inputEvent.kind, a.inputEvent.keyCode);
  if (a.kind == Kind::InputText) return ViewerMaySendText(hostCaptureFlags);
  if (a.kind == Kind::PhysicalKey) return ViewerMaySendPhysicalKey(hostCaptureFlags, a.physicalKey.vk);
  return true;
}

}  // namespace remote60::native_poc::prelogin
