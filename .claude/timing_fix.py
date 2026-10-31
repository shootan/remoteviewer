import io
p='apps/native_poc/src/update_handoff_process_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  DWORD workerExit = 0xFFFFFFFF;
  bool workerFinished = false;
};'''
new='''  DWORD workerExit = 0xFFFFFFFF;
  bool workerFinished = false;
  /**
   * How long the WAIT took, not how long the attempt took.
   *
   * The two are different and the difference matters: after the wait returns, this test keeps
   * polling the witness so it can say what the worker decided. A timing assertion over the whole
   * attempt measures that polling -- which is the test's own patience, not the product's.
   */
  DWORD waitMs = 0;
};'''
assert old in d,'Attempt'
d=d.replace(old,new,1)

old='''    a.step = await_handoff(ready, ack, pi.hProcess, readyName, hostTimeoutMs, &a.why);'''
new='''    const DWORD waitBegan = GetTickCount();
    a.step = await_handoff(ready, ack, pi.hProcess, readyName, hostTimeoutMs, &a.why);
    a.waitMs = GetTickCount() - waitBegan;'''
assert old in d,'await call'
d=d.replace(old,new,1)

old='''      if (text.find("worker WOULD stop") != std::string::npos ||
          text.find("worker stood down") != std::string::npos) {'''
new='''      if (text.find("worker WOULD stop") != std::string::npos ||
          text.find("worker stood down") != std::string::npos ||
          text.find("died before signalling") != std::string::npos) {'''
assert old in d,'poll'
d=d.replace(old,new,1)

old='''    const DWORD before = GetTickCount();
    const Attempt a = run(/*bootstrapExit=*/3, /*delayMs=*/0, /*signal=*/false,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000);
    const DWORD took = GetTickCount() - before;
    check("2 bootstrap failure: the host stops waiting", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("2 bootstrap failure: and does not sit out the whole timeout", took < 15000,
          std::to_string(took) + "ms");'''
new='''    const Attempt a = run(/*bootstrapExit=*/3, /*delayMs=*/0, /*signal=*/false,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000);
    check("2 bootstrap failure: the host stops waiting", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("2 bootstrap failure: and does not sit out the whole timeout", a.waitMs < 15000,
          std::to_string(a.waitMs) + "ms");'''
assert old in d,'case 2 timing'
d=d.replace(old,new,1)

old='''    const DWORD before = GetTickCount();
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/true);
    const DWORD took = GetTickCount() - before;
    check("4b worker dies: the host keeps running", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("4b worker dies: it is noticed rather than waited out", took < 15000,
          std::to_string(took) + "ms");'''
new='''    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/true);
    check("4b worker dies: the host keeps running", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    // The wait itself, against a 20s deadline. Well under it means the death was noticed rather
    // than waited out -- which is the whole reason the working copy holds a liveness handle.
    check("4b worker dies: it is noticed rather than waited out", a.waitMs < 5000,
          std::to_string(a.waitMs) + "ms of a 20000ms deadline");'''
assert old in d,'case 4b timing'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the timing assertions measure the wait')
