import sys, shutil, os
src = 'C:\\work\\gnlink-wt\\fix-lk-retry\\apps\\native_poc\\src\\'
bak = r'D:\remote\remote\.claude\lkretry\r4_mutant_bak' + '\\'
os.makedirs(bak, exist_ok=True)
MUTANTS = {
    # (1) reverted: the exchange's wait is the relative budget only.
    'M10': [('host_unlock_relay.cpp',
             '  if (gate && gate->deadlineMs != 0 && gate->deadlineMs < deadline) deadline = gate->deadlineMs;',
             '  // MUTANT M10: no clamp to the retry\'s end')],
    # (2) reverted: the adoption decided on "live" alone (the deadline not checked where it is held/cached).
    'M11': [('host_unlock_relay.cpp',
             '          !(live && (cmd.deadlineMs == 0 || GetTickCount64() < cmd.deadlineMs))) {',
             '          !live) {  // MUTANT M11')],
}
mode, name = sys.argv[1], sys.argv[2]
files = sorted(set(f for f, _, _ in MUTANTS[name]))
if mode == 'apply':
    for f in files:
        shutil.copy2(src + f, bak + f)
    for f, old, new in MUTANTS[name]:
        s = open(src + f, 'rb').read().decode('utf-8')
        crlf = '\r\n' in s
        s = s.replace('\r\n', '\n')
        assert s.count(old) == 1, (f, s.count(old))
        s = s.replace(old, new)
        if crlf:
            s = s.replace('\n', '\r\n')
        open(src + f, 'wb').write(s.encode('utf-8'))
else:
    for f in files:
        shutil.copyfile(bak + f, src + f)
        os.remove(bak + f)
print(mode, name, 'ok')
