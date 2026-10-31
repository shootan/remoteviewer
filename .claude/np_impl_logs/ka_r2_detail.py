import re
p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_shell_e2e_test.cpp'
s = open(p, encoding='utf-8').read()
# check(PRED, LABEL, DETAIL) where PRED waits: the detail must be read AFTER the wait (argument order is unspecified)
n0 = s.count('check(')
s = s.replace('void check(bool ok, const std::string& label, const std::string& detail = {}) {',
              '''// The detail is computed after the predicate: a function argument's evaluation order is unspecified (MSVC goes right
// to left), so a detail passed beside a waiting predicate would describe the moment BEFORE the wait.
void check_after(const std::function<bool()>& predicate, const std::string& label, const std::function<std::string()>& detail);
void check(bool ok, const std::string& label, const std::string& detail = {}) {''', 1)
s = s.replace('''std::wstring gRoot;   // with a trailing backslash''', '''void check_after(const std::function<bool()>& predicate, const std::string& label, const std::function<std::string()>& detail) {
  const bool ok = predicate();
  check(ok, label, detail());
}

std::wstring gRoot;   // with a trailing backslash''', 1)
pat = re.compile(r'check\((wait_card\([^;]*?\)(?: && [^,]*?)?), ((?:std::string\([^)]*\) \+ )?"[^"]*"(?: \+ "[^"]*")?(?:,)?)\s*(card_state\([^)]*\))\);')
count = 0
def fix(m):
    global count
    count += 1
    label = m.group(2).rstrip(',').strip()
    return 'check_after([&] { return ' + m.group(1) + '; }, ' + label + ', [&] { return ' + m.group(3) + '; });'
s2 = pat.sub(fix, s)
open(p, 'w', encoding='utf-8', newline='\n').write(s2)
print('rewritten', count)
for line in s2.splitlines():
    if 'card_state(' in line and 'check(' in line:
        print('LEFT:', line.strip()[:160])
