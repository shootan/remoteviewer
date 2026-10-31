$ErrorActionPreference = 'Continue'
$env:REMOTE60_ALLOW_HOST_E2E = '1'
Set-Location D:\remote\remote\.claude\worktrees\main-stability-integration
$out = ".claude\integ147"
New-Item -ItemType Directory -Force $out | Out-Null
"HEAD=$(git rev-parse --short HEAD)"
cmake -S . -B build-integ -G "Visual Studio 17 2022" -A x64 *> (Join-Path $out "configure.log"); "configure exit=$LASTEXITCODE"
cmake --build build-integ --config Release -- /m /v:m *> (Join-Path $out "build_all.log"); "build exit=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { Select-String -Path (Join-Path $out "build_all.log") -Pattern "error" | Select-Object -First 8; exit 1 }
$dir = "build-integ\apps\native_poc\Release"
$tests = @("remote60_update_effects_test", "remote60_updater_assembly_test", "remote60_payload_name_test", "remote60_shipped_helper_gate_test", "remote60_clip_image_build_gate_test", "remote60_host_migration_e2e_test", "remote60_login_flow_test")
$summary = @()
foreach ($t in $tests) {
  $exe = Join-Path $dir ($t + ".exe"); $log = Join-Path $out ($t + ".log")
  if (-not (Test-Path $exe)) { $summary += ("{0} MISSING" -f $t); continue }
  & $exe *> $log
  $code = $LASTEXITCODE
  $last = (Get-Content $log | Where-Object { $_ -match "RESULT|ALL PASS|FAILED" } | Select-Object -Last 1)
  $summary += ("{0} exit={1} | {2}" -f $t, $code, $last)
}
$summary += "ALLDONE"
$summary | Out-File -Encoding utf8 (Join-Path $out "summary.txt")
Get-Content (Join-Path $out "summary.txt")
