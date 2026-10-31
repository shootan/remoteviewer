# -*- coding: utf-8 -*-
"""Builds the 0.2.117 windows manifest.

Same shape as 0.2.114 -- ten artifacts, installed names on the left (`ui\\shell.html`, not
`shell.html`), sizes and hashes read from the staged files rather than typed. Written as bytes
with explicit LF, because the signature covers exact bytes.

`GNLinkUpdater.exe` is in the list on purpose: until 0.2.113 no release named it, so no release
had ever replaced the updater.
"""
import hashlib
import io
import os

VER = '0.2.117'
BASE = 'https://rem.shotan.net/updates/' + VER
REL = '.claude/rel/' + VER

WINDOWS = [
    ('GNLinkHost.exe', 'GNLinkHost.exe'),
    ('GNLinkStream.exe', 'GNLinkStream.exe'),
    ('GNLinkCapture.exe', 'GNLinkCapture.exe'),
    ('GNLinkInputService.exe', 'GNLinkInputService.exe'),
    ('GNLinkClient.exe', 'GNLinkClient.exe'),
    ('GNLinkViewer.exe', 'GNLinkViewer.exe'),
    ('GNLinkSetup.exe', 'GNLinkSetup.exe'),
    ('GNLinkUpdater.exe', 'GNLinkUpdater.exe'),
    ('ui\\shell.html', 'ui/shell.html'),
    ('ui\\macro.html', 'ui/macro.html'),
]

lines = [
    '# GNLink update manifest -- release %s, signed with the operational key.' % VER,
    'schema=2',
    'releaseId=r-' + VER,
    'platform=windows',
    'arch=x64',
    'version=' + VER,
]
for installed, built in WINDOWS:
    path = os.path.join(REL, 'payload', built)
    with open(path, 'rb') as f:
        data = f.read()
    lines.append('artifact=%s|%d|%s|%s' % (
        installed, len(data), hashlib.sha256(data).hexdigest(),
        BASE + '/' + installed.replace('\\', '/')))

out = os.path.join(REL, 'windows.manifest')
with io.open(out, 'wb') as f:
    f.write(('\n'.join(lines) + '\n').encode('utf-8'))

with open(out, 'rb') as f:
    raw = f.read()
print('windows.manifest  %d bytes  sha256=%s' % (len(raw), hashlib.sha256(raw).hexdigest()))
