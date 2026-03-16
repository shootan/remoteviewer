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
  "m9_easy.ps1",
  "run_native_video_with_config.ps1",
  "run_wan_host_capture.ps1",
  "run_wan_client_capture.ps1",
  "summarize_wan_capture.ps1",
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

## 1) Bundle Layout
- `bin/`
  - `remote60_native_video_host_poc.exe`
  - `remote60_native_video_client_poc.exe`
- `automation/`
  - `run_native_video_with_config.ps1`
  - `m9_easy.ps1`
  - `run_wan_host_capture.ps1`
  - `run_wan_client_capture.ps1`
  - `summarize_wan_capture.ps1`
- `docs/EXTERNAL_WAN_QUICKSTART.md`

## 2) Port Forwarding (router -> host PC)
- UDP `43000` -> host LAN IP (video channel, current profiles use UDP)
- TCP `43001` -> host LAN IP (control channel)
- If you switch to TCP media transport, also forward TCP `43000`.

## 3) Windows Firewall (host PC)
- Allow inbound UDP `43000`
- Allow inbound TCP `43001`

Example (run as admin):
```powershell
netsh advfirewall firewall add rule name="Remote60 Native Video UDP43000" dir=in action=allow protocol=UDP localport=43000
netsh advfirewall firewall add rule name="Remote60 Native Video TCP43001" dir=in action=allow protocol=TCP localport=43001
```

## 4) Recommended Profiles
- `native_video_profile_1080p_external_template.json`
  - default native 2PC GUI smoke, 1080p30 fixed, `8Mbps`, `mft_auto/mft_auto`, input/control on
- `native_video_profile_1080p_window_input_template.json`
  - config-locked window-target input template for fixed app HWND testing
- `native_video_profile_1080p_lowlat.json`
  - adaptive low-latency tuning, static scene can downshift to `10fps`
- `native_video_profile_1080p_wan_quality.json`
  - 1080p30, `10Mbps`, `keyint=60`, frame gating off
- `native_video_profile_720p.json`
  - 720p30, `5Mbps`, `keyint=60`, `h264NoPacing=1`

## 5) Quick 2PC Run
Host machine:
```powershell
Copy-Item .\automation\native_video_profile_1080p_external_template.json .\automation\run_native_video_with_config.json
# edit .\automation\run_native_video_with_config.json and add: "role": "host"
powershell -ExecutionPolicy Bypass -File .\automation\run_native_video_with_config.ps1
```

Client machine:
```powershell
Copy-Item .\automation\native_video_profile_1080p_external_template.json .\automation\run_native_video_with_config.json
# edit .\automation\run_native_video_with_config.json and add:
#   "role": "client"
#   "remoteHost": "<HOST_PUBLIC_IP_OR_DNS>"
powershell -ExecutionPolicy Bypass -File .\automation\run_native_video_with_config.ps1
```

`run_native_video_with_config.ps1` now defaults to `automation\run_native_video_with_config.json` and passes only `--config` through to the exe.
If you want the old explicit style, `-Role`, `-ConfigPath`, `-ExeDir`, `-RemoteHost` still work as overrides.
Use `native_video_profile_1080p_lowlat.json` only when you intentionally want adaptive/frame-gating behavior.

After connect, the native client starts in a home picker overlay:
- `Desktop Mode` first
- `Window list` below
- `Refresh` button in the picker

After you select a target:
- video becomes fullscreen
- use the small `Targets` button at the top-left to open the picker again

In this profile:
- video click/drag/wheel/keyboard input is on
- `Desktop Mode` routes input by screen point
- selecting a window routes input to that window

## 6) Quick M9 A/B Capture
One-time prepare:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 prepare
```

Host:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 host off
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 host on
```

Client:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 client off <HOST_PUBLIC_IP_OR_DNS>
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 client on <HOST_PUBLIC_IP_OR_DNS>
```

Summary:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 summary off
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 summary on
```

Note:
- `m9_easy.ps1` is for M9 A/B capture and uses `native_video_profile_1080p_lowlat.json`.
- That baseline keeps frame gating enabled, so static scenes can intentionally downshift to `10fps`.
- For generic fixed-30fps external smoke, use the Quick 2PC Run commands above instead.

## 7) Native Window-Target Input
Use `native_video_profile_1080p_window_input_template.json` only when you want click/drag/keyboard input into a specific HWND-backed app window.

Edit these keys first:
- `inputTargetProcess` or `inputTargetTitle`
- `captureWindowProcess` or `captureWindowTitle`
- `remoteHost`

Run:
```powershell
Copy-Item .\automation\native_video_profile_1080p_window_input_template.json .\automation\run_native_video_with_config.json
# host: add "role": "host"
# client: add "role": "client", "remoteHost": "<HOST_PUBLIC_IP_OR_DNS>"
powershell -ExecutionPolicy Bypass -File .\automation\run_native_video_with_config.ps1
```

Notes:
- `native_video_profile_1080p_external_template.json` is the general GUI profile.
- `native_video_profile_1080p_window_input_template.json` is for fixed-target automation or app-specific validation.
- `run_native_video_with_config.ps1` uses config-first defaults, so input/capture JSON keys are preserved without repeating CLI args.

## 8) Manual WAN Capture Commands
Host:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\run_wan_host_capture.ps1 -ConfigPath .\automation\native_video_profile_1080p_external_template.json -ExeDir .\bin -Tag manual
```

Client:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\run_wan_client_capture.ps1 -ConfigPath .\automation\native_video_profile_1080p_external_template.json -ExeDir .\bin -RemoteHost <HOST_PUBLIC_IP_OR_DNS> -Tag manual
```

Summary:
```powershell
powershell -ExecutionPolicy Bypass -File .\automation\summarize_wan_capture.ps1 -HostInput .\automation\logs\wan-capture-<timestamp>-host-manual -ClientInput .\automation\logs\wan-capture-<timestamp>-client-manual
```

## 9) Browser GUI Path
This bundle is the native path only.
If you need the browser GUI with `desktop / window list / touch input`, run the web runtime from the source tree instead:
```powershell
powershell -ExecutionPolicy Bypass -File automation\run_web_runtime.ps1 -Port 3000
```

Then open:
- `http://<HOST_PUBLIC_IP_OR_DNS>:3000`

## 10) Preflight Check
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
