$ErrorActionPreference = 'Continue'
$env:REMOTE60_ALLOW_HOST_E2E = '1'
Set-Location D:\remote\remote\.claude\worktrees\verify-c2-ef533f2
"HEAD=$(git rev-parse --short HEAD)"
$dir = "build-verify\apps\native_poc\Release"
$out = ".claude\rv_hst3"
$tests = @("remote60_file_copy_receiver_test", "remote60_clip_transfer_bar_test", "remote60_viewer_clip_bar_e2e_test", "remote60_clip_image_e2e_test", "remote60_update_effects_test", "remote60_updater_scenarios_test")
$summary = @()
foreach ($t in $tests) {
  $exe = Join-Path $dir ($t + ".exe"); $log = Join-Path $out ("rest_" + $t + ".log")
  if (-not (Test-Path $exe)) { $summary += ("{0} MISSING" -f $t); continue }
  & $exe *> $log
  $code = $LASTEXITCODE
  $last = (Get-Content $log | Where-Object { $_ -match "^RESULT|ALL PASS|FAILED" } | Select-Object -Last 1)
  $summary += ("{0} exit={1} | {2}" -f $t, $code, $last)
}
$summary += "ALLDONE"
$summary | Out-File -Encoding utf8 (Join-Path $out "rest_summary.txt")
Get-Content (Join-Path $out "rest_summary.txt")
