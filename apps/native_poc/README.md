# native_poc

Separate native host/client PoC project for low-latency path validation.

This directory is intentionally isolated from the existing web signaling/web client runtime.

## Targets
- `remote60_native_host_poc`
- `remote60_native_client_poc`
- `remote60_native_video_host_poc`
- `remote60_native_video_client_poc`

## Scope (Phase 0)
- UDP socket path latency baseline.
- Host sends frame-tick messages at target FPS.
- Client replies ACK immediately.
- Host prints RTT stats every second.
- Client prints receive stats every second.

## Scope (Phase 1: video + control baseline)
- Host captures desktop frames (WGC) and streams frames over TCP.
  - `--codec raw` (default)
  - `--codec h264` (experiment gate)
- Client receives and renders in a native Win32 window.
- Separate control/input channel is available on a dedicated port.
  - Input injection into host OS is still excluded in this PoC.

## Existing host code reuse
- `remote60_native_host_poc` links existing capture runtime source from `apps/host/src/capture_runtime.cpp`.
- You can run a capture-only probe:
  - `remote60_native_host_poc --capture-probe 5`

## Run
### Minimal external run (recommended)
1. Host (on the machine being shared):
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_video_host_poc.exe
```

2. Client (from another machine):
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_video_client_poc.exe --host <HOST_PUBLIC_IP_OR_DNS>
```

Defaults used by the two commands above:
- video port: `43000` (TCP)
- codec: `raw`
- control/input channel: disabled (`control-port=0`)

### A) Transport baseline (Phase 0)
1. Start client:
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_client_poc.exe --port 41000
```

2. Start host:
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_host_poc.exe --target 127.0.0.1 --port 41000 --fps 120
```

### B) Video-only render check (Phase 1)
1. Start native video client:
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_video_client_poc.exe --host 127.0.0.1 --port 43000
```

2. Start native video host:
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_video_host_poc.exe --bind-port 43000 --fps 30
```

### C) With telemetry + separate control RTT channel
1. Host:
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_video_host_poc.exe --bind-port 43000 --fps 30 --control-port 43001 --trace-every 60
```

2. Client:
```powershell
.\build-native2\apps\native_poc\Debug\remote60_native_video_client_poc.exe --host 127.0.0.1 --port 43000 --control-port 43001 --trace-every 60
```

### D) H.264 experiment path (feature-gated)
1. Host:
```powershell
$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1
.\build-native2\apps\native_poc\Debug\remote60_native_video_host_poc.exe --bind-port 43000 --fps 60 --codec h264 --bitrate 1100000 --keyint 45 --control-port 43001
```

2. Client:
```powershell
$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1
.\build-native2\apps\native_poc\Debug\remote60_native_video_client_poc.exe --host 127.0.0.1 --port 43000 --codec h264 --fps-hint 60 --control-port 43001
```

## Runtime automation
- Start (background): `automation/start_native_video_runtime.ps1`
- Status: `automation/status_native_video_runtime.ps1`
- Stop: `automation/stop_native_video_runtime.ps1`
- Verify (latency summary): `automation/verify_native_video_runtime.ps1`
- H264 tuning sweep: `automation/tune_native_video_h264.ps1`
- WAN preflight: `automation/wan_preflight_native_video.ps1`
- External bundle pack: `automation/package_native_video_external_bundle.ps1`
- JSON profile runner: `automation/run_native_video_with_config.ps1`
  - profiles:
    - `automation/native_video_profile_720p.json`
    - `automation/native_video_profile_1080p.json`
    - `automation/native_video_profile_1080p_lowlat.json`
    - `automation/native_video_profile_1080p_wan_quality.json`
    - `automation/native_video_profile_1080p_quality_10m_k60.json`
    - `automation/native_video_profile_1080p_quality_12m_k60.json`
    - `automation/native_video_profile_1080p_external_template.json`
    - `automation/native_video_profile_1296p_balanced.json`

### JSON profile run examples
Host:
```powershell
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role host -ConfigPath automation/native_video_profile_720p.json -ExeDir build-native2/apps/native_poc/Debug
```

Client:
```powershell
powershell -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role client -ConfigPath automation/native_video_profile_720p.json -ExeDir build-native2/apps/native_poc/Debug -RemoteHost <HOST_IP>
```

### External WAN checklist (UDP profile default)
- Port forwarding:
  - UDP `43000` (video)
  - TCP `43001` (control)
- Address input:
  - pass `-RemoteHost <HOST_PUBLIC_IP_OR_DNS>` on client, or
  - set `remoteHost` in profile JSON.
- Bundle output:
```powershell
powershell -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local
```

## Notes
- Phase 0 is transport baseline only.
- Phase 1 is raw BGRA video stream + native render.
- Encoded-path switches are guarded behind experiment gate:
  - build-time define `REMOTE60_NATIVE_ENCODED_EXPERIMENT`
  - runtime override env `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`

## Current Status (2026-02-23)
- Stable baseline (current best): raw BGRA path.
- Verified on localhost:
  - smooth rendering and low visible delay
  - no decode artifact path on raw
- Tradeoff:
  - very high bandwidth at 1080p30 (roughly ~2 Gbps class)
  - not suitable for WAN/public internet deployment as-is
- H264 experiment (localhost, 2026-02-23) is wired end-to-end:
  - measured average media bandwidth around ~2.0 Mbps in current settings
  - measured latency still higher than raw baseline, so tuning/optimization is still required

## Optimization Roadmap (Parsec-level target)
1. Measurement first
- Add per-frame timeline logs:
  - `capture_ts`, `encode_ts`, `send_ts`, `recv_ts`, `decode_ts`, `present_ts`
- Keep raw path as control group for regression comparison.

2. Re-introduce encoded path safely
- Rebuild H.264 path with strict AU/frame boundary handling.
- Do not drop reference-dependent frames blindly.
- Add decoder reset/re-sync path for corruption recovery.

3. Latency-first decode/render
- Prefer hardware decode + zero/low-copy present path.
- Keep queue depth shallow (latest-frame policy with keyframe safety).

4. Transport/congestion tuning
- Move media path to UDP-based real-time transport.
- Add pacing + bitrate/FPS adaptation under loss/jitter.

5. Input channel separation
- Keep input/control independent from video backpressure.
- Validate input-to-photon separately from video-only latency.

## Acceptance Gate (each optimization step)
- No visual corruption under normal desktop motion.
- No regression in connect/disconnect stability.
- Measurable latency/bandwidth improvement against previous step.

## Milestone tracking
- See `NATIVE_VIDEO_MILESTONES_M0_M7.md` for M0~M7 status and quick commands.
