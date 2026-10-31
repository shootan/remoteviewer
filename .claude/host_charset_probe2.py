# -*- coding: utf-8 -*-
import io
import sys

data = open('.claude/rel/0.2.117/payload/GNLinkHost.exe', 'rb').read()
out = []

probes = [
    u'새',                    # 새
    u'업데이트',  # 업데이트
    u'지금',              # 지금
    u'GNLink',                    # a control: plain ascii wide literal
]

for probe in probes:
    wide = probe.encode('utf-16-le')
    at = data.find(wide)
    out.append('%-28s utf16 %-28s at %d' % (repr(probe), wide.hex(), at))

# The misread form: UTF-8 bytes of the literal decoded as cp949, then stored as wide.
for probe in probes[:3]:
    raw = probe.encode('utf-8')
    broken = raw.decode('cp949', 'replace')
    wide = broken.encode('utf-16-le', 'replace')
    at = data.find(wide)
    out.append('%-28s MISREAD %-26s at %d  (%s)'
               % (repr(probe), wide.hex(), at, repr(broken)))

io.open('.claude/host_charset_probe.txt', 'w', encoding='utf-8').write('\n'.join(out) + '\n')
sys.stdout.write('ok\n')
