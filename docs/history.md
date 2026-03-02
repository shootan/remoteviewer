# remote60 작업 히스토리 (NEW)

업데이트: 2026-03-03 01:58:43

목적
- 이 파일은 최근 작업만 유지해서 컨텍스트 소모를 줄인다.
- 전체 아카이브는 `docs/history_old.md`를 본다.

운영 규칙
- 기본 조회는 `docs/history.md`(이 파일)만 사용.
- 과거 상세가 꼭 필요할 때만 `docs/history_old.md`를 추가 조회.
- 새 항목은 이 파일에 누적하고, 일정 크기 이상이면 다시 아카이브 스냅샷을 만든다.

최근 항목 범위
- 62) 2026-03-03 ~ 최신

### 62) 2026-03-03 M5 phase-1 implemented (frame gating + static-scene downshift + keyframe throttling)
Goal
- Start M5 to reduce static-scene bandwidth/latency pressure and prevent keyframe request bursts.

Changes
1. Host frame gating + static downshift
- `apps/native_poc/src/native_video_host_main.cpp`
- Added sampled BGRA change detection and static/motion mode transitions.
- Added gating skip logic with static-scene send interval downshift.
- Added metrics in host periodic logs:
  - `frameGatingMode`, `frameGatingSkips`, `frameGatingStaticSkips`, `frameGatingChangePm`, `frameGatingChangeAvgPm`.

2. Host keyframe request limiter
- Added token-bucket + min-interval filter in control request handling.
- Added dropped counter and throttle log (`keyframe-request-throttled`).

3. Client keyframe request limiter
- `apps/native_poc/src/native_video_client_main.cpp`
- `request_keyframe()` now uses token-bucket + min-interval guard.
- Startup log includes limiter parameters.

4. Automation/profile config wiring
- `automation/run_native_video_with_config.ps1`
  - added JSON->env mapping for frame-gating and keyframe-limiter parameters.
- updated profiles:
  - `automation/native_video_profile_1080p_lowlat.json`
  - `automation/native_video_profile_1080p_external_template.json`

Validation
- Build: success (`cmake --build --preset debug-vcpkg --parallel`)
- Verify log: `automation/logs/verify-native-video-20260303-001616`
- Result: `HOST_RC=0`, `CLIENT_RC=0`
- Observed host log:
  - frame-gating entered static mode and exited to motion as expected.
  - MBPS reduced significantly during static periods.

Deployment sync
- Updated `D:\remote\build\bin` host/client exes and `D:\remote\build\automation` script/profile files.

### 63) 2026-03-03 M5 tuning decision (staticSceneFps=10 selected)
Test summary
- A/B/C with `REMOTE60_NATIVE_STATIC_SCENE_FPS={8,10,12}` under same verify scenario.
- Best balance observed at `10`.

Selected defaults
- `staticSceneFps=10`
- `frameGatingStaticThresholdPm=6`
- `frameGatingMotionThresholdPm=14`
- `keyframeReqMinIntervalUs=150000`
- `keyframeReqTokenRefillUs=250000`
- `keyframeReqTokenCapacity=3`

Profile usage policy
- Canonical runtime profile: `automation/native_video_profile_1080p_lowlat.json`
- External template kept only for handoff convenience: `automation/native_video_profile_1080p_external_template.json`

### 64) 2026-03-03 M6 phase-1: UDP assembly telemetry + catchup re-entry guard
Goal
- Reduce repeated keyframe storm patterns (`reason=1/5` loop) and make UDP loss symptoms measurable from verify output.

Changes
1. Client catchup/keyframe conflict mitigation
- File: `apps/native_poc/src/native_video_client_main.cpp`
- Added catchup re-entry minimum interval guard:
  - env: `REMOTE60_NATIVE_CATCHUP_REENTER_MIN_INTERVAL_US`
  - default: `600000` (600ms)
- Applied guard to:
  - lag-trigger catchup entry (`reason=1`)
  - decode-empty recovery catchup entry (`reason=5`)
