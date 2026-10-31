// A kill that never confirms. Linked ONLY by the lingering test target.
//
// The product's branch for "the helper would not die" cannot be reached honestly: TerminateProcess
// does not fail for a process this process started, so the confirmation always succeeds and the
// branch never runs. Removing it would break no test, which is the same as saying it is untested.
//
// So the test links this instead. Nothing is stubbed on the product side; only the one OS call at
// the boundary is replaced, and the real teardown runs against a helper that is genuinely still
// alive afterwards.
//
// It WAITS the full confirmation window before reporting failure, and that is the point rather
// than an oversight. A kill that is never confirmed costs the caller the whole of kKillConfirmMs --
// that is what "the worst case is about six seconds, not one" means, and an injection that
// returned instantly would have left that number as arithmetic instead of a measurement. The first
// version did return instantly and reported a 3us teardown, which measured nothing.

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
  gKeepMarker = kFaultMarker;
  (void)process;
  // Deliberately does not terminate: the point is a helper that is still running afterwards. The
  // wait is what a real unconfirmed kill costs, so it is spent here rather than skipped.
  Sleep(timeoutMs);
  return false;
}

}  // namespace remote60::native_poc
