param(
  [string]$Root = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$sessionPath = Join-Path $Root "automation/state/web-live-session.json"
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

$sigPid = [int]$session.signalingPid
$hostPid = [int]$session.hostPid

Write-Output "SESSION_FOUND=1"
Write-Output "PORT=$($session.port)"
Write-Output "WEB_URL=$($session.webUrl)"
Write-Output "SIGNALING_PID=$sigPid"
Write-Output "HOST_PID=$hostPid"
Write-Output "SIGNALING_ALIVE=$(Is-Alive -ProcessId $sigPid)"
Write-Output "HOST_ALIVE=$(Is-Alive -ProcessId $hostPid)"
Write-Output "LOG_DIR=$($session.logDir)"
