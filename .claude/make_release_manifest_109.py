# -*- coding: utf-8 -*-
"""Builds the Windows manifest for the 0.2.109 target.

Windows only, on purpose. 0.2.16 (versionCode 15) is already published and nothing in this
release touches an Android file, so re-cutting the APK would be a version bump that describes no
change -- the number would stop being evidence of anything.

There is no new baseline either: the machine already runs 0.2.108, which carries the operational
public key, and its update check does not depend on the directory connection this release
repairs. `start_update_check()` reads the cached url and host token and calls out over https on
its own; it never asks whether the heartbeat succeeded.

The artifact names are the paths the installer lays down, because the updater replaces files in
the install directory by those names -- `ui\\shell.html`, not `shell.html`. Sizes and hashes are
read from the files that were actually built, never typed.

Written as bytes with explicit LF endings. The signature covers exact bytes, and anything that
rewrites line endings later produces a document that verifies nowhere.
"""
import hashlib
import io
import os

VERSION = '0.2.109'
BASE = 'https://rem.shotan.net/updates/' + VERSION
REL = '.claude/rel/' + VERSION

# Installed name -> file in the payload directory. The left side is what the updater writes.
WINDOWS = [
    ('GNLinkHost.exe', 'GNLinkHost.exe'),
    ('GNLinkStream.exe', 'GNLinkStream.exe'),
    ('GNLinkInputService.exe', 'GNLinkInputService.exe'),
    ('GNLinkCapture.exe', 'GNLinkCapture.exe'),
    ('GNLinkClient.exe', 'GNLinkClient.exe'),
    ('GNLinkViewer.exe', 'GNLinkViewer.exe'),
    ('GNLinkUpdater.exe', 'GNLinkUpdater.exe'),
    ('ui\\shell.html', 'ui/shell.html'),
    ('ui\\macro.html', 'ui/macro.html'),
]


def digest(path):
    with open(path, 'rb') as f:
        data = f.read()
    return len(data), hashlib.sha256(data).hexdigest()


lines = [
    '# GNLink update manifest -- release %s, signed with the operational key.' % VERSION,
    'schema=2',
    'releaseId=r-' + VERSION,
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
