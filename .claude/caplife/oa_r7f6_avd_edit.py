p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''def cursor():''', '''def applog_snapshot():
    """r7f (6, Codex codex_old_apk_lane_src_r7d_r2.md N2): the app log as ONE read that is known to have worked --
    (True, text) only when the pull succeeded and the file was read; else (False, why). applog() turns a failure into
    '' (its callers search it); a check of "nothing changed" must not read that '' as a valid, unchanged log."""
    p = os.path.join(OUT, '_snap.log')
    try:
        if os.path.exists(p):
            os.remove(p)
        ka.adb('pull', ka.LOG_PATH, p)
        return True, open(p, encoding='utf-8', errors='replace').read()
    except Exception as e:  # noqa: BLE001 -- any failure is "no snapshot", never "no change"
        return False, f'app log not read ({type(e).__name__}: {str(e)[:80]})'


def host_snapshot():
    """r7f (6): the host log as one read known to have worked -- (True, text) or (False, why)."""
    try:
        ready = open(os.path.join(ROOT, 'a', 'directoryhost_ready.txt'), encoding='utf-8').read().strip()
        m = re.match(r'^ready run=(\\S+) harness_pid=\\d+ host_pid=\\d+ host_log=(\\S+)$', ready)
        if not m or m.group(1) != os.path.basename(os.path.normpath(ROOT)) + '-a':
            return False, f"the host's readiness is not this run's: {ready[:80]}"
        return True, open(m.group(2), encoding='utf-8', errors='replace').read()
    except Exception as e:  # noqa: BLE001
        return False, f'host log not read ({type(e).__name__}: {str(e)[:80]})'


def cursor():''')

rep('''PICK_MARK = []  # [app log cursor, host log length, pinned id] at the last successful pick
''', '''PICK_MARK = []  # [app log cursor, host log length, pinned id, host prefix sha256] at the last successful pick


def set_pick_mark(pinned):
    """r7f (6): the marker "the same viewer, the same connection" is measured from -- registered ONLY from two reads that
    worked: the app log (its last line, which must be non-empty) and this run's host log (non-empty; its length and the
    sha256 of everything up to it, so a later read can show it CONTINUES this log). -> (ok, why)."""
    PICK_MARK[:] = []
    ok_a, app = applog_snapshot()
    ok_h, hl = host_snapshot()
    lines = [l for l in app.splitlines() if l.strip()] if ok_a else []
    if not ok_a or not lines:
        return False, app if not ok_a else 'app log empty'
    if not ok_h or not hl:
        return False, hl if not ok_h else 'host log empty'
    PICK_MARK[:] = [lines[-1], len(hl), pinned, hashlib.sha256(hl.encode('utf-8')).hexdigest()]
    return True, f'app marker {lines[-1][:40]!r}, host offset {len(hl)}'
''')

rep('''    if not PICK_MARK:
        return False, 'no pick to compare with'
    app = since(PICK_MARK[0])
    hl = host_log()[PICK_MARK[1]:]''', '''    if not PICK_MARK:
        return False, 'no pick to compare with'
    # r7f (6): only snapshots that were really read, the app marker still in the app log, the host log at least as long
    # as at the pick and beginning with exactly what it held then -- else "cannot tell", never "nothing changed"
    ok_a, app_all = applog_snapshot()
    if not ok_a:
        return False, f'no app snapshot: {app_all}'
    i = app_all.rfind(PICK_MARK[0])
    if i < 0:
        return False, 'the pick marker is gone from the app log (rotated / replaced): cannot tell'
    ok_h, host_all = host_snapshot()
    if not ok_h:
        return False, f'no host snapshot: {host_all}'
    if len(host_all) < PICK_MARK[1]:
        return False, f'the host log shrank ({len(host_all)} < {PICK_MARK[1]} at the pick): cannot tell'
    if hashlib.sha256(host_all[:PICK_MARK[1]].encode('utf-8')).hexdigest() != PICK_MARK[3]:
        return False, 'the host log does not continue the one read at the pick (replaced): cannot tell'
    app = app_all[i + len(PICK_MARK[0]):]
    hl = host_all[PICK_MARK[1]:]''')

rep('''    vd = re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', since(n0))
    PICK_MARK[:] = [cursor(), len(host_log()), pinned]  # r8: what "the same viewer, the same connection" is measured from
''', '''    vd = re.findall(r'\\[video_debug\\][^\\n]* out=(\\d+)', since(n0))
    marked, mwhy = set_pick_mark(pinned)  # r8 / r7f (6): what "the same viewer, the same connection" is measured from
    if not marked:
        verdict(f'{tag}: the pinned window picked (id {pinned}, Windows tab) -> first frame', False,
                f'{detail[:200]} -- but no marker could be taken: {mwhy}')
        return None
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
