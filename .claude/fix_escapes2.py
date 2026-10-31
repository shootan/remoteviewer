import io
import re

BS = chr(92)   # backslash, written this way so no heredoc can halve it
Q = chr(34)    # quote

p = 'apps/native_poc/src/update_manifest_test.cpp'
d = io.open(p, encoding='utf-8', errors='surrogateescape').read()

# 1) the non-ASCII case: replace whatever literal is in there with explicit byte escapes
pattern = re.compile(r'\{"a non-ASCII name is refused", line\(' + Q + r'[^"]*' + Q +
                     r'\), ManifestStatus::Malformed\},')
m = pattern.search(d)
assert m, 'non-ASCII case not found'
replacement = ('{"a non-ASCII name is refused", line(' + Q + BS + 'xed' + BS + 'x95' + BS + 'x9c.exe'
               + Q + '), ManifestStatus::Malformed},')
d = d[:m.start()] + replacement + d[m.end():]

# 2) path separators that lost a backslash: "ui\shell.html" must be "ui\\shell.html" in source
before = d
d = d.replace('ui' + BS + 'shell.html', 'ui' + BS + BS + 'shell.html')
print('separator fixes:', 0 if d == before else d.count('ui' + BS + BS + 'shell.html'))

io.open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(d)
print('done')
