import shutil
import sys

p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/e2e_injection_guard.hpp'
bak = 'D:/remote/remote/.claude/caplife/e2e_injection_guard.hpp.r8_good'
mode = sys.argv[1]
if mode == 'save':
    shutil.copyfile(p, bak)
    print('saved')
    sys.exit(0)
if mode == 'restore':
    shutil.copyfile(bak, p)
    print('restored')
    sys.exit(0)
s = open(bak, encoding='utf-8', newline='').read()
M = {
    # a: any read failure taken as the end of output (the r7 reader)
    'a': ('      if (e == ERROR_BROKEN_PIPE) return E2eReadEnd::Eof;', '      return E2eReadEnd::Eof;  // MUTANT a'),
    # b: complete is enough (no OFF lines required in the final text)
    'b': ('  if (st == 0) {\n    return "the final output (', '  if (false) {  // MUTANT b\n    return "the final output ('),
    # c: an open failure taken as an empty, complete output
    'c': ('    f.why = "its log could not be opened (error " + std::to_string(GetLastError()) + ")";\n    return f;',
          '    f.complete = true;  // MUTANT c\n    return f;'),
    # d: the startup-fault verdict without its power requirement
    'd': ('  if (power != 1) {\n    return "the final output (', '  if (false) {  // MUTANT d\n    return "the final output ('),
}
a, b = M[mode]
assert s.count(a) == 1, mode
open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))
print('mutant', mode)
