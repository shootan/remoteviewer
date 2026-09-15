// A kill that never confirms. Linked ONLY by the lingering test target.
//
// The product's branch for "the helper would not die" cannot be reached honestly: TerminateProcess
// does not fail for a process this process started, so the confirmation always succeeds and the
// branch never runs. Removing it would break no test, which is the same as saying it is untested.
//
// So the test links this instead. It does not terminate anything and reports failure, which drives
// the real product code down the real path -- the helper genuinely stays alive, the handles are
// genuinely held, and the next request is genuinely refused. Nothing is stubbed on the product
// side; only the one OS call at the boundary is replaced.

#include "host_thumbnail_kill.hpp"

namespace remote60::native_poc {
namespace {

// The marker shipped_helper_gate_test looks for, to prove this file is not in a shipping binary.
//
// It has to be REFERENCED to be in the binary at all. The first version declared it at namespace
// scope and never used it, so the linker dropped it and the gate's check passed against a product
// that really had linked this file -- a check that could only ever succeed. Writing it through a
// volatile pointer is what keeps it.
const char kFaultMarker[] = "REMOTE60_KILL_FAULT_INJECTION_TU";
const char* volatile gKeepMarker = nullptr;

}  // namespace

bool terminate_and_confirm(HANDLE process, DWORD timeoutMs) {
  (void)process;
  (void)timeoutMs;
  gKeepMarker = kFaultMarker;
  // Deliberately does not terminate: the point is a helper that is still running afterwards.
  return false;
}

}  // namespace remote60::native_poc
