import io

BS = chr(92)
p = 'apps/native_poc/src/update_endpoint.cpp'
d = io.open(p, encoding='utf-8').read()

start = d.find('  while (i < body.size() && body[i] != ' + chr(39) + '"' + chr(39) + ') {')
assert start > 0, 'start'
end = d.find('    out->push_back(body[i++]);', start)
assert end > start, 'end'

good = (
    '  while (i < body.size() && body[i] != ' + chr(39) + '"' + chr(39) + ') {\n'
    '    if (body[i] == ' + chr(39) + BS + BS + chr(39) + ' && i + 1 < body.size()) {\n'
    '      const char next = body[i + 1];\n'
    '      if (next == ' + chr(39) + 'n' + chr(39) + ') out->push_back(' + chr(39) + BS + 'n' + chr(39) + ');\n'
    '      else if (next == ' + chr(39) + 'r' + chr(39) + ') out->push_back(' + chr(39) + BS + 'r' + chr(39) + ');\n'
    '      else if (next == ' + chr(39) + 't' + chr(39) + ') out->push_back(' + chr(39) + BS + 't' + chr(39) + ');\n'
    '      else out->push_back(next);\n'
    '      i += 2;\n'
    '      continue;\n'
    '    }\n'
)
d = d[:start] + good + d[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('repaired')
