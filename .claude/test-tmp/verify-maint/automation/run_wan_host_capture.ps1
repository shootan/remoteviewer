param(
  [string]$Root = "",
  [string]$ConfigPath = "automation/native_video_profile_1080p_wan_quality.json",
  [string]$ExeDir = "build-vcpkg-local/apps/native_poc/Debug",
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
$captureDir = Join-Path $LogRoot ("wan-capture-" + $ts + "-host-" + $Tag)
New-Item -ItemType Directory -Path $captureDir -Force | Out-Null

$hostLog = Join-Path $captureDir "host.out.log"
$hostErr = Join-Path $captureDir "host.err.log"
$hostCfg = Join-Path $captureDir "host.config.json"
$metaPath = Join-Path $captureDir "meta.txt"

Copy-Item -LiteralPath $resolvedConfig -Destination $hostCfg -Force

$runScript = Join-Path $Root "automation/run_native_video_with_config.ps1"
if (-not (Test-Path -LiteralPath $runScript)) {
  throw "run script not found: $runScript"
}

$args = @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", $runScript,
  "-Role", "host",
  "-ConfigPath", $resolvedConfig,
  "-ExeDir", $resolvedExeDir
)

@(
  "ROLE=host",
  "TAG=$Tag",
  "CAPTURE_DIR=$captureDir",
  "CONFIG=$resolvedConfig",
  "EXE_DIR=$resolvedExeDir",
  "HOST_LOG=$hostLog",
  "HOST_ERR=$hostErr",
  "STARTED_AT=$([DateTime]::UtcNow.ToString('o'))"
) | Set-Content -LiteralPath $metaPath -Encoding UTF8

Write-Output "CAPTURE_DIR=$captureDir"
Write-Output "HOST_LOG=$hostLog"
Write-Output "HOST_ERR=$hostErr"
Write-Output "NOTE=Stop with Ctrl+C when enough WAN samples are collected."

$hostProc = Start-Process -FilePath "powershell" -ArgumentList $args `
  -WorkingDirectory $Root `
  -RedirectStandardOutput $hostLog `
  -RedirectStandardError $hostErr `
  -PassThru

Write-Output "HOST_PID=$($hostProc.Id)"
Write-Output "STATUS=running"

try {
  Wait-Process -Id $hostProc.Id
} finally {
  try { $hostProc.Refresh() } catch {}
}

$rc = if ($hostProc.HasExited) { [int]$hostProc.ExitCode } else { -999 }
Add-Content -LiteralPath $metaPath -Value ("EXIT_CODE=" + $rc)
Add-Content -LiteralPath $metaPath -Value ("ENDED_AT=" + [DateTime]::UtcNow.ToString('o'))

Write-Output "EXIT_CODE=$rc"
exit $rc
