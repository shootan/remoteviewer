p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_entry_client_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
t = s.replace('\r\n', '\n')
bad = '  std::printf("\n--- release (the app leaves the screen, the PC is evicted, sign-out) ---\n");'
good = '  std::printf("\\n--- release (the app leaves the screen, the PC is evicted, sign-out) ---\\n");'
assert t.count(bad) == 1, t.count(bad)
t = t.replace(bad, good)
if crlf:
    t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('utf-8'))
print('ok')
