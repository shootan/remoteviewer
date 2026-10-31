"""AVD harness r8 (Codex codex_apk_decoder_liveness_design_r3.md D2 / D3): the adb client pinned (path, sha256,
version) and read-only listener records; the slot's readiness through rc / stdout / stderr (adb's daemon notice is not
an error; state / qemu / boot fail closed); logcat's end said at once; the e2-shoff plan watches 60 s after the first
frame and records the first keep_end; the fixture's server.js and APKs may be named (the liveness branch's)."""
E = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/'
CR = chr(13)


def edit(name, reps):
    p = E + name
    s = open(p, encoding='utf-8', newline='').read()
    bom = s.startswith('\ufeff')
    crlf = (CR + '\n') in s
    if crlf:
        s = s.replace(CR + '\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (name, a[:80], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', CR + '\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name, 'bom' if bom else '', 'crlf' if crlf else 'lf')


edit('ka_avd.py', [
    ('''import os
import re
import subprocess''', '''import hashlib
import os
import re
import subprocess'''),
    ('''ADB = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'platform-tools', 'adb.exe')
''', '''ADB = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'platform-tools', 'adb.exe')
# AVD r8 (Codex design r3 D3): this ONE client, by absolute path, for every call (no PATH lookup anywhere). With
# KE_ADB_SHA256 set (ke_slot passes its pin) a different file refuses to run at all.
ADB_SHA256_PIN = os.environ.get('KE_ADB_SHA256', '').lower()


def adb_identity():
    """(sha256 of the client file, its `adb version` lines) -- `adb version` reads the client only, no server."""
    digest = hashlib.sha256(open(ADB, 'rb').read()).hexdigest()
    r = subprocess.run([ADB, 'version'], capture_output=True, timeout=30)
    lines = [l.strip() for l in r.stdout.decode('utf-8', errors='replace').splitlines() if l.strip()][:2]
    return digest, ' / '.join(lines)


if ADB_SHA256_PIN and adb_identity()[0] != ADB_SHA256_PIN:
    raise RuntimeError(f'the adb client {ADB} is not the pinned one (KE_ADB_SHA256)')
'''),
])

edit('ke_avd.py', [
    ('''    select('E2 A (ordinary connect, sharpening pipeline OFF)')
    log = since(n_sh)''', '''    first_ms = select('E2 A (ordinary connect, sharpening pipeline OFF)')
    log = since(n_sh)'''),
    ('''    env_note('after E2 select')
    n0, ok = to_list('E2')''', '''    env_note('after E2 select')
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
        verdict('E2-SHOFF the session stayed up 60 s after its first frame (no keep_end, still the viewer)',
                first_end is None, f'liveness_trace lines={traces}' + (f' first end: {first_end[:160]}' if first_end else ''))
        env_note('after the 60 s watch')
    n0, ok = to_list('E2')'''),
    ('''        logcat = subprocess.Popen([ka_avd.ADB, '-s', ka_avd.SERIAL, 'logcat', '-b', 'all', '-v', 'threadtime'],''',
     '''        digest, version = ka_avd.adb_identity()
        step(f'  adb client {ka_avd.ADB} sha256={digest[:16]} {version} serial={ka_avd.SERIAL}')
        logcat = subprocess.Popen([ka_avd.ADB, '-s', ka_avd.SERIAL, 'logcat', '-b', 'all', '-v', 'threadtime'],'''),
])

# the logcat watcher: right after the Popen statement (its closing line)
p = E + 'ke_avd.py'
s = open(p, encoding='utf-8', newline='').read()
crlf = (CR + '\n') in s
t = s.replace(CR + '\n', '\n') if crlf else s
anchor = "                                  stdout=open(os.path.join(OUT, 'logcat.txt'), 'wb'), stderr=subprocess.DEVNULL)\n"
assert t.count(anchor) == 1, t.count(anchor)
t = t.replace(anchor, anchor + '''        # AVD r8 (Codex design r3 D3): logcat's end is said when it happens -- from there on there is NO logcat (a gap),
        # which is not "no events". Its stop at the run's end is ours and says so.
        import threading
        logcat_stopping = threading.Event()

        def _logcat_watch(proc):
            rc = proc.wait()
            if not logcat_stopping.is_set():
                step(f'  LOGCAT ENDED rc={rc} -- no logcat from here (a gap, not an absence of events)')
        threading.Thread(target=_logcat_watch, args=(logcat,), daemon=True).start()
''')
a2 = '''        if logcat:
            logcat.terminate()'''
assert t.count(a2) == 1
t = t.replace(a2, '''        if logcat:
            try:
                logcat_stopping.set()
            except NameError:
                pass
            logcat.terminate()''')
s = t.replace('\n', CR + '\n') if crlf else t
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok ke_avd.py logcat watcher')

edit('ke_fixture.ps1', [
    ('''  if ($NodeCmd.Count -eq 0) {
    $server = "$wt\\apps\\directory\\server.js"''', '''  # AVD r8 (Codex design r3 D2): the directory may be another worktree's (KE_SERVER_JS: an absolute ...\\server.js that
  # exists), and its relay trace is on only with KE_RELAY_TRACE=1 (REMOTE60_RELAY_TRACE, the fixture's own server).
  if ($env:KE_RELAY_TRACE -eq '1') { $dirEnv['REMOTE60_RELAY_TRACE'] = '1' }
  if ($NodeCmd.Count -eq 0) {
    $server = "$wt\\apps\\directory\\server.js"
    if ($env:KE_SERVER_JS) {
      if (-not ([IO.Path]::IsPathRooted($env:KE_SERVER_JS)) -or -not $env:KE_SERVER_JS.EndsWith('\\server.js') -or
          -not (Test-Path -LiteralPath $env:KE_SERVER_JS -PathType Leaf)) { throw "KE_SERVER_JS is not an existing ...\\server.js ($($env:KE_SERVER_JS))" }
      $server = $env:KE_SERVER_JS
    }'''),
    ('''    Say "  directory server.js $((Get-FileHash $server).Hash.ToLower().Substring(0,16))"''',
     '''    Say "  directory server.js $server $((Get-FileHash $server).Hash.ToLower().Substring(0,16)) relayTrace=$($dirEnv['REMOTE60_RELAY_TRACE'])"'''),
])

edit('ke_slot.ps1', [
    ('''  [int]$ReadyTimeoutSec = 60,
  [int]$StopWaitSec = 180
)''', '''  [int]$ReadyTimeoutSec = 60,
  [int]$StopWaitSec = 180,
  # AVD r8 (Codex design r3 D2/D3): the APKs to install (default: this folder's ke-off / ke-on), and the adb client's pin
  # (the SDK client, by absolute path; its sha256 -- a different file stops the run before any device call).
  [string]$ApkOff = '',
  [string]$ApkOn = '',
  [string]$AdbSha256 = ''
)'''),
    ('''if ($AvdCmd.Count -eq 0) { Say "  apk off $((Get-FileHash "$e\\ke-off.apk").Hash.ToLower()) on $((Get-FileHash "$e\\ke-on.apk").Hash.ToLower())" }''',
     '''if (-not $ApkOff) { $ApkOff = "$e\\ke-off.apk" }
if (-not $ApkOn) { $ApkOn = "$e\\ke-on.apk" }
if ($AvdCmd.Count -eq 0) { Say "  apk off $ApkOff $((Get-FileHash $ApkOff).Hash.ToLower()) on $ApkOn $((Get-FileHash $ApkOn).Hash.ToLower())" }
# ---- AVD r8 (D3): the one adb client, its pin, and who listens on 5037 (read only)
$adb = "$env:LOCALAPPDATA\\Android\\Sdk\\platform-tools\\adb.exe"
function Invoke-Adb([string[]]$a) {
  # rc, stdout and stderr kept apart: adb's "daemon not running; starting now" on stderr is a notice, not a failure
  $psi = New-Object Diagnostics.ProcessStartInfo $adb
  $psi.Arguments = ($a -join ' ')
  $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
  $p = [Diagnostics.Process]::Start($psi)
  $outTask = $p.StandardOutput.ReadToEndAsync(); $errTask = $p.StandardError.ReadToEndAsync()
  if (-not $p.WaitForExit(30000)) { try { $p.Kill() } catch {} ; return [pscustomobject]@{ rc = -1; out = ''; err = 'timeout 30 s' } }
  [pscustomobject]@{ rc = $p.ExitCode; out = $outTask.Result.Trim(); err = (($errTask.Result -replace '\\s+', ' ').Trim()) }
}
function Adb-Listener {
  try {
    $c = Get-NetTCPConnection -LocalPort 5037 -State Listen -ErrorAction Stop | Select-Object -First 1
    $pr = Get-Process -Id $c.OwningProcess -ErrorAction Stop
    "pid=$($pr.Id) path=$($pr.Path) started=$($pr.StartTime.ToString('s')) addr=$($c.LocalAddress)"
  } catch { "none listening ($($_.Exception.Message))" }
}
if ($AvdCmd.Count -eq 0) {
  if (-not (Test-Path -LiteralPath $adb -PathType Leaf)) { "FAIL  SETUP slot: no adb client at $adb`nRESULT: INVALID"; exit 2 }
  $adbHash = (Get-FileHash $adb).Hash.ToLower()
  $adbVer = (Invoke-Adb @('version')).out -split "`r?`n" | Select-Object -First 2
  Say "  adb client $adb sha256=$($adbHash.Substring(0,16)) $($adbVer -join ' / ') pin=$(if ($AdbSha256) { $AdbSha256.Substring(0,16) } else { 'none' })"
  if ($AdbSha256 -and $adbHash -ne $AdbSha256.ToLower()) { Say "  NO AVD: the adb client is not the pinned one"; "FAIL  SETUP slot: adb pin`nRESULT: INVALID"; exit 2 }
  $env:KE_ADB_SHA256 = $adbHash
  Say "  adb listener 5037 (before): $(Adb-Listener)"
}'''),
    ('''  $adb = "$env:LOCALAPPDATA\\Android\\Sdk\\platform-tools\\adb.exe"
  $state = (& $adb -s emulator-5554 get-state 2>$null | Out-String).Trim()
  $qemu = (& $adb -s emulator-5554 shell getprop ro.kernel.qemu 2>$null | Out-String).Trim()
  if ($state -ne 'device' -or $qemu -ne '1') { $why = "emulator-5554 is not this slot's emulator (state=$state qemu=$qemu)" }''',
     '''  # AVD r8 (D3): a few tries over at most 20 s (a server just started shows the emulator `offline` for seconds); every
  # answer recorded; a state / qemu / boot that is not right at the end fails closed.
  $state = ''; $qemu = ''; $boot = ''
  $t0 = Get-Date
  do {
    $r1 = Invoke-Adb @('-s', 'emulator-5554', 'get-state'); $state = $r1.out
    $r2 = Invoke-Adb @('-s', 'emulator-5554', 'shell', 'getprop', 'ro.kernel.qemu'); $qemu = $r2.out
    $r3 = Invoke-Adb @('-s', 'emulator-5554', 'shell', 'getprop', 'sys.boot_completed'); $boot = $r3.out
    Say "  adb check: get-state rc=$($r1.rc) '$state' err='$($r1.err)' | qemu rc=$($r2.rc) '$qemu' | boot rc=$($r3.rc) '$boot' err='$($r3.err)'"
    if ($state -eq 'device' -and $qemu -eq '1' -and $boot -eq '1') { break }
    Start-Sleep 4
  } while (((Get-Date) - $t0).TotalSeconds -lt 20)
  Say "  adb listener 5037 (ready check): $(Adb-Listener)"
  if ($state -ne 'device' -or $qemu -ne '1' -or $boot -ne '1') { $why = "emulator-5554 is not this slot's ready emulator (state=$state qemu=$qemu boot=$boot)" }'''),
    ('''      & python -I "$e\\ke_avd.py" $root "$e\\ke-off.apk" "$e\\ke-on.apk" "$root\\avd" > "$root\\avd_stdout.txt" 2> "$root\\avd_stderr.txt"''',
     '''      & python -I "$e\\ke_avd.py" $root $ApkOff $ApkOn "$root\\avd" > "$root\\avd_stdout.txt" 2> "$root\\avd_stderr.txt"'''),
])
