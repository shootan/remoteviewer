import sys, shutil, os
src = 'C:\\work\\gnlink-wt\\fix-lk-retry\\apps\\native_poc\\src\\'
bak = r'D:\remote\remote\.claude\lkretry\r3_mutant_bak' + '\\'
os.makedirs(bak, exist_ok=True)
MUTANTS = {
    # N1 (r3) reverted: no gate at the write, no adoption fence.
    'M7': [
        ('host_unlock_relay.cpp',
         '    if (write && gate) {',
         '    if (false && /* MUTANT M7 */ write && gate) {'),
        ('host_unlock_relay.cpp',
         '        if (!inTime) {',
         '        if (false && /* MUTANT M7 */ !inTime) {'),
    ],
    # N2-a reverted: a password sent but unanswered is forgotten.
    'M8': [
        ('viewer_control_client.cpp',
         '          if (!exchanged && progress.sealSent) {',
         '          if (false && /* MUTANT M8 */ !exchanged && progress.sealSent) {'),
    ],
    # N2-b reverted: "the earlier wait is over" believed before the purpose check.
    'M9': [
        ('viewer_unlock.cpp',
         '      if (ch.reserved != kUnlockPurposeLockCred) {\n        set_status("challenge purpose mismatch");\n        return false;\n      }\n      if (progress) progress->supersededAnswer = true;',
         '      if (progress) progress->supersededAnswer = true;  // MUTANT M9'),
        ('viewer_unlock.cpp',
         '  // lockscreen-cred r1: a lock-screen challenge must say it was issued for that purpose -- the password is\n  // never sealed for a challenge of another purpose.\n  if (as == SealAs::LockCred && ch.reserved != kUnlockPurposeLockCred) {',
         '  if (progress) progress->challengeIssued = true;  // MUTANT M9: before the purpose check\n  if (as == SealAs::LockCred && ch.reserved != kUnlockPurposeLockCred) {'),
    ],
}
mode, name = sys.argv[1], sys.argv[2]
files = sorted(set(f for f, _, _ in MUTANTS[name]))
if mode == 'apply':
    for f in files:
        shutil.copy2(src + f, bak + f)
    try:
        for f, old, new in MUTANTS[name]:
            s = open(src + f, 'rb').read().decode('utf-8')
            crlf = '\r\n' in s
            s = s.replace('\r\n', '\n')
            assert s.count(old) == 1, (f, s.count(old), old[:80])
            s = s.replace(old, new)
            if crlf:
                s = s.replace('\n', '\r\n')
            open(src + f, 'wb').write(s.encode('utf-8'))
    except AssertionError:
        for f in files:
            shutil.copyfile(bak + f, src + f)
            os.remove(bak + f)
        raise
else:
    for f in files:
        shutil.copyfile(bak + f, src + f)
        os.remove(bak + f)
print(mode, name, 'ok')
