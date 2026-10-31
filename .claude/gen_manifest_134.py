# Generates .claude/rel/0.2.134/windows.manifest in the shape 0.2.133 used.
#
# The order is not alphabetical and is not arbitrary -- it is the order 0.2.133 published, kept so
# the two manifests can be read side by side. Sizes and hashes come from the packaged bytes, not
# from the build directory, so what is signed is what was packaged.

import hashlib
import io
import os

VERSION = '0.2.134'
REL = os.path.join('.claude', 'rel', VERSION)
PAYLOAD = os.path.join(REL, 'payload')
BASE = 'https://rem.shotan.net/updates/' + VERSION

# name-in-manifest, path-under-payload, url-suffix
ARTIFACTS = [
    ('GNLinkHost.exe', 'GNLinkHost.exe', 'GNLinkHost.exe'),
    ('GNLinkStream.exe', 'GNLinkStream.exe', 'GNLinkStream.exe'),
    ('GNLinkCapture.exe', 'GNLinkCapture.exe', 'GNLinkCapture.exe'),
    ('GNLinkInputService.exe', 'GNLinkInputService.exe', 'GNLinkInputService.exe'),
    ('GNLinkClient.exe', 'GNLinkClient.exe', 'GNLinkClient.exe'),
    ('GNLinkViewer.exe', 'GNLinkViewer.exe', 'GNLinkViewer.exe'),
    ('GNLinkSetup.exe', 'GNLinkSetup.exe', 'GNLinkSetup.exe'),
    ('GNLinkUpdater.exe', 'GNLinkUpdater.exe', 'GNLinkUpdater.exe'),
    ('ui' + chr(92) + 'shell.html', os.path.join('ui', 'shell.html'), 'ui/shell.html'),
    ('ui' + chr(92) + 'macro.html', os.path.join('ui', 'macro.html'), 'ui/macro.html'),
]

lines = [
    '# GNLink update manifest -- release %s, signed with the operational key.' % VERSION,
    'schema=2',
    'releaseId=r-%s' % VERSION,
    'platform=windows',
    'arch=x64',
    'version=%s' % VERSION,
]

for name, rel_path, url in ARTIFACTS:
    full = os.path.join(PAYLOAD, rel_path)
    data = io.open(full, 'rb').read()
    lines.append('artifact=%s|%d|%s|%s/%s'
                 % (name, len(data), hashlib.sha256(data).hexdigest(), BASE, url))

out = os.path.join(REL, 'windows.manifest')
io.open(out, 'w', encoding='utf-8', newline=chr(10)).write(chr(10).join(lines) + chr(10))
print('wrote ' + out)
for line in lines:
    print('  ' + line)
