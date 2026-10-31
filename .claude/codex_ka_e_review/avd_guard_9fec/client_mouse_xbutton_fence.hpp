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

#include <algorithm>
#include <cstdint>
#include <vector>

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
//
// r3 (codex_apk_xfence_src_r2.md N1): the release OWED for a down the host already received is its own record
// (owed_), apart from what became of any later press of the same button. A later down refused at the entry or dropped
// at sending never hides it: the next up goes to release the host's button as soon as the host takes X, and the debt
// ends only when that up was sent and answered (OnSentAction after the ACK). To a host without X it never goes.
//
// r4 (codex_apk_xfence_src_r3.md N1): a down admitted and QUEUED (not sent yet) whose up is then refused at the entry
// (no X by then) would leave the queue alone -- and if X came back before the sender took it, that down would go
// with no up after it. The down's queue sequence is noted (NoteQueued); an up refused at the entry cancels the
// still-unsent downs of its button by those sequences, and the sender drops a cancelled down (its held bit hidden
// until the next press). A down already taken by the sender is not cancelled: it is owed once answered, as before.
// A normal quick pair (down and up both queued) is untouched: its admitted up completes the pair.
class ClientMouseXPressLedger {
 public:
  // The entry: may this event be queued? *buttons is masked (no X bit for a host without X, nor for a refused press
  // while nothing of that button is held).
  bool AdmitQueued(bool hostTakesX, uint16_t kind, uint32_t keyCode, uint16_t* buttons) {
    bool ok = true;
    const int s = Slot(keyCode);
    if (IsMouseXButtonEdge(kind, keyCode)) {
      if (kind == 2) {
        if (hostTakesX) {
          entryHeld_[s] = true;  // a new press, admitted
          entryRefused_[s] = false;
        } else {
          entryRefused_[s] = true;  // refused; an admitted / sent press of the button is left as it is
          ok = false;
        }
      } else {
        // an up: it releases whatever of the button the host holds (admitted here, or sent: owed) -- only to a
        // host that takes X; otherwise it is the up of a refused press (refused) or a stray one (by the capability)
        if (entryHeld_[s] || owed_[s]) {
          ok = hostTakesX;
        } else if (entryRefused_[s]) {
          ok = false;
        } else {
          ok = hostTakesX;
        }
        if (!ok) {  // r4: the downs of the button still waiting in the queue go with this refused up
          cancelled_[s].insert(cancelled_[s].end(), pending_[s].begin(), pending_[s].end());
          pending_[s].clear();
        }
        entryHeld_[s] = false;  // the button is up: every press of it is over at the entry
        entryRefused_[s] = false;
      }
    }
    bool hide[2] = {EntryHides(0), EntryHides(1)};
    *buttons = Mask(*buttons, hostTakesX, hide);
    return ok;
  }

  // r4: an event admitted at the entry has been queued with this sequence (the controller calls it after Enqueue).
  void NoteQueued(uint16_t kind, uint32_t keyCode, uint32_t seq) {
    if (!IsMouseXButtonEdge(kind, keyCode)) return;
    const int s = Slot(keyCode);
    if (kind == 2) {
      pending_[s].push_back(seq);
    } else {
      pending_[s].clear();  // its up is queued behind it: the pair is whole
    }
  }

  // The send side, right before an InputEvent leaves. `seq`: the event's queue sequence.
  bool AdmitSend(bool hostTakesX, uint16_t kind, uint32_t keyCode, uint16_t* buttons, uint32_t seq = 0) {
    bool ok = true;
    const int s = Slot(keyCode);
    if (IsMouseXButtonEdge(kind, keyCode)) {
      // the queue is FIFO: a noted down older than this event that the sender never saw is gone (dropped by a park,
      // expired) -- forgotten, so the records stay bounded
      PruneBefore(&pending_[s], seq);
      PruneBefore(&cancelled_[s], seq);
      if (kind == 2 && Take(&cancelled_[s], seq)) {
        dropped_[s] = true;  // r4: its up was refused at the entry -- the down does not go either
        ok = false;
      } else if (kind == 2) {
        Take(&pending_[s], seq);  // taken by the sender: no longer cancellable (owed once answered)
        if (hostTakesX) {
          dropped_[s] = false;  // a new press
        } else {
          dropped_[s] = true;  // dropped; an owed release of the button stays owed
          ok = false;
        }
      } else if (owed_[s]) {
        ok = hostTakesX;  // the host holds it: released once it takes X -- never an exception to a host without X
        if (!ok) dropped_[s] = false;  // that up is gone; the debt stays (park / teardown)
      } else if (dropped_[s]) {
        dropped_[s] = false;  // the up of a press whose down never left
        ok = false;
      } else {
        ok = hostTakesX;
      }
    }
    bool hide[2] = {SendHides(0), SendHides(1)};
    *buttons = Mask(*buttons, hostTakesX, hide);
    return ok;
  }
  template <typename Action>
  bool AdmitSendAction(bool hostTakesX, Action* a) {
    using Kind = decltype(a->kind);
    if (a->kind != Kind::InputEvent) return true;
    return AdmitSend(hostTakesX, a->inputEvent.kind, a->inputEvent.keyCode, &a->inputEvent.buttons, a->inputEvent.seq);
  }

