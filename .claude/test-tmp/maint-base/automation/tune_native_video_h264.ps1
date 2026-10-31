param(
  [string]$Root = "",
  [string]$RemoteHost = "127.0.0.1",
  [int]$PortBase = 42200,
  [int]$ControlPortBase = 42300,
  [int[]]$FpsList = @(45, 60),
  [int[]]$BitrateList = @(800000, 1100000, 1500000),
  [int]$Repeats = 2,
  [int]$HostSeconds = 10,
  [int]$ClientSeconds = 6,
  [int]$Keyint = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$verify = Join-Path $PSScriptRoot "verify_native_video_runtime.ps1"
if (-not (Test-Path $verify)) { throw "verify script not found: $verify" }

$results = New-Object System.Collections.Generic.List[object]
$caseIdx = 0
foreach ($fps in $FpsList) {
  foreach ($bitrate in $BitrateList) {
    for ($r = 1; $r -le $Repeats; $r++) {
      $caseIdx++
      $port = $PortBase + $caseIdx
      $ctlPort = $ControlPortBase + $caseIdx
      Write-Output ("CASE fps={0} bitrate={1} repeat={2}/{3}" -f $fps, $bitrate, $r, $Repeats)
      $out = powershell -ExecutionPolicy Bypass -File $verify `
        -Root $Root `
        -RemoteHost $RemoteHost `
        -Port $port `
        -ControlPort $ctlPort `
        -Fps $fps `
        -Codec h264 `
        -Bitrate $bitrate `
        -Keyint $Keyint `
        -FpsHint $fps `
        -HostSeconds $HostSeconds `
        -ClientSeconds $ClientSeconds
      $out | ForEach-Object { $_ }

      $m = @{}
      foreach ($line in $out) {
        if ($line -match '^([A-Z0-9_]+)=(.*)$') { $m[$Matches[1]] = $Matches[2] }
      }
      $results.Add([pscustomobject]@{
        fps = $fps
        bitrate = $bitrate
        repeat = $r
        overall_ok = $m["OVERALL_OK"]
        lat_avg_us = [double]$m["LAT_AVG_US"]
        lat_p95_us = [double]$m["LAT_P95_US"]
        lat_max_us = [double]$m["LAT_MAX_US"]
        mbps_avg = [double]$m["MBPS_AVG"]
        mbps_p95 = [double]$m["MBPS_P95"]
        ctrl_rtt_avg_us = [double]$m["CTRL_RTT_AVG_US"]
        log_dir = $m["LOG_DIR"]
      })
      Start-Sleep -Milliseconds 300
    }
  }
}

Write-Output "=== H264 TUNING SUMMARY ==="
$results |
  Sort-Object @{Expression = "overall_ok"; Descending = $true}, @{Expression = "mbps_avg"; Ascending = $true}, @{Expression = "lat_p95_us"; Ascending = $true} |
  Format-Table -AutoSize |
  Out-String -Width 260 |
  Write-Output

