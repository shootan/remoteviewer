import os
base = r'D:\remote\remote\.claude\edits'
s = open(os.path.join(base, 'step21b_p11_rest.py'), 'rb').read().decode('utf-8')
# drop the already-applied host_capture_session.hpp block
start = s.index("edit(S + r'\\host_capture_session.hpp', [")
end = s.index("edit(S + r'\\host_capture_session.cpp', [")
s = s[:start] + s[end:]
old = '(r"""            << " size=" << width << "x" << height << "\\n";""",'
assert s.count(old) == 1, s.count(old)
new = '(r"""            << " size=" << width << "x" << height\n            << "\\n";""",'
s = s.replace(old, new)
open(os.path.join(base, 'step21c_p11_rest.py'), 'wb').write(s.encode('utf-8'))
print('step21c written')
