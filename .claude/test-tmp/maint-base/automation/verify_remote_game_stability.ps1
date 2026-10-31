param([string]$BuildDir = 'build-incident', [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $sourceRoot $BuildDir))
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
$owner = ($cache | Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' }) -replace '^CMAKE_HOME_DIRECTORY:INTERNAL=', ''
if ([System.IO.Path]::GetFullPath($owner) -ne $sourceRoot) { throw 'Build directory belongs to another source tree.' }
$tests = @(
  'remote60_native_video_client_shared_core_test', 'remote60_viewer_frame_gate_test',
  'remote60_host_kick_test', 'remote60_host_abr_test', 'remote60_viewer_liveness_test',
  'remote60_capture_readback_test', 'remote60_capture_final_update_test',
  'remote60_viewer_udp_ingress_test', 'remote60_viewer_window_proc_isolated_test',
  'remote60_windows_environment_snapshot_test',
  'remote60_viewer_udp_recovery_test'
)
$results = @()
foreach ($test in $tests) {
  $exe = Join-Path $buildRoot ('apps/native_poc/' + $Configuration + '/' + $test + '.exe')
  if (-not (Test-Path -LiteralPath $exe)) { throw "Build required: $test" }
  $log = Join-Path $buildRoot ($test + '-verified.log')
  & $exe > $log 2>&1
  $code = $LASTEXITCODE
  $results += [pscustomobject]@{test=$test; exit=$code; sha256=(Get-FileHash -LiteralPath $exe).Hash; log=$log}
  $results | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $buildRoot 'stability-test-results.json')
  Write-Output "$test exit=$code"
  Get-Content -LiteralPath $log -Tail 1
  if ($code -ne 0) { throw "Failed: $test (see $log)" }
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test_summarize_wan_capture.ps1')
if ($LASTEXITCODE -ne 0) { throw 'WAN gate regression failed' }
Write-Output 'AUTOMATED_CANDIDATE_CHECKS=PASS (field performance and release approval remain separate)'
