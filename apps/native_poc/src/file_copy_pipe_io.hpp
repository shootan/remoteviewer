#pragma once

// Framed, bounded I/O of file_copy_pipe.hpp frames over an overlapped named pipe handle. Used by
// both ends (the host's HelperLink and the helper's pipe threads). Header-only, Win32.
//
// Every wait is bounded, and a timed-out operation is cancelled and observed before its
// OVERLAPPED / buffer go out of scope (the same discipline as bounded_pipe_io.hpp). A short read
// or a broken pipe is a failure the caller treats as "the other side is gone".

#include <windows.h>

#include <cstdint>
#include <vector>

#include "bounded_process_exit.hpp"
#include "file_copy_pipe.hpp"

namespace remote60::native_poc::file_copy {

namespace detail {

// One overlapped transfer of exactly `size` bytes (WriteFile or ReadFile), bounded by `timeoutMs`.
// `abort` (optional, manual-reset) ends the wait early: the caller is shutting down.
template <bool kWrite>
bool pipe_transfer_exact(HANDLE pipe, void* data, DWORD size, DWORD timeoutMs, HANDLE abort) {
  auto* cursor = static_cast<uint8_t*>(data);
  DWORD done = 0;
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  while (done < size) {
    OVERLAPPED io{};
    io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!io.hEvent) return false;
    DWORD moved = 0;
    BOOL started;
    if (kWrite) {
      started = WriteFile(pipe, cursor + done, size - done, &moved, &io);
    } else {
      started = ReadFile(pipe, cursor + done, size - done, &moved, &io);
    }
    const DWORD error = started ? ERROR_SUCCESS : GetLastError();
    bool ok = started != FALSE;
    if (!started && error == ERROR_IO_PENDING) {
      const ULONGLONG now = GetTickCount64();
      const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
      HANDLE waits[2] = {io.hEvent, abort};
      const DWORD count = abort ? 2 : 1;
      const DWORD w = WaitForMultipleObjects(count, waits, FALSE, remaining);
      if (w == WAIT_OBJECT_0) {
        ok = GetOverlappedResult(pipe, &io, &moved, FALSE) != FALSE;
      } else {
        CancelIoEx(pipe, &io);
        if (WaitForSingleObject(io.hEvent, 2000) != WAIT_OBJECT_0) {
          const char text[] = "[file-copy-pipe] cancellation stuck; terminating before releasing pending I/O storage\n";
          terminate_with_diagnostic(47, text, sizeof(text) - 1);
        }
        ok = false;
        CloseHandle(io.hEvent);
        // Distinguishable by the caller: a timeout is not a broken pipe.
        SetLastError(w == WAIT_OBJECT_0 + 1 ? ERROR_OPERATION_ABORTED : WAIT_TIMEOUT);
        return false;
      }
    }
    CloseHandle(io.hEvent);
    if (!ok || moved == 0) {
      if (ok) SetLastError(ERROR_BROKEN_PIPE);
      return false;
    }
    done += moved;
  }
  return true;
}

}  // namespace detail

/** Writes one frame. False on a bound violation, a timeout, or a broken pipe. */
inline bool pipe_send_frame(HANDLE pipe, const PipeFrame& frame, DWORD timeoutMs, HANDLE abort = nullptr) {
  std::vector<uint8_t> wire;
  if (!encode_frame(frame, &wire)) return false;
  return detail::pipe_transfer_exact<true>(pipe, wire.data(), static_cast<DWORD>(wire.size()), timeoutMs, abort);
}

/**
 * Reads one frame: the header, then exactly its payload. `timeoutMs` bounds the wait for the
 * header; once a header has arrived the payload is given `payloadTimeoutMs` on its own, so a
 * peer that sends a header and stalls cannot hold the reader for ever.
 */
inline bool pipe_receive_frame(HANDLE pipe, PipeFrame* frame, DWORD timeoutMs, HANDLE abort = nullptr,
                               DWORD payloadTimeoutMs = 5000) {
  uint8_t header[kFrameHeaderBytes];
  if (!detail::pipe_transfer_exact<false>(pipe, header, sizeof(header), timeoutMs, abort)) return false;
  uint32_t length = 0;
  if (!decode_frame_header(header, &frame->type, &length)) {
    SetLastError(ERROR_INVALID_DATA);  // the stream is no longer framed: the caller drops it
    return false;
  }
  frame->payload.resize(length);
  if (length == 0) return true;
  return detail::pipe_transfer_exact<false>(pipe, frame->payload.data(), length, payloadTimeoutMs, abort);
}

}  // namespace remote60::native_poc::file_copy
