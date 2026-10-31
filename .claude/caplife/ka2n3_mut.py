import sys, shutil
p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_file_store.hpp'
bak = 'D:/remote/remote/.claude/caplife/ka2n3_fs.bak'
MUT = {
    'closeIgnored': [('''    const bool closed = CloseHandle(h) != 0 && Pass(Step::Close);
    if (!ok || !closed) return false;''', '''    CloseHandle(h);
    if (!ok) return false;  // MUTANT: the close result ignored''')],
    'replaceIgnored': [('''    return Pass(Step::Replace) &&
           MoveFileExW(Wide(tmp).c_str(), Wide(path_).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0 &&
           Pass(Step::DirSync);''', '''    MoveFileExW(Wide(tmp).c_str(), Wide(path_).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return true;  // MUTANT: success whatever the replace did''')],
}
m, act = sys.argv[1], sys.argv[2]
if act == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in MUT[m]:
        assert s.count(old) == 1, (m, s.count(old))
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(m, act, 'ok')
