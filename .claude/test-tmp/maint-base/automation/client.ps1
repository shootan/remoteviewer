param(
  [string]$RemoteHost = "",
  [string]$ConfigPath = "",
  [string]$ExeDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-StringValue {
  param($Obj, [string]$Name, [string]$Default)
  if ($null -eq $Obj) { return $Default }
  if ($Obj.PSObject.Properties.Name -contains $Name) {
    $v = [string]$Obj.$Name
    if (-not [string]::IsNullOrWhiteSpace($v)) { return $v }
  }
  return $Default
}

$scriptDir = Split-Path -Parent $PSCommandPath
$runner = Join-Path $scriptDir "run_native_video_with_config.ps1"
if (-not (Test-Path -LiteralPath $runner)) {
  throw "run script not found: $runner"
}

$effectiveConfig = $ConfigPath
if ([string]::IsNullOrWhiteSpace($effectiveConfig)) {
  $effectiveConfig = Join-Path $scriptDir "native_video_profile_1080p_external_template.json"
}

$cfg = Get-Content -LiteralPath $effectiveConfig -Raw | ConvertFrom-Json
$configRemoteHost = Get-StringValue $cfg "remoteHost" ""
$effectiveRemoteHost = $RemoteHost
if ([string]::IsNullOrWhiteSpace($effectiveRemoteHost)) {
  $effectiveRemoteHost = $configRemoteHost
}
if ([string]::IsNullOrWhiteSpace($effectiveRemoteHost) -or $effectiveRemoteHost -eq "YOUR_PUBLIC_IP_OR_DNS") {
  throw "remote host is required. Pass -RemoteHost <HOST_PUBLIC_IP_OR_DNS>, or set remoteHost in the config."
}

$invokeArgs = @{
  Role = "client"
  ConfigPath = $effectiveConfig
  RemoteHost = $effectiveRemoteHost
}
if (-not [string]::IsNullOrWhiteSpace($ExeDir)) {
  $invokeArgs.ExeDir = $ExeDir
}

& $runner @invokeArgs
exit $LASTEXITCODE
