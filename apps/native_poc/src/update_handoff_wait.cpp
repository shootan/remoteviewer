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

HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap, uint32_t timeoutMs,
                          std::string* detail) {
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

  while (step == HandoffStep::KeepWaiting) {
    const DWORD now = GetTickCount();
    const DWORD remaining = (deadline > now) ? (deadline - now) : 0;

    // Once the bootstrap has gone there is one thing left to wait for. Passing a signalled handle
    // again would return immediately, every time, forever.
    HANDLE handles[2] = {ready, boot};
    const DWORD count = boot ? 2 : 1;
    const DWORD waited = WaitForMultipleObjects(count, handles, FALSE, remaining);

    const bool signalled = (waited == WAIT_OBJECT_0);
    const bool exited = (count == 2 && waited == WAIT_OBJECT_0 + 1);
    const bool timedOut = (waited == WAIT_TIMEOUT);

    DWORD exitCode = 0;
    if (exited && !GetExitCodeProcess(boot, &exitCode)) {
      // Unreadable is not "fine". Treated as a failure so that an unknown exit cannot be mistaken
      // for the deliberate handover, which is the one exit code that means "keep waiting".
      exitCode = 1;
    }

    step = handoff_step(signalled, exited, exitCode, timedOut, &why);
    if (exited) boot = nullptr;  // the caller owns the handle; this only stops re-waiting on it
  }

  // The answer, and it goes out before this returns. An updater waiting to be acknowledged finds
  // it here or not at all -- and not at all means the caller is staying, which means the updater
  // must not stop it.
  if (step == HandoffStep::ExitNow && ack) SetEvent(ack);

  if (detail) *detail = why;
  return step;
}

}  // namespace remote60::native_poc::update
