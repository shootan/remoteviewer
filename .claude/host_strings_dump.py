# -*- coding: utf-8 -*-
"""Dumps every wide string in the Host that is not plain ASCII, so the actual bytes can be read.

Searching for one expected form and finding nothing proves only that the guess was wrong. This
lists what is really in there.
"""
import io
import re
import sys

EXE = sys.argv[1] if len(sys.argv) > 1 else '.claude/rel/0.2.117/payload/GNLinkHost.exe'
data = open(EXE, 'rb').read()

found = []
for match in re.finditer(rb'(?:[\x20-\xff\x00-\x19][\x00-\xff]){4,}\x00\x00', data):
    raw = match.group(0)[:-2]
    if len(raw) % 2:
        raw = raw[:-1]
    try:
        text = raw.decode('utf-16-le')
    except UnicodeDecodeError:
        continue
    if all(ord(ch) < 128 for ch in text):
        continue
    if not text.strip():
        continue
    found.append((match.start(), text))

lines = ['%s  %d non-ascii wide strings' % (EXE, len(found)), '']
for offset, text in found[:80]:
    codes = ' '.join('%04X' % ord(ch) for ch in text[:12])
    lines.append('%08x  %s' % (offset, repr(text)))
    lines.append('          %s' % codes)

io.open('.claude/host_strings_dump.txt', 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
sys.stdout.write('wrote .claude/host_strings_dump.txt (%d strings)\n' % len(found))
