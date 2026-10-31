"""rel-0164 old-APK lane: the lane's bundle scripts, made from the AVD harness r8 (feat-apk-0233\\.claude\\e2e, snapshot
avd_guard_r8_snapshot) by exact replacements -- the guard / job / run-folder / adb structure kept as it is."""
import os
import shutil

SRC = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/'
DST = 'C:/work/gnlink-wt/old-apk-lane/automation/old_apk_lane/'
CR = chr(13)
os.makedirs(DST + 'stubs', exist_ok=True)


def convert(src, dst, reps):
    s = open(SRC + src, encoding='utf-8-sig', newline='').read()
    crlf = (CR + '\n') in s
    if crlf:
        s = s.replace(CR + '\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (src, a[:90], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', CR + '\n')
    open(DST + dst, 'w', encoding='utf-8', newline='').write(s)
    print('made', dst)


convert('ka_avd.py', 'oa_adb.py', [
    ('''"""keepalive r1: drives the real APK UI''', '''"""rel-0164 old-APK lane: a copy of the AVD harness r8's ka_avd.py (the adb client pinned, every call's secrets
cleaned, the UI dump / find / tap / wait helpers) -- unchanged but this line.
keepalive r1: drives the real APK UI'''),
])
shutil.copyfile(SRC + 'ka_avd_redact_test.py', DST + 'oa_adb_redact_test.py')
s = open(DST + 'oa_adb_redact_test.py', encoding='utf-8').read()
s = s.replace("os.path.join(HERE, 'ka_avd.py')", "os.path.join(HERE, 'oa_adb.py')")
open(DST + 'oa_adb_redact_test.py', 'w', encoding='utf-8').write(s)
for f in ('stub_harness.py', 'stub_node.py'):
    shutil.copyfile(SRC + 'stubs/' + f, DST + 'stubs/' + f)

convert('ke_slot.ps1', 'oa_slot.ps1', [
    ('''  [string]$ApkOff = '',
  [string]$ApkOn = '',
  [string]$AdbSha256 = ''
)''', '''  [string]$ApkOff = '',
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
)'''),
    ('''# keepalive-r2 ② e: one AVD bundle''', '''# rel-0164 old-APK lane (Codex codex_rel_0164_split_decision.md (2), legacy combination 1): made from the AVD harness r8
# ke_slot.ps1 -- its run folder / job / readiness / adb rules unchanged -- for ONE host (oa-pc-a) and APK 0.2.32.
# keepalive-r2 ② e: one AVD bundle'''),
    ('''foreach ($v in 'HarnessCmd', 'NodeCmd', 'AvdCmd') {''', '''foreach ($v in 'HarnessCmd', 'NodeCmd', 'AvdCmd', 'HolderCmd') {'''),
    ('''$wt = 'C:\\work\\gnlink-wt\\feat-apk-0233'
$e = "$wt\\.claude\\e2e"
$anchor = "$e\\runs"''', '''$wt = (Resolve-Path "$PSScriptRoot\\..\\..").Path
$e = $PSScriptRoot
$anchor = "$wt\\.claude\\oa_runs"'''),
    ('''if (-not $ApkOff) { $ApkOff = "$e\\ke-off.apk" }
if (-not $ApkOn) { $ApkOn = "$e\\ke-on.apk" }
if ($AvdCmd.Count -eq 0) { Say "  apk off $ApkOff $((Get-FileHash $ApkOff).Hash.ToLower()) on $ApkOn $((Get-FileHash $ApkOn).Hash.ToLower())" }''',
     '''Say "  arm $Arm standby=$([bool]$WithStandby) build=$BuildDir server=$ServerJs"
if ($AvdCmd.Count -eq 0) {
  if (-not $Apk -or -not (Test-Path -LiteralPath $Apk -PathType Leaf)) { "FAIL  SETUP slot: -Apk is not a file ($Apk)`nRESULT: INVALID"; exit 2 }
  Say "  apk $Apk $((Get-FileHash $Apk).Hash.ToLower())"
}'''),
    ('''$jobRun = "$wt\\build\\apps\\native_poc\\Release\\e2e_job_run.exe"''',
     '''$jobRun = if ($JobRun) { $JobRun } else { "$wt\\build\\apps\\native_poc\\Release\\e2e_job_run.exe" }'''),
    ('''$fxCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $(& $q "$e\\ke_fixture.ps1") -Root $(& $q $root) -RunId $runId " +
         "-HostSec 2400 -ReadyTimeoutSec $ReadyTimeoutSec -StopWaitSec $StopWaitSec"''',
     '''$fxCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $(& $q "$e\\oa_fixture.ps1") -Root $(& $q $root) -RunId $runId " +
         "-HostSec 2400 -ReadyTimeoutSec $ReadyTimeoutSec -StopWaitSec $StopWaitSec -Arm $Arm"
if ($BuildDir) { $fxCmd += " -BuildDir $(& $q $BuildDir)" }
if ($ServerJs) { $fxCmd += " -ServerJs $(& $q $ServerJs)" }
if ($WithStandby) { $fxCmd += " -WithStandby" }
if ($HolderCmd.Count) { $fxCmd += " -HolderCmd $(& $q ($HolderCmd -join ','))" }'''),
    ('''    $ready = (Get-Content -LiteralPath "$root\\fixture_ready.txt" -Raw) -match "^ready run=$([regex]::Escape($runId)) hosts=a,b,c"''',
     '''    $ready = (Get-Content -LiteralPath "$root\\fixture_ready.txt" -Raw) -match "^ready run=$([regex]::Escape($runId)) hosts=a "'''),
    ('''      & python -I "$e\\ke_avd.py" $root $ApkOff $ApkOn "$root\\avd" > "$root\\avd_stdout.txt" 2> "$root\\avd_stderr.txt"''',
     '''      & python -I "$e\\oa_avd.py" $root $Arm $Apk "$root\\avd" > "$root\\avd_stdout.txt" 2> "$root\\avd_stderr.txt"'''),
])

