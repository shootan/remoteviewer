# apk-inbox C r1: the final pass at the fixed HEAD -- the whole tree built, then every test the change touches once
# (and the long idr-recovery run through the product session, which carries the owed retries C coalesces with).
param([string]$Wt = 'C:\work\gnlink-wt\apk-inbox-c')
"HEAD " + (git -C $Wt rev-parse HEAD) + " tracked changes: " + @(git -C $Wt status --short --untracked-files=no).Count
& cmake --build "$Wt\build" --config Release -- /m /v:m 2>&1 | Out-File -Encoding utf8 "$Wt\build\build_final.txt"
"build errors=" + @(Select-String -Path "$Wt\build\build_final.txt" -Pattern 'error C|error LNK|fatal error').Count
$list = @('remote60_decoder_chain_inbox_test', 'remote60_decoder_chain_inbox_r2_contrast_test', 'remote60_decoder_key_request_merge_test',
  'remote60_android_decoder_inbox_test', 'remote60_android_decoder_inbox_c_test',
  'remote60_android_decoder_inbox_session_test', 'remote60_android_decoder_owed_key_test', 'remote60_android_decoder_geometry_test',
  'remote60_android_decoder_binding_test', 'remote60_android_decoder_decode_stats_test', 'remote60_android_decoder_liveness_test',
  'remote60_android_decoder_session_liveness_test', 'remote60_decoder_pending_slot_test', 'remote60_client_session_prelogin_select_test',
  'remote60_android_decoder_idr_recovery_test')
$out = "$Wt\build\final"
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($t in $list) {
  $o = "$out\$t.txt"
  $p = Start-Process -FilePath "$Wt\build\apps\native_poc\Release\$t.exe" -NoNewWindow -PassThru -RedirectStandardOutput $o -RedirectStandardError "$o.err"
  $h = $p.Handle
  if (-not $p.WaitForExit(900000)) { $p.Kill(); "{0,-48} TIMEOUT" -f $t; continue }
  $p.WaitForExit()
  $pass = @(Select-String -Path $o -Pattern '^PASS').Count
  $fail = @(Select-String -Path $o -Pattern '^FAIL').Count
  $last = (Get-Content $o | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1)
  "{0,-48} exit={1} PASS={2} FAIL={3} | {4}" -f $t, $p.ExitCode, $pass, $fail, $last
}
