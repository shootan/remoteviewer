B = chr(92)
p = 'C:' + B + 'work' + B + 'gnlink-wt' + B + 'ka-pc' + B + 'apps' + B + 'native_poc' + B + 'src' + B + 'ka_pc_shell_logic_test.cpp'
s = open(p, encoding='utf-8').read()
fixes = [
    ("parent.back() == L'" + B + "'", "parent.back() == L'" + B + B + "'"),
    ('L"D:' + B + 'remote' + B + 'remote' + B + '.claude' + B + 'test-tmp"',
     'L"D:' + B + B + 'remote' + B + B + 'remote' + B + B + '.claude' + B + B + 'test-tmp"'),
    ('L"' + B + 'run-"', 'L"' + B + B + 'run-"'),
    ('L"' + B + 'shelldata"', 'L"' + B + B + 'shelldata"'),
]
for a, c in fixes:
    n = s.count(a)
    assert n == 1, (a, n)
    s = s.replace(a, c)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