convert('ke_fixture.ps1', 'oa_fixture.ps1', [
    ('''  [string[]]$HarnessCmd = @(),
  [string[]]$NodeCmd = @()
)''', '''  [string[]]$HarnessCmd = @(),
  [string[]]$NodeCmd = @(),
  # rel-0164 old-APK lane
  [ValidateSet('S', 'P')][string]$Arm = 'S',
  [string]$BuildDir = '',
  [string]$ServerJs = '',
  [switch]$WithStandby,
  [string[]]$HolderCmd = @(),
  [int]$ControlPort = 18460
)
# rel-0164 old-APK lane: made from the AVD harness r8 ke_fixture.ps1 (the supervisor rules unchanged) for ONE pinned test
# host, oa-pc-a, through the shared injection / power guard; arm P adds the host's TCP control listener (127.0.0.1:
# $ControlPort, APK 0.2.32's manual connect); -WithStandby holds ONE PC-side standby lease on it
# (remote60_ka_standby_holder_tool: the product managed viewer, Init only) once the host is ready -- its own readiness
# (standby\\standby_ready.txt) is part of the fixture's; it is ended through its stop file like a harness.'''),
    ('''if ($HarnessCmd.Count -eq 1) { $HarnessCmd = @($HarnessCmd[0] -split ',') }''',
     '''if ($HarnessCmd.Count -eq 1) { $HarnessCmd = @($HarnessCmd[0] -split ',') }
if ($HolderCmd.Count -eq 1) { $HolderCmd = @($HolderCmd[0] -split ',') }'''),
    ('''$wt = 'C:\\work\\gnlink-wt\\feat-apk-0233'
$e = "$wt\\.claude\\e2e"
$anchor = "$e\\runs"''', '''$wt = (Resolve-Path "$PSScriptRoot\\..\\..").Path
$e = $PSScriptRoot
$anchor = "$wt\\.claude\\oa_runs"'''),
    ('''foreach ($n in 'a', 'b', 'c', 'dir', 'fixture.txt',''', '''foreach ($n in 'a', 'standby', 'dir', 'fixture.txt','''),
    ('''  "ketest $pw" | Out-File "$Root\\fixture_account.txt" -Encoding ascii''',
     '''  "oatest $pw" | Out-File "$Root\\fixture_account.txt" -Encoding ascii'''),
    ('''    REMOTE60_RELAY_ALLOW_ACCOUNTS = 'ketest' }''', '''    REMOTE60_RELAY_ALLOW_ACCOUNTS = 'oatest' }'''),
    ('''    $server = "$wt\\apps\\directory\\server.js"
    if ($env:KE_SERVER_JS) {''', '''    $server = "$wt\\apps\\directory\\server.js"
    if ($ServerJs) { $env:KE_SERVER_JS = $ServerJs }
    if ($env:KE_SERVER_JS) {'''),
    ('''    & node $server --add-account ketest $pw *> "$Root\\dir\\add_account.txt"''',
     '''    & node $server --add-account oatest $pw *> "$Root\\dir\\add_account.txt"'''),
    ('''  $b = "$wt\\build\\apps\\native_poc\\Release"''', '''  $b = if ($BuildDir) { $BuildDir } else { "$wt\\build\\apps\\native_poc\\Release" }'''),
    ('''  $hostsDef = @(
    @{ n = 'a'; name = 'ke-pc-a'; mark = 'ff00ff' },
    @{ n = 'b'; name = 'ke-pc-b'; mark = '00ffff' },
    @{ n = 'c'; name = 'ke-pc-c'; mark = 'ffff00' })''', '''  $hostsDef = @(@{ n = 'a'; name = 'oa-pc-a'; mark = 'ff00ff' })'''),
    ("""      '--dir-url', 'http://127.0.0.1:18410', '--dir-id', 'ketest', '--dir-pw', $pw, '--host-name', $h.name, '--target-mark', $h.mark,""",
     """      '--dir-url', 'http://127.0.0.1:18410', '--dir-id', 'oatest', '--dir-pw', $pw, '--host-name', $h.name, '--target-mark', $h.mark,"""),
    ('''    $pr = Start-Owned $h.n $HarnessCmd[0] $argv "$out\\harness" @{ REMOTE60_ALLOW_HOST_E2E = '1';''',
     '''    if ($Arm -eq 'P') { $argv += @('--host-control-port', "$ControlPort") }
    $pr = Start-Owned $h.n $HarnessCmd[0] $argv "$out\\harness" @{ REMOTE60_ALLOW_HOST_E2E = '1';'''),
    ('''    REMOTE60_PINNED_TEST_FAULT_FILE = "$out\\fault.txt"; REMOTE60_PINNED_TEST_MACHINE_ID = "ke-fixture-machine-$($h.n)" }''',
     '''    REMOTE60_PINNED_TEST_FAULT_FILE = "$out\\fault.txt"; REMOTE60_PINNED_TEST_MACHINE_ID = "oa-fixture-machine-$($h.n)" }'''),
    ('''  # ---- every host ready AND every child still alive: said whole, then wait for the bundle's stop
  $dead = Dead-Child
  if ($dead) { throw "before the fixture's readiness: $dead" }
  "ready run=$RunId hosts=a,b,c" | Out-File "$Root\\fixture_ready.tmp" -Encoding ascii''',
     '''  # ---- case 7: ONE PC-side standby lease on oa-pc-a, held by its own child (in this job, ended by its stop file)
  $sb = 'none'
  if ($WithStandby) {
    $sdir = "$Root\\standby"
    New-Item -ItemType Directory $sdir | Out-Null
    $hc = if ($HolderCmd.Count) { $HolderCmd } else { @("$b\\remote60_ka_standby_holder_tool.exe") }
    $hargv = @($hc | Select-Object -Skip 1) + @('--url', 'http://127.0.0.1:18410', '--host-name', 'oa-pc-a',
      '--account-file', "$Root\\fixture_account.txt", '--viewer', "$b\\GNLinkViewerKaTest.exe", '--root', $sdir,
      '--stop-file', "$sdir\\stop", '--max-sec', "$HostSec")
    if (-not $HolderCmd.Count) { Say "  standby holder $((Get-FileHash $hc[0]).Hash.ToLower().Substring(0,16)) viewer $((Get-FileHash "$b\\GNLinkViewerKaTest.exe").Hash.ToLower().Substring(0,16))" }
    $hpr = Start-Owned 'standby' $hc[0] $hargv "$sdir\\holder" @{}
    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
    while ((Get-Date) -lt $deadline -and -not $hpr.HasExited -and -not (Test-Path -LiteralPath "$sdir\\standby_ready.txt")) { Start-Sleep -Milliseconds 250 }
    Start-Sleep -Milliseconds 300
    if ($hpr.HasExited) { throw "standby: its holder ended (exit $($hpr.ExitCode)) -- not ready" }
    if (-not (Test-Path -LiteralPath "$sdir\\standby_ready.txt")) { throw "standby: no ready lease within $ReadyTimeoutSec s" }
    $sb = 'ready'
    Say "  standby on oa-pc-a ready: $((Get-Content -LiteralPath "$sdir\\standby_ready.txt" -Raw).Trim())"
  }
  $cp = if ($Arm -eq 'P') { $ControlPort } else { 0 }

  # ---- every host ready AND every child still alive: said whole, then wait for the bundle's stop
  $dead = Dead-Child
  if ($dead) { throw "before the fixture's readiness: $dead" }
  "ready run=$RunId hosts=a standby=$sb controlPort=$cp" | Out-File "$Root\\fixture_ready.tmp" -Encoding ascii'''),
])
