param(
  [string]$ConfigPath = "",
  [string]$ExeDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$wrapper = Join-Path $scriptDir "host.ps1"
if (-not (Test-Path -LiteralPath $wrapper)) {
  throw "host wrapper not found: $wrapper"
}

& $wrapper -ConfigPath $ConfigPath -ExeDir $ExeDir
exit $LASTEXITCODE
