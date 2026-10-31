import io
import re

p = 'apps/native_poc/src/update_manifest_test.cpp'
d = io.open(p, encoding='utf-8').read()

# The heredoc turned "\n" inside C++ string literals into real newlines, splitting each literal
# across two lines. Rejoin: a line ending inside an open string literal, followed by a line that
# starts with a quote, is one literal that lost its escape.
lines = d.split('\n')
out = []
i = 0
fixed = 0
while i < len(lines):
    line = lines[i]
    # An odd number of unescaped quotes means the literal is still open at end of line.
    quotes = len(re.findall(r'(?<!\\)"', line))
    if quotes % 2 == 1 and i + 1 < len(lines):
        nxt = lines[i + 1]
        stripped = nxt.lstrip()
        if stripped.startswith('"'):
            # Put the escape back and glue the two halves together.
            out.append(line + '\\n' + stripped[1:] if stripped[1:].startswith('"') is False else line)
            # stripped begins with the closing quote of the same literal; drop that quote and keep
            # whatever followed it (";", "\n", etc).
            rest = stripped[1:]
            out[-1] = line + '\\n"' + rest
            i += 2
            fixed += 1
            continue
    out.append(line)
    i += 1

io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(out))
print('rejoined %d split literals' % fixed)
