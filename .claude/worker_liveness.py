import io

BS = chr(92)

# ================================================================== the name
p = 'apps/native_poc/src/update_handoff.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''std::wstring make_ack_event_name(const std::wstring& readyEventName);'''
new = '''std::wstring make_ack_event_name(const std::wstring& readyEventName);

/**
 * The name of the mutex the working copy holds for as long as it is alive.
 *
 * A mutex rather than an event, because the property wanted is "this process still exists" and
 * that is exactly what a mutex handle gives for free: when the holder dies, the wait is released
 * -- abandoned, but released. Nothing has to remember to signal anything on the way out, which
 * matters because the interesting death is the one that does not get to run any cleanup.
 *
 * The BOOTSTRAP must not hold it. It exits immediately by design, and a name held by the process
 * whose exit means nothing would say the worker had died every single time.
 */
std::wstring make_alive_mutex_name(const std::wstring& readyEventName);'''
assert old in d, 'ack name decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_handoff.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''const char* handoff_step_name(HandoffStep step) {'''
new = '''std::wstring make_alive_mutex_name(const std::wstring& readyEventName) {
  if (readyEventName.empty()) return {};
  return readyEventName + L".alive";
}

const char* handoff_step_name(HandoffStep step) {'''
assert old in d, 'step name def'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== the wait watches the worker
p = 'apps/native_poc/src/update_handoff_wait.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  const DWORD deadline = GetTickCount() + timeoutMs;
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
  }'''
new = '''  const DWORD deadline = GetTickCount() + timeoutMs;
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

    step = handoff_step(signalled, bootExited, exitCode, outOfTime, &why);
    if (bootExited) {
      boot = nullptr;  // the caller owns the handle; this only stops re-waiting on it
    }
    if (step == HandoffStep::KeepWaiting && !boot && !alive) {
      // Opened after the handover, because that is when it exists. Absent is not yet a verdict:
      // the worker may be moments from starting, and the deadline is what decides in the end.
      const std::wstring aliveName = make_alive_mutex_name(readyName);
      if (!aliveName.empty()) {
        alive = OpenMutexW(SYNCHRONIZE, FALSE, aliveName.c_str());
        if (alive) lookedForWorker = true;
      }
    }
  }

  if (alive) CloseHandle(alive);'''
assert old in d, 'wait loop'
d = d.replace(old, new, 1)

old = '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap, uint32_t timeoutMs,
                          std::string* detail) {'''
new = '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs, std::string* detail) {'''
assert old in d, 'signature'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

p = 'apps/native_poc/src/update_handoff.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap, uint32_t timeoutMs,
                          std::string* detail);'''
new = '''HandoffStep await_handoff(void* readyEvent, void* ackEvent, void* bootstrap,
                          const std::wstring& readyName, uint32_t timeoutMs, std::string* detail);'''
assert old in d, 'await decl'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the wait watches the working copy, not only the bootstrap')
