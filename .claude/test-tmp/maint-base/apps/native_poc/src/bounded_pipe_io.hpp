#pragma once
#include "bounded_process_exit.hpp"

namespace remote60::native_poc {
// The caller holds the pipe handle for the whole operation. A timeout cancels and observes the
// completion before the OVERLAPPED/event/buffer can be destroyed; uncertain writes are not replayed.
inline bool write_pipe_bounded(HANDLE pipe, const void* data, DWORD size, DWORD timeoutMs = 1000) {
  OVERLAPPED io{};
  io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!io.hEvent) return false;
  DWORD written = 0;
  const bool started = WriteFile(pipe, data, size, &written, &io) != FALSE;
  const DWORD error = started ? ERROR_SUCCESS : GetLastError();
  bool ok = started;
  if (!started && error == ERROR_IO_PENDING) {
    if (WaitForSingleObject(io.hEvent, timeoutMs) == WAIT_OBJECT_0) {
      ok = GetOverlappedResult(pipe, &io, &written, FALSE) != FALSE;
    } else {
      CancelIoEx(pipe, &io);
      if (WaitForSingleObject(io.hEvent, 1000) != WAIT_OBJECT_0) {
        const char text[] = "[pipe] cancellation stuck; terminating before releasing pending I/O storage\n";
        terminate_with_diagnostic(47, text, sizeof(text) - 1);
      }
    }
  }
  CloseHandle(io.hEvent);
  return ok && written == size;
}
}
