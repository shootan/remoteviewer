param(
  [string]$ConfigPath = "",
  [string]$ExeDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$runner = Join-Path $scriptDir "run_native_video_with_config.ps1"
if (-not (Test-Path -LiteralPath $runner)) {
  throw "run script not found: $runner"
}

$effectiveConfig = $ConfigPath
if ([string]::IsNullOrWhiteSpace($effectiveConfig)) {
  $effectiveConfig = Join-Path $scriptDir "native_video_profile_1080p_external_template.json"
}

$invokeArgs = @{
  Role = "host"
  ConfigPath = $effectiveConfig
}
if (-not [string]::IsNullOrWhiteSpace($ExeDir)) {
  $invokeArgs.ExeDir = $ExeDir
}

& $runner @invokeArgs
exit $LASTEXITCODE
