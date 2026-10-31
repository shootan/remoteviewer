param(
  [Parameter(Position = 0)]
  [ValidateSet("prepare", "host", "client", "summary", "help")]
  [string]$Action = "help",
  [Parameter(Position = 1)]
  [ValidateSet("off", "on")]
  [string]$Mode = "off",
  [Parameter(Position = 2)]
  [string]$RemoteHost = "",
  [string]$Root = "",
  [string]$ExeDir = "",
  [string]$TagPrefix = "m9",
  [string]$HostInput = "",
  [string]$ClientInput = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FromRoot {
  param(
    [string]$Base,
    [string]$Value
  )
  if ([System.IO.Path]::IsPathRooted($Value)) {
    return (Resolve-Path -LiteralPath $Value).Path
  }
  return (Resolve-Path -LiteralPath (Join-Path $Base $Value)).Path
}

function Ensure-ApplyProfile {
  param(
    [string]$BaseRoot,
    [string]$OffProfileRel,
    [string]$OnProfileRel
  )
  $offProfileAbs = Resolve-FromRoot -Base $BaseRoot -Value $OffProfileRel
  $onProfileAbs = Join-Path $BaseRoot $OnProfileRel
  if (-not (Test-Path -LiteralPath $onProfileAbs)) {
    $obj = Get-Content -LiteralPath $offProfileAbs -Raw | ConvertFrom-Json
    $obj.m9Apply = $true
    $obj | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $onProfileAbs -Encoding UTF8
  }
  return (Resolve-Path -LiteralPath $onProfileAbs).Path
}

function Get-LatestCaptureDir {
  param(
    [string]$BaseRoot,
    [string]$Role,
    [string]$Tag
  )
  $logRoot = Join-Path $BaseRoot "automation/logs"
  if (-not (Test-Path -LiteralPath $logRoot)) {
    throw "log root not found: $logRoot"
  }
  $pattern = "wan-capture-*-{0}-{1}" -f $Role, $Tag
  $item = Get-ChildItem -LiteralPath $logRoot -Directory |
    Where-Object { $_.Name -like $pattern } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if ($null -eq $item) {
    throw "no capture directory found for role=$Role tag=$Tag under $logRoot"
  }
  return $item.FullName
}

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$offProfileRel = "automation/native_video_profile_1080p_lowlat.json"
$onProfileRel = "automation/tmp_m9_apply.json"
$runHostScript = Resolve-FromRoot -Base $Root -Value "automation/run_wan_host_capture.ps1"
$runClientScript = Resolve-FromRoot -Base $Root -Value "automation/run_wan_client_capture.ps1"
$summaryScript = Resolve-FromRoot -Base $Root -Value "automation/summarize_wan_capture.ps1"

if ([string]::IsNullOrWhiteSpace($ExeDir)) {
  if (Test-Path -LiteralPath (Join-Path $Root "bin")) {
    $ExeDir = "bin"
  } else {
    $ExeDir = "build-vcpkg-local/apps/native_poc/Debug"
  }
}

$resolvedExeDir = Resolve-FromRoot -Base $Root -Value $ExeDir
$configPath = if ($Mode -eq "on") {
  Ensure-ApplyProfile -BaseRoot $Root -OffProfileRel $offProfileRel -OnProfileRel $onProfileRel
} else {
  Resolve-FromRoot -Base $Root -Value $offProfileRel
}
$tag = "{0}{1}" -f $TagPrefix, $Mode

if ($Action -eq "help") {
  Write-Output "USAGE:"
  Write-Output "  .\\automation\\m9_easy.ps1 prepare"
  Write-Output "  .\\automation\\m9_easy.ps1 host off"
  Write-Output "  .\\automation\\m9_easy.ps1 host on"
  Write-Output "  .\\automation\\m9_easy.ps1 client off [HOST_PUBLIC_IP_OR_DNS]"
  Write-Output "  .\\automation\\m9_easy.ps1 client on [HOST_PUBLIC_IP_OR_DNS]"
  Write-Output "  .\\automation\\m9_easy.ps1 summary off"
  Write-Output "  .\\automation\\m9_easy.ps1 summary on"
  Write-Output ""
  Write-Output "DEFAULTS:"
  Write-Output "  Root=$Root"
  Write-Output "  ExeDir=$resolvedExeDir"
  Write-Output "  Mode=$Mode Tag=$tag"
  Write-Output ""
  Write-Output "NOTE:"
  Write-Output "  m9_easy uses native_video_profile_1080p_lowlat.json as the baseline."
  Write-Output "  That profile keeps frame gating enabled and static scenes can drop to 10fps."
  Write-Output "  For fixed 30fps external smoke, use run_native_video_with_config.ps1 with native_video_profile_1080p_external_template.json."
  exit 0
}

if ($Action -eq "prepare") {
  $onProfileRaw = Join-Path $Root $onProfileRel
  $alreadyExists = Test-Path -LiteralPath $onProfileRaw
  $onProfile = Ensure-ApplyProfile -BaseRoot $Root -OffProfileRel $offProfileRel -OnProfileRel $onProfileRel
  Write-Output "APPLY_PROFILE_CREATED=$([string](-not $alreadyExists))"
  Write-Output "ROOT=$Root"
  Write-Output "OFF_PROFILE=$(Resolve-FromRoot -Base $Root -Value $offProfileRel)"
  Write-Output "ON_PROFILE=$onProfile"
  Write-Output "EXE_DIR=$resolvedExeDir"
  exit 0
}

if ($Action -eq "host") {
  Write-Output "ACTION=host"
  Write-Output "MODE=$Mode"
  Write-Output "TAG=$tag"
  Write-Output "CONFIG=$configPath"
  Write-Output "EXE_DIR=$resolvedExeDir"
  & $runHostScript -Root $Root -ConfigPath $configPath -ExeDir $resolvedExeDir -Tag $tag
  exit $LASTEXITCODE
}

if ($Action -eq "client") {
  Write-Output "ACTION=client"
  Write-Output "MODE=$Mode"
  Write-Output "TAG=$tag"
  Write-Output "CONFIG=$configPath"
  Write-Output "EXE_DIR=$resolvedExeDir"
  if (-not [string]::IsNullOrWhiteSpace($RemoteHost)) {
    Write-Output "REMOTE_HOST=$RemoteHost"
    & $runClientScript -Root $Root -ConfigPath $configPath -ExeDir $resolvedExeDir -RemoteHost $RemoteHost -Tag $tag
  } else {
    Write-Output "REMOTE_HOST=config.remoteHost"
    & $runClientScript -Root $Root -ConfigPath $configPath -ExeDir $resolvedExeDir -Tag $tag
  }
  exit $LASTEXITCODE
}

if ($Action -eq "summary") {
  if ([string]::IsNullOrWhiteSpace($HostInput)) {
    $HostInput = Get-LatestCaptureDir -BaseRoot $Root -Role "host" -Tag $tag
  } elseif (-not [System.IO.Path]::IsPathRooted($HostInput)) {
    $HostInput = Resolve-FromRoot -Base $Root -Value $HostInput
  }
  if ([string]::IsNullOrWhiteSpace($ClientInput)) {
    $ClientInput = Get-LatestCaptureDir -BaseRoot $Root -Role "client" -Tag $tag
  } elseif (-not [System.IO.Path]::IsPathRooted($ClientInput)) {
    $ClientInput = Resolve-FromRoot -Base $Root -Value $ClientInput
  }

  Write-Output "ACTION=summary"
  Write-Output "MODE=$Mode"
  Write-Output "HOST_INPUT=$HostInput"
  Write-Output "CLIENT_INPUT=$ClientInput"
  & $summaryScript -HostInput $HostInput -ClientInput $ClientInput
  exit $LASTEXITCODE
}

throw "unsupported action: $Action"
