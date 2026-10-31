import sys, shutil, os
src = 'C:\\work\\gnlink-wt\\fix-lk-retry\\apps\\native_poc\\src\\'
bak = r'D:\remote\remote\.claude\lkretry\r2_mutant_bak' + '\\'
os.makedirs(bak, exist_ok=True)
MUTANTS = {
    # N1 reverted: the caller's timeout withdraws nothing, and the worker runs the retry regardless.
    'M5': [
        ('host_unlock_relay.cpp',
         '      if (!live || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs)) {',
         '      if (false && /* MUTANT M5 */ (!live || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs))) {'),
        ('host_unlock_relay.cpp',
         '        if (!stillLive || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs)) {',
         '        if (false && /* MUTANT M5 */ (!stillLive || (cmd.deadlineMs != 0 && GetTickCount64() >= cmd.deadlineMs))) {'),
        ('host_unlock_relay.cpp',
         '    liveTx_.erase(key);\n    challengeResults_.erase(key);\n    return false;',
         '    return false;  // MUTANT M5: nothing withdrawn'),
        ('host_unlock_relay.cpp',
         '      if (it->op == Op::LockCredRetry && it->cookie == clientSessionCookie && it->challenge.requestId == req.requestId) {',
         '      if (false && /* MUTANT M5 */ it->op == Op::LockCredRetry) {'),
        ('host_unlock_relay.cpp',
         '        if (cmd.deadlineMs != 0) {\n          const uint64_t now = GetTickCount64();\n          const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 1;\n          supersedeBudget',
         '        if (false /* MUTANT M5 */) {\n          const uint64_t now = GetTickCount64();\n          const uint64_t left = cmd.deadlineMs > now ? cmd.deadlineMs - now : 1;\n          supersedeBudget'),
        ('host_unlock_relay.cpp',
         '      if (cmd.deadlineMs != 0) {  // lk-ux-retry r2: a retry\'s fresh challenge ends with the retry',
         '      if (false) {  // MUTANT M5'),
    ],
    # N2 reverted: "not 0 and not 18" means the earlier wait ended; a Superseded is said as "2 minutes".
    'M6': [
        ('viewer_control_client.cpp',
         '          if (supersedes != 0 && priorEnded) {',
         '          if (supersedes != 0 && stage != 0 && stage != 18) {  // MUTANT M6 (priorEnded unused)\n            (void)priorEnded;'),
        ('viewer_control_client.cpp',
         '        if (stage != static_cast<uint16_t>(remote60::native_poc::UnlockStage::SessionUnlocked) &&\n            stage != static_cast<uint16_t>(remote60::native_poc::UnlockStage::Superseded)) {',
         '        if (stage != static_cast<uint16_t>(remote60::native_poc::UnlockStage::SessionUnlocked)) {  // MUTANT M6'),
    ],
}
mode, name = sys.argv[1], sys.argv[2]
files = sorted(set(f for f, _, _ in MUTANTS[name]))
if mode == 'apply':
    for f in files:
        shutil.copy2(src + f, bak + f)
    for f, old, new in MUTANTS[name]:
        s = open(src + f, 'rb').read().decode('utf-8')
        crlf = '\r\n' in s
        s = s.replace('\r\n', '\n')
        assert s.count(old) == 1, (f, s.count(old), old[:80])
        s = s.replace(old, new)
        if crlf:
            s = s.replace('\n', '\r\n')
        open(src + f, 'wb').write(s.encode('utf-8'))
else:
    for f in files:
        shutil.copyfile(bak + f, src + f)
        os.remove(bak + f)
print(mode, name, 'ok')
