param(
  [string]$Root = "",
  [string]$BuildDir = "build-vcpkg-local",
  [string]$OutputDir = "",
  [string]$BundleName = "native-video-external",
  [switch]$IncludeSymbols
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $Root "dist"
}

$binSrc = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
  Join-Path $BuildDir "apps/native_poc/Debug"
} else {
  Join-Path $Root (Join-Path $BuildDir "apps/native_poc/Debug")
}

$hostExe = Join-Path $binSrc "remote60_native_video_host_poc.exe"
$clientExe = Join-Path $binSrc "remote60_native_video_client_poc.exe"
if (-not (Test-Path $hostExe)) { throw "host exe not found: $hostExe" }
if (-not (Test-Path $clientExe)) { throw "client exe not found: $clientExe" }

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$bundleDir = Join-Path $OutputDir ("{0}-{1}" -f $BundleName, $ts)
$bundleZip = $bundleDir + ".zip"
$binDst = Join-Path $bundleDir "bin"
$automationDst = Join-Path $bundleDir "automation"
$docsDst = Join-Path $bundleDir "docs"

New-Item -ItemType Directory -Path $binDst -Force | Out-Null
New-Item -ItemType Directory -Path $automationDst -Force | Out-Null
New-Item -ItemType Directory -Path $docsDst -Force | Out-Null

$copyExt = @(".exe", ".dll")
if ($IncludeSymbols) { $copyExt += ".pdb" }
Get-ChildItem -Path $binSrc -File | Where-Object { $copyExt -contains $_.Extension.ToLowerInvariant() } | ForEach-Object {
  Copy-Item -Path $_.FullName -Destination (Join-Path $binDst $_.Name) -Force
}

$scriptsToCopy = @(
  "run_native_video_with_config.ps1",
  "wan_preflight_native_video.ps1",
  "start_native_video_runtime.ps1",
  "stop_native_video_runtime.ps1",
  "status_native_video_runtime.ps1"
)
foreach ($name in $scriptsToCopy) {
  $src = Join-Path $PSScriptRoot $name
  if (Test-Path $src) {
    Copy-Item -Path $src -Destination (Join-Path $automationDst $name) -Force
  }
}

Get-ChildItem -Path $PSScriptRoot -Filter "native_video_profile_*.json" -File | ForEach-Object {
  Copy-Item -Path $_.FullName -Destination (Join-Path $automationDst $_.Name) -Force
}

$guidePath = Join-Path $docsDst "EXTERNAL_WAN_QUICKSTART.md"
$guide = @'
# External WAN Quickstart

## 1) Port Forwarding (router -> host PC)
- UDP `43000` -> host LAN IP (video channel, current profiles use UDP)
- TCP `43001` -> host LAN IP (control channel)
- If you switch to TCP media transport, also forward TCP `43000`.

## 2) Windows Firewall (host PC)
- Allow inbound UDP `43000`
- Allow inbound TCP `43001`

Example (run as admin):
```powershell
netsh advfirewall firewall add rule name="Remote60 Native Video UDP43000" dir=in action=allow protocol=UDP localport=43000
netsh advfirewall firewall add rule name="Remote60 Native Video TCP43001" dir=in action=allow protocol=TCP localport=43001
```

## 3) Host Start
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\run_native_video_with_config.ps1 -Role host -ConfigPath .\automation\native_video_profile_1080p_lowlat.json -ExeDir .\bin
```

## 4) Client Start
- Option A: pass address in command
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\run_native_video_with_config.ps1 -Role client -ConfigPath .\automation\native_video_profile_1080p_lowlat.json -ExeDir .\bin -RemoteHost <HOST_PUBLIC_IP_OR_DNS>
```
- Option B: edit `remoteHost` in `native_video_profile_1080p_external_template.json` and run without `-RemoteHost`.

## 5) Recommended FHD Profiles
- low latency: `native_video_profile_1080p_lowlat.json` (8Mbps, keyint 60)
- quality up: `native_video_profile_1080p_quality_10m_k60.json`
- quality max: `native_video_profile_1080p_quality_12m_k60.json`

## 6) Preflight Check
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\wan_preflight_native_video.ps1 -RemoteHost <HOST_PUBLIC_IP_OR_DNS> -VideoPort 43000 -ControlPort 43001
```
'@
Set-Content -Path $guidePath -Value $guide -Encoding UTF8

if (Test-Path $bundleZip) {
  Remove-Item $bundleZip -Force
}
Compress-Archive -Path (Join-Path $bundleDir "*") -DestinationPath $bundleZip -CompressionLevel Optimal

Write-Output "BUNDLE_DIR=$bundleDir"
Write-Output "BUNDLE_ZIP=$bundleZip"
Write-Output "BIN_DIR=$binDst"
Write-Output "AUTOMATION_DIR=$automationDst"
Write-Output "GUIDE=$guidePath"
