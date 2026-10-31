$wt = 'C:\work\gnlink-wt\fix-capture-lifecycle'
$rel = "$wt\build\apps\native_poc\Release"
$out = 'D:\remote\remote\.claude\caplife\rebase'
New-Item -ItemType Directory -Force $out | Out-Null
cmake -S $wt -B "$wt\build" 2>&1 | Select-Object -Last 1
cmake --build "$wt\build" --config Release -- /m /v:q 2>&1 | Select-String -Pattern "error C|error LNK|: error" | Select-Object -First 20
"full build exit=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { exit 1 }
"head: $(git -C $wt rev-parse --short HEAD) (+ uncommitted capture-lifecycle)"
foreach ($t in 'remote60_host_kick_test','remote60_host_unlock_relay_ondemand_test','remote60_viewer_lock_cred_retry_test','remote60_secure_unlock_lockcred_test') {
  & "$rel\$t.exe" > "$out\$t.txt" 2>&1; "$t rc=$LASTEXITCODE | $((Get-Content "$out\$t.txt" | Where-Object { $_.Trim() } | Select-Object -Last 1))"
}
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $b = [Text.Encoding]::UTF8.GetBytes("capture-lifecycle rebase e2e (작업용1) $(Get-Date -Format o)"); $fs.Write($b, 0, $b.Length); $fs.Close() } catch { "slot busy: $_"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
Remove-Item Env:REMOTE60_E2E_OLD_BIN -ErrorAction SilentlyContinue
foreach ($r in @(@('lk_lock','remote60_viewer_prelogin_login_e2e_test',@('--lock-screen')), @('ps','remote60_viewer_prelogin_login_e2e_test',@('--still-screen')), @('ua','remote60_host_capture_lifecycle_ua_e2e_test',@()))) {
  $exe = "$rel\$($r[1]).exe"; $a = $r[2]
  if ($a.Count -gt 0) { & $exe @a > "$out\e2e_$($r[0]).txt" 2>&1 } else { & $exe > "$out\e2e_$($r[0]).txt" 2>&1 }
  "$($r[0]) rc=$LASTEXITCODE | $((Get-Content "$out\e2e_$($r[0]).txt" | Where-Object { $_ -match '^RESULT' } | Select-Object -Last 1))"
}
Remove-Item $slot -Confirm:$false
"slot removed=$(-not (Test-Path $slot))"
"REBASE DONE"
