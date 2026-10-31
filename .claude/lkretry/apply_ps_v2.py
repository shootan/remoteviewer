import sys
d = r'D:\remote\remote\.claude\lkretry' + '\\'
p = r'C:\work\gnlink-wt\fix-capture-lifecycle\apps\native_poc\src\viewer_prelogin_login_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
v1 = open(d + 'ps_still_block_v1.txt', encoding='utf-8').read()
v2 = open(d + 'ps_still_block.cpp.txt', encoding='utf-8').read()
cover = open(d + 'ps_cover.cpp.txt', encoding='utf-8').read()
assert s.count(v1) == 1, 'v1 block'
s = s.replace(v1, v2)
anchor = '}  // namespace\n\nint wmain('
assert s.count(anchor) == 1, 'wmain anchor'
s = s.replace(anchor, cover + '\n' + anchor)
old = '    if (a == L"--no-alias") noAlias = true;\n'
assert s.count(old) == 1
s = s.replace(old, old + '    if (a == L"--no-cover") noCover = true;\n')
old = '  bool noAlias = false;      // ...served as "Default" (a host build without desktop_name_seam.hpp)\n'
assert s.count(old) == 1
s = s.replace(old, old + '  bool noCover = false;      // ...without the still cover (the desktop as it is)\n')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
