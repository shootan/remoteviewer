# -*- coding: utf-8 -*-
"""Reads each manifest back and checks every artifact line against the file it names.

Written because a signature proves the document has not changed since it was signed -- and says
nothing about whether the document describes the files that exist. Regenerating the APK without
regenerating the manifest produced a manifest that verified perfectly and pointed at a size and a
hash nothing on disk had.

The mapping from URL to local file is the one the handoff asks the NAS to create, so this also
checks that every URL has something staged behind it.
"""
import hashlib
import io
import os
import sys

REL = '.claude/rel/0.2.108'

# URL prefix -> local directory the handoff stages from.
ROOTS = [
    ('https://rem.shotan.net/updates/0.2.108/', os.path.join(REL, 'payload')),
    ('https://rem.shotan.net/updates/0.2.16/', 'dist'),
]

bad = 0
checked = 0
for name in ('windows.manifest', 'android.manifest'):
    path = os.path.join(REL, name)
    for line in io.open(path, encoding='utf-8').read().split('\n'):
        if not line.startswith('artifact='):
            continue
        installed, size, sha, url = line[len('artifact='):].split('|')
        local = None
        for prefix, root in ROOTS:
            if url.startswith(prefix):
                local = os.path.join(root, url[len(prefix):].replace('/', os.sep))
        if local is None:
            print('FAIL  %s: no staging root covers %s' % (name, url))
            bad += 1
            continue
        if not os.path.exists(local):
            print('FAIL  %s: nothing staged at %s (for %s)' % (name, local, url))
            bad += 1
            continue
        with open(local, 'rb') as f:
            data = f.read()
        realSize = len(data)
        realSha = hashlib.sha256(data).hexdigest()
        if str(realSize) != size or realSha != sha:
            print('FAIL  %s: %s says %s/%s, file is %d/%s'
                  % (name, installed, size, sha[:16], realSize, realSha[:16]))
            bad += 1
            continue
        checked += 1
        print('PASS  %-24s %9s bytes  %s' % (installed, size, sha[:16]))

print('\n%d artifacts match the staged files, %d do not' % (checked, bad))
sys.exit(1 if bad else 0)
