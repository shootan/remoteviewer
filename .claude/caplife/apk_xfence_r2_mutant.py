import shutil
import sys

R = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/'
B = 'D:/remote/remote/.claude/caplife/'
files = {'h': 'client_mouse_xbutton_fence.hpp', 'c': 'native_video_client_session.cpp'}
mode = sys.argv[1]
if mode == 'save':
    for k, f in files.items():
        shutil.copyfile(R + f, B + f + '.r2_good')
    print('saved')
    sys.exit(0)
if mode == 'restore':
    for k, f in files.items():
        shutil.copyfile(B + f + '.r2_good', R + f)
    print('restored')
    sys.exit(0)
M = {
    # R1: the entry side forgets the press (stateless capability only)
    'r1': ('h', '        entryRefused_[s] = !hostTakesX;  // a down: a new press, judged now',
           '        entryRefused_[s] = false;  // MUTANT r1'),
    # R2: the send side forgets a dropped down
    'r2': ('h', '          send_[s] = SendState::Dropped;\n          ok = false;',
           '          ok = false;  // MUTANT r2'),
    # R3: the ledger is not reset with the connection
    'r3': ('c', '    mouseXLedger_.Reset();  // r2: no press record carried into another connection',
           '    // MUTANT r3: no reset'),
}
which, a, b = M[mode]
for k, f in files.items():  # every file from its good copy first (one mutant at a time)
    shutil.copyfile(B + f + '.r2_good', R + f)
src = open(B + files[which] + '.r2_good', encoding='utf-8', newline='').read()
crlf = '\r\n' in src
t = src.replace('\r\n', '\n') if crlf else src
assert t.count(a) == 1, mode
t = t.replace(a, b)
if crlf:
    t = t.replace('\n', '\r\n')
open(R + files[which], 'w', encoding='utf-8', newline='').write(t)
print('mutant', mode)
