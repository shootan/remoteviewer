#pragma once

// The mouse-button vocabulary shared by every Win32 side of the input path. (mouse-xbutton r1)
//
// Three encodings of "which button" meet here, and until 2026-09-28 they were translated by hand
// in three places -- host_input_inject.cpp, secure_input_service_main.cpp and the viewer's window
// procedure -- each knowing only left / right / middle. The viewer never sent its X buttons (back
// and forward); had it done so, the host's default branch mapped any unknown key to the LEFT
// button, so a new viewer against an old host would have turned "back" into a left click. That
// default is gone: an unknown key maps to nothing here, and every caller treats nothing as
// "inject nothing".
//
//   wire bits   -- ControlInputEventMessage::buttons and the viewer's held mask (kMouseWire* in
//                  poc_protocol.hpp): bit0 left, bit1 right, bit2 middle, bit3 X1, bit4 X2. A host
//                  that predates kCaptureFlagMouseXButtonsV1 reads only the low three.
//   virtual key -- ControlInputEventMessage::keyCode of a button edge (kind 2 / 3): VK_LBUTTON 1,
//                  VK_RBUTTON 2, VK_MBUTTON 4, VK_XBUTTON1 5, VK_XBUTTON2 6.
//   Win32       -- the MK_* state bits in the LOWORD of a mouse message's wParam (MK_XBUTTON1 is
//                  0x20 and MK_XBUTTON2 0x40 -- NOT the wire's 0x8 / 0x10), the XBUTTON1 / 2
//                  identifier in the HIWORD of a WM_XBUTTON* wParam, and MOUSEEVENTF_* + mouseData
//                  for SendInput.
//
// Header-only and Win32-constant only, so the product paths and the pure test read one definition.
// Not for the shared client core (it compiles on Android): the wire constants it needs live in
// poc_protocol.hpp.

#include <windows.h>

#include <cstdint>

#include "poc_protocol.hpp"

namespace remote60::native_poc {

inline bool mouse_vk_is_xbutton(uint32_t vk) { return vk == VK_XBUTTON1 || vk == VK_XBUTTON2; }

/** The wire bit of a button's virtual key; 0 for anything that is not a mouse button. */
inline uint16_t mouse_vk_to_wire(uint32_t vk) {
  switch (vk) {
    case VK_LBUTTON: return kMouseWireLeft;
    case VK_RBUTTON: return kMouseWireRight;
    case VK_MBUTTON: return kMouseWireMiddle;
    case VK_XBUTTON1: return kMouseWireX1;
    case VK_XBUTTON2: return kMouseWireX2;
    default: return 0;
  }
}

/** The MK_* state word for a set of wire bits (the LOWORD of a mouse message's wParam). */
inline WORD mouse_wire_to_mk(uint16_t buttons) {
  WORD mk = 0;
  if ((buttons & kMouseWireLeft) != 0) mk |= MK_LBUTTON;
  if ((buttons & kMouseWireRight) != 0) mk |= MK_RBUTTON;
  if ((buttons & kMouseWireMiddle) != 0) mk |= MK_MBUTTON;
  if ((buttons & kMouseWireX1) != 0) mk |= MK_XBUTTON1;
  if ((buttons & kMouseWireX2) != 0) mk |= MK_XBUTTON2;
  return mk;
}

/** XBUTTON1 / XBUTTON2 for the X keys (the HIWORD of a WM_XBUTTON* wParam); 0 for L / R / M. */
inline WORD mouse_vk_to_xbutton(uint32_t vk) {
  if (vk == VK_XBUTTON1) return XBUTTON1;
  if (vk == VK_XBUTTON2) return XBUTTON2;
  return 0;
}

/** The virtual key named by a WM_XBUTTON* message's XBUTTON identifier; 0 for anything else. */
inline uint32_t mouse_xbutton_to_vk(WORD xbutton) {
  if (xbutton == XBUTTON1) return VK_XBUTTON1;
  if (xbutton == XBUTTON2) return VK_XBUTTON2;
  return 0;
}

/**
 * The window message for a button edge: kind 2 (down) or 3 (up) with a button's virtual key.
 * 0 for any other kind or key -- the caller then posts nothing.
 */
inline UINT mouse_vk_to_message(uint16_t kind, uint32_t vk) {
  const bool down = kind == 2;
  if (!down && kind != 3) return 0;
  switch (vk) {
    case VK_LBUTTON: return down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
    case VK_RBUTTON: return down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
    case VK_MBUTTON: return down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
    case VK_XBUTTON1:
    case VK_XBUTTON2: return down ? WM_XBUTTONDOWN : WM_XBUTTONUP;
    default: return 0;
  }
}

/**
 * The wParam of a posted button message: the MK_* state of every button held AFTER this edge in
 * the LOWORD and, for an X button, which one in the HIWORD. `heldBefore` is the wire mask the
 * viewer reported with the event -- its state before the edge -- so the edge's own bit is added
 * for a down and removed for an up, and a message never claims a button is held by the very up
 * that releases it. Bits outside the wire mask are dropped.
 */
inline WPARAM mouse_button_message_wparam(uint16_t kind, uint32_t vk, uint16_t heldBefore) {
  const uint16_t bit = mouse_vk_to_wire(vk);
  uint16_t held = static_cast<uint16_t>(heldBefore & kMouseWireMask);
  if (kind == 2) {
    held = static_cast<uint16_t>(held | bit);
  } else if (kind == 3) {
    held = static_cast<uint16_t>(held & static_cast<uint16_t>(~bit));
  }
  return MAKEWPARAM(mouse_wire_to_mk(held), mouse_vk_to_xbutton(vk));
}

struct MouseSendInput {
  DWORD flags = 0;      // MOUSEEVENTF_*; 0 = not a button edge, inject nothing
  DWORD mouseData = 0;  // XBUTTON1 / XBUTTON2 with the X flags, else 0
};

/** SendInput's dwFlags and mouseData for a button edge; flags 0 for an unknown kind or key. */
inline MouseSendInput mouse_vk_to_sendinput(uint16_t kind, uint32_t vk) {
  MouseSendInput out;
  const bool down = kind == 2;
  if (!down && kind != 3) return out;
  switch (vk) {
    case VK_LBUTTON: out.flags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case VK_RBUTTON: out.flags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case VK_MBUTTON: out.flags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case VK_XBUTTON1:
    case VK_XBUTTON2:
      out.flags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
      out.mouseData = mouse_vk_to_xbutton(vk);
      break;
    default: break;
  }
  return out;
}

}  // namespace remote60::native_poc
