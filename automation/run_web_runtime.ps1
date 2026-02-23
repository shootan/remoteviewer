param(
  [string]$Root = "",
  [int]$Port = 3000,
  [int]$HostReadyTimeoutSec = 25,
  [switch]$OpenBrowser,
  [string]$WebUrl = "",
  [int]$AutoStopSec = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Stop-ChildProcess {
  param([object]$Proc)
  if ($null -eq $Proc) { return }
  try { $Proc.Refresh() } catch {}
  if (-not $Proc.HasExited) {
    Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
  }
}

function Wait-HttpOk {
  param(
    [string]$Url,
    [int]$TimeoutSec
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    try {
      $r = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 2
      if ($r.StatusCode -ge 200 -and $r.StatusCode -lt 300) {
        return $true
      }
    } catch {
    }
    Start-Sleep -Milliseconds 300
  }
  return $false
}

function Wait-ForPattern {
  param(
    [string]$Path,
    [string]$Pattern,
    [int]$TimeoutSec
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ((Test-Path $Path) -and (Select-String -Path $Path -Pattern $Pattern -Quiet)) {
      return $true
    }
    Start-Sleep -Milliseconds 300
  }
  return $false
}

function To-WebUrl {
  param([string]$Raw)
  $t = $Raw.Trim()
  if ([string]::IsNullOrWhiteSpace($t)) { return "" }
  if ($t.StartsWith("ws://")) { return "http://" + $t.Substring(5) }
  if ($t.StartsWith("wss://")) { return "https://" + $t.Substring(6) }
  if ($t.StartsWith("http://") -or $t.StartsWith("https://")) { return $t }
  return "http://" + $t
}

$hostExe = Join-Path $Root "build-vcpkg-local/apps/host/Debug/remote60_host.exe"
if (-not (Test-Path $hostExe)) {
  throw "host exe not found: $hostExe"
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/web-live-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$sigOut = Join-Path $logDir "sig.out.log"
$sigErr = Join-Path $logDir "sig.err.log"
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$sessionPath = Join-Path $logDir "session.txt"

$sigProc = $null
$hostProc = $null
$effectiveWebUrl = ""

try {
  $sigProc = Start-Process -FilePath "node" -ArgumentList @("apps/signaling/server.js") `
    -WorkingDirectory $Root -RedirectStandardOutput $sigOut -RedirectStandardError $sigErr -PassThru

  if (-not (Wait-HttpOk -Url ("http://127.0.0.1:{0}/healthz" -f $Port) -TimeoutSec 10)) {
    throw "signaling health check failed on port $Port"
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

  $hostReady = Wait-ForPattern -Path $sigOut -Pattern "registered .*role: 'host'" -TimeoutSec $HostReadyTimeoutSec
  if (-not $hostReady) {
    throw "host did not register to signaling within timeout"
  }

  $effectiveWebUrl = $WebUrl
  if ([string]::IsNullOrWhiteSpace($effectiveWebUrl)) {
    $envUrl = [Environment]::GetEnvironmentVariable("REMOTE60_SIGNALING_URL")
    if (-not [string]::IsNullOrWhiteSpace($envUrl)) {
      $effectiveWebUrl = To-WebUrl $envUrl
    } else {
      $effectiveWebUrl = "http://127.0.0.1:$Port"
    }
  }

  @(
    "log_dir=$logDir",
    "signaling_pid=$($sigProc.Id)",
    "host_pid=$($hostProc.Id)",
    "web_url=$effectiveWebUrl",
    "started_at=$(Get-Date -Format o)"
  ) | Set-Content -Path $sessionPath -Encoding ascii

  Write-Output "LOG_DIR=$logDir"
  Write-Output "SIGNALING_PID=$($sigProc.Id)"
  Write-Output "HOST_PID=$($hostProc.Id)"
  Write-Output "WEB_URL=$effectiveWebUrl"
  Write-Output "READY=1"

  if ($OpenBrowser) {
    Start-Process $effectiveWebUrl | Out-Null
  }

  Write-Output "RUNNING=1 (Press Ctrl+C to stop)"
  $startedAt = Get-Date
  while ($true) {
    Start-Sleep -Milliseconds 400
    if ($AutoStopSec -gt 0) {
      $elapsed = (Get-Date) - $startedAt
      if ($elapsed.TotalSeconds -ge $AutoStopSec) {
        break
      }
    }
    $sigProc.Refresh()
    $hostProc.Refresh()
    if ($sigProc.HasExited -or $hostProc.HasExited) {
      break
    }
  }

  if ($sigProc.HasExited) {
    throw "signaling exited unexpectedly (code=$($sigProc.ExitCode))"
  }
  if ($hostProc.HasExited) {
    throw "host exited unexpectedly (code=$($hostProc.ExitCode))"
  }
}
finally {
  Stop-ChildProcess -Proc $hostProc
  Stop-ChildProcess -Proc $sigProc
  Write-Output "STOPPED=1"
}
