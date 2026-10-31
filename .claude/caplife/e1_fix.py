p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\host_capture_lifecycle_ua_e2e_test.cpp'
B = chr(92)
s = open(p, 'rb').read().decode('utf-8')
bad1 = 'shotsDir + L"' + B + 'E1-UA-target-red.bmp"'
good1 = 'shotsDir + L"' + B + B + 'E1-UA-target-red.bmp"'
assert s.count(bad1) == 1
s = s.replace(bad1, good1)
i = s.index('<< mc0.w << "x" << mc0.h << "')
j = s.index(';', i)
s = s[:i] + '<< mc0.w << "x" << mc0.h << "' + B + 'n"' + s[j:]
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
