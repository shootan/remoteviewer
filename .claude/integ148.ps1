$ErrorActionPreference = 'Continue'
$env:REMOTE60_ALLOW_HOST_E2E = '1'
Set-Location D:\remote\remote\.claude\worktrees\main-stability-integration
$out = ".claude\integ148"
New-Item -ItemType Directory -Force $out | Out-Null
"HEAD=$(git rev-parse --short HEAD)"
cmake -S . -B build-integ -G "Visual Studio 17 2022" -A x64 *> (Join-Path $out "configure.log"); "configure exit=$LASTEXITCODE"
cmake --build build-integ --config Release -- /m /v:m *> (Join-Path $out "build_all.log"); "build exit=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { Select-String -Path (Join-Path $out "build_all.log") -Pattern " error " | Select-Object -First 8; exit 1 }
$dir = "build-integ\apps\native_poc\Release"
$tests = @(
  "remote60_host_wire_limiter_test", "remote60_host_wire_cap_defer_test", "remote60_host_wire_cap_idr_test",
  "remote60_host_wire_cap_fix_test", "remote60_host_wire_cap_sender_thread_test", "remote60_host_rate_governor_test",
  "remote60_udp_fec_single_chunk_loss_test", "remote60_fec_single_chunk_host_e2e_test", "remote60_viewer_udp_recovery_test",
  "remote60_control_resume_e2e_test", "remote60_file_copy_net_e2e_test", "remote60_file_copy_helper_e2e_test",
  "remote60_clip_image_build_gate_test", "remote60_shipped_helper_gate_test", "remote60_payload_name_test",
  "remote60_update_effects_test", "remote60_updater_assembly_test", "remote60_host_migration_e2e_test", "remote60_login_flow_test"
)
$summary = @()
foreach ($t in $tests) {
  $exe = Join-Path $dir ($t + ".exe"); $log = Join-Path $out ($t + ".log")
  if (-not (Test-Path $exe)) { $summary += ("{0} MISSING" -f $t); continue }
  & $exe *> $log
  $code = $LASTEXITCODE
  $last = (Get-Content $log | Where-Object { $_ -match "RESULT|ALL PASS|FAILED|PASS \(" } | Select-Object -Last 1)
  $fails = (Get-Content $log | Where-Object { $_ -match "^FAIL" }).Count
  $summary += ("{0} exit={1} fail_lines={2} | {3}" -f $t, $code, $fails, $last)
}
$host_ = (Resolve-Path (Join-Path $dir "GNLinkStream.exe")).Path
& (Join-Path $dir "remote60_viewer_udp_recovery_test.exe") --real-recv-smoke $host_ *> (Join-Path $out "realrecv.log")
$summary += ("real-recv-smoke exit={0}" -f $LASTEXITCODE)
& (Join-Path $dir "remote60_fec_single_chunk_host_e2e_test.exe") --matrix *> (Join-Path $out "matrix.log")
$summary += ("matrix exit={0} | {1}" -f $LASTEXITCODE, (Get-Content (Join-Path $out "matrix.log") | Where-Object { $_ -match "PASS \(|FAIL" } | Select-Object -Last 1))
$summary += ("session: " + ((qwinsta 2>$null | Select-String 'Active') -join ';'))
$summary += "ALLDONE"
$summary | Out-File -Encoding utf8 (Join-Path $out "summary.txt")
Get-Content (Join-Path $out "summary.txt")
