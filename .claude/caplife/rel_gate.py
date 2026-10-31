p = 'C:\\work\\gnlink-wt\\rel-0163\\automation\\gnlink_check_fixed_server.py'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:60])
    s = s.replace(old, new)

rep('''    'GNLINK_VIEWER_TEST_',        # the same seam's environment (PRESENT_FAIL_FILE)
]''', '''    'GNLINK_VIEWER_TEST_',        # the same seam's environment (PRESENT_FAIL_FILE)
    # rel 0.2.163 (LK-UX + capture-lifecycle): the test-build host's own log tag for its seams
    # (host_stage_backend.cpp: "[native-video-host][test-seam] ..."), the LK relay's test hooks
    # (SetRetryIoHookForTest / KeptEntriesForTest -- a name that reaches a binary as text means a hook
    # was wired into something that prints or exports it), and the capture lib's test-only still-screen
    # modes as the test host spells them in its environment (=nofirst / =pointerfirst).
    '][test-seam]',
    'ForTest',
    'pointerfirst',
]

# rel 0.2.163: every test-build environment name LK-UX and capture-lifecycle added, each checked ALONE
# (ascii and wide) by the self-test -- the prefixes above cover them, this keeps a prefix change from
# silently uncovering one.
RELEASE_0163_SEAM_NAMES = [
    'GNLINK_STREAM_TEST_DESKTOP_ALIAS',
    'GNLINK_STREAM_TEST_STILL_SCREEN',
    'GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT',
    'GNLINK_STREAM_TEST_WGC_STILL',
    'GNLINK_STREAM_TEST_FAIL_RESTART_FILE',
    'GNLINK_STREAM_TEST_LOGON_DESKTOP',
    'GNLINK_STREAM_TEST_UNLOCK_PIPE',
    'GNLINK_VIEWER_TEST_STATUS_POLLS',
    'GNLINK_VIEWER_TEST_SEAL_FAIL',
]''')

rep('''        # sh-filter r2 (H4): the APK seam check on dex bytes''', '''        # rel 0.2.163: each LK-UX / capture-lifecycle test-build name alone, ascii and wide, in the
        # streaming host and the viewer.
        for seam in RELEASE_0163_SEAM_NAMES:
            for enc in ('ascii', 'utf-16-le'):
                target = 'GNLinkViewer.exe' if seam.startswith('GNLINK_VIEWER_') else 'GNLinkStream.exe'
                outer.check(verdict(make('r0163-%s-%s' % (seam, enc), {
                    target: lambda b, s=seam, e=enc: b + s.encode(e)})) == 1,
                    '%s carrying %s alone (%s) fails' % (target, seam, enc))

        # sh-filter r2 (H4): the APK seam check on dex bytes''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
