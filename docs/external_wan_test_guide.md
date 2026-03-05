# Native Video External WAN Test Guide

## Ports to forward
- UDP `43000` -> host PC LAN IP (video channel for current UDP profiles)
- TCP `43001` -> host PC LAN IP (control channel)
- If using TCP media transport, also forward TCP `43000`.

## Firewall on host PC
- Inbound allow UDP `43000`
- Inbound allow TCP `43001`

Example (admin PowerShell):
```powershell
netsh advfirewall firewall add rule name="Remote60 Native Video UDP43000" dir=in action=allow protocol=UDP localport=43000
netsh advfirewall firewall add rule name="Remote60 Native Video TCP43001" dir=in action=allow protocol=TCP localport=43001
```

## Address input for client
- Option A: command-line override
```powershell
-RemoteHost <HOST_PUBLIC_IP_OR_DNS>
```
- Option B: edit `remoteHost` in JSON profile
  - `automation/native_video_profile_1080p_external_template.json`

## Optional window-scoped capture (M13 phase1)
Add these keys in host JSON profile when you want to capture only a target app window:
- `captureWindowProcess`: comma-separated process names (example: `dnplayer.exe,HD-Player.exe,Nox.exe`)
- `captureWindowTitle`: substring filter for window title
- `captureWindowClientOnly`: `true` to crop to client-area
- `captureWindowRebindIntervalMs`: rebind polling interval (default/recommended `1000`)

Example:
```json
{
  "captureWindowProcess": "dnplayer.exe,HD-Player.exe,Nox.exe",
  "captureWindowTitle": "LDPlayer",
  "captureWindowClientOnly": true,
  "captureWindowRebindIntervalMs": 1000
}
```

Overlay check (client):
- Stats panel now shows `HostCapture ...` lines from control pong metadata:
  - pid/process/rebind count
  - hwnd/mode(window|monitor + client-area)/title

Local M13 rebind validation:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_m13_window_rebind.ps1 `
  -Root . `
  -ExeDir build-vcpkg-local/apps/native_poc/Debug `
  -BaseConfig automation/native_video_profile_1080p_lowlat.json `
  -HostClientSeconds 14 `
  -TargetProcess notepad.exe `
  -RemoteHost 127.0.0.1
```

Expected summary keys:
- `TARGET_FOUND_COUNT>=1`
- `REBIND_EVENT_COUNT>=1` (or `REBIND_STATS_MAX>=1`)
- `CLIENT_HOSTCAP_EVENT_COUNT>=1`
- `M13_REBIND_PASS=True`

## Recommended FHD profiles
- `automation/native_video_profile_1080p_lowlat.json` (8Mbps, low-latency baseline)
- `automation/native_video_profile_1080p_wan_quality.json` (10Mbps, keyint 60, frame gating off)
- `automation/native_video_profile_1080p_quality_10m_k60.json`
- `automation/native_video_profile_1080p_quality_12m_k60.json`

## Run examples
Host:
```powershell
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role host -ConfigPath automation/native_video_profile_1080p_lowlat.json -ExeDir build-vcpkg-local/apps/native_poc/Debug
```

Client:
```powershell
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role client -ConfigPath automation/native_video_profile_1080p_lowlat.json -ExeDir build-vcpkg-local/apps/native_poc/Debug -RemoteHost <HOST_PUBLIC_IP_OR_DNS>
```

## Quick Start (short commands)
Use one wrapper script:
- `automation/m9_easy.ps1`

One-time prepare:
```powershell
cd D:\remote\build
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 prepare
```

Host run:
```powershell
cd D:\remote\build
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 host off
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 host on
```

Client run:
```powershell
cd D:\remote\build
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 client off [HOST_PUBLIC_IP_OR_DNS]
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 client on [HOST_PUBLIC_IP_OR_DNS]
```

Summary:
```powershell
cd D:\remote\build
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 summary off
powershell -ExecutionPolicy Bypass -File .\automation\m9_easy.ps1 summary on
```

Notes:
- `summary off/on` uses the latest `m9off/m9on` host/client capture directories automatically.
- `client off/on` without host arg uses `remoteHost` from the selected JSON profile.
- Default exe path is auto-selected:
  - `bin` if present (bundle layout like `D:\remote\build`)
  - otherwise `build-vcpkg-local/apps/native_poc/Debug` (source tree layout)

## M9 apply A/B capture workflow (external 2PC)
Goal:
- Compare `m9Apply=false` baseline vs `m9Apply=true` apply mode on real WAN/LAN client sessions.
- Keep profile/settings identical except `m9Apply`.

### 1) Prepare two profile variants
Baseline:
- `automation/native_video_profile_1080p_lowlat.json` (default `m9Apply=false`)

Apply variant (create once):
```powershell
$p = Get-Content automation/native_video_profile_1080p_lowlat.json -Raw | ConvertFrom-Json
$p.m9Apply = $true
$p | ConvertTo-Json -Depth 8 | Set-Content automation/tmp_m9_apply.json -Encoding UTF8
```

### 2) Capture host/client logs per variant
Host machine:
```powershell
# baseline
powershell -ExecutionPolicy Bypass -File automation/run_wan_host_capture.ps1 -ConfigPath automation/native_video_profile_1080p_lowlat.json -ExeDir build-vcpkg-local/apps/native_poc/Debug -Tag m9off

# apply
powershell -ExecutionPolicy Bypass -File automation/run_wan_host_capture.ps1 -ConfigPath automation/tmp_m9_apply.json -ExeDir build-vcpkg-local/apps/native_poc/Debug -Tag m9on
```

Client machine:
```powershell
# baseline
powershell -ExecutionPolicy Bypass -File automation/run_wan_client_capture.ps1 -ConfigPath automation/native_video_profile_1080p_lowlat.json -ExeDir build-vcpkg-local/apps/native_poc/Debug -RemoteHost <HOST_PUBLIC_IP_OR_DNS> -Tag m9off

# apply
powershell -ExecutionPolicy Bypass -File automation/run_wan_client_capture.ps1 -ConfigPath automation/tmp_m9_apply.json -ExeDir build-vcpkg-local/apps/native_poc/Debug -RemoteHost <HOST_PUBLIC_IP_OR_DNS> -Tag m9on
```

Each run creates:
- `automation/logs/wan-capture-<timestamp>-host-<tag>/...`
- `automation/logs/wan-capture-<timestamp>-client-<tag>/...`

### 3) Summarize one run pair
After copying host/client logs to one machine (if needed):
```powershell
powershell -ExecutionPolicy Bypass -File automation/summarize_wan_capture.ps1 `
  -HostInput automation/logs/wan-capture-<timestamp>-host-m9off `
  -ClientInput automation/logs/wan-capture-<timestamp>-client-m9off
```

Key outputs:
- `CLIENT_DECODED_FPS_AVG` (Gate A primary)
- `CLIENT_UDP_DROP_PM_AVG/MAX`
- `M9_MODE`, `M9_EVENT_COUNT`, `M9_ACTION_*`
- `GATE_A_DECODED_FPS_OK`, `GATE_A_PASS`

Note:
- `GATE_A_PRESENT_GAP_OK` can be `Unknown` if `presentGapOver1s` is not present in client stats line.
- In that case, use visual freeze observation plus log traces for final Gate A judgment.

## Optional preflight
```powershell
powershell -ExecutionPolicy Bypass -File automation/wan_preflight_native_video.ps1 -RemoteHost <HOST_PUBLIC_IP_OR_DNS> -VideoPort 43000 -ControlPort 43001
```
