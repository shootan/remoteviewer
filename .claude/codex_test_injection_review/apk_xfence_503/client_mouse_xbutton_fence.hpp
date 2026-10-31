#pragma once

// The mouse X buttons (back / forward) in the shared client session the Android app drives (keepalive-r2 e,
// codex_ka_pc_src_r7.md "APK 영향"). The same contract as the PC viewer's fence_mouse_for_host
// (viewer_input_forward.cpp): an X edge -- ControlInputEvent kind 2 / 3 with keyCode VK_XBUTTON1 5 / VK_XBUTTON2 6
// -- and the X bits of `buttons` (kMouseWireX1 / X2) go only to a host whose Pong carries
// kCaptureFlagMouseXButtonsV1. A host that predates it would read keyCode 5 / 6 as something else (an old
// default mapped unknown keys to the LEFT button), so the edge is not sent at all and the X bits are masked.
//
// Before the first Pong the host's role is unknown: unknown is "no X", as on the PC (hostMouseXButtons stays
// false until the Pong).
//
// r2 (codex_apk_xfence_src_r1.md X1 / X2): the capability alone is not enough when it CHANGES under one press -- a
// down refused while the role was unknown, then the X Pong, would let that press's up (and the X bit of its moves)
// through with no down before them. ClientMouseXPressLedger below remembers, per X button, what became of the
// press: refused at the entry, dropped right before sending, or sent (a release owed). A refused press is refused
// whole -- its up and its held bit too -- until its up; the next press is judged afresh. A press that was sent keeps
// its owed release (ClientHeldInput makes it), which goes only while the host still takes X: no exception to a host
// without X (a park then cannot finish and ends by its own bound). One ledger per connection (reset with it).
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

// What became of each X button's press, at the two fences of one connection. The entry side (QueueInputEvent) and
// the send side (the control loop, right before an action leaves) keep apart records: a press refused at the entry
// never reaches the queue, and a press dropped at sending is seen only there. Not thread-safe: its owner's lock.
class ClientMouseXPressLedger {
 public:
  // The entry: may this event be queued? *buttons is masked (no X bit for a host without X, nor for a refused press).
  bool AdmitQueued(bool hostTakesX, uint16_t kind, uint32_t keyCode, uint16_t* buttons) {
    bool ok = true;
    const int s = Slot(keyCode);
    if (IsMouseXButtonEdge(kind, keyCode)) {
      if (kind == 2) {
        entryRefused_[s] = !hostTakesX;  // a down: a new press, judged now
        ok = hostTakesX;
      } else if (entryRefused_[s]) {
        entryRefused_[s] = false;  // the up of a refused press: refused too, and the press is over
        ok = false;
      } else {
        ok = hostTakesX;
      }
    }
    *buttons = Mask(*buttons, hostTakesX, entryRefused_);
    return ok;
  }

  // The send side, right before an InputEvent leaves.
  bool AdmitSend(bool hostTakesX, uint16_t kind, uint32_t keyCode, uint16_t* buttons) {
    bool ok = true;
    const int s = Slot(keyCode);
    if (IsMouseXButtonEdge(kind, keyCode)) {
      if (kind == 2) {
        if (hostTakesX) {
          if (send_[s] == SendState::Dropped) send_[s] = SendState::Idle;  // a new press
        } else {
          send_[s] = SendState::Dropped;
          ok = false;
        }
      } else if (send_[s] == SendState::Dropped) {
        send_[s] = SendState::Idle;  // the up of a press whose down never left
        ok = false;
      } else {
        ok = hostTakesX;  // an owed release (Sent) too: never an exception to a host without X
      }
    }
    bool dropped[2] = {send_[0] == SendState::Dropped, send_[1] == SendState::Dropped};
    *buttons = Mask(*buttons, hostTakesX, dropped);
    return ok;
  }
  template <typename Action>
  bool AdmitSendAction(bool hostTakesX, Action* a) {
    using Kind = decltype(a->kind);
    if (a->kind != Kind::InputEvent) return true;
    return AdmitSend(hostTakesX, a->inputEvent.kind, a->inputEvent.keyCode, &a->inputEvent.buttons);
  }

  // After an action was sent: a sent X down owes its release; a sent X up ends it.
  template <typename Action>
  void OnSentAction(const Action& a) {
    using Kind = decltype(a.kind);
    if (a.kind != Kind::InputEvent || !IsMouseXButtonEdge(a.inputEvent.kind, a.inputEvent.keyCode)) return;
    send_[Slot(a.inputEvent.keyCode)] = a.inputEvent.kind == 2 ? SendState::Sent : SendState::Idle;
  }

  void Reset() {
    entryRefused_[0] = entryRefused_[1] = false;
    send_[0] = send_[1] = SendState::Idle;
  }

  bool EntryRefused(uint32_t keyCode) const { return Slot(keyCode) >= 0 && entryRefused_[Slot(keyCode)]; }
  bool SendDropped(uint32_t keyCode) const { return Slot(keyCode) >= 0 && send_[Slot(keyCode)] == SendState::Dropped; }
  bool SendOwed(uint32_t keyCode) const { return Slot(keyCode) >= 0 && send_[Slot(keyCode)] == SendState::Sent; }

 private:
  enum class SendState : uint8_t { Idle, Dropped, Sent };
  static int Slot(uint32_t keyCode) { return keyCode == 5 ? 0 : keyCode == 6 ? 1 : -1; }
  static uint16_t Mask(uint16_t buttons, bool hostTakesX, const bool refused[2]) {
    if (!hostTakesX) return static_cast<uint16_t>(buttons & kMouseWireLegacyMask);
    if (refused[0]) buttons = static_cast<uint16_t>(buttons & ~kMouseWireX1);
    if (refused[1]) buttons = static_cast<uint16_t>(buttons & ~kMouseWireX2);
    return buttons;
  }
  bool entryRefused_[2] = {false, false};
  SendState send_[2] = {SendState::Idle, SendState::Idle};
};

}  // namespace remote60::native_poc
