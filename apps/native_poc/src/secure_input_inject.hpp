#pragma once

// What the SYSTEM input agent does for one input event, decided before anything is touched.
// (mouse-xbutton r2)
//
// The agent used to place the cursor first and only then look at the button: for an event kind
// of 2 or 3 it called SetCursorPos, and if the key turned out not to be a button it refused --
// after the cursor had already moved. The host's own path checks first, so the two disagreed
// about what an unknown key does. Now the whole decision is made here, as a plan, and the agent
// executes it: an invalid plan touches nothing, a valid one moves the cursor (if the kind calls
// for it) and then sends its one INPUT.
//
// The plan is a value and the executor takes the two OS calls as parameters, so a test can run
// the SAME executor with recording functions and see exactly which calls a message would make
// -- without SendInput ever reaching the desktop. The service passes the real SetCursorPos and
// SendInput.

#include <windows.h>

#include <cstdint>

#include "mouse_button_map.hpp"
#include "secure_input_protocol.hpp"

namespace remote60::native_poc {

struct InjectPlan {
  bool valid = false;         // false: refuse the event; nothing is moved and nothing is sent
  const char* why = "";       // the refusal, for the diagnostic log (valid == false only)
  bool moveCursor = false;    // SetCursorPos(point) first
  bool sendInput = false;     // then SendInput(1, &input)
  INPUT input{};
};

/**
 * The plan for an InputEvent message. Pure apart from MapVirtualKeyW (a table lookup).
 *
 *   1 move          cursor only
 *   2 / 3 button    cursor, then the button edge -- refused outright for a key that is not a
 *                   mouse button (pre-r2: the cursor moved and then it was refused)
 *   4 wheel         cursor, then MOUSEEVENTF_WHEEL with the delta
 *   5 / 6 key       the key only, no cursor
 *   anything else   refused
 */
inline InjectPlan plan_input_event(const SecureInputMessage& m) {
  InjectPlan p;
  switch (m.eventKind) {
    case 1:
      p.valid = true;
      p.moveCursor = true;
      return p;
    case 2:
    case 3: {
      const MouseSendInput mouse = mouse_vk_to_sendinput(m.eventKind, m.keyCode);
      if (mouse.flags == 0) {
        p.why = "unknown-mouse-key";  // decided before the cursor is placed: nothing moves
        return p;
      }
      p.valid = true;
      p.moveCursor = true;
      p.sendInput = true;
      p.input.type = INPUT_MOUSE;
      p.input.mi.dwFlags = mouse.flags;
      p.input.mi.mouseData = mouse.mouseData;
      return p;
    }
    case 4:
      p.valid = true;
      p.moveCursor = true;
      p.sendInput = true;
      p.input.type = INPUT_MOUSE;
      p.input.mi.dwFlags = MOUSEEVENTF_WHEEL;
      p.input.mi.mouseData = static_cast<DWORD>(static_cast<SHORT>(m.wheelDelta));
      return p;
    case 5:
    case 6:
      p.valid = true;
      p.sendInput = true;
      p.input.type = INPUT_KEYBOARD;
      p.input.ki.wVk = static_cast<WORD>(m.keyCode);
      p.input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(m.keyCode, MAPVK_VK_TO_VSC));
      if (m.eventKind == 6) p.input.ki.dwFlags |= KEYEVENTF_KEYUP;
      switch (m.keyCode) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_INSERT: case VK_DELETE: case VK_RCONTROL: case VK_RMENU:
          p.input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
          break;
        default:
          break;
      }
      return p;
    default:
      p.why = "unhandled-event-kind";
      return p;
  }
}

enum class InjectRun : uint8_t {
  Done = 0,
  Refused,            // the plan was invalid: no call was made
  SetCursorPosFailed, // the cursor call failed; nothing was sent after it
  SendInputFailed,
};

/**
 * Executes a plan: cursor first (when the plan says so), then the one INPUT (when it says so).
 * `setCursor(x, y)` and `sendInput(INPUT&)` return true on success. Returns right after the
 * failing call, so the caller's GetLastError still describes it.
 */
template <class SetCursorFn, class SendInputFn>
InjectRun run_input_event(const InjectPlan& plan, POINT point, SetCursorFn&& setCursor,
                          SendInputFn&& sendInput) {
  if (!plan.valid) return InjectRun::Refused;
  if (plan.moveCursor && !setCursor(point.x, point.y)) return InjectRun::SetCursorPosFailed;
  if (plan.sendInput) {
    INPUT input = plan.input;
    if (!sendInput(input)) return InjectRun::SendInputFailed;
  }
  return InjectRun::Done;
}

}  // namespace remote60::native_poc
