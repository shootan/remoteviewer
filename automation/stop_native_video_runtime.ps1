param(
  [string]$Root = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$sessionPath = Join-Path $Root "automation/state/native-video-session.json"

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
    return $false
  }
}

$stoppedHost = $false
$stoppedClient = $false

if (Test-Path $sessionPath) {
  try {
    $session = Get-Content $sessionPath -Raw | ConvertFrom-Json
    if ($session.hostPid) { $stoppedHost = Stop-IfAlive -ProcessId ([int]$session.hostPid) }
    if ($session.clientPid) { $stoppedClient = Stop-IfAlive -ProcessId ([int]$session.clientPid) }
  } catch {}
}

Get-Process remote60_native_video_host_poc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process remote60_native_video_client_poc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Output "STOPPED_HOST=$stoppedHost"
Write-Output "STOPPED_CLIENT=$stoppedClient"
Write-Output "DONE=1"
