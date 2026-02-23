# Next Project Plan: Native Ultra-Low-Latency Remote (Parsec-style)

Updated: 2026-02-23

## 1) Goal
- Target: "input feels immediate" on LAN.
- Practical latency target:
  - LAN p50: 20~60 ms end-to-end
  - WAN p50: 60~150 ms (network dependent)
- Keep smooth control first, visual quality second under load.

## 2) Why web mode cannot match Parsec feel
- Browser path adds playout/jitter buffering that is hard to force to near-zero.
- JS + browser WebRTC stack gives less direct control over decode/render timing.
- Native client can drop stale frames aggressively and render latest frame directly.

## 3) Architecture (new project)
- Host agent (Windows native):
  - Capture (Desktop Duplication / Windows Graphics Capture)
  - Hardware encode (H.264/HEVC low-latency settings)
  - Input injector (separate channel, high priority)
- Native client (Windows first, later mobile):
  - Hardware decode
  - Immediate present (latest-frame wins)
  - Separate input uplink
- Rendezvous/signaling service:
  - Session bootstrap only (not media relay in normal mode)
- Optional TURN/relay service for strict NAT environments.

## 4) Transport strategy
- Media: UDP-based real-time transport (WebRTC data model or custom QUIC/UDP stack).
- Input: separate low-volume reliable/ordered channel.
- Congestion policy:
  - Protect latency first (drop old frames before queueing deep).
  - Fast bitrate/FPS adaptation.

## 5) Core latency principles
- Zero or near-zero frame queue on both host and client.
- "Newest frame only" render policy.
- Disable B-frames and long lookahead.
- Keyframe interval short and controlled.
- Frame timestamping with capture clock (not synthetic frame counter only).
- Input channel never blocked by video channel backpressure.

## 6) Window-control mode requirement
- Support window-level capture/control even when not foreground.
- Known OS constraints:
  - Minimized windows often cannot provide live pixels.
  - Some apps ignore background input messages unless specific APIs are used.
- Strategy:
  - Explicit per-app capability matrix and fallback paths.

## 7) Milestones
- M1: Native host + native viewer, single-PC LAN test.
  - Measure capture->encode->decode->present and input RTT.
- M2: Multi-host via signaling server, direct P2P on LAN/WAN.
- M3: NAT hard cases with TURN/relay fallback.
- M4: Window-mode optimization + per-app compatibility profiles (LDPlayer etc.).
- M5: Hardening (reconnect, crash recovery, telemetry, installer/update).

## 8) Measurement and acceptance
- Required telemetry per session:
  - capture_ts, encode_ts, send_ts, recv_ts, decode_ts, present_ts
  - input_send_ts, host_apply_ts, client_visual_confirm_ts
- Pass criteria:
  - LAN p50 input-to-photon <= 60 ms
  - No long-tail freeze under normal CPU/GPU load
  - Reconnect without process crash

## 9) Security/ops baseline
- Auth token + session ACL.
- DTLS/SRTP or equivalent encrypted transport.
- Minimal bootstrap server cost (signaling-only in normal path).

## 10) Answer to "OSLink/Parsec is fast because mobile app player?"
- Partly no.
- Main reason is native low-latency pipeline (capture/encode/transport/decode/render), not "because it is a mobile app player".
- LDPlayer-like target may look faster because:
  - predictable rendering pattern,
  - often lower-motion UI than full desktop,
  - app-specific tuning.
- But the big latency win still comes from native pipeline control, not from emulator presence alone.

## 11) Current baseline and recent finding (2026-02-23)
- Current stable baseline in `apps/native_poc` is raw BGRA TCP streaming.
- Why it feels good now:
  - no inter-frame codec dependency
  - no decoder reordering/reference corruption path
- Limitation:
  - bandwidth is extremely high (~2 Gbps class at 1080p30), so not WAN-viable.
- Recent failed attempt summary:
  - direct H.264 path reintroduction caused visible artifact + extra delay
  - stale-frame skip strategy was unsafe for reference frames and increased corruption risk
- Decision:
  - keep raw path as known-good baseline
  - optimize in small measurable increments only

## 12) Execution order (optimization one-by-one)
1. Add precise end-to-end telemetry in native host/client.
2. Build encoded path branch behind feature flag (default OFF).
3. Validate frame boundary + decoder sync + reconnect stability.
4. Compare latency/bandwidth against raw baseline with fixed test scenes.
5. Promote encoded path only when corruption-free and latency target is met.

## 13) Implemented automation/tooling set (2026-02-23)
- Native video lifecycle scripts:
  - `automation/start_native_video_runtime.ps1`
  - `automation/status_native_video_runtime.ps1`
  - `automation/stop_native_video_runtime.ps1`
- Verification/summary:
  - `automation/verify_native_video_runtime.ps1`
- WAN preflight:
  - `automation/wan_preflight_native_video.ps1`
- Milestone status board:
  - `NATIVE_VIDEO_MILESTONES_M0_M7.md`
