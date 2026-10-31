import re, sys, zipfile
def dex(p):
    z = zipfile.ZipFile(p); return b''.join(z.read(n) for n in sorted(z.namelist()) if re.match(r'classes\d*\.dex$', n))
a, b = dex(sys.argv[1]), dex(sys.argv[2])
sa = set(re.findall(rb'[\x20-\x7e]{6,}', a)); sb = set(re.findall(rb'[\x20-\x7e]{6,}', b))
print('dex sizes', len(a), len(b))
print('only published:', [s[:120] for s in sorted(sa - sb)][:20])
print('only fixture:', [s[:120] for s in sorted(sb - sa)][:20])
