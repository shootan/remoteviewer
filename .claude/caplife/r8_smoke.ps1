$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\rel-0163'
$rel = "$wt\build-test\apps\native_poc\Release"
$payload = "$wt\.claude\rel-final\0.2.163\payload"
$out = 'D:\remote\remote\.claude\caplife\r8final'
New-Item -ItemType Directory -Force $out | Out-Null
"qwinsta:"; qwinsta | Select-Object -First 4
foreach ($t in 'remote60_viewer_selection_gate_test','remote60_viewer_window_proc_isolated_test') {
  & "$rel\$t.exe" > "$out\$t.txt" 2>&1
  "$t rc=$LASTEXITCODE | $((Get-Content "$out\$t.txt" | Where-Object { $_.Trim() } | Select-Object -Last 1))"
}
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
$runs = @(
  @('ps_e1', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen', '--render', '--shots', "$out\ps_e1")),
  @('ua_e1', 'remote60_host_capture_lifecycle_ua_e2e_test', @('--render', '--shots', "$out\ua_e1")),
  @('lk_lock', 'remote60_viewer_prelogin_login_e2e_test', @('--lock-screen')),
  @('lk_prelogin', 'remote60_viewer_prelogin_login_e2e_test', @()),
  @('ua', 'remote60_host_capture_lifecycle_ua_e2e_test', @()),
  @('ua_fullframe', 'remote60_host_capture_lifecycle_ua_e2e_test', @('--fullframe-fails')),
  @('real_recv', 'remote60_viewer_udp_recovery_test', @('--real-recv-smoke', "$payload\GNLinkStream.exe"))
)
try {
  foreach ($r in $runs) {
    $name = $r[0]; $exe = "$rel\$($r[1]).exe"; $a = $r[2]
    foreach ($x in $a) { if ($x -like "$out\*") { New-Item -ItemType Directory -Force $x | Out-Null } }
    if ($a.Count -gt 0) { & $exe @a > "$out\$name.txt" 2>&1 } else { & $exe > "$out\$name.txt" 2>&1 }
    "$name rc=$LASTEXITCODE | $((Get-Content "$out\$name.txt" | Where-Object { $_ -match 'RESULT|real-recv-smoke' } | Select-Object -Last 1))"
  }
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover: $((Get-Process | Where-Object { $_.ProcessName -match 'remote60_|GNLinkStreamMigrationTest' } | Measure-Object).Count)"
}
