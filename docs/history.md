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

### 67) 2026-03-03 M10 phase-1: reconnect hardening (host + soak script)
Goal
- Client restart/kill 후 host 재시작 없이 세션을 다시 붙일 수 있게 연결 라이프사이클을 강화.

Changes
1. Host reconnect lifecycle hardening
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Applied:
  - TCP listen socket lifetime changed to persistent (no one-shot close).
  - Added TCP data reconnect path on send failure:
    - log: `data disconnected reason=... waiting reconnect`
    - log: `client reconnected transport=tcp ...`
  - Control channel changed from single-accept to multi-accept loop:
    - reconnecting client can open control channel repeatedly without host restart.
    - logs:
      - `[native-video-host][control] client connected`
      - `[native-video-host][control] client disconnected`
  - UDP peer rebind support:
    - socket set to non-blocking after initial hello/ack.
    - runtime `hello` pump updates peer and re-acks.
    - log: `udp peer updated; forcing keyframe`

2. Reconnect soak automation
- Added script:
  - `automation/soak_native_video_reconnect.ps1`
- Purpose:
  - keep host alive
  - repeatedly launch/exit client (`Cycles`, `ClientRunSec`)
  - emit pass/fail summary with per-cycle RC.

Validation
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success
- Soak smoke:
  - UDP: `automation/logs/reconnect-soak-20260303-021117-m10smoke` (3/3 pass)
  - TCP: `automation/logs/reconnect-soak-20260303-021301-m10tcp` (2/2 pass)

Next
- Run Gate B full test:
  - `automation/soak_native_video_reconnect.ps1 -ConfigPath automation/native_video_profile_1080p_lowlat.json -Cycles 20 -ClientRunSec 3 -Tag m10-gateb`
- If full pass, mark M10 acceptance complete in `docs/구현계획.md`.

### 68) 2026-03-03 M10 Gate B pass (20-cycle reconnect soak)
Goal
- Validate M10 acceptance with long reconnect loop while host stays alive.

