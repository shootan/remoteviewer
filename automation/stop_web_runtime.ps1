param(
  [string]$Root = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$sessionPath = Join-Path $Root "automation/state/web-live-session.json"

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

function Stop-IfAlive {
  param([int]$ProcessId)
  if ($ProcessId -le 0) { return $false }
  if (-not (Is-Alive -ProcessId $ProcessId)) { return $false }
  try {
    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 120
    return (-not (Is-Alive -ProcessId $ProcessId))
  } catch {
  }
  return $false
}

$stoppedSig = $false
$stoppedHost = $false

if (Test-Path $sessionPath) {
  try {
    $session = Get-Content $sessionPath -Raw | ConvertFrom-Json
    if ($session.signalingPid) { $stoppedSig = Stop-IfAlive -ProcessId ([int]$session.signalingPid) }
    if ($session.hostPid) { $stoppedHost = Stop-IfAlive -ProcessId ([int]$session.hostPid) }
  } catch {
  }
}

# Extra guard for stale host process.
Get-Process remote60_host -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Output "STOPPED_SIGNALING=$stoppedSig"
Write-Output "STOPPED_HOST=$stoppedHost"
Write-Output "DONE=1"
