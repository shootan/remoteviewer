$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
while ($true) {
  if (-not (Test-Path $slot)) {
    try {
      $fs = [IO.File]::Open($slot, 'CreateNew', 'Write')
      $bb = [Text.Encoding]::UTF8.GetBytes('remote_작업용2 ka-pc-impl e2e ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
      $fs.Write($bb, 0, $bb.Length); $fs.Close()
      break
    } catch { }
  }
  Start-Sleep -Seconds 15
}
"slot taken " + (Get-Date -Format 'HH:mm:ss')
try {
  $env:REMOTE60_KA_REPO = 'C:\work\gnlink-wt\ka-pc'
  $env:REMOTE60_KA_OLD_STREAM = 'C:\work\gnlink-wt\rel-0163\.claude\rel-final\0.2.163\payload\GNLinkStream.exe'
  foreach ($i in 11, 12) {
    $env:REMOTE60_TEST_TMP = "D:\remote\remote\.claude\test-tmp\ka-e2e-$i"
    $log = "D:\remote\remote\.claude\np_impl_logs\ka_e2e_$i.log"
    & C:\work\gnlink-wt\ka-pc-build\apps\native_poc\Release\remote60_ka_pc_managed_e2e_test.exe *> $log
    $rc = $LASTEXITCODE
    $l = Get-Content $log
    "run$i rc=$rc PASS=" + ($l | Select-String '^PASS').Count + " FAIL=" + ($l | Select-String '^FAIL').Count
  }
} finally {
  Remove-Item -LiteralPath $slot -Force
  "slot released " + (Get-Date -Format 'HH:mm:ss')
}
