#pragma once

// What this host has pressed on the user's behalf and not yet let go of. (RV-01, C3 input)
//
// Role:    remembers every key (ControlInputEvent kind 5) and mouse button (kind 2) the host
//          injected as down, so that when a control session stops -- ended, or interrupted for a
//          control resume -- the host can release them itself instead of trusting an up that may
//          never come. The physical scan-code path already does this (physicalDown in
//          host_control_session.cpp); this is the same idea for virtual keys and buttons.
// Thread:  any; one mutex. A TCP control thread and the UDP dispatcher can overlap (ledger H-28).
// Input:   injected events, the session epoch at each Serve() start.
// Output:  the list to release at Serve() exit; whether an incoming up is one the host already did.
//
// Why an up can be swallowed. At a resume the host releases what it holds; the viewer, which kept
// the key-ups the user made during the break and also releases everything on resume, then sends
// ups for the same keys. Injected again, each would be a second WM_KEYUP for one keystroke. So an
// up for something the HOST released is dropped -- once it has been released, and only until
// that key or button goes down again. Ups for anything else pass untouched: the session-start
// modifier clear exists precisely for keys this session never pressed.
// A different epoch is a different client, and everything is forgotten.

#include <cstdint>
#include <mutex>
#include <set>
#include <vector>

namespace remote60::native_poc {

class InjectedInputTracker {
 public:
  struct Release {
    uint16_t kind = 0;     // 6 (key up) or 3 (button up)
    uint32_t keyCode = 0;  // VK, or VK_LBUTTON/VK_RBUTTON/VK_MBUTTON
  };

  /** Called at the start of every Serve(). A new epoch forgets everything from the last one. */
  void BeginServe(uint64_t epoch) {
    std::lock_guard<std::mutex> lock(mu_);
    if (epoch == epoch_) return;
    epoch_ = epoch;
    held_.clear();
    released_.clear();
  }

  /**
   * Called for every input event as it arrives, before injection. Returns true when the event
   * is an up for something the host already released itself, and must not be injected.
   * A down clears that mark for its key.
   */
  bool SwallowRelease(uint16_t kind, uint32_t keyCode) {
    const uint32_t id = identity(kind, keyCode);
    if (id == 0) return false;
    std::lock_guard<std::mutex> lock(mu_);
    if (is_down(kind)) {
      released_.erase(id);
      return false;
    }
    // Not erased: the viewer can send more than one up for the same key after a resume (the up
    // it kept from the break, then its release-all), and every one of them is a duplicate.
    return released_.count(id) > 0;
  }

  /** Called for an event the host actually injected. A failed up leaves the key held. */
  void NoteInjected(uint16_t kind, uint32_t keyCode, int32_t x, int32_t y) {
    std::lock_guard<std::mutex> lock(mu_);
    if (kind == 1 || kind == 2 || kind == 3) {
      lastX_ = x;
      lastY_ = y;
    }
    const uint32_t id = identity(kind, keyCode);
    if (id == 0) return;
    if (is_down(kind)) {
      held_.insert(id);
    } else {
      held_.erase(id);
    }
  }

  /**
   * Everything still held, as the ups to inject; each is marked released so the viewer's own up
   * for it is not injected a second time. `x`/`y` get the last pointer position seen.
   */
  std::vector<Release> TakeHeldForRelease(int32_t* x, int32_t* y) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Release> out;
    for (const uint32_t id : held_) {
      Release r;
      r.kind = (id & kButtonBit) ? 3 : 6;
      r.keyCode = id & 0xffu;
      out.push_back(r);
      released_.insert(id);
    }
    held_.clear();
    if (x) *x = lastX_;
    if (y) *y = lastY_;
    return out;
  }

  size_t held_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return held_.size();
  }

 private:
  static constexpr uint32_t kButtonBit = 0x100;

  static bool is_down(uint16_t kind) { return kind == 5 || kind == 2; }

  /** 0 for anything that is not a key or button edge. Keys and buttons never share an id. */
  static uint32_t identity(uint16_t kind, uint32_t keyCode) {
    if (keyCode == 0 || keyCode > 0xff) return 0;
    if (kind == 5 || kind == 6) return keyCode;
    if (kind == 2 || kind == 3) {
      // VK_LBUTTON, VK_RBUTTON, VK_MBUTTON -- the only codes the viewer sends for a button.
      if (keyCode == 1 || keyCode == 2 || keyCode == 4) return kButtonBit | keyCode;
    }
    return 0;
  }

  mutable std::mutex mu_;
  uint64_t epoch_ = 0;
  std::set<uint32_t> held_;
  std::set<uint32_t> released_;
  int32_t lastX_ = 0;
  int32_t lastY_ = 0;
};

}  // namespace remote60::native_poc
