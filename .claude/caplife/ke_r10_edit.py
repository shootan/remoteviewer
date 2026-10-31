base = r'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e'


def edit(name, reps):
    p = base + '\\' + name
    raw = open(p, 'rb').read()
    crlf = b'\r\n' in raw
    bom = raw.startswith(b'\xef\xbb\xbf')
    s = raw.decode('utf-8-sig').replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))


# ---- ke_slot.ps1: the plan decides the hosts; the readiness must name exactly those
edit('ke_slot.ps1', [
("""$q = { param($x) '"' + $x + '"' }""", """# AVD r10 (Codex codex_apk_decoder_inbox_decision_r1.md (5)): an E2-only plan (KE_PLAN e2-sh / e2-shoff) starts host
# a ONLY -- b and c had no client and failed their capture assertion (ke-r23 / r25 / r26: bundle rc 3); `full` keeps
# three. The fixture is told which, and its readiness must name exactly those.
$plan = if ($env:KE_PLAN) { $env:KE_PLAN } else { 'full' }
$hostSet = if ($plan -in @('e2-sh', 'e2-shoff')) { 'a' } else { 'a,b,c' }
Say "  plan $plan hosts $hostSet"
$q = { param($x) '"' + $x + '"' }"""),
("""if ($HarnessCmd.Count) { $fxCmd += " -HarnessCmd $(& $q ($HarnessCmd -join ','))" }""",
 """$fxCmd += " -Hosts $hostSet"
if ($HarnessCmd.Count) { $fxCmd += " -HarnessCmd $(& $q ($HarnessCmd -join ','))" }"""),
("""    $ready = (Get-Content -LiteralPath "$root\\fixture_ready.txt" -Raw) -match "^ready run=$([regex]::Escape($runId)) hosts=a,b,c\"""",
 """    $ready = (Get-Content -LiteralPath "$root\\fixture_ready.txt" -Raw) -match "^ready run=$([regex]::Escape($runId)) hosts=$([regex]::Escape($hostSet))\\s*$\""""),
])

# ---- ke_fixture.ps1: only the hosts asked for
edit('ke_fixture.ps1', [
("""  [string[]]$HarnessCmd = @(),
  [string[]]$NodeCmd = @()
)""", """  [string[]]$HarnessCmd = @(),
  [string[]]$NodeCmd = @(),
  # AVD r10: the hosts this plan uses (ke_slot.ps1 passes them; an E2-only plan: a)
  [string]$Hosts = 'a,b,c'
)"""),
("""  $hostsDef = @(
    @{ n = 'a'; name = 'ke-pc-a'; mark = 'ff00ff' },
    @{ n = 'b'; name = 'ke-pc-b'; mark = '00ffff' },
    @{ n = 'c'; name = 'ke-pc-c'; mark = 'ffff00' })""", """  if ($Hosts -notmatch '^[abc](,[abc])*$') { throw "-Hosts must be a list of a / b / c ($Hosts)" }
  $wanted = @($Hosts -split ',')
  $hostsDef = @(@(
    @{ n = 'a'; name = 'ke-pc-a'; mark = 'ff00ff' },
    @{ n = 'b'; name = 'ke-pc-b'; mark = '00ffff' },
    @{ n = 'c'; name = 'ke-pc-c'; mark = 'ffff00' }) | Where-Object { $wanted -contains $_.n })
  Say "  hosts: $(($hostsDef | ForEach-Object { $_.name }) -join ', ') (asked: $Hosts)\""""),
("""  "ready run=$RunId hosts=a,b,c" | Out-File "$Root\\fixture_ready.tmp" -Encoding ascii""",
 """  "ready run=$RunId hosts=$Hosts" | Out-File "$Root\\fixture_ready.tmp" -Encoding ascii"""),
])

