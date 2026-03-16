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

## Config-first launch
- `automation/run_native_video_with_config.ps1` can now run with config only.
- Default config file name: `automation/run_native_video_with_config.json`
- Recommended flow:
  - copy a profile JSON to `automation/run_native_video_with_config.json`
  - add `"role": "host"` or `"role": "client"`
  - for client, set `"remoteHost": "<HOST_PUBLIC_IP_OR_DNS>"`
  - run `powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1`
- Old explicit overrides (`-Role`, `-ConfigPath`, `-ExeDir`, `-RemoteHost`) still work when needed.

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

Local M13 phase4 mode-switch validation (no 2PC required):
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_m13_mode_switch.ps1 `
  -Root . `
  -ExeDir build-vcpkg-local/apps/native_poc/Debug `
  -BaseConfig automation/native_video_profile_1080p_lowlat.json `
  -HostClientSeconds 18 `
  -TargetProcess notepad.exe `
  -RemoteHost 127.0.0.1
```

Expected summary keys:
- `MODE_SWITCH_EVENT_COUNT>=2` (overview + focus applied)
- `FOCUS_APPLY_COUNT>=1`
- `HOSTCAP_METADATA_EVENT_COUNT>=1`
- `M13_MODE_SWITCH_PASS=True`

## Emulator preset profiles (M13 phase3 extension)
Use these presets when you want window-scoped capture for each emulator app:
- `automation/native_video_profile_1080p_ldplayer_window.json`
  - process: `dnplayer.exe`
  - title filter: `LDPlayer`
- `automation/native_video_profile_1080p_bluestacks_window.json`
  - process: `HD-Player.exe`
  - title filter: `BlueStacks`
- `automation/native_video_profile_1080p_nox_window.json`
  - process: `Nox.exe`
  - title filter: `Nox`

Common preset behavior:
- `captureWindowClientOnly=true`
- `captureWindowRebindIntervalMs=1000`

Run example (LDPlayer preset):
```powershell
Copy-Item automation/native_video_profile_1080p_ldplayer_window.json automation/run_native_video_with_config.json
# host: add "role": "host"
# client: add "role": "client", "remoteHost": "<HOST_PUBLIC_IP_OR_DNS>"
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1
```

## Recommended FHD profiles
- `automation/native_video_profile_1080p_external_template.json` (native 2PC GUI default, 30fps fixed, input/control on)
- `automation/native_video_profile_1080p_window_input_template.json` (native window-target input template, not desktop-wide input)
- `automation/native_video_profile_1080p_lowlat.json` (8Mbps, low-latency baseline)
- `automation/native_video_profile_1080p_wan_quality.json` (10Mbps, keyint 60, frame gating off)
- `automation/native_video_profile_1080p_quality_10m_k60.json`
- `automation/native_video_profile_1080p_quality_12m_k60.json`
- `automation/native_video_profile_1080p_ldplayer_window.json` (M13 window-scoped preset)
- `automation/native_video_profile_1080p_bluestacks_window.json` (M13 window-scoped preset)
- `automation/native_video_profile_1080p_nox_window.json` (M13 window-scoped preset)

## Run examples
Host:
```powershell
Copy-Item automation/native_video_profile_1080p_external_template.json automation/run_native_video_with_config.json
# add "role": "host"
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1
```

Client:
```powershell
Copy-Item automation/native_video_profile_1080p_external_template.json automation/run_native_video_with_config.json
# add "role": "client"
# set "remoteHost": "<HOST_PUBLIC_IP_OR_DNS>"
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1
```

Note:
- `native_video_profile_1080p_lowlat.json` is not a fixed-30fps smoke profile.
- It leaves frame gating enabled and can intentionally downshift to `10fps` on static scenes (`staticSceneFps=10`).
- For generic external 2PC connectivity/perf smoke, use `native_video_profile_1080p_external_template.json` or `automation/native_video_profile_1080p.json`.
- `native_video_profile_1080p_external_template.json` now enables control/input so the native client home picker can drive `Desktop Mode` and window selection directly.

## Native Input Scope
- The native bundle path is not the browser GUI path.
- It does not provide the old browser page, but it now provides equivalent native controls for `Desktop Mode`, window list, and selected-target input.
- Native input in current phase supports:
  - desktop mode: screen-point based input routing
  - selected window mode: fixed target window input routing
- The separate `window_input_template` profile remains for config-locked/fixed-target automation.

Native client UX:
- first screen: home picker overlay (`Desktop Mode` + window list + `Refresh`)
- after selection: fullscreen video
- reopen picker: top-left `Targets` button

## Native Window-Target Input
Use `automation/native_video_profile_1080p_window_input_template.json` when you want click/drag/keyboard injection into a specific target window.

Edit these keys first:
- `inputTargetProcess` or `inputTargetTitle`
- `captureWindowProcess` or `captureWindowTitle`
- `remoteHost`

Example:
```powershell
Copy-Item automation/native_video_profile_1080p_window_input_template.json automation/run_native_video_with_config.json
# host: add "role": "host"
# client: add "role": "client", "remoteHost": "<HOST_PUBLIC_IP_OR_DNS>"
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1
```

Notes:
- `run_native_video_with_config.ps1` now uses config-first defaults and passes only `--config` unless you explicitly override.
- For validation, `automation/validate_background_input_injection.ps1` remains the shortest local check.

## Browser GUI Path
If you need the browser UI with `desktop / window list / touch input`, use the web runtime instead of the native bundle.

Host machine:
```powershell
powershell -ExecutionPolicy Bypass -File automation/run_web_runtime.ps1 -Port 3000
```

Client device:
- Open `http://<HOST_PUBLIC_IP_OR_DNS>:3000`
- This is the path that contains the web client's window list UI and touch handlers.

Portable web bundle:
```powershell
powershell -ExecutionPolicy Bypass -File automation/package_web_runtime_external_bundle.ps1 -BuildDir build-vcpkg-local
```

Notes:
- This bundle packages `remote60_host.exe`, `apps/signaling`, and web runtime scripts.
- Node.js is still required on the host machine.

## Portable bundle
Create a portable bundle with current binaries, profiles, and helper scripts:
```powershell
powershell -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local
```

Bundle contents:
- `bin/`: host/client exe + dependent dll files
- `automation/`: `run_native_video_with_config.ps1`, `m9_easy.ps1`, WAN capture/summarize scripts, profile JSONs
- `docs/EXTERNAL_WAN_QUICKSTART.md`: copy-ready commands for host/client and M9 A/B capture

## Quick Start (short commands)
From the bundle root or source tree, use one wrapper script:
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
- `m9_easy.ps1` is an M9 A/B helper and uses `native_video_profile_1080p_lowlat.json` internally.
- That baseline keeps frame gating enabled, so static scenes can intentionally appear around `10fps`.
- For generic fixed-30fps external smoke, use the `run_native_video_with_config.ps1` examples above with `native_video_profile_1080p_external_template.json`.
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
