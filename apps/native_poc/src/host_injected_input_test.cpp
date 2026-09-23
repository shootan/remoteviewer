// InjectedInputTracker: what the host releases on its own, and which ups it then refuses to
// inject a second time. (RV-01)

#include <cstdio>
#include <string>

#include "host_injected_input.hpp"

using remote60::native_poc::InjectedInputTracker;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const char* name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  ", detail.c_str());
}

constexpr uint16_t kMouseDown = 2, kMouseUp = 3, kKeyDown = 5, kKeyUp = 6, kMove = 1;
constexpr uint32_t kShift = 0x10, kK = 'K', kLeft = 1;

}  // namespace

int main() {
  {
    InjectedInputTracker t;
    t.BeginServe(1);
    t.NoteInjected(kKeyDown, kShift, 0, 0);
    t.NoteInjected(kMouseDown, kLeft, 40, 50);
    t.NoteInjected(kKeyDown, kK, 0, 0);
    t.NoteInjected(kKeyUp, kK, 0, 0);
    int32_t x = 0, y = 0;
    const auto held = t.TakeHeldForRelease(&x, &y);
    bool shiftUp = false, leftUp = false, kUp = false;
    for (const auto& r : held) {
      if (r.kind == kKeyUp && r.keyCode == kShift) shiftUp = true;
      if (r.kind == kMouseUp && r.keyCode == kLeft) leftUp = true;
      if (r.keyCode == kK) kUp = true;
    }
    check("held keys and buttons are released, and only those", held.size() == 2 && shiftUp && leftUp && !kUp,
          std::to_string(held.size()) + " releases");
    check("...a button is released where the pointer last was", x == 40 && y == 50);
    check("...and nothing is left held", t.held_count() == 0);
    check("the viewer's up for a released key is swallowed", t.SwallowRelease(kKeyUp, kShift));
    check("...and so is a second one (kept up + release-all)", t.SwallowRelease(kKeyUp, kShift));
    check("...and the button's", t.SwallowRelease(kMouseUp, kLeft));
    check("an up for a key the host never released passes", !t.SwallowRelease(kKeyUp, kK));
    check("an up for a modifier this session never pressed passes (session-start clear)",
          !t.SwallowRelease(kKeyUp, 0xA0));
    check("a down is never swallowed", !t.SwallowRelease(kKeyDown, kShift));
    check("...and after it the key's up passes again", !t.SwallowRelease(kKeyUp, kShift));
    check("moves and wheel are not edges", !t.SwallowRelease(kMove, 0) && !t.SwallowRelease(4, 0));
  }
  {
    InjectedInputTracker t;
    t.BeginServe(1);
    t.NoteInjected(kKeyDown, kShift, 0, 0);
    // The key-up's injection failed: NoteInjected is not called for it, so it stays held.
    int32_t x = 0, y = 0;
    check("a key whose up failed to inject is still released at the end",
          t.TakeHeldForRelease(&x, &y).size() == 1);
  }
  {
    InjectedInputTracker t;
    t.BeginServe(1);
    t.NoteInjected(kKeyDown, kShift, 0, 0);
    int32_t x = 0, y = 0;
    (void)t.TakeHeldForRelease(&x, &y);
    t.BeginServe(1);  // a resume: same epoch
    check("a resume keeps the marks", t.SwallowRelease(kKeyUp, kShift));
    t.NoteInjected(kKeyDown, kK, 0, 0);
    t.BeginServe(2);  // a new client
    check("a new client forgets the marks", !t.SwallowRelease(kKeyUp, kShift));
    check("...and what an earlier client held", t.held_count() == 0);
  }
  {
    InjectedInputTracker t;
    t.BeginServe(1);
    t.NoteInjected(kMouseDown, 0x05, 0, 0);  // XBUTTON1 is not a button the viewer sends
    t.NoteInjected(kKeyDown, 0x1234, 0, 0);  // out of range
    check("codes outside the protocol's are not tracked", t.held_count() == 0);
  }
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED",
              gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
