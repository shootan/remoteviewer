import shutil
import sys

E = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/'
B = 'D:/remote/remote/.claude/caplife/'
files = ['ke_slot.ps1', 'ke_fixture.ps1']
mode = sys.argv[1]
if mode == 'save':
    for f in files:
        shutil.copyfile(E + f, B + f + '.r2_good')
    print('saved')
    sys.exit(0)
for f in files:  # every file from its good copy first: one mutant at a time
    shutil.copyfile(B + f + '.r2_good', E + f)
if mode == 'restore':
    print('restored')
    sys.exit(0)
M = {
    # m1: the slot starts the AVD whatever the fixture said (the 57fa793 ke_slot: it waited for the fixture's exit only)
    'm1': ('ke_slot.ps1', 'if ($why) {\n  Say "  NO AVD: $why"', 'if ($false) {  # MUTANT m1\n  Say "  NO AVD: $why"'),
    # m2: the bundle's rc is the AVD's alone (the 57fa793 ke_slot: exit $avdExit)
    'm2': ('ke_slot.ps1', '$rc = if ($fxExit -ne 0) { 3 } elseif', '$rc = if ($false) { 3 } elseif'),
    # m3: any readiness file counts (the 57fa793 ke_fixture: the file's existence, or the timeout)
    'm3': ('ke_fixture.ps1', '  elseif ($ready -notmatch "^ready run=', '  elseif ($false -and $ready -notmatch "^ready run='),
}
f, a, b = M[mode]
s = open(E + f, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
t = s.replace('\r\n', '\n') if crlf else s
assert t.count(a) == 1, (mode, t.count(a))
t = t.replace(a, b)
if crlf:
    t = t.replace('\n', '\r\n')
open(E + f, 'w', encoding='utf-8', newline='').write(t)
print('mutant', mode)
