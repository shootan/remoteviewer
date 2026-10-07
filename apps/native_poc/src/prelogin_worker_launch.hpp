#pragma once

// How the service starts the pre-login worker (prelogin-ui r6, V1/V2): its arguments, its environment,
// and how often it tries again when the worker does not stay up.
//
// One definition, used by the service (secure_input_service_main.cpp ensure_prelogin_worker) AND by the
// end-to-end test that starts a worker the way the service does -- so the test cannot quietly hand the
// worker something the service never does (that is how the missing encoder switch went unseen).
// Pure; no Win32 here.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "stream_child_env.hpp"

namespace remote60::native_poc::prelogin {

// The worker's whole argument list. Nothing else: no directory, no config -- the machine record is its
// only credential (host_startup_* refuse anything more in --prelogin).
inline std::wstring PreloginWorkerArgs() { return L"--prelogin"; }

// The worker's environment: the starter's own (`base`, a GetEnvironmentStringsW block) with the
// product's streaming switches stated on top (GPU surface encode off before login).
inline std::vector<wchar_t> PreloginWorkerEnvironment(const wchar_t* base) {
  return BuildEnvironmentBlock(base, ProductStreamChildEnv(/*nv12Surface=*/false));
}

// When to start the worker again. A worker that dies soon after it was started is not restarted every
// second for ever: each quick death doubles the wait (1 s .. 60 s); one that ran a while starts the wait
// over. Every decision is reported by the caller (exit code, how long it ran, the wait).
struct WorkerRestartBackoff {
  static constexpr uint64_t kMinMs = 1000;
  static constexpr uint64_t kMaxMs = 60000;
  static constexpr uint64_t kQuickDeathMs = 15000;
  uint64_t waitMs = kMinMs;
  uint64_t nextLaunchMs = 0;
  // The worker launched at `launchedMs` ended at `nowMs`: when may the next start be?
  uint64_t OnExit(uint64_t launchedMs, uint64_t nowMs) {
    const bool quick = nowMs - launchedMs < kQuickDeathMs;
    waitMs = quick ? (std::min)(waitMs * 2, kMaxMs) : kMinMs;
    nextLaunchMs = nowMs + waitMs;
    return waitMs;
  }
  // A start that failed outright counts as a quick death.
  uint64_t OnLaunchFailed(uint64_t nowMs) { return OnExit(nowMs, nowMs); }
  bool MayLaunch(uint64_t nowMs) const { return nowMs >= nextLaunchMs; }
};

}  // namespace remote60::native_poc::prelogin
