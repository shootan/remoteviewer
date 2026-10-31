# usage: python -I ua2_kmut.py <apply|revert>  -- the r1 completion rule (requested != asked) in place of the seq match
import sys
p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/DesktopBackendPresentation.kt'
good = '''        val pending = status.intentAsked == 0L || status.intentSent < status.intentAsked || status.intentSentSeq == 0L ||
            status.seq < status.intentSentSeq || status.flags and FLAG_NOT_YET_APPLIED != 0
        if (pending) return view(State.APPLYING, null, status, reasons)'''
bad = '''        val pending = status.requested != asked  // MUTANT ua2-N1 (the r1 rule)
        if (pending) return view(State.APPLYING, null, status, reasons)'''
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a, b = (good, bad) if sys.argv[1] == 'apply' else (bad, good)
assert s.count(a) == 1
s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print(sys.argv[1], 'ok')
