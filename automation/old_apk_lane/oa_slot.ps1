param(
  [string]$Tag = 'ke',
  [int]$FixtureReadyTimeoutSec = 300,
  # stand-ins for the orchestration test (ke_orchestration_test.ps1): the harness / directory / AVD commands. With an
  # -AvdCmd given, the emulator identity check is not made (no adb is touched at all); never with the real AVD run.
  [string[]]$HarnessCmd = @(),
  [string[]]$NodeCmd = @(),
  [string[]]$AvdCmd = @(),
  [int]$ReadyTimeoutSec = 60,
  [int]$StopWaitSec = 180,
  # AVD r8 (Codex design r3 D2/D3): the APKs to install (default: this folder's ke-off / ke-on), and the adb client's pin
  # (the SDK client, by absolute path; its sha256 -- a different file stops the run before any device call).
  [string]$ApkOff = '',
  [string]$ApkOn = '',
  [string]$AdbSha256 = '',
  # rel-0164 old-APK lane: which arm (S: the same-source fixture build of APK 0.2.32 through the directory; P: the
  # PUBLISHED 0.2.32 through its manual connect), the APK, the build the hosts / viewer / tools come from, the server,
  # the job runner, and whether a PC-side standby lease is held on the host meanwhile (case 7).
  [ValidateSet('S', 'P')][string]$Arm = 'S',
  [string]$Apk = '',
  [string]$BuildDir = '',
  [string]$ServerJs = '',
  [string]$JobRun = '',
  [switch]$WithStandby,
  [string[]]$HolderCmd = @()
)
# rel-0164 old-APK lane (Codex codex_rel_0164_split_decision.md (2), legacy combination 1): made from the AVD harness r8
# ke_slot.ps1 -- its run folder / job / readiness / adb rules unchanged -- for ONE host (oa-pc-a) and APK 0.2.32.
# keepalive-r2 ② e: one AVD bundle (run under sh_run.ps1 -NativeExe, which holds the measuring slot): ke_fixture.ps1's
# directory + three pinned test hosts, then ke_avd.py on the slot's emulator-5554, then the fixture's own end.
#
# AVD guard r2 (Codex codex_avd_guard_src_r1.md N1 / N2):
#   * a NEW run folder under the anchor (.claude\e2e\runs\<tag>-<time>-<pid>), made here and refused if it exists, its
#     path checked for links -- no earlier run's ready / pid / fault / log is ever read or overwritten;
#   * the fixture is this script's own child, held by its handle; no pid file anywhere;
#   * the AVD starts ONLY after the fixture wrote fixture_ready.txt naming this run (all three hosts passed the guard)
#     while it still runs, and the slot's emulator is the one it says (emulator-5554, an emulator). Else: no AVD at all;
#   * the bundle's result: the fixture's own end (every host's harness, its guard's final judgement) AND the AVD's. A
#     fixture that failed -- before or after the AVD -- makes the bundle fail, whatever the AVD said.
$ErrorActionPreference = 'Stop'
# -File passes a list as one comma-joined string: split it back
foreach ($v in 'HarnessCmd', 'NodeCmd', 'AvdCmd', 'HolderCmd') {
  $cur = @(Get-Variable $v -ValueOnly)
  if ($cur.Count -eq 1) { Set-Variable $v @($cur[0] -split ',') }
}
$wt = (Resolve-Path "$PSScriptRoot\..\..").Path
$e = $PSScriptRoot
$anchor = "$wt\.claude\oa_runs"
function Is-Link([string]$p) { (Test-Path -LiteralPath $p) -and (((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) }
if ($Tag -notmatch '^[A-Za-z0-9_-]{1,24}$') { "FAIL  SETUP slot: the tag must be plain ASCII (1-24)`nRESULT: INVALID"; exit 2 }
# G2 (Codex codex_avd_guard_src_r2.md): EVERY component from the drive down to the anchor checked before anything is made
$cur = Split-Path $anchor -Qualifier
foreach ($part in ($anchor.Substring($cur.Length).Trim('\') -split '\\')) {
  $cur = "$cur\$part"
  if (Is-Link $cur) { "FAIL  SETUP slot: a link on the anchor's path ($cur)`nRESULT: INVALID"; exit 2 }
  if ((Test-Path -LiteralPath $cur) -and -not (Test-Path -LiteralPath $cur -PathType Container)) {
    "FAIL  SETUP slot: $cur is not a folder`nRESULT: INVALID"; exit 2
  }
}
if (-not (Test-Path -LiteralPath $anchor)) { New-Item -ItemType Directory $anchor | Out-Null }
if (Is-Link $anchor) { "FAIL  SETUP slot: the anchor is a link`nRESULT: INVALID"; exit 2 }
$runId = "$Tag-$(Get-Date -Format yyyyMMddHHmmss)-$PID"
$root = "$anchor\$runId"
try { New-Item -ItemType Directory $root -ErrorAction Stop | Out-Null }  # no -Force: an existing folder is refused
catch { "FAIL  SETUP slot: the run folder $root could not be made new ($_)`nRESULT: INVALID"; exit 2 }
if (Is-Link $root) { "FAIL  SETUP slot: the run folder is a link`nRESULT: INVALID"; exit 2 }
$sum = "$root\slot.txt"
function Say([string]$line) { $line | Out-File $sum -Append -Encoding utf8 }
Say "slot run=$runId start $(Get-Date -Format s) (qwinsta: $((qwinsta | Select-String '^>').Line.Trim()))"
Say "  arm $Arm standby=$([bool]$WithStandby) build=$BuildDir server=$ServerJs"
if ($AvdCmd.Count -eq 0) {
  if (-not $Apk -or -not (Test-Path -LiteralPath $Apk -PathType Leaf)) { "FAIL  SETUP slot: -Apk is not a file ($Apk)`nRESULT: INVALID"; exit 2 }
  Say "  apk $Apk $((Get-FileHash $Apk).Hash.ToLower())"
}
# ---- AVD r8 (D3): the one adb client, its pin, and who listens on 5037 (read only)
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
function Invoke-Adb([string[]]$a) {
  # rc, stdout and stderr kept apart: adb's "daemon not running; starting now" on stderr is a notice, not a failure
  $psi = New-Object Diagnostics.ProcessStartInfo $adb
  $psi.Arguments = ($a -join ' ')
  $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
  $p = [Diagnostics.Process]::Start($psi)
  $outTask = $p.StandardOutput.ReadToEndAsync(); $errTask = $p.StandardError.ReadToEndAsync()
  if (-not $p.WaitForExit(30000)) { try { $p.Kill() } catch {} ; return [pscustomobject]@{ rc = -1; out = ''; err = 'timeout 30 s' } }
  [pscustomobject]@{ rc = $p.ExitCode; out = $outTask.Result.Trim(); err = (($errTask.Result -replace '\s+', ' ').Trim()) }
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
  Remove-Item env:OA_TEST_FAKE_ADB -ErrorAction SilentlyContinue  # r3: the fake adb is for oa_pmclear_test.py only
  Say "  adb listener 5037 (before): $(Adb-Listener)"
}

# ---- the fixture: inside a verified kill-on-close job of its own (e2e_job_run.exe, G2) -- created suspended, put in
# the job, then resumed, so nothing it starts can be outside it; whatever of it is left when it ends (a crash, a kill)
# is terminated by that job and its end confirmed. This script holds the job runner by its handle.
$jobRun = if ($JobRun) { $JobRun } else { "$wt\build\apps\native_poc\Release\e2e_job_run.exe" }
if (-not (Test-Path -LiteralPath $jobRun)) { "FAIL  SETUP slot: $jobRun is not built`nRESULT: INVALID"; exit 2 }
$graceSec = 3 * ($StopWaitSec + 15) + 60   # the fixture's own worst stop (each harness in turn, then the node)
$q = { param($x) '"' + $x + '"' }
$fxCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $(& $q "$e\oa_fixture.ps1") -Root $(& $q $root) -RunId $runId " +
         "-HostSec 2400 -ReadyTimeoutSec $ReadyTimeoutSec -StopWaitSec $StopWaitSec -Arm $Arm"
if ($BuildDir) { $fxCmd += " -BuildDir $(& $q $BuildDir)" }
if ($ServerJs) { $fxCmd += " -ServerJs $(& $q $ServerJs)" }
if ($WithStandby) { $fxCmd += " -WithStandby" }
if ($HolderCmd.Count) { $fxCmd += " -HolderCmd $(& $q ($HolderCmd -join ','))" }
if ($HarnessCmd.Count) { $fxCmd += " -HarnessCmd $(& $q ($HarnessCmd -join ','))" }
if ($NodeCmd.Count) { $fxCmd += " -NodeCmd $(& $q ($NodeCmd -join ','))" }
$fx = Start-Process -FilePath $jobRun -ArgumentList @('--stop-file', (& $q "$root\stop_fixture"), '--grace-sec', "$graceSec", '--cmd', (& $q ($fxCmd -replace '"', '\"'))) `
  -PassThru -NoNewWindow -RedirectStandardOutput "$root\fixture_stdout.txt" -RedirectStandardError "$root\fixture_stderr.txt"
$null = $fx.Handle
Say "  fixture (in its job, e2e_job_run $((Get-FileHash $jobRun).Hash.ToLower().Substring(0,16))) runner pid $($fx.Id) at $($fx.StartTime.ToString('s'))"

function End-Fixture() {
  if (-not (Test-Path -LiteralPath "$root\stop_fixture")) { New-Item -ItemType File "$root\stop_fixture" | Out-Null }
  # the job runner gives the fixture its grace, then terminates the job itself; it ends well within this
  if (-not $fx.WaitForExit(($graceSec + 120) * 1000)) {
    $fx.Kill()  # the runner's own handle: its job closes, so kill-on-close ends everything in it -- not confirmed here
    $null = $fx.WaitForExit(15000)
    Say "  the job runner did not end: killed by its handle (the job's kill-on-close ends the rest; NOT confirmed)"
    return 99
  }
  $line = (Get-Content -LiteralPath "$root\fixture_stdout.txt" -ErrorAction SilentlyContinue | Select-String '^job_run:' | Select-Object -Last 1).Line
  Say "  $line"
  return $fx.ExitCode
}

# any error from here on: the fixture (and so its hosts) is ended by its handle before this script ends
trap {
  Say "  slot error: $_"
  if ($fx -and -not $fx.HasExited) { $null = End-Fixture }
  "FAIL  slot: $_`nRESULT: INVALID"
  exit 2
}

# ---- wait for THIS run's fixture readiness while the fixture runs; anything else: no AVD at all
$deadline = (Get-Date).AddSeconds($FixtureReadyTimeoutSec)
$ready = $false
while ((Get-Date) -lt $deadline -and -not $fx.HasExited) {
  if (Test-Path -LiteralPath "$root\fixture_ready.txt") {
    $ready = (Get-Content -LiteralPath "$root\fixture_ready.txt" -Raw) -match "^ready run=$([regex]::Escape($runId)) hosts=a "
    break
  }
  Start-Sleep -Milliseconds 500
}
$why = $null
if (-not $ready) { $why = if ($fx.HasExited) { "the fixture ended before it was ready (exit $($fx.ExitCode))" } else { "the fixture was not ready (this run) within $FixtureReadyTimeoutSec s" } }
elseif ($fx.HasExited) { $why = "the fixture ended right after its readiness (exit $($fx.ExitCode))" }

# ---- the slot's emulator, as it says it is (skipped only for the orchestration test's stand-in AVD)
if (-not $why -and $AvdCmd.Count -eq 0) {
  # AVD r8 (D3): a few tries over at most 20 s (a server just started shows the emulator `offline` for seconds); every
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
  if ($state -ne 'device' -or $qemu -ne '1' -or $boot -ne '1') { $why = "emulator-5554 is not this slot's ready emulator (state=$state qemu=$qemu boot=$boot)" }
}
if ($why) {
  Say "  NO AVD: $why"
  $fxExit = End-Fixture
  Say "  fixture exit=$fxExit"
  Say "slot run=$runId done $(Get-Date -Format s): INVALID (no AVD)"
  "FAIL  SETUP slot: $why -- the AVD was not started`nRESULT: INVALID"
  exit 2
}

# ---- the AVD, then the fixture's own end -- the end runs whatever happens in between (an error here never leaves the
# fixture and its hosts running)
$avdExit = 99
$fxExit = 99
try {
  Say "  fixture ready (run $runId): the AVD starts"
  # AVD r5 (ke-r22): the AVD's stderr goes to its own file and is NEVER turned into a terminating error -- under 'Stop'
  # PowerShell 5.1 made the first stderr line ("Traceback ...") an exception, lost the rest and the exit code. Only
  # this call runs under 'Continue'; its exit code is the AVD's.
  $savedPref = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    if ($AvdCmd.Count -eq 0) {
      $env:KA_SERIAL = 'emulator-5554'
      & python -I "$e\oa_avd.py" $root $Arm $Apk "$root\avd" > "$root\avd_stdout.txt" 2> "$root\avd_stderr.txt"
    } else {
      $avdArgs = @($AvdCmd | Select-Object -Skip 1) + @($root)
      & $AvdCmd[0] @avdArgs > "$root\avd_stdout.txt" 2> "$root\avd_stderr.txt"
    }
    $avdExit = $LASTEXITCODE
    if ($AvdCmd.Count -eq 0) { Say "  adb listener 5037 (after the AVD): $(Adb-Listener)" }
  } finally {
    $ErrorActionPreference = $savedPref
  }
  $tb = if (Test-Path -LiteralPath "$root\avd_stderr.txt") { (Get-Content -LiteralPath "$root\avd_stderr.txt" | Select-String 'Traceback|Error' | Select-Object -Last 1).Line } else { '' }
  Say "  avd exit=$avdExit $((Get-Content "$root\avd_stdout.txt" | Select-String 'run end').Line)$(if ($tb) { " (stderr: $($tb.Trim()) -- in full: avd_stderr.txt)" })"
} catch {
  Say "  AVD step error: $_"
} finally {
  # the fixture's own end: every host stopped, its guard's final judgement in its harness's exit
  $fxExit = End-Fixture
  Say "  fixture exit=$fxExit $(if (Test-Path "$root\fixture_failed.txt") { (Get-Content "$root\fixture_failed.txt" -Raw).Trim() })"
}
$rc = if ($fxExit -ne 0) { 3 } elseif ($avdExit -ne 0) { $avdExit } else { 0 }
Say "slot run=$runId done $(Get-Date -Format s): rc=$rc (fixture $fxExit, avd $avdExit)"
Get-Content "$root\avd_stdout.txt" | Where-Object { $_ -match '(PASS|FAIL|NOT RUN) |run end' }
"slot run=${runId}: fixture exit=$fxExit avd exit=$avdExit"
"RESULT: $(if ($rc -eq 0) { 'PASS' } else { 'FAIL' })"
exit $rc
