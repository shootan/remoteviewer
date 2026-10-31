# -*- coding: utf-8 -*-
"""Builds the 0.2.110 Windows manifest, with the artifact list read from the product.

The names are no longer written here. 0.2.109 shipped without GNLinkSetup.exe because this file
carried its own copy of the list and the copy was wrong; item 7 now refuses such a manifest at
publish time, and this closes the same gap from the other end -- the list has one source, and it
is the one the swap actually walks (product_payload_names()).

Sizes and hashes are read from the files that were built, never typed.
"""
import hashlib
import io
import os
import re
import sys

# Version from argv so there is one generator rather than a copy per release -- a copy per
# release is exactly how 0.2.109 ended up with a stale artifact list.
VERSION = sys.argv[1] if len(sys.argv) > 1 else '0.2.110'
RELEASE_ID = 'r-' + VERSION
BASE = 'https://rem.shotan.net/updates/' + VERSION
REL = '.claude/rel/' + VERSION
SRC = 'apps/native_poc/src/update_process_targets.cpp'


def payload_names():
    src = io.open(SRC, encoding='utf-8').read()
    stop = src[src.index('product_image_names()'):src.index('return names;')]
    names = re.findall(r'L"([^"]+)"', stop)
    start = src.index('std::vector<std::wstring> product_payload_names()')
    body = src[start:src.index('return names;', start)]
    for extra in re.findall(r'names\.push_back\(L"([^"]+)"\)', body):
        names.append(extra.replace('\\\\', '\\'))
    return names


def digest(path):
    with open(path, 'rb') as f:
        data = f.read()
    return len(data), hashlib.sha256(data).hexdigest()


lines = [
    '# GNLink update manifest -- release %s, signed with the operational key.' % VERSION,
    'schema=2',
    'releaseId=' + RELEASE_ID,
    'platform=windows',
    'arch=x64',
    'version=' + VERSION,
]
missing = []
for installed in payload_names():
    local = os.path.join(REL, 'payload', installed.replace('\\', os.sep))
    if not os.path.exists(local):
        missing.append(installed)
        continue
    size, sha = digest(local)
    lines.append('artifact=%s|%d|%s|%s'
                 % (installed, size, sha, BASE + '/' + installed.replace('\\', '/')))

if missing:
    print('REFUSING: nothing staged for %s' % ', '.join(missing), file=sys.stderr)
    raise SystemExit(1)

path = os.path.join(REL, 'windows.manifest')
with io.open(path, 'wb') as f:
    f.write(('\n'.join(lines) + '\n').encode('utf-8'))

with open(path, 'rb') as f:
    raw = f.read()
print('windows.manifest  %d bytes  sha256=%s' % (len(raw), hashlib.sha256(raw).hexdigest()))
for line in raw.decode('utf-8').splitlines():
    print('  ' + line)
