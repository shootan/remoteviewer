p = r'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke_avd.py'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:90])
    s = s.replace(a, b)


# ---- sample(): every call bounded and timed, misses recorded; %CPU found by the header; qemu by its port
start = s.index('_qemu_pid = None\n')
end = s.index('def env_note(tag):')
s = s[:start] + '''_QEMU = {'pid': None, 'start': None, 'why': 'not looked up'}


def _run(args, timeout):
    """AVD r11: one bounded call for a sample -- (rc, stdout, ms); a timeout / error is rc None, never an exception."""
    t0 = time.monotonic()
    try:
        r = subprocess.run(args, capture_output=True, timeout=timeout)
        return r.returncode, r.stdout.decode('utf-8', errors='replace'), int((time.monotonic() - t0) * 1000)
    except Exception as e:  # noqa: BLE001 -- recorded as a miss
        return None, f'{type(e).__name__}', int((time.monotonic() - t0) * 1000)


def _ps(cmd, timeout=8):
    return _run(['powershell', '-NoProfile', '-Command', cmd], timeout)


def qemu_identify():
    """AVD r11 (Codex codex_apk_inbox_plan_src_r1.md r10-1): the qemu of THIS emulator, emulator-5554 -- the process
    listening on its console port 5554, by pid AND start time; a qemu name, exactly one owner. Anything else: unknown
    (and its CPU left out of every comparison) -- never the first qemu found (another emulator / LDPlayer)."""
    rc, out, _ = _ps("$c = @(Get-NetTCPConnection -LocalPort 5554 -State Listen -ErrorAction SilentlyContinue | "
                     "Select-Object -ExpandProperty OwningProcess -Unique); if ($c.Count -ne 1) { 'n=' + $c.Count } else { "
                     "$p = Get-Process -Id $c[0]; \\"$($p.Id)|$($p.ProcessName)|$($p.StartTime.ToString('o'))\\" }")
    line = out.strip()
    m = re.match(r'^(\\d+)\\|(qemu-system[^|]*)\\|(\\S+)$', line)
    if rc == 0 and m:
        _QEMU.update(pid=m.group(1), start=m.group(3), why='console port 5554 owner')
    else:
        _QEMU.update(pid=None, start=None, why=f'unknown ({line[:80] or "no answer"})')
    step(f'  qemu of emulator-5554: pid={_QEMU["pid"] or "-"} start={_QEMU["start"] or "-"} ({_QEMU["why"]})')


def sample(tag):
    """AVD r10/r11: one sample, the same in every arm -- the app's memory (dumpsys meminfo TOTAL PSS), the emulator's
    load, the app's CPU (top: the %CPU column found by its header), and on this PC the emulator's qemu (identified by
    emulator-5554's console port, pid + start time) total CPU time. r11: every call has its own deadline; the sample's
    own length and every miss are recorded (a miss is '?', never a 0). Recorded only."""
    t0 = time.monotonic()
    miss = []

    def adbq(*a, timeout=6):
        rc, out, ms = _run([ka_avd.ADB, '-s', ka_avd.SERIAL, *a], timeout)
        if rc != 0:
            miss.append(f'{a[1] if len(a) > 1 else a[0]}:{rc}')
        return out if rc == 0 else ''

    mi = adbq('shell', 'dumpsys', 'meminfo', PKG)
    pss = re.search(r'TOTAL PSS:\\s+(\\d+)|TOTAL\\s+(\\d+)', mi)
    pss_kb = (pss.group(1) or pss.group(2)) if pss else '?'
    load = adbq('shell', 'cat', '/proc/loadavg').strip() or '?'
    pid = adbq('shell', 'pidof', PKG).strip().split()
    cpu = '?'
    if pid:
        top = adbq('shell', 'top', '-b', '-n', '1', '-p', pid[0])
        header = next((l for l in top.splitlines() if '%CPU' in l), '')
        row = next((l for l in top.splitlines() if l.split()[:1] == [pid[0]]), '')
        if header and row:
            cols = header.replace('[', ' ').replace(']', ' ').split()
            vals = row.split()
            idx = [i for i, c in enumerate(cols) if c.endswith('%CPU')]
            if idx and len(vals) == len(cols):
                cpu = vals[idx[0]]
        if cpu == '?':
            miss.append('top:format')
    qms = '?'
    if _QEMU['pid']:
        rc, out, _ = _ps(f"$p = Get-Process -Id {_QEMU['pid']} -ErrorAction SilentlyContinue; if ($p -and "
                         f"$p.StartTime.ToString('o') -eq '{_QEMU['start']}') {{ [int64]$p.TotalProcessorTime.TotalMilliseconds }}")
        qms = out.strip() if rc == 0 and out.strip().isdigit() else '?'
        if qms == '?':
            miss.append('qemu:gone-or-changed')
    else:
        miss.append('qemu:unknown')
    step(f'SAMPLE {tag} epoch_ms={int(time.time() * 1000)} mono_ms={int(time.monotonic() * 1000)} '
         f'sample_ms={int((time.monotonic() - t0) * 1000)} app_pss_kb={pss_kb} app_cpu_pct={cpu} emu_loadavg="{load}" '
         f'qemu_cpu_ms={qms} app_pid={pid[0] if pid else "-"} miss=[{",".join(miss)}]')


def env_once():
    """AVD r10: the emulator's fixed shape, once per run (CPUs, memory, screen, GL driver) -- recorded only. r11: and
    which qemu is this emulator's."""
    nproc = adb('shell', 'nproc').strip()
    mem = adb('shell', 'cat', '/proc/meminfo').splitlines()[:1]
    size = adb('shell', 'wm', 'size').strip().replace('\\n', ' ')
    gles = re.findall(r'GLES:[^\\n]*', adb('shell', 'dumpsys', 'SurfaceFlinger'))[:1]
    step(f'  env[once]: nproc={nproc} {mem[0].strip() if mem else "?"} {size} {gles[0][:140] if gles else "GLES:?"}')
    qemu_identify()


''' + s[end:]

