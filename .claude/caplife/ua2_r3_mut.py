import sys, shutil
p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/src/native_video_client_session.hpp'
bak = 'D:/remote/remote/.claude/caplife/apk/session_hpp.ua2r3.bak'
if sys.argv[1] == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    old = '''    const auto role = hostRole_.Load();
    if (stopPoint) stopPoint();
    return {role.known, role.known && (role.flags & kCaptureFlagDesktopBackendStatusV1) != 0};'''
    new = '''    const bool answers = desktopBackend_.HostAnswers();  /* MUTANT r2 JNI order: answers first, known later */
    if (stopPoint) stopPoint();
    return {hostRole_.Load().known, answers};'''
    s2 = s.replace(old.replace('\n', '\r\n'), new) if '\r\n' in s else s.replace(old, new)
    assert s2 != s
    open(p, 'wb').write(s2.encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(sys.argv[1], 'ok')