Execution
- Command:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/soak_native_video_reconnect.ps1 -ConfigPath automation/native_video_profile_1080p_lowlat.json -RemoteHost 127.0.0.1 -Cycles 20 -ClientRunSec 3 -Tag m10-gateb`
- Log:
  - `automation/logs/reconnect-soak-20260303-021557-m10-gateb`
  - summary: `automation/logs/reconnect-soak-20260303-021557-m10-gateb/summary.txt`

Result
- `SOAK_CYCLES_REQUESTED=20`
- `SOAK_CYCLES_EXECUTED=20`
- `SOAK_OK_COUNT=20`
- `HOST_RC=0`
- `RESULT=PASS`

Decision
- M10 Gate B acceptance condition met (20-cycle reconnect soak pass).
- Next focus moves to M8 congestion handling + Gate A (`decoded FPS >= 20`) reporting integration.

### 69) 2026-03-03 M8 phase-1 implemented (client congestion state + Gate A report fields)
Goal
- Implement M8 core receive/decode congestion handling and wire measurable Gate A outputs.

Changes
1. Client congestion state machine
- File: `apps/native_poc/src/native_video_client_main.cpp`
- Added explicit states:
  - `normal`, `recovering`, `congested`
- Added transition logs:
  - `[native-video-client][congestion] state=... prev=... reason=...`
- Added recovery handling:
  - recover-min window and recover-timeout re-request path.

2. Deterministic stale/hold-latest drop policy
- Added stale-drop checks against:
  - last presented capture timestamp
  - latest seen capture timestamp (hold-to-latest behavior for delayed burst)
- Added counters:
  - `staleDrops`, `holdLatestDrops`, `burstDrops`.

3. Queue-depth / recovery telemetry
- Added per-second telemetry fields in client stats line:
  - `congestionState`, `congestionTransitions`, `congestionRecoveryCount`,
    `congestionRecoveryAvgUs`, `congestionRecoveryMaxUs`, `congestionRecoveryReq`,
    `queueDepthSamples`, `queueDepthMax`, `queueDepthH0..H4p`.

4. Config wiring for M8 knobs
- File: `automation/run_native_video_with_config.ps1`
- Added JSON -> env mapping:
  - `staleCaptureDropUs` -> `REMOTE60_NATIVE_STALE_CAPTURE_DROP_US`
  - `congestRecoverMinUs` -> `REMOTE60_NATIVE_CONGEST_RECOVER_MIN_US`
  - `congestRecoveryTimeoutUs` -> `REMOTE60_NATIVE_CONGEST_RECOVERY_TIMEOUT_US`
- Updated profiles:
  - `automation/native_video_profile_1080p_lowlat.json`
  - `automation/native_video_profile_1080p_external_template.json`
  - `automation/native_video_profile_1080p_wan_quality.json`

5. Gate A report fields
- File: `automation/verify_native_video_runtime.ps1`
- Added parsed outputs:
  - `CONGESTION_*`, `QUEUE_DEPTH_*`
  - `GATE_A_TARGET_*`, `GATE_A_DECODED_FPS_OK`, `GATE_A_FREEZE_OK`, `GATE_A_PASS`

Validation
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success
- Local short verify script run completed without runtime errors.
- Note: current local environment produced no capture frames, so Gate A acceptance still requires external two-PC run logs.

### 70) 2026-03-03 M9 stage1~2 implemented (telemetry extension + host dry-run planner)
Goal
- Progress M9 without external 2PC dependency:
  - Stage 1: extend client->host telemetry.
  - Stage 2: host adaptive policy planner in dry-run mode.

Changes
1. Protocol / telemetry extension
- File: `apps/native_poc/src/poc_protocol.hpp`
- `ControlClientMetricsMessage` extended with:
  - `congestionState`, `congestionTransitions`, `congestionRecoveryCount`,
    `congestionRecoveryReq`, `congestionRecoveryMaxUs`,
    `queueDepthMax`, `queueDepthH4p`, `udpAssemblyDropPm`.

2. Client metrics publish/send update
- File: `apps/native_poc/src/native_video_client_main.cpp`
- New M8 congestion/queue counters are now copied into `gClientMetrics` and sent through control channel.

3. Host metrics ingest update
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Host now receives/stores the extended telemetry set for policy decisions.

4. M9 dry-run decision engine
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Added host-side M9 planner with:
  - cooldown + down/up pressure windows (`downRequireSec`, `upRequireSec`)
  - level recommendation ladder (bitrate -> fps -> resolution ordering)
  - decision logs:
    - `[native-video-host][m9] action=... mode=dryrun ...`
- `m9Apply=true` currently logs `apply-path-deferred=1` only (actual apply path deferred to stage 3).

5. Config/runtime wiring + verify outputs
- File: `automation/run_native_video_with_config.ps1`
  - Added JSON/env mapping:
    - `m9Enable`, `m9Apply`, `m9CooldownSec`, `m9DownRequireSec`, `m9UpRequireSec`
- Updated profiles:
  - `automation/native_video_profile_1080p_lowlat.json`
  - `automation/native_video_profile_1080p_external_template.json`
  - `automation/native_video_profile_1080p_wan_quality.json`
  - default: `m9Enable=true`, `m9Apply=false` (dry-run)
- File: `automation/verify_native_video_runtime.ps1`
  - Added M9 summary keys:
    - `M9_EVENT_COUNT`, `M9_LAST_MODE`, `M9_LAST_ACTION`,
      `M9_LAST_FROM_LEVEL`, `M9_LAST_TO_LEVEL`, `M9_APPLY_DEFERRED_COUNT`.

Validation
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success
- Note: external Gate A/B acceptance remains pending (requires user-side 2PC runs).

### 71) 2026-03-03 M9 stage3 implemented (host apply path wired)
Goal
- Complete M9 Stage 3 by converting host decision output into actual encoder/pacing apply behavior.

Changes
1. Unified encoder target apply helper usage
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Reused `apply_encoder_target(...)` in runtime tune and ABR switch paths to remove duplicated
  initialize/reconfigure branches.
- Runtime tune now applies against active runtime state (`activeEncodeW/H`, `activeFps`).

2. Dynamic FPS pacing integration
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Removed fixed `frameIntervalUs` usage from send pacing and interval error metrics.
- Host tick pacing and frame-gating interval selection now use dynamic `activeFrameIntervalUs`.
- Host periodic stats now include `fpsTarget=`.

3. M9 apply mode activation
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Removed deferred-only path (`apply-path-deferred=1`).
- When `m9Apply=true`, M9 level transitions now apply target bitrate/fps/resolution through
  `apply_encoder_target(...)` and trigger keyframe on successful level switch.
- If apply fails, host logs error and exits loop (same failure posture as ABR apply failures).

4. Policy conflict guard
- File: `apps/native_poc/src/native_video_host_main.cpp`
- ABR auto-switch block is now gated off while `m9Apply=true` to avoid ABR vs M9 dual-control conflict.

Validation
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success

Next
- Run external 2PC validation with `m9Apply=true` and compare against `m9Apply=false` baseline:
  - Gate A (`decoded FPS >= 20`) stability/freeze/readability
  - M9 transition behavior and QoE impact in WAN logs.

### 72) 2026-03-03 local verify smoke after docs/plan sync
Goal
- Record post-stage3 local validation snapshot before moving to external 2PC tests.

Execution
- Verify script runs (`build-vcpkg-local`, H264 + `mft_hw`):
  1. `m9Apply=false` baseline run:
     - log dir: `automation/logs/verify-native-video-20260303-164726`
  2. `m9Apply=true` apply-mode run:
     - log dir: `automation/logs/verify-native-video-20260303-164800`

Result
- Common:
  - `HOST_RC=0`
  - `CLIENT_RC=0`
  - no process hang/timeout during run shutdown
- Mode check:
  - baseline host start log: `m9=off m9Mode=dry-run`
  - apply host start log: `m9=on m9Mode=apply`
- Encoder path:
  - both runs report `H264 encoder backend=mft_enum_hw hw=1`

Metrics note
- This local environment had no captured frame flow:
  - `DEC_COUNT=0`, `LAT_COUNT=0`, `M9_EVENT_COUNT=0`
- Therefore, this run validates boot/mode wiring and shutdown stability only.
- QoE/performance acceptance remains pending external 2PC measurement logs.

### 73) 2026-03-04 external M9 A/B execution prep (WAN capture scripts restored)
Goal
- Prepare immediate execution path for external 2PC validation of `m9Apply=false` vs `m9Apply=true`.

Changes
1. WAN capture helpers added
- `automation/run_wan_host_capture.ps1`
- `automation/run_wan_client_capture.ps1`
- Behavior:
  - create per-run log directories under `automation/logs/wan-capture-<timestamp>-<role>-<tag>`
  - save role config snapshot (`host.config.json` / `client.config.json`)
  - run `run_native_video_with_config.ps1` with stdout/stderr redirected to capture logs

2. WAN capture summary helper added
- `automation/summarize_wan_capture.ps1`
- Inputs:
  - host capture dir/file
  - client capture dir/file
- Outputs:
  - decoded/encoded fps averages, latency/mbps aggregates, UDP drop pm stats
  - `M9_MODE`, `M9_EVENT_COUNT`, `M9_ACTION_*`
  - `GATE_A_DECODED_FPS_OK`, `GATE_A_PRESENT_GAP_OK`, `GATE_A_PASS`

3. Runbook update
- `docs/external_wan_test_guide.md`
  - Added M9 apply A/B workflow:
    - create `tmp_m9_apply.json` (`m9Apply=true`)
    - run host/client capture scripts for `m9off` and `m9on`
    - run summary script for each pair

Validation
- Parser/syntax checks:
  - `run_wan_host_capture.ps1`: parse OK
  - `run_wan_client_capture.ps1`: parse OK
  - `summarize_wan_capture.ps1`: parse OK
- Summary script smoke:
  - command:
    - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/summarize_wan_capture.ps1 -HostInput automation/logs/verify-native-video-20260303-144249 -ClientInput automation/logs/verify-native-video-20260303-144249`
  - result:
    - summary keys emitted normally (`CLIENT_DECODED_FPS_AVG`, `M9_MODE`, `GATE_A_PASS` etc.)

