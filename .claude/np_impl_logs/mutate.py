import sys, shutil
p, mode = sys.argv[1], sys.argv[2]
bak = r'D:\remote\remote\.claude\np_impl_logs\directory_rendezvous.cpp.orig'
if mode == 'restore':
    shutil.copyfile(bak, p); print('restored'); sys.exit(0)
shutil.copyfile(p, bak)
s = open(p, 'rb').read().decode('utf-8')
if mode == 'm1':  # the commit check on the datagram path is gone
    a = 'if (heldRelay >= 0 && at >= commitAt) return choose(static_cast<size_t>(heldRelay), "relay");'
elif mode == 'm2':  # no shape check
    a = '      const bool valid = punch_answer_is_valid(scratch, n);'
    s2 = s.replace(a, '      const bool valid = n > 0;'); assert s2 != s; open(p, 'wb').write(s2.encode()); print(mode); sys.exit(0)
elif mode == 'm3':  # no hold: early behaves as legacy
    a = '  const bool early = options.relayCommitMs > 0;'
    s2 = s.replace(a, '  const bool early = false;'); assert s2 != s; open(p, 'wb').write(s2.encode()); print(mode); sys.exit(0)
assert s.count(a) == 1
open(p, 'wb').write(s.replace(a, '/* mutant */').encode()); print(mode)
