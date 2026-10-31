# One-click host for the Android direct client on the local network.
# Streams the desktop (or a picked window) on UDP 43000 + TCP control 43001
# using the latest Release build, with input injection enabled.
param(
  [string]$ConfigPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$runner = Join-Path $scriptDir "run_native_video_with_config.ps1"
$effectiveConfig = $ConfigPath
if ([string]::IsNullOrWhiteSpace($effectiveConfig)) {
  $effectiveConfig = Join-Path $scriptDir "native_video_profile_android_lan.json"
}

$lanIps = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
  Where-Object { $_.InterfaceAlias -notmatch 'Loopback|vEthernet|WSL' -and $_.IPAddress -notmatch '^169\.254' } |
  Select-Object -ExpandProperty IPAddress
Write-Host "[host-android] connect from the Android app to: $($lanIps -join ', ')  (video 43000 / control 43001)"

& $runner -Role host -ConfigPath $effectiveConfig
exit $LASTEXITCODE
