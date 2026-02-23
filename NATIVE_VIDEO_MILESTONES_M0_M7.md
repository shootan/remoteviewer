# Native Video Milestones (M0~M7)

Updated: 2026-02-23

## M0 Baseline Lock (Done)
- Stable baseline is `raw BGRA` path.
- Keep default runtime on raw path.

## M1 Telemetry (Done)
- Added timestamp points in raw frame header:
  - `captureQpcUs`
  - `sendQpcUs`
- Added trace logs:
  - host: `[trace] capture/send`
  - client: `[trace_recv]`, `[trace_present]`
- Trace controls:
  - `--trace-every <N>`
  - `--trace-max <N>`

## M2 Encoded-path Feature Flag Scaffold (Done)
- Added CMake option:
  - `REMOTE60_NATIVE_ENCODED_EXPERIMENT` (default: OFF)
- Added runtime switch:
  - `--codec raw` (default)
  - non-raw values are rejected unless experiment build is enabled.

## M3 Latency-first Render Stability (Done)
- Added paint-queue guard on client to avoid invalidation backlog.
- Keep latest-frame behavior and trigger repaint only when needed.

## M4 Transport Tuning Knobs (Done)
- Host:
  - `--tcp-sendbuf-kb`
- Client:
  - `--tcp-recvbuf-kb`
  - `--tcp-sendbuf-kb`
- Effective socket buffer values are printed at startup.

## M5 Input Channel Separation Baseline (Done)
- Added optional independent control channel (separate TCP port):
  - Host: `--control-port`
  - Client: `--control-port`, `--control-interval-ms`
- Added ping/pong RTT telemetry:
  - client logs `[control] seq=... rttUs=...`

## M6 WAN Rehearsal Tooling (Done)
- Added preflight script:
  - `automation/wan_preflight_native_video.ps1`
- Checks:
  - video/control TCP connectivity
  - local IPv4 interfaces

## M7 Ops/Validation Automation (Done)
- Added native video runtime lifecycle scripts:
  - `automation/start_native_video_runtime.ps1`
  - `automation/start_native_video_runtime_impl.ps1`
  - `automation/status_native_video_runtime.ps1`
  - `automation/stop_native_video_runtime.ps1`
- Added smoke verification script:
  - `automation/verify_native_video_runtime.ps1`

## Quick Commands
1. Start host/client in background:
```powershell
powershell -ExecutionPolicy Bypass -File automation/start_native_video_runtime.ps1 -Port 42010 -ControlPort 42011 -StartClient
```

2. Check status:
```powershell
powershell -ExecutionPolicy Bypass -File automation/status_native_video_runtime.ps1
```

3. Stop:
```powershell
powershell -ExecutionPolicy Bypass -File automation/stop_native_video_runtime.ps1
```

4. Verify and summarize latency:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Port 42020 -ControlPort 42021
```

## Execution Scope (M8~M14, 2026-02-23)

## M8 Baseline Lock (Done)
- `verify_native_video_runtime.ps1` raw profile repeated 5 runs.
- Baseline range (localhost):
  - `LAT_AVG_US`: ~14.1k to ~16.0k
  - `LAT_P95_US`: ~14.3k to ~15.3k
  - `CTRL_RTT_AVG_US`: ~160 to ~294

## M9 Encoded-path Switch Completion (Done)
- Default runtime remains `raw`.
- `--codec h264` path is guarded by experiment gate:
  - build define `REMOTE60_NATIVE_ENCODED_EXPERIMENT`
  - runtime override `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1` for local experiment runs

## M10 H.264 Minimum E2E (Done)
- Host path wired: `capture -> encode(H264) -> send`.
- Client path wired: `recv -> decode(H264) -> render`.
- Trace continuity preserved: `capture/send/recv/present`.

## M11 Corruption Recovery Stability (Done)
- Keyframe-aware resync guard added on client.
- Decoder flush/reset on decode failure.
- Latest-frame presentation behavior preserved.

## M12 Latency Optimization (In Progress)
- Queue/backlog controls and decode-side guard are in place.
- Additional decoder stabilization was applied:
  - `MF_E_NOTACCEPTING` drain/retry handling
  - input-sample-time fallback mapping when decoder output sample-time is missing
  - 2D NV12 output-buffer copy fallback for hardware decoder compatibility
- Latest localhost checkpoints (HW paths):
  - `automation/logs/verify-native-video-20260223-214859`
  - `EncoderBackend=mft_hw`, `DecoderBackend=mft_hw`
  - `STAGE_TOTALUS_AVG_US=67123.3` (~67ms)
  - bottleneck remains `c2eUs` (`27146.6us` average)
- Additional gate check run:
  - `automation/logs/verify-native-video-20260223-220535`
  - `STAGE_TOTALUS_AVG_US=76534.5`, bottleneck `c2eUs=35838.67`
  - host-side experimental knobs were added (default OFF):
    - `REMOTE60_NATIVE_H264_NO_PACING=1`
    - `REMOTE60_NATIVE_GUARD_STALE_PREENCODE=1`
    - `REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS=1..4`

## M13 Bandwidth/FPS Tuning (In Progress)
- Added tuning script: `automation/tune_native_video_h264.ps1`.
- Updated localhost checkpoints:
  - `automation/logs/verify-native-video-20260223-214743`
    - `EncoderBackend=amf_hw`, `DecoderBackend=amf_hw` 요청
    - 실제 decoder는 `mft_enum_hw` 폴백으로 동작
    - `STAGE_TOTALUS_AVG_US=74498.54`
  - `automation/logs/verify-native-video-20260223-214803`
    - `EncoderBackend=amf_hw`, `DecoderBackend=mft_sw`
    - `STAGE_TOTALUS_AVG_US=68833.75`
  - `automation/logs/verify-native-video-20260223-214923`
    - `EncoderBackend=mft_hw`, `DecoderBackend=mft_hw`, bitrate `4Mbps` (1080p)
    - `STAGE_TOTALUS_AVG_US=144685.33`, `MBPS_AVG=2.2`
- `~1.1Mbps` 구간(자동 720p 폴백)은 현재 60~80ms대 확인됐으나,
  `1080p` 고해상도 구간은 120ms대 이상으로 추가 튜닝이 필요함.
- `P1-Gate` 관련 최신 수치:
  - `720p60` (`bitrate=1.1Mbps`):
    - log `automation/logs/verify-native-video-20260223-220641`
    - `STAGE_TOTALUS_AVG_US=75233.71`, `LAT_P95_US=66580`
  - `1080p60` (`bitrate=4Mbps`):
    - log `automation/logs/verify-native-video-20260223-220659`
    - `STAGE_TOTALUS_AVG_US=135371.67`, `LAT_P95_US=137925`
    - `PRESENT_GAP_OVER_3S=1`
- 결론: 720p60은 목표권, 1080p60은 아직 게이트 미달(추가 최적화 필요).

## M14 Input Channel Integration (Done)
- Control channel extended to carry input events + ack:
  - `ControlInputEvent`
  - `ControlInputAck`
- Input/control flow remains separated from video stream/backpressure path.

Note: this execution intentionally stops at `M14`; `M15 (WAN rehearsal/promotion)` is excluded.
