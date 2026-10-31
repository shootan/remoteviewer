import shutil
import sys

p = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/native_video_client_session.cpp'
bak = 'D:/remote/remote/.claude/caplife/native_video_client_session.cpp.xfence_good'
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
A = '  if (!FenceMouseXButtonsForHost(HostTakesMouseXButtons(role.known, role.flags), kind, keyCode, &buttons)) {'
B = '      if (!FenceMouseXButtonsAction(HostTakesMouseXButtons(role.known, role.flags), &action)) {'
assert s.count(A) == 1 and s.count(B) == 1
s = s.replace(A, '  if (false) {  // MUTANT M1/M2: queue-time X fence removed')
if mode == 'm2':
    s = s.replace(B, '      if (false) {  // MUTANT M2: send-time X fence removed')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('mutant', mode)
