$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\fix-capture-lifecycle'
$rel = "$wt\build\apps\native_poc\Release"
$out = 'D:\remote\remote\.claude\caplife\final'
New-Item -ItemType Directory -Force $out | Out-Null
cmake --build "$wt\build" --config Release -- /m /v:q 2>&1 | Select-String -Pattern "error C|error LNK|: error" | Select-Object -First 20
"full build exit=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { exit 1 }
"head: $(git -C $wt rev-parse --short HEAD) (+ uncommitted capture-lifecycle)"
foreach ($t in 'remote60_host_kick_test','remote60_host_backend_policy_test','remote60_host_capture_quarantine_test','remote60_capture_attachment_test',
               'remote60_dxgi_output_selection_test','remote60_host_monitor_selection_test','remote60_capture_cadence_gate_test',
               'remote60_secure_unlock_lockcred_test','remote60_host_unlock_relay_ondemand_test','remote60_unlock_protocol_test') {
  if (-not (Test-Path "$rel\$t.exe")) { "$t MISSING"; continue }
  & "$rel\$t.exe" > "$out\$t.txt" 2>&1; $rc = $LASTEXITCODE
  "$t rc=$rc | $((Get-Content "$out\$t.txt" | Where-Object { $_.Trim() } | Select-Object -Last 1))"
}
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $b = [Text.Encoding]::UTF8.GetBytes("capture-lifecycle final e2e (작업용1) $(Get-Date -Format o)"); $fs.Write($b, 0, $b.Length); $fs.Close() } catch { "slot busy: $_"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
Remove-Item Env:REMOTE60_E2E_OLD_BIN -ErrorAction SilentlyContinue
$runs = @(
  @('ps1', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen')),
  @('ps2', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen')),
  @('ua1', 'remote60_host_capture_lifecycle_ua_e2e_test', @()),
  @('ua2', 'remote60_host_capture_lifecycle_ua_e2e_test', @()),
  @('lk_lock', 'remote60_viewer_prelogin_login_e2e_test', @('--lock-screen')),
  @('lk_prelogin', 'remote60_viewer_prelogin_login_e2e_test', @()),
  @('lk_notprelogin', 'remote60_viewer_prelogin_login_e2e_test', @('--not-prelogin')),
  @('stats_gap', 'remote60_viewer_stats_chain_e2e_test', @('--scenario', 'gap')),
  @('monitor_fallback', 'remote60_host_monitor_fallback_e2e_test', @())
)
foreach ($r in $runs) {
  $name = $r[0]; $exe = "$rel\$($r[1]).exe"; $a = $r[2]
  if (-not (Test-Path $exe)) { "$name MISSING"; continue }
  if ($a.Count -gt 0) { & $exe @a > "$out\e2e_$name.txt" 2>&1 } else { & $exe > "$out\e2e_$name.txt" 2>&1 }
  $rc = $LASTEXITCODE
  "$name rc=$rc | $((Get-Content "$out\e2e_$name.txt" | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1))"
}
Remove-Item $slot -Confirm:$false
"slot removed=$(-not (Test-Path $slot))"
"FINAL DONE"