Next
- Execute external 2PC A/B runs (`m9off` / `m9on`) and attach summary outputs/log dirs for Gate A judgment.

### 74) 2026-03-04 simplify external 2PC execution (single wrapper entrypoint)
Goal
- Reduce command length and script confusion for host/client M9 A/B runs.

Changes
1. Added unified wrapper
- `automation/m9_easy.ps1`
- Supported actions:
  - `prepare`
  - `host off|on`
  - `client off|on [HOST]`
  - `summary off|on`
- Behavior:
  - auto-create `automation/tmp_m9_apply.json` when needed
  - auto-select exe dir:
    - `bin` if present (bundle layout)
    - otherwise `build-vcpkg-local/apps/native_poc/Debug`
  - `summary` auto-selects latest host/client capture dirs by tag (`m9off`/`m9on`)
  - client host argument is optional; if omitted, uses JSON `remoteHost`

2. Updated runbook with short commands
- `docs/external_wan_test_guide.md`
  - Added `Quick Start (short commands)` section using only `m9_easy.ps1`.

3. Synced to bundle runtime tree
- Copied to `D:\\remote\\build`:
  - `automation/m9_easy.ps1`
  - `docs/external_wan_test_guide.md` (updated)

Validation
- `powershell -NoProfile -ExecutionPolicy Bypass -File automation/m9_easy.ps1 help` -> usage output OK
- `powershell -NoProfile -ExecutionPolicy Bypass -File automation/m9_easy.ps1 prepare` -> profile handling OK
- `powershell -NoProfile -ExecutionPolicy Bypass -File ..\\build\\automation\\m9_easy.ps1 help` -> default root/exe dir resolved to `D:\\remote\\build`/`bin`
- `powershell -NoProfile -ExecutionPolicy Bypass -File ..\\build\\automation\\m9_easy.ps1 prepare` -> `tmp_m9_apply.json` created under `D:\\remote\\build\\automation`

