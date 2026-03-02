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

## Optional preflight
```powershell
powershell -ExecutionPolicy Bypass -File automation/wan_preflight_native_video.ps1 -RemoteHost <HOST_PUBLIC_IP_OR_DNS> -VideoPort 43000 -ControlPort 43001
```
