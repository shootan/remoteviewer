param(
  [string]$RemoteHost = "",
  [string]$ConfigPath = "",
  [string]$ExeDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath
$wrapper = Join-Path $scriptDir "client.ps1"
if (-not (Test-Path -LiteralPath $wrapper)) {
  throw "client wrapper not found: $wrapper"
}

& $wrapper -RemoteHost $RemoteHost -ConfigPath $ConfigPath -ExeDir $ExeDir
exit $LASTEXITCODE
