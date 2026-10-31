import sys, shutil
p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_lease.hpp'
bak = 'D:/remote/remote/.claude/caplife/ka2s_lease.bak'
old = '    const SocketHandle sessionHandle = Duplicate(socket_);\n'
new = '    const SocketHandle sessionHandle = socket_;  // MUTANT: the lease gives its socket away (a new one after)\n    socket_ = kInvalidSocket;\n'
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
