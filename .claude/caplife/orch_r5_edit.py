p = 'C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/ke_orchestration_test.ps1'
s = open(p, encoding='utf-8', newline='').read()
a = '"--- the AVD fails, every host fine"'
assert s.count(a) == 1
new = r'''"--- AVD r5: the AVD fails with a Python traceback on stderr (as ke-r22)"
$r = Run-Case 'orch-stderr' 'a=ok,b=ok,c=ok' @{ KE_STUB_AVD_STDERR = '1' }
$stderrText = if ($r.root -and (Test-Path "$($r.root)\avd_stderr.txt")) { Get-Content "$($r.root)\avd_stderr.txt" -Raw } else { '' }
Check 'r5 traceback kept whole in avd_stderr.txt, the AVD exit code kept (1, not 99), no "AVD step error", rc 1, nothing left' (
  $r.avd -and $r.rc -eq 1 -and $stderrText -match 'Traceback \(most recent call last\)' -and
  $stderrText -match 'RuntimeError: stub: uiautomator dump failed' -and $r.slot -match 'avd exit=1 ' -and
  $r.slot -notmatch 'AVD step error' -and $r.left -eq 0) "rc=$($r.rc) left=$($r.left) stderr=$($stderrText.Length) bytes"

'''
s = s.replace(a, new + a)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
