// Test for the prelogin handoff ready signal primitive (prelogin_ready_signal.hpp). Single process: not
// signalled -> signal -> observed -> reset -> not observed -> signal again. The event is in the Global\
// namespace (it must cross the session boundary in production), which needs SeCreateGlobalPrivilege; the
// elevated user Host and the SYSTEM service hold it. If this (possibly non-elevated) runner cannot create
// a Global object, the primitive test SKIPs with a note -- the handoff REACTION to the ready signal is
// proven separately and always by prelogin_supervisor_test (ready-delay keeps the worker; ready completes
// the single-owner handoff; a stale generation is fenced).

#include "prelogin_ready_signal.hpp"

#include <cstdio>

using namespace remote60::native_poc::prelogin;

int main() {
  std::printf("prelogin ready signal primitive\n");
  int failures = 0;
  auto ok = [&](bool c, const char* w) { std::printf(c ? "  ok   %s\n" : "  FAIL %s\n", w); if (!c) ++failures; };

  ok(!UserHostReadySignaled(), "not signalled before any host signals");

  if (!SignalUserHostReady()) {
    std::printf("  SKIP primitive set/reset (Global\\ event needs SeCreateGlobalPrivilege; elevated host/"
                "service hold it)\n");
    std::printf("prelogin_ready_signal_test: PASS-with-SKIP\n");
    return failures == 0 ? 0 : 1;
  }

  ok(UserHostReadySignaled(), "after SignalUserHostReady -> observed as ready");
  ResetUserHostReady();
  ok(!UserHostReadySignaled(), "after ResetUserHostReady (new handoff) -> not ready");
  ok(SignalUserHostReady() && UserHostReadySignaled(), "signalling again -> ready once more");

  if (failures != 0) { std::printf("prelogin_ready_signal_test: FAIL (%d)\n", failures); return 1; }
  std::printf("prelogin_ready_signal_test: PASS\n");
  return 0;
}
