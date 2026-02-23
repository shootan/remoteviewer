param(
  [string]$Root = "",
  [int]$Port = 3014
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$impl = Join-Path $PSScriptRoot "start_web_runtime_impl.ps1"
if (-not (Test-Path $impl)) {
  throw "impl script not found: $impl"
}

$stateDir = Join-Path $Root "automation/state"
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
$sessionPath = Join-Path $stateDir "web-live-session.json"

$launcher = Start-Process -FilePath "powershell" -ArgumentList @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", $impl,
  "-Root", $Root,
  "-Port", "$Port",
  "-Internal"
) -WindowStyle Hidden -PassThru

# Return immediately; actual startup continues in detached child process.
Write-Output "START_LAUNCHER_PID=$($launcher.Id)"
Write-Output "PORT=$Port"
Write-Output "SESSION_FILE=$sessionPath"
Write-Output "STATUS=starting_in_background"
