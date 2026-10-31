# apk-inbox C r2: repeated runs of the tests the r2 fixes touch (a flaky boundary is not closed by one pass).
param([string]$Wt = 'C:\work\gnlink-wt\apk-inbox-c', [int]$N = 5)
function RunOnce([string]$exe, [string]$o, [int]$limit = 300) {
  $p = Start-Process -FilePath $exe -NoNewWindow -PassThru -RedirectStandardOutput $o -RedirectStandardError "$o.err"
  $h = $p.Handle
  if (-not $p.WaitForExit($limit * 1000)) { $p.Kill(); return 'TIMEOUT' }
  $p.WaitForExit()
  $pass = @(Select-String -Path $o -Pattern '^PASS').Count
  $fails = @(Select-String -Path $o -Pattern '^FAIL' | ForEach-Object { ($_.Line -replace '^FAIL\s+', '').Substring(0, [Math]::Min(60, ($_.Line -replace '^FAIL\s+', '').Length)) })
  $last = (Get-Content $o | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1)
  "exit=$($p.ExitCode) PASS=$pass FAIL=$($fails.Count) $last" + $(if ($fails.Count) { ' | ' + ($fails -join ' ; ') } else { '' })
}
"HEAD " + (git -C $Wt rev-parse HEAD) + " tracked changes: " + @(git -C $Wt status --short --untracked-files=no).Count
$rep = "$Wt\build\repeat_r2"
New-Item -ItemType Directory -Force $rep | Out-Null
foreach ($t in 'remote60_android_decoder_inbox_session_test', 'remote60_android_decoder_inbox_c_test', 'remote60_android_decoder_inbox_test',
               'remote60_decoder_chain_inbox_test', 'remote60_decoder_chain_inbox_r2_contrast_test', 'remote60_android_decoder_liveness_test') {
  for ($i = 1; $i -le $N; ++$i) { "{0,-46} run {1}: {2}" -f $t, $i, (RunOnce "$Wt\build\apps\native_poc\Release\$t.exe" "$rep\$t.$i.txt") }
}
