param(
  [int]$Last = 220
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$stateFile = Join-Path $root "automation/state/web-live-session.json"

Write-Output "=== TRACE_DISCONNECT_CRASH ==="
Write-Output "ROOT=$root"

if (-not (Test-Path $stateFile)) {
  Write-Output "NO_SESSION_FILE=$stateFile"
  exit 1
}

$session = Get-Content $stateFile | ConvertFrom-Json
$logDir = $session.logDir
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$diagLog = Join-Path $root "logs/host_runtime_diag.log"
$fatalLog = Join-Path $root "logs/host_fatal.log"

Write-Output "LOG_DIR=$logDir"

try {
  $hostAlive = $false
  if ($session.hostPid) {
    $p = Get-Process -Id ([int]$session.hostPid) -ErrorAction SilentlyContinue
    $hostAlive = [bool]$p
  }
  Write-Output "HOST_PID=$($session.hostPid) HOST_ALIVE=$hostAlive"
} catch {
  Write-Output "HOST_STATUS_ERROR=$($_.Exception.Message)"
}

if (Test-Path $hostOut) {
  $sessionId = ""
  try {
    $lastSessionLine = Get-Content $hostOut | Select-String -Pattern "session=" | Select-Object -Last 1
    if ($lastSessionLine) {
      $m = [regex]::Match($lastSessionLine.Line, "session=([^\s]+)")
      if ($m.Success) { $sessionId = $m.Groups[1].Value }
    }
  } catch {}
  if ($sessionId) { Write-Output "SESSION_ID=$sessionId" }

  Write-Output "--- HOST.OUT MATCHES ---"
  rg -n "session_end|control_queue|peer_state_cb|peer_state_terminal|peer_close|disconnect_cleanup|fatal|exception|abort|assert" $hostOut
  Write-Output "--- HOST.OUT TAIL ---"
  Get-Content $hostOut | Select-Object -Last $Last
} else {
  Write-Output "NO_HOST_OUT=$hostOut"
}

if (Test-Path $hostErr) {
  Write-Output "--- HOST.ERR TAIL ---"
  Get-Content $hostErr | Select-Object -Last $Last
}

if (Test-Path $diagLog) {
  Write-Output "--- HOST_RUNTIME_DIAG MATCHES (tail) ---"
  if ($sessionId) {
    rg -n "session=$sessionId .*?(session_end|control_queue|peer_state_cb|peer_state_terminal|peer_close|disconnect_cleanup|fatal|exception|abort|assert)" $diagLog | Select-Object -Last 220
  } else {
    rg -n "session_end|control_queue|peer_state_cb|peer_state_terminal|peer_close|disconnect_cleanup|fatal|exception|abort|assert" $diagLog | Select-Object -Last 220
  }
}

if (Test-Path $fatalLog) {
  Write-Output "--- HOST_FATAL LOG ---"
  Get-Content $fatalLog | Select-Object -Last 80
}

Write-Output "=== TRACE_DONE ==="
