# rel-0164 old-APK lane: the bundle's orchestration -- oa_slot.ps1 -> oa_fixture.ps1 -> harness (+ standby holder) -> AVD
# -> the fixture's end -- with stand-ins for the harness, the directory, the standby holder and the AVD (stubs\*.py): no
# host, no node server, no viewer, no window, no adb, no emulator. Each case is a whole oa_slot.ps1 run in its own NEW
# run folder (under <worktree>\.claude\oa_runs, inside the repository); nothing outside it is created or deleted.
# Checked per case: whether the AVD started, what the fixture started and passed, the bundle's exit code, what is left.
$ErrorActionPreference = 'Continue'
$e = $PSScriptRoot
$wt = (Resolve-Path "$e\..\..").Path
$runs = "$wt\.claude\oa_runs"
$py = (Get-Command python).Source
$script:checks = 0; $script:fails = 0
function Check([string]$what, [bool]$ok, [string]$detail = '') {
  $script:checks++; if (-not $ok) { $script:fails++ }
  "$(if ($ok) { 'PASS' } else { 'FAIL' })  $what$(if ($detail) { "  $detail" })"
}
function Run-Case([string]$tag, [string]$plan, [hashtable]$extraEnv, [string[]]$slotArgs, [bool]$defaultHolder = $false) {
  $env:KE_STUB_PLAN = $plan
  foreach ($k in $extraEnv.Keys) { Set-Item "env:$k" $extraEnv[$k] }
  $before = @(Get-ChildItem $runs -Directory -ErrorAction SilentlyContinue | ForEach-Object Name)
  # r4: $defaultHolder -- no -HolderCmd: the fixture runs <BuildDir>\remote60_ka_standby_holder_tool.exe itself
  $holderArgs = if ($defaultHolder) { @() } else { @('-HolderCmd', "$py,-I,$e\stubs\stub_holder.py") }
  $p = Start-Process -FilePath powershell -ArgumentList (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\oa_slot.ps1",
    '-Tag', $tag, '-ReadyTimeoutSec', '5', '-FixtureReadyTimeoutSec', '90', '-StopWaitSec', '5',
    '-HarnessCmd', "$py,-I,$e\stubs\stub_harness.py", '-NodeCmd', "$py,-I,$e\stubs\stub_node.py",
    '-AvdCmd', "$py,-I,$e\stubs\stub_avd.py") + $holderArgs + $slotArgs) `
    -PassThru -NoNewWindow -RedirectStandardOutput "$runs\_orch_$tag.out.txt" -RedirectStandardError "$runs\_orch_$tag.err.txt"
  $null = $p.Handle
  $done = $p.WaitForExit(240000)
  if (-not $done) { $p.Kill(); $null = $p.WaitForExit(15000) }
  foreach ($k in $extraEnv.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
  Remove-Item env:KE_STUB_PLAN -ErrorAction SilentlyContinue
  $new = @(Get-ChildItem $runs -Directory | Where-Object { $before -notcontains $_.Name -and $_.Name -like "$tag-*" })
  $root = if ($new.Count -eq 1) { $new[0].FullName } else { $null }
  function Raw([string]$rel) { if ($root -and (Test-Path -LiteralPath "$root\$rel")) { Get-Content -LiteralPath "$root\$rel" -Raw } else { '' } }
  $fx = Raw 'fixture.txt'
  [pscustomobject]@{
    rc = if ($done) { $p.ExitCode } else { 'timeout' }; root = $root
    avd = [bool]($root -and (Test-Path "$root\AVD_STARTED.txt"))
    avdSaw = Raw 'AVD_STARTED.txt'
    hostStarted = $fx -match 'started a pid'
    holderStarted = $fx -match 'started standby pid'
    harnessArgs = Raw 'a\harness_args.txt'
    holderArgs = Raw 'standby\holder_args.txt'
    pidFiles = @(if ($root) { Get-ChildItem $root -Recurse -Filter *.pid -ErrorAction SilentlyContinue })
    left = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like "*$e\stubs\stub_*" -or $_.CommandLine -like "*oa_fixture.ps1*" -or
      $_.CommandLine -like '*_stubbuild-*remote60_ka_standby_holder_tool.exe*' }).Count
    slot = Raw 'slot.txt'; result = Raw 'fixture_result.txt'; fx = $fx
  }
}
if (-not (Test-Path $runs)) { New-Item -ItemType Directory $runs | Out-Null }

"--- arm P + standby (r6: case 7 runs with arm P): host ready, standby ready, the AVD passes; relay 127.0.0.1"
$r = Run-Case 'orch-p-sb' 'a=ok' @{} @('-Arm', 'P', '-WithStandby')
Check 'P+standby: host and holder started, AVD saw "standby=ready controlPort=18460 relay=127.0.0.1", rc 0, nothing left' ($r.avd -and
  $r.hostStarted -and $r.holderStarted -and $r.avdSaw -match 'hosts=a standby=ready controlPort=18460 relay=127\.0\.0\.1' -and
  $r.rc -eq 0 -and $r.pidFiles.Count -eq 0 -and $r.left -eq 0) "rc=$($r.rc) avd=$($r.avd) saw=$($r.avdSaw.Trim())"
Check 'P+standby: the holder got this run''s standby root, stop file, oa-pc-a, the account file' (
  $r.harnessArgs -match '--host-name oa-pc-a' -and
  $r.holderArgs -match '--host-name oa-pc-a' -and $r.holderArgs -match [regex]::Escape("--root $($r.root)\standby ") -and
  $r.holderArgs -match [regex]::Escape("--stop-file $($r.root)\standby\stop") -and $r.holderArgs -match 'fixture_account\.txt' -and
  $r.result -match '  standby exit 0' -and $r.result -match '  a exit 0')
Check 'the test account file is in the run folder, its password never in slot.txt / fixture.txt' ($r.root -and
  (Test-Path "$($r.root)\fixture_account.txt") -and -not (
    ($r.slot + $r.fx) -match [regex]::Escape(((Get-Content "$($r.root)\fixture_account.txt" -Raw).Trim() -split ' ')[1])))

"--- arm P, no standby: the host's TCP control port is passed, no holder"
$r = Run-Case 'orch-p' 'a=ok' @{} @('-Arm', 'P')
Check 'P: harness got --host-control-port 18460, no holder, AVD saw "standby=none controlPort=18460", rc 0' ($r.avd -and
  $r.harnessArgs -match '--host-control-port 18460' -and -not $r.holderStarted -and
  $r.avdSaw -match 'standby=none controlPort=18460' -and $r.rc -eq 0 -and $r.left -eq 0) "rc=$($r.rc) saw=$($r.avdSaw.Trim())"

"--- arm S, no standby: no --host-control-port, relay 10.0.2.2 (the phone's)"
$r = Run-Case 'orch-s' 'a=ok' @{} @('-Arm', 'S')
Check 'S: harness got no --host-control-port, no holder, AVD saw "standby=none controlPort=0 relay=10.0.2.2", rc 0' ($r.avd -and
  $r.harnessArgs -notmatch '--host-control-port' -and -not $r.holderStarted -and
  $r.avdSaw -match 'standby=none controlPort=0 relay=10\.0\.2\.2' -and $r.rc -eq 0 -and $r.left -eq 0) "rc=$($r.rc) saw=$($r.avdSaw.Trim())"

"--- r6: arm S + standby is refused before anything starts (the PC cannot reach the phone's relay address)"
$r = Run-Case 'orch-s-sb-refused' 'a=ok' @{} @('-Arm', 'S', '-WithStandby')
Check 'S+standby: refused (rc 2), no node / host / holder started, no AVD, the reason said' (-not $r.avd -and $r.rc -eq 2 -and
  -not $r.hostStarted -and -not $r.holderStarted -and $r.fx -notmatch 'started node' -and
  $r.fx -match 'arm S cannot hold a PC-side standby lease') "rc=$($r.rc)"

"--- standby never ready (5 s)"
$r = Run-Case 'orch-sb-silent' 'a=ok' @{ OA_STUB_HOLDER = 'silent' } @('-Arm', 'P', '-WithStandby')
Check 'standby silent: no AVD, rc != 0, holder ended by its stop (exit 3), host ended 0, nothing left' (-not $r.avd -and $r.rc -ne 0 -and
  $r.fx -match 'no ready lease' -and $r.result -match '  standby exit 3' -and $r.result -match '  a exit 0' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

"--- standby holder dies at once (the viewer died)"
$r = Run-Case 'orch-sb-dies' 'a=ok' @{ OA_STUB_HOLDER = 'dies' } @('-Arm', 'P', '-WithStandby')
Check 'standby dies: no AVD, rc != 0, nothing left' (-not $r.avd -and $r.rc -ne 0 -and $r.fx -match 'standby: its holder ended \(exit 4\)' -and
  $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

"--- standby ready, then dies during the AVD (a late failure); the AVD itself passes"
$r = Run-Case 'orch-sb-late' 'a=ok' @{ OA_STUB_HOLDER = 'readydie'; OA_STUB_AVD_SEC = '6' } @('-Arm', 'P', '-WithStandby')
Check 'standby late: the AVD ran and passed, the bundle still fails (rc 3)' ($r.avd -and $r.rc -eq 3 -and
  $r.fx -match 'LATE FAILURE: standby ended' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

"--- r4: the DEFAULT holder path (no -HolderCmd): <BuildDir>\remote60_ka_standby_holder_tool.exe, one element"
# a stand-in build folder of this run: the holder is remote60_oa_stub_holder.exe (no viewer, no network) under the
# real name; the viewer exe is only hashed by the fixture
$stubBuild = "$runs\_stubbuild-$PID"
New-Item -ItemType Directory $stubBuild | Out-Null
$stubExe = "$wt\build\apps\native_poc\Release\remote60_oa_stub_holder.exe"
Copy-Item $stubExe "$stubBuild\remote60_ka_standby_holder_tool.exe"
Copy-Item $stubExe "$stubBuild\GNLinkViewerKaTest.exe"
$r = Run-Case 'orch-sb-default' 'a=ok' @{} @('-Arm', 'P', '-WithStandby', '-BuildDir', $stubBuild) $true
Check 'default holder path: the fixture ran <BuildDir>\remote60_ka_standby_holder_tool.exe (hashed, started, ready), rc 0, nothing left' (
  $r.avd -and $r.rc -eq 0 -and $r.holderStarted -and $r.fx -match 'standby holder [0-9a-f]{16} viewer [0-9a-f]{16}' -and
  $r.holderArgs -match [regex]::Escape("--root $($r.root)\standby") -and $r.result -match '  standby exit 0' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"
"--- r4: a relative -BuildDir is refused before anything starts"
$r = Run-Case 'orch-relbuild' 'a=ok' @{} @('-Arm', 'P', '-WithStandby', '-BuildDir', 'relative\build')
Check 'relative -BuildDir: refused (exit 2), no fixture started' ($r.rc -eq 2 -and -not $r.hostStarted -and -not $r.avd) "rc=$($r.rc)"

"--- r3 N2: the holder was ready, but its end was not a clean Release (exit 5): the bundle fails"
$r = Run-Case 'orch-sb-rel5' 'a=ok' @{ OA_STUB_HOLDER = 'release5' } @('-Arm', 'P', '-WithStandby')
Check 'standby rc 5 at its stop: the AVD ran, the fixture counts it (standby exit 5), the bundle fails (rc 3)' ($r.avd -and
  $r.rc -eq 3 -and $r.result -match '  standby exit 5' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"
"--- the host's harness ends at once (staging / launch / guard failure), standby asked"
$r = Run-Case 'orch-exit2' 'a=exit2' @{} @('-Arm', 'P', '-WithStandby')
Check 'host exit2: holder never started, no AVD, rc != 0, nothing left' (-not $r.avd -and $r.hostStarted -and -not $r.holderStarted -and
  $r.rc -ne 0 -and $r.left -eq 0) "rc=$($r.rc)"

"--- the host writes another run's readiness"
$r = Run-Case 'orch-foreign' 'a=foreign' @{} @('-Arm', 'P', '-WithStandby')
Check 'foreign readiness: holder never started, no AVD, rc != 0' (-not $r.avd -and -not $r.holderStarted -and $r.rc -ne 0) "rc=$($r.rc)"

"--- the directory dies at once"
$r = Run-Case 'orch-nodedies' 'a=ok' @{ KE_STUB_NODE = 'dies' } @('-Arm', 'P', '-WithStandby')
Check 'node dies: no host, no holder, no AVD, rc != 0' (-not $r.avd -and -not $r.hostStarted -and -not $r.holderStarted -and $r.rc -ne 0) "rc=$($r.rc)"

"--- the host fails late (during the AVD)"
$r = Run-Case 'orch-late' 'a=late' @{ OA_STUB_AVD_SEC = '6' } @('-Arm', 'P', '-WithStandby')
Check 'host late: the AVD ran, the bundle fails (rc 3), the holder still ended 0' ($r.avd -and $r.rc -eq 3 -and $r.fx -match 'LATE FAILURE: a ended' -and
  $r.result -match '  standby exit 0' -and $r.left -eq 0) "rc=$($r.rc)"

"--- the AVD fails, the fixture fine"
$r = Run-Case 'orch-avdfail' 'a=ok' @{ OA_STUB_AVD_RC = '1' } @('-Arm', 'P', '-WithStandby')
Check 'AVD rc 1: the bundle says the AVD (rc 1)' ($r.avd -and $r.rc -eq 1 -and $r.left -eq 0) "rc=$($r.rc)"

"--- the fixture crashes right after its readiness: its job ends the host, the holder and the node"
$r = Run-Case 'orch-crash' 'a=ok' @{ KE_FIXTURE_TEST_CRASH_AFTER_READY = '1' } @('-Arm', 'P', '-WithStandby')
Check 'fixture crash: residual ended by the job (confirmed), rc 3, nothing left' ($r.rc -eq 3 -and
  $r.slot -match 'job_run: child_exit=9 residual=[1-9]\d* forced=0 ended_confirmed=1' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left) $([regex]::Match($r.slot, 'job_run:[^\r\n]*').Value)"

"--- stopping the holder throws (one cleanup error): host and node still ended"
$r = Run-Case 'orch-stopthrow' 'a=ok' @{ KE_FIXTURE_TEST_STOP_THROW_FOR = 'standby' } @('-Arm', 'P', '-WithStandby')
Check 'one cleanup error: holder killed after its error, a and node ended, rc 3, nothing left' ($r.rc -eq 3 -and $r.result -match 'standby stop error' -and
  $r.result -match '  a exit 0' -and $r.result -match '  node ended' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

"--- r7f: the relay trace -- the variable alone traces nothing (53fced4); the pinned trace-only copy must say it is on"
$stubNodeSha = (Get-FileHash "$e\stubs\stub_node.py").Hash.ToLower()
$r = Run-Case 'orch-trace-ok' 'a=ok' @{ KE_RELAY_TRACE = '1'; KE_STUB_NODE_TRACE = '1' } @('-Arm', 'S', '-ServerSha256', $stubNodeSha)
Check 'trace on, the pinned directory says "[relay-trace] ENABLED": host started, AVD ran, rc 0' ($r.avd -and $r.hostStarted -and
  $r.rc -eq 0 -and $r.fx -match 'relay trace: the directory says it is on' -and $r.fx -match 'directory pinned:' -and $r.left -eq 0) "rc=$($r.rc)"
$r = Run-Case 'orch-trace-silent' 'a=ok' @{ KE_RELAY_TRACE = '1' } @('-Arm', 'S', '-ServerSha256', $stubNodeSha)
Check 'NEGATIVE trace on, the directory never says so (53fced4 as it is): no host, no AVD, rc != 0, the reason said' (-not $r.avd -and
  -not $r.hostStarted -and $r.rc -ne 0 -and $r.fx -match "never said '\[relay-trace\] ENABLED'" -and $r.left -eq 0) "rc=$($r.rc)"
$r = Run-Case 'orch-trace-nopin' 'a=ok' @{ KE_RELAY_TRACE = '1'; KE_STUB_NODE_TRACE = '1' } @('-Arm', 'S')
Check 'NEGATIVE trace on with no -ServerSha256: the directory is not even started, no host, no AVD' (-not $r.avd -and
  -not $r.hostStarted -and $r.rc -ne 0 -and $r.fx -notmatch 'started node' -and $r.fx -match 'needs -ServerSha256') "rc=$($r.rc)"
$r = Run-Case 'orch-trace-wrongpin' 'a=ok' @{ KE_RELAY_TRACE = '1'; KE_STUB_NODE_TRACE = '1' } @('-Arm', 'S', '-ServerSha256', ('0' * 64))
Check 'NEGATIVE trace on, the directory file is not the pinned one: not started, no host, no AVD' (-not $r.avd -and
  -not $r.hostStarted -and $r.rc -ne 0 -and $r.fx -notmatch 'started node' -and $r.fx -match 'is not the pinned one') "rc=$($r.rc)"

"--- a real run without an APK file is refused before anything starts"
$before = @(Get-ChildItem $runs -Directory | ForEach-Object Name)
$p = Start-Process -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\oa_slot.ps1", '-Tag', 'orch-noapk',
  '-Apk', "$runs\no-such.apk") -PassThru -NoNewWindow -RedirectStandardOutput "$runs\_orch_noapk.out.txt"
$null = $p.Handle; $null = $p.WaitForExit(60000)
$nr = @(Get-ChildItem $runs -Directory | Where-Object { $before -notcontains $_.Name -and $_.Name -like 'orch-noapk-*' })
Check 'no APK: exit 2, no fixture started' ($p.ExitCode -eq 2 -and $nr.Count -eq 1 -and -not (Test-Path "$($nr[0].FullName)\fixture.txt")) "exit=$($p.ExitCode)"

"--- a run folder is never reused / a root outside the anchor is refused"
$stale = "$runs\orch-stale-0"
if (-not (Test-Path $stale)) { New-Item -ItemType Directory $stale | Out-Null; New-Item -ItemType Directory "$stale\standby" | Out-Null }
$fx = Start-Process -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\oa_fixture.ps1",
  '-Root', $stale, '-RunId', 'orch-stale-0', '-HarnessCmd', "$py,-I,$e\stubs\stub_harness.py", '-NodeCmd', "$py,-I,$e\stubs\stub_node.py") `
  -PassThru -NoNewWindow -RedirectStandardOutput "$runs\_orch_stale.out.txt"
$null = $fx.Handle; $null = $fx.WaitForExit(60000)
Check 'stale root (holds standby\): refused (exit 2), starts nothing' ($fx.ExitCode -eq 2 -and -not (Test-Path "$stale\dir") -and
  ((Get-Content "$runs\_orch_stale.out.txt" -Raw) -match 'already exists')) "exit=$($fx.ExitCode)"
$fx = Start-Process -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\oa_fixture.ps1",
  '-Root', "$e", '-RunId', 'x') -PassThru -NoNewWindow -RedirectStandardOutput "$runs\_orch_outside.out.txt"
$null = $fx.Handle; $null = $fx.WaitForExit(60000)
Check 'a root not directly under the anchor: refused (exit 2)' ($fx.ExitCode -eq 2) "exit=$($fx.ExitCode)"

"`n$script:checks checks, $script:fails failed"
"RESULT: $(if ($script:fails -eq 0) { 'PASS' } else { 'FAIL' })"
exit $(if ($script:fails -eq 0) { 0 } else { 1 })
