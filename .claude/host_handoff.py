import io

p = 'apps/native_poc/src/host_app_main.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  std::thread([window, readyEvent, process = pi.hProcess]() {
    HANDLE handles[2] = {readyEvent, process};
    // Long enough for a download on a slow link, short enough that a wedged updater does not keep
    // the host waiting forever. Running out is not an error -- it just means no update today.
    const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 10 * 60 * 1000);
    const bool signalled = (waited == WAIT_OBJECT_0);
    const bool exited = (waited == WAIT_OBJECT_0 + 1);
    const bool timedOut = (waited == WAIT_TIMEOUT);

    std::string why;
    const upd::HandoffVerdict verdict =
        upd::handoff_verdict(signalled, exited, timedOut, &why);
    CloseHandle(readyEvent);
    CloseHandle(process);

    auto* detail = new std::string(std::string(upd::handoff_verdict_name(verdict)) + ": " + why);
    PostMessageW(window, kUpdateHandoffDoneMessage,
                 static_cast<WPARAM>(verdict == upd::HandoffVerdict::ExitNow),
                 reinterpret_cast<LPARAM>(detail));
  }).detach();'''
new = '''  std::thread([window, readyEvent, ackEvent, bootstrap = pi.hProcess]() {
    // The deadline belongs to the whole attempt, not to each wait. The bootstrap exits almost at
    // once and the wait then resumes for the working copy; measuring from here means a slow
    // download gets the time it was promised rather than the time left after a handover.
    const DWORD deadline = GetTickCount() + 10 * 60 * 1000;
    std::string why;
    upd::HandoffStep step = upd::HandoffStep::KeepWaiting;
    HANDLE bootstrapHandle = bootstrap;

    while (step == upd::HandoffStep::KeepWaiting) {
      const DWORD now = GetTickCount();
      const DWORD remaining = (deadline > now) ? (deadline - now) : 0;

      // Once the bootstrap has gone there is only one thing left to wait for. Continuing to pass
      // a signalled handle would spin.
      HANDLE handles[2] = {readyEvent, bootstrapHandle};
      const DWORD count = bootstrapHandle ? 2 : 1;
      const DWORD waited = WaitForMultipleObjects(count, handles, FALSE, remaining);

      const bool signalled = (waited == WAIT_OBJECT_0);
      const bool exited = (count == 2 && waited == WAIT_OBJECT_0 + 1);
      const bool timedOut = (waited == WAIT_TIMEOUT);

      DWORD exitCode = 0;
      if (exited) {
        // Read BEFORE the handle is closed, and read at all -- which is the whole point. The
        // bootstrap exiting is the normal path, not a death, and telling those apart is what its
        // exit code is for.
        if (!GetExitCodeProcess(bootstrapHandle, &exitCode)) exitCode = 1;
      }

      step = upd::handoff_step(signalled, exited, exitCode, timedOut, &why);
      if (exited) {
        CloseHandle(bootstrapHandle);
        bootstrapHandle = nullptr;
      }
    }

    // The answer, and it is sent BEFORE this thread lets go of anything. An updater waiting on
    // the acknowledgement must find it here or not at all: no answer means the host is staying,
    // and an updater that gets no answer must not stop it.
    if (step == upd::HandoffStep::ExitNow && ackEvent) SetEvent(ackEvent);

    if (bootstrapHandle) CloseHandle(bootstrapHandle);
    if (ackEvent) CloseHandle(ackEvent);
    CloseHandle(readyEvent);

    auto* detail = new std::string(std::string(upd::handoff_step_name(step)) + ": " + why);
    PostMessageW(window, kUpdateHandoffDoneMessage,
                 static_cast<WPARAM>(step == upd::HandoffStep::ExitNow),
                 reinterpret_cast<LPARAM>(detail));
  }).detach();'''
assert old in d, 'host waiter'
d = d.replace(old, new, 1)

old = '''  HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, spec.readyEventName.c_str());'''
new = '''  // Both events, made together and named from the same stem, so one attempt cannot end up
  // answering on another's channel. The acknowledgement exists because signalling ready tells the
  // host something; it does not tell the updater that anyone heard.
  const std::wstring ackEventName = upd::make_ack_event_name(spec.readyEventName);
  HANDLE ackEvent = CreateEventW(nullptr, TRUE, FALSE, ackEventName.c_str());
  HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, spec.readyEventName.c_str());'''
assert old in d, 'ready event create'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the host waits for the worker and answers it')
