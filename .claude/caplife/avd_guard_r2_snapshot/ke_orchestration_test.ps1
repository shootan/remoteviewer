# AVD guard r2 (Codex codex_avd_guard_src_r1.md N1 / N2): the bundle's orchestration -- ke_slot.ps1 -> ke_fixture.ps1 ->
# harnesses -> AVD -> the fixture's end -- with stand-ins for the harness, the directory and the AVD (stubs\*.py): no
# host, no node server, no window, no adb, no emulator. Each case is a whole ke_slot.ps1 run in its own NEW run folder
# (under .claude\e2e\runs, inside the repository); nothing outside it is created or deleted.
# Checked per case: whether the AVD started, which hosts were started, the bundle's exit code, no pid file anywhere.
param([switch]$Quick)
$ErrorActionPreference = 'Continue'
$e = 'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e'
$py = (Get-Command python).Source
$common = @('-HarnessCmd', "$py,-I,$e\stubs\stub_harness.py", '-NodeCmd', "$py,-I,$e\stubs\stub_node.py",
  '-AvdCmd', $py, '-I', "$e\stubs\stub_avd.py")
$script:checks = 0; $script:fails = 0
function Check([string]$what, [bool]$ok, [string]$detail = '') {
  $script:checks++; if (-not $ok) { $script:fails++ }
  "$(if ($ok) { 'PASS' } else { 'FAIL' })  $what$(if ($detail) { "  $detail" })"
}
function Run-Case([string]$tag, [string]$plan, [hashtable]$extraEnv) {
  $env:KE_STUB_PLAN = $plan
  foreach ($k in $extraEnv.Keys) { Set-Item "env:$k" $extraEnv[$k] }
  $before = @(Get-ChildItem "$e\runs" -Directory -ErrorAction SilentlyContinue | ForEach-Object Name)
  $p = Start-Process -FilePath powershell -ArgumentList (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\ke_slot.ps1",
    '-Tag', $tag, '-ReadyTimeoutSec', '5', '-FixtureReadyTimeoutSec', '90', '-StopWaitSec', '5',
    '-HarnessCmd', "$py,-I,$e\stubs\stub_harness.py", '-NodeCmd', "$py,-I,$e\stubs\stub_node.py",
    '-AvdCmd', "$py,-I,$e\stubs\stub_avd.py")) -PassThru -NoNewWindow -RedirectStandardOutput "$e\runs\_orch_$tag.out.txt" -RedirectStandardError "$e\runs\_orch_$tag.err.txt"
  $null = $p.Handle
  $done = $p.WaitForExit(240000)
  if (-not $done) { $p.Kill(); $null = $p.WaitForExit(15000) }
  foreach ($k in $extraEnv.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
  Remove-Item env:KE_STUB_PLAN -ErrorAction SilentlyContinue
  $new = @(Get-ChildItem "$e\runs" -Directory | Where-Object { $before -notcontains $_.Name -and $_.Name -like "$tag-*" })
  $root = if ($new.Count -eq 1) { $new[0].FullName } else { $null }
  $fx = if ($root -and (Test-Path "$root\fixture.txt")) { Get-Content "$root\fixture.txt" -Raw } else { '' }
  [pscustomobject]@{
    rc = if ($done) { $p.ExitCode } else { 'timeout' }; root = $root
    avd = [bool]($root -and (Test-Path "$root\AVD_STARTED.txt"))
    started = @('a', 'b', 'c' | Where-Object { $fx -match "started $_ pid" })
    pidFiles = @(if ($root) { Get-ChildItem $root -Recurse -Filter *.pid -ErrorAction SilentlyContinue })
    out = (Get-Content "$e\runs\_orch_$tag.out.txt" -Raw -ErrorAction SilentlyContinue)
    fx = $fx
  }
}
if (-not (Test-Path "$e\runs")) { New-Item -ItemType Directory "$e\runs" | Out-Null }

"--- control: every host ready, the AVD passes"
$r = Run-Case 'orch-ok' 'a=ok,b=ok,c=ok' @{}
Check 'control: a/b/c started, the AVD started, rc 0, no pid file' ($r.avd -and $r.started.Count -eq 3 -and $r.rc -eq 0 -and $r.pidFiles.Count -eq 0) "rc=$($r.rc) started=$($r.started -join '') avd=$($r.avd)"

"--- host b's harness ends at once (staging / launch / guard failure)"
$r = Run-Case 'orch-exit2' 'a=ok,b=exit2,c=ok' @{}
Check 'b exit2: no AVD, c never started, rc != 0' (-not $r.avd -and ($r.started -join '') -eq 'ab' -and $r.rc -ne 0) "rc=$($r.rc) started=$($r.started -join '') avd=$($r.avd)"

