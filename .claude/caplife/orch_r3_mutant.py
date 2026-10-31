import shutil
import sys

E = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/'
B = 'D:/remote/remote/.claude/caplife/'
files = ['ke_slot.ps1', 'ke_fixture.ps1']
mode = sys.argv[1]
if mode == 'save':
    for f in files:
        shutil.copyfile(E + f, B + f + '.r3_good')
    print('saved')
    sys.exit(0)
for f in files:  # every file from its good copy first: one mutant at a time
    shutil.copyfile(B + f + '.r3_good', E + f)
if mode == 'restore':
    print('restored')
    sys.exit(0)
M = {
    # g1: a harness found dead is still taken as ready (the 9fec1f9 fixture), and no child is checked before the readiness
    'g1': [('ke_fixture.ps1', "    if ($pr.HasExited) { $why = ", "    if ($false) { $why = "),
           ('ke_fixture.ps1', "  if ($dead) { throw \"before the fixture's readiness: $dead\" }", "  if ($false) { }"),
           ('ke_fixture.ps1', "    if ($dead) { throw \"before host $($h.n): $dead\" }", "    if ($false) { }")],
    # g2c: no cleanup on an exception (the 9fec1f9 fixture had none outside Start-Owned)
    'g2c': [('ke_fixture.ps1', "  Stop-All 'the fixture ends'", "  $null = 0")],
    # g2j: the fixture run directly, no job of its own (the 9fec1f9 slot)
    'g2j': [('ke_slot.ps1', 'ARGLINE', 'NEWLINE')],
}
SL = open(E + 'ke_slot.ps1', encoding='utf-8').read().splitlines()
i = next(n for n, l in enumerate(SL) if l.startswith('$fx = Start-Process -FilePath $jobRun'))
ARG = SL[i] + chr(10) + SL[i + 1]
NEW = ('$fx = Start-Process -FilePath powershell -ArgumentList ($fxCmd.Substring(15)) -PassThru -NoNewWindow '
       '-RedirectStandardOutput "$root' + chr(92) + 'fixture_stdout.txt" -RedirectStandardError "$root' + chr(92) + 'fixture_stderr.txt"  # MUTANT g2j')
M['g2j'] = [('ke_slot.ps1', ARG, NEW)]
for f, a, b in M[mode]:
    s = open(E + f, encoding='utf-8', newline='').read()
    assert s.count(a) == 1, (mode, a[:60], s.count(a))
    s = s.replace(a, b)
    open(E + f, 'w', encoding='utf-8', newline='').write(s)
print('mutant', mode)
