param(
  [string]$Root = "",
  [int]$Port = 3014,
  [switch]$Internal
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $Internal) {
  throw "Do not run start_web_runtime_impl.ps1 directly. Use automation/start_web_runtime.ps1."
}

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$hostExe = Join-Path $Root "build-vcpkg-local/apps/host/Debug/remote60_host.exe"
if (-not (Test-Path $hostExe)) {
  throw "host exe not found: $hostExe"
}

$stateDir = Join-Path $Root "automation/state"
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
$sessionPath = Join-Path $stateDir "web-live-session.json"

function Stop-IfAlive {
  param([int]$TargetProcessId)
  if ($TargetProcessId -le 0) { return }
  try {
    Stop-Process -Id $TargetProcessId -Force -ErrorAction SilentlyContinue
  } catch {
  }
}

if (Test-Path $sessionPath) {
  try {
    $prev = Get-Content $sessionPath -Raw | ConvertFrom-Json
    if ($prev.signalingPid) { Stop-IfAlive -TargetProcessId ([int]$prev.signalingPid) }
    if ($prev.hostPid) { Stop-IfAlive -TargetProcessId ([int]$prev.hostPid) }
  } catch {
  }
}

Get-Process remote60_host -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/web-live-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$sigOut = Join-Path $logDir "sig.out.log"
$sigErr = Join-Path $logDir "sig.err.log"
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"

$sigProc = $null
$hostProc = $null

try {
  $env:PORT = "$Port"
  $sigProc = Start-Process -FilePath "node" -ArgumentList @("apps/signaling/server.js") `
    -WorkingDirectory $Root -RedirectStandardOutput $sigOut -RedirectStandardError $sigErr -PassThru

  Start-Sleep -Milliseconds 300
  $sigProc.Refresh()
  if ($sigProc.HasExited) {
    throw "signaling exited early (code=$($sigProc.ExitCode))"
  }

  $env:REMOTE60_SIGNALING_URL = ("ws://127.0.0.1:{0}" -f $Port)
  $env:REMOTE60_WINDOW_MODE = "1"
  $env:REMOTE60_PRESERVE_LOCAL_CURSOR = "1"
  $env:REMOTE60_SAFE_CLICK_MODE = "1"
  $env:REMOTE60_MESSAGE_CLICK_MODE = "1"
  $env:REMOTE60_HIDE_LOCAL_CURSOR_FOR_INPUT = "1"
  if (-not $env:REMOTE60_VIDEO_FPS) { $env:REMOTE60_VIDEO_FPS = "30" }
  if (-not $env:REMOTE60_VIDEO_BITRATE_BPS) { $env:REMOTE60_VIDEO_BITRATE_BPS = "4000000" }
  if (-not $env:REMOTE60_VIDEO_PACKET_PACING_US) { $env:REMOTE60_VIDEO_PACKET_PACING_US = "0" }
  if (-not $env:REMOTE60_VERBOSE_MEDIA_LOGS) { $env:REMOTE60_VERBOSE_MEDIA_LOGS = "0" }
  $hostProc = Start-Process -FilePath $hostExe -ArgumentList @("--runtime-server") `
    -WorkingDirectory $Root -RedirectStandardOutput $hostOut -RedirectStandardError $hostErr -PassThru

  Start-Sleep -Milliseconds 400
  $hostProc.Refresh()
  if ($hostProc.HasExited) {
    throw "host exited early (code=$($hostProc.ExitCode))"
  }

  $session = [ordered]@{
    startedAt = (Get-Date -Format o)
    port = $Port
    webUrl = ("http://127.0.0.1:{0}" -f $Port)
    signalingPid = $sigProc.Id
    hostPid = $hostProc.Id
    logDir = $logDir
    signalingOut = $sigOut
    hostOut = $hostOut
  }
  ($session | ConvertTo-Json -Depth 4) | Set-Content -Path $sessionPath -Encoding ascii
}
catch {
  if ($hostProc) { Stop-IfAlive -TargetProcessId $hostProc.Id }
  if ($sigProc) { Stop-IfAlive -TargetProcessId $sigProc.Id }
  throw
}
