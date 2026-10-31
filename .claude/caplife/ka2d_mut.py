import sys, shutil
p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/native_video_client_session.cpp'
bak = 'D:/remote/remote/.claude/caplife/ka2d_session_cpp.bak'
old = '  const bool admitted = args.udpHandshake ? args.udpHandshake(connected, stopRequested_, error, &ackFeatures)\n'
new = '  const bool admitted = false ? args.udpHandshake(connected, stopRequested_, error, &ackFeatures)  // MUTANT: always the Hello\n'
if sys.argv[1] == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    t = s.replace('\r\n', '\n')
    assert t.count(old) == 1
    t = t.replace(old, new)
    if crlf:
        t = t.replace('\n', '\r\n')
    open(p, 'wb').write(t.encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(sys.argv[1], 'ok')
