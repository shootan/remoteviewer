param(
  [string]$Role = "",
  [string]$ConfigPath = "",
  [string]$ExeDir = "",
  [string]$RemoteHost = ""
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

function Resolve-ConfigFile {
  param(
    [string]$ScriptDir,
    [string]$RequestedPath
  )

  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    return (Resolve-Path -LiteralPath $RequestedPath).Path
  }

  $defaults = @(
    (Join-Path $ScriptDir "run_native_video_with_config.json"),
    (Join-Path $ScriptDir "native_video_launch.json")
  )
  foreach ($candidate in $defaults) {
    if (Test-Path -LiteralPath $candidate) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }

  throw "config file not found. Pass -ConfigPath, or place run_native_video_with_config.json next to this script."
}

function Resolve-ExeDirectory {
  param(
    [string]$ScriptDir,
    [string]$ConfigDir,
    [string]$RequestedExeDir,
    $ConfigObject
  )

  if (-not [string]::IsNullOrWhiteSpace($RequestedExeDir)) {
    return (Resolve-Path -LiteralPath $RequestedExeDir).Path
  }

  $exeDirFromConfig = Get-StringValue $ConfigObject "exeDir" ""
  if (-not [string]::IsNullOrWhiteSpace($exeDirFromConfig)) {
    $candidate = if ([System.IO.Path]::IsPathRooted($exeDirFromConfig)) {
      $exeDirFromConfig
    } else {
      Join-Path $ConfigDir $exeDirFromConfig
    }
    return (Resolve-Path -LiteralPath $candidate).Path
  }

  $bundleBin = Join-Path (Split-Path -Parent $ScriptDir) "bin"
  if (Test-Path -LiteralPath $bundleBin) {
    return (Resolve-Path -LiteralPath $bundleBin).Path
  }

  $sourceBin = Join-Path (Split-Path -Parent $ScriptDir) "build-vcpkg-local/apps/native_poc/Debug"
  if (Test-Path -LiteralPath $sourceBin) {
    return (Resolve-Path -LiteralPath $sourceBin).Path
  }

  return (Resolve-Path -LiteralPath $ScriptDir).Path
}

$scriptDir = Split-Path -Parent $PSCommandPath
$resolvedConfig = Resolve-ConfigFile -ScriptDir $scriptDir -RequestedPath $ConfigPath
$configDir = Split-Path -Parent $resolvedConfig
$cfg = Get-Content -LiteralPath $resolvedConfig -Raw | ConvertFrom-Json

$effectiveRole = $Role
if ([string]::IsNullOrWhiteSpace($effectiveRole)) {
  $effectiveRole = Get-StringValue $cfg "role" ""
}
$effectiveRole = [string]$effectiveRole
if ([string]::IsNullOrWhiteSpace($effectiveRole)) {
  throw "role is required. Add `"role`": `"host`" or `"client`" to the config, or pass -Role."
}
$effectiveRole = $effectiveRole.Trim().ToLowerInvariant()
if ($effectiveRole -ne "host" -and $effectiveRole -ne "client") {
  throw "unsupported role: $effectiveRole (expected host or client)"
}

$exeDirResolved = Resolve-ExeDirectory -ScriptDir $scriptDir -ConfigDir $configDir -RequestedExeDir $ExeDir -ConfigObject $cfg

if ($effectiveRole -eq "host") {
  $exe = Join-Path $exeDirResolved "GNLinkStream.exe"
  if (-not (Test-Path -LiteralPath $exe)) { throw "host exe not found: $exe" }

  $args = @("--config", $resolvedConfig)
  Write-Output "ROLE=host"
  Write-Output "CONFIG=$resolvedConfig"
  Write-Output "EXE=$exe"
  & $exe @args
  exit $LASTEXITCODE
}

$exe = Join-Path $exeDirResolved "GNLinkViewer.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "client exe not found: $exe" }

$args = @("--config", $resolvedConfig)
if (-not [string]::IsNullOrWhiteSpace($RemoteHost)) {
  $args += @("--host", $RemoteHost)
}

Write-Output "ROLE=client"
Write-Output "CONFIG=$resolvedConfig"
Write-Output "EXE=$exe"
if (-not [string]::IsNullOrWhiteSpace($RemoteHost)) {
  Write-Output "HOST_OVERRIDE=$RemoteHost"
}
& $exe @args
exit $LASTEXITCODE