# ---- the watch: first-present -> watch-begin delay recorded; monotonic length; samples on a monotonic schedule;
# an early end stops the watch and nothing more is driven; no log -> not judged
rep("""    first_ms = select('E2 A (ordinary connect, sharpening pipeline ' + ('OFF)' if sh_off else 'DEFAULT)'))""",
    """    first_ms = select('E2 A (ordinary connect, sharpening pipeline ' + ('OFF)' if sh_off else 'DEFAULT)'))
    t_first = time.monotonic()  # AVD r11: the first frame was seen here (select returned)""")
rep("""    if first_ms is not None:
        n_watch = cursor()
        t_watch = time.time()
        segment('watch', 'begin')
        first_end = None
        next_sample = t_watch
        while time.time() - t_watch < WATCH_SEC:
            if time.time() >= next_sample:
                sample(f'watch+{time.time() - t_watch:.0f}s')
                next_sample += SAMPLE_SEC
            time.sleep(1)
            seen = re.findall(r'[^\\n]*\\[(?:keep_end|scene)\\][^\\n]*', since(n_watch))
            ends = [l for l in seen if '[keep_end]' in l or '[scene] HOSTS' in l]
            if ends and first_end is None:
                first_end = ends[0]
                step(f'  FIRST END at +{time.time() - t_watch:.1f}s of the watch: {first_end[:200]}')
        sample('watch-end')
        segment('watch', 'end')
        watched = since(n_watch)
        traces = len(re.findall(r'\\[liveness_trace\\]', watched))
        verdict(f'{tag} the session stayed up {WATCH_SEC} s after its first frame (no keep_end, still the viewer)',
                first_end is None, f'liveness_trace lines={traces}' + (f' first end: {first_end[:160]}' if first_end else ''))
        env_note(f'after the {WATCH_SEC} s watch')
    segment('list', 'begin')""", """    first_end = None
    if first_ms is not None:
        n_watch = cursor()
        t_watch = time.monotonic()
        step(f'  watch begins {int((t_watch - t_first) * 1000)} ms after the first frame was seen (select returned)')
        segment('watch', 'begin')
        next_sample = t_watch
        while time.monotonic() - t_watch < WATCH_SEC and first_end is None:
            if time.monotonic() >= next_sample:
                sample(f'watch+{time.monotonic() - t_watch:.0f}s')
                next_sample += SAMPLE_SEC
            time.sleep(1)
            seen = re.findall(r'[^\\n]*\\[(?:keep_end|scene)\\][^\\n]*', since(n_watch))
            ends = [l for l in seen if '[keep_end]' in l or '[scene] HOSTS' in l]
            if ends:
                first_end = ends[0]
                step(f'  FIRST END at +{time.monotonic() - t_watch:.1f}s of the watch: {first_end[:200]} -- the watch stops here')
        watched_ms = int((time.monotonic() - t_watch) * 1000)
        sample('watch-end')
        segment('watch', 'end')
        watched = since(n_watch)
        traces = len(re.findall(r'\\[liveness_trace\\]', watched))
        debug_lines = len(re.findall(r'\\[video_debug\\]', watched))
        if first_end is None and debug_lines == 0:
            # AVD r11: no log of the watch could be had -- "no end" is not inferred from silence
            verdict(f'{tag} the watch could be judged (the app log of the watch was obtained)', False,
                    f'video_debug lines=0 watched={watched_ms}ms -- not judged as "stayed up"')
        else:
            verdict(f'{tag} the session stayed up {WATCH_SEC} s after its first frame (no keep_end, still the viewer)',
                    first_end is None, f'watched={watched_ms}ms liveness_trace lines={traces} video_debug lines={debug_lines}' +
                    (f' first end: {first_end[:160]}' if first_end else ''))
        env_note(f'after the {WATCH_SEC} s watch')
    if first_end is not None:
        # AVD r11 (Codex r10-2): the session already ended -- its data kept, the run ends; no menu is driven on an app
        # that is already elsewhere (a later menu failure must not look like a cause)
        skip_case('E2 PC 목록', 'the session ended during the watch (FIRST END above) -- nothing more is driven')
        for case in ('E3', 'E4', 'E5', 'E6', 'E7', 'E7b', 'E7c', 'E7e', 'E7f', 'E7g', 'E7h', 'E8', 'E9', 'E10', 'E11', 'E12',
                     'E13', 'E13b'):
            skip_case(case, f'plan {"e2-shoff" if sh_off else "e2-sh"}: E1 and E2 only')
        return
    segment('list', 'begin')""")
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
