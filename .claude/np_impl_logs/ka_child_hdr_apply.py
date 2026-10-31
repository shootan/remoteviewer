import sys
base = 'C:/work/gnlink-wt/ka-pc/apps/native_poc/src/'
for name in ['ka_pc_managed_e2e_test.cpp', 'ka_pc_shell_e2e_test.cpp']:
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    i = s.index('// One child of this test: started suspended')
    end_marker = '  ~Child() { End(); }\n};\n'
    j = s.index(end_marker, i) + len(end_marker)
    s = s[:i] + ('// One child of this test: the shared ka-pc e2e Child (ka_pc_e2e_child.hpp) -- every set-up step checked, its own\n'
                 '// log written as it is read, its end judged for every child (N2), a host through the shared guard as it is.\n'
                 'using ka_pc_e2e::Child;\n') + s[j:]
    a = '#include "e2e_injection_guard.hpp"\n'
    assert s.count(a) == 1, name
    s = s.replace(a, a + '#include "ka_pc_e2e_child.hpp"\n')
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
    print(name, 'ok', crlf)
