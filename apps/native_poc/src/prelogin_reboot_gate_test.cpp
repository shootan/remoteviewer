// Tests for the remote-reboot host-side gate (prelogin_reboot_gate.hpp). The negative controls are the
// contract (Codex r4 R2/R3): token-less is NotAuthorized; exactly one OS call; a retransmit dedups to
// Accepted with no second call; a different concurrent request is refused.

#include "prelogin_reboot_gate.hpp"

#include <cstdio>

using namespace remote60::native_poc;

namespace {
int gFailures = 0;
void check(bool cond, const char* what) {
  std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!cond) ++gFailures;
}
RebootRequestInputs in(bool auth, bool feat, uint32_t id) { return RebootRequestInputs{auth, feat, id}; }
}  // namespace

int main() {
  std::printf("remote reboot host-side gate\n");

  std::printf("[authorization]\n");
  {
    RebootGateState s;
    auto d = DecideReboot(s, in(/*auth=*/false, /*feat=*/true, 7));
    check(d == RebootDecision::NotAuthorized && RebootResultFor(d) == RebootResult::NotAuthorized && !RebootShouldCallOs(d),
          "NEGATIVE: token-less (not directory-authenticated) -> NotAuthorized, no OS call");
    check(!s.inFlight, "an unauthorized request does not arm the in-flight state");

    d = DecideReboot(s, in(true, /*feat=*/false, 7));
    check(d == RebootDecision::Refused && RebootResultFor(d) == RebootResult::Refused && !RebootShouldCallOs(d),
          "feature not allowed -> Refused, no OS call");
    check(!s.inFlight, "a refused request does not arm in-flight");
  }

  std::printf("[single restart + idempotent dedup]\n");
  {
    RebootGateState s;
    auto d = DecideReboot(s, in(true, true, 100));
    check(d == RebootDecision::AcceptAndInitiate && RebootResultFor(d) == RebootResult::Accepted && RebootShouldCallOs(d),
          "first valid request -> AcceptAndInitiate (one OS call), Accepted");
    check(s.inFlight && s.acceptedRequestId == 100, "in-flight armed with the accepted requestId");

    d = DecideReboot(s, in(true, true, 100));  // retransmit of the SAME request
    check(d == RebootDecision::DuplicateAccepted && RebootResultFor(d) == RebootResult::Accepted && !RebootShouldCallOs(d),
          "NEGATIVE: same-requestId retransmit -> Accepted again but NO second OS call");

    d = DecideReboot(s, in(true, true, 200));  // a DIFFERENT request while one is in flight
    check(d == RebootDecision::Refused && !RebootShouldCallOs(d),
          "NEGATIVE: a different request while one is in flight -> Refused (one restart)");
  }

  std::printf("[auth is checked before dedup: an unauthorized retransmit never dedups into Accept]\n");
  {
    RebootGateState s;
    DecideReboot(s, in(true, true, 55));  // accept 55 (in flight)
    auto d = DecideReboot(s, in(/*auth=*/false, true, 55));
    check(d == RebootDecision::NotAuthorized,
          "NEGATIVE: same id but now unauthenticated -> NotAuthorized (not DuplicateAccepted)");
  }

  if (gFailures != 0) {
    std::printf("prelogin_reboot_gate_test: FAIL (%d)\n", gFailures);
    return 1;
  }
  std::printf("prelogin_reboot_gate_test: PASS\n");
  return 0;
}
