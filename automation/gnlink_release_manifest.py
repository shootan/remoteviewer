# -*- coding: utf-8 -*-
"""Writes a release manifest, and checks the version strings inside a packaged payload.

Both halves were one-off scripts for 0.2.133 and 0.2.134 (.claude/gen_manifest_134.py, and a block
of Python pasted into a shell). Generalised here because a release procedure that lives in pasted
snippets is a procedure that drifts between releases -- 0.2.134's manifest had to be written by
reading 0.2.133's back out of an archived worktree to recover the artifact order.

  python gnlink_release_manifest.py write  --payload <dir> --version <x.y.z> --out <path>
  python gnlink_release_manifest.py parity --payload <dir> --version <x.y.z>

`write` computes sizes and hashes from THE PACKAGED BYTES, not from the build directory, so what
gets signed describes what was packaged.

`parity` answers the question that hash and signature checks cannot: does the binary actually
claim to be this version? A release can pass every integrity check while carrying a stale number,
because the number is compiled in and the checks only see bytes. It also refuses any OTHER 0.2.x
string found in the payload rather than comparing against a hand-written list of old releases --
a list like that stops catching the version nobody remembered to add to it.

Exit 0 = fine. Anything else = do not publish.
"""
import hashlib
import io
import os
import re
import sys

# The order 0.2.133 and 0.2.134 published, kept so manifests can be read side by side. It is not
# alphabetical and it is not the link order; it is just what was published, and changing it would
# make two releases hard to diff for no gain.
ARTIFACTS = [
    ('GNLinkHost.exe', 'GNLinkHost.exe', 'GNLinkHost.exe'),
    ('GNLinkStream.exe', 'GNLinkStream.exe', 'GNLinkStream.exe'),
    ('GNLinkCapture.exe', 'GNLinkCapture.exe', 'GNLinkCapture.exe'),
    ('GNLinkInputService.exe', 'GNLinkInputService.exe', 'GNLinkInputService.exe'),
    ('GNLinkClient.exe', 'GNLinkClient.exe', 'GNLinkClient.exe'),
    ('GNLinkViewer.exe', 'GNLinkViewer.exe', 'GNLinkViewer.exe'),
    ('GNLinkSetup.exe', 'GNLinkSetup.exe', 'GNLinkSetup.exe'),
    ('GNLinkUpdater.exe', 'GNLinkUpdater.exe', 'GNLinkUpdater.exe'),
    # Added after 0.2.134 (file copy). Kept beside the other exes; the html pair stays last.
    ('GNLinkClipHelper.exe', 'GNLinkClipHelper.exe', 'GNLinkClipHelper.exe'),
    ('ui' + chr(92) + 'shell.html', os.path.join('ui', 'shell.html'), 'ui/shell.html'),
    ('ui' + chr(92) + 'macro.html', os.path.join('ui', 'macro.html'), 'ui/macro.html'),
]

BASE_URL = 'https://rem.shotan.net/updates'

# The four that carry the version once, and the installer that carries it five times. Measured on
# 0.2.133 and 0.2.134; a change here means the product changed how it stamps itself, which is
# worth failing over rather than adjusting quietly.
CARRIERS = {
    'GNLinkHost.exe': 1,
    'GNLinkClient.exe': 1,
    'GNLinkViewer.exe': 1,
    'GNLinkStream.exe': 1,
    'GNLinkSetup.exe': 5,
}
NO_VERSION = ['GNLinkCapture.exe', 'GNLinkInputService.exe', 'GNLinkUpdater.exe',
              'GNLinkClipHelper.exe']


def read(path):
    return io.open(path, 'rb').read()


def write_manifest(payload, version, out):
    missing = [rel for _, rel, _ in ARTIFACTS if not os.path.isfile(os.path.join(payload, rel))]
    if missing:
        sys.stderr.write('missing from the payload: %s\n' % ', '.join(missing))
        return 1

    lines = [
        '# GNLink update manifest -- release %s, signed with the operational key.' % version,
        'schema=2',
        'releaseId=r-%s' % version,
        'platform=windows',
        'arch=x64',
        'version=%s' % version,
    ]
    for name, rel, url in ARTIFACTS:
        data = read(os.path.join(payload, rel))
        lines.append('artifact=%s|%d|%s|%s/%s/%s'
                     % (name, len(data), hashlib.sha256(data).hexdigest(), BASE_URL, version, url))

    # LF, and a trailing newline: the signature covers these bytes exactly.
    io.open(out, 'w', encoding='utf-8', newline=chr(10)).write(chr(10).join(lines) + chr(10))
    sys.stdout.write('wrote %s\n' % out)
    for line in lines:
        sys.stdout.write('  %s\n' % line)
    return 0


# 0.2.<digits> as UTF-16LE, matched against the RAW BYTES rather than a decode of the whole file.
# Decoding from offset zero only finds strings that happen to start on an even offset, and nothing
# guarantees that -- the first version of this check missed its own fixture for exactly that
# reason, because the filler in front of the string was an odd number of bytes.
_UTF16_VERSION = re.compile(b'0\x00\\.\x002\x00\\.\x00(?:[0-9]\x00)+')


def versions_in(data):
    """Every 0.2.x string stored as UTF-16LE, which is how the product compiles them in."""
    return [m.group(0).decode('utf-16-le') for m in _UTF16_VERSION.finditer(data)]


def check_parity(payload, version):
    problems = []
    print('%-24s %10s %s' % ('binary', 'wanted', 'found'))
    for name in [a[0] for a in ARTIFACTS if a[0].endswith('.exe')]:
        path = os.path.join(payload, name)
        if not os.path.isfile(path):
            problems.append('%s: not in the payload' % name)
            continue
        found = versions_in(read(path))
        mine = [v for v in found if v == version]
        stale = sorted(set(v for v in found if v != version))
        want = CARRIERS.get(name, 0)
        print('%-24s %10s %s' % (name, want, '%d x %s%s' % (
            len(mine), version, ('  STALE: ' + ', '.join(stale)) if stale else '')))
        if stale:
            problems.append('%s: carries %s' % (name, ', '.join(stale)))
        if name in CARRIERS and len(mine) != want:
            problems.append('%s: %d occurrences of %s, expected %d'
                            % (name, len(mine), version, want))
        if name in NO_VERSION and mine:
            problems.append('%s: carries a version string and is not expected to' % name)

    print('')
    if problems:
        for p in problems:
            print('PARITY FAIL  %s' % p)
        return 1
    print('parity ok: the carriers claim %s and nothing carries an older one' % version)
    return 0


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    mode = argv[1]
    args = {}
    rest = argv[2:]
    while rest:
        if not rest[0].startswith('--') or len(rest) < 2:
            sys.stderr.write('bad argument: %s\n' % rest[0])
            return 2
        args[rest[0][2:]] = rest[1]
        rest = rest[2:]

    payload = args.get('payload')
    version = args.get('version')
    if not payload or not version:
        sys.stderr.write('--payload and --version are required\n')
        return 2
    if not os.path.isdir(payload):
        sys.stderr.write('no such payload directory: %s\n' % payload)
        return 2

    if mode == 'write':
        out = args.get('out')
        if not out:
            sys.stderr.write('--out is required for write\n')
            return 2
        return write_manifest(payload, version, out)
    if mode == 'parity':
        return check_parity(payload, version)
    sys.stderr.write('unknown mode: %s\n' % mode)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
