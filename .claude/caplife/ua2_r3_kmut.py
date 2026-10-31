import sys, shutil
p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/DesktopBackendPresentation.kt'
bak = 'D:/remote/remote/.claude/caplife/apk/presentation.ua2r3.bak'
if sys.argv[1] == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    # the r2 rules: handled = applied; no request = the host's own at once
    a = '''        if (effective != status.requested && status.active == effective) return view(State.POLICY_REPLACED, handledLit, status, reasons)
        if (status.active != status.requested) return view(State.RECEIVED, handledLit, status, reasons)
'''
    b = '''            return if (noStateYet) view(State.CHECKING) else view(State.IDLE, handledLit, status, reasons)'''
    assert s.count(a) == 1 and s.count(b) == 1
    s = s.replace(a, '').replace(b, '''            return view(State.IDLE, handledLit, status, reasons)  // MUTANT''')
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(sys.argv[1], 'ok')