  // After an action was sent AND answered: a sent X down owes its release; a sent X up pays it (and ends the button's
  // presses on the send side).
  template <typename Action>
  void OnSentAction(const Action& a) {
    using Kind = decltype(a.kind);
    if (a.kind != Kind::InputEvent || !IsMouseXButtonEdge(a.inputEvent.kind, a.inputEvent.keyCode)) return;
    const int s = Slot(a.inputEvent.keyCode);
    if (a.inputEvent.kind == 2) {
      owed_[s] = true;
    } else {
      owed_[s] = false;
      dropped_[s] = false;
    }
  }

  void Reset() {
    for (int s = 0; s < 2; ++s) {
      entryHeld_[s] = entryRefused_[s] = dropped_[s] = owed_[s] = false;
      pending_[s].clear();
      cancelled_[s].clear();
    }
  }

  bool EntryRefused(uint32_t keyCode) const { return Slot(keyCode) >= 0 && entryRefused_[Slot(keyCode)]; }
  bool SendDropped(uint32_t keyCode) const { return Slot(keyCode) >= 0 && dropped_[Slot(keyCode)]; }
  bool SendOwed(uint32_t keyCode) const { return Slot(keyCode) >= 0 && owed_[Slot(keyCode)]; }
  size_t PendingDowns(uint32_t keyCode) const { return Slot(keyCode) >= 0 ? pending_[Slot(keyCode)].size() : 0; }
  size_t CancelledDowns(uint32_t keyCode) const { return Slot(keyCode) >= 0 ? cancelled_[Slot(keyCode)].size() : 0; }

 private:
  static int Slot(uint32_t keyCode) { return keyCode == 5 ? 0 : keyCode == 6 ? 1 : -1; }
  static bool Take(std::vector<uint32_t>* v, uint32_t seq) {
    const auto it = std::find(v->begin(), v->end(), seq);
    if (it == v->end()) return false;
    v->erase(it);
    return true;
  }
  static void PruneBefore(std::vector<uint32_t>* v, uint32_t seq) {
    v->erase(std::remove_if(v->begin(), v->end(), [seq](uint32_t x) { return x < seq; }), v->end());
  }
  // a refused / dropped press's held bit is hidden only while nothing of that button is held on the host
  bool EntryHides(int s) const { return entryRefused_[s] && !entryHeld_[s] && !owed_[s]; }
  bool SendHides(int s) const { return dropped_[s] && !owed_[s]; }
  static uint16_t Mask(uint16_t buttons, bool hostTakesX, const bool hide[2]) {
    if (!hostTakesX) return static_cast<uint16_t>(buttons & kMouseWireLegacyMask);
    if (hide[0]) buttons = static_cast<uint16_t>(buttons & ~kMouseWireX1);
    if (hide[1]) buttons = static_cast<uint16_t>(buttons & ~kMouseWireX2);
    return buttons;
  }
  bool entryHeld_[2] = {false, false};     // a down admitted at the entry, its up not admitted yet
  bool entryRefused_[2] = {false, false};  // a down refused at the entry, its up not seen yet
  bool dropped_[2] = {false, false};       // a down dropped at sending, its up not seen yet
  bool owed_[2] = {false, false};          // a down the host received (sent + answered), its up not yet
  std::vector<uint32_t> pending_[2];       // r4: queued downs (their sequences) not taken by the sender yet
  std::vector<uint32_t> cancelled_[2];     // r4: queued downs whose up was refused at the entry: not to be sent
};

}  // namespace remote60::native_poc
