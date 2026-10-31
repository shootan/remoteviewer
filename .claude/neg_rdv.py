import io, sys

p = 'apps/native_poc/src/directory_rendezvous.cpp'
d = io.open(p, encoding='utf-8').read()

BS = chr(92)
old2 = ('    if (port > 65535) return false;\n'
        '    ++cursor;\n'
        '  }\n'
        '  if (cursor == digitsBegin) return false;')
new2 = ('    ++cursor;\n'
        '  }\n'
        '  if (cursor == digitsBegin) return false;')
assert d.count(old2) == 1, 'old2'
d = d.replace(old2, new2, 1)

old3 = ('  if (cursor < json.size()) {\n'
        '    const char after = json[cursor];\n'
        "    if (after != ',' && after != '}' && after != ' ' && after != '" + BS + "t' && after != '" + BS + "r' &&\n"
        "        after != '" + BS + "n') {\n"
        '      return false;\n'
        '    }\n'
        '  }\n')
assert d.count(old3) == 1, 'old3'
d = d.replace(old3, '', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('rendezvous reverted for negative control')