- Added throttle logs:
  - `catchup-enter-throttled ...`
  - `decode-empty-recovery-throttled ...`

2. UDP assembly observability (client)
- Added per-second UDP assembly log:
  - `udp-assembly chunks=... completed=... dropped=... dropPm=... malformed=... reorder=... keyReq=...`
- Added conflict reduction in reorder/drop path:
  - keyframe request on assembly mismatch is suppressed while `waitForKeyFrame` or `catchupMode` is active.

3. Verify summary support
- File: `automation/verify_native_video_runtime.ps1`
- Added output keys:
  - `UDP_ASSEMBLY_SAMPLE_COUNT`
  - `UDP_ASSEMBLY_*_TOTAL`
  - `UDP_ASSEMBLY_DROP_PM_{COUNT,AVG,P95,MAX}`

4. Host UDP tx observability
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Added periodic host stats:
  - `udpTxFrames`, `udpTxChunks`, `udpTxChunkPerFrameX100`, `udpTxBytes`, `udpTxFail`, `udpTxNoPeer`

5. Config wiring
- File: `automation/run_native_video_with_config.ps1`
  - JSON -> env mapping added for `catchupReenterMinIntervalUs`.
- Updated profiles:
  - `automation/native_video_profile_1080p_lowlat.json`
  - `automation/native_video_profile_1080p_external_template.json`

Notes
- Runtime usage remains unified around one canonical profile:
  - `native_video_profile_1080p_lowlat.json` (host/client 怨듯넻, client??`-RemoteHost`濡?二쇱냼 二쇱엯).

### 65) 2026-03-03 WAN practical tuning result (quality/fps recovery)
Goal
- Validate real WAN usability (two-PC host/client) and recover fps/quality while preserving acceptable latency.

Execution
1. WAN capture workflow finalized
- Added host/client separated log capture workflow under `D:\remote\build\automation`:
  - `run_wan_host_capture.ps1`
  - `run_wan_client_capture.ps1`
  - `summarize_wan_capture.ps1`
- Logs were collected and summarized from real external runs.

2. Baseline vs tuned profile comparison
- Baseline pair:
  - host: `wan-capture-20260303-011441-host-wan1`
  - client: `wan-capture-20260303-011450-client-wan1`
- Tuned pair (`wanQ1`):
  - host: `wan-capture-20260303-012502-host-wanQ1`
  - client: `wan-capture-20260303-012514-client-wanQ1`

Metrics (baseline -> tuned)
- Host encoded fps avg: `17.59 -> 27.94` (+58.8%)
- Client decoded fps avg: `14.58 -> 24.50` (+68.0%)
- Client latency avg: `29.89ms -> 28.88ms` (3.4% 媛쒖꽑)
- Client latency p95: `67.60ms -> 44.20ms` (34.6% 媛쒖꽑)
- UDP assembly drop ratio: `5.36% -> 2.68%` (49.9% 媛쒖꽑)
- Client mbps avg: `4.65 -> 9.27`

Interpretation
- Frame/quality bottleneck in WAN baseline was mainly from low bitrate + frame gating policy.
- Raising bitrate and disabling frame gating recovered visual quality and effective fps without latency regression.

Operational decision
- Added WAN practical profile:
  - `automation/native_video_profile_1080p_wan_quality.json`
  - `10Mbps / keyint 60 / frameGatingDisable=true`
- Keep `1080p_lowlat` for worst-network or strict-latency preference.

### 66) 2026-03-03 history split (old/new)
- 배경: `docs/history.md` 파일 크기 증가로 컨텍스트 소모가 커짐.
- 조치:
  - 전체 기존 히스토리를 `docs/history_old.md`로 아카이브.
  - `docs/history.md`는 최근 항목(62~)만 유지하는 경량 파일로 재구성.
- 기대효과: 다음 컨텍스트에서 히스토리 로드 비용 감소 및 탐색 속도 개선.
