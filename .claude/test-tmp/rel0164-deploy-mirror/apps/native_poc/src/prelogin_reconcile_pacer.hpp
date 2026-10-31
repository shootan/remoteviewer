#pragma once

// The pre-login reconcile loop's wait between ticks (prelogin-ui r14, H1). Pure.
//
// The loop used to sleep in 100 ms slices for up to 1 s and leave early while gSessionChangePending was
// set -- a flag only ensure_agent clears, i.e. only when a secure-input message arrives. After a logon, a
// lock, an unlock or a console connect it stayed set, so every wait ended at once: the loop spun at 100%
// CPU (registry, DPAPI unprotect, WTSQueryUserToken, a toolhelp snapshot each turn) until the next secure
// input, and the supervisor's 10-try transient budget was spent in milliseconds -- a short token-query
// error became a Fault that stopped a healthy worker.
//
// Now the loop owns what it waits on: the session-topology generation as it stood when this tick began.
// A change that came after that ends the wait early (once -- the next tick starts from the new value); no
// change, the whole period. Nothing here is shared with ensure_agent.

#include <cstdint>

namespace remote60::native_poc::prelogin {

inline constexpr int kReconcileSlices = 10;
inline constexpr uint32_t kReconcileSliceMs = 100;

// Waits up to slices * sliceMs; returns the slices actually slept.
template <typename Running, typename Generation, typename SleepFn>
int WaitReconcileTick(uint32_t generationAtTickStart, Running running, Generation generationNow, SleepFn sleep,
                      int slices = kReconcileSlices, uint32_t sliceMs = kReconcileSliceMs) {
  int slept = 0;
  while (slept < slices && running()) {
    if (generationNow() != generationAtTickStart) break;  // a change since this tick began: reconcile now
    sleep(sliceMs);
    ++slept;
  }
  return slept;
}

}  // namespace remote60::native_poc::prelogin
