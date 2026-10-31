import io
p='apps/native_poc/src/handoff_fixture_main.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  // ---------------------------------------------------------------- the working copy
  const std::wstring delay = value_of(args, L"--delay");'''
new='''  // ---------------------------------------------------------------- the working copy
  const std::wstring readyForAlive = value_of(args, L"--ready");
  // Held for this process's lifetime, exactly as the real working copy holds it. Whoever is
  // waiting learns of a death without this having to announce one -- which is the point, because
  // the death worth catching is the one that runs no cleanup.
  HANDLE alive = nullptr;
  {
    const std::wstring aliveName = make_alive_mutex_name(readyForAlive);
    if (!aliveName.empty()) alive = CreateMutexW(nullptr, TRUE, aliveName.c_str());
  }

  const std::wstring delay = value_of(args, L"--delay");'''
assert old in d,'worker head'
d=d.replace(old,new,1)

old='''  const std::wstring readyName = value_of(args, L"--ready");
  bool delivered = false;'''
new='''  if (has_flag(args, L"--die-before-signal")) {
    // Stops without releasing anything, the way a crash does. The mutex goes with the process and
    // the waiter sees it abandoned -- which is the whole reason it is a mutex.
    record(witness, "worker died before signalling");
    TerminateProcess(GetCurrentProcess(), 99);
  }

  const std::wstring readyName = value_of(args, L"--ready");
  bool delivered = false;'''
assert old in d,'die flag'
d=d.replace(old,new,1)

old='''  record(witness, std::string(proceed ? "worker WOULD stop the product" : "worker stood down")
                      + ": " + why);
  return proceed ? 0 : 20;'''
new='''  record(witness, std::string(proceed ? "worker WOULD stop the product" : "worker stood down")
                      + ": " + why);
  if (alive) {
    ReleaseMutex(alive);
    CloseHandle(alive);
  }
  return proceed ? 0 : 20;'''
assert old in d,'worker tail'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)

p='apps/native_poc/src/update_handoff_process_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''  const auto run = [&](int bootstrapExit, int delayMs, bool signal, DWORD hostTimeoutMs,
                       int ackWaitMs) {'''
new='''  const auto run = [&](int bootstrapExit, int delayMs, bool signal, DWORD hostTimeoutMs,
                       int ackWaitMs, bool dieEarly = false) {'''
assert old in d,'run sig'
d=d.replace(old,new,1)
old='''    if (!signal) command += L" --no-signal";'''
new='''    if (!signal) command += L" --no-signal";
    if (dieEarly) command += L" --die-before-signal";'''
assert old in d,'command'
d=d.replace(old,new,1)

old='''  // ---------------------------------------------------------------- 5. the race, both directions'''
new='''  // ---------------------------------------------------------------- 4b. the worker dies
  {
    // The download process stops. Watching only the bootstrap, this cost the caller the entire
    // timeout -- there was nothing left being watched, so "died at once" and "still going" looked
    // identical for ten minutes. The liveness the worker holds is released by its death, so the
    // caller learns promptly and, importantly, learns WHY.
    const DWORD before = GetTickCount();
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/true);
    const DWORD took = GetTickCount() - before;
    check("4b worker dies: the host keeps running", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("4b worker dies: it is noticed rather than waited out", took < 15000,
          std::to_string(took) + "ms");
    check("4b worker dies: and the reason says the working copy stopped, not that time ran out",
          a.why.find("stopped before") != std::string::npos, a.why);
  }

  // ---------------------------------------------------------------- 5. the race, both directions'''
assert old in d,'race anchor'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('the fixture can die, and the test watches for it')
