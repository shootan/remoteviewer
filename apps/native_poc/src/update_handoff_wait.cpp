// Waiting for an update handover to reach a decision.
//
// Kept out of the host so that something other than the host can run it. It used to live inside a
// lambda in host_app_main.cpp, where the only way to exercise it was to start the product and
// perform a real update -- so it was never exercised, and it was wrong.
//
// It was wrong in a way worth writing down. The process the host starts is a BOOTSTRAP: it copies
// the updater into a working directory, starts that copy, and exits at once, because it cannot
// hold open the file the update is going to replace. The wait treated any exit as the updater
// having died. The bootstrap exits every single time, so on the ordinary path the host decided the
// update had failed while the working copy was still downloading -- told the user the installed
// version was unchanged, closed the ready event, and stopped listening. Then the copy finished,
// signalled into an event nobody held, and stopped the host it had just been promised would be
// left alone.
//
// What makes it testable is that the caller supplies the handles. The host supplies real ones; a
// test supplies real ones too, belonging to processes it started itself. Nothing here knows which
// it is, which is the point -- the code that ships is the code that runs under test.

#include <windows.h>

#include <string>

#include "update_handoff.hpp"

namespace remote60::native_poc::update {

HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs, std::string* detail) {
  HANDLE ready = static_cast<HANDLE>(readyEvent);
  HANDLE ack = static_cast<HANDLE>(ackEvent);
  HANDLE boot = static_cast<HANDLE>(bootstrap);
  if (!ready) {
    if (detail) *detail = "there is no ready event to wait on";
    return HandoffStep::KeepRunning;
  }

  // The deadline belongs to the WHOLE attempt. The bootstrap exits almost immediately and the
  // wait then resumes for the working copy; restarting the clock each time would give a slow
  // download unbounded time, and measuring only the last leg would give it almost none.
  const DWORD deadline = GetTickCount() + timeoutMs;
  HandoffStep step = HandoffStep::KeepWaiting;
  std::string why;

  // The working copy's liveness, picked up once the bootstrap has handed over.
  //
  // Watching the bootstrap alone leaves a gap: after it exits there is nothing being watched
  // except a signal that may never come, so a worker that dies mid-download costs the caller the
  // entire timeout before it learns anything. The worker holds this for its lifetime, and the
  // wait is released when it dies -- abandoned rather than signalled, but released, which is the
  // point: the interesting death is the one that runs no cleanup.
  HANDLE alive = nullptr;
  bool lookedForWorker = false;

  while (step == HandoffStep::KeepWaiting) {
    const DWORD now = GetTickCount();
    const DWORD remaining = (deadline > now) ? (deadline - now) : 0;

    // Once the bootstrap has gone there is one thing left to wait for. Passing a signalled handle
    // again would return immediately, every time, forever.
    HANDLE handles[2] = {ready, boot ? boot : alive};
    const DWORD count = (boot || alive) ? 2 : 1;
    // Capped while looking for the worker, so its arrival is noticed rather than waited past. The
    // deadline still governs -- this only shortens individual waits.
    const DWORD slice = (!boot && !alive && !lookedForWorker) ? 100 : remaining;
    const DWORD waited =
        WaitForMultipleObjects(count, handles, FALSE, slice < remaining ? slice : remaining);

    const bool signalled = (waited == WAIT_OBJECT_0);
    const bool bootExited = (boot && count == 2 && waited == WAIT_OBJECT_0 + 1);
    // A mutex released by a dying owner comes back ABANDONED. Both mean the same thing here.
    const bool workerGone =
        (!boot && alive && (waited == WAIT_OBJECT_0 + 1 || waited == WAIT_ABANDONED_0 + 1));
    const bool outOfTime = remaining == 0;

    DWORD exitCode = 0;
    if (bootExited && !GetExitCodeProcess(boot, &exitCode)) {
      // Unreadable is not "fine". Treated as a failure so that an unknown exit cannot be mistaken
      // for the deliberate handover, which is the one exit code that means "keep waiting".
      exitCode = 1;
    }

    if (workerGone) {
      // Acquired by dying, so it has to be released again -- and said plainly, because "the
      // worker died" and "it never got there in time" call for different looking-into.
      ReleaseMutex(alive);
      CloseHandle(alive);
      alive = nullptr;
      if (detail) *detail = "the working copy stopped before it had a verified download";
      return HandoffStep::KeepRunning;
    }

    // Asked ONLY when something happened, or when the time is actually up.
    //
    // The wait is now sliced -- short waits while looking for the working copy -- and a slice
    // expiring is not an event. Feeding "nothing yet" into the decision turned every 100ms of
    // ordinary waiting into a final answer of "the updater did not signal", so the very first
    // slice ended the wait. Nothing had gone wrong; the loop was reporting its own patience as a
    // failure.
    if (step == HandoffStep::KeepWaiting && !boot && !alive) {
      // Looked for on every pass, including the ones where nothing happened -- that is exactly
      // when it appears. Absent is not yet a verdict: the copy may be moments from starting, and
      // the deadline is what decides in the end.
      const std::wstring aliveName = make_alive_mutex_name(readyName);
      if (!aliveName.empty()) {
        alive = OpenMutexW(SYNCHRONIZE, FALSE, aliveName.c_str());
        if (alive) lookedForWorker = true;
      }
    }

    if (!signalled && !bootExited && !outOfTime) {
      continue;
    }

    step = handoff_step(signalled, bootExited, exitCode, outOfTime, &why);
    if (bootExited) {
      boot = nullptr;  // the caller owns the handle; this only stops re-waiting on it
    }
  }

  if (alive) CloseHandle(alive);

  // The answer, and it goes out before this returns. An updater waiting to be acknowledged finds
  // it here or not at all -- and not at all means the caller is staying, which means the updater
  // must not stop it.
  if (step == HandoffStep::ExitNow && ack) SetEvent(ack);

  if (detail) *detail = why;
  return step;
}

}  // namespace remote60::native_poc::update
