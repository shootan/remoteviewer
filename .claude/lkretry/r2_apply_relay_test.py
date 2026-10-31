p = r'C:\work\gnlink-wt\fix-lk-retry\apps\native_poc\src\host_unlock_relay_ondemand_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:90]
    s = s.replace(old, new)
rep('''  std::atomic<int> supersedeSeen{0};''', '''  std::atomic<int> supersedeSeen{0};
  std::atomic<int> supersedeDelayMs{0};  // lk-ux-retry r2: answer a supersede this late''')
rep('''      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede)) {
        SecureUnlockResult r{};''', '''      } else if (kind == static_cast<uint16_t>(SecureUnlockKind::LockCredSupersede)) {
        if (supersedeDelayMs.load() > 0) Sleep(supersedeDelayMs.load());
        SecureUnlockResult r{};''')
block = open(r'D:\remote\remote\.claude\lkretry\r2_relay_block.cpp.txt', encoding='utf-8').read()
rep('  a.Stop();\n  b.Stop();\n  svc.Stop();', block + '  a.Stop();\n  b.Stop();\n  svc.Stop();')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
