#pragma once

// Overlapped, STOP-latched pipe serve loop for the secure-pipe-identity UAC probe service (C4).
//
// Factored out of the service exe so a NON-ELEVATED fixture can drive every stop point without the SCM.
// STOP is a manual-reset event observed by every blocking wait (not a one-shot CancelIoEx). The loop
// honours it at four points and always returns bounded, reporting which point it exited at.

namespace remote60::native_poc {

enum class ProbeServeExit {
  StopBeforeConnect,  // (1) STOP seen at the loop top, before ConnectNamedPipe
  StopDuringConnect,  // (3) STOP won the wait while ConnectNamedPipe was pending
  StopBetween,        // (2) STOP seen after a completed connect, before the read
  StopDuringRead,     // (4) STOP won the wait while ReadFile was pending
  ConnectError,       // a real connect error (not STOP) -> stop serving, fail closed
  EventError,         // CreateEvent / GetOverlappedResult failed -> cannot serve safely
};

const char* probe_serve_exit_name(ProbeServeExit e);

// Serve on `pipeHandle` (an OVERLAPPED named-pipe server instance) until `stopEvent` is set or an
// unrecoverable error. Returns the exit reason. Does not close either handle.
ProbeServeExit ProbeServeLoop(void* pipeHandle, void* stopEvent);

}  // namespace remote60::native_poc
