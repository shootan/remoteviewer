import re
for name in ['ka_pc_shell_e2e_test.cpp', 'ka_pc_managed_e2e_test.cpp']:
    p = 'C:\\work\\gnlink-wt\\ka-pc\\apps\\native_poc\\src\\' + name
    b = open(p, 'rb').read().decode('utf-8')
    n0 = b.count('viewer connects\r\n"') + b.count('viewer connects\n"')
    b = b.replace('viewer connects\r\n"', 'viewer connects' + chr(92) + 'n"').replace('viewer connects\n"', 'viewer connects' + chr(92) + 'n"')
    open(p, 'wb').write(b.encode('utf-8'))
    print(name, n0)
