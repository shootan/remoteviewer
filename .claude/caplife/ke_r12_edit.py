p = r'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
old = '''def _run(args, timeout):
    """AVD r11: one bounded call for a sample -- (rc, stdout, ms); a timeout / error is rc None, never an exception."""
    t0 = time.monotonic()
    import tempfile
    # stdout into a file, not a pipe: on the deadline the child is ended and this returns -- a grandchild (a wrapper's
    # interpreter, an adb server it starts) holding a pipe open cannot keep the sample waiting
    with tempfile.TemporaryFile() as out:
        try:
            r = subprocess.run(args, stdout=out, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, timeout=timeout)
            out.seek(0)
            return r.returncode, out.read().decode('utf-8', errors='replace'), int((time.monotonic() - t0) * 1000)
        except Exception as e:  # noqa: BLE001 -- recorded as a miss
            return None, f'{type(e).__name__}', int((time.monotonic() - t0) * 1000)
'''
new = '''_RUN_SEQ = [0]


def _run(args, timeout):
    """AVD r11: one bounded call for a sample -- (rc, stdout, ms); a timeout / error is rc None, never an exception.
    stdout goes into a file, not a pipe: on the deadline the child is ended and this returns -- a grandchild (a
    wrapper's interpreter, an adb server it starts) holding a pipe open cannot keep the sample waiting.
    AVD r12 (Codex codex_apk_inbox_plan_src_r2.md N1): that file is THIS run's own -- <OUT>/sample_io/<pid>-<n>.out,
    created new (an existing name is refused, never reused) and KEPT (no delete, no overwrite, no %TEMP% fallback)."""
    t0 = time.monotonic()
    io_dir = os.path.join(OUT, 'sample_io')
    os.makedirs(io_dir, exist_ok=True)
    _RUN_SEQ[0] += 1
    path = os.path.join(io_dir, f'{os.getpid()}-{_RUN_SEQ[0]:06d}.out')
    try:
        out = open(path, 'xb')  # CreateNew
    except Exception as e:  # noqa: BLE001 -- recorded as a miss; nothing else is tried
        return None, f'stdout file not new: {type(e).__name__}', int((time.monotonic() - t0) * 1000)
    with out:
        try:
            r = subprocess.run(args, stdout=out, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, timeout=timeout)
            rc = r.returncode
        except Exception as e:  # noqa: BLE001 -- recorded as a miss
            return None, f'{type(e).__name__}', int((time.monotonic() - t0) * 1000)
    with open(path, 'rb') as f:
        return rc, f.read().decode('utf-8', errors='replace'), int((time.monotonic() - t0) * 1000)
'''
assert s.count(old) == 1
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

t = r'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke_sample_test.py'
raw = open(t, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
for a, b in (
    ("import tempfile\n", ""),
    ("tmp = tempfile.mkdtemp(prefix='ke_sample_test_', dir=os.path.join(E, 'runs'))\n",
     "# r12: this run's own new folder under runs\\ (no tempfile, no %TEMP%; kept)\n"
     "tmp = os.path.join(E, 'runs', f'ke_sample_test-{time.strftime(\"%Y%m%d%H%M%S\")}-{os.getpid()}')\n"
     "os.makedirs(tmp)  # refused if it exists\n"),
):
    assert s.count(a) == 1, a
    s = s.replace(a, b)
# the boundary itself checked: every sample's stdout file is under this run's OUT, kept
anchor = "ns['_ps'] = real_ps\n"
assert s.count(anchor) == 1
s = s.replace(anchor, '''io = os.path.join(tmp, 'avd', 'sample_io')
kept = sorted(os.listdir(io)) if os.path.isdir(io) else []
check("r12: every call's stdout file is this run's own (<OUT>/sample_io), created new and kept",
      len(kept) >= 6 and all(re.match(r'^\\d+-\\d{6}\\.out$', k) for k in kept), f'{len(kept)} files in {io}')
''' + anchor)
if crlf:
    s = s.replace('\n', '\r\n')
open(t, 'wb').write(s.encode('utf-8'))
print('ok')
