param([string[]]$Runs = @('managed:r2-1', 'shell:r2-1'))
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
while ($true) {
  if (-not (Test-Path $slot)) {
    try {
      $fs = [IO.File]::Open($slot, 'CreateNew', 'Write')
      $bb = [Text.Encoding]::UTF8.GetBytes('remote_작업용2 ka-pc-impl r2 e2e ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
      $fs.Write($bb, 0, $bb.Length); $fs.Close()
      break
    } catch { }
  }
  Start-Sleep -Seconds 15
}
"slot taken " + (Get-Date -Format 'HH:mm:ss')
try {
  $env:REMOTE60_KA_REPO = 'C:\work\gnlink-wt\ka-pc'
  # older host binaries are never started on this PC (the shared test-host guard): no REMOTE60_KA_OLD_STREAM
  Remove-Item Env:REMOTE60_KA_OLD_STREAM -ErrorAction SilentlyContinue
  $bin = 'C:\work\gnlink-wt\ka-pc-build\apps\native_poc\Release'
  foreach ($r in $Runs) {
    $kind, $tag = $r.Split(':')
    $env:REMOTE60_TEST_TMP = "D:\remote\remote\.claude\test-tmp\ka-$kind-$tag"
    $log = "D:\remote\remote\.claude\np_impl_logs\ka_${kind}_$tag.log"
    $exe = if ($kind -eq 'shell') { "$bin\remote60_ka_pc_shell_e2e_test.exe" } else { "$bin\remote60_ka_pc_managed_e2e_test.exe" }
    & $exe *> $log
    $rc = $LASTEXITCODE
    $l = Get-Content $log
    "$r rc=$rc PASS=" + ($l | Select-String '^PASS').Count + " FAIL=" + ($l | Select-String '^FAIL').Count
  }
} finally {
  Remove-Item -LiteralPath $slot -Force
  "slot released " + (Get-Date -Format 'HH:mm:ss')
}
