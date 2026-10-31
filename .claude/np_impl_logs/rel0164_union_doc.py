"""rel-0164: resolve an append-only DOC conflict by keeping both sides (ours, then theirs). Refuses product files and
any conflict whose sides are not plain appended sections (a nested marker). Usage: python rel0164_union_doc.py <file>"""
import sys

p = sys.argv[1]
if not (p.endswith('.md') or p.endswith('.txt')):
    sys.exit('refused: only documents are unioned this way: ' + p)
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
out, i, n = [], 0, 0
lines = s.split('\n')
while i < len(lines):
    l = lines[i]
    if l.startswith('<<<<<<< '):
        ours, theirs, j = [], [], i + 1
        while not lines[j].startswith('======='):
            assert not lines[j].startswith('<<<<<<<'), 'nested marker'
            ours.append(lines[j]); j += 1
        j += 1
        while not lines[j].startswith('>>>>>>> '):
            assert not lines[j].startswith(('<<<<<<<', '=======')), 'nested marker'
            theirs.append(lines[j]); j += 1
        while ours and ours[-1] == '':
            ours.pop()
        out += ours + [''] + theirs
        n += 1
        i = j + 1
        continue
    out.append(l)
    i += 1
t = '\n'.join(out)
assert '<<<<<<<' not in t and '>>>>>>>' not in t
open(p, 'wb').write((t.replace('\n', '\r\n') if crlf else t).encode('utf-8'))
print(p, 'conflicts unioned:', n)
