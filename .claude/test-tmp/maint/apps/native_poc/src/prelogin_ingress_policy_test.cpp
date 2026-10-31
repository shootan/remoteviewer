// Counter-example tests for the prelogin ingress authentication policy (prelogin_ingress_policy.hpp, G2).
//
// Codex G2: a token-less FIRST Hello was accepted and a separate unauthenticated link could borrow the
// session's auth. The gate below refuses a token-less prelogin Hello and marks the side channels (TCP
// control listener / LAN direct-dial) as disabled in prelogin. Non-prelogin behaviour is unchanged.

#include "prelogin_ingress_policy.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

int main() {
  int failures = 0;
  auto ok = [&](bool c, const char* w) { std::printf(c ? "  ok    %s\n" : "  FAIL  %s\n", w); if (!c) ++failures; };
  std::printf("prelogin ingress policy (G2)\n");

  // Non-prelogin is unchanged: a LAN client with no directory token is still admitted.
  ok(PreloginAdmitFirstHello(/*prelogin=*/false, /*authed=*/false),
     "non-prelogin token-less first hello is admitted (unchanged LAN behaviour)");
  // Counter-example (the G2 defect): a token-less FIRST hello in prelogin must be refused.
  ok(!PreloginAdmitFirstHello(/*prelogin=*/true, /*authed=*/false),
     "NEGATIVE: prelogin token-less first hello is REFUSED");
  ok(PreloginAdmitFirstHello(/*prelogin=*/true, /*authed=*/true),
     "prelogin authenticated first hello is admitted");

  ok(PreloginAllowsSideChannel(false), "non-prelogin keeps the TCP control + LAN direct-dial listeners");
  ok(!PreloginAllowsSideChannel(true),
     "NEGATIVE: prelogin opens NO side channel (no unauthenticated link to borrow auth)");

  if (failures != 0) { std::printf("prelogin_ingress_policy_test: FAIL (%d)\n", failures); return 1; }
  std::printf("prelogin_ingress_policy_test: PASS\n");
  return 0;
}
