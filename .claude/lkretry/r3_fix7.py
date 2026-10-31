p = r'C:\work\gnlink-wt\fix-lk-retry\apps\native_poc\src\host_unlock_relay_ondemand_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old_start = '    // (7) The fresh challenge WAS written and answered (issued), and the worker is held before adopting it while\n'
old_end = '    h.EndSession(0x97);\n'
i = s.index(old_start)
j = s.index(old_end, i)
new = '''    // (7) The fresh challenge WAS written and answered (issued); the worker is held before adopting it until the
    // retry's own deadline has passed -- while its caller is still waiting (2000 ms wait; the retry ends 250 ms
    // inside it). Let go: the late "issued" is NOT adopted -- the caller hears Superseded (the earlier wait is
    // over, no new challenge), and the pipe is not held for it.
    check("N1 (7) setup: A typed and waiting", typed_wait(0x97, 97));
    ResetEvent(go);
    holdAt = "adopt";
    ControlUnlockChallengeMessage r7{};
    bool got7 = false;
    const auto t7 = std::chrono::steady_clock::now();
    std::thread caller7([&] { got7 = h.LockCredRetryChallengeSync(challenge_req(98), 97, 0x97, &r7, 2000); });
    const bool held7 = wait_until([&] { return at.load() == 1; }, 3000);
    while (ms_since(t7) < 1850) Sleep(10);  // past the retry's deadline (1750), inside the caller's wait (2000)
    SetEvent(go);
    caller7.join();
    Sleep(300);
    ControlUnlockStatusResultMessage a7{};
    const bool noHold7 = wait_until([&] { return !svc.connected.load(); }, 3000);
    check("N1 (7): an issued challenge adopted after the retry's deadline? NO -- the caller hears Superseded, no hold",
          held7 && got7 && r7.status == static_cast<uint16_t>(UnlockStage::Superseded) && noHold7 &&
              !h.PollResult(0x97, 98, &a7),
          "got=" + std::to_string(got7) + " status=" + std::to_string(r7.status) + " noHold=" + std::to_string(noHold7));
'''
s = s[:i] + new + s[j:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
