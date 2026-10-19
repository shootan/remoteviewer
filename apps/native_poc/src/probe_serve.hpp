#pragma once

// Overlapped, STOP-latched pipe serve loop for the secure-pipe-identity UAC probe service (C4).
//
// Factored out of the service exe so a NON-ELEVATED fixture can drive every stop point without the SCM.
// STOP is a manual-reset event observed by every blocking wait (not a one-shot CancelIoEx). The loop
// honours it at four points and always returns bounded, reporting which point it exited at.
//
// r6 (D4): an I/O the loop issued is never left behind. On EVERY way out of a pending connect or read --
// STOP, a failed wait (WAIT_FAILED), a failed completion -- the I/O is cancelled (CancelIoEx) and its
// completion collected (GetOverlappedResult, wait TRUE) before the OVERLAPPED, its event or the read buffer
// go. The failure's Win32 error is kept (taken right after the failing call) and returned, not dropped.
// The I/O event is the caller's, created BEFORE the service reports RUNNING.

#include <cstdint>
#include <functional>

namespace remote60::native_poc {

enum class ProbeServeExit {
  StopBeforeConnect,  // (1) STOP seen at the loop top, before ConnectNamedPipe
  StopDuringConnect,  // (3) STOP won the wait while ConnectNamedPipe was pending
  StopBetween,        // (2) STOP seen after a completed connect, before the read
  StopDuringRead,     // (4) STOP won the wait while ReadFile was pending
  ConnectError,       // a real connect error (not STOP) -> stop serving, fail closed (win32 kept)
  EventError,         // a wait / read completion failed -> cannot serve safely (win32 kept)
};

const char* probe_serve_exit_name(ProbeServeExit e);

struct ProbeServeResult {
  ProbeServeExit exit = ProbeServeExit::EventError;
  uint32_t win32 = 0;  // the failing call's own error (0 for a STOP exit)
};

// Tests only: where the loop is, a replacement for its waits, and a note each time a pending I/O was
// cancelled AND its completion collected. Production passes none.
enum class ProbeServePoint { BeforeConnect, ConnectPending, Between, ReadPending };
struct ProbeServeHooks {
  std::function<void(ProbeServePoint)> at;
  std::function<uint32_t(uint32_t count, void* const* handles)> wait;  // as WaitForMultipleObjects(.., INFINITE)
  std::function<void(ProbeServePoint)> reaped;
};

// Serve on `pipeHandle` (an OVERLAPPED named-pipe server instance) until `stopEvent` is set or an
// unrecoverable error, using `ioEvent` (a manual-reset event the caller created before RUNNING) for its
// overlapped I/O. Does not close any handle.
ProbeServeResult ProbeServeRun(void* pipeHandle, void* stopEvent, void* ioEvent, const ProbeServeHooks* hooks = nullptr);

}  // namespace remote60::native_poc
