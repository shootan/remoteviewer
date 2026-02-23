param(
  [string]$Root = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$sessionPath = Join-Path $Root "automation/state/native-video-session.json"
if (-not (Test-Path $sessionPath)) {
  Write-Output "SESSION_FOUND=0"
  exit 0
}

$session = Get-Content $sessionPath -Raw | ConvertFrom-Json

function Is-Alive {
  param([int]$ProcessId)
  if ($ProcessId -le 0) { return $false }
  try {
    $p = Get-Process -Id $ProcessId -ErrorAction Stop
    return ($null -ne $p -and -not $p.HasExited)
  } catch {
    return $false
  }
}

$hostPid = [int]$session.hostPid
$clientPid = [int]$session.clientPid
Write-Output "SESSION_FOUND=1"
Write-Output "REMOTE_HOST=$($session.remoteHost)"
Write-Output "PORT=$($session.port)"
Write-Output "CONTROL_PORT=$($session.controlPort)"
if ($session.PSObject.Properties.Name -contains "transport") { Write-Output "TRANSPORT=$($session.transport)" }
if ($session.PSObject.Properties.Name -contains "udpMtu") { Write-Output "UDP_MTU=$($session.udpMtu)" }
if ($session.PSObject.Properties.Name -contains "codec") { Write-Output "CODEC=$($session.codec)" }
if ($session.PSObject.Properties.Name -contains "bitrate") { Write-Output "BITRATE=$($session.bitrate)" }
if ($session.PSObject.Properties.Name -contains "keyint") { Write-Output "KEYINT=$($session.keyint)" }
if ($session.PSObject.Properties.Name -contains "encodeWidth") { Write-Output "ENCODE_WIDTH=$($session.encodeWidth)" }
if ($session.PSObject.Properties.Name -contains "encodeHeight") { Write-Output "ENCODE_HEIGHT=$($session.encodeHeight)" }
if ($session.PSObject.Properties.Name -contains "encoderBackend") { Write-Output "ENCODER_BACKEND=$($session.encoderBackend)" }
if ($session.PSObject.Properties.Name -contains "decoderBackend") { Write-Output "DECODER_BACKEND=$($session.decoderBackend)" }
Write-Output "HOST_PID=$hostPid"
Write-Output "CLIENT_PID=$clientPid"
Write-Output "HOST_ALIVE=$(Is-Alive -ProcessId $hostPid)"
Write-Output "CLIENT_ALIVE=$(Is-Alive -ProcessId $clientPid)"
Write-Output "LOG_DIR=$($session.logDir)"
