p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_slot.ps1'
lines = open(p, encoding='utf-8').read().split('\n')
assert 'RedirectStandardOutput' in lines[16], lines[16]
lines[16] = ("  '-Tag', $Tag, '-HostSec', '2400') -PassThru -NoNewWindow -RedirectStandardOutput \"$root\\fixture_stdout.txt\" "
             "-RedirectStandardError \"$root\\fixture_stderr.txt\"")
s = '\n'.join(lines)
assert not any(ord(c) < 32 and c not in '\n\r\t' for c in s)
open(p, 'w', encoding='utf-8').write(s)
print(lines[16])
