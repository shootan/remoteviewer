param(
  [string]$Root = "",
  [string]$RemoteHost = "127.0.0.1",
  [int]$VideoPort = 43000,
  [int]$ControlPort = 43001
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

Write-Output "CHECK_HOST=$RemoteHost"
Write-Output "CHECK_VIDEO_PORT=$VideoPort"
Write-Output "CHECK_CONTROL_PORT=$ControlPort"

$video = Test-NetConnection -ComputerName $RemoteHost -Port $VideoPort -WarningAction SilentlyContinue
Write-Output "VIDEO_TCP_TEST=$($video.TcpTestSucceeded)"
if ($ControlPort -gt 0) {
  $ctrl = Test-NetConnection -ComputerName $RemoteHost -Port $ControlPort -WarningAction SilentlyContinue
  Write-Output "CONTROL_TCP_TEST=$($ctrl.TcpTestSucceeded)"
}

$ips = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
  Where-Object { $_.IPAddress -notlike '169.254.*' -and $_.IPAddress -ne '127.0.0.1' } |
  Select-Object -First 8 InterfaceAlias,IPAddress

foreach ($ip in $ips) {
  Write-Output ("LOCAL_IPV4[{0}]={1}" -f $ip.InterfaceAlias, $ip.IPAddress)
}

Write-Output "NOTE=For WAN use, forward VideoPort/ControlPort and verify firewall inbound rules."
