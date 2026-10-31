"""The strings that differ between the two APKs' x86_64 native libs (printable runs >= 8), and the ELF build-id."""
import re
import sys
import zipfile

n = 'lib/x86_64/libremote60_android_direct.so'
a = zipfile.ZipFile(sys.argv[1]).read(n)
b = zipfile.ZipFile(sys.argv[2]).read(n)
sa = set(re.findall(rb'[\x20-\x7e]{8,}', a))
sb = set(re.findall(rb'[\x20-\x7e]{8,}', b))
print('only published:', len(sa - sb))
for s in sorted(sa - sb)[:15]:
    print('  P', s[:160])
print('only fixture:', len(sb - sa))
for s in sorted(sb - sa)[:15]:
    print('  S', s[:160])
for name, d in (('published', a), ('fixture', b)):
    i = d.find(b'GNU\x00')
    print(name, 'build-id', d[i + 4:i + 24].hex() if i >= 0 else '?')
# byte positions that differ, grouped
diff = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
print('differing bytes (same offset):', len(diff), 'first', diff[:5])