"--- host b writes a 'failed' readiness"
$r = Run-Case 'orch-failed' 'a=ok,b=failed,c=ok' @{}
Check 'b failed readiness: no AVD, c never started, rc != 0' (-not $r.avd -and ($r.started -join '') -eq 'ab' -and $r.rc -ne 0) "rc=$($r.rc) started=$($r.started -join '')"

"--- host b writes another run's readiness"
$r = Run-Case 'orch-foreign' 'a=ok,b=foreign,c=ok' @{}
Check 'b foreign readiness: no AVD, c never started, rc != 0' (-not $r.avd -and ($r.started -join '') -eq 'ab' -and $r.rc -ne 0) "rc=$($r.rc) started=$($r.started -join '')"

"--- host b never says ready (readiness timeout, 5 s)"
$r = Run-Case 'orch-silent' 'a=ok,b=silent,c=ok' @{}
Check 'b silent: no AVD, c never started, rc != 0' (-not $r.avd -and ($r.started -join '') -eq 'ab' -and $r.rc -ne 0) "rc=$($r.rc) started=$($r.started -join '')"

"--- the directory dies at once (fixture failure before any host)"
$r = Run-Case 'orch-nodedies' 'a=ok,b=ok,c=ok' @{ KE_STUB_NODE = 'dies' }
Check 'node dies: no host, no AVD, rc != 0' (-not $r.avd -and $r.started.Count -eq 0 -and $r.rc -ne 0) "rc=$($r.rc) started=$($r.started -join '')"

"--- host c fails late (after the readiness, during the AVD), the AVD itself passes"
$r = Run-Case 'orch-late' 'a=ok,b=ok,c=late' @{ KE_STUB_AVD_SEC = '6' }
Check 'c late INVALID: the AVD ran and passed, the bundle still fails (rc 3)' ($r.avd -and $r.rc -eq 3 -and $r.fx -match 'LATE FAILURE: c ended') "rc=$($r.rc) avd=$($r.avd)"

"--- host a ignores its stop: ended by the fixture's own handle, and that is a failure"
$r = Run-Case 'orch-nostop' 'a=nostop,b=ok,c=ok' @{}
Check 'a nostop: killed by its handle, the bundle fails (rc 3)' ($r.avd -and $r.rc -eq 3 -and $r.fx -match 'a killed by its handle') "rc=$($r.rc)"

"--- the AVD fails, every host fine"
$r = Run-Case 'orch-avdfail' 'a=ok,b=ok,c=ok' @{ KE_STUB_AVD_RC = '1' }
Check 'AVD rc 1, fixture fine: the bundle says the AVD (rc 1)' ($r.avd -and $r.rc -eq 1) "rc=$($r.rc)"

"--- a run folder is never reused: the fixture refuses a root that already holds a host"
$stale = "$e\runs\orch-stale-0"
if (-not (Test-Path $stale)) { New-Item -ItemType Directory $stale | Out-Null; New-Item -ItemType Directory "$stale\a" | Out-Null }
$fx = Start-Process -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\ke_fixture.ps1",
  '-Root', $stale, '-RunId', 'orch-stale-0', '-HarnessCmd', "$py,-I,$e\stubs\stub_harness.py", '-NodeCmd', "$py,-I,$e\stubs\stub_node.py") `
  -PassThru -NoNewWindow -RedirectStandardOutput "$e\runs\_orch_stale.out.txt"
$null = $fx.Handle; $null = $fx.WaitForExit(60000)
Check 'stale root: the fixture refuses (exit 2), starts nothing' ($fx.ExitCode -eq 2 -and -not (Test-Path "$stale\dir") -and
  ((Get-Content "$e\runs\_orch_stale.out.txt" -Raw) -match 'already exists')) "exit=$($fx.ExitCode)"
"--- a root outside the anchor is refused"
$fx = Start-Process -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\ke_fixture.ps1",
  '-Root', "$e", '-RunId', 'x') -PassThru -NoNewWindow -RedirectStandardOutput "$e\runs\_orch_outside.out.txt"
$null = $fx.Handle; $null = $fx.WaitForExit(60000)
Check 'a root not directly under the anchor: refused (exit 2)' ($fx.ExitCode -eq 2) "exit=$($fx.ExitCode)"

"`n$script:checks checks, $script:fails failed"
"RESULT: $(if ($script:fails -eq 0) { 'PASS' } else { 'FAIL' })"
exit $(if ($script:fails -eq 0) { 0 } else { 1 })