# ---- ke_avd.py: the plan's PCs; the watch length; segment markers; samples during the watch
edit('ke_avd.py', [
("""H = {k: f'ke-pc-{k}' for k in (('a', 'b', 'c', 'old') if OLD_HOST else ('a', 'b', 'c'))}""",
 """# AVD r10: the plan decides which PCs exist (the fixture starts only those): an E2-only plan has ke-pc-a alone.
PLAN = os.environ.get('KE_PLAN', 'full')
E2_ONLY = PLAN in ('e2-sh', 'e2-shoff')
H = {k: f'ke-pc-{k}' for k in (('a',) if E2_ONLY else ('a', 'b', 'c', 'old') if OLD_HOST else ('a', 'b', 'c'))}
# AVD r10 (Codex inbox decision r1 (4)): the watch after the first frame (A/B: 120 s), sampled every SAMPLE_SEC
WATCH_SEC = int(os.environ.get('KE_WATCH_SEC', '60'))
if not 10 <= WATCH_SEC <= 600:
    raise RuntimeError(f'KE_WATCH_SEC out of range: {WATCH_SEC}')
SAMPLE_SEC = 10"""),
("""def env_note(tag):""", """def segment(name, edge):
    \"\"\"AVD r10: a segment boundary (startup / watch / list) on the run's clock AND as epoch ms, so the Host log, the
    relay trace and the app log (each its own clock) can be cut at the same instants.\"\"\"
    step(f'SEGMENT {name} {edge} epoch_ms={int(time.time() * 1000)}')


_qemu_pid = None


def sample(tag):
    \"\"\"AVD r10: one sample, the same in every arm -- the app's memory (dumpsys meminfo TOTAL PSS), the emulator's
    load, the app's CPU (top), and on this PC the emulator process's total CPU time. Recorded only.\"\"\"
    global _qemu_pid
    pss = re.search(r'TOTAL PSS:\\s+(\\d+)|TOTAL\\s+(\\d+)', adb('shell', 'dumpsys', 'meminfo', PKG))
    pss_kb = (pss.group(1) or pss.group(2)) if pss else '?'
    load = adb('shell', 'cat', '/proc/loadavg').strip()
    pid = adb('shell', 'pidof', PKG).strip()
    cpu = '?'
    if pid:
        top = adb('shell', 'top', '-b', '-n', '1', '-p', pid.split()[0])
        m = re.search(r'^\\s*' + re.escape(pid.split()[0]) + r'\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\S+\\s+([\\d.]+)', top, re.M)
        cpu = m.group(1) if m else '?'
    qms = '?'
    try:
        if _qemu_pid is None:
            r = subprocess.run(['powershell', '-NoProfile', '-Command',
                                "(Get-Process -Name 'qemu-system-x86_64-headless' -ErrorAction Stop | Select-Object -First 1).Id"],
                               capture_output=True, timeout=20)
            _qemu_pid = r.stdout.decode(errors='replace').strip() or ''
        if _qemu_pid:
            r = subprocess.run(['powershell', '-NoProfile', '-Command',
                                f'[int64](Get-Process -Id {_qemu_pid} -ErrorAction Stop).TotalProcessorTime.TotalMilliseconds'],
                               capture_output=True, timeout=20)
            qms = r.stdout.decode(errors='replace').strip() or '?'
    except Exception as e:  # noqa: BLE001 -- a sample that cannot be had is recorded as such
        qms = f'error:{type(e).__name__}'
    step(f'SAMPLE {tag} epoch_ms={int(time.time() * 1000)} app_pss_kb={pss_kb} app_cpu_pct={cpu} '
         f'emu_loadavg="{load}" qemu_cpu_ms={qms} app_pid={pid or "-"}')


def env_once():
    \"\"\"AVD r10: the emulator's fixed shape, once per run (CPUs, memory, screen, GL driver) -- recorded only.\"\"\"
    nproc = adb('shell', 'nproc').strip()
    mem = adb('shell', 'cat', '/proc/meminfo').splitlines()[:1]
    size = adb('shell', 'wm', 'size').strip().replace('\\n', ' ')
    gles = re.findall(r'GLES:[^\\n]*', adb('shell', 'dumpsys', 'SurfaceFlinger'))[:1]
    step(f'  env[once]: nproc={nproc} {mem[0].strip() if mem else "?"} {size} {gles[0][:140] if gles else "GLES:?"}')


def env_note(tag):"""),
("""    env_note('after E2 select')
    # AVD r8 (Codex design r3 D2): the session watched, untouched, for 60 s after its first frame -- the FIRST end, if
    # any, recorded as it is (so a later menu step cannot look like the first failure).
    if first_ms is not None:
        n_watch = cursor()
        t_watch = time.time()
        first_end = None
        while time.time() - t_watch < 60:
            time.sleep(5)
            seen = re.findall(r'[^\\n]*\\[(?:keep_end|scene)\\][^\\n]*', since(n_watch))
            ends = [l for l in seen if '[keep_end]' in l or '[scene] HOSTS' in l]
            if ends and first_end is None:
                first_end = ends[0]
                step(f'  FIRST END at +{time.time() - t_watch:.1f}s of the watch: {first_end[:200]}')
        watched = since(n_watch)
        traces = len(re.findall(r'\\[liveness_trace\\]', watched))
        verdict(f'{tag} the session stayed up 60 s after its first frame (no keep_end, still the viewer)',
                first_end is None, f'liveness_trace lines={traces}' + (f' first end: {first_end[:160]}' if first_end else ''))
        env_note('after the 60 s watch')
    n0, ok = to_list('E2')""", """    env_note('after E2 select')
    segment('startup', 'end')
    # AVD r8 (Codex design r3 D2): the session watched, untouched, for WATCH_SEC (r10; was 60 s) after its first frame
    # -- the FIRST end, if any, recorded as it is (so a later menu step cannot look like the first failure). r10: a
    # sample every SAMPLE_SEC; an early end is recorded with the length actually watched (never scaled up).
    if first_ms is not None:
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
    segment('list', 'begin')
    n0, ok = to_list('E2')"""),
("""    verdict('E2 A shows 대기 중 (a confirmed entry on a live lease)', st is not None and st[0] == '대기 중', str(st))
    env_note('end')
    for case in ('E3',""", """    verdict('E2 A shows 대기 중 (a confirmed entry on a live lease)', st is not None and st[0] == '대기 중', str(st))
    env_note('end')
    segment('list', 'end')
    for case in ('E3',"""),
("""        threading.Thread(target=_logcat_watch, args=(logcat,), daemon=True).start()
        start_app(APK_OFF, clear=True)

        # E1
        n0 = cursor()
        verdict('E1 signed in; four PCs listed', login())""", """        threading.Thread(target=_logcat_watch, args=(logcat,), daemon=True).start()
        env_once()
        sample('idle-before-start')  # AVD r10: the load before the product runs (the A/B arms alike)
        segment('startup', 'begin')
        start_app(APK_OFF, clear=True)

        # E1
        n0 = cursor()
        verdict(f'E1 signed in; the plan\\'s PCs listed ({", ".join(H.values())})', login())"""),
("""        plan = os.environ.get('KE_PLAN', 'full')
        step(f'plan: {plan}')""", """        plan = PLAN
        step(f'plan: {plan} hosts={",".join(H)} watch={WATCH_SEC}s')"""),
])
print('ok')
