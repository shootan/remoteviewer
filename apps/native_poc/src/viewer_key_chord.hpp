#pragma once

// A modifier+key chord, as the four input events the host must receive.
//
// Role:    host_key_chord() -- the order of down/up events for chords the local OS will not let
//          the user type at the remote machine (Win+D, Alt+Tab). Pure, so the order is testable.
// Thread:  none (pure).
// Callers: viewer_startup (the toolbar's 바탕화면 / 화면전환 buttons), viewer tests.
//
// The order is the whole content of this file, and it is not arbitrary. The modifier goes down
// first and comes up LAST, because the host applies these as real keystrokes: releasing the
// modifier early turns Alt+Tab into a bare Tab, and failing to release it at all leaves Alt stuck
// down on someone else's machine, where every later click behaves strangely and nothing the viewer
// does afterwards explains why. This project has already paid for a stuck modifier once -- the
// viewer clears them on connect for exactly that reason (viewer_startup.cpp).

#include <array>
#include <cstdint>

namespace remote60::native_poc::viewer {

// kind matches ControlInputEventMessage: 5 = key_down, 6 = key_up.
struct HostKeyStep {
  uint16_t kind = 0;
  uint32_t vk = 0;
};

constexpr uint16_t kHostKeyDown = 5;
constexpr uint16_t kHostKeyUp = 6;

/** The four steps of `modifier`+`key`, in the order the host must see them. */
inline std::array<HostKeyStep, 4> host_key_chord(uint32_t modifierVk, uint32_t keyVk) {
  return {HostKeyStep{kHostKeyDown, modifierVk},
          HostKeyStep{kHostKeyDown, keyVk},
          HostKeyStep{kHostKeyUp, keyVk},
          HostKeyStep{kHostKeyUp, modifierVk}};
}

}  // namespace remote60::native_poc::viewer
