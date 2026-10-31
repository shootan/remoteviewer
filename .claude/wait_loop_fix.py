import io
p='apps/native_poc/src/update_handoff_wait.cpp'
d=io.open(p,encoding='utf-8').read()
old='''    step = handoff_step(signalled, bootExited, exitCode, outOfTime, &why);
    if (bootExited) {
      boot = nullptr;  // the caller owns the handle; this only stops re-waiting on it
    }'''
new='''    // Asked ONLY when something happened, or when the time is actually up.
    //
    // The wait is now sliced -- short waits while looking for the working copy -- and a slice
    // expiring is not an event. Feeding "nothing yet" into the decision turned every 100ms of
    // ordinary waiting into a final answer of "the updater did not signal", so the very first
    // slice ended the wait. Nothing had gone wrong; the loop was reporting its own patience as a
    // failure.
    if (!signalled && !bootExited && !outOfTime) {
      continue;
    }

    step = handoff_step(signalled, bootExited, exitCode, outOfTime, &why);
    if (bootExited) {
      boot = nullptr;  // the caller owns the handle; this only stops re-waiting on it
    }'''
assert old in d,'step call'
d=d.replace(old,new,1)
# the alive lookup must still happen on a plain slice expiry, so move it before the continue
old='''    if (!signalled && !bootExited && !outOfTime) {
      continue;
    }
'''
new='''    if (step == HandoffStep::KeepWaiting && !boot && !alive) {
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
'''
d=d.replace(old,new,1)
# and drop the now-duplicated lookup at the end of the loop
old='''    if (step == HandoffStep::KeepWaiting && !boot && !alive) {
      // Opened after the handover, because that is when it exists. Absent is not yet a verdict:
      // the worker may be moments from starting, and the deadline is what decides in the end.
      const std::wstring aliveName = make_alive_mutex_name(readyName);
      if (!aliveName.empty()) {
        alive = OpenMutexW(SYNCHRONIZE, FALSE, aliveName.c_str());
        if (alive) lookedForWorker = true;
      }
    }
  }'''
assert old in d,'trailing lookup'
d=d.replace(old,'  }',1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('a slice expiring is no longer a verdict')
