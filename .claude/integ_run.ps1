$ErrorActionPreference = 'Continue'
$env:REMOTE60_ALLOW_HOST_E2E = '1'
Set-Location D:\remote\remote\.claude\worktrees\main-stability-integration
$dir = "build-integ\apps\native_poc\Release"
$out = ".claude\integ"
New-Item -ItemType Directory -Force $out | Out-Null
$tests = @(
  "remote60_clip_image_build_gate_test", "remote60_shipped_helper_gate_test", "remote60_payload_name_test",
  "remote60_file_copy_receiver_test", "remote60_file_copy_net_e2e_test", "remote60_file_copy_helper_e2e_test",
  "remote60_clip_image_e2e_test", "remote60_viewer_clip_bar_e2e_test",
  "remote60_login_flow_test", "remote60_client_shell_bridge_test", "remote60_clip_transfer_bar_test",
  "remote60_host_migration_e2e_test", "remote60_client_auto_login_ui_test", "remote60_client_recovery_ui_test"
)
$summary = @()
foreach ($t in $tests) {
  $exe = Join-Path $dir ($t + ".exe")
  $log = Join-Path $out ("integ_" + $t + ".log")
  if (-not (Test-Path $exe)) { $summary += ("{0} MISSING (not built)" -f $t); continue }
  & $exe *> $log
  $code = $LASTEXITCODE
  $last = (Get-Content $log | Where-Object { $_ -match "^RESULT" } | Select-Object -Last 1)
  $fails = (Get-Content $log | Where-Object { $_ -match "^FAIL" }).Count
  $summary += ("{0} exit={1} fail_lines={2} | {3}" -f $t, $code, $fails, $last)
}
$summary += ("session: " + ((qwinsta 2>$null | Select-String 'Active') -join ';'))
$summary += "ALLDONE"
$summary | Out-File -Encoding utf8 (Join-Path $out "integ_summary.txt")
Get-Content (Join-Path $out "integ_summary.txt")
