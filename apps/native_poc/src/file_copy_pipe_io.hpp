#pragma once

// Framed, bounded I/O of file_copy_pipe.hpp frames over an overlapped named pipe handle. Used by
// both ends (the host's HelperLink and the helper's pipe threads). Header-only, Win32.
//
// r2: the first version cancelled a timed-out read and DROPPED whatever that read had moved before
// the cancellation took effect. A byte-mode pipe read completes as soon as any bytes arrive, so
// when a frame landed in the microseconds between the wait timing out and CancelIoEx, its first
// bytes were consumed and discarded and the stream was left mid-frame: every later header was
// garbage, and the peer's PasteBegin / PasteEnd simply never "arrived" (the intermittent failures
// the verifier saw on 61092c0 under load, where that window is wide). Two things fix it:
//
//   - a cancelled I/O is asked, through GetOverlappedResult, what it moved, and those bytes count;
//   - FrameReader keeps the bytes of a partially received frame across calls, so a poll that runs
//     out of time mid-frame resumes exactly where it was instead of starting a new header.
//
// A send that cannot finish within its bound has possibly left a partial frame on the wire; the
// caller then treats the link as dead. Every wait is bounded; a timed-out operation is cancelled
// and observed before its OVERLAPPED / buffer go out of scope (the discipline of bounded_pipe_io.hpp).

#include <windows.h>

#include <cstdint>
#include <vector>

#include "bounded_process_exit.hpp"
#include "file_copy_pipe.hpp"

namespace remote60::native_poc::file_copy {

namespace detail {

/**
 * One bounded ReadFile / WriteFile of up to `size` bytes. True when `*moved` > 0. On a timeout
 * or an abort the I/O is cancelled -- and if it had completed just before, its bytes are still
 * returned as moved. False sets the last error: WAIT_TIMEOUT, ERROR_OPERATION_ABORTED (abort
 * signalled), ERROR_BROKEN_PIPE (0 bytes = the other end is gone), or the API's own error.
 */
template <bool kWrite>
bool pipe_io_once(HANDLE pipe, void* data, DWORD size, DWORD timeoutMs, HANDLE abort, DWORD* moved) {
  *moved = 0;
  OVERLAPPED io{};
  io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!io.hEvent) return false;
  DWORD n = 0;
  BOOL started;
  if (kWrite) {
    started = WriteFile(pipe, data, size, &n, &io);
  } else {
    started = ReadFile(pipe, data, size, &n, &io);
  }
  if (!started) {
    const DWORD error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      CloseHandle(io.hEvent);
      SetLastError(error);
      return false;
    }
    HANDLE waits[2] = {io.hEvent, abort};
    const DWORD w = WaitForMultipleObjects(abort ? 2 : 1, waits, FALSE, timeoutMs);
    DWORD why = ERROR_SUCCESS;
    if (w != WAIT_OBJECT_0) {
      why = (w == WAIT_OBJECT_0 + 1) ? ERROR_OPERATION_ABORTED : WAIT_TIMEOUT;
      CancelIoEx(pipe, &io);
      if (WaitForSingleObject(io.hEvent, 2000) != WAIT_OBJECT_0) {
        const char text[] = "[file-copy-pipe] cancellation stuck; terminating before releasing pending I/O storage\n";
        terminate_with_diagnostic(47, text, sizeof(text) - 1);
      }
    }
    // Whether it finished on its own or was cancelled: what did it move? A cancelled I/O that had
    // already completed reports its bytes here; one that was really cancelled reports
    // ERROR_OPERATION_ABORTED and nothing.
    if (!GetOverlappedResult(pipe, &io, &n, FALSE)) {
      const DWORD error = GetLastError();
      CloseHandle(io.hEvent);
      SetLastError(error == ERROR_OPERATION_ABORTED && why != ERROR_SUCCESS ? why : error);
      return false;
    }
  }
  CloseHandle(io.hEvent);
  if (n == 0) {
    SetLastError(ERROR_BROKEN_PIPE);  // a zero-byte completion on a pipe: the other end closed it
    return false;
  }
  *moved = n;
  return true;
}

}  // namespace detail

/**
 * Receives frames from one pipe, keeping the bytes of an unfinished frame between calls.
 * One reader per pipe handle, used by one thread.
 */
class FrameReader {
 public:
  /**
   * The next frame, within `timeoutMs`. False with GetLastError() == WAIT_TIMEOUT when it is not
   * complete yet (call again: nothing is lost), ERROR_OPERATION_ABORTED when `abort` was signalled,
   * ERROR_INVALID_DATA when the bytes are not a frame (the stream can no longer be trusted), or the
   * pipe's own error (broken pipe: the other end is gone).
   */
  bool Receive(HANDLE pipe, PipeFrame* frame, DWORD timeoutMs, HANDLE abort = nullptr) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
      const size_t need = headerDone_ ? kFrameHeaderBytes + length_ : kFrameHeaderBytes;
      if (buf_.size() < need) buf_.resize(need);
      if (have_ < need) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
          SetLastError(WAIT_TIMEOUT);
          return false;
        }
        DWORD moved = 0;
        if (!detail::pipe_io_once<false>(pipe, buf_.data() + have_, static_cast<DWORD>(need - have_),
                                         static_cast<DWORD>(deadline - now), abort, &moved)) {
          return false;  // the last error says why; what arrived so far is kept
        }
        have_ += moved;
        continue;
      }
      if (!headerDone_) {
        if (!decode_frame_header(buf_.data(), &type_, &length_)) {
          SetLastError(ERROR_INVALID_DATA);
          return false;
        }
        headerDone_ = true;
        continue;  // now the payload (possibly none)
      }
      frame->type = type_;
      frame->payload.assign(buf_.begin() + kFrameHeaderBytes, buf_.begin() + kFrameHeaderBytes + length_);
      have_ = 0;
      headerDone_ = false;
      length_ = 0;
      return true;
    }
  }

  /** Bytes of an unfinished frame held right now (diagnostics / tests). */
  size_t pending_bytes() const { return have_; }

 private:
  std::vector<uint8_t> buf_;
  size_t have_ = 0;
  bool headerDone_ = false;
  PipeMsg type_ = PipeMsg::Hello;
  uint32_t length_ = 0;
};

/**
 * Writes one frame within `timeoutMs`. False on a bound violation (ERROR_INVALID_DATA), a timeout
 * (WAIT_TIMEOUT), an abort, or a broken pipe. After a false the frame may be partly on the wire:
 * the caller must not send on this pipe again.
 */
inline bool pipe_send_frame(HANDLE pipe, const PipeFrame& frame, DWORD timeoutMs, HANDLE abort = nullptr) {
  std::vector<uint8_t> wire;
  if (!encode_frame(frame, &wire)) {
    SetLastError(ERROR_INVALID_DATA);
    return false;
  }
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  size_t done = 0;
  while (done < wire.size()) {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) {
      SetLastError(WAIT_TIMEOUT);
      return false;
    }
    DWORD moved = 0;
    if (!detail::pipe_io_once<true>(pipe, wire.data() + done, static_cast<DWORD>(wire.size() - done),
                                    static_cast<DWORD>(deadline - now), abort, &moved)) {
      return false;
    }
    done += moved;
  }
  return true;
}

}  // namespace remote60::native_poc::file_copy
