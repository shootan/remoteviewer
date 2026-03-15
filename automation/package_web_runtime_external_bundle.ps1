param(
  [string]$Root = "",
  [string]$BuildDir = "build-vcpkg-local",
  [string]$OutputDir = "",
  [string]$BundleName = "web-runtime-external",
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

$hostBinSrc = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
  Join-Path $BuildDir "apps/host/Debug"
} else {
  Join-Path $Root (Join-Path $BuildDir "apps/host/Debug")
}
$signalingSrc = Join-Path $Root "apps/signaling"

$hostExe = Join-Path $hostBinSrc "remote60_host.exe"
if (-not (Test-Path -LiteralPath $hostExe)) { throw "host exe not found: $hostExe" }
if (-not (Test-Path -LiteralPath (Join-Path $signalingSrc "server.js"))) {
  throw "signaling server not found under: $signalingSrc"
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$bundleDir = Join-Path $OutputDir ("{0}-{1}" -f $BundleName, $ts)
$bundleZip = $bundleDir + ".zip"
$hostBinDst = Join-Path $bundleDir "build-vcpkg-local/apps/host/Debug"
$signalingDst = Join-Path $bundleDir "apps/signaling"
$automationDst = Join-Path $bundleDir "automation"
$docsDst = Join-Path $bundleDir "docs"

New-Item -ItemType Directory -Path $hostBinDst -Force | Out-Null
New-Item -ItemType Directory -Path $signalingDst -Force | Out-Null
New-Item -ItemType Directory -Path $automationDst -Force | Out-Null
New-Item -ItemType Directory -Path $docsDst -Force | Out-Null

$copyExt = @(".exe", ".dll")
if ($IncludeSymbols) { $copyExt += ".pdb" }
Get-ChildItem -Path $hostBinSrc -File | Where-Object { $copyExt -contains $_.Extension.ToLowerInvariant() } | ForEach-Object {
  Copy-Item -Path $_.FullName -Destination (Join-Path $hostBinDst $_.Name) -Force
}

Copy-Item -Path (Join-Path $signalingSrc "*") -Destination $signalingDst -Recurse -Force

$scriptsToCopy = @(
  "run_web_runtime.ps1",
  "start_web_runtime.ps1",
  "start_web_runtime_impl.ps1",
  "status_web_runtime.ps1",
  "stop_web_runtime.ps1"
)
foreach ($name in $scriptsToCopy) {
  $src = Join-Path $PSScriptRoot $name
  if (Test-Path -LiteralPath $src) {
    Copy-Item -Path $src -Destination (Join-Path $automationDst $name) -Force
  }
}

$guidePath = Join-Path $docsDst "WEB_RUNTIME_QUICKSTART.md"
$guide = @'
# Web Runtime Quickstart

## 1) Bundle Layout
- `build-vcpkg-local/apps/host/Debug/`
  - `remote60_host.exe`
  - required dll files
- `apps/signaling/`
  - `server.js`
  - `public/`
  - `node_modules/`
- `automation/`
  - `run_web_runtime.ps1`
  - `start_web_runtime.ps1`
  - `status_web_runtime.ps1`
  - `stop_web_runtime.ps1`

## 2) Prerequisite
- Node.js must be installed on the host machine.

## 3) Host Run
Foreground run:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\run_web_runtime.ps1 -Port 3000
```

Background run:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\start_web_runtime.ps1 -Port 3000
powershell -ExecutionPolicy Bypass -File .\automation\status_web_runtime.ps1
```

Stop:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\stop_web_runtime.ps1
```

## 4) Client Access
- Open `http://<HOST_IP_OR_DNS>:3000`

This is the path that contains:
- desktop view
- window list / selected window UI
- touch handlers
- mouse / keyboard forwarding UI

## 5) Port
- TCP `3000` for HTTP / WS signaling

## 6) Notes
- This bundle is for the browser GUI path, not the native client path.
- For native low-latency PoC binaries, use the native bundle instead.
'@
Set-Content -Path $guidePath -Value $guide -Encoding UTF8

if (Test-Path -LiteralPath $bundleZip) {
  Remove-Item -LiteralPath $bundleZip -Force
}
Compress-Archive -Path (Join-Path $bundleDir "*") -DestinationPath $bundleZip -CompressionLevel Optimal

Write-Output "BUNDLE_DIR=$bundleDir"
Write-Output "BUNDLE_ZIP=$bundleZip"
Write-Output "HOST_BIN_DIR=$hostBinDst"
Write-Output "SIGNALING_DIR=$signalingDst"
Write-Output "GUIDE=$guidePath"
