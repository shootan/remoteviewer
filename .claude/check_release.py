# -*- coding: utf-8 -*-
"""Checks a release directory against the files on disk AND against the product's own lists.

Two separate questions, kept separate:

1. Does the manifest describe the files that exist? A signature says the document has not changed
   since it was signed; it says nothing about whether the files it names are there. On 0.2.108
   those two came apart -- everything verified while the APK hash belonged to a build that was
   gone.

2. Does the manifest name what the product expects? The names are read out of the source rather
   than typed here, because a list transcribed by hand is a list that drifts. product_image_names()
   is what the updater stops and therefore expects to replace; the installer payload is what a
   fresh install lays down.

Usage: python .claude/check_release.py .claude/rel/0.2.109-r2
"""
import hashlib
import io
import os
import re
import sys

REL = sys.argv[1] if len(sys.argv) > 1 else '.claude/rel/0.2.109-r2'
PLATFORM = sys.argv[2] if len(sys.argv) > 2 else 'windows'

bad = 0


def fail(msg):
    global bad
    print('FAIL  ' + msg)
    bad += 1


# ---------------------------------------------------------------- read the manifest
manifest_path = os.path.join(REL, PLATFORM + '.manifest')
artifacts = []
version = releaseId = ''
for line in io.open(manifest_path, encoding='utf-8').read().split('\n'):
    if line.startswith('version='):
        version = line[len('version='):]
    elif line.startswith('releaseId='):
        releaseId = line[len('releaseId='):]
    elif line.startswith('artifact='):
        name, size, sha, url = line[len('artifact='):].split('|')
        artifacts.append((name, int(size), sha, url))

print('%s  version=%s releaseId=%s artifacts=%d\n' % (PLATFORM, version, releaseId, len(artifacts)))

# ---------------------------------------------------------------- 1. the document vs the disk
print('-- the manifest against the files it names')
for name, size, sha, url in artifacts:
    local = os.path.join(REL, 'payload', name.replace('\\', os.sep))
    if not os.path.exists(local):
        fail('%s: nothing at %s' % (name, local))
        continue
    with open(local, 'rb') as f:
        data = f.read()
    if len(data) != size or hashlib.sha256(data).hexdigest() != sha:
        fail('%s: manifest says %d/%s, file is %d/%s'
             % (name, size, sha[:16], len(data), hashlib.sha256(data).hexdigest()[:16]))
        continue
    print('PASS  %-24s %9d bytes  %s' % (name, size, sha[:16]))

# ---------------------------------------------------------------- 2. the product's own lists
print('\n-- the manifest against what the product expects')
names = set(a[0] for a in artifacts)

src = io.open('apps/native_poc/src/update_process_targets.cpp', encoding='utf-8').read()
block = src[src.index('product_image_names()'):src.index('return names;')]
stop_list = set(re.findall(r'L"([A-Za-z0-9_.]+\.exe)"', block))
missing = stop_list - names
print('product_image_names(): %d entries' % len(stop_list))
for n in sorted(stop_list):
    print('%s  %-24s %s' % ('PASS' if n in names else 'FAIL', n,
                            'in the manifest' if n in names else 'NOT in the manifest'))
if missing:
    fail('the updater stops %s but the manifest does not replace it' % ', '.join(sorted(missing)))

cmake = io.open('apps/native_poc/CMakeLists.txt', encoding='utf-8').read()
payload = set(re.findall(r'_gnlink_payload_dir\}/([A-Za-z0-9_.]+\.(?:exe|html))', cmake))
# The installer stages the two html files flat; the manifest names them where they are INSTALLED.
installed = set(n.split('\\')[-1] for n in names)
print('\ninstaller payload: %d entries' % len(payload))
for n in sorted(payload):
    ok = n in installed
    print('%s  %-24s %s' % ('PASS' if ok else 'FAIL', n,
                            'in the manifest' if ok else 'NOT in the manifest'))
    if not ok:
        fail('the installer lays down %s but the manifest does not update it' % n)

extra = installed - payload - stop_list
if extra:
    print('\nin the manifest but in neither list: %s' % ', '.join(sorted(extra)))

print('\n%s' % ('all checks passed' if bad == 0 else '%d check(s) failed' % bad))
sys.exit(1 if bad else 0)
