// The SYSTEM input agent's decision for one event, and the calls it makes. (mouse-xbutton r2)
//
// The defect being pinned: for a button edge whose key was not a button, the agent called
// SetCursorPos first and refused afterwards -- the cursor had moved for an event that was then
// dropped, while the host's own path (checked first) had not moved it. Codex review of r1.
//
// The product's executor (run_input_event) is run here with RECORDING functions in place of
// SetCursorPos and SendInput, so what is asserted is the sequence of OS calls a message would
// produce, with nothing reaching the desktop. plan_input_event is the product's decision; the
// service passes the real APIs to the same executor.
//
// Build: remote60_secure_input_inject_test (CMake).

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "secure_input_inject.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ",
              detail.c_str());
}

std::string hex(unsigned long long v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%llx", v);
  return buf;
}

SecureInputMessage event(uint16_t eventKind, uint32_t keyCode = 0, int32_t wheel = 0) {
  SecureInputMessage m{};
  m.kind = static_cast<uint16_t>(SecureInputKind::InputEvent);
  m.eventKind = eventKind;
  m.keyCode = keyCode;
  m.wheelDelta = wheel;
  return m;
}

/** What the executor asked the OS to do, in order. */
struct Recorder {
  std::vector<std::string> calls;  // "cursor(x,y)" / "input(...)"
  int cursorCalls = 0;
  int inputCalls = 0;
  POINT lastCursor{};
  INPUT lastInput{};
  bool cursorResult = true;
  bool inputResult = true;

  auto setCursor() {
    return [this](long x, long y) {
      ++cursorCalls;
      lastCursor = POINT{x, y};
      calls.push_back("cursor(" + std::to_string(x) + "," + std::to_string(y) + ")");
      return cursorResult;
    };
  }
  auto sendInput() {
    return [this](INPUT& in) {
      ++inputCalls;
      lastInput = in;
      calls.push_back(in.type == INPUT_MOUSE
                          ? "mouse(flags=" + hex(in.mi.dwFlags) + ",data=" + std::to_string(in.mi.mouseData) + ")"
                          : "key(vk=" + std::to_string(in.ki.wVk) + ",flags=" + hex(in.ki.dwFlags) + ")");
      return inputResult;
    };
  }
  std::string trace() const {
    std::string s;
    for (const auto& c : calls) s += (s.empty() ? "" : " -> ") + c;
    return s.empty() ? "(no OS call)" : s;
  }
};

const POINT kPoint{640, 360};

