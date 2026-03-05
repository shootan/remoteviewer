param(
  [string]$Root = "",
  [string]$ConfigPath = "automation/native_video_profile_1080p_wan_quality.json",
  [string]$ExeDir = "build-vcpkg-local/apps/native_poc/Debug",
  [string]$RemoteHost = "",
  [string]$Tag = "wan",
  [string]$LogRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Resolve-FromRoot {
  param([string]$Base, [string]$Value)
  if ([System.IO.Path]::IsPathRooted($Value)) {
    return (Resolve-Path -LiteralPath $Value).Path
  }
  return (Resolve-Path -LiteralPath (Join-Path $Base $Value)).Path
}

$resolvedConfig = Resolve-FromRoot -Base $Root -Value $ConfigPath
$resolvedExeDir = Resolve-FromRoot -Base $Root -Value $ExeDir

if ([string]::IsNullOrWhiteSpace($LogRoot)) {
  $LogRoot = Join-Path $Root "automation/logs"
}
if (-not [System.IO.Path]::IsPathRooted($LogRoot)) {
  $LogRoot = Join-Path $Root $LogRoot
}
New-Item -ItemType Directory -Path $LogRoot -Force | Out-Null

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$captureDir = Join-Path $LogRoot ("wan-capture-" + $ts + "-client-" + $Tag)
New-Item -ItemType Directory -Path $captureDir -Force | Out-Null

$clientLog = Join-Path $captureDir "client.out.log"
$clientErr = Join-Path $captureDir "client.err.log"
$clientCfg = Join-Path $captureDir "client.config.json"
$metaPath = Join-Path $captureDir "meta.txt"

Copy-Item -LiteralPath $resolvedConfig -Destination $clientCfg -Force

$runScript = Join-Path $Root "automation/run_native_video_with_config.ps1"
if (-not (Test-Path -LiteralPath $runScript)) {
  throw "run script not found: $runScript"
}

$args = @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", $runScript,
  "-Role", "client",
  "-ConfigPath", $resolvedConfig,
  "-ExeDir", $resolvedExeDir
)
if (-not [string]::IsNullOrWhiteSpace($RemoteHost)) {
  $args += @("-RemoteHost", $RemoteHost)
}

@(
  "ROLE=client",
  "TAG=$Tag",
  "CAPTURE_DIR=$captureDir",
  "CONFIG=$resolvedConfig",
  "EXE_DIR=$resolvedExeDir",
  "REMOTE_HOST=$RemoteHost",
  "CLIENT_LOG=$clientLog",
  "CLIENT_ERR=$clientErr",
  "STARTED_AT=$([DateTime]::UtcNow.ToString('o'))"
) | Set-Content -LiteralPath $metaPath -Encoding UTF8

Write-Output "CAPTURE_DIR=$captureDir"
Write-Output "CLIENT_LOG=$clientLog"
Write-Output "CLIENT_ERR=$clientErr"
if (-not [string]::IsNullOrWhiteSpace($RemoteHost)) {
  Write-Output "REMOTE_HOST=$RemoteHost"
} else {
  Write-Output "REMOTE_HOST=config.remoteHost"
}

$clientProc = Start-Process -FilePath "powershell" -ArgumentList $args `
  -WorkingDirectory $Root `
  -RedirectStandardOutput $clientLog `
  -RedirectStandardError $clientErr `
  -PassThru

Write-Output "CLIENT_PID=$($clientProc.Id)"
Write-Output "STATUS=running"

try {
  Wait-Process -Id $clientProc.Id
} finally {
  try { $clientProc.Refresh() } catch {}
}

$rc = if ($clientProc.HasExited) { [int]$clientProc.ExitCode } else { -999 }
Add-Content -LiteralPath $metaPath -Value ("EXIT_CODE=" + $rc)
Add-Content -LiteralPath $metaPath -Value ("ENDED_AT=" + [DateTime]::UtcNow.ToString('o'))

Write-Output "EXIT_CODE=$rc"
exit $rc
