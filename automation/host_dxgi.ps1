param(
  [string]$ConfigPath = "",
  [string]$ExeDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$runner = Join-Path $scriptDir "host.ps1"
if (-not (Test-Path -LiteralPath $runner)) {
  throw "host wrapper not found: $runner"
}

$env:REMOTE60_DESKTOP_CAPTURE_BACKEND = "dxgi"
Write-Output "REMOTE60_DESKTOP_CAPTURE_BACKEND=dxgi"

& $runner -ConfigPath $ConfigPath -ExeDir $ExeDir
exit $LASTEXITCODE