/** The agent's sequence for one message: plan, and -- only when valid -- run. As the service does. */
InjectRun drive(const SecureInputMessage& m, Recorder& rec, InjectPlan* planOut = nullptr) {
  const InjectPlan plan = plan_input_event(m);
  if (planOut) *planOut = plan;
  // The service refuses an invalid plan before running it. Running it anyway here shows the
  // executor itself makes no call for one either.
  return run_input_event(plan, kPoint, rec.setCursor(), rec.sendInput());
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("--- a button edge whose key is not a button: nothing moves, nothing is sent ---\n");
  for (const uint32_t key : {0x07u, 0x00u, static_cast<uint32_t>('A'), 0x1234u}) {
    for (const uint16_t kind : {uint16_t{2}, uint16_t{3}}) {
      Recorder rec;
      InjectPlan plan;
      const InjectRun run = drive(event(kind, key), rec, &plan);
      check("kind " + std::to_string(kind) + " key " + hex(key) + ": refused before any call",
            !plan.valid && std::string(plan.why) == "unknown-mouse-key" && run == InjectRun::Refused &&
                rec.cursorCalls == 0 && rec.inputCalls == 0,
            rec.trace());
    }
  }
  {
    Recorder rec;
    InjectPlan plan;
    const InjectRun run = drive(event(9, VK_LBUTTON), rec, &plan);
    check("an unknown event kind is refused before any call",
          !plan.valid && std::string(plan.why) == "unhandled-event-kind" && run == InjectRun::Refused &&
              rec.cursorCalls == 0 && rec.inputCalls == 0,
          rec.trace());
  }

  std::printf("\n--- X1 / X2: the cursor is placed once, then the one INPUT ---\n");
  {
    Recorder rec;
    const InjectRun run = drive(event(2, VK_XBUTTON1), rec);
    check("X1 down: cursor(640,360) then MOUSEEVENTF_XDOWN with mouseData XBUTTON1",
          run == InjectRun::Done && rec.cursorCalls == 1 && rec.lastCursor.x == 640 && rec.lastCursor.y == 360 &&
              rec.inputCalls == 1 && rec.lastInput.type == INPUT_MOUSE && rec.lastInput.mi.dwFlags == MOUSEEVENTF_XDOWN &&
              rec.lastInput.mi.mouseData == XBUTTON1 && rec.calls.size() == 2 && rec.calls[0].rfind("cursor", 0) == 0,
          rec.trace());
  }
  {
    Recorder rec;
    const InjectRun run = drive(event(3, VK_XBUTTON1), rec);
    check("X1 up: cursor then MOUSEEVENTF_XUP / XBUTTON1",
          run == InjectRun::Done && rec.cursorCalls == 1 && rec.inputCalls == 1 &&
              rec.lastInput.mi.dwFlags == MOUSEEVENTF_XUP && rec.lastInput.mi.mouseData == XBUTTON1,
          rec.trace());
  }
  {
    Recorder rec;
    const InjectRun d = drive(event(2, VK_XBUTTON2), rec);
    const bool downOk = d == InjectRun::Done && rec.lastInput.mi.dwFlags == MOUSEEVENTF_XDOWN &&
                        rec.lastInput.mi.mouseData == XBUTTON2;
    const InjectRun u = drive(event(3, VK_XBUTTON2), rec);
    check("X2 down / up: XDOWN then XUP, mouseData XBUTTON2, one cursor call each",
          downOk && u == InjectRun::Done && rec.lastInput.mi.dwFlags == MOUSEEVENTF_XUP &&
              rec.lastInput.mi.mouseData == XBUTTON2 && rec.cursorCalls == 2 && rec.inputCalls == 2,
          rec.trace());
  }

  std::printf("\n--- everything else is as before ---\n");
  {
    Recorder rec;
    const InjectRun run = drive(event(1), rec);
    check("a move places the cursor and sends no INPUT",
          run == InjectRun::Done && rec.cursorCalls == 1 && rec.inputCalls == 0, rec.trace());
  }
  {
    Recorder rec;
    const InjectRun run = drive(event(4, 0, -120), rec);
    check("a wheel places the cursor then MOUSEEVENTF_WHEEL with the delta",
          run == InjectRun::Done && rec.cursorCalls == 1 && rec.inputCalls == 1 &&
              rec.lastInput.mi.dwFlags == MOUSEEVENTF_WHEEL &&
              static_cast<SHORT>(static_cast<WORD>(rec.lastInput.mi.mouseData)) == -120,
          rec.trace());
  }
  {
    Recorder rec;
    drive(event(2, VK_LBUTTON), rec);
    const bool l = rec.lastInput.mi.dwFlags == MOUSEEVENTF_LEFTDOWN && rec.lastInput.mi.mouseData == 0;
    drive(event(3, VK_RBUTTON), rec);
    const bool r = rec.lastInput.mi.dwFlags == MOUSEEVENTF_RIGHTUP;
    drive(event(2, VK_MBUTTON), rec);
    const bool m = rec.lastInput.mi.dwFlags == MOUSEEVENTF_MIDDLEDOWN;
    check("left / right / middle unchanged, cursor once per edge",
          l && r && m && rec.cursorCalls == 3 && rec.inputCalls == 3, rec.trace());
  }
  {
    Recorder rec;
    const InjectRun run = drive(event(5, 'A'), rec);
    check("a key down sends the key and does not touch the cursor",
          run == InjectRun::Done && rec.cursorCalls == 0 && rec.inputCalls == 1 &&
              rec.lastInput.type == INPUT_KEYBOARD && rec.lastInput.ki.wVk == 'A' &&
              rec.lastInput.ki.wScan == MapVirtualKeyW('A', MAPVK_VK_TO_VSC) && rec.lastInput.ki.dwFlags == 0,
          rec.trace());
    drive(event(6, VK_LEFT), rec);
    check("a key up of an extended key: KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY, still no cursor",
          rec.cursorCalls == 0 && rec.lastInput.ki.wVk == VK_LEFT &&
              rec.lastInput.ki.dwFlags == (KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY),
          rec.trace());
  }

  std::printf("\n--- a failing cursor call stops the edge; nothing is sent after it ---\n");
  {
    Recorder rec;
    rec.cursorResult = false;
    const InjectRun run = drive(event(2, VK_XBUTTON1), rec);
    check("SetCursorPos fails -> SetCursorPosFailed, 0 INPUT",
          run == InjectRun::SetCursorPosFailed && rec.cursorCalls == 1 && rec.inputCalls == 0, rec.trace());
  }
  {
    Recorder rec;
    rec.inputResult = false;
    const InjectRun run = drive(event(2, VK_XBUTTON1), rec);
    check("SendInput fails -> SendInputFailed after one cursor call",
          run == InjectRun::SendInputFailed && rec.cursorCalls == 1 && rec.inputCalls == 1, rec.trace());
  }

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED",
              gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
