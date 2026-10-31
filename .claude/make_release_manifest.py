# -*- coding: utf-8 -*-
"""Builds the two release manifests for the 0.2.108 / 0.2.16 target.

The artifact names are the paths the installer lays down, because the updater replaces files in
the install directory by those names -- `ui\\shell.html`, not `shell.html`. Sizes and hashes are
read from the files that were actually built, never typed.

The URLs point at the static path the handoff document asks the NAS to serve. That path is a
condition of this release, not a guess: the directory server has nine routes and none of them
serves an artifact, so the files have to come from somewhere else.

Written as bytes with explicit LF endings. The signature covers exact bytes, and anything that
rewrites line endings later produces a document that verifies nowhere.
"""
import hashlib
import io
import os

BASE = 'https://rem.shotan.net/updates/0.2.108'
APK_BASE = 'https://rem.shotan.net/updates/0.2.16'
REL = '.claude/rel/0.2.108'

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


def write(path, text):
    with io.open(path, 'wb') as f:
        f.write(text.encode('utf-8'))


lines = [
    '# GNLink update manifest -- release 0.2.108, signed with the operational key.',
    'schema=2',
    'releaseId=r-0.2.108',
    'platform=windows',
    'arch=x64',
    'version=0.2.108',
]
for installed, built in WINDOWS:
    size, sha = digest(os.path.join(REL, 'payload', built))
    url = BASE + '/' + installed.replace('\\', '/')
    lines.append('artifact=%s|%d|%s|%s' % (installed, size, sha, url))
write(os.path.join(REL, 'windows.manifest'), '\n'.join(lines) + '\n')

apkSize, apkSha = digest('dist/GNLink-0.2.16.apk')
android = [
    '# GNLink update manifest -- release 0.2.16 (versionCode 15), signed with the operational key.',
    'schema=2',
    'releaseId=r-0.2.16',
    'platform=android',
    'arch=arm64-v8a',
    'version=0.2.16',
    'artifact=GNLink-0.2.16.apk|%d|%s|%s' % (apkSize, apkSha, APK_BASE + '/GNLink-0.2.16.apk'),
]
write(os.path.join(REL, 'android.manifest'), '\n'.join(android) + '\n')

for name in ('windows.manifest', 'android.manifest'):
    path = os.path.join(REL, name)
    with open(path, 'rb') as f:
        raw = f.read()
    print('%s  %d bytes  sha256=%s' % (name, len(raw), hashlib.sha256(raw).hexdigest()))
