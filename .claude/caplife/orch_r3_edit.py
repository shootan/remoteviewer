p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_orchestration_test.ps1'
s = open(p, encoding='utf-8', newline='').read()
a = r'''    out = (Get-Content "$e\runs\_orch_$tag.out.txt" -Raw -ErrorAction SilentlyContinue)'''
assert s.count(a) == 1, s.count(a)
s = s.replace(a, a + r'''
    left = @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like "*$e\stubs\stub_*" -or $_.CommandLine -like "*ke_fixture.ps1*" }).Count
    slot = if ($root -and (Test-Path "$root\slot.txt")) { Get-Content "$root\slot.txt" -Raw } else { '' }
    result = if ($root -and (Test-Path "$root\fixture_result.txt")) { Get-Content "$root\fixture_result.txt" -Raw } else { '' }''')
a = '"--- a run folder is never reused: the fixture refuses a root that already holds a host"'
assert s.count(a) == 1
new = r'''"--- G1: b writes a valid readiness, then dies at once"
$r = Run-Case 'orch-readyexit' 'a=ok,b=readyexit,c=ok' @{}
Check 'G1 b ready-then-dead: not taken as ready -- c never started, no AVD, rc != 0, nothing left' (-not $r.avd -and ($r.started -join '') -eq 'ab' -and $r.rc -ne 0 -and $r.left -eq 0) "rc=$($r.rc) started=$($r.started -join '') left=$($r.left)"

"--- G1: a dies while c is being prepared (c slow to be ready)"
$r = Run-Case 'orch-readydie' 'a=readydie,b=ok,c=slowready' @{}
Check 'G1 a dead before the fixture readiness: no fixture_ready, no AVD, rc != 0, nothing left' (-not $r.avd -and $r.rc -ne 0 -and $r.fx -match 'a ended' -and $r.left -eq 0) "rc=$($r.rc) started=$($r.started -join '') left=$($r.left)"

"--- G1: c writes a valid readiness, then dies at once"
$r = Run-Case 'orch-cexit' 'a=ok,b=ok,c=readyexit' @{}
Check 'G1 c ready-then-dead: no AVD, rc != 0, nothing left' (-not $r.avd -and $r.rc -ne 0 -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

"--- G2: host b's launch throws (its harness path does not exist): node and a still ended"
$r = Run-Case 'orch-throw' 'a=ok,b=ok,c=ok' @{ KE_FIXTURE_TEST_BAD_EXE_FOR = 'b' }
Check 'G2 launch throw: no AVD, rc != 0, a and node ended by the fixture (fixture_result), nothing left' (-not $r.avd -and $r.rc -ne 0 -and
  $r.result -match '  a exit 0' -and $r.result -match '  node ended' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

"--- G2: the fixture crashes right after its readiness (no cleanup of its own): its job ends the rest"
$r = Run-Case 'orch-crash' 'a=ok,b=ok,c=ok' @{ KE_FIXTURE_TEST_CRASH_AFTER_READY = '1' }
Check 'G2 fixture crash: residual ended by the job (confirmed), the bundle fails (rc 3), nothing left' ($r.rc -eq 3 -and
  $r.slot -match 'job_run: child_exit=9 residual=[1-9]\d* forced=0 ended_confirmed=1' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left) $([regex]::Match($r.slot, 'job_run:[^\r\n]*').Value)"

"--- G2: two harnesses ignore their stop: each ended by its handle"
$r = Run-Case 'orch-nostop2' 'a=nostop,b=nostop,c=ok' @{}
Check 'G2 two nostop: both killed by their handles, c stopped, rc 3, nothing left' ($r.rc -eq 3 -and $r.result -match 'a killed by its handle' -and
  $r.result -match 'b killed by its handle' -and $r.result -match '  c exit 0' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

"--- G2: stopping a throws (one cleanup error): b, c and node still ended"
$r = Run-Case 'orch-stopthrow' 'a=ok,b=ok,c=ok' @{ KE_FIXTURE_TEST_STOP_THROW_FOR = 'a' }
Check 'G2 one cleanup error: a killed after its error, b c node ended, rc 3, nothing left' ($r.rc -eq 3 -and $r.result -match 'a stop error' -and
  $r.result -match '  b exit 0' -and $r.result -match '  c exit 0' -and $r.result -match '  node ended' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left)"

'''
s = s.replace(a, new + a)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
