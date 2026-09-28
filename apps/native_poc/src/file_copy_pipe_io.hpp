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
// r3: who dies when a cancellation does not complete. A cancelled I/O is not a finished one; until
// the OS reports it complete, its OVERLAPPED and its buffer must stay alive. r2 handled that by
// terminating the process -- right for the helper, which is an isolated process whose death is
// its contract, and wrong for the host, where the same header will one day run inside
// GNLinkStream and a file-copy hiccup must not end a remote session. So the storage of every
// pending operation is heap-owned (PendingIo), and what happens to a stuck one is a POLICY:
//
//   StuckIoPolicy::OrphanAndFail  (host)   the storage is leaked on purpose -- never freed, so the
//                                          OS can still write into it -- the operation fails, the
//                                          link is closed by the caller, and a counter bounds how
//                                          many such orphans a process may accumulate before the
//                                          I/O layer refuses to start new ones.
//   StuckIoPolicy::TerminateProcess (helper) terminate_with_diagnostic, as before.
//
// A send that cannot finish within its bound has possibly left a partial frame on the wire; the
// caller then treats the link as dead. Every wait is bounded.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "bounded_process_exit.hpp"
#include "file_copy_pipe.hpp"

namespace remote60::native_poc::file_copy {

enum class StuckIoPolicy : uint8_t {
  OrphanAndFail = 0,  // the host: leak the storage, fail the operation, count it
  TerminateProcess,   // the helper: an isolated process ends itself
};

/**
 * The bound on what this process may leave to the OS: orphans (pending I/O whose cancellation
 * never completed, leaked for ever) plus operations still in flight, which may yet become
 * orphans. At the bound no new operation is started at all.
 */
constexpr uint32_t kMaxOrphanedIo = 16;

namespace detail {

/** Telemetry: how many operations became orphans (each keeps its reservation below). */
inline std::atomic<uint32_t>& orphan_counter() {
  static std::atomic<uint32_t> n{0};
  return n;
}

/**
 * Admission (r4). Every operation -- a read, a write, a connection wait -- takes one reservation
 * BEFORE it makes any OS call (no event, no ReadFile/WriteFile, no ConnectNamedPipe without one)
 * and gives it back once the OS is done with its storage; an orphan keeps its reservation for
 * ever. Taken with a compare-and-swap, so however many threads start I/O at once the number of
 * orphans can never exceed kMaxOrphanedIo, and at the bound every new operation fails with
 * ERROR_NO_SYSTEM_RESOURCES instead of adding storage. A link that keeps timing out and getting
 * stuck therefore costs at most kMaxOrphanedIo leaked operations per process, connections
 * included -- it used to be able to pile those up without end (Codex r3 review).
 */
inline std::atomic<uint32_t>& io_reservations() {
  static std::atomic<uint32_t> n{0};
  return n;
}
inline bool reserve_io() {
  uint32_t current = io_reservations().load();
  for (;;) {
    if (current >= kMaxOrphanedIo) return false;
    if (io_reservations().compare_exchange_weak(current, current + 1)) return true;
  }
}
inline void release_io() { io_reservations().fetch_sub(1); }
/** Frees the reservation on every exit but the one where the operation became an orphan. */
struct IoReservation {
  bool held = true;
  ~IoReservation() {
    if (held) release_io();
  }
};

/** How long a cancellation is given to complete. */
inline DWORD& cancel_wait_ms() {
  static DWORD ms = 2000;
  return ms;
}

/**
 * Test seam: makes every cancellation look as if it never completed. A real pipe cancellation
 * completes within microseconds, so the stuck path cannot be reached by timing; this injects the
 * OS outcome so the bookkeeping (leak, count, refuse, survive) can be driven deterministically.
 */
inline bool& simulate_stuck_cancel() {
  static bool on = false;
  return on;
}

/**
 * The storage of one overlapped operation. Heap-owned so that, when its cancellation does not
 * complete in time, it can be released to the OS for ever (leaked) instead of freed under it.
 * `storage` receives the caller's buffer in that case.
 */
struct PendingIo {
  OVERLAPPED io{};
  std::vector<uint8_t> storage;
};

/**
 * Waits for a pending operation, cancelling it at the bound. Returns true when the operation is
 * COMPLETE (successfully or not -- `completedOk` says which, `moved` how much); false when it is
 * stuck: cancellation was requested but never observed, and under OrphanAndFail the PendingIo
 * has been leaked (the caller's buffer moved into it) and counted. `abortedBy` names why the wait
 * ended early: 0 = the operation finished, WAIT_TIMEOUT, or ERROR_OPERATION_ABORTED (abort).
 */
inline bool settle_pending(HANDLE handle, std::unique_ptr<PendingIo>& pending, DWORD timeoutMs, HANDLE abort,
                           StuckIoPolicy policy, std::vector<uint8_t>* callerBuffer, bool* completedOk,
                           DWORD* moved, DWORD* completionError, DWORD* abortedBy) {
  *completedOk = false;
  *moved = 0;
  *completionError = ERROR_SUCCESS;
  *abortedBy = 0;
  HANDLE waits[2] = {pending->io.hEvent, abort};
  const DWORD w = WaitForMultipleObjects(abort ? 2 : 1, waits, FALSE, timeoutMs);
  if (w != WAIT_OBJECT_0) {
    *abortedBy = (w == WAIT_OBJECT_0 + 1) ? ERROR_OPERATION_ABORTED : WAIT_TIMEOUT;
    CancelIoEx(handle, &pending->io);
    if (simulate_stuck_cancel() || WaitForSingleObject(pending->io.hEvent, cancel_wait_ms()) != WAIT_OBJECT_0) {
      if (policy == StuckIoPolicy::TerminateProcess) {
        const char text[] = "[file-copy-pipe] cancellation stuck; terminating before releasing pending I/O storage\n";
        terminate_with_diagnostic(47, text, sizeof(text) - 1);
      }
      // The OS may still touch this storage: it is never freed. The event handle goes with it.
      if (callerBuffer) pending->storage = std::move(*callerBuffer);
      (void)pending.release();
      orphan_counter().fetch_add(1);
      return false;
    }
  }
  DWORD n = 0;
  if (GetOverlappedResult(handle, &pending->io, &n, FALSE)) {
    *completedOk = true;
    *moved = n;
  } else {
    *completionError = GetLastError();
  }
  return true;
}

/**
 * One bounded ReadFile / WriteFile of up to `size` bytes at `storage[offset]`. True when `*moved`
 * > 0. On a timeout or an abort the I/O is cancelled -- and if it had completed just before, its
 * bytes are still returned as moved. False sets the last error: WAIT_TIMEOUT, ERROR_OPERATION_ABORTED
 * (abort signalled), ERROR_BROKEN_PIPE (0 bytes = the other end is gone), ERROR_IO_INCOMPLETE (the
 * cancellation never completed: the storage was orphaned and `storage` is now empty),
 * ERROR_NO_SYSTEM_RESOURCES (too many orphans: no new I/O is started), or the API's own error.
 */
template <bool kWrite>
bool pipe_io_once(HANDLE pipe, std::vector<uint8_t>& storage, size_t offset, DWORD size, DWORD timeoutMs, HANDLE abort,
                  DWORD* moved, StuckIoPolicy policy) {
  *moved = 0;
  if (!reserve_io()) {
    SetLastError(ERROR_NO_SYSTEM_RESOURCES);
    return false;
  }
  IoReservation reservation;
  auto pending = std::make_unique<PendingIo>();
  pending->io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!pending->io.hEvent) return false;
  DWORD n = 0;
  BOOL started;
  void* target = storage.data() + offset;
  if (kWrite) {
    started = WriteFile(pipe, target, size, &n, &pending->io);
  } else {
    started = ReadFile(pipe, target, size, &n, &pending->io);
  }
  if (!started) {
    const DWORD error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      CloseHandle(pending->io.hEvent);
      SetLastError(error);
      return false;
    }
    bool completedOk = false;
    DWORD completionError = 0, abortedBy = 0;
    if (!settle_pending(pipe, pending, timeoutMs, abort, policy, &storage, &completedOk, &n, &completionError, &abortedBy)) {
      reservation.held = false;  // the orphan keeps it: this is what the bound counts
      SetLastError(ERROR_IO_INCOMPLETE);
      return false;  // orphaned: pending is gone, storage is gone
    }
    if (!completedOk) {
      CloseHandle(pending->io.hEvent);
      // A cancelled I/O moved nothing; anything else is the pipe failing.
      SetLastError(completionError == ERROR_OPERATION_ABORTED && abortedBy != 0 ? abortedBy : completionError);
      return false;
    }
  }
  CloseHandle(pending->io.hEvent);
  if (n == 0) {
    SetLastError(ERROR_BROKEN_PIPE);  // a zero-byte completion on a pipe: the other end closed it
    return false;
  }
  *moved = n;
  return true;
}

}  // namespace detail

