import io
p='apps/native_poc/src/update_handoff_process_test.cpp'
d=io.open(p,encoding='utf-8').read()
old='''                       int ackWaitMs, bool dieEarly = false, bool openAckLate = false,
                       int ackOpenDelayMs = 0, bool withoutAckChannel = false) {'''
new='''                       int ackWaitMs, bool dieEarly = false, bool openAckLate = false,
                       int ackOpenDelayMs = 0, bool withoutAckChannel = false,
                       bool ackUnwritable = false) {'''
assert old in d,'run sig'
d=d.replace(old,new,1)

old='''    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    HANDLE ack = CreateEventW(nullptr, TRUE, FALSE, ackName.c_str());'''
new='''    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    HANDLE ack = CreateEventW(nullptr, TRUE, FALSE, ackName.c_str());
    // A handle that can be waited on but NOT set. `SetEvent` fails on it with access denied,
    // which is a different case from having no channel at all: here there is something, and the
    // answer still cannot be sent. Opening with only SYNCHRONIZE is a deterministic way to get
    // that, rather than closing a handle and relying on undefined behaviour.
    HANDLE ackOwned = nullptr;
    if (ackUnwritable && ack) {
      ackOwned = ack;
      ack = OpenEventW(SYNCHRONIZE, FALSE, ackName.c_str());
    }'''
assert old in d,'event create'
d=d.replace(old,new,1)

old='''    if (ready) CloseHandle(ready);
    if (ack) CloseHandle(ack);
    ready = nullptr;
    ack = nullptr;'''
new='''    if (ready) CloseHandle(ready);
    if (ack) CloseHandle(ack);
    if (ackOwned) CloseHandle(ackOwned);
    ready = nullptr;
    ack = nullptr;
    ackOwned = nullptr;'''
assert old in d,'close'
d=d.replace(old,new,1)

old='''    if (!started) {
      a.why = "the fixture would not start";
      if (ready) CloseHandle(ready);
      if (ack) CloseHandle(ack);
      return a;
    }'''
new='''    if (!started) {
      a.why = "the fixture would not start";
      if (ready) CloseHandle(ready);
      if (ack) CloseHandle(ack);
      if (ackOwned) CloseHandle(ackOwned);
      return a;
    }'''
assert old in d,'start fail'
d=d.replace(old,new,1)

old='''  // ---------------------------------------------------------------- 5. the race, both directions'''
new='''  {
    // The channel exists and the answer cannot be SENT -- distinct from having no channel, and
    // the distinction matters because the code path is different: one skips the send, the other
    // attempts it and is refused. Both must end the same way, with the caller staying put.
    const Attempt a = run(/*bootstrapExit=*/0, /*delayMs=*/300, /*signal=*/true,
                          /*hostTimeoutMs=*/20000, /*ackWaitMs=*/1000, /*dieEarly=*/false,
                          /*openAckLate=*/false, /*ackOpenDelayMs=*/0,
                          /*withoutAckChannel=*/false, /*ackUnwritable=*/true);
    check("6 unwritable channel: the host does NOT agree to exit",
          a.step == HandoffStep::KeepRunning,
          std::string(handoff_step_name(a.step)) + ": " + a.why);
    check("6 unwritable channel: and says the acknowledgement could not be delivered",
          a.why.find("could not be delivered") != std::string::npos, a.why);
    check("6 unwritable channel: the worker stands down rather than stopping anything",
          a.witness.find("worker stood down") != std::string::npos, a.witness);
  }

  // ---------------------------------------------------------------- 5. the race, both directions'''
assert old in d,'race anchor'
d=d.replace(old,new,1)
io.open(p,'w',encoding='utf-8',newline='').write(d)
print('SetEvent failure is its own case')
