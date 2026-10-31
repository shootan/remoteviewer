p = 'C:/work/gnlink-wt/feat-apk-0233/apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

# resolve the five hunks in order: which side, and a per-hunk fix-up
choices = ['head', 'theirs', 'head_binding', 'both', 'theirs']
out = []
i = 0
k = 0
lines = s.split('\n')
while i < len(lines):
    l = lines[i]
    if l.startswith('<<<<<<< HEAD'):
        head, theirs = [], []
        i += 1
        while lines[i] != '=======':
            head.append(lines[i]); i += 1
        i += 1
        while not lines[i].startswith('>>>>>>> 6d85ce2'):
            theirs.append(lines[i]); i += 1
        c = choices[k]; k += 1
        if c == 'head':
            out += head
        elif c == 'theirs':
            out += theirs
        elif c == 'head_binding':
            out += [x.replace('nativeQueryDesktopCaptureBackend()', 'nativeQueryDesktopCaptureBackend(activeBinding)') for x in head]
        elif c == 'both':
            out += head + theirs
        i += 1
        continue
    out.append(l)
    i += 1
assert k == 5, k
s = '\n'.join(out)

# ua2-apk: nothing re-sent on (re)activation; the per-connection choice starts over with the PC on screen
for old, new in [
    ('''        connectionPathKind = p.pathKind
        connectFlowActive = true
        pendingRuntimeConfigSync = true
        pendingDesktopBackendSync = true''', '''        connectionPathKind = p.pathKind
        connectFlowActive = true
        pendingRuntimeConfigSync = true
        resetSessionDesktopBackend("commit")  // ua2-apk: this connection's own choice, none yet (nothing re-sent)'''),
    ('''        // the user's current quality / capture settings go to THIS PC (multi §5: settings are tagged with it)
        pendingRuntimeConfigSync = true
        pendingDesktopBackendSync = true''', '''        // the user's current quality settings go to THIS PC (multi §5: settings are tagged with it); the capture backend
        // is per connection (ua2-apk) -- not re-sent, the screen shows what this PC reports
        pendingRuntimeConfigSync = true
        resetSessionDesktopBackend("park_reuse")'''),
    ('''        if (NativeSessionBridge.nativeRequestDesktopCaptureBackend(choice.code)) {''',
     '''        if (NativeSessionBridge.nativeRequestDesktopCaptureBackend(activeBinding, choice.code)) {'''),
    ('''        val status = if (connected) DesktopBackendPresentation.HostStatus.of(NativeSessionBridge.nativeGetDesktopBackendStatus()) else null''',
     '''        val status = if (connected) DesktopBackendPresentation.HostStatus.of(NativeSessionBridge.nativeGetDesktopBackendStatus(activeBinding)) else null'''),
]:
    assert s.count(old) == 1, (s.count(old), old[:70])
    s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
