# -*- coding: utf-8 -*-
"""Builds the 0.2.109 manifest again, this time naming GNLinkSetup.exe.

Why a revision rather than a new version: the nine artifacts already published under
/updates/0.2.109/ are unchanged, byte for byte. Bumping the version would describe a change that
did not happen and would make the number stop being evidence. `version` therefore stays 0.2.109
and only `releaseId` moves -- both readers require releaseId to be non-empty and constrain nothing
else (update_manifest.cpp:195, update_manifest.js:163).

Why the installer belongs in the manifest at all: the uninstall entry points at
<installDir>\\GNLinkSetup.exe. An update that replaces the application without replacing that file
leaves the uninstaller describing a version that is gone -- which is what install_registration_test
exists to pin, and why payload_name_test already accepts the name.

Sizes and hashes are read from the files, never typed. The nine existing entries are regenerated
from the SAME payload bytes that were published, so any drift shows up here as a hash that no
longer matches the server.
"""
import hashlib
import io
import os

VERSION = '0.2.109'
RELEASE_ID = 'r-0.2.109-2'
BASE = 'https://rem.shotan.net/updates/' + VERSION
REL = '.claude/rel/0.2.109-r2'

# Installed name -> file in the payload directory. The left side is what the updater writes.
WINDOWS = [
    ('GNLinkHost.exe', 'GNLinkHost.exe'),
    ('GNLinkStream.exe', 'GNLinkStream.exe'),
    ('GNLinkInputService.exe', 'GNLinkInputService.exe'),
    ('GNLinkCapture.exe', 'GNLinkCapture.exe'),
    ('GNLinkClient.exe', 'GNLinkClient.exe'),
    ('GNLinkViewer.exe', 'GNLinkViewer.exe'),
    ('GNLinkUpdater.exe', 'GNLinkUpdater.exe'),
    # New in this revision. Its absence is why the public URL answered 404.
    ('GNLinkSetup.exe', 'GNLinkSetup.exe'),
    ('ui\\shell.html', 'ui/shell.html'),
    ('ui\\macro.html', 'ui/macro.html'),
]


def digest(path):
    with open(path, 'rb') as f:
        data = f.read()
    return len(data), hashlib.sha256(data).hexdigest()


lines = [
    '# GNLink update manifest -- release %s (%s), signed with the operational key.' % (VERSION, RELEASE_ID),
    'schema=2',
    'releaseId=' + RELEASE_ID,
    'platform=windows',
    'arch=x64',
    'version=' + VERSION,
]
for installed, built in WINDOWS:
    size, sha = digest(os.path.join(REL, 'payload', built))
    url = BASE + '/' + installed.replace('\\', '/')
    lines.append('artifact=%s|%d|%s|%s' % (installed, size, sha, url))

path = os.path.join(REL, 'windows.manifest')
with io.open(path, 'wb') as f:
    f.write(('\n'.join(lines) + '\n').encode('utf-8'))

with open(path, 'rb') as f:
    raw = f.read()
print('windows.manifest  %d bytes  sha256=%s' % (len(raw), hashlib.sha256(raw).hexdigest()))
for line in raw.decode('utf-8').splitlines():
    print('  ' + line)
