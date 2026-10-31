# old-APK lane r4 negative: the NEW orchestration test with 494d05b's oa_fixture.ps1 swapped in; the r4 fixture is
# backed up first and put back in a finally (hash checked).
$wt = 'C:\work\gnlink-wt\old-apk-lane'
$f = "$wt\automation\old_apk_lane\oa_fixture.ps1"
$bak = "$wt\.claude\oa_fixture.r4.bak"
Copy-Item $f $bak
$h0 = (Get-FileHash $f).Hash
try {
  $old = git -C $wt show 494d05b:automation/old_apk_lane/oa_fixture.ps1
  [IO.File]::WriteAllText($f, (($old -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
  $hasFix = Select-String -Path $f -SimpleMatch -Pattern 'hc = @(if' -Quiet
  "old fixture in place (no r4 fix): $(-not $hasFix)"
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$wt\automation\old_apk_lane\oa_orchestration_test.ps1" *> "$wt\.claude\oa_orch_neg_r4.txt"
  "neg rc=$LASTEXITCODE"
} finally {
  Copy-Item $bak $f -Force
  "restored: $((Get-FileHash $f).Hash -eq $h0)"
}
Get-Content "$wt\.claude\oa_orch_neg_r4.txt" | Where-Object { $_ -match '^FAIL|checks|RESULT' }
