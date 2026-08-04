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

void expect(const std::string& what, uint32_t requester, uint32_t console, uint32_t wantSession,
            SessionSource wantSource) {
  const auto got = resolve_target_session(requester, console);
  if (got.sessionId != wantSession || got.source != wantSource) {
    std::printf("  FAIL %s: requester=%u console=%u -> session=%u source=%s, wanted %u/%s\n",
                what.c_str(), requester, console, got.sessionId,
                session_source_name(got.source), wantSession, session_source_name(wantSource));
    ++gFailures;
  } else {
    std::printf("  ok   %s -> session=%u source=%s\n", what.c_str(), got.sessionId,
                session_source_name(got.source));
  }
}

// The exact configuration measured on the development machine. If this ever regresses, clicks go
// to a screen nobody is looking at and the product still reports success.
void TestRdpSessionBeatsConsole() {
  std::printf("the streamed session wins over the physical console\n");
  expect("host in RDP session 1, console is 8", 1, 8, 1, SessionSource::Requester);
}

void TestConsoleUsedOnlyWhenRequesterUnknown() {
  std::printf("the console is a fallback, not the default\n");
  expect("requester unknown", kInvalidSessionId, 8, 8, SessionSource::Console);
  expect("requester is session 0 (a service has no desktop)", 0, 8, 8, SessionSource::Console);
}

// A session-0 agent could attach to no interactive desktop at all, so it must never be chosen --
// including when it is all that is on offer.
void TestSessionZeroIsNeverChosen() {
  std::printf("session 0 is the services session and is never a target\n");
  expect("both are session 0", 0, 0, kInvalidSessionId, SessionSource::None);
  expect("console is 0, requester unknown", kInvalidSessionId, 0, kInvalidSessionId,
         SessionSource::None);
}

// WTSGetActiveConsoleSessionId returns 0xffffffff while no session is attached to the console,
// which happens during fast user switching and during a console transition.
void TestNothingUsableIsReported() {
  std::printf("no usable session is reported rather than guessed\n");
  expect("both unknown", kInvalidSessionId, kInvalidSessionId, kInvalidSessionId,
         SessionSource::None);
}

void TestOrdinaryConsoleLogin() {
  std::printf("sitting at the machine, both agree\n");
  expect("requester and console are both 1", 1, 1, 1, SessionSource::Requester);
}

}  // namespace

int main() {
  TestRdpSessionBeatsConsole();
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
