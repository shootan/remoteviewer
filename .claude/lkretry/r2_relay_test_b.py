p = r'C:\work\gnlink-wt\fix-lk-retry\apps\native_poc\src\host_unlock_relay_ondemand_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:90]
    s = s.replace(old, new)
rep('''    // (2) The supersede is answered only after the caller's deadline (the service is slow): the retry's own
    // exchange ends with that deadline -- no fresh challenge, no hold for it, the caller said 18.''',
    '''    // (2) The supersede is answered only after the retry's deadline (the service is slow): the retry's own
    // exchange ends at that deadline -- inside the caller's wait, so the caller hears it: 18, nothing new asked.''')
rep('''    check("N1 (2): a supersede slower than the retry's deadline -> the caller said 18 (no answer), and NO fresh challenge",
          !got2 && svc.challengeSeen.load() == ch2,
          std::to_string(svc.challengeSeen.load() - ch2) + " challenges");''',
    '''    check("N1 (2): a supersede slower than the retry's deadline -> the caller hears 18 (not ended), and NO fresh challenge",
          got2 && r2.status == static_cast<uint16_t>(UnlockStage::SupersedeRefused) && svc.challengeSeen.load() == ch2,
          "got=" + std::to_string(got2) + " status=" + std::to_string(r2.status) + ", " +
              std::to_string(svc.challengeSeen.load() - ch2) + " challenges");''')
rep('''    // (2b) The supersede succeeds in time, then the fresh challenge is answered after the retry's deadline:
    // the earlier request keeps its real end (17) -- and no challenge of the retry is held.''',
    '''    // (2b) The supersede succeeds in time, then the fresh challenge is answered after the retry's deadline:
    // the earlier request keeps its real end (17), the retry is answered 17 too (the earlier wait is over, no
    // new challenge) -- and no challenge of the retry is held.''')
rep('''    check("N1 (2b): the earlier wait ended (its own Superseded, final), while the retry's caller said 18",
          !got2b && h.PollResult(0x85, 85, &a2b) && a2b.stage == static_cast<uint16_t>(UnlockStage::Superseded) &&
              a2b.terminal == 1,
          "A stage=" + std::to_string(a2b.stage));''',
    '''    const bool aEnded = h.PollResult(0x85, 85, &a2b);
    check("N1 (2b): the earlier wait ended (its own Superseded, final); the retry is answered Superseded (no challenge)",
          got2b && r2b.status == static_cast<uint16_t>(UnlockStage::Superseded) && aEnded &&
              a2b.stage == static_cast<uint16_t>(UnlockStage::Superseded) && a2b.terminal == 1,
          "retry status=" + std::to_string(r2b.status) + " A stage=" + std::to_string(a2b.stage));''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
