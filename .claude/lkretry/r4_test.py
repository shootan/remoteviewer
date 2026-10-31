p = r'C:\work\gnlink-wt\fix-lk-retry\apps\native_poc\src\host_unlock_relay_ondemand_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
anchor = '''    h.EndSession(0x97);
    holdAt.clear();
    h.SetRetryIoHookForTest(nullptr);'''
assert s.count(anchor) == 1
new = '''    h.EndSession(0x97);
    holdAt.clear();
    wait_until([&] { return !svc.connected.load(); }, 3000);
    reprobe();

    // (8) lk-ux-retry r4 (Codex N1): the fresh challenge's write starts just before the retry's absolute end (held
    // until ~1650 ms of a 1750 ms end; caller waits 2000) and the service answers late (900 ms): the read must end
    // AT the retry's end -- not at the relative budget worked out before -- so the caller hears 17 before its own
    // wait ends, and nothing is held.
    check("N1 (8) setup: A typed and waiting", typed_wait(0x99, 99));
    ResetEvent(go);
    holdAt = "challenge";
    svc.challengeDelayMs = 900;
    ControlUnlockChallengeMessage r8{};
    bool got8 = false;
    long long ms8 = 0;
    const auto t8 = std::chrono::steady_clock::now();
    std::thread caller8([&] {
      got8 = h.LockCredRetryChallengeSync(challenge_req(100), 99, 0x99, &r8, 2000);
      ms8 = ms_since(t8);
    });
    const bool held8 = wait_until([&] { return at.load() == 1; }, 3000);
    while (ms_since(t8) < 1650) Sleep(5);
    SetEvent(go);
    caller8.join();
    svc.challengeDelayMs = 0;
    Sleep(300);
    check("N1 (8): a write started just before the retry's end waits only TO that end: the caller hears 17 in its wait",
          held8 && got8 && r8.status == static_cast<uint16_t>(UnlockStage::Superseded) && ms8 < 1950,
          "got=" + std::to_string(got8) + " status=" + std::to_string(r8.status) + " in " + std::to_string(ms8) + " ms");
    check("N1 (8): ...and nothing is held for it", wait_until([&] { return !svc.connected.load(); }, 3000));
    h.EndSession(0x99);
    holdAt.clear();
    h.SetRetryIoHookForTest(nullptr);'''
s = s.replace(anchor, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
