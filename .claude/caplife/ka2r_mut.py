import sys, shutil
p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/directory_session_client.cpp'
bak = 'D:/remote/remote/.claude/caplife/ka2r_dsc.bak'
old = '<< json_escape(observeToken) << "\\"" << (standby ? ",\\"standby\\":true" : "") << "}";'
new = '<< json_escape(observeToken) << "\\"" << (false ? ",\\"standby\\":true" : "") << "}";  // MUTANT: no standby asked'
if sys.argv[1] == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    assert s.count(old) == 1, s.count(old)
    open(p, 'wb').write(s.replace(old, new).encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(sys.argv[1], 'ok')
