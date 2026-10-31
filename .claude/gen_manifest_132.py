import sys, os, hashlib
W = sys.argv[1]
old = os.path.join(W, '.claude/rel/0.2.130/windows.manifest')
new_dir = os.path.join(W, '.claude/rel/0.2.132')
ob = open(old, 'rb').read()
nl = b'\r\n' if b'\r\n' in ob else b'\n'
trailing = ob.endswith(nl)
order = [l.split(b'=', 1)[1].split(b'|')[0].decode() for l in ob.split(nl) if l.startswith(b'artifact=')]
lines = [b'# GNLink update manifest -- release 0.2.132, signed with the operational key.',
         b'schema=2', b'releaseId=r-0.2.132', b'platform=windows', b'arch=x64', b'version=0.2.132']
for name in order:
    rel = name.replace(chr(92), '/')
    p = os.path.join(new_dir, 'payload', rel)
    data = open(p, 'rb').read()
    lines.append(('artifact=%s|%d|%s|https://rem.shotan.net/updates/0.2.132/%s' %
                  (name, len(data), hashlib.sha256(data).hexdigest(), rel)).encode())
out = nl.join(lines) + (nl if trailing else b'')
open(os.path.join(new_dir, 'windows.manifest'), 'wb').write(out)
print('newline', repr(nl), 'trailing', trailing, 'artifacts', len(order))
