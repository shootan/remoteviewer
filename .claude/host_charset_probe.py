# -*- coding: utf-8 -*-
"""Is the Korean in the shipped Host actually Korean?

The literal is UTF-8 in the source. Without /utf-8 MSVC reads the source in the system code page
(949 here), so each UTF-8 byte becomes one CP949 character and the wide literal ends up holding
that. Both forms are searched for as bytes, so the console's own encoding cannot confuse this.
"""
import io
import sys

EXE = '.claude/rel/0.2.117/payload/GNLinkHost.exe'
TEXT = u'새 버전 '           # "새 버전 "
out = []

data = open(EXE, 'rb').read()

correct = TEXT.encode('utf-16-le')
out.append('exe                : %s (%d bytes)' % (EXE, len(data)))
out.append('correct UTF-16LE   : %s  -> found at %d'
           % (correct.hex(), data.find(correct)))

# What the compiler produces when it decodes UTF-8 source bytes as CP949.
raw = TEXT.encode('utf-8')
try:
    misread = raw.decode('cp949')
except UnicodeDecodeError:
    misread = raw.decode('cp949', 'replace')
broken = misread.encode('utf-16-le')
out.append('misread-as-cp949   : %s  -> found at %d'
           % (broken.hex(), data.find(broken)))

# And the other direction, in case the source were CP949 and read as UTF-8.
out.append('')
for label, probe in (('"지금 업데이트" correct', u'지금 업데이트'),):
    ok_bytes = probe.encode('utf-16-le')
    bad = probe.encode('utf-8').decode('cp949', 'replace').encode('utf-16-le')
    out.append('%-24s found at %d' % (label, data.find(ok_bytes)))
    out.append('%-24s found at %d' % (label + ' (broken)', data.find(bad)))

io.open('.claude/host_charset_probe.txt', 'w', encoding='utf-8').write('\n'.join(out) + '\n')
sys.stdout.write('wrote .claude/host_charset_probe.txt\n')
