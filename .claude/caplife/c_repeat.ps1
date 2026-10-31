# apk-inbox C r1: repeated runs (a flaky boundary is not closed by one pass) and the two negative reruns that the first
# negative pass could not judge (m_anyidr's pure test was built against src's header; m_skip_held's S3 had no way to
# tell the held check from the send check).
param([string]$Wt = 'C:\work\gnlink-wt\apk-inbox-c', [int]$N = 3)
function RunOnce([string]$exe, [string]$o, [int]$limit = 300) {
  $p = Start-Process -FilePath $exe -NoNewWindow -PassThru -RedirectStandardOutput $o -RedirectStandardError "$o.err"
  $h = $p.Handle
  if (-not $p.WaitForExit($limit * 1000)) { $p.Kill(); return 'TIMEOUT' }
  $p.WaitForExit()
  $fails = @(Select-String -Path $o -Pattern '^FAIL' | ForEach-Object { ($_.Line -replace '^FAIL\s+', '').Substring(0, [Math]::Min(60, ($_.Line -replace '^FAIL\s+', '').Length)) })
  $last = (Get-Content $o | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1)
  "exit=$($p.ExitCode) FAIL=$($fails.Count) $last" + $(if ($fails.Count) { ' | ' + ($fails -join ' ; ') } else { '' })
}
& cmake --build "$Wt\build" --config Release --target remote60_android_decoder_inbox_session_test -- /m /v:q 2>&1 | Select-String 'error C'
$rep = "$Wt\build\repeat"
New-Item -ItemType Directory -Force $rep | Out-Null
foreach ($t in 'remote60_android_decoder_inbox_session_test', 'remote60_android_decoder_inbox_c_test', 'remote60_android_decoder_inbox_test',
               'remote60_android_decoder_liveness_test') {
  for ($i = 1; $i -le $N; ++$i) { "{0,-46} run {1}: {2}" -f $t, $i, (RunOnce "$Wt\build\apps\native_poc\Release\$t.exe" "$rep\$t.$i.txt") }
}
foreach ($m in @(@('m_skip_held', 'remote60_android_decoder_inbox_session_test'), @('m_anyidr', 'remote60_decoder_chain_inbox_test'))) {
  $b = "$Wt\build-neg\$($m[0])"
  & cmake --build $b --config Release --target $m[1] -- /m /v:q 2>&1 | Select-String 'error C'
  "{0,-20} {1,-44} rerun: {2}" -f $m[0], $m[1], (RunOnce "$b\apps\native_poc\Release\$($m[1]).exe" "$b\$($m[1]).rerun.txt")
}
