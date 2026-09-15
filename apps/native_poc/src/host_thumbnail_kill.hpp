#pragma once

// Ending a capture helper, and finding out whether it actually ended.
//
// Role:    one call -- terminate the process and report whether its exit was confirmed.
// Thread:  called from whichever thread is tearing an attempt down; touches only its argument.
// Callers: host_thumbnail_helper.cpp.
//
// This is a separate translation unit for one reason: it is the only place the teardown talks to
// the OS, and the branch that matters is the one where the OS says no. That branch cannot be
// reached on demand -- TerminateProcess does not fail for a process we started -- so the way to
// exercise it is to link a different implementation into a test target.
//
// Link-time substitution rather than a runtime switch, deliberately. A flag or an environment
// variable would mean the shipping binary contains a way to make its own kill fail, and then the
// interesting question becomes whether that way can be reached in production. There is no such
// way: the product links host_thumbnail_kill.cpp, the lingering test links a different file, and
// shipped_helper_gate_test checks that the shipped executables carry no trace of the second one.

#include <windows.h>

namespace remote60::native_poc {

/**
 * Terminate `process` and wait up to `timeoutMs` for it to be gone.
 *
 * Returns true only when the exit was observed. False means the process may still be running --
 * which the caller has to treat as "still out there" rather than "probably fine".
 *
 * A process that has already exited is confirmed without being terminated again.
 */
bool terminate_and_confirm(HANDLE process, DWORD timeoutMs);

}  // namespace remote60::native_poc