inline uint32_t orphaned_io_count() { return detail::orphan_counter().load(); }
/** Orphans + operations in flight: what counts against kMaxOrphanedIo right now. */
inline uint32_t io_reserved_count() { return detail::io_reservations().load(); }
/** Test seam: how long a cancellation may take before the stuck policy applies (default 2000 ms). */
inline void set_cancel_wait_for_test(DWORD ms) { detail::cancel_wait_ms() = ms; }

/**
 * Receives frames from one pipe, keeping the bytes of an unfinished frame between calls.
 * One reader per pipe handle, used by one thread.
 */
class FrameReader {
 public:
  explicit FrameReader(StuckIoPolicy policy = StuckIoPolicy::OrphanAndFail) : policy_(policy) {}

  /**
   * The next frame, within `timeoutMs`. False with GetLastError() == WAIT_TIMEOUT when it is not
   * complete yet (call again: nothing is lost), ERROR_OPERATION_ABORTED when `abort` was signalled,
   * ERROR_INVALID_DATA when the bytes are not a frame (the stream can no longer be trusted),
   * ERROR_IO_INCOMPLETE when a cancellation never completed (the buffer was orphaned; this reader
   * is finished and the pipe must be closed), or the pipe's own error (the other end is gone).
   */
  bool Receive(HANDLE pipe, PipeFrame* frame, DWORD timeoutMs, HANDLE abort = nullptr) {
    if (orphaned_) {
      SetLastError(ERROR_IO_INCOMPLETE);
      return false;
    }
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
        if (!detail::pipe_io_once<false>(pipe, buf_, have_, static_cast<DWORD>(need - have_),
                                         static_cast<DWORD>(deadline - now), abort, &moved, policy_)) {
          const DWORD error = GetLastError();
          if (error == ERROR_IO_INCOMPLETE) {
            // The buffer now belongs to the orphan; this reader has nothing left to resume.
            orphaned_ = true;
            buf_ = std::vector<uint8_t>();
            have_ = 0;
            headerDone_ = false;
            SetLastError(ERROR_IO_INCOMPLETE);
          }
          return false;  // what arrived so far is kept (unless orphaned)
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
  bool orphaned() const { return orphaned_; }

 private:
  StuckIoPolicy policy_;
  std::vector<uint8_t> buf_;
  size_t have_ = 0;
  bool headerDone_ = false;
  bool orphaned_ = false;
  PipeMsg type_ = PipeMsg::Hello;
  uint32_t length_ = 0;
};

/**
 * Writes one frame within `timeoutMs`. False on a bound violation (ERROR_INVALID_DATA), a timeout
 * (WAIT_TIMEOUT), an abort, a broken pipe, or an orphaned cancellation (ERROR_IO_INCOMPLETE). After
 * a false the frame may be partly on the wire: the caller must not send on this pipe again.
 */
inline bool pipe_send_frame(HANDLE pipe, const PipeFrame& frame, DWORD timeoutMs, HANDLE abort = nullptr,
                            StuckIoPolicy policy = StuckIoPolicy::OrphanAndFail) {
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
    if (!detail::pipe_io_once<true>(pipe, wire, done, static_cast<DWORD>(wire.size() - done),
                                    static_cast<DWORD>(deadline - now), abort, &moved, policy)) {
      return false;
    }
    done += moved;
  }
  return true;
}

}  // namespace remote60::native_poc::file_copy
