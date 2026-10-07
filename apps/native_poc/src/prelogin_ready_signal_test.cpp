// Test for the prelogin handoff ready signal primitive (prelogin_ready_signal.hpp). Single process: not
// signalled -> signal -> observed -> reset -> not observed -> signal again. The event is in the Global\
// namespace (it must cross the session boundary in production), which needs SeCreateGlobalPrivilege; the
// elevated user Host and the SYSTEM service hold it. If this (possibly non-elevated) runner cannot create
// a Global object, the primitive test SKIPs with a note -- the handoff REACTION to the ready signal is
// proven separately and always by prelogin_supervisor_test (ready-delay keeps the worker; ready completes
// the single-owner handoff; a stale generation is fenced).
//
// r6 G9: also proves the per-launch nonce binding -- a host launched with nonce A satisfies ONLY the
// nonce-A observer; a foreign host's nonce B and the legacy base name are NOT satisfied (the counter-
// example for "any GNLinkStream of the install could complete the handoff").

#include "prelogin_ready_signal.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

using namespace remote60::native_poc::prelogin;

int main() {
  std::printf("prelogin ready signal primitive\n");
  int failures = 0;
  auto ok = [&](bool c, const char* w) { std::printf(c ? "  ok   %s\n" : "  FAIL %s\n", w); if (!c) ++failures; };

  // The launching service injects a per-launch nonce; emulate that BEFORE the host signals (the held
  // event handle is created on the first Signal call, so the nonce must be in the environment first).
  SetEnvironmentVariableW(L"REMOTE60_PRELOGIN_READY_NONCE", L"A");
  ok(ReadReadyNonceFromEnv() == L"A", "host reads the injected launch nonce from the environment");

  ok(!UserHostReadySignaled(L"A"), "not signalled before any host signals (nonce A)");

  if (!SignalUserHostReady()) {
    std::printf("  SKIP primitive set/reset (Global\\ event needs SeCreateGlobalPrivilege; elevated host/"
                "service hold it)\n");
    std::printf("prelogin_ready_signal_test: PASS-with-SKIP\n");
    return failures == 0 ? 0 : 1;
  }

  ok(UserHostReadySignaled(L"A"), "after SignalUserHostReady -> observed as ready on the owning nonce A");

  // Counter-examples (G9): a DIFFERENT launch's observer (nonce B) and the legacy base-name observer must
  // NOT see this host as ready -- the handoff is bound to the launch that started it.
  ok(!UserHostReadySignaled(L"B"), "a foreign launch nonce B does NOT observe this host as ready");
  ok(!UserHostReadySignaled(std::wstring()), "the legacy base name does NOT observe a nonce'd host as ready");

  ResetUserHostReady(L"A");
  ok(!UserHostReadySignaled(L"A"), "after ResetUserHostReady (new handoff) -> not ready");
  ok(SignalUserHostReady() && UserHostReadySignaled(L"A"), "signalling again -> ready once more (nonce A)");

  if (failures != 0) { std::printf("prelogin_ready_signal_test: FAIL (%d)\n", failures); return 1; }
  std::printf("prelogin_ready_signal_test: PASS\n");
  return 0;
}