Next
- Run external 2PC with the short sequence:
  - host `off/on`, client `off/on`, summary `off/on`
  - judge Gate A and M9 transition behavior from summary output.

### 75) 2026-03-05 M13 phase1 start (window-scoped host capture + rebind)
Goal
- Start M13 implementation that does not require external 2PC:
  - window-target selection by process/title
  - client-area scoped capture path
  - automatic capture session rebind when target window handle/size changes

Changes
1. Host window target selection + rebind
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Added host args:
  - `--capture-window-process`
  - `--capture-window-title`
  - `--capture-window-client-only`
  - `--capture-window-rebind-interval-ms`
- Added window discovery/match logic:
  - visible/non-minimized top-level candidate filtering
  - process-name and title-substring filtering
- Added periodic target re-discovery and session rebind.

2. Capture resource refresh for size changes
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Capture callback now detects source size mismatch and requests session restart.
- Restart path now recreates staging texture + frame pool size from current capture item size.
- Host periodic stats now include:
  - `captureTargetPid`
  - `captureTargetProc`
  - `captureTargetHwnd`

3. Client-area crop path
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Added optional client-area crop in callback when `--capture-window-client-only` is enabled.

4. JSON/runtime wiring
- File: `automation/run_native_video_with_config.ps1`
- Added profile keys:
  - `captureWindowProcess`
  - `captureWindowTitle`
  - `captureWindowClientOnly`
  - `captureWindowRebindIntervalMs`
- Added host arg wiring for those keys.
- Updated profiles:
  - `automation/native_video_profile_1080p_lowlat.json`
  - `automation/native_video_profile_1080p_wan_quality.json`
  - `automation/native_video_profile_1080p_external_template.json`

5. Runbook note
- File: `docs/external_wan_test_guide.md`
- Added optional section for window-scoped capture keys/example.

Validation
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success

Next
- Run local/real host validation for emulator targets with:
  - `captureWindowProcess`, `captureWindowTitle`, `captureWindowClientOnly=true`
- Collect logs for:
  - `capture-window rebound ...`
  - `capture-size-updated ...`
