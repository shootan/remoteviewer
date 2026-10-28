#pragma once

// The mouse X buttons (back / forward) in the shared client session the Android app drives (keepalive-r2 e,
// codex_ka_pc_src_r7.md "APK 영향"). The same contract as the PC viewer's fence_mouse_for_host
// (viewer_input_forward.cpp): an X edge -- ControlInputEvent kind 2 / 3 with keyCode VK_XBUTTON1 5 / VK_XBUTTON2 6
// -- and the X bits of `buttons` (kMouseWireX1 / X2) go only to a host whose Pong carries
// kCaptureFlagMouseXButtonsV1. A host that predates it would read keyCode 5 / 6 as something else (an old
// default mapped unknown keys to the LEFT button), so the edge is not sent at all and the X bits are masked.
//
// Before the first Pong the host's role is unknown: unknown is "no X", as on the PC (hostMouseXButtons stays
// false until the Pong). A held X button needs no exception: ClientHeldInput records a down only once it was
// sent, i.e. once this fence let it through to a host that takes X -- its release passes the same fence.
//
// Header-only and free of Win32 (native_video_client_session.cpp compiles on Android).

#include <cstdint>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

inline bool HostTakesMouseXButtons(bool roleKnown, uint32_t hostCaptureFlags) {
  return roleKnown && (hostCaptureFlags & kCaptureFlagMouseXButtonsV1) != 0;
}

inline bool IsMouseXButtonEdge(uint16_t kind, uint32_t keyCode) {
  return (kind == 2 || kind == 3) && (keyCode == 5 || keyCode == 6);
}

// May this input event go to the host? A host without X: the X bits of *buttons are cleared (every kind) and
// an X edge is refused. A host with X: untouched.
inline bool FenceMouseXButtonsForHost(bool hostTakesX, uint16_t kind, uint32_t keyCode, uint16_t* buttons) {
  if (hostTakesX) return true;
  *buttons = static_cast<uint16_t>(*buttons & kMouseWireLegacyMask);
  return !IsMouseXButtonEdge(kind, keyCode);
}

// The same on an outbound control action right before it is sent (a ControlOutboundAction of the shared client
// core; a template so this header stays free of it). Anything but an InputEvent passes.
template <typename Action>
inline bool FenceMouseXButtonsAction(bool hostTakesX, Action* a) {
  using Kind = decltype(a->kind);
  if (a->kind != Kind::InputEvent) return true;
  return FenceMouseXButtonsForHost(hostTakesX, a->inputEvent.kind, a->inputEvent.keyCode, &a->inputEvent.buttons);
}

}  // namespace remote60::native_poc
