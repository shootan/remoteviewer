#pragma once

// Waiting for an isolated thumbnail capture, with a deadline and a way out.
//
// Role:    wait until the worker says it has a result, OR the worker dies, OR the deadline
//          expires, OR the session cancels -- and say which of the four happened.
// Thread:  the control thread, which is the only one that asks for thumbnails.
// Input:   the worker's completion event, its process handle, an optional cancel event, and how
//          long the caller is willing to wait.
// Output:  one of four outcomes, plus how long it actually took.
// Callers: the host control session's thumbnail path (once the worker exists).
//
// Waiting on the completion event alone would be the obvious thing and would be wrong twice.
//
// A worker that dies without producing anything -- killed during an update, crashed, terminated
// by anything at all -- never signals. The caller would then sit out the whole deadline to learn
// something the OS could have told it immediately. Waiting on the process handle as well turns
// that into an answer that arrives the moment the process ends, and it makes the CAUSE of death
// irrelevant: crash, update, or a kill from somewhere else all arrive on the same path.
//
// The order of the handles is load-bearing. A worker that writes its result, signals, and then
// exits leaves BOTH handles signalled, and WaitForMultipleObjects returns the lowest index it
// finds -- so completion has to be index zero or a successful capture would be read as a dead
// worker. There is a test for exactly that.

#include <windows.h>

#include <cstdint>

#include "host_thumbnail_budget.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

enum class ThumbnailWaitResult {
  Completed,   // the worker signalled: there is a result to read
  WorkerGone,  // the process ended without signalling -- no result, and no reason to wait longer
  TimedOut,    // the deadline expired with the worker still alive and still silent
  Canceled,    // the caller stopped caring; nothing is held against the window for this
};

struct ThumbnailWaitHandles {
  HANDLE done = nullptr;    // signalled by the worker when its result is in the mapping
  HANDLE worker = nullptr;  // the worker process
  HANDLE cancel = nullptr;  // optional; the session going away
};

/**
 * Wait for whichever comes first. `elapsedUs` is always written when non-null.
 *
 * A null `done` handle is a programming error rather than a state to wait on, and is reported as
 * WorkerGone: there is no way for a result to arrive, so pretending otherwise would spend the
 * deadline to reach the same answer.
 */
inline ThumbnailWaitResult wait_for_thumbnail(const ThumbnailWaitHandles& handles,
                                              uint64_t deadlineUs, uint64_t* elapsedUs = nullptr) {
  const uint64_t start = qpc_now_us();
  const auto finish = [&](ThumbnailWaitResult result) {
    if (elapsedUs) *elapsedUs = qpc_now_us() - start;
    return result;
  };

  if (!handles.done) return finish(ThumbnailWaitResult::WorkerGone);

  // Completion first: see the note above about both handles being signalled at once.
  HANDLE waitOn[3] = {handles.done, nullptr, nullptr};
  DWORD count = 1;
  const DWORD workerIndex = handles.worker ? count++ : 0xFFFFFFFFu;
  if (handles.worker) waitOn[workerIndex] = handles.worker;
  const DWORD cancelIndex = handles.cancel ? count++ : 0xFFFFFFFFu;
  if (handles.cancel) waitOn[cancelIndex] = handles.cancel;

  // Rounded up: a sub-millisecond deadline would otherwise round to zero and never wait at all.
  DWORD timeoutMs = static_cast<DWORD>((deadlineUs + 999) / 1000);
  if (deadlineUs > 0 && timeoutMs == 0) timeoutMs = 1;

  const DWORD signalled = WaitForMultipleObjects(count, waitOn, FALSE, timeoutMs);
  if (signalled == WAIT_OBJECT_0) return finish(ThumbnailWaitResult::Completed);
  if (handles.worker && signalled == WAIT_OBJECT_0 + workerIndex) {
    return finish(ThumbnailWaitResult::WorkerGone);
  }
  if (handles.cancel && signalled == WAIT_OBJECT_0 + cancelIndex) {
    return finish(ThumbnailWaitResult::Canceled);
  }
  if (signalled == WAIT_TIMEOUT) return finish(ThumbnailWaitResult::TimedOut);
  // WAIT_FAILED or an abandoned mutex: something is wrong with the handles themselves, and the
  // safe reading is that no result is coming. Not TimedOut -- the caller would wait again.
  return finish(ThumbnailWaitResult::WorkerGone);
}

/**
 * How a wait outcome charges against the window's retry budget.
 *
 * Completed maps to Ok because the worker said it had a result. Whether those bytes are usable is
 * the caller's question, and a caller that finds them malformed records Failed itself.
 */
inline ThumbnailOutcome outcome_for_wait(ThumbnailWaitResult result) {
  switch (result) {
    case ThumbnailWaitResult::Completed:
      return ThumbnailOutcome::Ok;
    case ThumbnailWaitResult::TimedOut:
      return ThumbnailOutcome::TimedOut;
    case ThumbnailWaitResult::Canceled:
      return ThumbnailOutcome::Canceled;
    case ThumbnailWaitResult::WorkerGone:
      break;
  }
  // A worker that died produced no pixels, and the window is as likely to be the reason as not --
  // it is charged, the same as a timeout. Only cancellation is free.
  return ThumbnailOutcome::Failed;
}

}  // namespace remote60::native_poc
