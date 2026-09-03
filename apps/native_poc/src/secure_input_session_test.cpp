// The measured bug this covers: on a machine reached over RDP the host streamed session 1 while
// WTSGetActiveConsoleSessionId() returned 8, so every injected click landed on the physical
// console's desktop and was still reported as delivered. The regression that matters is silent,
// so the rules get pinned here rather than left to the call site.

#include "secure_input_session.hpp"

#include <cstdio>
#include <string>

using remote60::native_poc::kInvalidSessionId;
using remote60::native_poc::resolve_target_session;
using remote60::native_poc::SessionSource;
using remote60::native_poc::session_source_name;

namespace {

int gFailures = 0;

void expect(const std::string& what, uint32_t requester, bool stateKnown, bool active,
            uint32_t console, uint32_t wantSession, SessionSource wantSource) {
  const auto got = resolve_target_session(requester, stateKnown, active, console);
  if (got.sessionId != wantSession || got.source != wantSource) {
    std::printf("  FAIL %s: requester=%u known=%d active=%d console=%u -> session=%u source=%s, wanted %u/%s\n",
                what.c_str(), requester, stateKnown ? 1 : 0, active ? 1 : 0, console, got.sessionId,
                session_source_name(got.source), wantSession, session_source_name(wantSource));
    ++gFailures;
  } else {
    std::printf("  ok   %s -> session=%u source=%s\n", what.c_str(), got.sessionId,
                session_source_name(got.source));
  }
}

// The exact configuration measured on the development machine. If this ever regresses, clicks go
// to a screen nobody is looking at and the product still reports success.
// Active RDP: the streamed session is connected, so it wins over the physical console.
void TestRdpSessionBeatsConsole() {
  std::printf("the streamed session wins over the physical console when active\n");
  expect("host in RDP session 1 (active), console is 8", 1, true, true, 8, 1,
         SessionSource::Requester);
}

// G2 (Codex review #362): the measured lock case. The RDP client dropped, session 1 is Disconnected
// and the LogonUI runs on console 13. Targeting session 1 gave SendInput err=5; the console is right.
void TestDisconnectedRequesterFallsToConsole() {
  std::printf("a disconnected requester yields to the console (the lock screen lives there)\n");
  expect("requester 1 disconnected, console 13", 1, true, false, 13, 13, SessionSource::Console);
  expect("requester 1 connecting (not yet active), console 13", 1, true, false, 13, 13,
         SessionSource::Console);
  // Probe failed: a valid console is the stronger signal; never inject into a stale requester.
  expect("requester 1 state unknown, console 13", 1, false, false, 13, 13, SessionSource::Console);
}

// Requester == console means the console API itself named the requester as attached: honor it even
// when the connect-state probe could not decide.
void TestRequesterEqualsConsole() {
  std::printf("requester equal to console is trusted regardless of the state probe\n");
  expect("both are 13, state unknown", 13, false, false, 13, 13, SessionSource::Requester);
}

void TestConsoleUsedOnlyWhenRequesterUnknown() {
  std::printf("the console is a fallback, not the default\n");
  expect("requester unknown", kInvalidSessionId, false, false, 8, 8, SessionSource::Console);
  expect("requester is session 0 (a service has no desktop)", 0, true, true, 8, 8,
         SessionSource::Console);
}

// A session-0 agent could attach to no interactive desktop at all, so it must never be chosen --
// including when it is all that is on offer.
void TestSessionZeroIsNeverChosen() {
  std::printf("session 0 is the services session and is never a target\n");
  expect("both are session 0", 0, true, true, 0, kInvalidSessionId, SessionSource::None);
  expect("console is 0, requester unknown", kInvalidSessionId, false, false, 0, kInvalidSessionId,
         SessionSource::None);
}

// WTSGetActiveConsoleSessionId returns 0xffffffff while no session is attached to the console,
// which happens during fast user switching and during a console transition.
void TestNothingUsableIsReported() {
  std::printf("no usable session is reported rather than guessed\n");
  expect("both unknown", kInvalidSessionId, false, false, kInvalidSessionId, kInvalidSessionId,
         SessionSource::None);
  // Disconnected requester during a console transition (console 0xffffffff): guess nothing, retry.
  expect("requester 1 disconnected, console in transition", 1, true, false, kInvalidSessionId,
         kInvalidSessionId, SessionSource::None);
}

void TestOrdinaryConsoleLogin() {
  std::printf("sitting at the machine, both agree\n");
  expect("requester and console are both 1", 1, true, true, 1, 1, SessionSource::Requester);
}

}  // namespace

int main() {
  TestRdpSessionBeatsConsole();
  TestDisconnectedRequesterFallsToConsole();
  TestRequesterEqualsConsole();
  TestConsoleUsedOnlyWhenRequesterUnknown();
  TestSessionZeroIsNeverChosen();
  TestNothingUsableIsReported();
  TestOrdinaryConsoleLogin();

  if (gFailures != 0) {
    std::printf("secure_input_session_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("secure_input_session_test: PASS\n");
  return 0;
}
