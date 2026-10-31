p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_slot.ps1'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''foreach ($p in @($wt, "$wt\\.claude", $e)) { if (Is-Link $p) { "FAIL  SETUP slot: a link on the anchor's path ($p)`nRESULT: INVALID"; exit 2 } }
''', '''# G2 (Codex codex_avd_guard_src_r2.md): EVERY component from the drive down to the anchor checked before anything is made
$cur = Split-Path $anchor -Qualifier
foreach ($part in ($anchor.Substring($cur.Length).Trim('\\') -split '\\\\')) {
  $cur = "$cur\\$part"
  if (Is-Link $cur) { "FAIL  SETUP slot: a link on the anchor's path ($cur)`nRESULT: INVALID"; exit 2 }
  if ((Test-Path -LiteralPath $cur) -and -not (Test-Path -LiteralPath $cur -PathType Container)) {
    "FAIL  SETUP slot: $cur is not a folder`nRESULT: INVALID"; exit 2
  }
}
''')

rep('''# ---- the fixture: this script's own child (NOT "& powershell ... | Out-Null": its children would hold that pipe open)
$fxArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\\ke_fixture.ps1", '-Root', $root, '-RunId', $runId,
  '-HostSec', '2400', '-ReadyTimeoutSec', "$ReadyTimeoutSec", '-StopWaitSec', "$StopWaitSec")
if ($HarnessCmd.Count) { $fxArgs += @('-HarnessCmd', ($HarnessCmd -join ',')) }
if ($NodeCmd.Count) { $fxArgs += @('-NodeCmd', ($NodeCmd -join ',')) }
$fx = Start-Process -FilePath powershell -ArgumentList $fxArgs -PassThru -NoNewWindow -RedirectStandardOutput "$root\\fixture_stdout.txt" -RedirectStandardError "$root\\fixture_stderr.txt"
$null = $fx.Handle
Say "  fixture pid $($fx.Id) at $($fx.StartTime.ToString('s'))"

function End-Fixture([int]$waitMs) {
  if (-not (Test-Path -LiteralPath "$root\\stop_fixture")) { New-Item -ItemType File "$root\\stop_fixture" | Out-Null }
  if (-not $fx.WaitForExit($waitMs)) {
    $fx.Kill()   # its own handle -- and only when it would not end by itself
    $null = $fx.WaitForExit(15000)
    Say "  fixture did not end by itself: killed by its handle (its children: see fixture_result.txt if written)"
    return 98
  }
  return $fx.ExitCode
}
''', '''# ---- the fixture: inside a verified kill-on-close job of its own (e2e_job_run.exe, G2) -- created suspended, put in
# the job, then resumed, so nothing it starts can be outside it; whatever of it is left when it ends (a crash, a kill)
# is terminated by that job and its end confirmed. This script holds the job runner by its handle.
$jobRun = "$wt\\build\\apps\\native_poc\\Release\\e2e_job_run.exe"
if (-not (Test-Path -LiteralPath $jobRun)) { "FAIL  SETUP slot: $jobRun is not built`nRESULT: INVALID"; exit 2 }
$graceSec = 3 * ($StopWaitSec + 15) + 60   # the fixture's own worst stop (each harness in turn, then the node)
$q = { param($x) '"' + $x + '"' }
$fxCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $(& $q "$e\\ke_fixture.ps1") -Root $(& $q $root) -RunId $runId " +
         "-HostSec 2400 -ReadyTimeoutSec $ReadyTimeoutSec -StopWaitSec $StopWaitSec"
if ($HarnessCmd.Count) { $fxCmd += " -HarnessCmd $(& $q ($HarnessCmd -join ','))" }
if ($NodeCmd.Count) { $fxCmd += " -NodeCmd $(& $q ($NodeCmd -join ','))" }
$fx = Start-Process -FilePath $jobRun -ArgumentList @('--stop-file', (& $q "$root\\stop_fixture"), '--grace-sec', "$graceSec", '--cmd', (& $q ($fxCmd -replace '"', '\\"'))) `
  -PassThru -NoNewWindow -RedirectStandardOutput "$root\\fixture_stdout.txt" -RedirectStandardError "$root\\fixture_stderr.txt"
$null = $fx.Handle
Say "  fixture (in its job, e2e_job_run $((Get-FileHash $jobRun).Hash.ToLower().Substring(0,16))) runner pid $($fx.Id) at $($fx.StartTime.ToString('s'))"

function End-Fixture() {
  if (-not (Test-Path -LiteralPath "$root\\stop_fixture")) { New-Item -ItemType File "$root\\stop_fixture" | Out-Null }
  # the job runner gives the fixture its grace, then terminates the job itself; it ends well within this
  if (-not $fx.WaitForExit(($graceSec + 120) * 1000)) {
    $fx.Kill()  # the runner's own handle: its job closes, so kill-on-close ends everything in it -- not confirmed here
    $null = $fx.WaitForExit(15000)
    Say "  the job runner did not end: killed by its handle (the job's kill-on-close ends the rest; NOT confirmed)"
    return 99
  }
  $line = (Get-Content -LiteralPath "$root\\fixture_stdout.txt" -ErrorAction SilentlyContinue | Select-String '^job_run:' | Select-Object -Last 1).Line
  Say "  $line"
  return $fx.ExitCode
}
''')
rep('''  if ($fx -and -not $fx.HasExited) { $null = End-Fixture 300000 }''', '''  if ($fx -and -not $fx.HasExited) { $null = End-Fixture }''')
rep('''  $fxExit = End-Fixture 300000
''', '''  $fxExit = End-Fixture
''')
rep('''  $fxExit = End-Fixture 600000
''', '''  $fxExit = End-Fixture
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
