import io

# ================================================================== fixture: open early or late
p = 'apps/native_poc/src/handoff_fixture_main.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  const std::wstring readyName = value_of(args, L"--ready");
  bool delivered = false;'''
new = '''  const std::wstring readyName = value_of(args, L"--ready");
  const std::wstring ackName = make_ack_event_name(readyName);

  // The channel is opened BEFORE the signal, as the updater does. The caller closes its own
  // handle the moment it has answered, so a handle opened later can find nothing there -- the
  // named object goes with the last handle. Holding one from before the signal keeps it alive
  // across the window where both sides are letting go.
  //
  // --open-ack-late is that window, made deliberate: it is what the code did before, and it is
  // here so the difference can be measured rather than argued about.
  const bool openLate = has_flag(args, L"--open-ack-late");
  HANDLE ack = nullptr;
  if (!openLate && !ackName.empty()) ack = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
  record(witness, ack ? "worker holds the ack channel before signalling"
                      : (openLate ? "worker deferred opening the ack channel"
                                  : "worker could not open the ack channel"));

  bool delivered = false;'''
assert old in d, 'worker ready'
d = d.replace(old, new, 1)

old = '''  const std::wstring ackWait = value_of(args, L"--ack-wait");
  const DWORD ackTimeout = ackWait.empty() ? 5000 : static_cast<DWORD>(_wtoi(ackWait.c_str()));
  bool acked = false;
  const std::wstring ackName = make_ack_event_name(readyName);
  if (!ackName.empty()) {
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    if (event) {
      acked = WaitForSingleObject(event, ackTimeout) == WAIT_OBJECT_0;
      CloseHandle(event);
    }
  }'''
new = '''  const std::wstring ackWait = value_of(args, L"--ack-wait");
  const DWORD ackTimeout = ackWait.empty() ? 5000 : static_cast<DWORD>(_wtoi(ackWait.c_str()));

  // Long enough, when asked for, that the caller has certainly answered and let go by now.
  const std::wstring openDelay = value_of(args, L"--ack-open-delay");
  if (!openDelay.empty()) Sleep(static_cast<DWORD>(_wtoi(openDelay.c_str())));
  if (openLate && !ackName.empty()) {
    ack = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    record(witness, ack ? "worker opened the ack channel late and found it"
                        : "worker opened the ack channel late and it was gone");
  }

  bool acked = false;
  if (ack) {
    acked = WaitForSingleObject(ack, ackTimeout) == WAIT_OBJECT_0;
    CloseHandle(ack);
    ack = nullptr;
  }'''
assert old in d, 'ack wait'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)

# ================================================================== the process test
p = 'apps/native_poc/src/update_handoff_process_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  const auto run = [&](int bootstrapExit, int delayMs, bool signal, DWORD hostTimeoutMs,
                       int ackWaitMs, bool dieEarly = false) {'''
new = '''  const auto run = [&](int bootstrapExit, int delayMs, bool signal, DWORD hostTimeoutMs,
                       int ackWaitMs, bool dieEarly = false, bool openAckLate = false,
                       int ackOpenDelayMs = 0, bool withoutAckChannel = false) {'''
assert old in d, 'run sig'
d = d.replace(old, new, 1)

old = '''    if (dieEarly) command += L" --die-before-signal";'''
new = '''    if (dieEarly) command += L" --die-before-signal";
    if (openAckLate) command += L" --open-ack-late";
    if (ackOpenDelayMs > 0) command += L" --ack-open-delay " + std::to_wstring(ackOpenDelayMs);'''
assert old in d, 'command'
d = d.replace(old, new, 1)

old = '''    const DWORD waitBegan = GetTickCount();
    a.step = await_handoff(ready, ack, pi.hProcess, readyName, hostTimeoutMs, nullptr, &a.why);
    a.waitMs = GetTickCount() - waitBegan;'''
new = '''    const DWORD waitBegan = GetTickCount();
    // `withoutAckChannel` hands the wait no channel at all -- the caller cannot answer. It must
    // not agree to exit on that basis.
    a.step = await_handoff(ready, withoutAckChannel ? nullptr : ack, pi.hProcess, readyName,
                           hostTimeoutMs, nullptr, &a.why);
    a.waitMs = GetTickCount() - waitBegan;

    // Released IMMEDIATELY, the way the host releases them -- before the worker has necessarily
    // got to its side of the handshake. That is the window; leaving them open until the end of
    // the attempt would close it and the test would prove nothing.
    if (ready) CloseHandle(ready);
    if (ack) CloseHandle(ack);
    ready = nullptr;
    ack = nullptr;'''
assert old in d, 'await call'
d = d.replace(old, new, 1)

old = '''    a.witness = read_text(witness);
    if (ready) CloseHandle(ready);
    if (ack) CloseHandle(ack);
    return a;'''
new = '''    a.witness = read_text(witness);
    return a;'''
assert old in d, 'run tail'
d = d.replace(old, new, 1)

old = '''  // ---------------------------------------------------------------- 5. the race, both directions'''
new = '''  // ---------------------------------------------------------------- 6. the acknowledgement window
  {
    // The channel is held from before the signal. The host answers and lets go of its handle at
    // once -- as it does -- and the worker must still find the answer, because its own handle has
    // been keeping the object alive the whole time.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/5000, /*dieEarly=*/false,
                          /*openAckLate=*/false, /*ackOpenDelayMs=*/600);
    check("6 ack held early: the host agrees to exit", a.step == HandoffStep::ExitNow,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 ack held early: the worker held the channel before signalling",
          a.witness.find("holds the ack channel before signalling") != std::string::npos,
          a.witness);
    check("6 ack held early: and the answer was still there after the host let go",
          a.witness.find("worker WOULD stop the product") != std::string::npos, a.witness);
  }
  {
    // The counter-example, and it is the defect: open the channel only when the answer is wanted.
    // By then the host has answered and closed, the named object is gone with the last handle,
    // and the answer that WAS delivered cannot be found. The update stands down having already
    // told the host to leave.
    //
    // A test on the return value of the wait would not see this. The host returned ExitNow and
    // was right to; the handle stopped existing.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/2000, /*dieEarly=*/false,
                          /*openAckLate=*/true, /*ackOpenDelayMs=*/600);
    check("6 counter-example: the host still agreed to exit", a.step == HandoffStep::ExitNow,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 counter-example: opening late finds nothing",
          a.witness.find("late and it was gone") != std::string::npos, a.witness);
    check("6 counter-example: so the worker stands down -- which is why it opens early",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }
  {
    // No channel at all on the caller's side. It cannot answer, so it must not leave: the updater
    // is waiting to be told it may go ahead, and a caller that departs unable to say so leaves it
    // holding a product it is not allowed to stop.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/false,
                          /*openAckLate=*/false, /*ackOpenDelayMs=*/0,
                          /*withoutAckChannel=*/true);
    check("6 no channel: the host does NOT agree to exit", a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 no channel: and says the acknowledgement could not be delivered",
          a.why.find("could not be delivered") != std::string::npos, a.why);
    check("6 no channel: the worker stands down too",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }

  // ---------------------------------------------------------------- 5. the race, both directions'''
assert old in d, 'race anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('the ack window has a case, a counter-example and a no-channel case')
