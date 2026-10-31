$ErrorActionPreference = 'Continue'
$rel = 'C:\work\gnlink-wt\rel-0163\build-test\apps\native_poc\Release'
$payload = 'C:\work\gnlink-wt\rel-0163\.claude\rel\0.2.163\payload'
$out = 'D:\remote\remote\.claude\caplife\rel_smoke'
New-Item -ItemType Directory -Force $out | Out-Null
"qwinsta:"; qwinsta | Select-Object -First 4
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $b = [Text.Encoding]::UTF8.GetBytes("rel-0163 smoke (worker1) $(Get-Date -Format o)"); $fs.Write($b, 0, $b.Length); $fs.Close() } catch { "slot busy: $_"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
Remove-Item Env:REMOTE60_E2E_OLD_BIN -ErrorAction SilentlyContinue
$runs = @(
  @('lk_lock', 'remote60_viewer_prelogin_login_e2e_test', @('--lock-screen')),
  @('lk_prelogin', 'remote60_viewer_prelogin_login_e2e_test', @()),
  @('ps_still', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen')),
  @('ua', 'remote60_host_capture_lifecycle_ua_e2e_test', @()),
  @('ua_fullframe', 'remote60_host_capture_lifecycle_ua_e2e_test', @('--fullframe-fails')),
  @('real_recv', 'remote60_viewer_udp_recovery_test', @('--real-recv-smoke', "$payload\GNLinkStream.exe")),
  @('punch_reply', 'remote60_host_punch_reply_e2e_test', @()),
  @('hello_backlog', 'remote60_hello_backlog_test', @()),
  @('punch_any', 'remote60_punch_any_test', @())
)
try {
  foreach ($r in $runs) {
    $name = $r[0]; $exe = "$rel\$($r[1]).exe"; $a = $r[2]
    if ($a.Count -gt 0) { & $exe @a > "$out\$name.txt" 2>&1 } else { & $exe > "$out\$name.txt" 2>&1 }
    $rc = $LASTEXITCODE
    "$name rc=$rc | $((Get-Content "$out\$name.txt" | Where-Object { $_ -match 'RESULT|PASS|FAIL' } | Select-Object -Last 1))"
  }
  # dxgi_wedge_reconnect_runner once (deferred from DW to R1)
  & node C:\work\gnlink-wt\rel-0163\apps\directory\test\dxgi_wedge_reconnect_runner.js $rel desktop > "$out\dxgi_wedge.txt" 2>&1
  "dxgi_wedge rc=$LASTEXITCODE | $((Get-Content "$out\dxgi_wedge.txt" | Where-Object { $_.Trim() } | Select-Object -Last 1))"
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover test processes: $((Get-Process | Where-Object { $_.ProcessName -match 'remote60_|GNLinkStreamMigrationTest' } | Measure-Object).Count)"
}
