# -*- coding: utf-8 -*-
"""Replaces the mangled char literal with a real escape sequence.

Built with chr(92) rather than written as a backslash: every attempt to pass this through a shell
heredoc turned the escape into the byte it denotes, which is how a NUL ended up compiled into a
character literal.
"""
import io

BS = chr(92)
p = 'apps/native_poc/src/update_stop_process_test.cpp'
d = io.open(p, 'rb').read()

good = ("    cmdO.push_back(L'" + BS + "0');").encode('utf-8')

# Whatever is there now, the line is identifiable by its neighbours.
lines = d.split(b'\r\n') if b'\r\n' in d else d.split(b'\n')
sep = b'\r\n' if b'\r\n' in d else b'\n'
hits = 0
for i, line in enumerate(lines):
    if b'cmdO.push_back(' in line:
        lines[i] = good
        hits += 1
assert hits == 1, hits
io.open(p, 'wb').write(sep.join(lines))
print('fixed, hits =', hits)
