# remote60 작업 히스토리 (NEW)

업데이트: 2026-04-09

목적
- 이 파일은 최근 작업만 유지해서 컨텍스트 소모를 줄인다.
- 아카이브는 `docs/history_old.md`, `docs/history_old2.md`를 본다.

운영 규칙
- 기본 조회는 `docs/history.md`(이 파일)만 사용.
- 과거 상세가 꼭 필요할 때만 `docs/history_old.md` 또는 `docs/history_old2.md`를 추가 조회.
- 새 항목은 이 파일에 누적하고, 일정 크기 이상이면 다시 아카이브 스냅샷을 만든다.

최근 항목 범위
- 83) 2026-03-05 ~ 최신

### 83) 2026-03-05 docs policy cleanup (plan/history split + old2 archive)
Goal
- Keep `docs/구현계획.md` checklist-only and separate all history records into history docs.

Changes
1. Plan document cleanup
- File: `docs/구현계획.md`
- Removed appended history/progress snapshot blocks.
- Added explicit rule: checklist/priority only in plan, history/log snapshots in `docs/history.md`.

2. History archive rollover
- File: `docs/history_old2.md`
- Created new archive file from full previous `docs/history.md` snapshot.

3. History file slimming
- File: `docs/history.md`
- Rebuilt as lightweight current-history file.

4. Docs operation rules update
- Files:
  - `docs/README.md`
  - `AGENTS.md`
- Clarified that `history_old2.md` is also archive-only.
- Clarified `구현계획.md` update policy as checklist status only (no history narrative append).

Validation
- Confirmed `docs/구현계획.md` no longer contains `Latest Progress Snapshot` style sections.
- Confirmed `docs/history_old2.md` exists with archived content.
- Confirmed `docs/history.md` is reduced to lightweight structure.

Next
- Keep future task logs only in `docs/history.md`.
- Update `docs/구현계획.md` by checkbox/status changes only.

### 84) 2026-03-05 docs rule enforcement (plan cleanup + role separation)
Goal
- Enforce strict split: history content in `docs/history.md` only, checklist status in `docs/구현계획.md` only.

Changes
1. Plan cleanup
- File: `docs/구현계획.md`
- Removed tail handoff/history sections (from `## 7) Context Handoff Snapshot ...` onward).
- Kept initial checklist/priorities and added explicit document-operation rules.

2. Agent policy clarification
- File: `AGENTS.md`
- Added explicit line that:
  - `docs/history.md` keeps history only
  - `docs/구현계획.md` keeps checklist status updates only

Validation
- Confirmed `docs/구현계획.md` no longer contains `Context Handoff Snapshot`, `Roadmap Update`, or trailing English roadmap blocks.
- Confirmed AGENTS workflow now explicitly states history/plan role separation.

Next
- Continue updating only checklist checkboxes/status in `docs/구현계획.md`.
- Write all execution narratives and outcomes only to `docs/history.md`.

### 85) 2026-03-05 direct app JSON config support (`--config`)
Goal
- Run native video host/client directly from app executable using JSON profile, without PowerShell wrapper dependency.

Changes
1. Added shared JSON profile loader/env mapper
- File: `apps/native_poc/src/json_profile.hpp`
- Added lightweight JSON key readers (string/u32/bool) and runtime env override mapping previously handled by script.

2. Host app direct config support
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Added `--config <path>` handling.
- Parse order: JSON defaults first, then CLI flags override.

3. Client app direct config support
- File: `apps/native_poc/src/native_video_client_main.cpp`
- Added `--config <path>` handling.
- Supports `remoteHost/host` from JSON, with CLI `--host` override.

4. Usage docs update
- File: `apps/native_poc/README.md`
- Added direct executable examples using `--config`.

Validation
- Build passed:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
- Output binaries:
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_host_poc.exe`
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_poc.exe`

Next
- Run 2PC smoke with direct `--config` flow and confirm gate logs are equivalent to PowerShell wrapper execution.

### 86) 2026-03-05 plan update: highest priority set to background input injection
Goal
- Reflect new top-priority work in `docs/구현계획.md`: background input injection without real cursor movement.

Changes
1. Priority notice added
- File: `docs/구현계획.md`
- Added `긴급 우선순위 공지 (2026-03-05)` with constraints:
  - no real OS cursor move
  - click/drag/keyboard only
  - input must work for occluded target window (direct HWND injection)

2. Milestone priority updated
- File: `docs/구현계획.md`
- Changed current top milestone to `M3.5 Background 입력 주입 (우선)`.
- Added new section `M3.5. Background 입력 주입 (최우선)` checklist.
- Added this item to the top of execution order as highest priority.

Validation
- Docs-only change; no build/test required.

Next
- Start M3.5 implementation in host input path with `background_message` injection mode.

### 87) 2026-03-06 M3.5 1차 구현: background_message 입력 주입 경로 추가
Goal
- M3.5 최우선 항목 중 코드 구현 파트를 먼저 완료한다.
- 실 커서 이동 없이(HWND 메시지 주입) 클릭/드래그/키보드 입력을 주입한다.

Changes
1. Host 입력 주입 구현
- File: `apps/native_poc/src/native_video_host_main.cpp`
- `enableInputInjection`, `inputInjectionMode`, `inputTargetProcess`, `inputTargetTitle` 인자/JSON 파싱 추가.
- `background_message` 모드에서만 입력 주입 활성화.
- `PostMessageW` 기반 입력 주입 추가:
  - 마우스: down/up + drag(move with button only)
  - 키보드: key down/up
  - wheel 이벤트는 현재 단계에서 의도적으로 미주입.
- 타겟 HWND 해석:
  - `inputTargetProcess`/`inputTargetTitle` 지정 시 해당 윈도우 우선.
  - 미지정 시 현재 capture target HWND 사용.
- 입력 통계 카운터(`inputEvents`, `inputNoTarget`, `inputInjectFail` 등) 로그 반영.

2. Client 입력 전송 정책 정리
- File: `apps/native_poc/src/native_video_client_main.cpp`
- compile-time 입력 하드블록 상수 제거(기본 허용 + 런타임 설정으로 제어).
- `WM_MOUSEMOVE`는 드래그 중(버튼 눌림 상태)일 때만 전송하도록 변경.
- JSON/CLI에 `enableInputInjection`/`--enable-input-injection`를 입력 채널 활성 alias로 추가.

3. 문서 업데이트
- File: `apps/native_poc/README.md`
- background 입력 주입 가능 상태 및 JSON 키 설명 추가.
- File: `docs/구현계획.md`
- M3.5 구현 체크리스트 중 구현 완료 항목 체크 반영.

Validation
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Static check:
  - `rg -n \"SendInput|SetCursorPos\" apps/native_poc/src/native_video_host_main.cpp apps/native_poc/src/native_video_client_main.cpp`
  - 결과: 매치 없음

Next
- M3.5 1차 검증 시나리오 수행:
  - 백그라운드 Notepad 대상 클릭/드래그/키입력 확인
  - OS 커서 비이동 확인
  - occluded 상태 입력 반영 확인

### 88) 2026-03-06 M3.5 검증 보조 자동화 추가 (background input)
Goal
- M3.5 1차 검증을 빠르게 반복할 수 있도록 자동 실행/로그 판정 스크립트를 추가한다.

Changes
1. Validation helper script added
- File: `automation/validate_background_input_injection.ps1`
- Added end-to-end helper that:
  - builds temporary JSON profile with `enableInputInjection=true`, `inputInjectionMode=background_message`
  - sets target/capture filter to Notepad
  - runs host/client with `--config`
  - parses host log counters (`inputEvents`, `inputNoTarget`, `inputInjectFail`, `inputUnsupported`, `inputIgnoredMove`)
  - writes summary output (`summary.txt`) including manual-check checklist.

2. README usage update
- File: `apps/native_poc/README.md`
- Added script entry and usage section for M3.5 validation helper.
- Added note that interactive desktop session is required (`CLIENT_HWND=0x0` means auto input-burst binding failed).

3. Plan checklist status update
- File: `docs/구현계획.md`
- Marked validation-helper script addition as completed.
- Kept M3.5 manual verification scenario unchecked.

Validation
- Command:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation\\validate_background_input_injection.ps1 -DurationSec 8`
- Result snapshot (latest run):
  - `AUTO_PASS=0`
  - `CLIENT_HWND=0x0`
  - `INPUT_EVENTS=0`
  - `INPUT_NO_TARGET=0`
  - `INPUT_INJECT_FAIL=0`
- Interpretation:
  - Script path works and host metric parsing is valid.
  - Current execution context could not bind to native client window (`CLIENT_HWND=0x0`), so automatic input burst was not injected in this run.

Next
- Run the same helper in an interactive desktop session and complete manual checks:
  - cursor does not move
  - occluded target window still receives click/drag/keyboard input
- If manual checks pass, mark M3.5 verification scenario complete in `docs/구현계획.md`.

### 89) 2026-03-06 수동확인 항목 통합 정리 (pending-only checklist)
Goal
- 지금까지 남아있던 수동확인 대기 항목을 한 문서로 모아, 나중에 한 번에 확인 가능하게 만든다.

Changes
1. Manual-only checklist document added
- File: `docs/수동확인_체크리스트.md`
- Added pending manual verification items only:
  - M3.5 background input final manual checks
  - direct `--config` 2PC smoke confirmation
  - freeze user-side repro final confirmation
  - M9 external 2PC Gate A/B acceptance
  - M13 phase3 emulator rebind validation
  - M13 phase4 2PC mode-switch acceptance

2. Plan checkbox update
- File: `docs/구현계획.md`
- Added completed checkbox for manual-pending checklist documentation.

Validation
- Confirmed `docs/수동확인_체크리스트.md` exists and contains manual-only pending items with source references.
- Confirmed `docs/구현계획.md` reflects the documentation task as completed status update.

Next
- When interactive/2PC environment is available, execute items in `docs/수동확인_체크리스트.md` from P0 to P2 order.
- After each manual pass, update checkbox in that document and append detailed evidence to `docs/history.md`.

### 90) 2026-03-06 구현계획 정리: 코드 작업/검증 분리 + 미구현 코드 항목 식별
Goal
- 미완료 항목을 `코드 작업`과 `검증`으로 분리해 한 번에 필터링 가능하게 만든다.
- 체크되지 않은 항목 중 실제 코드 미구현 항목을 구분해 우선순위 판단 비용을 줄인다.

Changes
1. Plan 체크리스트 구조 개편
- File: `docs/구현계획.md`
- `미완료 항목 빠른 필터` 추가:
  - 코드 작업 필요
  - 검증/판정 필요(코드 완료 또는 부분 완료)
  - 검증 전용(추가 코드 작업 없음)
- `M3.5/M4/M5/M6/M7`을 `코드 작업`/`검증`(또는 `검증/설계`)으로 분리.
- `M5`는 코드 항목을 완료(`[x]`)로 반영하고 검증 항목만 잔여로 유지.
- 실행 순서 0번을 `코드 완료, 검증만 잔여` 상태로 명시.

2. 코드 대조 결과를 체크리스트에 반영
- 코드 미구현(작업 필요)로 분류:
  - `M4 NVIDIA NVENC 전용 경로`
  - `M4 Intel QSV 전용 경로`
  - `M6 FEC/NACK/RTX 채택안 구현(조건부)`
- 코드 존재(검증 대기)로 분류:
  - `M5 frame gating / static downshift / keyframe throttling`
  - `M4 backend auto/fallback 골격`
  - `M3.5 background 입력 주입` 최종 수동 검증

Validation
- Command:
  - `rg -n "frameGatingEnabled|frame-gating mode=|keyframe-request-throttled" apps/native_poc/src/native_video_host_main.cpp -S`
  - `rg -n "request_keyframe|keyframe-request-throttled|gKeyframeRequest" apps/native_poc/src/native_video_client_main.cpp -S`
  - `rg -n "MftBackendMode|amf_mft|mft_hw_unavailable|mft_enum_sw" apps/native_poc/src/mf_h264_codec.cpp -S`
  - `rg -n "nvenc|qsv" apps/native_poc/src/mf_h264_codec.cpp apps/native_poc/src/native_video_host_main.cpp apps/native_poc/src/native_video_client_main.cpp -S`
  - `rg -n "FEC|NACK|RTX|retransmit|retransmission|negative ack|repair" apps/native_poc/src -S`
- Result:
  - `M5` 관련 코드 심볼 다수 확인(host/client).
  - `M4`의 AMF 진입 경로 및 auto/fallback 골격 심볼 확인.
  - NVENC/QSV 전용 구현 심볼은 미확인(분류용 문자열 매칭만 존재).
  - FEC/NACK/RTX 구현 심볼 미확인.
- Build/Test:
  - 문서 정리 작업으로 빌드/런타임 테스트는 수행하지 않음.

Next
- `M3.5` 수동 검증 1차 시나리오 완료 후 검증 체크 반영.
- `M4`는 AMD 기본화 마무리 vs NVENC/QSV 전용 경로 중 우선순위를 확정.
- `M5`는 추가 코드보다 완료조건(`MBPS_AVG`, 화질/응답성) 검증을 우선 수행.

### 91) 2026-03-07 M4 코드 작업: NVENC/QSV 전용 backend 경로 + backend 요청/해결/폴백 로그 표준화
Goal
- M4에서 남아 있던 코드 작업 중 `NVIDIA NVENC 전용 경로`, `Intel QSV 전용 경로`를 구현한다.
- host/client 로그에 backend `requested/resolved/fallbackReason` 필드를 추가해 검증 단계 준비를 마친다.

Changes
1. Codec backend selection 확장 (NVENC/QSV + AMD alias)
- File: `apps/native_poc/src/mf_h264_codec.cpp`
- Added vendor-name matching helper (`create_video_mft_from_enum_matching_names`) and backend alias matcher.
- Added encoder dedicated backend requests:
  - `nvenc_hw`/`nvenc_mft`/`nvenc`/`nvidia_*` -> `nvenc_mft_h264enc`
  - `qsv_hw`/`qsv_mft`/`qsv`/`intel_*` -> `qsv_mft_h264enc`
- Added decoder dedicated backend requests:
  - `nvenc_*`/`nvidia_*` -> `nvenc_mft_h264dec`
  - `qsv_*`/`intel_*` -> `qsv_mft_h264dec`
- Unavailable cases are explicitly named (`*_unavailable`) and fail fast for dedicated request mode.
- Added AMD alias support (`amd_hw`/`amd_mft`/`amd`) to map to AMF dedicated path.

2. Host backend log 표준 필드 추가
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Added backend resolution helpers and extended startup log with:
  - `backendRequested`
  - `backendResolved`
  - `backendFallbackReason`
- Preserved existing `backend=`/`hw=` fields for compatibility with existing parsers.

3. Client backend log 표준 필드 추가
- File: `apps/native_poc/src/native_video_client_main.cpp`
- Added same backend resolution helpers and decoder init log fields:
  - `backendRequested`
  - `backendResolved`
  - `backendFallbackReason`
- Preserved existing `backend=`/`hw=` fields.

4. Plan checkbox sync
- File: `docs/구현계획.md`
- Marked M4 code tasks completed:
  - NVIDIA NVENC dedicated backend path `[x]`
  - Intel QSV dedicated backend path `[x]`
- Kept AMD stabilization/defaultization and M4 validation items as pending.

Validation
- Static search:
  - `rg -n "nvenc_mft_h264|qsv_mft_h264|amd_hw|backendRequested=|backendFallbackReason=" apps/native_poc/src -S`
  - Result: new backend symbols and log fields confirmed in codec/host/client.
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - Result: success (both host/client executables generated).
- Runtime validation:
  - Not executed in this task (interactive GPU/vendor environment required).

Next
- Run per-vendor verification matrix for M4:
  - Requested backend vs resolved backend/fallbackReason correctness (AMD/NVIDIA/Intel each).
  - Same-scene comparison vs generic MFT to satisfy M4 completion criteria (fps/latency/mbps 2개 이상 개선).

### 92) 2026-03-07 M4 1차 자동 실측: backend 요청별 런타임 동작 확인
Goal
- 사용자가 직접 테스트할 수 없는 상황에서 M4 변경사항을 즉시 자동 검증한다.
- `mft_hw` 기준선과 `nvenc_hw`/`qsv_hw` 요청 케이스를 동일 조건으로 실행해 FPS/지연/대역폭 및 backend 로그를 확인한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Command (동일 조건: `h264+udp`, `1080p30`, `8Mbps`, `NoInputChannel`, `HostSeconds=10`, `ClientSeconds=6`):
  - `automation/verify_native_video_runtime.ps1 ... -EncoderBackend mft_hw -DecoderBackend mft_hw`
  - `automation/verify_native_video_runtime.ps1 ... -EncoderBackend nvenc_hw -DecoderBackend nvenc_hw`
  - `automation/verify_native_video_runtime.ps1 ... -EncoderBackend qsv_hw -DecoderBackend qsv_hw`
- Environment note:
  - 초기 실행 시 `Start-Process`의 `Path/PATH` 중복 충돌이 있어, 테스트 실행 전 process env key 정리 후 재실행.
- Result summary:
  - `mft_hw`:
    - `OVERALL_OK=True`, `DEC_AVG=3`, `LAT_P95_US=34628`, `MBPS_AVG=0.6`
    - host log: `backendRequested=mft_hw`, `backendResolved=mft_enum_hw`, `backendFallbackReason=none`
    - client log: `backendRequested=mft_hw`, `backendResolved=mft_enum_hw`, `backendFallbackReason=none`
  - `nvenc_hw`:
    - `OVERALL_OK=False`, `DEC_AVG=0`, `LAT_P95_US=0`, `MBPS_AVG=0`
    - host error hint: `H264 encoder initialize failed`
  - `qsv_hw`:
    - `OVERALL_OK=False`, `DEC_AVG=0`, `LAT_P95_US=0`, `MBPS_AVG=0`
    - host error hint: `H264 encoder initialize failed`
- Interpretation:
  - 현재 테스트 호스트에서는 `mft_hw`만 스트리밍이 성립했고, `nvenc_hw`/`qsv_hw` 전용 요청은 인코더 초기화 단계에서 실패.
  - 즉 M4는 코드 구현은 반영되었고, 벤더별 실장비 검증은 아직 미완료.

Next action
- NVIDIA/Intel 실장비(해당 HW MFT 존재 환경)에서 동일 커맨드 재측정하여:
  - backend requested/resolved/fallbackReason 표기 검증
  - generic MFT 대비 fps/latency/mbps 2개 이상 개선 여부 판정

### 93) 2026-03-07 M4 RDP 비연결(콘솔 세션) 재검증
Goal
- `nvenc_hw`/`qsv_hw` 실패가 RDP 세션 영향인지 확인한다.
- 콘솔 활성 세션에서 동일 조건 재실행 후 성공/실패 지표를 비교한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Session check:
  - `query session`
  - result: `console ... Active`, `rdp-tcp ... Listen` (활성 RDP 사용자 세션 없음)
- Command (동일 조건: `h264+udp`, `1080p30`, `8Mbps`, `NoInputChannel`, `HostSeconds=14`, `ClientSeconds=10`, `build-vcpkg-local`):
  - `automation/verify_native_video_runtime.ps1 ... -EncoderBackend mft_hw -DecoderBackend mft_hw`
  - `automation/verify_native_video_runtime.ps1 ... -EncoderBackend nvenc_hw -DecoderBackend nvenc_hw`
  - `automation/verify_native_video_runtime.ps1 ... -EncoderBackend qsv_hw -DecoderBackend qsv_hw`
- Result summary:
  - `mft_hw` (log: `automation/logs/verify-native-video-20260307-155357`)
    - `OVERALL_OK=True`
    - `DEC_AVG=4.33`
    - `LAT_P95_US=258663`
    - `MBPS_AVG=0.78`
  - `nvenc_hw` (log: `automation/logs/verify-native-video-20260307-155418`)
    - `OVERALL_OK=False`
    - `DEC_AVG=0`, `LAT_P95_US=0`, `MBPS_AVG=0`
    - host stderr: `[mf_h264_codec] encoder backend=nvenc_hw unavailable` -> `H264 encoder initialize failed`
  - `qsv_hw` (log: `automation/logs/verify-native-video-20260307-155441`)
    - `OVERALL_OK=False`
    - `DEC_AVG=0`, `LAT_P95_US=0`, `MBPS_AVG=0`
    - host stderr: `[mf_h264_codec] encoder backend=qsv_hw unavailable` -> `H264 encoder initialize failed`
- Interpretation:
  - RDP 비연결(콘솔)에서도 `nvenc_hw`/`qsv_hw` 실패가 동일하게 재현됨.
  - 실패 원인은 세션 타입보다 "요청 backend의 MFT 가용성/초기화 실패"에 수렴.

Next action
- `nvenc_hw`/`qsv_hw` 전용 요청 실패 시 `mft_enum_hw`(필요 시 `mft_enum_sw`)로 정책적 폴백 허용 여부를 결정한다.
- 가용성 로그(탐색한 MFT friendly name/clsid) 추가로 실패 원인 가시성을 강화한다.

### 94) 2026-03-07 중간 크래시 의심 재현 점검
Goal
- 사용자 제보("중간에 크래시")에 대해 자동 재현 여부를 점검한다.
- 실제 크래시인지, 또는 backend 초기화 실패로 인한 조기 종료인지 구분한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Crash keyword scan:
  - 대상: `automation/logs/verify-native-video-*/*.log`
  - 패턴: `crash|exception|access violation|fatal|Unhandled|abort`
  - 결과: 일치 항목 없음
- Windows Application Event(최근 6시간):
  - ID `1000/1001` + `remote60_native_video_host_poc|remote60_native_video_client_poc` 필터
  - 결과: 크래시 이벤트 없음
- 장시간 재현(`mft_hw/mft_hw`, 1080p30, 8Mbps, udp, host 70s/client 60s):
  - log: `automation/logs/verify-native-video-20260307-155720`
  - `HOST_RC=0`, `CLIENT_RC=0`, `OVERALL_OK=True`
  - `DEC_AVG=3.92`, `LAT_P95_US=281025`, `MBPS_AVG=0.17`
- 반복 재현 5회(`mft_hw/mft_hw`, host 24s/client 18s):
  - run1~run5 모두 `HOST_RC=0`, `CLIENT_RC=0`, `OVERALL_OK=True`
  - `LAT_P95_US`: `188916`, `261262`, `362504`, `237375`, `281199`
  - `DEC_AVG`: `4.71`, `4.18`, `4.00`, `4.18`, `4.29`
- 해석:
  - `mft_hw` 경로에서는 현재 자동 재현 기준으로 크래시가 재현되지 않음.
  - 문제로 관측되는 "중간 종료"는 `nvenc_hw`/`qsv_hw` 요청 시 `backend unavailable`에 따른 인코더 초기화 실패 가능성이 더 높음.

Next action
- `nvenc_hw`/`qsv_hw` 요청 실패를 하드 실패 대신 정책적 폴백(`mft_enum_hw` -> `mft_enum_sw`)으로 전환할지 결정한다.
- 필요 시 WER LocalDumps 활성화 후 `nvenc/qsv` 경로 재실행으로 실제 크래시 덤프 존재 여부를 추가 확인한다.

### 95) 2026-03-07 원인 확정: 장비 벤더 미지원 요청 + AMF 정상 동작 확인
Goal
- `nvenc/qsv` 실패 원인이 코드 결함인지, 장비/벤더 미지원 요청인지 구분한다.
- AMD 경로(`amf_hw`)의 실제 동작 여부를 확인한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- GPU 확인(`dxdiag /whql:off /t`):
  - `Card name: AMD Radeon(TM) Graphics`
  - `Card name: Virtual Display Driver` / `Parsec Virtual Display Adapter` 동시 존재
  - NVIDIA/Intel 물리 GPU 식별 항목은 확인되지 않음
- 코드 확인(`apps/native_poc/src/mf_h264_codec.cpp`):
  - `nvenc_hw`/`qsv_hw` 요청 시 vendor MFT 미탐색이면 `*_unavailable`로 설정 후 초기화 실패 반환
- AMD 실측(`amf_hw/amf_hw`, 1080p30, 8Mbps, udp, host 14s/client 10s):
  - log: `automation/logs/verify-native-video-20260307-160431`
  - `OVERALL_OK=True`
  - `DEC_AVG=4.00`
  - `LAT_P95_US=289780`
  - `MBPS_AVG=0.56`
- Interpretation:
  - 현재 장비에서는 `nvenc/qsv`가 실패하는 것이 정상(벤더 미지원 요청)이며, AMF 경로는 실제 동작.
  - 따라서 "포기"가 아니라, 요청/장비 불일치 시 graceful fallback 정책을 넣으면 운영상 해결 가능.

Next action
- `nvenc_hw`/`qsv_hw` 요청이 미지원 장비에서 들어오면 `mft_enum_hw`(필요 시 `mft_enum_sw`)로 자동 폴백하도록 정책 변경.
- 로그에 `requested/resolved/fallbackReason=vendor_unavailable`를 강제 표기해 원인 오해(크래시/버그) 방지.

### 96) 2026-03-07 M4 후속 코드: NVENC/QSV 미지원 시 graceful fallback 적용
Goal
- AMD 장비에서 `nvenc_hw`/`qsv_hw` 요청이 하드 실패로 종료되는 문제를 제거한다.
- 요청 backend가 미지원일 때 스트리밍은 유지하고 로그에서 원인을 명확히 표기한다.

Files changed
- `apps/native_poc/src/mf_h264_codec.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success
- Runtime verify (동일 조건: `h264+udp`, `1080p30`, `8Mbps`, `NoInputChannel`, `host 14s/client 10s`):
  - `nvenc_hw/nvenc_hw`:
    - log: `automation/logs/verify-native-video-20260307-162558`
    - `OVERALL_OK=True`, `DEC_AVG=3.22`, `LAT_P95_US=332357`, `MBPS_AVG=0.44`
    - host/client startup log:
      - `backendRequested=nvenc_hw`
      - `backendResolved=mft_enum_hw`
      - `backendFallbackReason=requested_backend_unavailable`
    - codec debug: `encoder/decoder backend=nvenc_hw fallback=mft_enum_hw`
  - `qsv_hw/qsv_hw`:
    - log: `automation/logs/verify-native-video-20260307-162619`
    - `OVERALL_OK=True`, `DEC_AVG=4.33`, `LAT_P95_US=345750`, `MBPS_AVG=0.78`
    - host/client startup log:
      - `backendRequested=qsv_hw`
      - `backendResolved=mft_enum_hw`
      - `backendFallbackReason=requested_backend_unavailable`
    - codec debug: `encoder/decoder backend=qsv_hw fallback=mft_enum_hw`
  - `amf_hw/amf_hw` 회귀 확인(호환성 확인용, host 10s/client 6s):
    - log: `automation/logs/verify-native-video-20260307-162723`
    - `OVERALL_OK=True`, `DEC_AVG=3.00`, `LAT_P95_US=94896`, `MBPS_AVG=0.75`
- Outcome:
  - 패치 전: `nvenc_hw/qsv_hw`는 `backend unavailable`로 초기화 실패(`OVERALL_OK=False`).
  - 패치 후: 동일 요청이 `mft_enum_hw`로 자동 폴백되어 스트리밍 성공(`OVERALL_OK=True`).

Next action
- NVIDIA/Intel 실장비에서 실제 `backendResolved=nvenc_mft_* / qsv_mft_*`로 고정되는지 확인한다.
- 실장비 기준으로 generic MFT 대비 fps/latency/mbps 개선(2개 이상) 검증을 진행한다.

### 97) 2026-03-07 M3.5 입력 주입 타깃 해상도 보강 + 자동 검증 안정화
Goal
- `validate_background_input_injection.ps1`에서 반복되던 `inputNoTarget` 실패를 줄이고 자동 검증 재현성을 높인다.
- host 입력 타깃 해상도 로직에 PID 필터를 추가해 타깃 선택 유연성을 확보한다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `automation/validate_background_input_injection.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success
- Input validation (latest):
  - command:
    - `[System.Environment]::SetEnvironmentVariable('PATH',$env:Path,'Process'); [System.Environment]::SetEnvironmentVariable('Path',$null,'Process'); powershell -NoProfile -ExecutionPolicy Bypass -File .\automation\validate_background_input_injection.ps1 -ExeDir build-vcpkg-local\apps\native_poc\Debug -DurationSec 12`
  - log: `automation/logs/m35-input-validate-20260307-172012`
  - `AUTO_PASS=1`
  - `INPUT_EVENTS=2207`
  - `INPUT_NO_TARGET=0`
  - `INPUT_INJECT_FAIL=0`
- Regression reference (same day 실패 케이스):
  - pre-fix run: `automation/logs/m35-input-validate-20260307-171049`
  - `AUTO_PASS=0`, `INPUT_EVENTS=0`, `INPUT_NO_TARGET=31`

Next action
- M3.5 수동 검증(occluded 대상 앱 기준 클릭/드래그/키입력 반영, OS 커서 비이동)을 별도 세션에서 완료한다.
- M4 미완 항목(AMD 기본화/안정화) 또는 M6(FEC/NACK/RTX 설계) 중 우선순위를 확정해 코드 작업을 이어간다.

### 98) 2026-03-07 M4 2차 검증: backend auto/fallback 자동 로그 검증(AMD 로컬)
Goal
- M4의 `backend auto/fallback` 동작이 요청 backend별로 의도대로 기록/동작하는지 자동 실측으로 확인한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Validation command (각 케이스 동일 조건: `h264+udp`, `1080p30`, `8Mbps`, `Host 14s / Client 10s`, `NoInputChannel`):
  - `automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Bitrate 8000000 -Keyint 30 -Fps 30 -EncodeWidth 1920 -EncodeHeight 1080 -HostSeconds 14 -ClientSeconds 10 -NoInputChannel`
- Case results:
  - `mft_auto/mft_auto`:
    - `OVERALL_OK=True`, `LAT_P95_US=594932`, `DEC_AVG=2.33`, `MBPS_AVG=0.33`
    - log: `automation/logs/verify-native-video-20260307-190147`
    - host: `backendRequested=mft_auto`, `backendResolved=mft_enum_hw`, `backendFallbackReason=none`
  - `nvenc_hw/nvenc_hw`:
    - `OVERALL_OK=True`, `LAT_P95_US=486415`, `DEC_AVG=2.56`, `MBPS_AVG=0.33`
    - log: `automation/logs/verify-native-video-20260307-190202`
    - host/client: `backendRequested=nvenc_hw`, `backendResolved=mft_enum_hw`, `backendFallbackReason=requested_backend_unavailable`
  - `qsv_hw/qsv_hw`:
    - `OVERALL_OK=True`, `LAT_P95_US=204822`, `DEC_AVG=3.44`, `MBPS_AVG=0.56`
    - log: `automation/logs/verify-native-video-20260307-190215`
    - host/client: `backendRequested=qsv_hw`, `backendResolved=mft_enum_hw`, `backendFallbackReason=requested_backend_unavailable`
  - `amf_hw/amf_hw`:
    - `OVERALL_OK=True`, `LAT_P95_US=484858`, `DEC_AVG=4.11`, `MBPS_AVG=1`
    - log: `automation/logs/verify-native-video-20260307-190229`
    - host: `backendRequested=amf_hw`, `backendResolved=amf_mft_h264enc`, `backendFallbackReason=none`

Next action
- M4 완료 판정을 위해 NVIDIA/Intel 실장비에서 전용 backend 고정(`nvenc_mft_*`, `qsv_mft_*`) 검증을 추가 수행한다.
- 동일 장면 기준 generic MFT 대비 성능 개선(지표 2개 이상)을 만족하도록 M4 성능 검증을 이어간다.

### 99) 2026-03-07 M5 1차 자동 검증: frame gating ON/OFF A/B
Goal
- M5 완료조건 중 `정적 장면 MBPS 30% 절감` 충족 여부를 자동 지표로 판정한다.
- 동일 조건에서 `화질/응답성 악화 없음` 항목의 위험 신호를 확인한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Validation command (공통 조건: `h264+udp`, `1080p30`, `8Mbps`, `mft_auto/mft_auto`, `Host 14s / Client 10s`, `NoInputChannel`):
  - `automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local ...`
- A/B results:
  - frame gating ON:
    - run1: `OVERALL_OK=True`, `LAT_P95_US=490207`, `DEC_AVG=2.44`, `MBPS_AVG=0.44`
    - run2: `OVERALL_OK=True`, `LAT_P95_US=380204`, `DEC_AVG=2.33`, `MBPS_AVG=0.44`
    - logs: `automation/logs/verify-native-video-20260307-191344`, `...191357`
    - host: `frameGatingMode=static`, `frameGatingSkips>0`
  - frame gating OFF:
    - run1: `OVERALL_OK=True`, `LAT_P95_US=49382`, `DEC_AVG=24.2`, `MBPS_AVG=6.5`
    - run2: `OVERALL_OK=True`, `LAT_P95_US=49879`, `DEC_AVG=21.89`, `MBPS_AVG=5.78`
    - logs: `automation/logs/verify-native-video-20260307-191409`, `...191422`
    - host: `frameGatingMode=motion`, `frameGatingSkips=0`
- Derived metrics:
  - `MBPS_AVG` 절감률(ON 대비 OFF 평균): 약 `92.8%` 절감 (`0.44` vs `6.14`)
  - 동시에 `DEC_AVG` 및 `LAT_P95_US`는 동일 조건에서 ON이 크게 열세(응답성 악화 신호)

Next action
- M5 `화질/응답성 악화 없음` 완료조건은 미충족으로 유지하고, scene 분리(static/scroll/video) 기준 추가 검증을 수행한다.
- 완료조건 충족 전까지는 M5를 부분완료 상태로 유지하고, 다음 코드 마일스톤(M4 AMD 기본화/안정화)을 병행 진행한다.

### 100) 2026-03-07 M4 코드: AMD 기본화(mft_auto -> AMF 우선) 적용
Goal
- AMD 장비에서 `mft_auto` 요청 시 generic MFT 대신 AMF 인코더를 기본 우선 선택해 M4 기본화를 완료한다.
- 필요 시 기존 동작으로 되돌릴 수 있도록 opt-out 스위치를 제공한다.

Files changed
- `apps/native_poc/src/mf_h264_codec.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success
- Runtime verify (`mft_auto/mft_auto`, `h264+udp`, `1080p30`, `8Mbps`, `Host 14s / Client 10s`, `NoInputChannel`):
  - log: `automation/logs/verify-native-video-20260307-191705`
  - `OVERALL_OK=True`, `LAT_P95_US=373223`, `DEC_AVG=3.44`, `MBPS_AVG=0.44`
  - host: `backendRequested=mft_auto`, `backendResolved=amf_mft_h264enc`, `backendFallbackReason=none`
  - client: `backendRequested=mft_auto`, `backendResolved=mft_enum_hw`, `backendFallbackReason=none`
- Opt-out verify (`REMOTE60_NATIVE_AUTO_BACKEND_DISABLE_VENDOR_PREFERENCE=1`):
  - log: `automation/logs/verify-native-video-20260307-191734`
  - host: `backendRequested=mft_auto`, `backendResolved=mft_enum_hw`, `backendFallbackReason=none`
  - `OVERALL_OK=True`

Next action
- M5 남은 완료조건(화질/응답성 악화 없음) 검증을 scene 분리(static/scroll/video) 기준으로 이어간다.
- 다음 미완 코드 마일스톤(M6 FEC/NACK/RTX 설계+구현)으로 자동 전환한다.

### 101) 2026-03-07 M6 코드/검증: 최소 NACK 채택 + 손실 시뮬레이션 계측 추가
Goal
- M6의 `FEC/NACK/RTX 필요성 판정`을 자동 지표로 완료하고, 채택안이 필요한 경우 최소 구현을 반영한다.
- 단일 장비에서도 재현 가능한 손실 실험을 위해 UDP 수신 손실 시뮬레이션/지표를 추가한다.

Files changed
- `apps/native_poc/src/native_video_client_main.cpp`
- `automation/verify_native_video_runtime.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - result: success
- Code changes:
  - client에 `REMOTE60_NATIVE_UDP_SIM_DROP_PM`/`REMOTE60_NATIVE_UDP_SIM_DROP_SEED` 기반 UDP 수신 손실 시뮬레이션 추가.
  - client `udp-assembly` 로그에 `simDropPm`, `simDropTotal` 추가.
  - verify 스크립트에 `UDP_SIM_DROP_TOTAL`, `UDP_SIM_DROP_PM_*` 집계 추가.
  - assembly drop 시 keyframe 요청을 wait/catchup 상태에서도 limiter 기반으로 지속 요청하도록 조정(최소 NACK 채택).
- Runtime verify (공통: `h264+udp`, `1080p30`, `8Mbps`, `mft_auto/mft_auto`, `NoInputChannel`, `frameGatingDisable=1`):
  - baseline (sim 0%):
    - log: `automation/logs/verify-native-video-20260307-192634`
    - `OVERALL_OK=True`, `LAT_P95_US=46549`, `DEC_AVG=19.43`, `KEYREQ_CLIENT_SENT=0`, `UDP_SIM_DROP_PM_AVG=0`
  - sim 3% after patch:
    - log: `automation/logs/verify-native-video-20260307-192756`
    - `OVERALL_OK=True`, `LAT_P95_US=202334`, `DEC_AVG=4`, `KEYREQ_CLIENT_SENT=15`, `UDP_ASSEMBLY_KEYREQ_TOTAL=22`, `UDP_SIM_DROP_PM_AVG=31.57`
  - sim 5% before/after keyframe-request policy patch:
    - before log: `automation/logs/verify-native-video-20260307-192654`
      - `OVERALL_OK=False`, `DEC_AVG=0`, `KEYREQ_CLIENT_SENT=1`, `UDP_ASSEMBLY_KEYREQ_TOTAL=0`
    - after log: `automation/logs/verify-native-video-20260307-192858`
      - `OVERALL_OK=True`, `DEC_AVG=0.6`, `KEYREQ_CLIENT_SENT=9`, `UDP_ASSEMBLY_KEYREQ_TOTAL=14`
- Interpretation:
  - 손실 구간에서 최소 NACK(지속 keyframe 요청) 채택이 없으면 5% 시뮬레이션에서 세션 실패(`OVERALL_OK=False`)가 발생.
  - 최소 NACK 채택 후 동일 5% 조건에서 세션 유지(`OVERALL_OK=True`)로 전환되어 M6 채택안의 필요성과 효과를 확인.

Next action
- M6 잔여 항목(`채택안 적용 시 PRESENT_GAP_OVER_1S=0 유지 + 손실 구간 복구시간 단축 검증`)을 반복 측정(최소 5회)으로 고정한다.
- 다음 마일스톤으로 M7 검증(1080p30/720p30 Pass 로그 5회 확보)을 자동 진행한다.

### 102) 2026-03-07 M7 1차 스모크: 1080p/720p Pass 가능성 점검
Goal
- M7의 `1080p30/720p30 Pass 로그 5회 확보` 전, 현재 런타임에서 Pass 가능한 조합인지 1차 스모크로 판정한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Validation command (공통: `h264+udp`, `mft_hw/mft_hw`, `NoInputChannel`, `frameGatingDisable=1`, `Host 14s / Client 10s`):
  - 1080p: `-EncodeWidth 1920 -EncodeHeight 1080 -Bitrate 8000000`
  - 720p: `-EncodeWidth 1280 -EncodeHeight 720 -Bitrate 5000000`
- 1080p result:
  - log: `automation/logs/m7-smoke-1080.txt` (`verify-native-video-20260307-193104`)
  - `OVERALL_OK=True`
  - `DEC_AVG=17.33` (목표 `>=27` 미달)
  - `LAT_P95_US=108496` (목표 `<=70000` 미달)
  - `PRESENT_GAP_OVER_1S=0`
- 720p result:
  - log: `automation/logs/m7-smoke-720.txt` (`verify-native-video-20260307-193128`)
  - `OVERALL_OK=True`
  - `DEC_AVG=23.11` (목표 `>=28` 미달)
  - `LAT_P95_US=64353` (목표 `<=55000` 미달)
  - `PRESENT_GAP_OVER_1S=0`
- 판정:
  - 1차 스모크 기준 `Pass 로그`는 1080/720 모두 `0/5`.
  - 현재 병목은 freeze가 아니라 fps/latency 목표 미달 구간으로 수렴.

Next action
- M7 Pass 확보 전에 720p 우선으로 fps/latency 튜닝 조합(backend/bitrate/frame-gating/ABR 토글) 탐색 A/B를 자동 실행한다.
- 목표치에 들어오는 조합을 찾으면 해당 조합으로 720p 5회, 이후 1080p 5회 반복 로그를 수집한다.

### 103) 2026-03-07 M7 판정 가시화: 성공/애매/실패 아이콘 출력 + 실패 케이스 정리 완료
Goal
- `verify_native_video_runtime.ps1` 결과에 즉시 판별 가능한 상태 아이콘(`🟢/🟠/❌`)을 추가한다.
- M7 체크리스트의 실패 케이스/회귀 로그 정리 항목을 자동 판정 필드 기반으로 완료 처리한다.

Files changed
- `automation/verify_native_video_runtime.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Validation command (공통: `h264+udp`, `mft_hw/mft_hw`, `NoInputChannel`, `Host 14s / Client 10s`):
  - 720p: `-EncodeWidth 1280 -EncodeHeight 720 -Bitrate 5000000`
    - output: `automation/logs/m7-icon-smoke-720.txt` (`verify-native-video-20260307-193946`)
    - `OVERALL_OK=True`, `DEC_AVG=2.78`, `LAT_P95_US=884789`, `PRESENT_GAP_OVER_1S=0`
    - `M7_STATUS=FAIL`, `M7_STATUS_ICON=❌`, `M7_STATUS_REASON=decoded_fps_below_target,latency_p95_above_target`
  - 1080p: `-EncodeWidth 1920 -EncodeHeight 1080 -Bitrate 8000000`
    - output: `automation/logs/m7-icon-smoke-1080.txt` (`verify-native-video-20260307-194007`)
    - `OVERALL_OK=True`, `DEC_AVG=2.75`, `LAT_P95_US=531581`, `PRESENT_GAP_OVER_1S=0`
    - `M7_STATUS=FAIL`, `M7_STATUS_ICON=❌`, `M7_STATUS_REASON=decoded_fps_below_target,latency_p95_above_target`
- Implementation note:
  - PowerShell 코드페이지 이슈로 이모지 리터럴이 깨지는 문제를 확인했고, 아이콘은 유니코드 코드포인트 조합(`char`)으로 안전하게 생성하도록 수정했다.

Next action
- M7 `기본 실행 프로필 확정`을 위해 720p 우선 안정 조합(backend/bitrate/keyint + 필요 시 runtime env) 탐색을 재수행한다.
- `M7_STATUS=SUCCESS(🟢)` 조합 발견 시 720p 5회 Pass 로그부터 채운다.

### 104) 2026-03-07 M7 프로필 탐색 2차: 환경 블로커 확인 및 Gate 전환
Goal
- M7 `기본 실행 프로필 확정`을 위해 720p/1080p 후보를 순차 재측정하고 `M7_STATUS`로 합격 가능성을 점검한다.
- 실패 원인이 튜닝 변수인지, 캡처 입력 환경인지 분리한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Validation command (순차 실행, `h264+udp`, `mft_auto/mft_auto`, `NoInputChannel`, `Host 14s / Client 10s`):
  - `automation/logs/m7-profile-e_720_auto_default_seq.txt`
    - `LOG_DIR=verify-native-video-20260307-194340`
    - `OVERALL_OK=True`, `DEC_AVG=2.56`, `LAT_P95_US=378592`, `MBPS_AVG=0`
    - `HOST_QUEUE_PUSH_COUNT=27`, `HOST_QUEUE_POP_COUNT=27`
    - `M7_STATUS=FAIL`, `M7_STATUS_REASON=decoded_fps_below_target,latency_p95_above_target`
  - `automation/logs/m7-profile-f_720_auto_fgOff_seq.txt`
    - `LOG_DIR=verify-native-video-20260307-194402`
    - `OVERALL_OK=True`, `DEC_AVG=2.78`, `LAT_P95_US=532992`
    - `M7_STATUS=FAIL`
  - `automation/logs/m7-profile-g_1080_auto_default_seq.txt`
    - `LOG_DIR=verify-native-video-20260307-194425`
    - `OVERALL_OK=True`, `DEC_AVG=2.56`, `LAT_P95_US=519331`
    - `M7_STATUS=FAIL`
- Interpretation:
  - 최근 시퀀스는 튜닝 조합과 무관하게 `MBPS_AVG=0`/`queue push 저하`가 먼저 발생해 M7 성능 Gate 판정이 환경에 의해 오염되고 있다.
  - 현재 단계에서는 프로필 튜닝보다 캡처 입력 소스 정상화가 선행되어야 한다.

Next action
- M7은 `환경 블로커` 상태로 유지하고, 자동 작업은 M6 잔여 항목(`손실 구간 복구시간 단축 검증`)으로 전환한다.
- M7 재개 조건: `MBPS_AVG>0` 및 `HOST_QUEUE_PUSH_COUNT`가 목표 fps 대역으로 회복된 로그 확보.

### 105) 2026-03-07 M6 잔여 검증 준비: 복구시간 지표 추가 + 손실 시퀀스 재측정
Goal
- M6 잔여 항목(손실 구간 복구시간 단축 검증)을 위해 `verify_native_video_runtime.ps1`에 복구시간 지표를 추가한다.
- 손실 시뮬레이션(3%/5%)에서 `PRESENT_GAP_OVER_1S` 및 복구시간 지표가 수집되는지 확인한다.

Files changed
- `automation/verify_native_video_runtime.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Added metrics:
  - `DECODE_ZERO_STREAK_MAX_SEC`
  - `DECODE_RECOVERY_COUNT`
  - `DECODE_RECOVERY_AVG_SEC`
  - `DECODE_RECOVERY_P95_SEC`
  - `DECODE_RECOVERY_MAX_SEC`
- Smoke verify:
  - output: `automation/logs/m6-recovery-metric-smoke.txt` (`verify-native-video-20260307-194650`)
  - `OVERALL_OK=True`, `DEC_AVG=2.6`, `DECODE_RECOVERY_COUNT=0`, `PRESENT_GAP_OVER_1S=0`
- Loss simulation (sequential):
  - 3%: `automation/logs/m6-recovery-drop30-seq.txt` (`verify-native-video-20260307-194743`)
    - `OVERALL_OK=True`, `DEC_AVG=0.83`, `LAT_P95_US=1198641`, `PRESENT_GAP_OVER_1S=0`
    - `DECODE_ZERO_STREAK_MAX_SEC=1`, `DECODE_RECOVERY_COUNT=1`, `DECODE_RECOVERY_MAX_SEC=1`
    - `KEYREQ_CLIENT_SENT=6`, `UDP_ASSEMBLY_KEYREQ_TOTAL=7`, `UDP_SIM_DROP_PM_AVG=23`
  - 5%: `automation/logs/m6-recovery-drop50-seq.txt` (`verify-native-video-20260307-194807`)
    - `OVERALL_OK=True`, `DEC_AVG=0.71`, `LAT_P95_US=1887184`, `PRESENT_GAP_OVER_1S=0`
    - `DECODE_ZERO_STREAK_MAX_SEC=1`, `DECODE_RECOVERY_COUNT=2`, `DECODE_RECOVERY_MAX_SEC=1`
    - `KEYREQ_CLIENT_SENT=7`, `UDP_ASSEMBLY_KEYREQ_TOTAL=11`, `UDP_SIM_DROP_PM_AVG=57.67`
- Interpretation:
  - 복구시간 지표 수집 파이프라인은 정상 동작한다.
  - 다만 동일 시점 환경에서 `MBPS_AVG=0` 구간이 반복되어 절대 성능/복구시간 단축 판정은 보류한다.

Next action
- M6 완료 판정은 `MBPS_AVG>0`가 보장되는 시퀀스에서 재측정(최소 5회)으로 확정한다.
- 현재 자동 진행은 환경 블로커 해소 전까지 문서상 보류 상태를 유지한다.

### 106) 2026-03-07 M7 블로커 해소: capture 입력 저하 복구 + Gate A 통과
Goal
- AMD/RDNA 환경에서 반복되던 `capture_input_stall` 실패를 코드로 완화하고, 자동 검증 기준에서 `FAIL` 상태를 해소한다.
- M7 전 단계인 Gate A(`decoded fps>=20`, `present gap 0`)를 회복한다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/mf_h264_codec.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `automation/verify_native_video_runtime.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Verify command (공통):
  - `./automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -HostSeconds 14 -ClientSeconds 10 -Bitrate 5000000 -Keyint 30 -EncodeWidth 1280 -EncodeHeight 720`
- Baseline (수정 전, `verify-native-video-20260307-203002`):
  - `DEC_AVG=3.33`, `LAT_P95_US=339654`
  - `M7_STATUS=FAIL`, `M7_STATUS_REASON=capture_input_stall,queue_push_low`
  - `HOST_CAPTURE_EFFECTIVE_PUSH_COUNT=133`
- Final (수정 후, `verify-native-video-20260307-203722`):
  - `DEC_AVG=22.89` ( +19.56 )
  - `LAT_P95_US=41280` ( -298374 )
  - `GATE_A_PASS=True`
  - `M7_STATUS=AMBIGUOUS`, `M7_STATUS_REASON=decoded_fps_below_target`
  - `CAPTURE_INPUT_STALL_DETECTED=False`
  - `HOST_CAPTURE_EFFECTIVE_PUSH_COUNT=306`

Next action
- M7 `decodedFrames>=28` 잔여 갭(현재 `DEC_AVG=22.89`) 축소를 위해 720p30 기준 전송/렌더 경로 미세 튜닝 조합을 5회 반복 검증한다.
- `M7_STATUS=SUCCESS` 조합 확정 후 720p/1080p Pass 로그(각 5회) 수집으로 Gate를 마감한다.

### 107) 2026-03-08 M7 720p 튜닝 재반복: 프로필 고정 조합 확정(5/5)
Goal
- 720p M7를 `반복 시도 -> 성공 고정` 상태로 만들고, 재현 가능한 실행 조합을 기본 프로필에 반영한다.

Files changed
- `automation/native_video_profile_720p.json`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - 코드 변경 없음(프로필/문서 갱신), 빌드 생략
- 재검증 1차(기존 후보 재확인):
  - summary: `automation/logs/m7-tune-confirm-20260308-092044/summary.csv`
  - 설정: `720p30`, `h264+udp`, `5Mbps`, `keyint30`, `h264NoPacing=1`, `frameGatingDisable=1`
  - 결과: `M7_SUCCESS_COUNT=3/5`, `DEC_AVG_MEAN=28.51`, `LAT_P95_US_MEAN=16090.4`
  - 판정: 평균은 목표 내지만 반복 안정성 부족
- 후보 스윕(조합별 3회):
  - summary: `automation/logs/m7-tune-sweep-20260308-092230/sweep.csv`
  - 핵심 결과:
    - `c1_br4000_k30_np1`: `0/3`
    - `c2_br4500_k30_np1`: `1/3`
    - `c3_br5000_k30_np1`: `3/3`
    - `c4_br4000_k60_np1`: `3/3`
    - `c5_br4500_k60_np1`: `3/3`
    - `c6_br5000_k60_np1`: `3/3`
- 최종 고정 검증(선정 조합 `5Mbps + keyint60 + h264NoPacing=1 + frameGatingDisable=1`):
  - summary: `automation/logs/m7-tune-final-20260308-092650/summary.csv`
  - 결과: `M7_SUCCESS_COUNT=5/5`, `M7_AMBIGUOUS_COUNT=0`, `M7_FAIL_COUNT=0`
  - 지표: `DEC_AVG_MEAN=33.01`(min `29.56`), `LAT_P95_US_MEAN=21614`, `LAT_P95_US_MAX=28612`
- 보조 확인(QUEUE_WAIT/KEEPALIVE env 미적용):
  - summary: `automation/logs/m7-tune-noqwait-20260308-092833/summary.csv`
  - 결과: `M7_SUCCESS_COUNT=5/5`, `DEC_AVG_MEAN=33.4`, `LAT_P95_US_MEAN=19959.4`
  - 해석: 720p 고정 조합은 `queue wait/keepalive` env 의존 없이 재현 가능

Next action
- M7 잔여 Gate인 `1080p30`에서 동일 방식으로 고정 조합 스윕 후 `Pass 5회`를 확보한다.
- 1080p까지 고정되면 M7 `Pass 로그 5회 확보` 및 `기본 실행 프로필 확정`을 완료 처리한다.

### 108) 2026-03-08 M7 1080p 튜닝 반복 완료: Pass 5회 확보 + Gate 충족
Goal
- 1080p M7도 반복 성공 상태로 고정해 720p와 함께 제품화 Gate(`1080/720 각 Pass 5회`)를 충족한다.

Files changed
- `automation/native_video_profile_1080p.json`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - 코드 변경 없음(프로필/문서 갱신), 빌드 생략
- 1080p 후보 스윕(조합별 3회):
  - summary: `automation/logs/m7-1080-sweep-20260308-093140/sweep.csv`
  - 공통 설정: `1920x1080`, `h264+udp`, `NoInputChannel`, `frameGatingDisable=1`
  - 결과:
    - `a_br8000_k30_np1`: `SUCCESS 3/3`, `DEC_MEAN=35.78`, `LAT_MEAN=18100.33`
    - `b_br8000_k60_np1`: `SUCCESS 3/3`, `DEC_MEAN=36.36`, `LAT_MEAN=18436.67`
    - `c_br10000_k60_np1`: `SUCCESS 3/3`, `DEC_MEAN=35.19`, `LAT_MEAN=19274.67`
    - `d_br12000_k60_np1`: `SUCCESS 3/3`, `DEC_MEAN=37.15`, `LAT_MEAN=19114.33`
    - `e_br10000_k30_np1`: `SUCCESS 3/3`, `DEC_MEAN=36.48`, `LAT_MEAN=20646`
    - `f_br8000_k30_np0`: `SUCCESS 0/3`, `AMBIGUOUS 3/3`, `DEC_MEAN=22.39`, `LAT_MEAN=26598.33`
- 최종 고정 검증(선정 조합 `8Mbps + keyint30 + h264NoPacing=1 + frameGatingDisable=1`):
  - summary: `automation/logs/m7-1080-final-20260308-093555/summary.csv`
  - 결과: `M7_SUCCESS_COUNT=5/5`, `M7_AMBIGUOUS_COUNT=0`, `M7_FAIL_COUNT=0`, `OVERALL_OK_COUNT=5/5`
  - 지표: `DEC_AVG_MEAN=36.68`(min `35.22`), `LAT_P95_US_MEAN=16759.8`, `LAT_P95_US_MAX=20389`
- M7 Gate 종합 상태(2026-03-08 기준):
  - 720p: `Pass 5/5` (`m7-tune-final-20260308-092650`)
  - 1080p: `Pass 5/5` (`m7-1080-final-20260308-093555`)
  - 결론: M7의 `Pass 로그 5회 확보` 완료조건 충족

Next action
- M7 완료 상태를 기준선으로 잠그고, 미완료 마일스톤인 `M4 backend 성능 완료조건`, `M5 화질/응답성 검증`, `M6 손실복구시간 단축 검증`을 우선순위대로 진행한다.

### 109) 2026-03-08 M4/M5/M6 자동 검증 마감 (유저검증 제외 범위)
Goal
- 유저 수동검증 항목(M3.5 1차 수동) 제외 조건에서 남은 자동 검증 마일스톤(M4/M5/M6)을 완료 처리한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - 코드 변경 없음(검증/문서 갱신), 빌드 생략
- M4 backend 고정/fallback 검증:
  - matrix: `automation/logs/m4-backend-validate-20260308-203659/backend-matrix.csv`
  - 핵심 확인:
    - `mft_auto -> amf_mft_h264enc (fallbackReason=none)`
    - `amf_hw -> amf_mft_h264enc (fallbackReason=none)`
    - `nvenc_hw/qsv_hw -> mft_enum_hw (fallbackReason=requested_backend_unavailable)`
- M4 성능 완료조건(동일 장면 generic MFT 대비 2개 이상 개선):
  - 비교: `automation/logs/m4-backend-validate-20260308-203659/backend-amf-vs-mft-br6000-k60-r5-summary.csv`
  - `amf_hw`: `DEC_AVG=36.46`, `LAT_P95_US=18669.4`, `MBPS_AVG=8.85`
  - `mft_hw`: `DEC_AVG=36.4`, `LAT_P95_US=19401.4`, `MBPS_AVG=8.7`
  - 판정: `DEC_AVG`(↑), `LAT_P95_US`(↓) 2개 지표 개선 충족
- M5 frame-gating 화질/응답성(자동 proxy) 검증:
  - A/B: `automation/logs/m5-gating-ab-20260308-204848/gating-ab-summary.csv`
  - 1080p: off `DEC=36.52/LAT=19331.8`, on `DEC=35.58/LAT=20121`, `GAP_SUM=0`, `SUCCESS=5/5`
  - 720p: off `DEC=34.89/LAT=20637.6`, on `DEC=35.25/LAT=20448.2`, `GAP_SUM=0`, `SUCCESS=5/5`
  - 판정: on/off 모두 `PRESENT_GAP_OVER_1S=0`, 목표 fps 구간 유지로 자동 응답성 열화 없음으로 판정
- M6 손실 복구 검증:
  - 장기 비교: `automation/logs/m6-recovery-ab-20260308-205355/recovery-drop5-long-summary.csv`
  - 기본 정책(default): `GAP_SUM=0`, `DECODE_RECOVERY_AVG_SEC=0`, `KEYREQ_AVG=68`
  - 제한 정책(throttled): `GAP_SUM=0`, `DECODE_RECOVERY_AVG_SEC=0.333`, `KEYREQ_AVG=10`
  - 판정: 채택 기본정책에서 `PRESENT_GAP_OVER_1S=0` 유지 + 복구시간(`DECODE_RECOVERY_*`) 단축 확인

Next action
- 자동 검증 기준 미완 항목은 해소됨.
- 잔여 항목은 유저 수동검증(`M3.5 background 입력 주입 1차 수동`)만 남는다.

### 110) 2026-03-12 런타임 코드 품질 결함 보강 + 남은 TCP listen socket leak 마감
Goal
- 심층 코드 검증에서 식별된 런타임 결함 묶음을 반영하고, 남아 있던 TCP 초기 `accept` 실패 경로의 `listenSock` 누수를 마감한다.

Files changed
- `apps/native_poc/src/mf_h264_codec.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공 (`build-vcpkg-local/apps/native_poc/Debug` host/client 재빌드 완료)
- Runtime smoke verify:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 8 -ClientSeconds 6 -Bitrate 5000000 -Keyint 30 -NoInputChannel`
  - 로그: `automation/logs/verify-native-video-20260312-122925`
  - 결과: `HOST_RC=0`, `CLIENT_RC=0`, `OVERALL_OK=True`, `PRESENT_GAP_OVER_1S=0`, `UDP_ASSEMBLY_DROPPED_TOTAL=0`
  - 비고: 이번 verify는 수정 반영 후 회귀 스모크 목적이며, M7 성능 Gate 재판정용 프로필/지속시간은 아님 (`DEC_AVG=9.4`, `LAT_P95_US=41672`)

Next action
- 실제 데스크톱 세션에서 수동 입력/확장키 시나리오와 장시간 reconnect/soak를 한 번 더 확인해, 이번 안정성 보강이 장기 런에서도 회귀 없이 유지되는지 검증한다.

### 111) 2026-03-12 장시간 루프백 검증: 720p/1080p 프레임·지연·프리즈 재확인
Goal
- 최신 안정성 보강 이후 현재 기준선 프로필에서 장시간 루프백으로 프레임, 지연, 프리즈/복구, UDP assembly drop 지표를 다시 확인한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - 이번 턴은 검증 전용 작업으로 코드 변경 없음
  - 직전 빌드 산출물 `build-vcpkg-local/apps/native_poc/Debug` 사용
- Long-run verify 720p30 (60초):
  - 명령: `REMOTE60_NATIVE_H264_NO_PACING=1`, `REMOTE60_NATIVE_FRAME_GATING_DISABLE=1`, `REMOTE60_NATIVE_ABR_DISABLE=1` + `automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 65 -ClientSeconds 60 -Bitrate 5000000 -Keyint 60 -EncodeWidth 1280 -EncodeHeight 720 -EncoderBackend mft_auto -DecoderBackend mft_auto -NoInputChannel`
  - 로그: `automation/logs/verify-native-video-20260312-133741`
  - 결과: `HOST_RC=0`, `CLIENT_RC=0`, `DEC_AVG=31.82`, `LAT_P95_US=9909`, `PRESENT_GAP_OVER_1S=0`, `DECODE_ZERO_STREAK_MAX_SEC=0`, `UDP_ASSEMBLY_DROPPED_TOTAL=0`, `M7_PASS=True`
- Long-run verify 1080p30 (60초):
  - 명령: `REMOTE60_NATIVE_H264_NO_PACING=1`, `REMOTE60_NATIVE_FRAME_GATING_DISABLE=1`, `REMOTE60_NATIVE_ABR_DISABLE=1` + `automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 65 -ClientSeconds 60 -Bitrate 8000000 -Keyint 30 -EncodeWidth 1920 -EncodeHeight 1080 -EncoderBackend mft_auto -DecoderBackend mft_auto -NoInputChannel`
  - 로그: `automation/logs/verify-native-video-20260312-133852`
  - 결과: `HOST_RC=0`, `CLIENT_RC=0`, `DEC_AVG=35.58`, `LAT_P95_US=13692`, `PRESENT_GAP_OVER_1S=0`, `DECODE_ZERO_STREAK_MAX_SEC=0`, `UDP_ASSEMBLY_DROPPED_TOTAL=0`, `M7_PASS=True`
- 종합 판정:
  - 현재 루프백 장시간 검증 범위에서는 720p/1080p 모두 fps/latency 목표치 이내이며, 프리즈/복구 이벤트와 assembly drop이 관찰되지 않았다.

Next action
- 동일 기준선으로 WAN 또는 reconnect soak를 추가 실행해, 루프백 외 조건에서도 이번 안정성 보강의 지속성을 확인한다.

### 112) 2026-03-15 외부 2PC 테스트 준비물 최신화 (번들 + 커맨드 정리)
Goal
- 다른 디바이스 간 테스트에 바로 사용할 수 있도록 최신 host/client 빌드 산출물과 실행 커맨드를 portable bundle 기준으로 정리한다.

Files changed
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Bundle package:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-20260315-230003`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-20260315-230003.zip`
- Bundle validation:
  - 번들 `automation/`에 `m9_easy.ps1`, `run_wan_host_capture.ps1`, `run_wan_client_capture.ps1`, `summarize_wan_capture.ps1` 포함 확인
  - 번들 `docs/EXTERNAL_WAN_QUICKSTART.md` 생성 및 최신 프로필 기준 커맨드 반영 확인
  - `powershell -NoProfile -ExecutionPolicy Bypass -File dist\native-video-external-20260315-230003\automation\m9_easy.ps1 help` 성공
  - `powershell -NoProfile -ExecutionPolicy Bypass -File dist\native-video-external-20260315-230003\automation\m9_easy.ps1 prepare` 성공 (`tmp_m9_apply.json` 생성 확인)

Next action
- host PC에서는 번들 루트에서 `run_native_video_with_config.ps1` 또는 `m9_easy.ps1 host off/on`으로 실행한다.
- client PC에서는 동일 번들을 복사해 `run_native_video_with_config.ps1 -Role client` 또는 `m9_easy.ps1 client off/on <HOST_PUBLIC_IP_OR_DNS>`로 연결한다.
- 실제 외부 2PC 실행 후 `CFG-2PC-01`, `M9-2PC-GATE-01` 수동 확인 결과를 별도로 기록한다.

### 113) 2026-03-15 외부 2PC 기본 프로필 수정: 10fps downshift 오해 제거
Goal
- 외부 2PC 기본 실행 예제가 정적 장면에서 10fps로 내려가 보이던 원인을 제거하고, generic smoke 기준을 fixed 30fps 프로필로 분리한다.

Files changed
- `automation/native_video_profile_1080p_external_template.json`
- `automation/m9_easy.ps1`
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Root cause check:
  - 기존 외부 예제 기본값으로 사용하던 `native_video_profile_1080p_lowlat.json`은 `frameGatingDisable=false`, `staticSceneFps=10`으로 확인
  - 해석: 정적 장면에서는 의도적으로 10fps까지 downshift 가능
- Profile fix:
  - `native_video_profile_1080p_external_template.json`을 external smoke 기본값으로 재정의
  - 핵심 설정:
    - `encoderBackend=mft_auto`
    - `decoderBackend=mft_auto`
    - `h264NoPacing=true`
    - `frameGatingDisable=true`
- Bundle refresh:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-20260315-231325`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-20260315-231325.zip`
- Output validation:
  - 새 번들 `docs/EXTERNAL_WAN_QUICKSTART.md`에서 Quick 2PC Run 기본 프로필이 `native_video_profile_1080p_external_template.json`으로 바뀐 것 확인
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/m9_easy.ps1 help` 성공, lowlat baseline이 static scene에서 10fps로 내려갈 수 있다는 안내 문구 추가 확인
  - 번들 내부 `powershell -NoProfile -ExecutionPolicy Bypass -File dist\native-video-external-20260315-231325\automation\m9_easy.ps1 help` 성공

Next action
- 일반 외부 2PC 스모크는 `native_video_profile_1080p_external_template.json` 또는 `native_video_profile_1080p.json` 기준으로 실행한다.
- `m9_easy.ps1`는 M9 A/B 전용으로만 사용하고, static scene 10fps는 정상 동작으로 해석한다.

### 114) 2026-03-15 native 입력 설정 전달 복구 + native/web GUI 경로 분리
Goal
- 외부 테스트에서 키/마우스 입력이 전혀 동작하지 않던 원인을 수정하고, native bundle과 web GUI 경로의 역할 차이를 명확히 문서화한다.

Files changed
- `automation/run_native_video_with_config.ps1`
- `automation/native_video_profile_1080p_window_input_template.json`
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Root cause check:
  - `run_native_video_with_config.ps1`가 exe에 `--config`를 넘기지 않아 JSON의 `enableInputInjection`, `inputTarget*`, `noInputChannel=false`가 native host/client에 전달되지 않던 문제 확인
  - native bundle 자체는 browser GUI path가 아니므로 `desktop / window list / touch UI`는 포함하지 않는 구조임을 코드/문서 기준으로 재확인
- Runtime smoke (config passthrough):
  - 사용 config: `automation/logs/m35-input-validate-20260307-172012/m35_profile.json`
  - 결과 로그: `tmp/config-pass-smoke/host.out.log`, `tmp/config-pass-smoke/client.out.log`
  - 확인 사항:
    - host wrapper args에 `--config ...m35_profile.json` 포함
    - host 로그에 `input injection enabled mode=background_message targetProcess=remote60_native_video_client_poc.exe` 출력
    - client 로그에 `control connected port=43001 inputChannel=1` 출력
- Bundle refresh:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v2`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v2-20260315-232939`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v2-20260315-232939.zip`
  - 번들 확인:
    - `native_video_profile_1080p_window_input_template.json` 포함
    - `EXTERNAL_WAN_QUICKSTART.md`에 native window-target input 섹션 + web GUI path 안내 추가

Next action
- native 경로에서 키/마우스 입력이 필요하면 `native_video_profile_1080p_window_input_template.json`을 수정해 특정 HWND 대상(window-target)으로 실행한다.
- `desktop / window list / touch UI`가 필요하면 native bundle이 아니라 `automation/run_web_runtime.ps1` 기반 web runtime으로 테스트한다.

### 115) 2026-03-15 web GUI 외부 테스트 준비물 추가 + run_web_runtime 포트 전달 수정
Goal
- `desktop / window list / touch UI`를 실제로 포함하는 web runtime 외부 테스트 경로를 별도 bundle로 준비하고, `run_web_runtime.ps1 -Port`가 signaling 서버에 반영되지 않던 버그를 수정한다.

Files changed
- `automation/run_web_runtime.ps1`
- `automation/package_web_runtime_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Web runtime local smoke:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/run_web_runtime.ps1 -Port 3001 -AutoStopSec 5`
  - 결과: `READY=1`, `WEB_URL=http://127.0.0.1:3001`
- Web bundle package:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_web_runtime_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName web-runtime-external-v2`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\web-runtime-external-v2-20260315-234022`
    - `BUNDLE_ZIP=D:\remote\remote\dist\web-runtime-external-v2-20260315-234022.zip`
- Bundled runtime smoke:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File dist\web-runtime-external-v2-20260315-234022\automation\run_web_runtime.ps1 -Port 3002 -AutoStopSec 5`
  - 결과: `READY=1`, `WEB_URL=http://127.0.0.1:3002`

Next action
- `desktop / window list / touch UI` 검증은 `web-runtime-external-v2-20260315-234022.zip` 기준으로 진행한다.
- native bundle은 low-latency PoC/video-only + window-target input 용도로만 유지한다.

### 116) 2026-03-16 native window GUI parity v1 구현
Goal
- native host/client에 web과 유사한 선택형 GUI를 붙여 `Desktop Mode`, 창 목록, 현재 선택 타깃 표시, 선택 타깃 기준 캡처/입력 라우팅을 지원한다.
- 기존 `overview -> 클릭한 창 확대`를 기본 UX에서 제거하고, video 클릭은 항상 입력으로 보내도록 정리한다.

Files changed
- `apps/native_poc/src/poc_protocol.hpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `automation/validate_background_input_injection.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Native input regression check:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_background_input_injection.ps1 -DurationSec 12`
  - 결과:
    - `AUTO_PASS=1`
    - `INPUT_EVENTS=961`
    - `INPUT_NO_TARGET=0`
    - `INPUT_INJECT_FAIL=0`
  - 비고: 새 좌측 panel layout에 맞춰 client input burst 좌표를 video 영역 기준으로 보정
- Native control/window GUI smoke:
  - 로컬 host/client 실행 후 client panel 자동 refresh 확인
  - client log: `tmp/native-gui-smoke4/client.out.log`
    - `[native-video-client][control] window-list seq=1 count=12 selectedId=0 locked=0 firstId=788396 ...`
  - selection smoke:
    - host log: `tmp/native-gui-smoke5/host.out.log`
      - `[native-video-host][control] window-select seq=1 requestedId=788396 applied=1 selectedId=788396 reason=ok ...`
    - client log: `tmp/native-gui-smoke5/client.out.log`
      - `[native-video-client][control] window-selected seq=1 ok=1 windowId=788396 reason=ok ...`
- 구현 요약:
  - native control protocol에 `ControlWindowListRequest`, `ControlWindowList`, `ControlWindowSelect`, `ControlWindowSelected` 추가
  - host에 shareable window enumeration + selected window state + desktop/window 캡처 전환 추가
  - client에 상시 좌측 panel(`Refresh`, `Desktop Mode`, selected target, window list, stats) 추가
  - client 입력 좌표를 video 영역 기준으로 정규화해 desktop/window 모드 모두 일관된 입력 라우팅이 가능하도록 수정
  - `WM_POINTER*` 기반 단일 touch tap/drag 입력 추가

Next action
- 실제 외부 2PC에서 창 목록 UI, 특정 창 선택, Desktop Mode 복귀, 작업표시줄 클릭 입력을 수동으로 1차 확인한다.
- 필요 시 panel hit area/scroll UX와 selection visual polish를 후속 미세조정한다.

### 117) 2026-03-16 native 2PC 테스트 번들/커맨드 최신화
Goal
- 바로 다른 장비에서 2PC 테스트를 할 수 있도록 최신 native GUI 기준 bundle과 실행 커맨드를 다시 고정한다.

Files changed
- `automation/native_video_profile_1080p_external_template.json`
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- External template smoke:
  - local host/client short run with `automation/native_video_profile_1080p_external_template.json`
  - client log: `tmp/native-2pc-ready-smoke/client.out.log`
    - `control connected port=43001 inputChannel=1`
    - `window-list seq=1 count=12 selectedId=0 locked=0 ...`
  - host log: `tmp/native-2pc-ready-smoke/host.out.log`
    - `input injection enabled mode=background_message`
- Bundle package:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v3`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v3-20260316-005313`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v3-20260316-005313.zip`
  - 번들 가이드 확인:
    - 기본 profile이 `native_video_profile_1080p_external_template.json`
    - left panel(`Refresh`, `Desktop Mode`, `Window list`) 사용 안내 포함
    - `input/control on` 동작 안내 포함

Next action
- host PC에는 `native-video-external-v3-20260316-005313.zip`를 풀고 `run_native_video_with_config.ps1 -Role host`로 실행한다.
- client PC에는 같은 번들을 풀고 `run_native_video_with_config.ps1 -Role client -RemoteHost <HOST_PUBLIC_IP_OR_DNS>`로 연결한다.
- 실제 외부 2PC에서 `Desktop Mode`, 특정 창 선택, 키/마우스/휠 입력, 작업표시줄 클릭을 순서대로 수동 확인한다.

### 118) 2026-03-16 native GUI polish: UTF-8 title rendering + keyboard target routing
Goal
- native 좌측 panel에서 한글 창 제목이 깨져 보이던 문제를 줄이고, 선택된 타깃에 키 입력이 전달되지 않던 경로를 보강한다.

Files changed
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Input regression:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_background_input_injection.ps1 -DurationSec 12`
  - 결과:
    - `AUTO_PASS=1`
    - `INPUT_EVENTS=293`
    - `INPUT_NO_TARGET=0`
    - `INPUT_INJECT_FAIL=0`
- Native GUI/control smoke:
  - `tmp/title-key-smoke/client.out.log`
    - `control connected port=43001 inputChannel=1`
    - `window-list seq=1 ...`
- Bundle refresh:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v4`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v4-20260316-010834`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v4-20260316-010834.zip`

Next action
- 외부 2PC에서 한글 제목 표시와 실제 텍스트 입력(예: 메모장/노트패드++)을 수동으로 재확인한다.
- 필요 시 `WM_CHAR` 확장 범위와 key target selection fallback을 추가 보정한다.

### 119) 2026-03-16 native UI 전환: 상시 사이드바 -> home picker overlay + Targets 토글
Goal
- 상시 좌측 사이드바 때문에 video 영역이 줄어드는 문제를 줄이기 위해, native client를 `처음엔 홈 선택 화면, 선택 후엔 전체화면 영상` 구조로 바꾼다.

Files changed
- `apps/native_poc/src/native_video_client_main.cpp`
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Input regression:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_background_input_injection.ps1 -DurationSec 12`
  - 결과:
    - `AUTO_PASS=1`
    - `INPUT_EVENTS=720`
    - `INPUT_NO_TARGET=0`
    - `INPUT_INJECT_FAIL=0`
- Native GUI smoke:
  - `tmp/native-home-smoke/client.out.log`
    - `window-list seq=1 ...` 확인
  - 비고: 자동 클릭 smoke에서는 `window-select`까지 안정적으로 재현하지 못했고, 실제 picker UX는 수동 2PC 확인이 필요
- Bundle refresh:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v6`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v6-20260316-012420`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v6-20260316-012420.zip`
  - 가이드 반영:
    - 시작 화면이 home picker overlay임을 명시
    - 선택 후 fullscreen video + top-left `Targets` 버튼으로 다시 열기 동작 명시

Next action
- 실제 외부 2PC에서 `Desktop Mode -> fullscreen`, `Targets 버튼 -> picker reopen`, 특정 창 선택 후 fullscreen 전환을 수동으로 확인한다.
- mouse input 체감이 여전히 비정상이면 selected window/desktop 각각에서 별도 repro 로그를 추가 수집한다.

### 120) 2026-03-16 native home scene polish: centered picker layout + static scene + desktop default close
Goal
- home picker를 더 `scene`처럼 보이게 다듬고, picker가 켜져 있을 때 뒤 영상 때문에 깜빡이던 문제를 줄인다.
- `Desktop Mode`가 이미 기본 선택일 때는 버튼 한 번으로 바로 picker를 닫게 해 UX를 단순화한다.
- mouse click 경로는 old runtime처럼 `move -> click` 순서를 보내도록 보강한다.

Files changed
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Input regression:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_background_input_injection.ps1 -DurationSec 12`
  - 결과:
    - `AUTO_PASS=1`
    - `INPUT_EVENTS=954`
    - `INPUT_NO_TARGET=0`
    - `INPUT_INJECT_FAIL=0`
- Native smoke:
  - picker/home overlay 상태에서 client control 연결 및 `window-list seq=1 ...` 확인
  - 자동 클릭 smoke로는 desktop button -> video click 경로를 안정적으로 재현하지 못해 `desktop mode 실제 클릭`은 여전히 수동 2PC 확인이 필요

Next action
- 실제 외부 2PC에서 `Desktop Mode` 버튼으로 picker가 바로 닫히는지, 이후 desktop 클릭/작업표시줄 클릭이 먹는지 먼저 확인한다.
- mouse가 여전히 안 먹으면 `desktop mode`와 `selected window mode`를 분리해서 repro 로그를 따로 수집한다.

### 121) 2026-03-16 native input polish: drag capture + syskey forwarding + modifier-aware char synthesis
Goal
- native 입력 경로를 다시 점검해 drag 중 버튼 해제 누락 가능성을 줄이고, 로컬 단축키 충돌 없이 키보드 입력 범위를 넓힌다.
- background message 주입에서 `Shift`/`Alt` 계열 modifier가 실제 문자 생성에 반영되도록 보강한다.

Files changed
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Input regression:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_background_input_injection.ps1 -DurationSec 12`
  - 결과:
    - `AUTO_PASS=1`
    - `INPUT_EVENTS=337`
    - `INPUT_NO_TARGET=0`
    - `INPUT_INJECT_FAIL=0`
    - `INPUT_UNSUPPORTED=0`
    - `INPUT_IGNORED_MOVE=0`
- 로그 확인:
  - host log: `automation/logs/m35-input-validate-20260316-123208/host.out.log`
    - `inputNoTarget=0`, `inputInjectFail=0`, `inputUnsupported=0` 유지
  - client log: `automation/logs/m35-input-validate-20260316-123208/client.out.log`
    - `ackSeq=1080`, `dropped=0`
- 구현 요약:
  - client에서 mouse/touch down 시 `SetCapture`를 사용하고 `WM_CAPTURECHANGED`/`WM_CANCELMODE`에서 눌린 버튼 release event를 보정
  - plain `F5`, `[`, `]`, `;`, `'`를 더 이상 로컬 튜닝 단축키로 가로채지 않고, 로컬 단축키는 `Ctrl+Alt+...` 조합으로 제한
  - `WM_SYSKEYDOWN/WM_SYSKEYUP`도 remote input으로 전달되게 수정
  - host가 synthetic modifier key state를 유지하면서 `WM_CHAR`를 생성하도록 바꿔 `Shift`/`Alt` 조합 문자 입력 정합성을 보강

Next action
- 실제 외부 2PC에서 `Shift`/`Alt` 조합 문자, `[` `]` `;` `'`, drag 후 창 밖 release, 우클릭/휠을 수동으로 확인한다.
- `automation/validate_background_input_injection.ps1`는 여전히 occluded 타깃 창을 직접 생성하지 않으므로, 실제 가려진 앱 대상 수동 검증 또는 스크립트 확장이 추가로 필요하다.

### 122) 2026-03-16 config-first launcher 정리
Goal
- native host/client 실행 시 PowerShell wrapper가 JSON 값을 다시 CLI/env로 재조합하지 않도록 정리한다.
- `config 1개 + 실행 파일 1개` 형태에 맞춰 `run_native_video_with_config.ps1`가 기본 config 파일만으로도 실행될 수 있게 단순화한다.

Files changed
- `automation/run_native_video_with_config.ps1`
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Launcher smoke:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role host -ConfigPath automation/logs/m35-input-validate-20260316-123208/m35_profile.json`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role client -ConfigPath automation/logs/m35-input-validate-20260316-123208/m35_profile.json`
  - 결과:
    - host log: `tmp/launcher-smoke2/host.out.log`
      - `ROLE=host`, `CONFIG=...m35_profile.json`, `EXE=...remote60_native_video_host_poc.exe`
      - runtime completed with `client connected`, `control connected`, `done`
    - client log: `tmp/launcher-smoke2/client.out.log`
      - `ROLE=client`, `CONFIG=...m35_profile.json`, `EXE=...remote60_native_video_client_poc.exe`
      - runtime completed with `connected host=127.0.0.1`, `control connected`, `done`
- 구현 요약:
  - `run_native_video_with_config.ps1`를 thin wrapper로 재작성
  - wrapper는 이제 기본적으로 exe에 `--config`만 전달하고, 역할은 `role` 키 또는 선택적 `-Role` override로 결정
  - 기본 config 파일명 `automation/run_native_video_with_config.json`, 기본 exe 경로 auto-detect(`..\bin` 우선, source tree는 `build-vcpkg-local/apps/native_poc/Debug`)
  - bundle/source guide를 config-first 흐름 기준으로 갱신

Next action
- bundle 실사용 기준으로 `automation/run_native_video_with_config.json` 기본 파일명 흐름을 한 번 더 짧게 수동 확인한다.
- 필요 시 host/client 전용 convenience launcher(`run_native_video_host.ps1`, `run_native_video_client.ps1`)를 추가해 role 설정조차 숨길지 결정한다.

### 123) 2026-03-17 config-first native external bundle 재패키징
Goal
- config-first launcher가 반영된 최신 native external bundle을 다시 생성해 바로 테스트 가능한 산출물을 고정한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Bundle package:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v9`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v9-20260317-234839`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v9-20260317-234839.zip`
- Bundle content spot check:
  - `dist/native-video-external-v9-20260317-234839/automation/run_native_video_with_config.ps1`
    - config-first launcher(`run_native_video_with_config.json` 기본 탐색, exe auto-detect) 반영 확인
  - `dist/native-video-external-v9-20260317-234839/docs/EXTERNAL_WAN_QUICKSTART.md`
    - host/client 모두 `config 1개 + launcher 1개` 흐름으로 가이드 반영 확인

Next action
- 테스트는 `D:\remote\remote\dist\native-video-external-v9-20260317-234839` 기준으로 진행한다.
- 각 장비에서 `automation\run_native_video_with_config.json`만 준비하고 `powershell -ExecutionPolicy Bypass -File .\automation\run_native_video_with_config.ps1`로 host/client를 실행한다.

### 124) 2026-03-18 native desktop input + committed text/IME + stable_text tune
Goal
- `desktop mode` 클릭 무반응, 영문 2중 입력, 한글/IME 조합창 문제를 함께 정리한다.
- external 기본 profile의 정지 텍스트/어두운 장면 blur 펌핑을 줄이기 위해 `stable_text` encoder tune을 추가하고 최신 bundle로 다시 패키징한다.

Files changed
- `apps/native_poc/src/poc_protocol.hpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/json_profile.hpp`
- `apps/native_poc/src/mf_h264_codec.hpp`
- `apps/native_poc/src/mf_h264_codec.cpp`
- `automation/native_video_profile_1080p_external_template.json`
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Input regression:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_background_input_injection.ps1 -DurationSec 12`
  - 결과:
    - `AUTO_PASS=1`
    - `INPUT_EVENTS=418`
    - `INPUT_NO_TARGET=0`
    - `INPUT_INJECT_FAIL=0`
    - `INPUT_UNSUPPORTED=0`
    - `INPUT_IGNORED_MOVE=0`
- External stable_text smoke:
  - temp config: `tmp/external-stable-text-smoke/cfg.json`
  - host/client launch:
    - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role host -ConfigPath tmp/external-stable-text-smoke/cfg.json`
    - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/run_native_video_with_config.ps1 -Role client -ConfigPath tmp/external-stable-text-smoke/cfg.json`
  - 결과:
    - host log: `tmp/external-stable-text-smoke/host.out.log`
      - `encoderTuneMode=stable_text`
      - `keyintTarget=60`
      - runtime completed with `done`
    - client log: `tmp/external-stable-text-smoke/client.out.log`
      - `control connected`
      - runtime completed with `done`
- Bundle package:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v10`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v10-20260318-004248`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v10-20260318-004248.zip`
- 구현 요약:
  - protocol에 committed text message 추가
  - client는 `WM_CHAR`/`WM_IME_COMPOSITION(GCS_RESULTSTR)`를 text message로 보내고 local IME UI를 suppress
  - host raw key path의 `WM_CHAR` 합성을 제거하고 text message만 `WM_CHAR`로 주입
  - desktop mode는 top-level visible window 기준으로 target을 다시 잡도록 수정
  - external 기본 profile을 `encoderTuneMode=stable_text`, `keyint=60`으로 변경

Next action
- 실제 외부 2PC에서 `Desktop Mode` taskbar/start click, 영문 1회 입력, 한글 committed text, 로컬 IME 조합창 미표시를 수동 확인한다.
- blur/clean pumping이 실제 체감에서 충분히 줄었는지 dark text 장면 기준으로 bundle `v10`에서 확인한다.

### 125) 2026-03-18 desktop actual click + static idle-hold
Goal
- `desktop mode` mouse 입력을 실제 OS cursor 이동 + 실제 click/wheel/drag로 분리하고, selected-window `background_message` 경로는 그대로 유지한다.
- static capture idle 시 synthetic keepalive 재인코딩을 제거하고 마지막 decoded frame hold 기준의 관측 지표로 바꾼다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_host_poc remote60_native_video_client_poc --parallel`
  - 결과: 성공
- Window-mode regression:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/validate_background_input_injection.ps1 -DurationSec 20`
  - 결과:
    - `AUTO_PASS=1`
    - `INPUT_EVENTS=25406`
    - `INPUT_NO_TARGET=0`
    - `INPUT_INJECT_FAIL=0`
    - selected-window 경로는 계속 `mode=window`로 주입됨
- Desktop actual click smoke (source tree):
  - temp config/log: `tmp/desktop-actual-click-smoke-src`
  - 결과:
    - host log에 `mode=desktop` 입력 13건 기록
    - `CURSOR_BEFORE=1666,0` -> `CURSOR_AFTER=210,1141`
    - `HAS_SYNTHETIC_LOGS=0`
- Idle-hold observability spot checks:
  - `automation/logs/m35-input-validate-20260318-172840/host.out.log`
    - `idleHoldPerSec=1 idleHoldTotal=1`
    - `syntheticKeepalive*` 로그 없음
  - 전용 static notepad/self-capture smoke(`tmp/idle-hold-static-smoke`, `tmp/idle-hold-self-capture-smoke`)에서는 이 장비에서 실제 callback이 계속 들어와 `idleHoldTotal`이 증가하지 않았음
    - 해석: idle-hold는 `callbackFrames==0`일 때만 증가하도록 바뀌었고, real callback이 유지된 케이스에서는 synthetic 대체 인코딩 없이 그대로 동작함
- Bundle refresh + desktop smoke:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v11`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v11-20260318-173446`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v11-20260318-173446.zip`
  - bundle smoke log: `tmp/desktop-actual-click-smoke-bundle`
    - `CURSOR_BEFORE=960,540` -> `CURSOR_AFTER=191,1000`
    - host log에 `mode=desktop` 입력 14건 기록
    - `HAS_SYNTHETIC_LOGS=0`

Next action
- 실제 외부 2PC bundle `v11`에서 `Desktop Mode` taskbar/start/title bar drag/close `X`를 수동으로 확인한다.
- static dark/text-heavy 장면을 30~60초 고정한 실장비 세션에서 `idleHoldTotal` 증가 여부와 체감 선명도 유지 여부를 한 번 더 확인한다.

### 126) 2026-04-01 native external bundle test-ready wrapper 정리
Goal
- native external bundle을 푼 직후 host/client를 가장 짧은 명령으로 바로 테스트할 수 있게 준비한다.
- bundle 안에서 호출되는 런타임 보조 스크립트 누락도 함께 정리한다.

Files changed
- `automation/host.ps1`
- `automation/client.ps1`
- `automation/run_native_video_host.ps1`
- `automation/run_native_video_client.ps1`
- `automation/package_native_video_external_bundle.ps1`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Wrapper smoke (source tree, existing binaries reuse):
  - temp config: `tmp/wrapper-smoke-20260401-211139/cfg.json`
  - host:
    - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/host.ps1 -ConfigPath tmp/wrapper-smoke-20260401-211139/cfg.json -ExeDir build-vcpkg-local/apps/native_poc/Debug`
    - 결과: `ROLE=host`, `client connected`, `control connected`, `done`
  - client:
    - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/client.ps1 -ConfigPath tmp/wrapper-smoke-20260401-211139/cfg.json -ExeDir build-vcpkg-local/apps/native_poc/Debug -RemoteHost 127.0.0.1`
    - 결과: `ROLE=client`, `connected host=127.0.0.1`, `control connected`, `done`
- Bundle package:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local -BundleName native-video-external-v13`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v13-20260401-211156`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v13-20260401-211156.zip`
  - bundle spot-check:
    - `automation/host.ps1` 포함
    - `automation/client.ps1` 포함
    - `automation/start_native_video_runtime_impl.ps1` 포함
    - `docs/EXTERNAL_WAN_QUICKSTART.md`에 one-command host/client 실행 예시 반영
- Build:
  - 코드 컴파일 변경이 없어 추가 빌드는 수행하지 않음

Next action
- 외부 host PC에서는 bundle `v13` 기준으로 `automation/host.ps1`를 바로 실행한다.
- client PC에서는 `automation/client.ps1 -RemoteHost <HOST_PUBLIC_IP_OR_DNS>`로 바로 접속 테스트를 시작한다.
- 실제 외부 2PC에서 `Desktop Mode`, window picker, 입력 전달을 수동 확인한다.

### 127) 2026-04-01 package script moved to D:\share
Goal
- 사용 요청대로 `automation/package_native_video_external_bundle.ps1`를 저장소 밖 `D:\share`로 이동한다.
- 이동 후에도 shared path에서 실제 패키징이 가능한지 확인하고, 현재 문서의 실행 경로를 맞춘다.

Files changed
- `automation/package_native_video_external_bundle.ps1` (repo에서 제거, `D:\share\package_native_video_external_bundle.ps1`로 이동)
- `apps/native_poc/README.md`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- File move:
  - `SOURCE_REMOVED=True`
  - `SHARE_EXISTS=True`
- Shared-path packaging smoke:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File D:\share\package_native_video_external_bundle.ps1 -Root D:\remote\remote -BuildDir build-vcpkg-local -BundleName native-video-external-v14`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-v14-20260401-211511`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-v14-20260401-211511.zip`
- Build:
  - 코드 변경이 없어 추가 컴파일은 수행하지 않음

Next action
- 이후 외부 bundle 재생성이 필요하면 저장소 내부가 아니라 `D:\share\package_native_video_external_bundle.ps1 -Root D:\remote\remote` 경로를 사용한다.
- 다른 자동화 문서/스크립트가 repo 내부 pack script를 직접 가리키지 않는지 추가 정리가 필요하면 후속 반영한다.

### 128) 2026-04-01 minimal bundle layout rewrite
Goal
- external bundle을 테스트 최소 세트만 남는 형태로 다시 정리한다.
- 결과물은 `실행파일 + config + host.ps1 + client.ps1`만 보이게 하고, `bin/docs/automation` 하위 폴더 구조를 제거한다.

Files changed
- `D:\share\package_native_video_external_bundle.ps1`
- `apps/native_poc/README.md`
- `docs/external_wan_test_guide.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Minimal bundle package:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File D:\share\package_native_video_external_bundle.ps1 -Root D:\remote\remote -BuildDir build-vcpkg-local -BundleName native-video-min2`
  - 결과:
    - `BUNDLE_DIR=D:\remote\remote\dist\native-video-min2-20260401-211955`
    - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-min2-20260401-211955.zip`
  - bundle root contents:
    - `remote60_native_video_host_poc.exe`
    - `remote60_native_video_client_poc.exe`
    - `config.json`
    - `host.ps1`
    - `client.ps1`
- Bundle script smoke:
  - 테스트용으로 생성된 bundle의 `config.json`에 `seconds=6`, `remoteHost=127.0.0.1`만 임시 반영 후 실행
  - host:
    - `powershell -NoProfile -ExecutionPolicy Bypass -File D:\remote\remote\dist\native-video-min2-20260401-211955\host.ps1`
    - 결과: `client connected`, `control connected`, `done`
  - client:
    - `powershell -NoProfile -ExecutionPolicy Bypass -File D:\remote\remote\dist\native-video-min2-20260401-211955\client.ps1 -RemoteHost 127.0.0.1`
    - 결과: `connected host=127.0.0.1`, `control connected`, `done`
- Build:
  - 코드 변경이 없어 추가 컴파일은 수행하지 않음

Next action
- 이후 전달용 번들은 `D:\share\package_native_video_external_bundle.ps1`로 생성한다.
- 사용자는 번들 루트에서 `host.ps1` 또는 `client.ps1 -RemoteHost <IP>`만 실행하면 된다.

### 129) 2026-04-01 share bundle config cleanup
Goal
- `D:\share`로 옮긴 minimal bundle에 테스트용 종료 설정이 남아 host/client가 자동 종료되던 문제를 바로잡는다.

Files changed
- `D:\share\native-video-min2-20260401-211955\config.json`
- `D:\share\native-video-min2-20260401-211955.zip`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Root cause:
  - shared bundle `config.json`에 테스트값 `seconds=6`, `encodeWidth=1280`, `encodeHeight=720`, `bitrate=5000000`, `remoteHost=192.168.0.76`가 남아 있었음
- Fix applied:
  - `seconds=0`
  - `encodeWidth=1920`
  - `encodeHeight=1080`
  - `bitrate=8000000`
  - `remoteHost=YOUR_PUBLIC_IP_OR_DNS`
  - bundle 내부 `*.log` 제거 후 zip 재생성
- Final shared bundle contents:
  - `remote60_native_video_host_poc.exe`
  - `remote60_native_video_client_poc.exe`
  - `config.json`
  - `host.ps1`
  - `client.ps1`

Next action
- shared bundle는 이제 시간 제한 없이 실행된다.
- client 실행 시에는 `client.ps1 -RemoteHost <HOST_PUBLIC_IP_OR_DNS>`만 넣어 사용하면 된다.

### 130) 2026-04-02 android direct client plan document split
Goal
- Android direct client 작업을 기존 성능/운영 체크리스트와 분리해 별도 구현계획 문서로 정리한다.
- 계획은 `한 번에 전체 구현`이 아니라 `공용 client core 분리 -> Windows 회귀 고정 -> Android 단계별 확장` 순서로 안정적으로 나누는 것을 기준으로 고정한다.

Files changed
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- 문서 검토 기준:
  - Android direct client 목표/범위/제외 범위를 한국어로 고정
  - 기존 Windows native client와 함께 쓸 공용 core 재사용 전략 명시
  - `Phase A~G` 단계와 각 단계 gate/완료조건을 명시
- Build/Test:
  - 문서 작업만 수행했으므로 추가 빌드/테스트는 실행하지 않음

Next action
- `Phase A. 공용 core 추출 범위 고정`부터 착수한다.
- 구현 시작 전 Windows client에서 core로 이동할 함수/상태 묶음을 먼저 잘라내고, direct-connect 회귀 기준을 선행 정리한다.

### 131) 2026-04-02 android prework shared client core slice
Goal
- Android direct client 착수 전에 Windows native video client에서 공용 core로 분리 가능한 첫 상태 묶음을 실제 코드로 추출한다.
- Windows 전용 UI/렌더링은 유지하고, Android/Windows 공통 후보인 `input queue`, `window panel state`, `keyframe request limiter`를 core 파일로 이동한다.

Files changed
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_poc --config Debug`
  - 결과: 성공
- Code/structure:
  - `native_video_client_main.cpp`에서 공용 후보 상태기계 일부를 새 shared core로 치환
  - Windows 클라이언트는 새 core 객체를 통해 기존 input/window/keyframe 흐름을 계속 사용
- Test:
  - 로컬 서버를 띄운 direct-connect 런타임 smoke 및 `M3.5 background 입력 주입 1차 수동 검증`은 이번 턴에서 수행하지 않음

Next action
- `Phase B` 범위로 `UDP handshake/assembly`, `TCP control loop`, `window list/select`, `input ack`를 계속 core 쪽으로 이동한다.
- 이후 Windows localhost direct-connect 회귀와 남아 있는 `M3.5 background 입력 주입 1차 수동 검증`을 진행한다.

### 132) 2026-04-02 android prework control state extraction
Goal
- Android direct client 선행 작업으로 Windows native video client의 control-thread 상태를 추가로 shared core로 이동한다.
- `capture mode request`와 `runtime tune state`를 공용 상태기계로 분리해 이후 `TCP control loop` 자체 분리에 필요한 경계를 더 선명하게 만든다.

Files changed
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_poc --config Debug`
  - 결과: 성공
- Code/structure:
  - `capture mode request` pending/sequence 관리가 shared core로 이동
  - `runtime tune` enable/dirty/default/consume 로직이 shared core로 이동
  - Windows main/control thread는 shared core 객체를 소비하는 glue 역할로 축소
- Test:
  - direct-connect 런타임 smoke와 수동 입력 검증은 이번 턴에 수행하지 않음

Next action
- `TCP control loop`의 메시지 송수신 절차 자체를 shared core helper로 옮긴다.
- 이후 `UDP handshake/assembly` 분리와 Windows localhost 회귀 확인으로 `Phase B`를 더 진행한다.

### 133) 2026-04-02 android prework control scheduler and test
Goal
- Android/향후 UDP 전환을 고려해 Windows native video client의 control loop에서 `무엇을 언제 보내는지`를 transport-independent scheduler로 분리한다.
- shared core용 테스트 실행 파일을 추가해 ping/window/input/runtime-tune/keyframe action 흐름을 코드 레벨에서 검증한다.

Files changed
- `apps/native_poc/CMakeLists.txt`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug`
  - 결과: 성공
- Test:
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Code/structure:
  - shared core에 `ClientControlScheduler` 추가
  - scheduler가 ping/window-select/metrics/keyframe/runtime-tune/input action과 expected response를 결정
  - Windows main은 TCP adapter처럼 action send + typed response consume만 담당

Next action
- `TCP adapter` 자체를 helper로 정리해 `send/recv` 절차를 main에서 더 걷어낸다.
- 그 다음 `UDP handshake/assembly` 분리와 Windows localhost direct-connect 회귀 확인을 진행한다.

### 134) 2026-04-02 android prework udp assembly helper and localhost smoke
Goal
- Android/향후 transport 분리를 위해 Windows native video client의 UDP H.264 assembly 상태를 shared core helper로 이동한다.
- shared core 테스트와 localhost runtime smoke를 다시 실행해 scheduler/assembler refactor 이후 최소 자동 경로를 확인한다.

Files changed
- `apps/native_poc/CMakeLists.txt`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug`
  - 결과: 성공
- Shared core test:
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Localhost smoke:
  - `powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 10 -ClientSeconds 6 -Bitrate 1100000 -Keyint 15 -TraceEvery 0 -NoInputChannel`
  - 결과:
    - `HOST_RC=0`, `CLIENT_RC=0`
    - `UDP_ASSEMBLY_DROPPED_TOTAL=0`
    - `CTRL_RTT_AVG_US=294.67`
    - `OVERALL_OK=True`
    - 단, `DEC_AVG=6.2`, `GATE_A_PASS=False`, `CAPTURE_INPUT_STALL_DETECTED=True`
- Code/structure:
  - shared core에 `UdpH264FrameAssembler` 추가
  - recv thread가 assembly 상태를 직접 들지 않고 helper 결과만 소비
  - shared core test에 UDP assembler 케이스 추가

Next action
- `TCP adapter`의 send/recv/typed-response 처리도 별도 helper로 옮겨 main을 더 얇게 만든다.
- 이후 실제 Gate A 판정용 localhost/2PC 회귀는 capture stall 원인을 분리한 뒤 다시 본다.

### 135) 2026-04-02 android prework tcp control adapter helper
Goal
- Windows native video client에서 TCP control `send/recv/typed-response` 절차를 별도 helper로 분리해 main loop를 더 얇게 만든다.
- helper 분리 후 shared core test와 localhost UDP/H.264 smoke를 다시 실행해 구조 변경 회귀를 확인한다.

Files changed
- `apps/native_poc/CMakeLists.txt`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_tcp_control.hpp`
- `apps/native_poc/src/native_video_client_tcp_control.cpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug`
  - 결과: 성공
- Shared core test:
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Localhost smoke:
  - `powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 10 -ClientSeconds 6 -Bitrate 1100000 -Keyint 15 -TraceEvery 0 -NoInputChannel`
  - 결과:
    - `HOST_RC=0`, `CLIENT_RC=0`
    - `UDP_ASSEMBLY_DROPPED_TOTAL=0`
    - `CTRL_RTT_AVG_US=271.83`
    - `OVERALL_OK=True`
    - 단, `DEC_AVG=6.2`, `GATE_A_PASS=False`, `CAPTURE_INPUT_STALL_DETECTED=True`

Next action
- 실제 남은 큰 작업은 `capture_input_stall` 원인 분리다. Gate A가 아직 실패하므로 Android 기능 단계로 넘어가면 안 된다.
- 그 다음 `TCP/UDP transport adapter` 경계를 더 일반화하거나, stall 해소 후 localhost/2PC 회귀를 다시 돌린다.

### 136) 2026-04-03 gate-a localhost profile fixed + shell fallback diagnosis
Goal
- Android 선행 검증용 Gate A localhost 프로필을 `frame gating off`, `ABR off`, `h264 no pacing`으로 고정한다.
- 같은 프로필에서도 host capture source가 `GetShellWindow()`로 떨어지면 무효 판정이 나오는 점을 진단 출력으로 분리한다.

Files changed
- `automation/verify_native_video_runtime.ps1`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug`
  - 결과: 성공
- Gate A profile run (`-GateAProfile`):
  - 결과: `HOST_CAPTURE_SOURCE_LAST=CreateForWindow(GetShellWindow())`
  - `GATE_A_SHELLWINDOW_FALLBACK_DETECTED=True`
  - `GATE_A_PASS=False`
- Direct env run (`REMOTE60_NATIVE_H264_NO_PACING=1`, `REMOTE60_NATIVE_FRAME_GATING_DISABLE=1`, `REMOTE60_NATIVE_ABR_DISABLE=1`):
  - 결과: `DEC_AVG=26.6`, `GATE_A_PASS=True`, `CAPTURE_INPUT_STALL_DETECTED=False`
- 결론:
  - 이전 `capture_input_stall`은 기본 frame gating/static scene 검증 충돌이 주원인
  - 현재 남은 불안정성은 Gate A profile 자체가 아니라 host capture source가 `ShellWindow`로 fallback되는 환경 케이스

Next action
- Gate A 자동 검증에서 monitor capture source를 더 안정적으로 고정하거나, `ShellWindow` fallback 시 재시도/실패 사유 분리 정책을 추가한다.
- 그 다음 Gate A pass 로그를 재현성 있게 1회 더 확보하고 Android `Phase B` 종료 판정을 정리한다.

### 137) 2026-04-03 gate-a shell fallback retry and pass
Goal
- Gate A localhost 자동 검증에서 `CreateForWindow(GetShellWindow())` fallback이 걸릴 때 재시도해 monitor capture가 잡히는지 확인한다.
- Android 선행 검증용 Gate A pass 로그를 스크립트 옵션 하나로 재현 가능하게 만든다.

Files changed
- `automation/verify_native_video_runtime.ps1`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Gate A profile run:
  - `powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 10 -ClientSeconds 6 -Bitrate 1100000 -Keyint 15 -TraceEvery 0 -NoInputChannel -GateAProfile`
  - 결과:
    - `GATE_A_PROFILE_APPLIED=True`
    - `HOST_CAPTURE_SOURCE_LAST=MonitorFromWindow(GetDesktopWindow())`
    - `GATE_A_CAPTURE_SOURCE_MONITOR_OK=True`
    - `GATE_A_SHELLWINDOW_FALLBACK_DETECTED=False`
    - `DEC_AVG=21.2`
    - `LAT_P95_US=4521`
    - `CAPTURE_INPUT_STALL_DETECTED=False`
    - `GATE_A_PASS=True`
- 결론:
  - Gate A 실패의 주원인은 리팩터링 회귀가 아니라 `frame gating/static scene` 검증 충돌 + 일부 세션의 `ShellWindow` fallback이었다.
  - 현재 `-GateAProfile`로 Gate A localhost 통과 로그를 재현할 수 있다.

Next action
- Android `Phase B` 종료 판정을 문서화하고, 이후 실제 남은 리스크를 `M3.5 수동 입력 검증`과 Android 앱 셸 착수 준비로 정리한다.
- 필요하면 host capture source를 monitor-only로 더 강제하는 옵션을 추가해 재현성을 더 높인다.

### 138) 2026-04-03 android phase-c shell scaffold
Goal
- Android direct client `Phase C` 착수를 위해 Android 앱 프로젝트 최소 골격을 추가한다.
- Kotlin UI와 JNI bridge stub를 통해 host/port 입력, connect/disconnect, status/error 표시 흐름의 뼈대를 만든다.

Files changed
- `apps/android_direct_client/settings.gradle.kts`
- `apps/android_direct_client/build.gradle.kts`
- `apps/android_direct_client/gradle.properties`
- `apps/android_direct_client/app/build.gradle.kts`
- `apps/android_direct_client/app/proguard-rules.pro`
- `apps/android_direct_client/app/src/main/AndroidManifest.xml`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `apps/android_direct_client/app/src/main/res/values/themes.xml`
- `apps/android_direct_client/app/src/main/cpp/CMakeLists.txt`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- File structure:
  - `rg --files apps/android_direct_client`
  - 결과: Gradle/Kotlin/JNI scaffold 파일 생성 확인
- Static review:
  - `app/build.gradle.kts`, `MainActivity.kt`, `native_bridge.cpp` 내용 확인
- Build/Test:
  - 현재 환경에는 `java`, `gradle`, `ANDROID_HOME/ANDROID_SDK_ROOT`가 없어 Android build는 실행하지 못함
  - 따라서 이번 단계 검증은 파일 정합성과 scaffold 존재 확인까지 수행

Next action
- Android toolchain이 있는 환경에서 `apps/android_direct_client` Gradle sync/build를 실제로 돌려 Phase C 골격이 컴파일되는지 확인한다.
- 그 다음 JNI stub를 현재 공용 client core 연결 지점으로 교체하고 실제 connect/disconnect 상태를 native session으로 넘긴다.

### 139) 2026-04-03 android phase-c shared session controller
Goal
- Android JNI bridge가 임시 전역 문자열 상태 대신 공용 C++ session controller를 사용하도록 교체한다.
- 이후 Android/Windows 양쪽에서 재사용 가능한 최소 session 상태 API를 고정한다.

Files changed
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/CMakeLists.txt`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_shared_core_test --config Debug`
  - 결과: 성공
- Test:
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
  - session controller 유효성(빈 host/포트 검증, connect/disconnect snapshot) 포함
- Android scaffold check:
  - `rg --files apps/android_direct_client`
  - 결과: Android project/JNI scaffold 파일 유지 확인
- Build/Test limitation:
  - 현재 환경에는 `java`, `gradle`, `ANDROID_HOME/ANDROID_SDK_ROOT`가 없어 Android app build는 실행하지 못함

Next action
- Android toolchain 환경에서 `apps/android_direct_client` Gradle sync/build를 실행해 JNI/controller 연결이 실제로 컴파일되는지 확인한다.
- 그 다음 session controller를 실제 native session 구현으로 확장해 connect/disconnect가 stub가 아니라 공용 transport/session core를 타도록 바꾼다.

### 140) 2026-04-03 android phase-c session probe wiring
Goal
- Android `Phase C`의 공용 session controller가 실제 TCP control connect와 UDP hello handshake를 수행하도록 올린다.
- JNI bridge가 더 이상 상태 문자열 stub만 바꾸는 것이 아니라, 최소 네트워크 probe 결과를 반영하도록 만든다.

Files changed
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/CMakeLists.txt`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `apps/android_direct_client/app/src/main/cpp/CMakeLists.txt`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_shared_core_test --config Debug`
  - 결과: 성공
- Test:
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Scope note:
  - session controller는 현재 `TCP control connect + UDP hello ack` 수준의 probe까지 연결
  - Android toolchain이 없어 실제 Gradle/NDK Android build는 아직 실행하지 못함

Next action
- Android toolchain 환경에서 `apps/android_direct_client` Gradle sync/build를 실제로 돌려 JNI/controller/network probe 조합이 컴파일되는지 확인한다.
- 그 다음 session controller를 현재 probe 수준에서 공용 transport/session core 기반의 실제 session lifecycle로 확장한다.

### 141) 2026-04-06 android ldplayer2 build install smoke
Goal
- Android Studio/SDK/NDK/CMake 설치 후 Android direct client를 실제로 빌드한다.
- LDPlayer 인스턴스 `2`에 APK를 설치하고 앱 셸 실행과 `CONNECT`/`DISCONNECT` UI 반응을 확인한다.

Files changed
- `apps/android_direct_client/app/build.gradle.kts`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/values/themes.xml`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Environment confirmed:
  - Android SDK: `C:\Users\shota\AppData\Local\Android\Sdk`
  - NDK: `30.0.14904198`
  - CMake: `4.1.2`
  - LDPlayer device: `emulator-5558`
- Build:
  - `gradle clean assembleDebug` with Android Studio JBR + local SDK
  - 결과: 성공
- Install:
  - `adb -s emulator-5558 install -r app-debug.apk`
  - 결과: 성공
- Launch/runtime:
  - `adb -s emulator-5558 shell am start -W -n com.remote60.androiddirect/.MainActivity`
  - 결과: 실행 성공
  - `pidof com.remote60.androiddirect` -> 프로세스 확인
  - `dumpsys activity activities` -> `MainActivity` task/resumed 확인
- UI check:
  - `uiautomator dump` 결과에서 shell UI 요소 확인:
    - `Android Direct Client Shell`
    - host `192.168.0.10`
    - ports `43000` / `43001`
    - `CONNECT` / `DISCONNECT` / `REFRESH`
  - `CONNECT` 탭 후:
    - status=`error`
    - error=`tcp control connect failed`
  - `DISCONNECT` 탭 후:
    - status=`disconnected`
    - error cleared
- Fix applied during verification:
  - LDPlayer Android 9 호환을 위해 AppCompat/Material inflater 경로를 제거하고 기본 `Activity` + platform theme로 낮춤

Next action
- `ClientSessionController`를 현재 TCP/UDP probe에서 실제 공용 transport/session core 연결로 확장한다.
- 그 다음 Android `Phase D` 영상 수신용 decoder/surface adapter 경계를 정의한다.

### 142) 2026-04-06 android phase-c real session lifecycle wiring
Goal
- Android `Phase C` 버튼이 probe가 아니라 실제 공용 session core의 비동기 lifecycle/control loop를 타도록 올린다.
- LDPlayer 2에서 `connecting -> connected/error -> disconnected` 상태 변화가 자동 polling으로 보이도록 검증한다.

Files changed
- `apps/native_poc/src/native_socket.hpp`
- `apps/native_poc/src/native_video_client_tcp_control.hpp`
- `apps/native_poc/src/native_video_client_tcp_control.cpp`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `apps/native_poc/CMakeLists.txt`
- `apps/android_direct_client/app/src/main/AndroidManifest.xml`
- `apps/android_direct_client/app/src/main/cpp/CMakeLists.txt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_shared_core_test --config Debug`
  - 결과: 성공
- Native test:
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
  - fake UDP hello + TCP control server 기준으로 async connect, window list summary, control loop failure, disconnect 복귀 검증
- Android build/install:
  - `gradle-8.7/bin/gradle.bat clean assembleDebug` with Android Studio JBR + local SDK
  - 결과: 성공
  - `adb -s emulator-5558 install -r app-debug.apk`
  - 결과: 성공
- LDPlayer 2 runtime:
  - `adb -s emulator-5558 shell am start -W -n com.remote60.androiddirect/.MainActivity --es host 192.168.0.76 --ei videoPort 43000 --ei controlPort 43001`
  - 결과: launch extra 기반 host/port prefill 성공
  - validation fake host(`UDP hello ack + ControlPong + ControlWindowList`) 기준:
    - `CONNECT` 후 status=`connected window_list_received count=2 selected=desktop`
    - 잘못된 control port(`43009`) 기준 status=`connecting -> error`, error=`connect failed`
    - `DISCONNECT` 후 status=`disconnected`
- Scope note:
  - Android manifest에 `INTERNET`/`ACCESS_NETWORK_STATE` 권한 추가
  - Phase D 준비로 `ClientEncodedFrameSink` 경계만 추가했고 실제 MediaCodec/Surface wiring은 아직 미구현

Next action
- `ClientEncodedFrameSink`를 실제 UDP frame receive path와 연결하고 Android `MediaCodec + Surface` adapter를 붙인다.
- 그 다음 Android window list/select UI와 decoder reset 경계를 `Phase D/E` 범위로 확장한다.

### 143) 2026-04-06 android phase-d video receive wiring
Goal
- `ClientEncodedFrameSink` 뒤에 실제 UDP H.264 receive path를 연결하고 Android `SurfaceView + MediaCodec` decode/render 경계를 붙인다.
- 실제 UDP host 순서(`UDP hello -> control listen`)에 맞춰 Android session core가 real host에도 붙도록 control connect 순서를 정정한다.

Files changed
- `apps/native_poc/src/native_socket.hpp`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/android_direct_client/app/src/main/cpp/CMakeLists.txt`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build/test:
  - `cmake --build build-vcpkg-local --target remote60_native_video_client_shared_core_test --config Debug`
  - `build-vcpkg-local/apps/native_poc/Debug/remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build/install:
  - `gradle-8.7/bin/gradle.bat assembleDebug` with Android Studio JBR + local SDK
  - 결과: 성공
  - `adb -s emulator-5558 install -r app-debug.apk`
  - 결과: 성공
- LDPlayer 2 runtime:
  - real host(`remote60_native_video_host_poc --bind-port 43000 --control-port 43001 --codec h264`) 기준
    - host log: `client connected transport=udp`, `control waiting port=43001`, `[control] client connected`
    - app screenshot: status=`connected window_list_received count=9 selected=desktop`
    - Android logcat: `updated codec config`, `MediaCodec started width=1280 height=720`, `released output frame count=1`
  - disconnect 검증:
    - status=`disconnected`
  - 잘못된 control port(`43009`) 검증:
    - status=`error`
    - error=`connect failed`
- Scope note:
  - Android session controller는 이제 real host 순서에 맞게 `UDP hello` 후 video receive thread를 시작하고, 그 다음 `TCP control`을 retry 연결한다.
  - LDPlayer screenshot 상 surface는 검게 보였지만, host UDP 송신/Android `MediaCodec started`/output release 로그까지 확인되어 decode 경로 자체는 동작한다.
  - 현재 환경의 host capture source가 `MonitorFromWindow(GetDesktopWindow())`로 잡혀 있어 표시 내용이 검게 들어오는지 추가 확인이 필요하다.

Next action
- Android window list/select UI를 실제로 열고 capture target을 desktop 외 다른 shareable window로 바꿔 visible content를 검증한다.
- 그 다음 touch/input 경로를 Android surface 좌표계와 연결해 `Phase E/F`로 진행한다.

### 144) 2026-04-06 android textureview render visibility debug
Goal
- Android `Phase D`에서 black surface 원인을 줄이기 위해 `SurfaceView` 대신 `TextureView` 경로로 바꾸고, 스크린샷에서도 실제 디코드 출력이 보이는지 확인한다.
- decoder 상태를 UI에 직접 노출해 `surface/codec/csd/in/out` 값을 LDPlayer에서 바로 확인 가능하게 만든다.

Files changed
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `docs/history.md`
- `docs/구현계획.md`
- `docs/android_구현계획.md`

Validation / build / test result
- Android build/install:
  - `gradle-8.7/bin/gradle.bat assembleDebug`
  - 결과: 성공
  - `adb -s emulator-5558 install -r app-debug.apk`
  - 결과: 성공
- LDPlayer 2 runtime:
  - launch: `adb -s emulator-5558 shell am start -W -n com.remote60.androiddirect/.MainActivity --es host 192.168.0.76 --ei videoPort 43000 --ei controlPort 43001`
  - 결과: 실행 성공
  - screenshot:
    - `textureview-check2.png`에서 기존 완전 검은 영역 대신 좌상단 video content 일부가 캡처됨
    - status=`connected window_list_received count=9 selected=desktop`
    - debug=`surface=on codec=on size=1234x720 csd=1/1 in=2 out=2`
  - Android logcat:
    - `updated codec config`
    - `MediaCodec started width=1234 height=720`
    - `queued h264 frame count=1`
    - `released output frame count=1`
- Scope note:
  - `TextureView`는 배경 drawable을 직접 지원하지 않아, `FrameLayout` 배경으로 우회했다.
  - 스크린샷에 video content 일부가 잡히기 시작했으므로 기존 `SurfaceView` 별도 composition 문제는 사실상 해소됐다.
  - 아직 full-frame이 아니라 상단 일부만 보이는 상태라 `TextureView`/surface sizing 또는 crop/transform 보정이 추가로 필요하다.

Next action
- `TextureView` 표시 영역이 전체 프레임을 채우도록 surface sizing/transform을 보정한다.
- 그 다음 Android window list/select UI를 열어 desktop 외 실제 window target으로 visible content를 재검증한다.

### 145) 2026-04-06 android ui reference tab semantics docs
Goal
- Android 구현계획 문서에 목표 UI 레퍼런스 이미지와 탭 의미를 명시해 다음 세션 구현 기준을 고정한다.

Files changed
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Docs only:
  - `D:\remote\remote\image\f3c9df0d83454ce1ba78a2fd6cb7e7801735893664.webp`를 확인해 목표 UI 예시 이미지로 명시
  - 탭 의미를 문서에 고정:
    - `LD플레이어` 탭 = 각 윈도우별 화면
    - `디바이스` 탭 = 각 모니터 화면
- Build/Test:
  - 코드 변경 없음
  - 추가 빌드/테스트 없음

Next action
- Android window list/select UI를 위 레퍼런스 탭 구조에 맞춰 구현한다.
- `LD플레이어` 탭은 window grid/list로, `디바이스` 탭은 monitor view로 연결한다.

### 146) 2026-04-06 android next-work plan freeze
Goal
- Android direct client의 현재 완료 상태를 기준으로 다음 구현 순서를 고정한다.
- `Phase D` 마감과 `Phase E` 착수 사이의 실제 선행조건을 문서에 남긴다.

Files changed
- `docs/android_구현계획.md`
- `docs/구현계획.md`
- `docs/history.md`

Validation / build / test result
- Docs/code review only:
  - `docs/android_구현계획.md`
  - `docs/구현계획.md`
  - `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
  - `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
  - `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
  - `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
  - `apps/native_poc/src/native_video_client_session.hpp`
  - `apps/native_poc/src/native_video_client_session.cpp`
  - `apps/native_poc/src/native_video_client_shared_core.hpp`
  - `apps/native_poc/src/poc_protocol.hpp`
- 결론:
  - 현재 Android 앱은 `connect/disconnect + decoder debug` 셸 수준이며, `TextureView` full-frame 보정이 아직 남아 있다.
  - 공용 core에는 `window list/select` 상태기와 요청 모델이 이미 있으나 Android JNI는 아직 `connect/disconnect/status`만 노출한다.
  - 현재 `ControlWindowListMessage`는 title/size 중심 메타데이터만 주므로, `Phase E` 1차는 썸네일 생성보다 `refresh/select/Desktop Mode` 흐름을 먼저 닫는 것이 맞다.
- Build/Test:
  - 코드 변경 없음
  - 추가 빌드/테스트 없음

Next action
- `TextureView` full-frame 보정으로 `Phase D` 완료조건을 먼저 닫는다.
- 그 다음 `ClientSessionController -> JNI -> Kotlin` 제어 브리지를 추가해 `refresh/select/Desktop Mode`와 레퍼런스 탭 UI 1차를 진행한다.

### 147) 2026-04-06 android phase-d surface buffer rebind and viewport expansion
Goal
- `TextureView`가 영상 실제 크기를 모른 채 작은 기본 buffer로 붙는 문제를 줄인다.
- LDPlayer 2에서 Android shell layout이 video viewport를 과도하게 눌러 `상단 일부만` 보이던 상태를 완화한다.

Files changed
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build:
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- LDPlayer 2 runtime:
  - host:
    - `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`
    - `build-vcpkg-local\\apps\\native_poc\\Debug\\remote60_native_video_host_poc.exe --bind-port 43000 --control-port 43001 --codec h264`
  - app:
    - `adb -s emulator-5558 install -r app-debug.apk`
    - `adb -s emulator-5558 shell am start -W -n com.remote60.androiddirect/.MainActivity --es host 192.168.0.76 --ei videoPort 43000 --ei controlPort 43001`
  - runtime log:
    - 이전 shell layout 기준 초기 viewport: `bind video surface buffer=928x86 video=0x0 view=928x86`
    - compact shell layout 적용 후 초기 viewport: `bind video surface buffer=936x254 video=0x0 view=936x254`
    - video size 수신 후 재바인딩: `bind video surface buffer=1234x720 video=1234x720 view=936x254`
    - decoder log: `updated codec config`, `MediaCodec started width=1234 height=720`
    - host log: `client connected transport=udp`, `[control] client connected`
- Scope note:
  - Android JNI에 `nativeGetVideoSizePacked`를 추가해 Kotlin이 decoder output size를 polling할 수 있게 했다.
  - `MainActivity`는 video size가 바뀌면 `TextureView.setDefaultBufferSize(...)` 기준으로 surface를 재바인딩하고 fit-center transform을 적용한다.
  - shell layout은 host/video/control 입력을 1행으로 압축해 LDPlayer 2 기준 video viewport 높이를 `86px -> 254px`로 늘렸다.
  - `uiautomator dump`가 idle state에서 반복 실패해 screenshot 기반 full-frame 최종 판정은 이번 턴에서 닫지 못했다.

Next action
- LDPlayer screenshot 또는 실기기에서 full-frame visible content 최종 확인을 마저 한다.
- 그 다음 `ClientSessionController -> JNI -> Kotlin` 제어 브리지와 `Phase E` 탭 UI 1차 구현으로 넘어간다.

### 148) 2026-04-06 android phase-e control bridge and target panel ui
Goal
- Android에 `window list/select`와 `Desktop Mode`를 붙일 최소 JNI 제어 브리지를 추가한다.
- `LDPlayer/Devices` 탭과 target selector를 붙여 `Phase E` 1차 UI를 시작한다.

Files changed
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build/test:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_client_shared_core_test --config Debug`
  - `d:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build:
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- LDPlayer 2 runtime:
  - app launch:
    - `adb -s emulator-5558 install -r app-debug.apk`
    - `adb -s emulator-5558 shell am start -W -n com.remote60.androiddirect/.MainActivity --es host 192.168.0.76 --ei videoPort 43000 --ei controlPort 43001`
  - host:
    - `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`
    - `build-vcpkg-local\\apps\\native_poc\\Debug\\remote60_native_video_host_poc.exe --bind-port 43000 --control-port 43001 --codec h264`
  - runtime log:
    - JNI/session bridge added: `nativeRequestWindowList`, `nativeSelectWindow`, `nativeSelectDesktopMode`, `nativeGetWindowPanelJson`
    - Android launch 후 connect tap 기준 host log: `client connected transport=udp`, `[control] client connected`
    - Android logcat 기준 viewport: `bind video surface buffer=936x172 video=0x0 view=936x172`
    - video size 수신 후 재바인딩: `bind video surface buffer=1234x720 video=1234x720 view=936x172`
- Scope note:
  - `ClientSessionController`에 window panel snapshot 복사와 refresh/select/Desktop Mode 요청 API를 추가했다.
  - Android JNI는 window panel 상태를 JSON으로 노출하고, Kotlin은 이를 polling해 `LDPlayer/Devices` 탭과 `Spinner` 기반 target selector를 그린다.
  - `selected target` 상태는 native status와 spinner label prefix(`*`)에 반영되도록 정리했다.
  - LDPlayer 자동 탭으로 connect/live video는 재현했지만, `Refresh/Desktop Mode/window select`의 최종 live verify는 이번 턴에서 닫지 못했다.

Next action
- LDPlayer screenshot/실기기 기준으로 `full-frame video`와 `Refresh/Desktop Mode/window select` live verify를 마저 한다.
- 그 다음 `Phase F` 입력 착수 전 gate를 재확인한다.

### 149) 2026-04-06 android connected compact mode
Goal
- 연결 후 상단 설정/타깃 패널을 기본으로 접어 video viewport를 더 확보한다.
- background host를 유지한 상태에서 compact mode screenshot으로 실제 viewport 개선을 확인한다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build:
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Background host:
  - process: `remote60_native_video_host_poc` PID `46776`
  - log: `d:\remote\remote\tmp\bg_host\host_stdout.log`
  - start command:
    - `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`
    - `build-vcpkg-local\\apps\\native_poc\\Debug\\remote60_native_video_host_poc.exe --bind-port 43000 --control-port 43001 --codec h264`
- LDPlayer 2 runtime:
  - connect screenshot:
    - `compact_mode_connected.png`
    - connected compact toolbar visible: `Show Panel / Disconnect / Refresh / Desktop Mode`
    - status: `connected window_list_received count=9 selected=desktop`
    - viewport: `view=936x310`
  - previous Phase E screenshot baseline:
    - connected full panel viewport: `view=936x172`
  - host log:
    - `window-select seq=1 requestedId=0 applied=1 selectedId=0 reason=desktop_mode_selected title=desktop`
- Scope note:
  - compact toolbar는 connected 상태에서만 보이고, full controls panel은 기본 collapse된다.
  - `Show Panel`로 host/port, target buttons, spinner를 다시 펼칠 수 있다.
  - background host는 유지 중이지만 현재 capture source가 `CreateForWindow(GetShellWindow())`로 fallback되어 있어 video는 계속 black이며, 이는 이번 client-only 작업 범위 밖의 runtime blocker다.

Next action
- valid capture source 환경에서 `full-frame video`와 `Refresh/Desktop Mode/window select` live verify를 다시 수행한다.
- 그 다음 `Phase F` 입력 착수 전 gate를 재확인한다.

### 150) 2026-04-06 android split scenes connect-targets-viewer
Goal
- Android UI를 `connect scene -> targets scene -> fullscreen viewer scene` 흐름으로 완전히 분리한다.
- viewer scene 좌상단에 숨겨진 back button을 두고 다시 targets scene으로 복귀하게 만든다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build:
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Background host:
  - process: `remote60_native_video_host_poc` PID `46776`
  - log: `d:\remote\remote\tmp\bg_host\host_stdout.log`
- LDPlayer 2 runtime screenshots:
  - connect scene:
    - `scene_connect2.png`
    - host/ports 입력 + `CONNECT` 버튼 확인
  - targets scene:
    - `scene_targets.png`
    - `Select A Target`, `DISCONNECT`, `WINDOWS/DEVICES/REFRESH`, window list 확인
  - viewer scene:
    - `scene_window_viewer.png`
    - selected window full-screen 표시 확인
    - 좌상단 hidden button `LIST` 확인
    - 하단 overlay status 확인
  - back to list:
    - `scene_back_to_list.png`
    - hidden back button 탭 후 targets scene 복귀 확인
- Runtime log / host log:
  - Android log: `MediaCodec started width=1234 height=720`
  - Android log: `bind video surface buffer=1234x720 video=1234x720 view=960x516`
  - Android log: `released output frame count=1`
  - host log: `[control] window-select seq=2 requestedId=67382 applied=1 selectedId=67382 reason=ok title=1`
  - host log: `capture item source=CreateForWindow(window-select)`
- Scope note:
  - connect scene에서는 host/port와 connect만 노출한다.
  - connect 성공 후 targets scene으로 이동하고, list item 탭 시 viewer scene으로 전환한다.
  - viewer scene에서는 좌상단 hidden back button 외의 제어를 제거해 전체 화면 viewer 구조를 유지한다.
  - desktop path는 환경에 따라 `GetShellWindow()` fallback이 남아 있어, desktop full-screen 검증은 별도 후속 항목으로 남긴다.

Next action
- desktop path capture source 검증을 분리해 `Devices/Desktop` scene 흐름도 안정화한다.
- 그 다음 `Phase F` 입력 착수 전 gate를 재확인한다.

### 164) 2026-04-10 desktop fullscreen dxgi backend split and ldplayer2 rerun
Goal
- desktop full-screen 경로를 DXGI desktop duplication으로 분리하고, window mode는 기존 WGC를 유지한다.
- `REMOTE60_DESKTOP_CAPTURE_BACKEND=dxgi|wgc` 토글과 desktop DXGI 실패 시 same-session WGC fallback을 넣는다.
- LDPlayer 인스턴스 `2`(`emulator-5558`)에 Android direct client를 재설치하고 desktop path를 다시 붙여 본다.

Files changed
- `apps/host/CMakeLists.txt`
- `apps/host/src/capture_backend_dxgi.hpp`
- `apps/host/src/capture_backend_dxgi.cpp`
- `apps/host/src/realtime_runtime.cpp`
- `apps/native_poc/CMakeLists.txt`
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Host build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --config Debug --target remote60_host`
  - 결과: 성공
- Native host build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc`
  - 결과: 성공
- Native localhost smoke:
  - host:
    - `REMOTE60_DESKTOP_CAPTURE_BACKEND=dxgi`
    - `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`
    - `remote60_native_video_host_poc.exe --bind-port 43000 --control-port 43001 --transport udp --codec h264 --fps 30`
  - client:
    - `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`
    - `remote60_native_video_client_poc.exe --host 127.0.0.1 --port 43000 --control-port 43001 --codec h264 --fps-hint 30 --seconds 3`
  - 결과:
    - client는 `control connected`, `window-list seq=1 count=13`까지 성공
    - host는 `desktop_backend=dxgi capture=2112x1232`까지 진입했지만, 현재 세션에서 `fallback_reason=dxgi_no_output_found`로 내려가 `CreateForWindow(GetShellWindow())` WGC desktop fallback이 걸렸다.
- LDPlayer 2 rerun:
  - APK reinstall:
    - `C:\Users\shota\AppData\Local\Android\Sdk\platform-tools\adb.exe -s emulator-5558 install -r d:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
    - 결과: 성공
  - launch:
    - `adb -s emulator-5558 shell am start -W -n com.remote60.androiddirect/.MainActivity --es host 192.168.0.76 --ei videoPort 43000 --ei controlPort 43001`
    - 결과: 성공
  - runtime:
    - `dumpsys window windows` 기준 `mCurrentFocus=com.remote60.androiddirect/.MainActivity`
    - session log에서 `connect_tap -> connected window_list_received count=12 selected=desktop` 1회 재확인
    - 다만 이번 DXGI build 재기동 이후 동일 LDPlayer 재연결은 `connect_tap -> status=error`로 끝나 desktop viewer 진입까지는 닫지 못함
- Scope note:
  - `apps/host`와 `apps/native_poc` 모두 desktop mode에서 DXGI desktop duplication을 우선 시도하고, window mode는 기존 WGC를 그대로 유지한다.
  - 현재 RDP 세션에서는 DXGI output enumeration이 실제 primary output을 못 잡아 `dxgi_no_output_found -> WGC ShellWindow fallback`이 남는다.

Next action
- 실제 console/physical desktop 또는 output enumeration이 살아 있는 세션에서 `desktop_backend=dxgi`가 fallback 없이 유지되는지 다시 검증한다.
- LDPlayer 2에서 `Desktop` 탭 진입 후 viewer first-frame까지 이어지는 재연결 불안정성을 분리해 다시 본다.

### 170) 2026-04-10 portrait/landscape viewer aspect-fit parity
Goal
- 세로 창을 선택해도 Windows/Android 클라이언트가 가로로 강제 stretch 하지 않도록 viewer 비율 처리를 맞춘다.
- 선택 직후에는 타깃 해상도 힌트를 쓰고, 첫 디코드 프레임 이후에는 실제 프레임 크기로 자연스럽게 전환한다.

Files changed
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Windows native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_client_poc remote60_native_video_client_shared_core_test`
  - 결과: 성공
- Shared core test:
  - `d:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: PASS
- Android build:
  - `set JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `d:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Scope note:
  - wire protocol은 그대로 두고, 기존 window list entry의 `width/height`를 shared snapshot의 `selectedWidth/selectedHeight`로 승격했다.
  - Windows native client는 실제 프레임 세대가 새 selection과 맞지 않으면 선택 타깃 비율을 우선해 letterbox/pillarbox rect를 계산하고, 동일 rect를 입력 좌표 매핑에도 재사용한다.
  - Android client는 선택 요청 직후 예상 타깃 크기를 `TextureView` transform/buffer 크기 힌트로 사용하고, 디코더 output size가 들어오면 실제 frame size로 전환한다.
  - diagnostics log에는 expected content size와 decoded video size를 함께 남기도록 보강했다.

Next action
- LDPlayer/실기기에서 `1000x575`와 `580x995`를 각각 선택해 실제 viewer screenshot으로 letterbox/pillarbox 동작을 확인한다.
- 만약 portrait target 선택 후에도 decoded size가 landscape로 고정되면 host capture/encode 경로에서 세대별 실제 frame size를 추가 조사한다.

### 169) 2026-04-10 android debug build + visible host launch
Goal
- 사용자가 바로 동작 테스트할 수 있도록 최신 Android debug APK를 다시 빌드하고, native video host를 로그가 보이는 별도 PowerShell 창으로 실행한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test`
  - 결과: 성공
- Shared core test:
  - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat -p D:\remote\remote\apps\android_direct_client assembleDebug`
  - 결과: 성공
  - APK: `D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - APK timestamp: `2026-04-10 14:06:03`
- Host visible launch:
  - process: `remote60_native_video_host_poc.exe`
  - PID: `57056`
  - mode: visible PowerShell window, `h264 + udp`, port `43000`, control port `43001`
  - env: `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`, `REMOTE60_NATIVE_H264_NO_PACING=1`, `REMOTE60_NATIVE_FRAME_GATING_DISABLE=1`, `REMOTE60_NATIVE_ABR_DISABLE=1`, `REMOTE60_NATIVE_ENCODER_BACKEND=mft_auto`
- ADB/device state:
  - `C:\Users\shota\AppData\Local\Android\Sdk\platform-tools\adb.exe devices`
  - 결과: `emulator-5554 offline`
  - 따라서 APK 자동 설치는 수행하지 않음

Next action
- 에뮬레이터 또는 실기기 `adb` 상태가 `device`로 올라오면 최신 `app-debug.apk`를 설치한다.
- 현재 떠 있는 visible host 창 상태에서 Android client를 연결해 수동 동작 테스트를 진행한다.

### 168) 2026-04-10 D3D capture/scaler contention mitigation v1
Goal
- capture readback와 GPU scaler가 같은 D3D11 immediate context를 오래 점유하는 구간을 줄여 host-side buffering 악화 가능성을 낮춘다.
- `single staging` 구조에서 바로 lock만 쪼개지 않고, safe staging slot ring과 readback timing metrics를 함께 넣어 회귀 가능성을 낮춘다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `automation/verify_native_video_runtime.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native Debug build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test`
  - 결과: 성공
- Shared core test:
  - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Runtime smoke 1:
  - `automation/verify_native_video_runtime.ps1 ... -GateAProfile -TraceEvery 30 -TraceMax 6`
  - 결과: 실패
  - 원인: capture source가 `CreateForWindow(GetShellWindow())`로 fallback되어 callback이 생성되지 않음
- Runtime smoke 2:
  - host/client 직접 실행 + `--capture-window-pid 13608` (`Codex Plan - Server - Visual Studio Code`)
  - 결과: control attach는 성공했지만 capture callback이 들어오지 않아 `capture session restarted` 반복
- Runtime smoke 3:
  - host/client 직접 실행 + `--capture-window-pid 39980` (Chrome)
  - 결과: target lookup 실패로 monitor fallback, 이후 `GetShellWindow()` capture source로 callback 미생성
- Scope note:
  - capture callback은 shared staging 단일 객체 대신 slot ring에서 free slot을 점유한 뒤 `CopyResource -> Map`만 lock 안에서 수행하고, memcpy는 lock 밖에서 처리한다.
  - `GpuBgraScaler`는 내부 `dstStaging`이 encode thread 단독 소유이므로 같은 방식으로 `Map` memcpy 구간을 lock 밖으로 이동했다.
  - host trace/user-feedback와 verify parser에 `captureD3DWaitUs`, `captureCopyMapUs`, `captureMemcpyUs`, `captureUnmapWaitUs`, `scaleD3DWaitUs`, `scaleCopyMapUs`, `scaleMemcpyUs`, `scaleUnmapWaitUs` 필드를 추가했다.

Next action
- interactive desktop 세션에서 `non-ShellWindow` capture source를 명시적으로 잡아 새 timing 필드가 실제로 찍히는 smoke를 다시 수행한다.
- 그 뒤 `cb2eAvgUs`와 새 D3D wait/copy/memcpy 지표를 기준으로 개선 여부를 판정한다.

### 167) 2026-04-09 buffering/gpu contention analysis review
Goal
- `docs/버퍼링_GPU경합_분석_20260409.md`의 주장 중 현재 코드 기준으로 인정 가능한 부분과 인정하기 어려운 부분을 분리한다.
- 인정하는 부분을 실제 후속 수정 방향으로 어떻게 바꿔 써야 하는지 별도 검토 문서로 남긴다.

Files changed
- `docs/버퍼링_GPU경합_검토_20260409.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Static code inspection only:
  - `docs/버퍼링_GPU경합_분석_20260409.md` 본문 검토
  - `apps/native_poc/src/native_video_host_main.cpp`에서 D3D device/context 생성, `d3dContextMu`, capture callback, `GpuBgraScaler::scale()` 구간 대조
  - `apps/native_poc/src/mf_h264_codec.cpp`에서 `H264Encoder::set_d3d11_device()`, `encode_frame()` 입력 샘플 생성 경로 대조
- Conclusion:
  - capture readback + GPU scaler shared immediate context/mutex 경합 가능성은 인정
  - MFT encoder가 같은 mutex direct contender라는 주장, 단일 staging 구조에서 안 (C)가 바로 안전하다는 주장, 런타임 근거 없는 원인 확정 톤은 비인정
- Build/test:
  - 문서화 작업만 수행
  - 추가 빌드/런타임 테스트 없음

Next action
- 실제 수정에 들어가려면 먼저 capture/scaler의 `d3dContextMu` wait/hold 시간을 분리 계측한다.
- short-term fix는 단일 staging 공유를 유지한 채 memcpy만 lock 밖으로 빼는 방식이 아니라, staging ring/ownership 분리까지 포함해 설계한다.

### 166) 2026-04-09 H264 stability hardening follow-up
Goal
- `docs/h264_코드리뷰_20260409.md`에서 지적된 즉시 대응 항목(C1/C2/C3)과 후속 안정성 항목(H1/H2)을 현재 코드에 반영한다.
- silent corruption과 oversized payload/input에 대한 무방비 경로를 없애고, Windows/native와 Android 양쪽에 최소 진단을 남긴다.

Files changed
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp`
- `apps/native_poc/src/mf_h264_codec.cpp`
- `apps/native_poc/src/mf_h264_codec.hpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native Debug build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test`
  - 결과: 성공
- Shared core test:
  - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
  - 추가 검증: oversized UDP payload가 `Malformed + oversizePayload + rejectedPayloadSize`로 거부됨
- Localhost H264 smoke:
  - `powershell -ExecutionPolicy Bypass -File D:\remote\remote\automation\verify_native_video_runtime.ps1 -Root D:\remote\remote -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 8 -Bitrate 5000000 -Keyint 60 -EncodeWidth 1280 -EncodeHeight 720 -EncoderBackend mft_auto -DecoderBackend mft_auto -NoInputChannel -GateAProfile`
  - log: `D:\remote\remote\automation\logs\verify-native-video-20260409-160801`
  - 결과: `HOST_RC=0`, `CLIENT_RC=0`, `OVERALL_OK=True`, `GATE_A_PASS=True`, `UDP_ASSEMBLY_MALFORMED_TOTAL=0`
- Android build/runtime:
  - 실행 안 함
  - 사유: 저장소에 `gradlew`가 없고 현재 셸에서 `gradle`도 사용 불가

Next action
- Android Studio 기준 `:app:assembleDebug`와 `connect -> select -> viewer` 1회로 oversized input log/drop 경로를 실제 런타임에서 확인한다.
- 그 다음 남은 Android 실기기 검증(`Phase F tap/drag`, `soft keyboard`)과 별도로 desktop capture source/soak 항목을 이어간다.

### 165) 2026-04-08 android phase-f keyboard button and soft text bridge
Goal
- viewer에 keyboard 모양 버튼을 추가해 soft keyboard를 바로 띄울 수 있게 한다.
- hidden IME capture view를 통해 `committed text`와 기본 특수키를 기존 control input 경로로 보낸다.
- 메뉴 버튼은 상시 노출 대신 top rail이 옅게 남아 있다가 터치 시 다시 또렷해지는 방식으로 정리한다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/ImeCaptureView.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `apps/android_direct_client/app/src/main/res/drawable/viewer_control_bar_background.xml`
- `apps/android_direct_client/app/src/main/res/drawable/viewer_control_button_background.xml`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Shared core/native client build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_client_shared_core_test --parallel`
  - 결과: 성공
- Android JNI/native bridge rebuild:
  - `cmake --build d:\remote\remote\apps\android_direct_client\app\.cxx\Debug\3m5je1h6\x86_64 --parallel`
  - `cmake --build d:\remote\remote\apps\android_direct_client\app\.cxx\Debug\3m5je1h6\arm64-v8a --parallel`
  - 결과: 둘 다 성공
- Android resource compile:
  - `aapt2 compile --dir d:\remote\remote\apps\android_direct_client\app\src\main\res -o d:\remote\remote\tmp\android-direct-res.zip`
  - 결과: 성공
- Full APK rebuild:
  - 실행 안 함
  - 사유: 저장소에 `gradlew`가 없고 로컬 `gradle`도 PATH에 없음

Next action
- LDPlayer/실기기에서 keyboard 버튼 -> IME open -> committed text/backspace/enter가 실제 host 입력으로 반영되는지 확인한다.
- 그 다음 `Desktop Mode`, selected-window mode 각각에서 touch + text를 묶어 Phase F runtime verify를 닫는다.

### 164) 2026-04-08 android phase-f touch input bridge
Goal
- Android direct client viewer에서 안 먹던 `tap/drag` 입력을 기존 control input 경로로 연결한다.
- scene 전환, pause, cancel 시 left button이 눌린 채 남지 않도록 release guard를 추가한다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Shared core/native client build:
  - `cmake --build --preset debug-vcpkg --target remote60_native_video_client_shared_core_test --parallel`
  - 결과: 성공
- Android JNI/native bridge rebuild:
  - `cmake --build d:\remote\remote\apps\android_direct_client\app\.cxx\Debug\3m5je1h6\x86_64 --parallel`
  - `cmake --build d:\remote\remote\apps\android_direct_client\app\.cxx\Debug\3m5je1h6\arm64-v8a --parallel`
  - 결과: 둘 다 성공
- Android Gradle/APK rebuild:
  - 실행 안 함
  - 사유: 저장소에 `gradlew`가 없고 로컬 `gradle`도 PATH에 없음

Next action
- LDPlayer/실기기에서 `Desktop Mode`, selected-window 각각 tap/drag가 실제 host 입력으로 반영되는지 확인한다.
- 그 다음 `committed text -> existing UTF-16 text message` 브리지를 같은 세션 컨트롤러 경로에 추가한다.

### 156) 2026-04-07 android selection generation gating and ldplayer fps investigation
Goal
- Android target 전환을 `request -> ack -> first-frame -> viewer` 상태기계로 고정해 stale frame 섞임과 재선택 freeze를 줄인다.
- host/window-select 경로에 stream generation, capture flush, first callback/frame 로그를 넣어 전환 경계를 명확히 한다.
- LDPlayer에서 보이던 저프레임이 host 송신 병목인지 emulator/client 병목인지 기준선을 잡아 확인한다.

Files changed
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/poc_protocol.hpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc remote60_native_video_client_poc --config Debug --parallel`
  - 결과: 성공
- Shared core test:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_client_shared_core_test --config Debug --parallel`
  - `d:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `d:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Windows baseline probe:
  - host log: `d:\remote\remote\tmp\ld_fps_probe\host.out.log`
  - client log: `d:\remote\remote\tmp\ld_fps_probe\windows_client.out.log`
  - `frameGating=off`, `abr=off` 기준 host average:
    - `callbackFrames ~= 30.71 fps`
    - `sentFrames ~= 20.00 fps`
  - same host에서 Windows native client average:
    - `recvFrames ~= 18.71 fps`
    - `decodedFrames ~= 14.57 fps`
- LDPlayer fps investigation:
  - logcat dump: `d:\remote\remote\tmp\ld_logcat.txt`
  - prior LDPlayer Android session(`size=1280x720`) sample window:
    - `13:32:22.031 -> 13:33:46.882`
    - `in/out delta = 802 / 84.851s`
    - `Android in/out ~= 9.45 fps`
  - same day slow/stall sample:
    - `13:29:33.269 -> 13:31:28.311`
    - `in/out delta = 58 / 115.042s`
    - `Android in/out ~= 0.50 fps`
  - fresh rebuilt APK live rerun은 LDPlayer에서 `connect_tap -> status=connecting -> status=error`로 끝났고, 새 host에는 connect event가 찍히지 않았다.
  - 결론:
    - 기존 LDPlayer low-fps 현상은 host가 20fps 안팎으로 보내던 조건에서도 Android emulator 쪽 `in/out`이 9~10fps 수준으로 묶인 로그가 있어, host만의 병목으로 보기 어렵다.
    - 특히 low-fps 구간에서 `in`과 `out`이 거의 같이 움직여 decoder drop보다 emulator/client-side scheduling 또는 capture/render 환경 영향이 더 커 보인다.
    - 위 결론은 today log evidence 기반 추론이며, rebuilt app/live rerun은 네트워크 경로 문제 때문에 재확인하지 못했다.
- Scope note:
  - Android decoder는 pending local selection generation과 host stream generation이 맞는 프레임만 받도록 바뀌었다.
  - viewer는 `SWITCHING` scene에서 surface를 먼저 붙이고, first-frame ready generation이 확인될 때만 실제 viewer로 노출된다.
  - host는 window-select 성공 시 capture pipeline을 flush하고, 새 generation 기준 first callback/frame 로그를 남긴다.

Next action
- LDPlayer에서 rebuilt APK live rerun이 다시 붙도록 host inbound path(방화벽/포트 경로)를 확인한 뒤, `targets -> viewer -> back` 10회 이상 soak으로 freeze 재현 여부를 다시 본다.
- `REMOTE60_NATIVE_WINDOWLIST_EXCLUDE_PIDS`를 current Android client instance와 자동 동기화하는 경로를 남은 별도 작업으로 마무리한다.

### 157) 2026-04-07 android settings tab and runtime bitrate-fps control
Goal
- Android target scene를 `Windows / Desktop / Settings` 3탭으로 나눠 target 선택과 품질 조절 공간을 분리한다.
- client control channel에서 host `bitrate / fps`를 런타임에 바꿔 host process 재시작 없이 encoder target을 바꿀 수 있게 한다.

Files changed
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/poc_protocol.hpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug --parallel`
  - 결과: 성공
- Shared core test:
  - `d:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `d:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Scope note:
  - `ControlRuntimeEncoderConfigMessage`에 `fps`와 `flags bit2`를 추가했다.
  - host는 runtime control 수신 시 `apply_encoder_target(...)`로 bitrate/keyint/fps를 갱신하고, host process 자체는 재시작하지 않는다.
  - bitrate-only 변경은 가능한 경우 encoder bitrate reconfigure만 타고, fps 변경은 encoder 재초기화가 있을 수 있으나 host process restart는 아니다.
  - Android UI는 기존 `Devices` 의미를 `Desktop`으로 명확히 바꾸고, `Settings` 탭에서 bitrate kbps / fps 값을 입력 후 apply하도록 했다.

Next action
- 실기기에서 `Settings` 탭으로 bitrate/fps를 바꾼 뒤 체감 화질/트래픽 tradeoff를 몇 개 프리셋으로 정리한다.
- 이후 `targets -> viewer -> back` soak과 LD current-instance exclude 자동화를 이어서 닫는다.

### 158) 2026-04-07 android settings persistence and viewer-only host streaming
Goal
- Android `Settings` 탭 값(bitrate/fps)을 endpoint처럼 저장해 다음 접속에서도 자동으로 host에 적용한다.
- viewer를 벗어나 targets/list scene으로 돌아가면 host가 계속 video를 보내지 않도록 client-host stream active control을 추가한다.

Files changed
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SessionPersistence.kt`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `apps/native_poc/src/native_video_client_tcp_control.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/poc_protocol.hpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug --parallel`
  - 결과: 성공
- Shared core test:
  - `d:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `d:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Scope note:
  - `SessionPersistence`에 `bitrateKbps/fps`를 추가해 endpoint와 함께 저장/복원한다.
  - Android client는 connect 성공 후 saved bitrate/fps를 한 번 자동으로 host runtime config로 보내고, 이후 viewer/switching에서는 `stream active=true`, targets/list에서는 `stream active=false`를 보낸다.
  - host는 `ControlStreamState` 수신 시 encode/send loop를 멈추고, stream 재활성화 시 keyframe을 강제한다.
  - host process 자체를 내리지 않고 stream on/off와 runtime config만 바꾼다.

Next action
- 실기기에서 `viewer -> list` 전환 후 host 트래픽이 실제로 멈추는지 로그/네트워크 지표로 한번 확인한다.
- saved bitrate/fps auto-apply가 connect 직후 체감 화질에 반영되는지 실기기에서 재확인한다.

### 159) 2026-04-07 host stream-inactive stall guard
Goal
- `stream active=false` 상태에서 host가 `capture-input-stall`로 반복 restart하지 않도록 막아 list scene idle 상태를 안정화한다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug --parallel`
  - 결과: 성공
- Shared core test:
  - `d:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Runtime restart:
  - host PID: `61048`
  - host log: `d:\remote\remote\tmp\android_live_host\host.out.log`
- Scope note:
  - `stream active=false`일 때 capture callback stall watchdog과 low-push restart 경로를 함께 건너뛰도록 바꿨다.
  - list scene idle 상태에서는 host가 stream을 멈춘 채 불필요한 capture session restart를 반복하지 않는다.

Next action
- 실기기에서 다시 `connect -> windows list -> desktop/window select`를 확인해 control channel 안정성이 실제로 좋아졌는지 본다.
- 이어서 `viewer -> list` 전환 시 트래픽/host log가 예상대로 quiet 상태로 유지되는지 확인한다.

### 160) 2026-04-07 stream-state ordering and list refresh fix
Goal
- 첫 접속 후 window list는 보이지만 window 선택/복귀 뒤 목록이 다시 안 보이던 회귀를 줄인다.
- `stream active`와 `window select` 제어 순서를 바로잡고, list 복귀 시 window list를 다시 요청해 target scene을 안정화한다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test --config Debug --parallel`
  - 결과: 성공
- Shared core test:
  - `d:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `d:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Runtime restart:
  - host PID: `54960`
  - host log: `d:\remote\remote\tmp\android_live_host\host.out.log`
- Scope note:
  - control scheduler에서 `stream-state`를 `window list / window select`보다 먼저 보낸다.
  - Android `LIST` 복귀와 `Windows` 탭 진입 시 `nativeRequestWindowList()`를 다시 보내 목록을 재동기화한다.
  - host는 `window-list seq=... count=...` 로그를 남겨 이후 재현 시 control/list 경계를 바로 볼 수 있게 했다.

Next action
- 실기기에서 `connect -> windows list -> select -> LIST -> windows list`를 다시 확인해 회귀가 사라졌는지 본다.
- 여전히 control TCP가 끊기면, Android diagnostics/logcat을 받아 control disconnect 원인을 추가 추적한다.

### 161) 2026-04-08 ldplayer direct deploy verify after stream/list fixes
Goal
- LDPlayer에 최신 APK를 직접 설치해 `Connect -> Windows list -> select -> viewer -> LIST -> list`가 실제로 복구됐는지 확인한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Deploy:
  - `adb -s emulator-5558 install -r D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - 결과: 성공
- Host runtime:
  - host PID: `54960`
  - log: `d:\remote\remote\tmp\android_live_host\host.out.log`
- LDPlayer runtime:
  - connect screenshot: `d:\remote\remote\tmp\ld_after_connect.png`
    - `window_list_received count=14`
  - select screenshot: `d:\remote\remote\tmp\ld_after_select.png`
    - `select_request targetId=67382`
    - `select_ack streamGen=2`
    - `select_ready`
    - `scene=VIEWER`
    - `video_debug ... in=62 out=4 -> in=1118 out=1046`
  - back-to-list screenshot: `d:\remote\remote\tmp\ld_after_back.png`
    - `viewer_back`
    - `targets_return reason=viewer_back`
    - `stream_state_request active=false`
    - `window_list_request pending`
    - `window_list_received count=14`
- Host log confirms same flow:
  - `[control] window-list seq=1 count=14`
  - `[control] stream-state seq=2 active=1`
  - `[control] window-select seq=1 ... streamGen=2`
  - selected target viewer streaming continued with steady `sentFrames`
- Scope note:
  - latest fixes restored target list visibility after viewer roundtrip on LDPlayer.
  - selection to viewer and viewer back to list both reproduced directly on emulator, not inferred from code only.

Next action
- 실기기에서도 같은 roundtrip이 유지되는지 한 번 더 확인한다.
- 이후 `Desktop` path와 traffic stop behavior를 실제 네트워크 지표로 다시 본다.

### 162) 2026-04-08 android fullscreen/back/rotation polish
Goal
- Android direct client를 폴리싱 단계로 올리기 위해 viewer/fullscreen, 시스템 뒤로가기, 회전 안정성을 함께 정리한다.

Files changed
- `apps/android_direct_client/app/src/main/AndroidManifest.xml`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build:
  - `$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'; $env:Path=\"$env:JAVA_HOME\\bin;$env:Path\"; & d:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Runtime note:
  - 이번 턴에서는 LDPlayer/실기기 수동 검증은 아직 수행하지 않음

Scope note
- `MainActivity`가 resume/focus/config change마다 immersive fullscreen을 다시 적용해 상단 상태바/시스템 바가 기본적으로 숨겨진다.
- 시스템 뒤로가기 입력을 scene-aware로 라우팅해 viewer/switching에서는 list로 복귀하고, connect/targets에서는 `종료하시겠습니까?` 확인 다이얼로그를 띄운다.
- `AndroidManifest.xml`에 `configChanges`를 추가해 회전 시 액티비티 재생성을 막고, 구성 변경 시 surface/UI를 재동기화한다.
- `renderStatus()`가 네이티브 세션 상태로 `connectFlowActive`를 복원하도록 보강해 lifecycle 경계에서도 UI scene 복구 여지를 늘렸다.

Next action
- LDPlayer나 실기기에서 `connect -> select -> viewer -> system back -> list`와 `connect/list -> system back -> exit dialog`를 직접 확인한다.
- 세로/가로 전환 중 연결 유지와 viewer surface 재바인딩이 실제 장비에서 안정적인지 추가 검증한다.

### 163) 2026-04-08 ldplayer fullscreen and back-flow verify
Goal
- LDPlayer에서 Android direct client의 fullscreen/back UX를 직접 눌러 확인한다.
- connect scene과 targets scene의 종료 팝업, fullscreen 복원, system back 복귀 경로를 검증한다.

Files changed
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- LDPlayer runtime:
  - device: `emulator-5558`
  - app launch: `adb -s emulator-5558 shell am start -W -n com.remote60.androiddirect/.MainActivity --es host 192.168.0.76 --ei videoPort 43000 --ei controlPort 43001`
  - connect scene screenshot:
    - `d:\remote\remote\tmp\ld_verify_app_connect2.png`
    - 상태바 없는 fullscreen shell 확인
  - targets scene screenshot:
    - `d:\remote\remote\tmp\ld_verify_targets.png`
    - `connected window_list_received count=14`
  - targets exit dialog:
    - `d:\remote\remote\tmp\ld_verify_targets_exit_dialog.png`
    - `종료하시겠습니까?` + `NO/YES` 확인
  - targets dialog dismiss after `NO`:
    - `d:\remote\remote\tmp\ld_verify_targets_after_no.png`
    - fullscreen targets scene 복원 확인
  - switching/system back path:
    - `d:\remote\remote\tmp\ld_verify_back_from_switching.png`
    - log:
      - `viewer_back reason=system_back`
      - `targets_return reason=system_back`
      - `window_list_received count=14`
  - connect scene exit dialog:
    - `d:\remote\remote\tmp\ld_verify_connect_exit_dialog.png`
    - `종료하시겠습니까?` + `NO/YES` 확인
- Runtime limitation:
  - selected-window path 재검증 중 `AGENTS.md - remote - Visual Studio Code` 선택은 `select_timeout`으로 viewer first frame까지는 재도달하지 못함
  - log:
    - `select_request targetId=1903738`
    - `select_ack streamGen=12`
    - `select_timeout ... codec=off in=0 out=0`

Scope note
- connect scene과 targets scene에서 뒤로가기 종료 팝업은 직접 캡처로 확인했다.
- targets scene에서 `NO`를 누르면 dialog 종료 뒤 fullscreen list 화면으로 복원된다.
- viewer 전환 중(`SWITCHING`) system back은 list 복귀로 정상 라우팅되며 reconnect 없이 window list가 다시 채워진다.
- 이번 턴에서는 rotation은 LDPlayer에서 강제 재현하지 못했고, user가 별도로 회전 정상 동작을 확인했다고 전달함.

Next action
- viewer first-frame가 재현되는 대상(window 또는 desktop)을 기준으로 `VIEWER` 진입 상태의 system back까지 다시 한 번 확인한다.
- user가 확인한 rotation 결과를 포함해 실기기 기준 최종 폴리싱 체크를 마무리한다.

### 152) 2026-04-06 host window-capture stall false-positive guard
Goal
- static window를 캡처할 때 `callbackFramesPerSec < 10`만으로 freeze로 오판정해 restart하는 문제를 줄인다.
- recursive emulator target filter 이후에도 남아 있던 반복 freeze를 host stall 정책에서 완화한다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc --config Debug`
  - 결과: 성공
- Background host restart:
  - new host PID: `32032`
  - log: `d:\remote\remote\tmp\bg_host\host_stdout.log`
- Runtime observation:
  - 수정 전:
    - selected window가 static일 때 `callbackFramesPerSec=2..9` 구간만으로
      `capture session restarted reason=capture-input-stall`
      가 반복 발생
    - 예시 target: `unity hub.exe`
  - 수정 후:
    - window capture mode에서는 low-push 기준 restart가 비활성화됨
    - host steady log에서 `captureDeadRestartCount=0`, `captureTargetProc=monitor` 상태 유지 확인
    - Android reconnect 후 targets scene과 viewer 진입 재확인
- Scope note:
  - hard stall guard(`lastCallbackUs` 기반 3초 이상 무응답 restart)는 그대로 유지했다.
  - 이번 수정은 `window capture mode`에서만 false-positive restart를 막는 목적이다.
  - 아직 장시간 반복 soak은 별도 검증 항목으로 남긴다.

Next action
- `targets -> viewer -> back` 반복 soak을 추가로 돌려 장시간 freeze 재현 여부를 본다.
- desktop path capture source 검증을 분리해 `Devices/Desktop` 흐름도 안정화한다.

### 154) 2026-04-06 ldplayer list restore with pid-scoped exclude
Goal
- `LDPlayer`를 targets 목록에 다시 보이게 한다.
- blanket `dnplayer.exe` 제외를 풀고, 현재 Android client를 띄운 LDPlayer instance만 좁게 제외한다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc --config Debug`
  - 결과: 성공
- Background host restart:
  - current host PID: `47284`
  - env: `REMOTE60_NATIVE_WINDOWLIST_EXCLUDE_PIDS=15124`
  - note:
    - `dnplayer.exe index=1` PID `8256`
    - `dnplayer.exe index=2` PID `15124`
    - current Android client instance는 `index=2` / PID `15124`로 보고 해당 PID만 제외
- LDPlayer 2 runtime:
  - targets scene screenshot:
    - `ldplayer_list_back.png`
    - `window_list_received count=9`
    - `1 • 1000x575` LDPlayer window가 목록에 다시 노출됨
  - viewer screenshot:
    - `ldplayer_viewer.png`
    - LDPlayer game content가 full-screen viewer에 실제 표시됨
- Scope note:
  - blanket `dnplayer.exe` exclusion은 과도했으므로 되돌리고, `REMOTE60_NATIVE_WINDOWLIST_EXCLUDE_PIDS` 기반으로 현재 client instance만 제외하는 방식으로 운영했다.
  - `textinputhost.exe` exclusion은 유지한다.
  - 현재는 session-local env 방식이라 emulator instance PID가 바뀌면 host 재기동 시 다시 맞춰야 한다.

Next action
- `REMOTE60_NATIVE_WINDOWLIST_EXCLUDE_PIDS`를 current emulator instance와 자동 동기화하는 방식으로 다듬는다.
- 그 다음 `targets -> viewer -> back` 반복 soak을 추가로 돌려 장시간 freeze 재현 여부를 본다.

### 155) 2026-04-07 android endpoint persistence diagnostics and second-selection guard
Goal
- Android에서 마지막으로 사용한 `host/videoPort/controlPort`를 다음 실행에도 복원한다.
- freeze 분석용 diagnostics log file을 남기고, 첫 선택 후 다른 윈도우 재선택 시 viewer 진입을 안정화한다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SessionPersistence.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SessionDiagnosticsLog.kt`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build:
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Persistence verify:
  - app relaunch without extras 기준 screenshot `persist_check.png`
  - host `192.168.0.76`, ports `43000/43001`이 복원됨
- Diagnostics log verify:
  - file path: `/storage/emulated/0/Android/data/com.remote60.androiddirect/files/android_direct_client_session.log`
  - pulled file: `d:\remote\remote\tmp\android_direct_client_session.log`
  - log contains:
    - `app_start`
    - `connect_tap`
    - `select_request`
    - `select_applied`
    - `viewer_surface_bound`
    - `video_debug`
- Second-selection verify:
  - first viewer screenshot: `select_first.png` (`1 • 1000x575`)
  - second viewer screenshot: `select_second.png` (`Codex`)
  - diagnostics log shows second selection path:
    - `select_request targetId=330458`
    - `select_applied title=Codex`
    - `video_debug ... out=18 out=2` progression
  - short repeat scenario:
    - `repeat_after_android_fix.png`
    - same session에서 `viewer -> list -> viewer` 왕복 후 최종 viewer 유지 확인
- Scope note:
  - persistence는 `SharedPreferences`로 저장한다.
  - diagnostics는 `android_direct_client_session.log`에 append하며, viewer freeze 의심 시 `viewer_stall` 이벤트를 남긴다.
  - list item tap 시 바로 viewer scene으로 들어가지 않고, `window_select_requested`가 실제 `selectedId`에 반영된 뒤 viewer로 전환한다.
  - target switch 시 `nativeResetVideoStream()`으로 decoder를 초기화해 이전 프레임 잔상/오염을 줄인다.

Next action
- `REMOTE60_NATIVE_WINDOWLIST_EXCLUDE_PIDS`를 current emulator instance와 자동 동기화하는 방식으로 다듬는다.
- 그 다음 `targets -> viewer -> back` 반복 soak을 추가로 돌려 장시간 freeze 재현 여부를 본다.

### 153) 2026-04-06 host utility window filter extension
Goal
- recursive freeze와 잘못된 선택을 줄이기 위해 shareable windows 목록에서 helper window를 더 제외한다.
- `textinputhost.exe`가 Android targets scene에 섞여 들어오는 케이스를 막는다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc --config Debug`
  - 결과: 성공
- Background host restart:
  - new host PID: `36640`
  - log: `d:\remote\remote\tmp\bg_host\host_stdout.log`
- LDPlayer 2 runtime:
  - targets scene screenshot:
    - `filter_targets2.png`
    - `window_list_received count=8`
    - recursive/self candidate로 보이던 emulator/input helper window가 목록에서 제거됨
- Scope note:
  - `should_exclude_recursive_window_process()`에 `textinputhost.exe`를 추가했다.
  - 현재 targets scene 목록은 일반 top-level windows 위주로 유지된다.
  - 장시간 반복 soak은 아직 별도 검증으로 남겨둔다.

Next action
- `targets -> viewer -> back` 반복 soak을 추가로 돌려 장시간 freeze 재현 여부를 본다.
- desktop path capture source 검증을 분리해 `Devices/Desktop` 흐름도 안정화한다.

### 151) 2026-04-06 host recursive emulator target filter
Goal
- Android viewer freeze 원인이던 recursive capture 경로를 줄인다.
- LDPlayer 창(`dnplayer.exe`)이 shareable windows에 섞여 선택되는 것을 host에서 막는다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/android_구현계획.md`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build d:\remote\remote\build-vcpkg-local --target remote60_native_video_host_poc --config Debug`
  - 결과: 성공
- Background host restart:
  - old host stop 후 new host PID: `40348`
  - log: `d:\remote\remote\tmp\bg_host\host_stdout.log`
- LDPlayer 2 runtime:
  - targets scene screenshot:
    - `filter_targets.png`
    - `window_list_received count=7`
    - 이전에 보이던 recursive candidate(`1 • 1000x575` / emulator window)가 목록에서 제거됨
  - safe viewer screenshot:
    - `safe_viewer.png`
    - selected non-emulator window가 정상 표시됨
  - host log:
    - filter 적용 후 steady state에서 `captureTargetProc=monitor`, `captureDeadRestartCount=0`
    - selected-window path에서도 emulator 대신 일반 window 위주로 선택 가능
  - Android log:
    - `MediaCodec started width=1280 height=720`
    - `bind video surface buffer=1280x720 video=1280x720 view=960x516`
    - `released output frame count=151`
- Scope note:
  - `should_include_window()`에서 `dnplayer.exe`, `dnmultiplayer.exe`, `ldplayer.exe`, `hd-player.exe`를 제외했다.
  - 이 수정은 Android client가 emulator 자기 자신을 다시 캡처하는 recursive target을 고르지 못하게 하는 목적이다.
  - desktop path의 `GetShellWindow()` fallback 문제는 별도 이슈로 남아 있다.

Next action
- desktop path capture source 검증을 분리해 `Devices/Desktop` scene 흐름도 안정화한다.
- 그 다음 `Phase F` 입력 착수 전 gate를 재확인한다.

### 171) 2026-04-13 android direct timestamp and first-keyframe recovery
Goal
- Android direct client의 실기기/LDPlayer 저프레임 및 무출력 원인 후보 중 timestamp overflow와 selection 직후 first-frame 경계를 먼저 교정한다.
- host absolute QPC를 Android decoder PTS로 직접 쓰지 않게 바꾸고, selection generation에서 첫 송신 frame이 keyframe인지 host에서 강제한다.

Files changed
- `apps/native_poc/src/time_utils.hpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Native build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test`
  - 결과: 성공
- Native shared-core test:
  - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Native localhost smoke:
  - `powershell -ExecutionPolicy Bypass -File D:\remote\remote\automation\verify_native_video_runtime.ps1 -Root D:\remote\remote -BuildDir build-vcpkg-local -Codec h264 -Transport udp -GateAProfile -HostSeconds 12 -ClientSeconds 8`
  - 결과:
    - `OVERALL_OK=True`
    - `UDP_ASSEMBLY_MALFORMED_TOTAL=0`
    - `GATE_A_PASS=False` (`capture_input_stall`, 기존 capture source/host 환경 영향)
- Android build/deploy:
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat -p D:\remote\remote\apps\android_direct_client assembleDebug`
  - `adb -s emulator-5558 install -r D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - 결과: 성공
- LDPlayer 2 runtime:
  - device: `emulator-5558`
  - host launch note:
    - H264 runtime 검증은 `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`
    - 추가 비교로 `--encode-width 1280 --encode-height 720` variant도 실행
  - connect:
    - external host IP(`175.209.236.194`) 기준 `connect_tap -> connected window_list_received` 확인
  - desktop select:
    - host log:
      - `window-select ... streamGen=2`
      - `selection first keyframe sent streamGen=2`
    - Android diagnostics/logcat:
      - `pts reanchor reason=init ...`
      - `viewer_surface_buffer_resize`
      - `video_debug ... in=1 out=0` (full-res)
      - `video_debug ... in=2 out=0 ptsClamp=1` (same-path retry)
      - `video_debug ... in=1 out=0` (`1280x720` encode variant)
      - `select_timeout`은 계속 재현
  - window select (`C:\WINDOWS\system32\cmd.exe`):
    - `select_ack ... streamGen=3`
    - `video_debug ... in=0 out=0`
    - `select_timeout` 재현
  - timestamp evidence:
    - host/Android 로그에서 `9223372036854775807` 또는 overflow성 절대 timestamp sentinel은 관측되지 않음
    - Android debug/status에 local PTS rebase 상태(`ptsBaseRemote`, `ptsBaseLocal`, `ptsReanchor`, `ptsClamp`)가 반영됨
- Physical device:
  - `adb devices` 기준 실기기 없음
  - 이번 턴 검증은 LDPlayer 2만 수행

Next action
- Android `MediaCodec`가 selection 이후 queued keyframe을 받아도 first output을 못 여는 원인을 추가 분해한다.
- desktop path의 host WGC restart/stall과 window path `in=0` 경계를 나눠서 보고, 필요하면 decoder output polling/flush 또는 host post-select streaming cadence를 더 보강한다.

### 172) 2026-04-13 android direct first-output bootstrap replay + desktop closeout
Goal
- selection 직후 첫 queued keyframe만 들어오고 output drain이 열리지 않던 Android decoder 경로를 계속 보강한다.
- LDPlayer 2에서 desktop select는 실제 viewer 진입까지 닫고, window select는 host-side restart failure로 잔여 원인을 분리한다.

Files changed
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.hpp`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build/deploy:
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat -p D:\remote\remote\apps\android_direct_client assembleDebug`
  - `adb -s emulator-5558 install -r D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - 결과: 성공
- LDPlayer 2 runtime:
  - device: `emulator-5558`
  - host runtime:
    - `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`
    - `REMOTE60_NATIVE_H264_NO_PACING=1`
    - `REMOTE60_NATIVE_FRAME_GATING_DISABLE=1`
    - `REMOTE60_NATIVE_ABR_DISABLE=1`
    - `--encode-width 1280 --encode-height 720`
  - desktop select:
    - Android diagnostics:
      - `select_ack ... streamGen=2`
      - `select_ready targetId=0 gen=1 lastOutUs=10504406643`
      - `video_debug ... in=2 out=2 ... lastOutUs=10504406643`
    - 결과:
      - `scene=VIEWER` 진입
      - `select_timeout` 해소
      - `video_debug out` 실제 증가
  - window select:
    - `cmd.exe`와 `Codex Plan - remote - Visual Studio Code` 모두 `window_select_failed: capture_restart_failed`
    - host stderr:
      - `staging texture recreate failed size=1115x628`
      - `staging texture recreate failed size=2246x1184`
    - 결과:
      - window path 실패 원인이 Android decoder output이 아니라 host capture restart/staging recreate로 분리됨

Scope note
- Android decoder에 pending frame retry와 selection bootstrap replay를 추가해, 새 frame 유입이 없더라도 status poll 경로에서 input/output pump가 계속 돌게 했다.
- desktop path는 이제 LDPlayer 2에서 실제 first output이 열리는 수준까지 복구됐다.
- window path는 별도 host D3D/staging restart 이슈로 남았다.

Next action
- `restart_capture_session()`에서 window-select 시 staging texture recreate 실패 원인을 직접 수정한다.
- 그 후 LDPlayer 2에서 `Windows -> select -> viewer`까지 다시 닫고, 가능하면 실기기 재확인을 이어간다.

### 173) 2026-04-13 host staging recreate retry + LDPlayer window-select closeout
Goal
- window-select 잔여 실패 원인이던 host `capture_restart_failed` / `staging texture recreate failed`를 직접 완화한다.
- 콘솔 세션에서 LDPlayer 2 desktop/window select를 모두 viewer 진입까지 다시 닫고, AMD 이벤트가 새로 추가되는지도 같이 확인한다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Session state:
  - `quser` / `qwinsta` 기준 `console` active
  - RDP active session 아님
- Native build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc`
  - 결과: 성공
- LDPlayer 2 desktop select (console session):
  - Android diagnostics:
    - `select_ack ... streamGen=2`
    - `select_ready ...`
    - `scene=VIEWER`
    - `video_debug ... in=2 out=2`
  - 결과: 성공
- LDPlayer 2 window select (console session):
  - target:
    - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_host_poc.exe`
  - Android diagnostics:
    - `select_ack ... streamGen=2`
    - `select_ready ...`
    - `scene=VIEWER`
    - `video_debug ... in=1 out=1 -> in=19 out=19`
  - 결과: 성공
- Host-side recovery evidence:
  - `CreateTexture2D` staging failure 경로에 hr/removal logging 추가
  - failure 시 D3D11 device/context recreate + encoder/gpu scaler rebind 후 staging recreate retry 수행
  - window-select closeout run에서는 `capture_restart_failed` 재현되지 않음
- AMD / Event Viewer:
  - 최근 재시험 구간에서 새 `atidxx64.dll` / `remote60_native_video_host_poc.exe` / `LiveKernelEvent` 추가 이벤트는 관측되지 않음
  - 보이는 AMD 관련 이벤트는 `18:32` 시점의 과거 crash/live-kernel 기록

Scope note
- 이번 수정은 window-select failure를 Android decoder 문제가 아니라 host D3D resource recreate 문제로 분리한 뒤, host에서 직접 복구 경로를 넣은 것이다.
- 현재 기준 LDPlayer 2에서는 desktop/window 둘 다 viewer first output이 실제로 열린다.

Next action
- 실기기에서도 같은 외부 IP 경로로 `desktop -> viewer`, `window -> viewer`가 유지되는지 재확인한다.
- AMD driver warning popup이 실제 콘솔 재현에서도 다시 뜨는지 장시간 반복 select soak으로 본다.

### 174) 2026-04-14 visible host input-disabled launch path fix
Goal
- Android manual test에 쓰던 visible host 직접 실행 경로에서 `enableInputInjection=false`로 뜨던 원인을 제거한다.
- DXGI visible launch도 config-first wrapper를 타도록 고정해 클릭/키 입력이 꺼진 상태로 올라오는 회귀를 막는다.

Files changed
- `automation/host_dxgi.ps1`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Root cause check:
  - `tmp/host_manual_run/host.out.log`
  - `tmp/android_ts_verify_720p_live/host.out.log`
  - `tmp/android_ts_verify_window2/host.out.log`
  - 결과: 모두 시작 직후 `input injection disabled (enableInputInjection=false)` 확인
- Launch path check:
  - `tmp/run_visible_native_host_dxgi.cmd`
  - 결과: `--config` 없이 exe를 직접 실행하고 있어 profile의 `enableInputInjection=true`를 우회하고 있었음
- Config sanity:
  - `automation/native_video_profile_1080p_external_template.json`
  - 결과: `enableInputInjection=true`, `inputInjectionMode=background_message`
- Wrapper smoke:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/host_dxgi.ps1 -ExeDir build-vcpkg-local/apps/native_poc/Debug`
  - 결과: startup 로그에서 `REMOTE60_DESKTOP_CAPTURE_BACKEND=dxgi`와 `input injection enabled mode=background_message` 확인 후 프로세스 종료

Next action
- Android/LDPlayer에서 `automation/host_dxgi.ps1`로 host를 띄운 뒤 viewer tap + soft keyboard 입력이 실제 `inputEvents` 증가로 이어지는지 한 번 더 닫는다.
- 기존 직접 exe 실행 메모/임시 cmd 대신 config-first wrapper만 사용하도록 수동 테스트 동선을 정리한다.

### 175) 2026-04-14 android build + click verify + keyboard focus verify
Goal
- 최신 Android debug APK를 다시 빌드/설치하고, LDPlayer에서 현재 클릭/키입력 경로가 실제로 어디까지 정상인지 다시 닫는다.
- `host_dxgi.ps1` 경로 기준으로 desktop viewer click과 keyboard focus/IME 상태를 분리해서 확인한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build / install:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat -p D:\remote\remote\apps\android_direct_client assembleDebug`
  - `adb -s emulator-5558 install -r D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - 결과: 성공
- Device / network:
  - `adb devices`: `emulator-5558 device`, `emulator-5554 offline`
  - `adb -s emulator-5558 shell ping -c 1 192.168.0.76`
  - 결과: host reachability 성공
- Manual verify workspace:
  - temp dir: `tmp/android-input-verify-20260414-142229`
  - host config: external template 기반 + `inputLogEvery=1`
  - probe window: `Remote60 Android Input Probe` (`button_click`, textbox event를 파일로 기록)
- Host launch:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File automation/host_dxgi.ps1 -ConfigPath tmp/android-input-verify-20260414-142229/host_config.json -ExeDir build-vcpkg-local/apps/native_poc/Debug`
  - 결과: `input injection enabled mode=background_message`
- Android desktop viewer / click:
  - Android diagnostics:
    - `connect_tap host=192.168.0.76 ...`
    - `tab_switch tab=desktop`
    - `scene=VIEWER`
    - `select_ready ...`
  - probe log:
    - `button_click`
  - host log:
    - desktop mode input `seq=7/8` injected to `targetTitle=Remote60 Android Input Probe`
  - 결과: Android click path end-to-end 성공
- Android keyboard button / IME focus:
  - Android diagnostics:
    - `viewer_keyboard_tap scene=VIEWER`
  - `dumpsys activity top`:
    - `viewerKeyboardButton` bounds `65,8-109,52`
    - `ImeCaptureView ... .F......`
  - `dumpsys input_method`:
    - `mServedView=com.remote60.androiddirect.ImeCaptureView`
    - `mInputShown=true`, `mIsInputViewShown=true`
  - 결과: keyboard button tap과 IME served-view focus는 성공
- Keyboard text commit:
  - 시도:
    - `adb shell input text hello42`
    - bottom-half keyboard grid tap
    - Windows `AppActivate(14248)` + `SendKeys('hello42')`
    - `adb shell input keyevent 66/61`
  - 결과:
    - `probe_events.log`에 `textbox_text=...` 추가 없음
    - host log에 `input-text` 또는 `kind=5/6` keyboard injection evidence 없음
    - LDPlayer 기본 IME `com.android.inputmethod.pinyin/.InputService` 환경에서는 text/special-key commit을 자동화로 재현하지 못함

Scope note
- 이번 턴은 코드 수정이 아니라 Android build와 실제 런타임 경로 재검증이다.
- click은 host probe log까지 닫혔고, keyboard는 `button -> IME focus`까지는 닫혔지만 text commit은 LDPlayer IME 한계로 이번 환경에서 미확인 상태다.

Next action
- 실기기 또는 표준 Android Emulator(LatinIME/Gboard 계열)에서 `committed text`/backspace/enter를 다시 검증해 `Android Phase F soft keyboard runtime verify`를 닫는다.
- 필요하면 debug build 한정 text-injection verify hook을 추가해 `nativeQueueInputText` end-to-end를 자동화한다.

### 176) 2026-04-14 android special-key fallback + desktop dxgi flicker guard
Goal
- Android soft keyboard에서 빠지던 `Backspace`/`Enter`/`Space` 경로를 더 넓게 받아 host key/text 주입으로 연결한다.
- desktop mode에서 보이던 검은 깜빡임 후보 원인인 DXGI desktop `capture-input-stall` restart를 막는다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/ImeCaptureView.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android input patch:
  - `ImeCaptureView`:
    - `TYPE_TEXT_FLAG_NO_SUGGESTIONS`
    - `IME_FLAG_NO_ENTER_ACTION`, `IME_ACTION_NONE`
    - `deleteSurroundingTextInCodePoints()` 추가
    - `sendKeyEvent(KEYCODE_DEL)`을 backspace callback으로 처리
    - `performEditorAction()`을 enter down/up으로 처리
  - `MainActivity`:
    - committed text에서 `\n -> \r`, `NBSP -> space` 정규화
    - `KEYCODE_SPACE -> VK_SPACE`
- Desktop flicker guard:
  - `native_video_host_main.cpp`에서 `desktop + dxgi` 경로는 low-push `capture-input-stall` restart 대상에서 제외
  - 근거 로그:
    - `tmp/android-input-verify-20260414-142229/host.out.log`
    - desktop mode steady 구간에서 `capture session restarted reason=capture-input-stall` 반복 관측
    - 해당 restart가 black flicker와 직접 연동되는 후보로 판단
- Native build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc`
  - 결과: 성공
- Android build:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat -p D:\remote\remote\apps\android_direct_client assembleDebug`
  - 결과: 성공

Scope note
- 이번 수정은 runtime symptom 기반 보강이다.
- soft keyboard는 LDPlayer IME가 `committed text`를 불안정하게 보내는 점을 고려해 key-event/editor-action/delete fallback을 추가했고, desktop 깜빡임은 DXGI desktop low-push false positive를 먼저 제거했다.

Next action
- 현재 빌드로 `Backspace`/`Enter`/`Space`를 실기기 또는 LDPlayer에서 다시 수동 검증한다.
- desktop viewer black flicker가 사라졌는지 `host_dxgi.ps1` 경로에서 바로 재확인한다.

### 177) 2026-04-14 android desktop backend setting + host runtime switch
Goal
- Android settings 탭에서 desktop capture backend를 `DXGI/WGC` 중 선택할 수 있게 한다.
- 선택값이 저장되고, connect 직후 host로 sync되며, desktop mode일 때는 host capture backend가 런타임 전환되도록 만든다.

Files changed
- `apps/native_poc/src/poc_protocol.hpp`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_client_session.hpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/src/native_video_client_tcp_control.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SessionPersistence.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Protocol / host runtime:
  - control message `ControlDesktopBackendRequest` 추가
  - Android/native client session에서 queued request 전송 지원
  - host에서 request 수신 시 `requestedDesktopBackend` 갱신
  - desktop mode active 상태에서는 backend 차이가 있을 때 `restart_capture_session()`으로 즉시 재적용
  - window-target mode에서는 다음 desktop selection용 preference로만 저장
- Android UI / persistence:
  - settings panel에 `DXGI` / `WGC` 버튼 추가
  - 선택값 `SharedPreferences` 저장/복원 추가
  - connect 직후 saved backend auto-sync 추가
  - Apply 시 runtime bitrate/fps와 desktop backend를 함께 요청
- Native build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc`
  - 결과: 성공
- Native client/shared core build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_client_shared_core_test remote60_native_video_client_poc`
  - 결과: 성공
- Shared core test:
  - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build / install:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat -p D:\remote\remote\apps\android_direct_client assembleDebug`
  - `adb -s emulator-5558 install -r D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - 결과: 성공

Scope note
- 이번 턴은 “settings에서 backend 선택” 요청을 protocol부터 host runtime까지 실제 동작하도록 연결한 것이다.
- background click 차이는 여전히 app별 입력 경로 차이로 남는다. 현재 host는 `background_message`로 `WM_MOUSE*` / `WM_KEY*` / `WM_CHAR`를 보내므로, LDPlayer처럼 Win32 message를 직접 처리하는 창은 background click이 먹지만, BlueStacks처럼 foreground/raw-input 성격이 강한 창은 앞에 올라와야 반응할 수 있다.

Next action
- Android settings에서 `DXGI/WGC`를 바꿔 desktop viewer black flicker 차이를 바로 비교 확인한다.
- BlueStacks 배경 입력이 꼭 필요하면 `background_message` 외 별도 injection mode(포인터 이동 허용 `SendInput` 계열 또는 대상별 foreground fallback) 채택 여부를 따로 결정한다.

### 178) 2026-04-14 default desktop backend -> WGC + DXGI root-cause note
Goal
- desktop capture 기본 경로를 `WGC`로 바꿔 AMD 환경의 DXGI crash risk를 기본 동선에서 제거한다.
- DXGI issue 원인을 공식 자료와 로컬 crash evidence 기준으로 정리한다.

Files changed
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SessionPersistence.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/native_poc/src/native_video_client_shared_core.hpp`
- `apps/native_poc/src/native_video_client_shared_core.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Default switch:
  - Android saved/default desktop backend code `2 (WGC)`로 변경
  - Android in-memory default backend도 `WGC`로 변경
  - host `desktop_capture_backend_from_env()` 기본 반환값을 `WGC`로 변경
  - client shared-core desktop-backend control default도 `WGC`로 변경
- Native build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc remote60_native_video_client_shared_core_test`
  - 결과: 성공
- Shared core test:
  - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_client_shared_core_test.exe`
  - 결과: `PASS`
- Android build / install:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat -p D:\remote\remote\apps\android_direct_client assembleDebug`
  - `adb -s emulator-5558 install -r D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - 결과: 성공
- Local crash evidence:
  - Windows Event Log `Application Error 1000`
  - faulting module: `atidxx64.dll`
  - faulting app: `remote60_native_video_host_poc.exe`
  - same time range `LiveKernelEvent 141` 발생
- Official source note:
  - Microsoft Desktop Duplication/DDA는 DXGI 기반이며, 일부 GPU/topology 조건에서 `DuplicateOutput`이 `DXGI_ERROR_UNSUPPORTED`로 실패할 수 있음
  - Microsoft TDR 문서상 GPU가 timeout/reset되면 flicker/driver reset/app failure가 함께 나타날 수 있음
  - 이번 케이스의 정확한 내부 fault는 AMD 비공개 드라이버 코드 영역(`atidxx64.dll`)이라 공개 문서로 세부 root cause를 특정할 수는 없고, “AMD driver instability in DXGI desktop duplication path”로 판단하는 것이 현재 가장 강한 설명임

Scope note
- 이번 변경은 “기본 경로를 안정 쪽으로 돌리는” 조치다.
- DXGI는 계속 settings에서 수동으로 켤 수는 있지만, 기본값은 `WGC`로 내려 안정 동선을 우선한다.

Next action
- 기본값 `WGC` 상태에서 desktop viewer 안정성을 다시 본다.
- DXGI는 실험용으로만 유지하고, 필요하면 UI에 `experimental` 경고를 추가한다.

### 179) 2026-04-15 native host input injection default-on
Goal
- direct exe/manual launch 경로에서도 터치/키 입력이 기본 동작이 되도록 native host의 입력 주입 기본값을 상시 활성화로 바꾼다.
- 표준 1080p 프로필에도 입력 활성 의도를 명시해 실행 경로에 따라 터치가 빠지는 혼선을 줄인다.

Files changed
- `apps/native_poc/src/native_video_host_main.cpp`
- `automation/native_video_profile_1080p.json`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Code change:
  - `Args.enableInputInjection` 기본값을 `true`로 변경
  - 표준 profile `automation/native_video_profile_1080p.json`에 `enableInputInjection=true`, `inputInjectionMode=background_message` 명시
- Native build:
  - `cmake --build D:\remote\remote\build-vcpkg-local --config Debug --target remote60_native_video_host_poc`
  - 결과: 성공
- Runtime smoke:
  - `D:\remote\remote\build-vcpkg-local\apps\native_poc\Debug\remote60_native_video_host_poc.exe --bind-port 43000 --control-port 43001 --transport udp --codec h264 --fps 30 --bitrate 8000000 --keyint 30 --encode-width 1920 --encode-height 1080`
  - 환경변수: `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`, `REMOTE60_NATIVE_ENCODER_BACKEND=mft_auto`
  - 결과: `43000/UDP`, `43001/TCP` listen 확인, startup 로그에서 `input injection enabled mode=background_message` 확인

Next action
- Android/Windows client로 다시 붙여 실제 tap/keyboard가 `inputEventsApplied` 증가와 함께 유지되는지 한 번 더 확인한다.
- manual host를 직접 exe로 띄우는 안내가 남아 있으면 config-first 또는 default-on 전제와 맞게 문서/메모를 정리한다.

### 180) 2026-04-15 Android viewer scroll-hold/log overlay + local artifact cleanup
Goal
- Android direct viewer에서 desktop/window mode 공통으로 고정 포인트 휠 스크롤 제스처를 추가해 터치만으로 스크롤 입력을 보낼 수 있게 한다.
- 하단 고정 로그를 없애고 toolbar `LOG` 버튼으로 반투명 전체 로그 오버레이를 띄우도록 바꾼다.
- 저장소 내 안드로이드/로컬 빌드 산출물과 임시 폴더가 계속 워크트리에 남지 않도록 정리하고 ignore 규칙을 보강한다.

Files changed
- `.gitignore`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SessionDiagnosticsLog.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/layout/viewer_log_dialog.xml`
- `apps/android_direct_client/app/src/main/res/drawable/viewer_log_dialog_background.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `query`
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Android build:
  - `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr`
  - `D:\remote\remote\tmp\gradle\gradle-8.7\bin\gradle.bat assembleDebug`
  - 결과: 성공
- Android install / launch:
  - `adb -s emulator-5558 install -r D:\remote\remote\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk`
  - `adb -s emulator-5558 shell am start -n com.remote60.androiddirect/.MainActivity`
  - `adb -s emulator-5558 shell dumpsys activity activities`
  - 결과: install 성공, `com.remote60.androiddirect/.MainActivity` resumed 확인
- Visual evidence:
  - `Logs/verification/2026-04-15/android-direct-viewer-ui/connect-scene.png`
  - `Logs/verification/2026-04-15/android-direct-viewer-ui/uidump.xml`
  - 결과: 앱 launch 증적은 남겼지만, 이번 턴에는 실제 host session을 붙이지 않아 viewer 내부 `SCROLL`/`LOG` 버튼의 런타임 화면 검증은 미완료
- Cleanup:
  - Android `.gradle`, `app/.cxx`, `app/build`, top-level `.vcpkg`, `vcpkg_installed`, `build-vcpkg-local`, `dist`, `tmp`, `image`, stray `*.out/*.err/*.pdb`, tracked scratch file `query` 삭제
  - `.gitignore`에 Android local build/temp/log artifact 경로 추가

Next action
- 실제 host에 연결해 viewer 상태에서 `SCROLL` hold 제스처가 고정 포인트 wheel 입력으로 원하는 속도로 동작하는지 확인한다.
- `LOG` 오버레이가 viewer 상태/diagnostics 전체 로그를 충분히 보여주는지 실기기 또는 LDPlayer viewer 화면 기준으로 한 번 더 캡처 검증한다.

### 181) 2026-04-16 pre-window-capture-tuning snapshot + bottleneck review
Goal
- 윈도우 캡처 체감 렉 개선 작업 전에 현재 기준점을 안전하게 되돌릴 수 있도록 git 스냅샷 브랜치를 만든다.
- 현재 host window capture 경로를 확인해 창 이동 시 버벅임을 만들 가능성이 큰 병목 지점을 정리한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Git snapshot:
  - branch: `snapshot/pre-window-capture-tuning-20260416`
  - base commit: `c62cec61290b8e12800159b6573d23f0e082c158`
  - 결과: 현재 `main` HEAD 기준 롤백 포인트 생성 완료
- Code inspection:
  - `apps/native_poc/src/native_video_host_main.cpp`의 `publish_captured_texture(...)`가 capture callback 안에서 `CopyResource -> Map(D3D11_MAP_READ) -> memcpy -> Unmap`를 바로 수행한다.
  - 같은 파일의 encode 경로는 해상도가 다를 때 `GpuBgraScaler::scale(...)`에서 다시 `CopyResource -> Map -> memcpy -> Unmap` readback을 수행한 뒤 `bgra_to_nv12(...)` CPU 변환까지 이어진다.
  - 현재 구조상 window capture에서는 프레임당 GPU->CPU readback과 CPU 메모리 복사가 최소 1회, resize 시 사실상 2회 발생할 수 있어 창 이동/리사이즈 중 compositor/GPU contention과 callback stall을 만들 가능성이 높다.

Next action
- 1차: capture callback에서는 GPU copy까지만 처리하고, staging texture map/readback은 별도 worker가 늦은 슬롯을 읽도록 분리한다.
- 2차: window client crop과 resize를 CPU가 아니라 GPU source/dest rect 또는 texture crop으로 앞당겨 readback 바이트 수를 줄인다.
- 3차: BGRA->NV12를 CPU 변환 대신 GPU/NV12 입력 경로로 바꿔 resize 시 발생하는 두 번째 readback을 제거한다.

### 182) 2026-04-16 window capture tuning milestone planning
Goal
- 다음 작업으로 바로 착수할 수 있도록 window capture 개선 항목을 구현계획의 독립 마일스톤으로 승격한다.
- `callback copy-only -> worker readback ring`, `GPU-front crop/resize`, `GPU NV12 path`를 단계별 체크리스트와 검증 조건으로 고정한다.

Files changed
- `docs/history.md`
- `docs/구현계획.md`

Validation / build / test result
- Docs update:
  - `docs/구현계획.md`에 `M1.6 Window capture zero/low-readback pipeline (2026-04-16)` 마일스톤 추가
  - 코드 작업, 검증 항목, 완료조건, 다음 작업 계획을 체크리스트로 분리
  - 현재 최우선 마일스톤을 `M1.6`으로 갱신하고 실행 순서 상단에 다음 작업 계획을 고정
- Runtime/build:
  - 이번 턴은 계획 문서화만 수행
  - 코드 변경, 빌드, 런타임 검증은 수행하지 않음

Next action
- `M1.6-1`부터 착수: capture callback에서는 `CopyResource`만 수행하고 `Map/readback`은 worker/staging ring consumer로 분리한다.
- 이후 `M1.6-2`, `M1.6-3`를 순서대로 적용하면서 `captureD3DWaitUs`, `captureCopyMapUs`, `captureMemcpyUs`, `cb2eAvgUs`를 A/B 비교한다.

### 183) 2026-07-27 화질/UI 실사용 품질 개선 (color, profile, aspect, DPI, Android UX)
Goal
- 사용자 보고("이미지가 이상하다 / UI가 이상하다") 원인을 코드 근거로 규명하고 실사용 가능한 수준으로 끌어올린다.
- 기술 스택과 구현계획 대비 실제 구현 상태를 재확인한다.

Files changed
- `apps/native_poc/src/mf_h264_codec.cpp`, `apps/native_poc/src/mf_h264_codec.hpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/host/src/realtime_runtime.cpp`
- `apps/android_direct_client/app/src/main/AndroidManifest.xml`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/SessionDiagnosticsLog.kt`
- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `apps/android_direct_client/app/src/main/res/values-ko/strings.xml` (신규)
- `apps/android_direct_client/app/src/main/res/drawable/panel_button_background.xml` (신규)
- `apps/android_direct_client/app/src/main/res/drawable/panel_input_background.xml` (신규)
- `apps/android_direct_client/app/src/main/res/color/panel_button_text.xml` (신규)
- `docs/history.md`, `docs/구현계획.md`

확인한 기술 스택
- Host(Windows): C++20 / D3D11 / Windows.Graphics.Capture + DXGI Desktop Duplication / Media Foundation H.264(AMF·NVENC·QSV·MFT) / TCP 제어 + UDP 영상
- Client(Windows): C++20 / D3D11 NV12 셰이더 렌더 + GDI fallback / MF H.264 디코더
- Client(Android): Kotlin(minSdk 28, targetSdk 34) / NDK C++20 / MediaCodec(mediandk) / TextureView
- Web 경로: Node.js + ws 시그널링 / libdatachannel(WebRTC) / Opus
- 빌드: CMake + vcpkg(nlohmann-json, libdatachannel, opus), Gradle 8.5.2 / Kotlin 1.9.24
- 검증: PowerShell 자동화(`automation/verify_native_video_runtime.ps1` 등)

화질 결함 수정 (근거: 코드 조사 + 런타임 A/B)
- 색공간 불일치: host CPU `bgra_to_nv12`는 BT.601 limited였고 스트림에 VUI 색상 정보가 전무했다. Android MediaCodec은 HD에서 BT.709를 가정하므로 색이 틀어졌다.
  → 전 경로를 BT.709 limited로 통일(`bgra_to_nv12`, `nv12_to_bgra`, 클라이언트 D3D 셰이더) + `MF_MT_YUV_MATRIX`/`VIDEO_NOMINAL_RANGE`/`VIDEO_PRIMARIES`/`TRANSFER_FUNCTION` 명시.
- H.264 프로파일 미지정 → MFT 기본값 사용. `MF_MT_MPEG2_PROFILE=High` + 해상도/fps 기반 level 지정(수용 실패 시 기본값으로 폴백).
- WGC 캡처 세션의 노란 "캡처 중" 테두리가 인코딩 프레임에 포함되던 문제 → `IsBorderRequired(false)`. 커서는 원격 조작에 필요하므로 명시적으로 유지(`REMOTE60_NATIVE_HIDE_CURSOR`로 opt-out).
- 종횡비 왜곡: `encodeWidth/Height`를 축별로 독립 클램프해 16:10/3:2 모니터와 창 캡처에서 화면이 눌렸다. → 바운딩 박스 fit으로 변경하고, 소스 크기 변경 시 `nominalEncode*` 기준으로 재적합(`encode-refit` 로그).
- 창 클라이언트 크롭이 홀수 크기를 낼 수 있어 NV12 마지막 크로마 열이 미기록 → 짝수로 내림.
- GPU 스케일러가 색공간/auto-processing 미설정이라 드라이버별 레벨·샤프닝 편차 발생 → full-range RGB/BT.709 명시 + `SetStreamAutoProcessingMode(FALSE)`. `apps/host`의 WebRTC 경로에도 동일 적용.
- CPU 리사이즈가 순수 bilinear라 2배 초과 축소(4K→1080p 등)에서 앨리어싱 → 2x2 박스 프리필터 반복 후 bilinear.
- 기본 비트레이트 1.1Mbps/keyint 15 → 720p 자동 강등이 상시 발동. M7 확정값(8Mbps/keyint 30)으로 교체.
- CBR VBV가 약 12.5ms로 과도하게 짧아 장면 전환이 뭉개짐 → 약 50ms로 확대.
- 창 목록이 외곽 window rect를 보고해 뷰어의 첫 프레임 전 레터박스/터치 매핑 기준이 어긋남 → client rect 기준으로 변경.

Windows 클라이언트 UI 수정
- DPI 인식 없음 → OS가 창 전체를 비트맵 확대해 텍스트/영상이 흐림. `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` + `WM_DPICHANGED` 처리 + 패널 메트릭 DPI 스케일링.
- GDI 기본 System 비트맵 폰트 사용 → 한글 등 유니코드 창 제목 깨짐. Segoe UI 폰트 생성/선택.
- 창 목록 항목의 제목이 가운데 정렬로 한 번, 왼쪽 정렬로 또 한 번 겹쳐 그려지던 버그 제거.
- GDI fallback의 `COLORONCOLOR`(픽셀 드롭) → `HALFTONE` + `SetBrushOrgEx`.

Android 클라이언트 수정
- 타깃 전환(SWITCHING) 중 상태 오버레이가 `visibility="gone"`으로 고정되어 최대 6초간 순수 검은 화면만 보이던 문제 → ProgressBar + 안내 문구를 가진 로딩 패널 신설.
- connect/targets 화면에 스크롤 컨테이너가 없어 가로 모드에서 Connect 버튼이 잘려 접근 불가 → `ScrollView(fillViewport)` 적용.
- `android:background` 단색 지정이 프레임워크 StateListDrawable을 대체해 활성/비활성/눌림이 시각적으로 동일하던 문제 → 상태별 drawable + `ColorStateList` 도입.
- `maxLines`만 있고 `ellipsize`가 없어 문자 중간에서 잘리던 텍스트 보정, 오류 라인에서 로그 절대경로 제거.
- 터치 타깃 44dp → 48dp, `windowSoftInputMode=adjustResize` 추가.
- 스크롤 제스처 상수가 원시 픽셀이라 밀도별 감도가 달라지던 문제 → dp 기반으로 변환.
- MediaCodec 저지연 키(`low-latency`, 벤더 키, `PRIORITY=0`) 추가.
- Annex-B 시작코드 탐색이 버퍼 끝 경계를 놓쳐 프레임 마지막 NAL을 흘리던 버그 수정.
- 입력 보강: 수정자키(Shift/Ctrl/Alt/Meta)·F1~F12·A~Z·0~9 매핑 추가, `dispatchKeyEvent`로 물리/블루투스 키보드 지원.
- 진단 로그 append/read를 단일 백그라운드 스레드로 이동(메인 스레드 I/O 제거), 250ms 폴링마다 무조건 리스트를 재구성하던 동작을 변경 시에만 수행하도록 수정.
- 하드코딩 한국어 문자열을 `values-ko/`로 분리하고 기본 로케일을 영어로 정리.

Validation / build / test result
- Windows 빌드: `cmake --build D:\remote\remote\build-local --config Release/Debug --target remote60_native_video_host_poc remote60_native_video_client_poc remote60_native_video_client_shared_core_test` → 성공
- 단위 테스트: `remote60_native_video_client_shared_core_test.exe` → `[shared-core-test] PASS`
- Android 빌드: `gradle clean assembleDebug` (JBR 17, offline) → `BUILD SUCCESSFUL`, `app-debug.apk` 생성
- 런타임 게이트: `automation/verify_native_video_runtime.ps1 -BuildDir build-local -Codec h264 -Bitrate 8000000 -Keyint 30 -EncodeWidth 1920 -EncodeHeight 1080`
  - `OVERALL_OK=True`, `encodeSize=1920x1080 auto720=0`(기본 비트레이트 상향으로 720p 자동 강등 미발생)
  - UDP 조립 `dropped=0 malformed=0`, `encoderResets=0`
- 인코더 프로파일 A/B (동일 조건, `git worktree`로 HEAD 베이스라인 별도 빌드):
  - 베이스라인(HEAD): `h264 sps profile_idc=77 (main) level_idc=40`
  - 수정본: `h264 sps profile_idc=100 (high) level_idc=40`
- 회귀 확인: `GATE_A_DECODED_FPS_OK=False`는 베이스라인에서도 동일하게 재현됨(현재 원격/헤드리스 세션에서 WGC 콜백이 초당 수 프레임만 발생하는 환경 제약이며 이번 변경과 무관). 처리량은 `DECODED_RAW_MBPS_AVG` 86.11(baseline) → 88.89(modified)로 동등 이상.

Next action
- 실제 Android 기기 또는 LDPlayer에서 연결 → 타깃 선택 → 뷰어 흐름을 돌려 로딩 패널, 버튼 상태, 가로 모드 스크롤, 물리 키보드 입력을 눈으로 확인한다.
- 물리 디스플레이가 연결된 세션에서 `GATE_A_DECODED_FPS_OK`를 다시 측정해 30fps 목표 달성 여부를 판정한다.
- 색상 정확도는 컬러바를 띄운 상태에서 host 원본과 Android 뷰어를 나란히 캡처해 육안/픽셀 비교로 확정한다.

### 184) 2026-07-27 전체 재점검 2차 + OSLink형 카드 그리드 UI 재설계
Goal
- 사용자 재점검 요청("UI가 OSLink처럼 되면 좋겠다, 전체 점검 다시") 대응.
- 직전 커밋(9886b2d)을 포함한 전체 코드 재감사와, 타깃 선택 화면의 전면 재설계.

재감사에서 발견/수정한 결함
- [Critical] encode-refit이 창 리사이즈 드래그 중 매 프레임 인코더를 재초기화(초당 최대 60회 MFT teardown). → 0.4초 settle 디바운스 + 종횡비 2% 이내 변화 무시.
- [Critical] refit 실패 시 encoder.shutdown()만 되고 스트림이 조용히 사망. → 다른 호출부와 동일하게 루프 종료로 전환.
- [High] Windows 클라이언트가 Unicode 창에 ANSI DefWindowProc/PeekMessage를 사용 — 창 제목이 "r" 한 글자로 깨지고 WM_CHAR가 ANSI로 전달. → 전부 *W 명시형으로 교체(실행 화면으로 확인).
- [High] Android 물리 키보드가 VK와 unicode 텍스트를 모두 전송해 모든 문자가 이중 입력("hheelllloo"). → VK만 전송(호스트측 TranslateMessage가 WM_CHAR 생성).
- [High] DPI 스케일 디스플레이에서 패널 텍스트 줄 간격이 원시 픽셀이라 줄이 겹침. → dpi_scale 적용.
- [Medium] runtime-config 핸들러가 fitted 크기를 nominal로 되돌려 써서 타깃 전환 후 해상도가 영구 축소(래칫). → nominal 박스 전달로 수정.
- [Medium] h264_level_for가 MaxFS 미검증 — 1080p 저fps에서 level 3.2 선언(규격 위반). → MaxMBPS+MaxFS 동시 검사 테이블.
- [Medium] VBV 계산이 비트/바이트 혼동으로 의도(50ms)와 달리 400ms. → bitrate/40(=200ms)로 정정, 주석의 수치 오류도 정정.
- [Medium] Android 뷰어가 첫 프레임 후 상태 오버레이를 영구 숨김 — 연결이 죽어도 마지막 프레임이 "살아있는 화면"처럼 보임. → PTS 정체 3초/연결 끊김 시 오버레이 복귀.
- [Medium] configChanges에 keyboard 누락 — 블루투스 키보드 연결 시 Activity 재생성. → keyboard 추가.
- [Medium] GDI fallback HALFTONE이 매 프레임 소프트웨어 리샘플로 GPU 없는 환경에서 프레임률 저하. → 비디오 경로는 COLORONCOLOR 복귀(썸네일 렌더에만 HALFTONE 유지).
- [Low] apps/host WebRTC 인코더 VUI 미명시, 크로마 시팅 선언(MPEG2)과 실제 필터(2x2 박스=MPEG1) 불일치, 셰이더 크로마 영점 0.5(≠128/255), DPI-aware 전환 후 창 크기 물리픽셀 고정 — 전부 수정.
- 검증 완료 항목: BT.709 정수 계수 수학적 검증 통과(감사자 독립 재계산), box_halve/bilinear 메모리 안전, find_start_code 경계 수정 확인.

UI 재설계 (OSLink 스타일, docs/android_구현계획.md의 "탭/카드+창별 썸네일" 사양 이행)
- 프로토콜: `ControlWindowThumbnailRequest(35)`/`ControlWindowThumbnail(36)` 신설. 기존 메시지는 바이트 단위 불변 유지, 호스트가 window list flags bit1로 capability 광고, 클라이언트는 광고 시에만 요청(구버전 피어와 상호 호환).
- 호스트: `capture_window_thumbnail` — PrintWindow(PW_RENDERFULLCONTENT)로 가려진 창 포함 창별 미리보기, 데스크톱은 BitBlt, aspect-fit 축소 후 BGRA 전송(최대 320x320, payload 상한 검증).
- Windows 클라이언트: 홈 화면을 창 전체를 쓰는 카드 그리드로 재설계(헤더 타이틀+Refresh/Desktop, 16:10 썸네일 카드+캡션, 선택 카드 초록 강조, 행 단위 휠 스크롤, 상태 푸터). 썸네일은 control 스레드가 스케줄러 유휴 시간에 1장씩 페치(입력 이벤트 기아 방지), 소켓 오류 시 세션 정리.
- Android: ListView → GridView 카드(target_card.xml, 96dp 썸네일+캡션, activated 상태로 선택 표시). 세션 컨트롤러에 썸네일 캐시/페치 추가, JNI `nativeGetWindowThumbnail`(RGBA, Bitmap.copyPixelsFromBuffer 호환), JSON에 thumbVersion 마커로 재디코드 회피.

Validation / build / test result
- Windows Release/Debug 전체 빌드 성공, `shared_core_test` PASS (fake host는 thumbnail 미광고 → 신규 경로 하위호환 확인).
- Android `assembleDebug` 성공.
- localhost 게이트: `OVERALL_OK=True`, `UDP_ASSEMBLY_MALFORMED_TOTAL=0`, 썸네일 트래픽 활성 상태에서 control rtt 100~170us 정상, `sps profile_idc=100(high)` 유지.
- 실행 스크린샷: `Logs/ui-shots/client-02-picker.png` — 12개 창의 라이브 썸네일 카드 그리드, 선택 강조, 한글 제목 정상 렌더 확인.
- ANSI/유니코드 버그는 실행 중 창 열거로 before(`title='r'`)/after(`title='remote60 native video client'`) 실측 확인.

Next action
- LDPlayer/실기기에서 Android 카드 그리드와 썸네일 로딩 확인.
- 창 리사이즈 드래그 중 refit 디바운스 체감 확인(호스트 hitch 없어야 함).

### 185) 2026-07-27 실기기 리포트 대응: Wi-Fi 화면 깨짐 + 한글 IME 입력 중단
Goal
- 실기기 접속에서 보고된 2건을 원인까지 규명해 수정한다.
  1. 가끔 화면이 깨졌다가 흐릿하게 복구된 뒤 서서히 선명해짐
  2. 한글 입력 시 몇 글자 뒤 입력 중단("유튜브" -> "유튜"), 백스페이스 무반응

원인 1: UDP 버스트 + 작은 수신 버퍼 (양쪽 동시 원인)
- 호스트가 한 프레임의 모든 UDP 조각을 지연 없이 연속 전송한다 (`native_video_host_main.cpp` `send_udp_chunks`).
  1080p 키프레임은 200KB 내외 = 1200바이트 데이터그램 약 170개가 순간 버스트로 나간다.
  유선/로컬호스트에서는 문제없지만 Wi-Fi에서는 AP와 단말 버퍼를 넘겨 손실이 난다.
- Android 클라이언트(`native_video_client_session.cpp`)에는 `setsockopt`가 하나도 없어
  OS 기본 UDP 수신 버퍼(약 100KB)를 그대로 쓴다. Windows 클라이언트는 튜닝하는데 Android만 누락되어 있었다.
- 손실 -> 프레임 손상 -> 키프레임 복구 -> CBR/VBV 상한에 눌린 소프트한 I-프레임 -> 이후 P-프레임이
  점진 보정. 사용자가 말한 "겹치면서 선명해지는" 현상이 정확히 이 progressive refinement다.

수정 1
- 호스트에 intra-frame 패킷 페이싱 추가. 평균 비트레이트의 배수를 피크로 삼아 한 프레임의
  데이터그램을 시간에 분산한다. `REMOTE60_NATIVE_UDP_PACE_PEAK_PERCENT` (기본 250 = 2.5배, 0이면 비활성).
  조각 8개 이하 소형 프레임은 페이싱을 건너뛰어 지연 오버헤드를 만들지 않는다.
  Windows 타이머 해상도가 부족하므로 긴 대기만 sleep하고 나머지는 yield 스핀으로 처리한다.
- Android UDP 비디오 소켓에 `SO_RCVBUF` 4MB(실패 시 1MB) 설정.

원인 2: 조합 중 텍스트(composing text) 전량 폐기
- `ImeCaptureView.setComposingText`가 아무 동작 없이 true만 반환했다.
  한글 IME는 음절을 조합 중에 setComposingText로 계속 갱신하고, 다음 음절이 시작될 때에야 commitText를 부른다.
  따라서 마지막 음절은 커밋 이벤트가 오지 않아 영원히 전송되지 않는다 -> "유튜브"가 "유튜"로 전달됨.
- 조합 중 백스페이스도 IME가 내부에서 음절을 분해해 setComposingText로 보고하므로,
  이를 무시하면 백스페이스가 완전히 죽은 것처럼 보인다.

수정 2
- 호스트에 현재 표시 중인 조합 문자열을 추적하고, 변경 시 최소 편집만 전송한다.
  공통 접두사 이후를 코드포인트 단위 백스페이스로 지우고 새 꼬리를 보낸다.
  commitText가 같은 문자열로 오면 공통 접두사 로직이 자연히 중복 입력을 막는다.
- 조합 중 `deleteSurroundingText`/`sendKeyEvent(DEL)`은 무시한다(IME가 setComposingText로 이미 보고하므로 이중 삭제 방지).
- 서로게이트 페어를 쪼개지 않도록 공통 접두사 계산에 가드를 둔다.
- 텍스트와 키 이벤트가 동일한 `ClientInputQueue`(단일 deque)를 공유하므로 백스페이스/문자 순서는 보장된다.

Validation / build / test result
- Windows Release/Debug 빌드 성공, `shared_core_test` PASS
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신
- localhost 게이트(화면 모션 생성 상태): 페이싱 활성 확인
  `udpPacePeakPercent=250 udpPacePeakBps=20000000`
  `UDP_ASSEMBLY_DROPPED_TOTAL=0`, `MALFORMED_TOTAL=0`, `REORDER_TOTAL=0`
  지연 회귀 없음: `avgLatencyUs=11997`, `maxLatencyUs=81304`

Next action
- 실기기에서 Wi-Fi 재확인. 여전히 깨지면 `REMOTE60_NATIVE_UDP_PACE_PEAK_PERCENT=150`으로 더 조여본다.
- 한글 조합이 호스트에서 음절 단위로 실시간 갱신되는지, 백스페이스가 분해로 동작하는지 확인.
- 손실이 사라진 뒤에도 복구 흐림이 남으면 그때 키프레임 비트 여유(VBV/peak) 조정을 A/B로 판단한다.

### 186) 2026-07-27 프레임 게이팅 정확도 수정 + 콘솔 세션 DXGI 검증
Goal
- 사용자 제안("이전 화면과 비교해 변화 없으면 전송 중단") 검토 및 반영.
- 기능은 이미 M5 frame gating으로 존재했으나 프로파일에서 꺼져 있었고, 켜기 전에 검출기 정확도를 먼저 확인했다.

발견한 결함 1: 변화 검출기가 작은 변화를 전혀 감지하지 못함
- `estimate_bgra_change_permille`는 수천 개 샘플 픽셀의 **평균 밝기 차이**를 permille로 환산했다.
- 1920x1080에서 글자 한 개(약 200픽셀)가 바뀌어도 평균은 거의 움직이지 않아 결과가 0이 된다.
  즉 타이핑이 "정적"으로 분류되어 5~8fps로 눌린다. 이 상태로 게이팅을 켜면 명백한 회귀였다.
- 수정: 4KB 블록 단위 `memcmp` 전수 비교로 교체. 두 프레임이 바이트 단위로 동일할 때만 0을 반환하고,
  그 외에는 변경 블록 비율(최소 1)을 반환한다. memcmp는 CRT에서 SIMD 최적화되어 있어
  이미 수행 중인 프레임 복사보다 훨씬 싸다.

발견한 결함 2: 정적 모드에서 움직임이 최대 125ms 지연됨
- 기존 skip 조건이 변화 여부와 무관하게 `staticInterval`(8fps=125ms) 내 프레임을 전부 버렸고,
  정적 모드 탈출도 motion streak 2프레임을 요구했다.
  결과적으로 유휴 상태에서 첫 클릭/키입력이 최대 125~250ms 늦게 전송된다. 사용자가 우려한 그대로다.
- 수정: `motionNow`(= 변화가 조금이라도 있음)이면 즉시 정적 모드를 벗어나고 interval skip도 우회한다.
  이제 유휴 구간만 throttle되고, 변화는 항상 즉시 전송된다.

프로파일
- `automation/native_video_profile_android_lan.json`: `frameGatingDisable=false`, `staticSceneFps=5`.
- permille 임계값은 사실상 레거시 노브가 되었다(게이트가 "동일 여부"로 판정).

참고: 대역폭 관점의 실제 이득
- H.264 inter prediction이 이미 정적 화면을 처리한다. 실측에서 정적 장면은 `mbps=0.12`,
  `encRatioX100=20247`(약 202배 압축)까지 떨어진다.
  따라서 게이팅의 주된 이득은 대역폭보다 **캡처 readback/스케일/NV12 변환/인코딩을 통째로 건너뛰는 CPU·GPU 절감**이다.
- 사용자가 언급한 "변경된 부분만 전송"은 H.264가 이미 하고 있는 일이므로 별도 구현은 중복이다.

콘솔 세션 DXGI 검증 (RDP 종료 후)
- 사용자가 RDP를 끊고 물리 콘솔에서 로그인하면서 세션 구조가 바뀌었다:
  이전 `console 9 Conn`(무인, 잠금화면) + `shotan 2 Disc`(RDP) -> 현재 `console shotan 2 Active`.
- 이 상태에서 DXGI를 강제해 측정한 결과 **fallback 없이 성공**했다:
  `desktop_backend=dxgi capture=1920x1080`, `capture-started=1`, `dxgi_no_output_found` 없음.
  `callbackFrames`가 초당 44프레임으로, RDP 세션에서 WGC가 내주던 2~5프레임과 크게 다르다.
- 즉 `docs/구현계획.md`에 장기 미해결로 남아 있던 `current RDP session: dxgi_no_output_found -> WGC fallback`은
  코드 결함이 아니라 **RDP 세션 환경 제약**이었음이 확정되었다.
  같은 이유로 그동안 `GATE_A_DECODED_FPS_OK=False`가 계속 나온 것도 설명된다.
- 이는 잠금화면 설계(S2)에도 직접적인 근거다: 콘솔 세션에서는 DXGI가 정상 동작하므로
  보안 데스크톱 캡처의 전제가 성립한다.

Validation / build / test result
- Windows Release/Debug 빌드 성공, `shared_core_test` PASS
- localhost 게이트: `OVERALL_OK=True`, `UDP_ASSEMBLY_DROPPED_TOTAL=0`, `MALFORMED_TOTAL=0`
- 게이팅 동작 확인: 유휴 시 초당 15~19프레임 skip, 변화 발생 시 `motionStreak=1`에서 즉시 탈출
- 새 검출기가 클라이언트 창이 화면에 있는 상황을 올바르게 `motion`으로 유지(기존 검출기는 static으로 오판)

Next action
- 실기기에서 타이핑 반응성 확인(게이팅 켠 상태에서 지연 없어야 함).
- 유휴 시 대역폭이 실제로 떨어지는지 폰 기준으로 확인.
- `구현계획.md`의 RDP/DXGI 미검증 항목을 닫는다.

### 187) 2026-07-27 키보드 입력 먹통 근본 원인: PostMessage(WM_CHAR)를 최신 앱이 무시
Goal
- 실기기 리포트("크롬에 글씨가 안 써진다, 키보드 계속 먹통") 재현 및 수정.

원인
- 호스트의 텍스트 주입이 `PostMessageW(targetHwnd, WM_CHAR, ch, 1)`이었다
  (`native_video_host_main.cpp` `apply_input_text_message`).
  특수키도 `PostMessageW(WM_KEYDOWN/WM_KEYUP)`.
- Chrome/Electron/UWP 계열은 자체 focus manager로 키보드를 라우팅하고 실제 키 상태를 참조하므로,
  최상위 창에 post된 합성 WM_CHAR를 무시한다. 대상 HWND도 마지막 클릭 위치에서 해석한 창이라
  실제 포커스된 입력 필드가 아니다.
- 즉 앞서 고친 Android IME 조합 문제와 무관하게, **호스트 단에서 애초에 아무 글자도 전달되지 않고 있었다.**

실측 증거 (메모장, 동일 창에 두 방식 각각 주입 후 WM_GETTEXT로 회수)
- `PostMessage(WM_CHAR)` 최상위 창 -> 결과 `''` (전달 안 됨)
- `SendInput(KEYEVENTF_UNICODE)`   -> 결과 `'SENDINPUT-한글'` (한글 포함 정상)
- 메모장이 이 정도이므로 Chrome은 더 엄격하다.

수정
- desktop 모드: 텍스트는 `SendInput` + `KEYEVENTF_UNICODE`, 특수키는 `SendInput` 가상키로 전환.
  desktop 모드는 이미 실제 커서를 움직이므로 키보드도 실제 포커스로 가는 것이 일관적이다.
  확장키(방향키/Home/End/PgUp/PgDn/Insert/Delete/우Ctrl/우Alt)는 `KEYEVENTF_EXTENDEDKEY` 부여.
- window 모드: 배경 창에 포커스를 뺏지 않고 넣는 것이 설계 의도이므로 PostMessage를 유지하되,
  대상 창이 이미 포그라운드면(자기 자신/조상 일치) `SendInput`을 우선 사용한다.
  실제 사용에서 "보고 있는 창을 클릭하고 타이핑"하는 흔한 경우를 커버한다.

Validation / build / test result
- Windows Release/Debug 빌드 성공, `shared_core_test` PASS
- localhost 게이트 `OVERALL_OK=True`, `UDP_ASSEMBLY_DROPPED_TOTAL=0`
- 주입 방식 A/B는 위 실측 증거로 확정

정정
- 직전 항목(186)에서 "RDP 때문에 성능이 낮게 측정됐다"고 기록했으나, 사용자는 평소 RDP를 끄고 사용해 왔다.
  해당 발견은 **이 세션의 측정 환경에만 해당**하며 사용자 체감 성능과는 무관하다. 과대해석이었다.

Next action
- 실기기에서 크롬 주소창에 한글/영문 입력 재확인.
- window 모드(창 목록에서 선택) 상태에서도 입력이 되는지 별도 확인 필요.
  포그라운드가 아닌 배경 창은 여전히 PostMessage 경로이므로 Chrome 대상이면 실패할 수 있다.

### 188) 2026-07-27 뷰어 UI 재구성: 사이드 레일 + 자동 회전 + 빠른 설정 + 데이터 사용량
Goal
- 사용자 요청(OSLink 스크린샷 대조): 버튼이 영상을 가리지 않도록 하고, 원격 화면 비율에 따라
  자동 회전, 빠른 프리셋 팝업, 실시간 데이터 사용량(MB) 표시.

변경
1. 뷰어 레이아웃 재구성 (`activity_main.xml`)
   - 기존: `FrameLayout` 안에서 `TextureView`가 전체를 채우고 컨트롤 바가 좌상단에 겹쳐 떠 있었다.
     가로 모드에서 버튼이 영상 위를 덮는다는 지적 그대로였다.
   - 변경: `viewerSplit`(LinearLayout) = [영상 프레임 weight=1][컨트롤 레일 wrap].
     레일이 영상 바깥 여백에 위치하므로 어떤 방향에서도 화면을 가리지 않는다.
   - 기기가 가로면 레일을 오른쪽 세로 배치, 세로면 하단 가로 배치로 런타임 전환.
   - 레일이 더 이상 영상을 덮지 않으므로 자동 페이드(0.34 알파)를 제거하고 항상 표시로 변경.
2. 자동 회전 (`applyViewerOrientation`)
   - 디코드된 영상이 가로(w>=h)면 `SCREEN_ORIENTATION_SENSOR_LANDSCAPE`,
     아니면 `SENSOR_PORTRAIT`로 기기 방향을 맞춘다.
   - `forcePortrait` 토글(레일의 ROTATE 버튼, 빠른 설정 메뉴에도 동일 항목)로 세로 강제 가능.
   - 중복 호출 방지를 위해 `lastAppliedLandscape`로 변경 시에만 `requestedOrientation`을 세팅.
3. 빠른 설정 팝업 (`showQuickSettingsDialog`)
   - 레일의 MENU 버튼 -> `AlertDialog` 목록: 모바일(3Mbps/15fps) / 균형(6/30) / 선명(8/30) / 화면 방향 토글.
   - 선택 시 즉시 `nativeRequestRuntimeConfig` 적용 + 설정 탭 입력값 동기화 + `saveCurrentEndpoint()`로 영속화.
   - 별도 프리셋 버튼을 두지 말고 메뉴로 달라는 요청 반영.
4. 실시간 데이터 사용량
   - `ClientSessionController`에 `sessionBytesReceived_` 원자 카운터 추가.
     UDP 수신 루프에서 수신 바이트를 누적하고 세션 리셋 시 0으로 초기화.
   - JNI `nativeGetSessionBytesReceived()` 추가, 레일 하단에 MB 단위로 표시(100MB 이상은 정수).

Validation / build / test result
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신(8.9MB)
- Windows Release 빌드 성공, `shared_core_test` PASS
- 빌드 중 발견해 수정: `viewerControlsBar` 중복 선언(View/LinearLayout), 레이아웃 재작성 시 누락된 LOG 버튼 복원

보안 메모
- 사용자가 대화 중 외부 서버 자격증명을 평문으로 제공했다. 저장소에 기록하지 않았고 접속도 하지 않았다.
  해당 비밀번호는 노출된 것으로 간주하고 변경이 필요하다.

Next action
- 실기기에서 가로/세로 자동 전환, 레일이 영상을 가리지 않는지, MENU 프리셋 즉시 반영, MB 카운터 확인.
- 계정/호스트 등록 서버(디렉터리 서비스)는 별도 설계 필요. 미착수.

### 189) 2026-07-27 회귀 수정: 자동 회전이 첫 프레임 핸드셰이크를 깨뜨림
Goal
- 사용자 리포트: "화면 누르면 검은 화면에 있다가 다시 목록으로 간다".
  직전 커밋(f559a2a)에서 넣은 뷰어 자동 회전의 회귀.

원인
- `applyViewerOrientation()`이 `renderViewerScene`에서 무조건 호출되었다.
- 타깃 선택 직후에는 아직 디코드된 프레임이 없어 `videoWidth/videoHeight`가 0이다.
  따라서 `landscapeContent=false` -> `wantLandscape=false`가 되어 **세로로 강제 회전**을 요청한다.
- 기기가 가로 상태였다면 실제 회전이 일어나고, 회전은 `TextureView`의 `SurfaceTexture`를
  파괴/재생성한다. `onSurfaceTextureDestroyed` -> `releaseVideoSurface()`로 디코더가 출력 서피스를
  잃고 첫 프레임이 나오지 않는다.
- `readySelectionGeneration`이 끝내 일치하지 않아 6초 후 `select_timeout` ->
  `moveToTargets(abortPendingSwitch=true)`로 목록에 되돌아간다.
  사용자가 본 "검은 화면 -> 목록 복귀"가 정확히 이 경로다.

수정
- 함수를 둘로 분리했다.
  - `applyViewerRailLayout()`: 레일 방향/배치만 갱신. 항상 안전하며 config 변경 시에도 이것만 호출.
  - `applyViewerOrientation()`: **`currentScene == VIEWER`이고 디코드 크기가 확정된 경우에만**
    `requestedOrientation`을 변경. SWITCHING 중에는 절대 회전하지 않는다.
- `onConfigurationChanged`는 `applyViewerRailLayout()`만 호출하도록 변경.
- 뷰어를 벗어날 때 `resetViewerOrientationState()`로 방향 잠금을 해제(`SCREEN_ORIENTATION_UNSPECIFIED`)해
  목록 화면이 자유 회전 가능하고 다음 선택이 깨끗하게 재평가되도록 했다.
- ROTATE 버튼/메뉴 토글은 `lastAppliedLandscape = null`로 초기화해 즉시 반영되게 했다.

Validation / build / test result
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신
- 실기기 재확인 필요(카드 선택 -> 뷰어 진입이 목록으로 되돌아가지 않는지)

Next action
- 실기기에서 선택 -> 뷰어 진입 정상 여부 확인.
- 가로 원격 화면에서 첫 프레임 이후 자동 가로 전환이 일어나는지, 그때 영상이 끊기지 않는지 확인.
  (회전 시 서피스 재생성은 여전히 발생하므로, 끊김이 보이면 회전 후 재바인딩 경로를 추가 보강해야 한다.)

### 190) 2026-07-27 방향 전환을 선택 시점으로 앞당김 (사용자 제안)
Goal
- 사용자 제안: "화면이 뜨고 가로로 돌리지 말고, 가로 비율이면 가로로 돌린 채로 뜨게 하자."
- 189에서 회전을 뷰어 진입 이후로 미뤘지만, 여전히 스트림 도중 회전이 발생해
  서피스 재생성으로 끊길 여지가 남아 있었다.

접근
- window list에는 이미 각 대상의 client 크기가 들어 있고(`window_content_extent`로 host가 채움),
  클라이언트는 선택 시점에 `resolveSelectionHintSize()`로 이를 읽는다.
  즉 **첫 프레임을 기다리지 않아도 대상의 가로/세로 비율을 알 수 있다.**
- 따라서 `startSelectionTransition`에서 `applyOrientationForContent()`를 호출해
  **뷰어 서피스가 만들어지기 전에** 방향을 확정한다.
  결과적으로 서피스는 최종 방향에서 단 한 번 생성되고, 스트리밍 중 회전이 아예 일어나지 않는다.
- Desktop 대상은 목록에 크기가 없으므로 모니터 특성상 가로를 기본값으로 둔다.
- `applyViewerOrientation()`은 남겨두되 역할을 축소했다:
  목록이 알려준 크기와 실제 디코드 크기가 어긋난 드문 경우만 보정한다(`orientation_corrected` 로그).
- ROTATE 버튼과 빠른 설정의 방향 토글은 디코드 크기(없으면 expected 크기)를 근거로
  `applyOrientationForContent()`를 다시 호출한다.
- 진단 로그 추가: `orientation_preset`(선택 시 확정), `orientation_corrected`(사후 보정).

Validation / build / test result
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신
- 실기기 확인 필요: 가로 PC 카드를 누르면 **처음부터 가로로** 뷰어가 열리는지,
  진입 후 추가 회전이 없는지(`orientation_corrected`가 로그에 안 찍혀야 정상)

Next action
- 실기기 확인 후, `orientation_corrected`가 자주 찍히면 host가 보고하는 window 크기와
  실제 인코드 크기의 불일치를 따로 조사한다.

### 191) 2026-07-27 키보드 먹통 원인(포커스 탈취) 수정 + PC 키/단축키 패널 추가
Goal
- 사용자 리포트: "키보드가 뜬 채로 칠 곳을 터치해서 이동하는 순간 키보드가 먹통".
- 사용자 제안: OSLink처럼 커스텀 키보드(단축키/키보드 탭) 추가.

원인: 삼성 키보드 문제가 아니라 포커스 탈취
- `videoTextureView`가 `isFocusableInTouchMode = true`이고,
  ACTION_DOWN 처리에서 `view.requestFocus()`를 무조건 호출했다.
- 소프트 키보드는 숨은 `ImeCaptureView`가 포커스를 보유해야 살아 있다.
  영상 터치로 캐럿을 옮기는 순간 포커스가 `videoTextureView`로 넘어가
  `InputConnection`이 해제되고, 키보드는 화면에 남아 있지만 보낼 대상이 없어 먹통이 된다.
- 수정: `if (!viewerImeCaptureView.hasFocus()) view.requestFocus()`
  즉 IME가 포커스를 쥐고 있으면 빼앗지 않는다.

PC 키/단축키 패널 (`ViewerKeyPanel.kt`, `viewer_key_panel.xml`)
- 소프트 키보드는 원리적으로 텍스트만 만든다. Ctrl/Alt/Win, F1~F12, Ctrl+C 같은 조합은
  표현 자체가 불가능하므로 IME를 우회해 **Windows 가상키 down/up을 직접 전송**한다.
- 레일에 `KEYS` 버튼 추가 -> 하단 패널 토글. 탭 2개:
  - `단축키`: 복사/붙여넣기/잘라내기/전체선택/실행취소/저장/창닫기/새로고침/삭제/이름바꾸기/
    실행(Win+R)/탐색기(Win+E)/작업관리자(Ctrl+Shift+Esc)/창전환(Alt+Tab)/화면잠금(Win+L)/검색(Win+Q)
  - `키보드`: Esc+F1~F12, 숫자열, QWERTY 3행, Ctrl/Win/Alt/Space/Ins/Home/End/PgUp/PgDn/PrtSc, 방향키
- 조합키는 sticky 방식: Ctrl 탭하면 눌린 상태 유지 -> 다음 일반키 입력 시 자동 해제.
  한 손가락 터치로 코드를 표현하기 위한 선택이며, 현재 눌린 조합키를 패널 상단에 표시한다.
- 뷰어를 벗어나거나 패널을 닫으면 눌린 조합키를 모두 up으로 해제해 호스트에 키가 눌린 채 남지 않게 한다.
- 한글 텍스트 입력은 여전히 시스템 IME 담당(조합 필요). 패널은 키/조합 전용으로 역할 분리.

Validation / build / test result
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신(8.95MB)
- 실기기 확인 필요: 타이핑 중 화면 터치 후에도 키보드 유지, 단축키 탭 동작, sticky 조합키 동작

Next action
- 실기기 확인. 단축키 실제 동작 여부는 host의 SendInput 경로(187)와 함께 검증된다.
- 필요 시 단축키 목록을 사용자 편집 가능하게 확장(OSLink의 "단축키 추가"에 해당).

### 192) 2026-07-27 키 패널 미동작 수정(창 모드 경로) + 키보드 배열을 실제 키보드 형태로 재작성
Goal
- 사용자 리포트: "키보드 만든 거 동작 안 한다, 복사/붙여넣기 안 된다",
  "키보드 배치가 아니다 — 스크린샷처럼 키보드처럼 만들어야 한다".

원인 1: 창(window) 모드에서 키가 여전히 PostMessage로 나감
- 187에서 desktop 모드 kind 5/6만 `SendInput`으로 전환했고, window 모드 분기는
  `PostMessageW(WM_KEYDOWN/WM_KEYUP)`를 그대로 유지하고 있었다.
- 사용자가 Windows 탭에서 크롬 창을 직접 선택한 상태였다면 모든 키가 무시된다.
  특히 조합키는 posted message로는 실제 키 상태를 만들지 못하므로 Ctrl+C가 성립할 수 없다.
- 수정: window 모드에서도 대상 창이 포그라운드면 `send_desktop_virtual_key`(SendInput)를 사용하고,
  포커스가 없는 배경 창일 때만 기존 PostMessage 경로를 유지한다(배경 주입 설계 의도 보존).

원인 2: 키 배열이 키보드 모양이 아니었음
- 첫 구현이 `wrap_content` 버튼을 가로로 나열해 라벨 길이에 따라 키 폭이 제각각이었다.
- 재작성: 각 행을 `layout_weight` 기반 그리드로 구성. 일반 키 1 유닛,
  Tab 1.5 / Caps 1.75 / Enter 2.25 / Shift 2.25·1.75 / Space 5 / Back 2 등 실제 키보드 비율을 따른다.
  행이 서로 정렬되어 키보드 형태가 나온다.
- 스크린샷과 동일하게 키에 한글 자모 병기(두벌식): Q/ㅂ W/ㅈ E/ㄷ ... M/ㅡ,
  숫자열은 shift 기호 병기(1/! 2/@ ...), 기호키는 OEM 가상키로 매핑
  (`;:` OEM_1, `=+` OEM_PLUS, `,<` OEM_COMMA, `-_` OEM_MINUS, `.>` OEM_PERIOD,
   `/?` OEM_2, `` `~ `` OEM_3, `[{` OEM_4, `\|` OEM_5, `]}` OEM_6, `'"` OEM_7).
- 행 구성: Esc+F1~F12+PrtSc/Scr/Pause / 숫자열+Back / Tab+QWERTY / Caps+ASDF+Enter /
  Shift+ZXCV+Shift+↑ / Ctrl·Win·Alt·Space·Alt·Menu·Ctrl+←↓→ / Ins·Home·PgUp·Del·End·PgDn.
- 조합키 버튼은 눌린 상태를 alpha로 표시하고, 같은 vk의 좌/우 키가 함께 갱신되도록 버튼 목록을 유지한다.
- 기본 탭을 `키보드`로 변경(단축키는 보조).
- 단축키 목록 확장: 다시실행(Ctrl+Y), 찾기(Ctrl+F), 바탕화면(Win+D), 창 캡처(Alt+PrtSc) 추가.

Validation / build / test result
- Windows Release/Debug 빌드 성공, `shared_core_test` PASS
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신(8.95MB)
- 호스트 재빌드를 위해 실행 중이던 호스트를 종료함(접속된 클라이언트 없음 확인 후).
  **새 호스트 실행 필요** — 키 입력 수정은 호스트 측 변경이므로 재시작해야 반영된다.

Next action
- 실기기 확인: 단축키 탭 복사/붙여넣기, 키보드 탭 Ctrl+C, 배열이 키보드 형태로 보이는지.
- 여전히 안 되면 host 로그의 `inputEvents`/`inputInjectFail`/resolved target을 확인해
  desktop/window 어느 경로로 들어가는지 판별한다.

### 193) 2026-07-28 키패널이 영상을 덮던 문제 수정
Goal
- 사용자 확인: "키보드 아주 잘 먹는다"(한글/기호/공백 모두 전달 확인).
- 남은 문제: "내가 무슨 글을 썼는지 볼 수가 없네" — 입력 결과가 보이지 않는다.

원인
- 191에서 키패널을 `viewerVideoFrame`(FrameLayout) 안에 `layout_gravity="bottom"`으로 넣었다.
  즉 영상 위에 겹쳐 뜨는 구조라, 타이핑 대상이 있는 화면 하단을 그대로 가린다.
- 188에서 "버튼이 영상을 가리면 안 된다"는 요구로 컨트롤 바를 레일로 빼놓고도,
  키패널에서 같은 실수를 반복했다.

수정
- 키패널을 `viewerVideoFrame` 밖으로 꺼내 `viewerScene`의 형제로 이동.
  `viewerScene`을 세로 방향으로 두고 `viewerSplit`(영상+레일)에 `weight=1`,
  키패널은 그 아래 `wrap_content`로 배치했다.
  이제 패널을 열면 영상이 위로 밀려 올라가며 축소될 뿐, 가려지지 않는다.
  영상은 기존 aspect-fit 로직이 레이아웃 변경에 반응해 새 영역에 다시 맞춘다.
- 높이 상한 추가: 7행 키보드는 가로 모드 폰에서 화면 대부분을 차지할 수 있으므로,
  `show()`에서 부모 높이의 55%로 제한한다. `hide()`에서 `WRAP_CONTENT`로 되돌린다.
- 세로 LinearLayout에서 무의미해진 `layout_gravity="bottom"` 제거.

Validation / build / test result
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신
- 실기기 확인 필요: 키패널을 열었을 때 입력 중인 내용이 보이는지, 영상이 축소되어도 조작 가능한지

Next action
- 확인 후, 가로 모드에서 55% 상한이 적절한지 조정(키 높이 축소 또는 행 접기 옵션 검토).

### 194) 2026-07-28 우클릭 지원 (레터박스 여백을 조합키로 사용)
Goal
- 사용자 요청: "우측 클릭 버튼을 할 수가 없다. 왼쪽 여백 공간을 클릭한 채로 터치하면
  마우스 오른쪽 버튼 효과가 나면 좋겠다."

배경
- 기존 터치 경로는 단일 포인터 + 좌클릭 고정이었다.
  `mapTouchToVideoCoords(clampToContent=false)`가 콘텐츠 밖(레터박스 여백) 터치에 null을 반환하고
  그대로 버려서, 여백은 완전한 사각지대였다.
- 호스트는 이미 우클릭을 지원한다:
  `mouse_vk_to_sendinput_flag`가 `VK_RBUTTON`을 `MOUSEEVENTF_RIGHTDOWN/RIGHTUP`으로,
  window 모드에서는 `mouse_vk_to_message`가 `WM_RBUTTONDOWN/UP`으로 매핑한다.
  즉 클라이언트가 우클릭을 보내지 않았을 뿐이다.

구현
- 여백 터치를 버리지 않고 **조합키로 사용**한다. 콘텐츠 밖에 내려온 포인터를
  `rightClickModifierPointerId`로 붙잡아 두고, 그 상태에서 화면(콘텐츠)에 내려오는 터치는
  좌클릭 대신 우클릭으로 전송한다.
- 눌림/드래그/뗌 전 구간에서 같은 버튼을 유지하도록 `activeTouchIsSecondary`를 도입해
  DOWN/UP/취소 경로가 모두 동일 버튼(`INPUT_VK_RBUTTON`/`INPUT_BUTTON_SECONDARY`)을 쓴다.
- 여백 손가락을 떼거나 제스처가 취소되면 조합키를 해제한다.
- 여백 터치가 이제 유효 이벤트이므로 `ACTION_POINTER_DOWN`에서 기존 포인터 유무 검사보다
  먼저 여백 판정을 수행하도록 순서를 조정했다.
- 발견성 문제: 여백을 누르는 제스처는 화면에 아무 단서가 없다.
  뷰어 진입 시 세션당 1회 Toast로 안내한다(`viewer_right_click_hint`, 한국어 리소스 포함).

Validation / build / test result
- Android `clean assembleDebug` 성공, `dist/remote60-android-20260727.apk` 갱신
- 호스트 측 우클릭 매핑은 기존 코드로 확인(추가 변경 없음)
- 실기기 확인 필요: 여백 누른 채 터치 시 컨텍스트 메뉴가 뜨는지, 여백에서 손을 떼면 좌클릭으로 복귀하는지

Next action
- 세로 모드에서는 여백이 좌우가 아니라 상하에 생기므로, 실제 사용감 확인 후
  필요하면 안내 문구를 방향에 맞게 조정한다.

### 195) 2026-07-28 텍스트 화면 흐림 원인: CBR + QP 상한 부재
Goal
- 사용자 리포트: "아직도 한 번씩 화면이 엄청 흐린데, 특히 텍스트 많은 화면에서.
  그러고 다시 정상으로 돌아온다."

원인
- 인코더가 `eAVEncCommonRateControlMode_CBR`(고정 비트레이트)로 동작하고 있었고,
  `AVEncCommonMaxBitRate`가 평균의 1.1배(stable_text는 1.3배)에 불과했다.
- 화면 콘텐츠는 본질적으로 버스트다. 텍스트가 빽빽한 장면 전환은 한 프레임에
  평균의 수 배에 달하는 비트를 요구하는데, CBR은 이를 허용하지 않는다.
- 인코더에 남은 유일한 수단은 양자화 계수(QP) 상향뿐이므로,
  **텍스트를 뭉개서 용량을 맞추고** 이후 P-프레임이 1초에 걸쳐 디테일을 복원한다.
  사용자가 본 "흐려졌다가 다시 선명해짐"이 정확히 이 과정이다.
- `AVEncVideoMaxQP`가 설정되어 있지 않아 화질 하한선도 없었다.

수정
- 기본 rate control을 `PeakConstrainedVBR`로 전환. 평균은 목표 비트레이트, 피크는 3배.
  버스트 프레임이 비트를 빌려 쓸 수 있고, 정적 장면에서는 오히려 덜 쓴다.
- `CODECAPI_AVEncVideoMaxQP = 32` 설정으로 화질 하한선을 둔다.
  피크마저 소진돼도 가독성 이하로는 떨어지지 않는다.
- 세 값 모두 환경변수로 조정 가능:
  `REMOTE60_NATIVE_RATE_CONTROL=cbr`(구동작 복귀),
  `REMOTE60_NATIVE_PEAK_BITRATE_PERCENT`(기본 300),
  `REMOTE60_NATIVE_MAX_QP`(기본 32, 0이면 미설정).
- MFT마다 지원 여부가 다르고 거부되면 이전 모드가 그대로 남으므로,
  **요청값이 아니라 실제 수용 여부를 로그로 출력**한다.

Validation / build / test result
- 실측(AMD 하드웨어 인코더, 1080p/8Mbps/keyint60):
  `rate-control mode=vbr_peak modeAccepted=1 mean=8000000 peak=24000000 vbvBytes=200000
   maxQp=32 maxQpAccepted=1`
  -> **두 설정 모두 수용됨**.
- 비트레이트가 콘텐츠에 따라 0.55~2.7 Mbps로 변동(VBR 정상 동작, 정적 구간에서 절약).
- Windows Release/Debug 빌드 성공, `shared_core_test` PASS
- localhost 게이트 `OVERALL_OK=True`, UDP 드롭/malformed 0

Next action
- 실기기에서 텍스트 많은 화면 전환 시 흐림이 사라졌는지 확인.
- 여전하면 `REMOTE60_NATIVE_MAX_QP=28`로 더 조이거나 피크를 400%로 올려 A/B.
- 반대로 모바일 데이터에서 피크 3배가 부담되면 `PEAK_BITRATE_PERCENT=180` 권장.

### 196) 2026-07-28 계정/호스트 디렉터리 서버 1차 구현 (D1)
Goal
- 사용자 확정 요구: 클라이언트를 켜면 로그인 -> 그 계정의 호스트 목록만 표시 -> 선택 시 연결.
  호스트도 같은 id/pw로 자기를 등록(1회 입력 후 캐싱, 변경 가능).
  회사 PC처럼 인바운드가 막힌 환경에서도 접속.
- 방식 결정: **B안(기존 UDP 경로 유지 + 홀펀칭 추가)**. WebRTC 전환(A안)은 채택하지 않음.

설계 요지 (`docs/계정_호스트등록_홀펀칭_설계.md`)
- 방화벽은 아웃바운드를 허용하므로 호스트와 클라이언트가 **양쪽 다 밖으로 나가** 서버에서 만난다.
- 서버는 주소만 교환하고 **영상은 통과시키지 않는다** -> 저사양 인스턴스로 충분.
- 릴레이(TURN)는 홀펀칭 실패 환경이 실제 확인될 때까지 만들지 않는다.

구현 (`apps/directory/`)
- `POST /api/login` -> sessionToken(12h). 계정 존재 여부를 응답으로 구분할 수 없게 통일.
- `POST /api/host/register` -> hostId/hostToken. `machineId` 기준이라 재설치해도 목록이 늘지 않는다.
- `POST /api/host/heartbeat` -> 공인 주소 갱신 + `pendingPunch` 전달(1회 소비). 30초 주기, 90초 무응답 시 오프라인.
- `GET /api/hosts` -> 해당 계정의 호스트만, 온라인 우선 정렬.
- `POST /api/connect` -> 호스트 공인 주소 + 1회용 punchToken 반환, 동시에 호스트에 클라 주소 예약.
- **UDP 관측(STUN 최소 구현)**: 미디어에 쓸 바로 그 소켓으로 `OBSERVE <token>`을 보내면
  서버가 관측한 `ip:port`를 회신한다. NAT는 포트까지 바꾸므로 공인 IP만으로는 부족하다.
- 비밀번호는 scrypt + 계정별 salt, 비교는 `timingSafeEqual`. 평문 저장/로그 없음.
- 저장은 tmp 파일 기록 후 rename(원자적)으로 half-written 상태를 만들지 않는다.

테스트로 잡은 결함
- 최초 구현의 로그인 백오프가 **1회 실패에 즉시 1초 차단**이었다.
  오타 한 번에 "시도가 너무 많습니다"가 뜨는 동작이라, 3회까지는 지연 없이 허용하고
  그 이후부터 지수 백오프(최대 30초)로 변경했다.

Validation / build / test result
- `node apps/directory/test/run.js` -> **14개 검사 ALL PASS**
  (오답 거부 / 로그인 / 빈 목록 / 잘못된 세션 거부 / 등록 / 재등록 중복 없음 /
   UDP 관측 포트 일치 / 하트비트 / 관측 포트 반영 / 목록·온라인 / connect 주소 반환 /
   펀치 전달 / 펀치 1회 소비 / 없는 호스트 404)
- 테스트는 18080/18081 포트에 임시 서버를 띄우고 종료 시 정리한다.

보안 메모
- 운영에서는 TLS 필수(미설정 시 기동 경고 출력). 토큰이 평문으로 흐르면 무의미하다.
- **미디어 자체는 아직 평문**이다. 홀펀칭으로 인터넷을 건너가게 되면 실제 위험이므로 D5로 남긴다.
- 실제 자격증명은 저장소에 두지 않는다. 계정 생성은 `--add-account` CLI로 서버에서 직접 수행.

Next action
- D2: 호스트에 등록/하트비트/펀치 + 토큰 캐싱 붙이기
- D3: 안드로이드 로그인 화면 + 호스트 목록 + 펀치
- D4: 호스트 GUI(로그인 창/트레이)

### 197) 2026-07-29 제어 채널을 UDP로 이전 (홀펀칭 전제조건)

배경 / 문제
- D2를 붙이던 중 설계의 구멍이 드러났다. 영상은 UDP라 홀펀칭이 되는데,
  **제어 채널은 별도 TCP 연결**이었다. NAT 뒤 호스트에는 인바운드 TCP가 닿지 않으므로
  디렉터리로 호스트를 찾아도 창 목록·입력·설정이 전부 죽는다. 화면만 보이고 조작이 안 되는 상태.
- 설계 문서의 "기존 영상/입력 프로토콜은 그대로 둔다"는 전제가 틀렸다.

해결
- 제어 프로토콜을 **뚫린 미디어 소켓 위로** 옮겼다. 프로토콜 자체는 그대로 두고,
  전송만 교체할 수 있도록 `ControlLink` 추상화를 넣었다(TCP/UDP 두 구현).
- `UdpControlChannel`: 메시지 단위 신뢰 전송. 요청/응답이 엄격히 교대하는 프로토콜이라
  슬라이딩 윈도우나 바이트 스트림이 필요 없다. 메시지를 조각으로 한 번에 쏘고,
  받는 쪽이 빠진 조각만 NACK으로 요구한다. 완성되면 ACK, 무응답이면 전체 재전송.
- 호스트는 미디어 소켓 전용 수신 스레드를 두었다. 이전에는 렌더 루프에서 폴링했는데,
  그대로 두면 제어 메시지가 다음 프레임까지 기다려 입력 지연이 생긴다.

발견하고 고친 결함
- `CanQueueControlRequestLocked`가 **TCP 소켓 존재 여부**로 판단하고 있어서,
  UDP 경로에서는 창 선택·입력·스트림 시작 요청이 전부 조용히 거부됐다. 전송 종류와 무관하게
  "제어 링크가 있는가"로 바꿨다. 테스트에서 잡히지 않았으면 화면만 나오고 아무것도 안 되는 증상이 됐을 것.
- `TcpControlLink`가 소켓 핸들을 복사해 들고 있어, 연결 종료 후에도 그 핸들을 계속 썼다.
  핸들은 재사용되므로 엉뚱한 소켓에 쓸 수 있다. getter로 매번 현재 값을 읽도록 변경.

Validation
- `remote60_udp_control_channel_test`: 손실·재정렬·중복 네트워크에서 **5개 케이스 ALL PASS**.
  400KB 메시지(썸네일 최대 크기)를 5% 손실 링크로 통과시켜 바이트 단위 일치 확인.
- `remote60_udp_control_e2e_test`: **실제 호스트에 붙여 9개 검사 ALL PASS**.
  창 목록(10개) 수신, 데스크톱 선택 왕복, 스트림 시작, 영상 프레임 수신, 입력 이벤트까지 터널 경유.
- `remote60_native_video_client_shared_core_test`: PASS (기존 TCP 경로 무회귀)

### 198) 2026-07-29 호스트 GUI 앱 + 안드로이드 로그인/호스트 목록 (D2~D4)

호스트 앱 (`remote60_host_app`)
- 콘솔 플래그로 돌리던 호스트를 사용자가 설치해 쓰는 형태로 감쌌다.
  로그인 창(서버/아이디/비밀번호/PC 이름) -> 트레이 상주 -> 상태 표시(실행 중/재시작 횟수).
- 스트리밍은 그대로 `remote60_native_video_host_poc.exe` 자식 프로세스가 담당한다.
  죽으면 감시 스레드가 재시작한다. 캡처 장치 분실 같은 대부분의 실패에 대해 옳은 대응이다.
- 트레이 메뉴: 열기 / 계정 변경 / 로그아웃 / 종료. "Windows 시작 시 자동 실행" 체크박스.
- **비밀번호는 디스크에 쓰지 않는다.** 토큰만 `%LOCALAPPDATA%\remote60\host.json`에 캐싱.

안드로이드
- 로그인 화면 -> 내 PC 목록 -> 선택 시 연결. IP를 몰라도 된다. "IP로 직접 연결"은 LAN용으로 유지.
- 주소 관측과 펀칭은 **네이티브**에서 수행한다. NAT는 소켓마다 다른 포트를 매핑하므로,
  미디어에 쓸 바로 그 소켓으로 관측해야 호스트가 실제로 닿는 주소가 나온다.
  HTTP(로그인/목록/connect)만 Kotlin에서 처리하고, 관측·펀치는 JNI로 내렸다.

발견하고 고친 결함
- 호스트 앱이 띄운 자식이 즉시 종료되고 무한 재시작하고 있었다.
  H.264가 아직 빌드 타임 실험 플래그 뒤에 있어 `--codec h264`를 거부한 것.
  제품에 다른 경로가 없으므로 자식 환경변수로 켜도록 했다.

Validation
- 실제 로그인 창을 조작해 검증(9개 검사 ALL PASS): 창/컨트롤 존재, **오답 비밀번호 거부**,
  로그인 성공, 성공 후 입력 필드 숨김, 자동 실행 옵션 노출, 토큰 캐싱,
  **비밀번호가 디스크에 없음**, 지정한 PC 이름 저장, 스트리밍 자식 기동.
- 디렉터리 API로 재확인: 앱이 띄운 호스트가 사용자가 정한 이름으로 온라인 표시됨.

미검증 / 막힌 것
- **안드로이드 APK 빌드 불가**: 이 PC에 Gradle과 JDK가 없다. Kotlin/JNI 코드는 컴파일 검증되지 않았다.
- **네이버클라우드 배포 대기**: `automation/deploy_directory.ps1` 준비 완료.
  공개키를 서버에 한 번 등록해야 자동 배포가 가능하다.

### 199) 2026-07-30 Host/Client 최적화·UI 전수 감사

현재 작업 목표
- Host 관리 앱·영상 Host·Windows Client·Android GNLink Client를 각각 검토하고,
  성능 병목과 UI 개선 필요 사항을 구현 우선순위로 정리한다.

변경 사항
- `docs/Host_Client_최적화_UI_감사_20260730.md`에 코드 감사, 실행 화면 판정,
  1080p30 진단 수치, 우선순위와 다음 성능 Gate를 기록했다.
- Windows Client가 이미 선택된 대상으로 진입할 때 stream-state 활성화 요청을 보내지 않는
  검은 화면 결함을 P0로 분류했다.
- Host 캡처의 동기 readback, CPU crop/NV12, 반복 할당·복사를 M1.6 핵심 병목으로 재확인했다.
- Host 로그인 후 잔여 라벨·상태 잘림, Windows viewer 상태 정보 부족,
  Android 설정/landscape rail 과밀과 썸네일 갱신 문제를 UI 개선 항목으로 분류했다.
- 평문 directory API와 미암호화 영상·제어를 외부 배포 전 P0 보안 항목으로 기록했다.

완료 결과
- 격리 포트 1080p30 진단은 평균 19.42fps로 목표 27fps에 미달했다.
- Host 약 49.65%, Windows Client 약 23.73%의 단일 코어 사용량을 확인했다.
- Host 관리 앱과 Android 로그인 화면 idle은 CPU 0% 표본으로, 우선 최적화 대상에서 제외했다.
- Android MediaCodec Surface 출력과 Windows 대상 카드 UI는 유지할 구조로 판정했다.

검증
- CMake Debug Host/Windows Client/shared core/UDP control/input macro target build: PASS
- `remote60_native_video_client_shared_core_test.exe`: PASS
- `remote60_udp_control_channel_test.exe`: 5/5 PASS
- `remote60_input_macro_test.exe`: 23개 검사 PASS
- `node apps/directory/test/run.js`: 전체 PASS
- Android `:app:assembleDebug`: PASS, 4 ABI native build 포함

다음 작업
- P0 Windows stream-state 결함과 Host signed-in UI를 먼저 수정한다.
- 이후 M1.6을 callback copy-only → worker readback ring → GPU crop/resize/NV12 순서로
  한 단계씩 적용하고 동일 장면 5회 A/B 측정을 수행한다.

### 200) 2026-07-30 Host/Client 최적화·UI 상세 구현계획

현재 작업 목표
- 감사 결과를 실제 구현자가 파일·함수·검증 기준에 따라 순차 작업할 수 있는 상세 계획으로 전환한다.

변경 사항
- `docs/Host_Client_최적화_UI_상세계획_20260730.md`를 추가했다.
- Windows stream-state 결함, Host signed-in UI, Release 기준선 자동화,
  Host callback/readback/GPU NV12, Windows 렌더, Android 상태·썸네일,
  공통 UI와 HTTPS/미디어 암호화를 F1~G1 작업 ID로 분리했다.
- 각 작업에 수정 파일, 구현 순서, 테스트, 성능 완료 기준과 롤백 조건을 기록했다.
- 현재 자동 검증의 Debug 경로 고정과 동일 이름 프로세스 일괄 종료 문제를 B1 선행 작업에 포함했다.

완료 결과
- 기능 수정은 F1/U1, 성능 수정은 B1/H1~H4/C1~C2/A1~A2,
  제품 UI는 U2, 외부 배포 보안은 S1~S2 순서로 착수할 수 있다.
- M1.6은 callback copy-only, GPU-front crop/resize, NV12 D3D surface 직접 인코딩으로
  독립 검증·롤백 가능한 단계가 됐다.

검증
- 계획에 적은 source, test target, automation script 경로 존재 확인
- 최신 shared scheduler가 stream-state를 window-select보다 먼저 소비하는 순서 확인
- 기존 D3D manager, CPU NV12 sample copy, Android 250ms poll과 thumbnail cache 조건 재확인
- 문서 변경만 수행했으므로 코드 build/test는 재실행하지 않음

다음 작업
- F1 Windows stream-state 검은 화면 결함부터 구현한다.
- 이어서 U1 Host signed-in UI를 수정하고 B1 Release 격리 기준선을 수집한다.

### 201) 2026-07-31 검은 화면 결함 Host측 해소·모바일 UX 정리·문서 현행화

현재 작업 목표
- 감사 P0였던 "제어는 되는데 영상이 검다" 상태의 실제 원인을 제거하고, 사용자 피드백으로
  들어온 모바일 UX 문제를 정리한 뒤 계획 문서를 현행화한다.

변경 사항
- Host(`bf19eee`): UDP 리더 스레드가 길이 0 데이터그램·알 수 없는 recv 오류로 조용히 죽던
  문제 수정(리더가 죽으면 Hello를 못 읽어 영상 피어를 새 클라이언트로 넘기지 못했다).
  새 제어 세션 시작 시 스트림을 기본 활성으로 복원해, stream-state를 보내지 않는 Windows
  Client도 재연결 후 영상을 받는다. 재현 검증: A 스트리밍 → 길이 0 주입 → A 강제 종료 →
  B 접속만으로 수신.
- 매크로(`1cb37a3`~`b2acc08`): 일시정지/재개(기록·재생, 정지 구간 무흔적), 스텝 편집·삭제
  (지연 병합), 이름 저장/불러오기(공용 텍스트 포맷), Windows 클라이언트 전용 매크로 창.
  엔진 테스트 23 → 46개.
- Android 뷰어 UX: 좌측 존 바(우클릭/태블릿/마우스) — 레터박스 여백 의존 제거로 16:9에서도
  동작, 태블릿 모드 잠금 버튼(`d7d2afa`), 자연 스크롤·화면 마우스 클러스터 고정·레일 축소
  (`289609c`), 세로 회전 레이아웃 붕괴 수정(`2f47497`), 키보드 패널 잘림 수정.
- 문서: 감사·상세계획에 현행화 부기 추가, 구현계획 체크리스트에서 P0 검은 화면을 완료로
  전환하고 클라이언트 stream-state 호출은 절전 목적 P2로 강등.

검증
- `remote60_input_macro_test.exe`: 46개 검사 PASS
- LDPlayer 실기기: 존 바 3종, 잠금 유지·해제, 자연 스크롤 방향, 매크로 녹화/일시정지/편집/
  저장/재생, 세로↔가로 왕복, 디렉터리 로그인→호스트 목록→펀치→뷰어 전체 흐름
- Windows Client: 연결/영상/매크로 창/저장 목록, 재연결 후 무조작 영상 수신

다음 작업
- U1 Host signed-in UI 재배치부터 진행한다(F1 결함은 해소, 절전 동기화는 P2).
- 이후 B1 Release 격리 기준선을 수집하고 M1.6(H1~H3)을 단계별로 적용한다.

### 202) 2026-07-31 Host/Client 최적화 상세계획 2차 코드 검증

현재 작업 목표
- 상세계획에 기록된 1차 검증 결과를 실제 Host/Windows/Android 코드와 로그에 다시 대조하고,
  누락된 화질·프레임 정합성 문제를 추가하며 부정확한 구현 전제를 바로잡는다.

변경 사항
- `docs/Host_Client_최적화_UI_상세계획_20260730.md`
  - Q1을 추가해 Windows MFT의 `1920x1080 → coded 1920x1088` visible aperture 누락,
    runtime bitrate 변경 시 rate-control peak/VBV와 UDP pacing target 불일치,
    제품 Host와 검증 profile의 encoder tune 차이를 선행 작업으로 고정했다.
  - H1 frame gating을 최종 GPU surface 경로까지 유지되는 설계로 보강했다.
  - H2의 `GpuBgraScaler`가 shader가 아니라 D3D11 video processor라는 점과 실제 transfer
    leg 3개를 정확히 기록했다.
  - H3 surface pool 수명을 `ProcessInput` 반환이 아니라 async MFT 참조 해제까지 보장하도록
    수정하고, C2 direct surface에도 visible rect를 적용하도록 연결했다.
  - A1 JNI 직접 호출 6회를 확인했으나 전체 `renderStatus()` 호출 그래프의 scene별 합계는
    후속 3차 검증에서 비뷰어 9회/VIEWER 9회/SWITCHING 8회로 재정정했다(203번 참조).
  - A2의 Host version이 콘텐츠 버전이 아닌 매 fetch 시각임을 확인해, TTL 갱신 시 wire BGRA
    content hash 기반 로컬 동일성 비교와 negotiated conditional-fetch 확장을 구분했다.
- `docs/구현계획.md`
  - 검증 전용 체크리스트에 상세계획 2차 코드 검증·정정 완료 상태만 추가했다.

검증
- `mf_h264_codec.cpp`: Windows decoder가 `MF_MT_FRAME_SIZE`만 읽고 aperture를 읽지 않으며,
  encoder 초기화의 peak 정책과 `reconfigure_bitrate()`의 110/130% 정책이 다른 것을 확인했다.
- `logs/audit_20260730/perf`: Host `1920x1080`, Windows Client `1920x1088` 반복 로그를 확인했다.
- `native_video_host_main.cpp`: UDP pacing bitrate가 시작 시 한 번만 저장되고, 제품 기본 tune은
  `low_latency`이며 thumbnail response version은 매 fetch `qpc_now_us()`인 것을 확인했다.
- `host_app_main.cpp`: child에 encoded experiment만 설정하고 encoder tune은 지정하지 않음을 확인했다.
- `native_video_client_session.*`/`poc_protocol.hpp`: request/list에 이전 thumbnail version이 없고,
  기존 cache version은 Host timestamp를 그대로 저장함을 확인했다.
- `MainActivity.kt`: `renderStatus()` 직접 호출 6회와 기존 thumbnail version gate를 확인했다.
  scene별 전체 합계는 후속 3차 검증에서 비뷰어 9회/VIEWER 9회/SWITCHING 8회로 확정했다.
- 문서 전용 변경이므로 C++/Android build와 런타임 성능 테스트는 실행하지 않았다.

다음 작업
- B1 격리 Release 기준선을 먼저 고정한다.
- 이어서 Q1-1 visible aperture, Q1-2 rate-control/pacing, Q1-3 제품 preset A/B를 순서대로
  구현·측정한 뒤 H1~H3 GPU 경로 최적화에 착수한다.

### 203) 2026-07-31 Android A1 JNI 씬별 호출 수 재정정

현재 작업 목표
- 2차 검증에서 "기본 6회, VIEWER 9회"로 적은 A1 JNI 호출 수가 전체 호출 그래프 기준으로
  맞는지 다시 확인하고, 잘못된 수치와 구현 계획을 정정한다.

변경 사항
- `docs/Host_Client_최적화_UI_상세계획_20260730.md`
  - 씬별 안정 tick을 비뷰어 9회, VIEWER 9회, SWITCHING 8회로 확정한 3차 검증 결과를 유지했다.
  - `renderTargetsScene()`/`renderViewerScene()`을 활성 scene에서만 호출하도록 A1 구현 단계를
    추가했다.
  - 비뷰어 tick의 viewer data/presentation/video-size 조회와
    `applySceneVisibility()`의 매 tick `nativeMacroState()` 조회 제거를 완료 기준에 추가했다.
- `docs/구현계획.md`
  - 기존 검증 체크 항목을 2·3차 검증과 A1 재정정 내용을 포함하도록 현행화했다.
- 202번 이력의 잘못된 A1 합계 표현에 후속 정정 참조를 남겼다.

검증
- `renderStatus()`의 직접 JNI getter 6회 확인.
- `renderViewerScene()`은 모든 scene에서 호출되고 SWITCHING만 조기 반환함을 확인:
  비뷰어와 VIEWER에서 data usage/presentation 조회 2회 추가.
- `applySceneVisibility()`는 비VIEWER에서 `nativeMacroState()`를 1회 호출함을 확인.
- `syncVideoSurface()`는 VIEWER/SWITCHING에서 `nativeGetVideoSizePacked()`를 1회 호출함을 확인.
- 따라서 안정 tick 합계는 LOGIN/HOSTS/CONNECT/TARGETS 9회, VIEWER 9회, SWITCHING 8회다.
- 문서 전용 변경이므로 Android build/runtime test는 실행하지 않았다.

다음 작업
- A1 구현 시 snapshot/version 통합 전에 inactive-scene renderer 호출 차단을 독립 커밋으로
  적용하고, scene별 JNI 카운터 또는 trace로 9/9/8 → 목표값 감소를 검증한다.

### 204) 2026-07-31 U1 Host signed-in UI 수정

작업 ID: U1

변경 파일
- `apps/native_poc/src/host_app_main.cpp`
- 신규 `apps/native_poc/host_app.rc`, `apps/native_poc/res/gnlink.ico`
- `apps/native_poc/CMakeLists.txt`

변경 전 문제
- 로그인 라벨 4개(Server/ID/Password/PC name)가 control id 0의 익명 라벨이라 AppState가
  참조를 갖지 못했고, signed-in 전환 시 숨길 수 없어 상태 카드 위에 그대로 남았다.
- statusLabel 고정 40px에 3줄 상태 문구가 잘렸고, 레이아웃이 96dpi 픽셀 하드코딩이라
  DPI 변경 시 재배치가 없었다. 창 제목/트레이가 "remote60"이었고 아이콘은 기본
  IDI_APPLICATION이었다.

구현 내용
- 모든 control(라벨 포함)을 AppState 소유로 만들고 `layout_signed_out()` /
  `layout_signed_in()`으로 상태별 배치와 창 높이를 분리했다. 전 좌표를 DPI 스케일
  `sc()`로 계산하고 `WM_DPICHANGED`에서 폰트 재생성 + 재배치한다.
- signed-out: 제목/설명, ID·비밀번호·PC 이름, 기본 접힘 "Advanced settings" 안의
  Server 주소, 계정 생성 체크+signup key, 기본 버튼 Sign in. 빈 서버 주소로 로그인
  시 고급 설정을 자동으로 펼친다.
- signed-in: 계정/PC 이름, 상태 badge(STARTING/REACHABLE/SIGN IN AGAIN/NOT
  REACHABLE - 텍스트가 상태를 전달하고 색은 보조), 3줄 상세, 자동 시작, Change
  account/Sign out/Open log. 창 높이가 카드 크기로 줄어든다.
- 자식 스트리밍 호스트 stdout을 `%LOCALAPPDATA%\GNLink\host_app.log`에 기록(2MB
  rotate)하고 Open log 버튼으로 연다. 토큰만 기록되는 기존 원칙 유지, 비밀번호는
  로그/캐시 어디에도 남지 않는다.
- 사용자 노출 명칭을 GNLink Host로 통일(창 제목/트레이/메뉴). 내부 식별자(창 클래스,
  Run value, 캐시 경로)는 remote60 유지. gnlink.ico(16~256px)를 .rc로 연결해 창/트레이
  아이콘에 사용.
- `--ui-preview[=signedin]` 플래그: 캐시를 읽지 않고 자식도 띄우지 않는 레이아웃 검증
  전용 모드.

실행한 build/test
- Debug 빌드 경고 0. input_macro(46) / shared_core / udp_control_channel 테스트 ALL PASS.
- udp_control_e2e_test는 기본 포트 43000이 실행 중인 실제 Host라 접속해 버리는 문제를
  확인, 격리 포트 44100에 전용 host poc를 띄워 실행해 ALL PASS(9/9). B1에서 이 격리
  실행을 스크립트로 굳힌다.
- `--ui-preview` 스크린샷으로 signed-out(접힘/펼침), signed-in 카드 검증: signed-in
  화면에 로그인 control 잔존 0, 문구 잘림 없음, 상태별 창 높이 전환 확인.

Before/After 지표: UI 작업으로 성능 지표 변화 없음(성능 무영향).

fallback/부작용: 아이콘 로드 실패 시 기존 기본 아이콘 경로 유지. DPI 150/200% 실측은
현 모니터 DPI 제약으로 코드 검증만 수행 - 실기기 확인 필요 시 후속.

미완료: 없음.

다음 작업: B1 Release 기준선·격리 실행기.

### 205) 2026-07-31 B1 Release 성능 기준선·격리 실행기

작업 ID: B1

변경 파일
- `automation/verify_native_video_runtime.ps1` (-Configuration 인자, 포트 소유자 검사로
  이름 기반 일괄 종료 제거, 실행 중 CPU/working set 샘플링, run-metadata.json)
- `automation/verify_native_video_scene_suite.ps1` (-Configuration 전달)
- 신규 `automation/run_perf_baseline.ps1` (매트릭스 × 반복, run별 JSON)
- 신규 `automation/perf_scene_generator.ps1` (합성 scroll/video 장면)
- 신규 `automation/perf_display_keepalive.ps1` (ES_DISPLAY_REQUIRED)
- 신규 `automation/compare_optimization_runs.ps1` (중앙값/최소/최대, before/after delta)

변경 전 문제
- 실행 파일 경로에 Debug가 리터럴로 박혀 Release 측정 불가. 시작 시 이름으로
  remote60_* 프로세스를 전부 강제 종료해 2026-07-30 감사가 사용자 Host를 죽였다.
- CPU/메모리 지표가 없고 결과가 stdout 텍스트뿐이라 회귀 비교가 수작업이었다.

구현 내용
- 포트 사용 중이면 소유 PID를 출력하고 실패(격리 포트 안내). 스크립트가 시작한 PID만 종료.
- 클라이언트 생존 중 500ms 간격으로 host/client TotalProcessorTime·WorkingSet 샘플링 →
  HOST/CLIENT_CPU_SINGLE_CORE_PCT, PEAK_WS_MB 지표 추가. commit/구성/HW/포트/PID를
  run-metadata.json으로 보존.
- 기준선 러너: 1080p30 static/scroll/video + 720p30 scroll × 5회, 격리 포트 44100/44101,
  장면은 합성 생성기(사전 렌더 비트맵 DrawImage)로 재현 가능하게 고정.
- 함정 2개를 수정하며 배웠다: (1) PowerShell Paint 핸들러에서 프레임마다 GDI+ 호출
  40여 개를 그리면 ~250ms/frame이라 "30fps 장면"이 실제로는 4fps가 된다 - 사전 렌더
  비트맵 1~2회 DrawImage로 교체. (2) 입력 유휴로 디스플레이가 꺼지면 WGC가 프레임을
  안 밀어주므로 keep-alive가 기준선 수집의 전제다.

실행한 build/test
- Release host/client 빌드. 스모크 1회 → 20/20 런 전부 OVERALL_OK=True.

Before/After 지표 (1차 Release 기준선, baseline-b1-pre-q1, commit f9b5435)
- 1080p-scroll(주 비교): DEC 중앙값 22.44fps(21.11~23), Host CPU 63.19%, Client CPU
  52.29%, NV12 6.77ms, enc 4.87ms, captureCopyMap 1.09ms, captureMemcpy 0.84ms,
  queueToSend 52.1ms, LAT_P95 4.65ms
- 1080p-static: DEC 19.67, Host CPU 57.46%, queueToSend 36.4ms
- 1080p-video: DEC 25.44, Host CPU 54.43%, queueToSend 15.8ms
- 720p-scroll: DEC 22.78, Host CPU 62.3%, scale 4.34ms(다운스케일 경로), NV12 3.19ms
- Debug 감사 수치(19.42fps/NV12 8.38ms)와 방향 일치. queue-to-send가 Release에서도
  최대 병목으로 확인 - Q1-2/H4의 근거가 강화됐다.

fallback/부작용: 기준선 원본은 automation/logs/baseline-b1-pre-q1 (gitignore 대상,
로컬 보존). Q1 병합 후 재수집 예정이라 이 수치는 Q1 이후 작업과 비교하지 않는다.

미완료: 없음.

다음 작업: Q1-1 visible aperture.

### 206) 2026-07-31 Q1-1 Windows visible aperture와 coded size 분리

작업 ID: Q1-1

변경 파일
- `apps/native_poc/src/mf_h264_codec.hpp/.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`

변경 전 문제
- H.264 coded height는 16행 정렬이라 1080p가 1088행 평면으로 디코드되는데,
  `query_output_size()`가 `MF_MT_FRAME_SIZE`만 읽어 1088이 콘텐츠 크기로 흘렀다.
  aspect-fit·입력 좌표·렌더가 전부 1920×1088을 기준으로 동작해 세로 0.74% 왜곡과
  하단 8행 쓰레기 표시, 불필요한 재샘플링이 발생했다.

구현 내용
- `H264Decoder::query_output_geometry()`: `MF_MT_MINIMUM_DISPLAY_APERTURE` →
  `MF_MT_GEOMETRIC_APERTURE` → coded 전체 순서로 aperture를 읽고, coded 평면 밖이면
  거부, NV12 2x2 서브샘플링 때문에 좌표·크기를 짝수 정렬한다.
- `DecodedFrameNv12`에 visibleLeft/Top/Width/Height 추가. width/height는 buffer layout
  (coded) 의미를 유지한다.
- `SharedFrame`: width/height는 visible(콘텐츠), codedWidth/Height와 visibleLeft/Top을
  별도 보관. aspect-fit과 입력 좌표는 visible을 그대로 쓰게 된다(코드 변경 불필요 -
  resolve_active_video_content_size가 gFrame.width를 읽으므로).
- D3D NV12 렌더러: 텍스처를 visible 크기로 만들고 coded stride 평면에서 visible 행만
  업로드. 셰이더가 padding 행을 아예 샘플링하지 않는다.
- GDI 폴백: coded 평면을 변환 후 소스 rect(visibleLeft, 행 오프셋 포인터)로 visible만
  StretchDIBits.
- 통계/로그: size=visible로 보고하고 codedSize를 별도 표기.

실행한 build/test
- Debug 빌드 후 unit 3종 ALL PASS. verify 격리 실행 OVERALL_OK=True.
- 실동작 검증: 격리 host+client를 띄워 Desktop 선택 후 뷰어 스크린샷 - renderPath=
  d3d_nv12, fallback 0, d3dPresentSuccess 41+, 클라이언트 로그 size=1920x1080
  codedSize=1920x1088. 1600x900 창에 16:9 콘텐츠가 정확히 맞고 하단 쓰레기 행 없음.

Before/After 지표: 화질 정확성 작업(성능 목적 아님). 성능 지표는 Q1 완료 후 재기준선에서
일괄 수집.

fallback/부작용: aperture가 없거나 비정상인 디코더에서는 coded 전체를 visible로 사용
(기존 동작과 동일). Android는 이미 crop을 읽으므로 변경 없음.

미완료: 없음.

다음 작업: Q1-2 rate-control/pacing 동기화.

### 207) 2026-07-31 Q1-2 runtime bitrate의 rate-control·UDP pacing 동기화

작업 ID: Q1-2

변경 파일
- `apps/native_poc/src/mf_h264_codec.hpp/.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/udp_control_e2e_test.cpp`

변경 전 문제
- 초기화는 env 정책(PeakConstrainedVBR, peak 300%/CBR 110%, tune별 VBV)을 쓰는데
  `reconfigure_bitrate()`는 peak를 110%/130%로 하드코딩해, 런타임 설정/ABR로 bitrate만
  바뀌면 scene-change 비트 여유가 1/3 이하로 조용히 무너졌다.
- `gUdpPacePeakBitrateBps`는 시작 시 한 번만 계산돼 ABR 하향 후 과도 burst, 상향 후
  불필요한 전송 지연을 만들었다.

구현 내용
- `H264Encoder::apply_rate_control(reason)` 공용 함수로 mode/mean/peak/VBV/MaxQP 정책을
  일원화. 초기화와 `reconfigure_bitrate()`가 같은 정책을 적용하고 reason과 수용 여부를
  로그로 남긴다.
- `apply_encoder_target()` 성공 시 active bitrate × udpPacePeakPercent로
  `gUdpPacePeakBitrateBps`를 재계산·갱신(변경 시 로그).
- e2e 테스트에 runtime bitrate 8M→4M→10M 시나리오 추가. 제어 틱(200ms)이 메시지를
  보내기 전에 세션을 닫으면 거짓 통과가 되므로 요청 후 800ms 대기.

실행한 build/test
- e2e 13체크 ALL PASS(격리 포트 44100). 호스트 로그 검증:
  init 8M: peak=24M(300%) vbv=200000 / 4M 적용: peak=12M(300%) vbv=100000,
  pacing 10M(250%) / 10M reconfigure: peak=30M(300%) vbv=250000, pacing 25M(250%).
  변경 전엔 4M 변경 시 peak가 4.4M(110%)로 떨어졌을 값이다.

Before/After 지표: 정합성 작업. 화질 영향은 "변경 직후 텍스트 뭉개짐" 소멸로 나타나며
Q1 재기준선에서 회귀 없음을 확인한다.

fallback/부작용: keyint/fps가 함께 바뀌면 기존대로 encoder 재초기화 경로(init reason)를
탄다 - 정책은 동일하게 적용된다.

미완료: 없음.

다음 작업: Q1-3 제품 encoder tune 명시.

### 208) 2026-07-31 Q1-3 제품 encoder tune 명시 + A/B (low_latency 확정)

작업 ID: Q1-3

변경 파일
- `apps/native_poc/src/host_app_main.cpp` (자식에 REMOTE60_NATIVE_ENCODER_TUNE_MODE=
  low_latency 명시)
- `automation/native_video_profile_android_lan.json`,
  `automation/native_video_profile_1080p_external_template.json` (stable_text →
  low_latency, 제품 preset과 일치)
- `apps/native_poc/src/native_video_host_main.cpp` (--bind-address 인자)
- `automation/verify_native_video_runtime.ps1` (로컬 실행 시 루프백 바인드)
- `automation/run_perf_baseline.ps1` (-TuneMode, -CaptureBackend 파라미터)

변경 전 문제
- 제품 host_app이 tune을 지정하지 않아 native 기본 low_latency로 돌고, 검증 프로필은
  stable_text라 제품과 검증의 화질 결론이 달랐다.

A/B 결과 (1080p30 scroll, Release, DXGI, 각 3회 중앙값)
- decoded fps: low_latency 22.78 vs stable_text 22.33 (-2%, 노이즈 범위)
- LAT_P95: 13.9ms vs 28.9ms (stable_text +109%)
- Host CPU 동일, Client CPU stable_text +8.9%
- 결론: **low_latency를 제품 기본으로 확정**. stable_text의 텍스트 보호 목적은 Q1-2로
  고정된 PeakConstrainedVBR(300% peak)+MaxQP 32가 이미 담당하며, fps 이득 없이 지연
  꼬리만 나빠진다. 검증 프로필을 제품과 동일하게 맞췄다.

측정 인프라 이슈 2건 (이번 세션에서 해결)
- 새 빌드 경로의 exe가 0.0.0.0에 바인드하면 Windows 방화벽 동의 대화상자가 실행마다
  떠서 측정을 방해한다. 호스트에 --bind-address를 추가하고 로컬 검증은 127.0.0.1에
  바인드해 대화상자 자체를 차단했다.
- 16:00경부터 WGC 프레임 공급이 시스템 수준에서 2~5fps로 저하됐다(같은 장면에서 DXGI는
  20fps+ 정상, 15:52까지는 WGC도 정상). scene 애니메이션은 픽셀 diff로 확인된 상태라
  WGC 세션 레벨 문제로 판단 - 재부팅 전까지 지속될 수 있어 기준선·A/B는 DXGI 백엔드로
  수행한다(제품 지원 백엔드이자 H1이 최적화하는 바로 그 경로). 사용자 실호스트(10:35
  시작, WGC)는 저하 이전에 세션을 만들었으므로 즉시 영향은 불명.

실행한 build/test
- host_app/host_poc/client_poc Debug+Release(build-perf) 빌드, A/B 6런 전부 OVERALL_OK.

미완료: WGC 저하 근본 원인(재부팅 후 재확인 필요).

다음 작업: Q1 완료 기준선 재수집(DXGI).

### 209) 2026-07-31 Q1 이후 기준선 재수집 (DXGI, H1+ 비교 앵커)

작업 ID: B1 재수집

- baseline-b1-post-q1 (build-perf Release, commit 5c8ecf0, DXGI, 4구성×5회 전부 OK).
- 1080p-scroll(주 비교): DEC 23.44(22.22~23.67), Host CPU 67.6%, Client CPU 54.3%,
  captureCopyMap 0.99ms, captureMemcpy 0.76ms, NV12 6.74ms, enc 4.45ms,
  queueToSend 45.3ms, LAT_P95 26.9ms
- 1080p-static: DEC 21.2, Host CPU 61.3% / 1080p-video: DEC 26.1, Host CPU 59.3% /
  720p-scroll: DEC 24.1, scale 4.47ms, NV12 3.20ms
- H1부터의 A/B는 이 수치만 기준으로 한다. pre-Q1 기준선(WGC)과는 캡처 백엔드가 달라
  직접 비교하지 않는다.

다음 작업: H1.

### 210) 2026-07-31 H1 캡처 콜백 copy-only + 비동기 readback ring

작업 ID: H1

변경 파일
- 신규 `apps/native_poc/src/d3d_capture_readback.hpp/.cpp` (D3dCaptureReadbackPipeline,
  CaptureBufferPool, pick_latest_ready_slot)
- 신규 `apps/native_poc/src/capture_readback_test.cpp` (링 정책·버퍼 풀 13체크)
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/CMakeLists.txt`

변경 전 문제
- 캡처 콜백이 CopyResource + 블로킹 Map + 전행 memcpy + crop을 인라인 수행했고, DXGI
  경로는 duplication 프레임을 쥔 채 동기 readback을 기다렸다(콜백 ~1.9ms).

구현 내용
- 콜백: staging slot에 CopyResource + D3D11_QUERY_EVENT End + Flush만 수행(무 Map/무
  memcpy/무 할당). Flush가 없으면 유휴 컨텍스트에서 복사가 커맨드 버퍼에 머물러 쿼리가
  영원히 미완료가 된다 - 실측으로 확인한 함정.
- 워커: GetData로 완료 확인된 slot만 Map(무정지, 실측 12us) 후 재사용 CPU 버퍼로 복사,
  창 client crop도 워커에서 수행. latest-wins로 오래된 완료 프레임은 폐기.
- CaptureBufferPool: shared_ptr 딜리터가 마지막 참조 해제 시 풀로 반환 - gating이
  프레임 간 참조를 쥐어도 재사용이 절대 겹치지 않는다(단위 테스트로 고정).
- 게이팅: 소비자 측 encode-크기 CPU 비교는 유지(H1 임시 경로, H3에서 GPU 비교로 이관
  예정). 죽은 설정 gatingMotionPm(REMOTE60_NATIVE_FRAME_GATING_MOTION_THRESHOLD_PM)
  제거.
- FrameState 타이밍 필드는 로그 키 호환을 위해 이름 유지, 의미 재정의(copyMap=콜백
  submit, unmapWait=GPU pending, unmap=워커 Map). 통계에 captureSupersededDrops,
  captureCpuBufferReuse 추가.

실행한 build/test
- 단위 4스위트 + capture_readback_test(13체크) ALL PASS, e2e 13체크 ALL PASS.
- Debug 실측: submitCopy 145us / workerMap 12us / workerMemcpy 550us / gpuPending
  2.6ms(비동기 겹침) / busy·superseded drop 0 / bufferReuse 동작.

Before/After (1080p30 scroll Release 5회 중앙값, post-Q1 기준선 대비)
- Host CPU 67.6% → 58.9% (-12.9%)
- 콜백 비용(captureCopyMap) 990.7us → 128.8us (-87%)
- LAT_P95 26.9ms → 18.0ms (-32.9%)
- Host peak WS 134.4 → 126.7MB (-5.7%)
- DEC_AVG 23.44 → 22.56 (-3.8%): 런 간 편차 범위(21.67~23.44 vs 22.22~23.67) 겹침,
  롤백 기준(-5%) 미달. H2/H3 후 재확인.
- queueToSend +9%: pacing 지배 구간의 노이즈. H4 대상.

측정 인프라 (세션 중 확정한 사실)
- 디스플레이가 꺼지면 WGC와 DXGI duplication 모두 프레임 공급이 죽는다. 이날 WGC
  15:59 / DXGI 16:41 "저하"의 근본 원인. keep-alive에 1px 왕복 SendInput 지글(0-델타
  이동은 입력으로 집계되지 않음 - x64 INPUT 40바이트 레이아웃 필수)을 추가해 해결.
  DXGI는 지글 후 완전 회복(19~23cb/s)을 확인했다.

fallback/부작용: 10분 resize/창 전환 소크는 미수행(e2e의 선택 전환은 통과) - G1에서
수행 예정.

미완료: gating 비교 입력의 GPU 이관(H3에서).

다음 작업: H2 GPU-front crop/resize.

### 211) 2026-07-31 H2 GPU-front crop/resize

작업 ID: H2

변경 파일
- `apps/native_poc/src/d3d_capture_readback.hpp/.cpp` (파이프라인에 GPU 전처리 단계)
- `apps/native_poc/src/native_video_host_main.cpp` (SetOutputSize 연결, 통계)

변경 전 문제
- 다운스케일 경로가 원본 해상도 전체를 CPU로 읽고, GpuBgraScaler가 CPU 업로드→blt→CPU
  재독으로 프레임당 full-frame 전송 leg 3개를 만들었다(720p scroll 실측 scaleUs 4.47ms).

구현 내용
- Submit에서 인코드 박스가 소스 콘텐츠의 정확한 aspect-fit이고 업스케일이 아닐 때만
  GPU 전처리: 소유 텍스처로 CopyResource → VideoProcessorBlt(crop rect + scale, full-range
  RGB, auto-processing off) → 인코드 크기만 staging에 region copy. 조건 미충족(창 crop
  비율 변화 등)이나 blt 실패 시 기존 경로로 폴백해 소비자의 재적합 로직이 그대로 동작
  - 스트레치 프레임이 나갈 수 없는 구조.
- staging slot은 캡처 크기로 유지하고 CopySubresourceRegion으로 인코드 영역만 복사,
  meta.payloadW/H가 워커의 읽기 크기를 지정. 전처리 시 창 crop도 blt가 수행(원패스).
- 통계: capturePreprocessed / capturePreprocessFallbacks.

실행한 build/test
- capture_readback_test ALL PASS, e2e ALL PASS.
- Debug 1080p→720p: 전 프레임 preprocessed(480), fallback 0, 레거시 gpuScaleAttempts 0,
  첫 프레임부터 size=1280x720 직행.

Before/After (720p-scroll Release 5회 중앙값, post-Q1 기준선 대비)
- Host CPU 67.14% → 52.64% (-21.6%)
- CPU scale(scaleUs) 4469us → 0
- 콜백 submit 1005 → 389us, 워커 memcpy 777 → 368us (readback bytes가 encode 크기로 축소)
- LAT_P95 19.5ms → 1.5ms (-92%)
- DEC 24.11 → 25.11 (+4.1%)
- 1080p 동일 크기 경로는 전처리를 건너뛰므로 무영향(H1 결과 유지).

fallback/부작용: 720p 화질 screenshot 승인은 미수행(16:9 정합은 aspect-fit 가드로 구조
보장, 뷰어 육안 확인은 1080p에서 수행) - G1 화질 체크리스트에 포함.

미완료: 없음.

다음 작업: H3 GPU NV12 → MF encoder.

### 212) 2026-07-31 H3 GPU NV12 surface → MF encoder (opt-in으로 랜딩)

작업 ID: H3

변경 파일
- `apps/native_poc/src/mf_h264_codec.hpp/.cpp` (encode_sample_common 추출,
  encode_frame_surface 추가 - MFCreateDXGISurfaceBuffer, 무 memcpy)
- `apps/native_poc/src/d3d_capture_readback.hpp/.cpp` (NV12 4-slot 링, BGRA→NV12
  VideoProcessorBlt BT.709 limited, 소유권 있는 슬롯 수명)
- `apps/native_poc/src/native_video_host_main.cpp` (표면 인코드 분기, 지연 해제 큐,
  인코더 재초기화 시 일괄 해제, 통계)

구현 내용
- 캡처 파이프라인이 aspect-fit 조건에서 프레임마다 NV12 텍스처(4-slot 링)를 GPU 변환.
  slot은 프레임을 pop한 소비자가 소유하고, 인코더의 누적 출력 수가 제출 시점을 넘어야
  해제된다(async MFT가 아직 읽는 텍스처를 절대 재기록하지 않음). 게이팅 skip/초과
  드랍/재적합 경합 등 모든 경로에서 해제를 보장(worker superseded 해제, publish
  overwrite 해제, loop-top 해제).
- 인코더는 표면 sample을 거부하면 세션 단위로 CPU 경로 폴백(1프레임 손실 후 지속).
- 디바이스 손실 견고성: GetData가 실패한 쿼리는 즉시 slot을 해제 - 이전에는 드라이버
  오류 1번이 링 동결→캡처 사망으로 번졌다(실측 재현).

검증
- 단위 4스위트 + e2e ALL PASS. Debug 실동작: 정상 구간에서 인코드 전량이 표면 경로
  (nv12SurfaceFrames=encodedFrames, rejected 0), 클라이언트 d3d_nv12 렌더.
- 색상 검증: video 장면 컬러 블록을 뷰어 미러로 실화소 비교 - 색조 정확, 물빠짐/크러시
  없음(BT.709 limited 출력 + full-range RGB 입력 명시).

**측정 판정: 오늘 이 머신에서는 불가.** Release 5런 중 fps 12~15로 오히려 저하 + 1런
실패였는데, 로그상 원인은 mid-run DXGI_ERROR_DRIVER_INTERNAL_ERROR로 인한 디바이스
제거(시작 시 staging 생성조차 첫 시도 실패 후 재생성으로만 성공). 이 머신의 GPU
스택은 세션 내내 누적 저하됐고(WGC 사망 → 디스플레이 절전 시 DXGI 사망 → 디바이스
제거) H3의 프레임당 NV12 blt가 유발자인지 환경인지 분리할 수 없다.

**결정: 기본 OFF(opt-in REMOTE60_NATIVE_NV12_SURFACE=1).** 제품 경로는 H1/H2 검증
상태를 유지하며(off 재확인: 23.8fps/기준선 동등), 건강한 드라이버(재부팅 후)에서
A/B로 켠다. 계획의 "성능 수치 없는 최적화는 완료로 치지 않는다" 원칙에 따라 H3
성능 항목은 미완으로 남긴다.

미완료: 건강 환경 A/B 및 기본화 여부 판정, gating 비교 입력의 GPU 이관.

다음 작업: C1. H4는 착수 조건(H1~H3 후 측정) 자체가 현 환경에서 판정 불가라 동일하게
보류하고 G1 전에 재평가한다.

### 213) 2026-07-31 C1 Windows Client 저위험 최적화 + F1 절전 배선

작업 ID: C1 (+F1)

변경 파일
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_host_main.cpp` (썸네일 hung-window 가드)

구현 내용
- C1-1 RTV 캐시: `ensure_rtv()`가 크기 불변이면 즉시 반환(기존: 매 프레임
  GetDesc+GetBuffer+CreateRenderTargetView). rtvCreateCount/rtvResizeCount 진단 추가.
- C1-2 썸네일 락 축소: gThumbs를 `shared_ptr<const WindowThumb>` 맵으로 바꿔 paint는
  락 안에서 포인터 스냅샷만 뜨고 StretchDIBits는 락 밖에서 수행. 수신 스레드의
  InvalidateRect도 락 밖으로.
- C1-3 GDI 캐시: 색상별 브러시 캐시(cached_brush)로 카드/버튼/오버레이의 매 페인트
  CreateSolidBrush/DeleteObject 제거, 오버레이 제목 폰트를 DPI 변경 시에만 재생성
  (gUiTitleFont), WM_DESTROY에서 일괄 정리.
- F1 절전 배선: picker 전환 지점 5곳이 `set_picker_visible_and_sync_stream()`을 통해
  `gStreamStateControl.Request(!visible)`를 보낸다(열림=false, 선택/닫힘=true). 시작
  시에는 요청하지 않아 화면을 열지 않는 하네스/구클라이언트 동작 불변.
- 호스트 썸네일: `IsHungAppWindow` 가드 - PrintWindow는 타임아웃 없는 SendMessage라
  행 상태 창(오늘 실제로 뜬 AMD 드라이버 크래시 신고 창 등) 하나가 제어 세션 전체를
  막는다.

Before/After (1080p-scroll Release 3회, post-Q1 기준선 대비)
- Client CPU 54.34% → 44.22% (-18.6%)
- DEC 23.44 → 23.67 (동등), Host CPU 67.6 → 67.2 (동등)

검증/미완
- 클릭 시나리오(뷰어→Targets→복귀)로 picker 전환 자체는 스크린샷으로 확인. 그러나 이
  머신에서는 썸네일 캡처(GDI BitBlt/PrintWindow)가 손상된 그래픽 스택에 막혀 제어
  루프가 썸네일 recv에 고착, stream-state 송신까지 확인하지 못했다(hung 가드로도 미
  해소 - 데스크톱 BitBlt CAPTUREBLT 단계 의심). 프로토콜 자체는 e2e의
  RequestStreamActive로 검증돼 있고 Android가 동일 경로를 상용 사용 중.
- 미완: 인코드 정지/재개 실측(재부팅 후), 썸네일 전송의 비동기화(제어 채널과 분리) -
  C1-2 후속으로 U2/G1 전에 재평가.

다음 작업: C2는 착수 조건(H3 채택 후) 미충족으로 보류. A1 Android로 진행.

### 214) 2026-07-31 A1 1단계 - inactive-scene renderer 차단 + adaptive poll

작업 ID: A1 (1/2단계)

변경 파일
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`

구현 내용 (코덱스 합의안의 독립 커밋 1)
- renderStatus가 활성 씬의 renderer만 호출: renderTargetsScene은 TARGETS에서만,
  renderViewerScene/updateViewerLogHeader는 VIEWER·SWITCHING에서만. LOGIN/HOSTS/CONNECT
  tick에서 viewer 전용 JNI(data usage, presentation timestamp) 호출이 사라진다.
- applySceneVisibility의 매 tick nativeMacroState() 제거: macro 정리는 씬 전환 시 1회
  (lastVisibilityScene 추적). viewer 재진입 전환에서 stall tracker
  (lastVideoOutputPtsUs/SeenUs)를 리셋해 이전 세션 잔존값의 1-tick 가짜 stall overlay
  차단(계획 문서 9번 단계).
- 씬별 adaptive poll: LOGIN/HOSTS 1000ms, TARGETS 750ms, CONNECT/SWITCHING 250ms,
  VIEWER 500ms. 선택 진행 중(selectionStage != IDLE)은 어느 씬이든 250ms 유지 -
  selection timeout(6s)과 전환 피드백 무회귀.
- 효과(계산): 안정 tick JNI 비뷰어 9→6회 + 주기 250→1000ms = 로그인/목록 초당 JNI
  36→6회(-83%). VIEWER 36→18회/s.

실행한 build/test
- assembleDebug 성공(컴파일 검증). 기기 스모크는 보류: 연결된 LDPlayer 인스턴스의
  pm install이 4분+ 무응답(사용자 용도 인스턴스로 추정, 강제 개입하지 않음).
  /data/local/tmp/gnlink_a1.apk 푸시 잔여물 있음 - 다음 기기 세션에서 설치·정리.

미완료 (A1 2단계): native 단일 snapshot JSON(nativeGetUiSnapshotJson) + version 게이트로
직접 getter 6회→1회 통합, 씬별 JNI 카운터 실측(9/9/8→목표) - 기기 검증 가능 시점에.

다음 작업: A2.

### 215) 2026-07-31 최적화 실행 세션 마감 - 완료/보류 정리

이 세션에서 완료(커밋 f9b5435..646f821, 11 커밋)
- U1 Host signed-in UI (204), B1 기준선 인프라+1차 수집 (205), Q1-1/2/3 (206~208),
  Q1 후 재기준선 (209), H1 (210), H2 (211), H3 구현/opt-in (212), C1+F1 배선 (213),
  A1 1단계 (214).

측정 성과 요약 (post-Q1 DXGI 기준선 대비, Release 5회 중앙값)
- 1080p-scroll: Host CPU 67.6→58.9%(H1), 콜백 비용 991→129us, LAT p95 -33%,
  Client CPU 54.3→44.2%(C1). 720p-scroll: Host CPU 67.1→52.6%, CPU scale 4.47ms→0(H2),
  LAT p95 19.5→1.5ms.
- 디코드 fps는 22~24에서 정체(목표 27 미달) - 남은 병목은 queue-to-send(pacing 인라인,
  H4 대상)와 NV12 6.7ms(H3 활성화 대상)로 특정돼 있으며 둘 다 환경 회복 후 항목.

보류 항목과 차단 사유 (다음 세션 착수 순서)
1. [재부팅 후] H3 활성화 A/B(REMOTE60_NATIVE_NV12_SURFACE=1) → 기대: NV12 6.7ms 제거,
   디코드 27fps 달성 여부 판정. 이어서 H4 착수 조건 재평가.
   - 이 머신의 GPU 스택이 세션 중 누적 붕괴(WGC 사망 → 디스플레이 절전 연동 DXGI 사망
     → DXGI_ERROR_DRIVER_INTERNAL_ERROR 디바이스 제거, AMD 버그 신고 창 출현).
     재부팅 전 측정은 신뢰 불가.
2. [재부팅 후] F1 인코드 정지/재개 실측, C1-2 후속(썸네일 전송을 제어 채널과 분리).
3. [기기 확보 시] A1 2단계(nativeGetUiSnapshotJson 통합 + 씬별 JNI 카운터 실측), A2
   (thumbnail TTL+버퍼 재사용). LDPlayer 인스턴스 pm 무응답으로 이번 세션 설치 불가,
   /data/local/tmp/gnlink_a1.apk 잔여.
4. [사용자 결정 필요] U2 잔여(Windows picker/Android 로그인 재배치 - 뷰어부는 기존
   사용자 결정으로 현행 유지), S1 HTTPS(서버 TLS 종단: 도메인/인증서 확보 필요),
   S2 미디어 암호화(계획이 요구하는 protocol decision record를 먼저 작성해 검토 후 구현).
5. G1 최종 게이트는 위 항목 정리 후.

시스템 상태 원복
- 디스플레이 타임아웃 AC 5분 복원(DC는 원값 미기록으로 5분 설정 - 확인 요망).
- keep-alive/scene/테스트 프로세스 전부 종료, 사용자 실호스트(PID 10852)만 유지.
- 참고: 사용자 실호스트는 build-local Release 구버전 바이너리로 계속 실행 중 -
  재시작해야 이 세션의 Q1/H1/H2/C1 수정이 제품 경로에 반영된다.


### 216) 2026-07-31 (재부팅 후) H3 최종 판정 - AMF 벤더 경로 느림 확정, 자동 폴백 추가

작업 ID: H3 후속

재부팅 후 환경 검증
- 캡처 스택 완전 회복: DXGI 52~63cb/s, WGC 49~55cb/s, 디바이스 오류 0. 어제의 WGC/DXGI
  사망과 디바이스 제거는 전부 드라이버 상태였음이 확정(어제 "정상" 측정치도 저하 상태
  - 콜백이 어제 20~25/s vs 오늘 50~63/s).

H3 A/B (1080p-scroll Release, 건강한 드라이버)
- OFF: 22.3~23.4fps, Host CPU ~65%
- ON: 5.4~9.4fps로 붕괴, encUs 평균 68ms(max 211ms), 매 프레임 MF_E_NOTACCEPTING.
  async poll 노브(POLL_MAX=16, SLEEP=500us)로도 6~13fps.
- 결론: 환경 문제가 아니라 AMF MFT의 DXGI 입력 샘플 경로가 이 GPU에서 프레임당
  수십 ms의 내부 동기화 비용을 가진다(CPU 입력 경로는 4.5ms). 어제의 기본 OFF 판단이
  옳았다.

구현 (자동 성능 폴백)
- 표면 인코드 첫 30프레임의 encodeCallUs를 프로브해 평균 16ms 초과 시 세션 내 CPU
  경로로 자동 복귀 + 로그. 실측: "too slow avgUs=18619 ... reverting" 후 fps 18~20
  회복. 샘플 수락 여부만으로는 벤더 경로 품질을 알 수 없다는 것이 핵심 교훈.
- 기본값은 opt-in 유지: AMF에서는 켜도 세션 시작 1~2초 프로브 비용 후 어차피 CPU로
  돌아오므로 이득이 없고, encoder 재초기화마다 재프로브 비용이 반복된다. NVENC/QSV
  머신에서 프로브가 통과하면 그때 기본화를 재논의(백엔드별 verdict 기억 개선 포함).

시사점
- 디코드 fps 27 목표의 남은 병목은 H4(pacing 인라인 전송, queue-to-send 45ms)가 유력.
  H3는 이 머신에서는 닫힌 카드.


### 217) 2026-07-31 H3 판정 보강 - 내장그래픽에서 GPU TDR 유발 확인

- GPU 확인: AMD Radeon(TM) Graphics **내장그래픽(APU, 공유 VRAM 512MB)** + Parsec 가상
  디스플레이 어댑터 2개 동작 환경.
- 22:40 WER에 **Kernel_141(비디오 엔진 타임아웃/TDR) 6건 + Kernel_193 2건** - H3-ON
  A/B를 돌리던 시각과 정확히 일치. 사용자가 본 "그래픽 팝업"은 이 TDR의 AMD 크래시
  리포터다.
- 결론 강화: 이 내장 GPU에서 AMF의 DXGI 표면 입력 경로는 느린 것을 넘어 **비디오
  엔진을 타임아웃(드라이버 리셋)까지 몰고 간다.** 이 머신에서 H3-ON 실험은 더 하지
  않는다(기본 OFF + 자동 폴백 유지가 정답). 어제의 드라이버 붕괴 연쇄에도 H3 개발 중
  테스트가 기여했을 가능성이 높다.
- H3 재평가 조건: 외장 NVENC/QSV GPU 머신에서 프로브 통과 시.


### 218) 2026-07-31 H4 전송/pacing 분리 - 인코드 스레드에서 와이어를 떼어냄

작업 ID: H4

변경 파일
- `apps/native_poc/src/native_video_host_main.cpp`

변경 전 문제
- 인코드→패킷화→pacing 대기→sendto가 전부 인코드 스레드 인라인이라, 20Mbps pacing
  기준 키프레임 하나가 최대 60~96ms 동안 다음 인코드 시작을 직접 막았다
  (queue-to-send 평균 45~52ms).

구현 내용
- UDP h264 경로에 전송 스레드 + 깊이 2 큐. 인코드 스레드는 enqueue 후 즉시 다음
  프레임으로. 드랍 정책: (1) 키프레임 도착 시 백로그 전체 폐기(새 IDR이 이전 프레임을
  무의미화), (2) 델타가 백로그를 넘치면 백로그 폐기 + keyframe 재동기 요청 - 인코딩된
  델타를 조용히 건너뛰면 참조 체인이 깨지므로 반드시 IDR로 복구한다.
- UDP 피어는 senderMu로 보호된 복사본(최초 Hello + pump_udp_hello 갱신). 전송 실패는
  기존 인라인 정책 그대로(무한 세션은 피어 re-Hello 대기). 종료 시 clientSock을 닫기
  전에 sender join.
- 지표: senderQueueDrops / senderSendDurAvg·MaxUs 추가, udpTx 카운터는 sender 소유로
  이관. queueToSendUs의 의미는 "enqueue까지"로 변경(전송은 병렬).

실행한 build/test
- 단위 3스위트 + e2e 13체크 ALL PASS. 첫 e2e에서 video FAIL 1건 - 최초 Hello 수락
  지점의 sender 피어 복사 누락이 원인, 수정 후 ALL PASS.

Before/After (1080p-scroll Release 5회, 오늘 H4-off 대비)
- DEC 22.3~23.4 → **26.1~26.6fps (+14%, 목표 27 사실상 도달)**
- LAT_P95 2~18ms → 0.8~5.2ms
- 인코드 스레드의 전송 구간 45~52ms → 11.3ms(핸드오프+인코드), 와이어 12.3ms는 병렬
- MBPS 5.4 → 6.2 (더 많은 프레임 출하), PRESENT_GAP 0
- Host CPU 65 → 72~78%: 초당 인코드 프레임 증가분의 정직한 비용(프레임당 비용 유사)
- senderQueueDrops 5회/10초: 키프레임 supersede + 재동기 정상 작동


### 219) 2026-07-31 H4 전체 매트릭스 확인 + 클라이언트 flip-discard 스왑체인

- H4 전체 매트릭스(4구성×5회, 20/20 OK): 1080p-static 21.7~24.2 / 1080p-scroll
  24.8~26.4 / **1080p-video 24.2~27.0(목표 27 도달)** / 720p-scroll 24.8~26.1.
  전 장면 LAT_P95 1.3~13.8ms(기준 70ms), PRESENT_GAP 0, 회귀 없음.
- 클라이언트 스왑체인을 DXGI_SWAP_EFFECT_FLIP_DISCARD로 전환(거부 시 legacy discard
  폴백). 하네스는 픽커 화면이라 present 경로를 측정하지 못함 - fps 무변화 확인 +
  뷰어 컬러 블록 시각 검증으로 무회귀 확인. present 비용 개선은 구조상 이득(블릿 제거,
  DWM 참조 합성)이며 인터랙티브 실사용 대상.


### 220) 2026-07-31 제품 호스트 신 바이너리 교체 + host_app 로그 공유 열기 수정

- build-local Release 재빌드(host_app/host_poc/client_poc) 후 실호스트 재시작. 새 child가
  tune=low_latency 명시로 기동, directory online 등록 확인 - Q1/H1/H2/H4/C1이 이제 실제
  제품 경로에서 동작한다.
- U1 후속 결함 수정: host_app.log를 _SH_DENYNO 공유로 열도록 변경 - 이전에는 스트리밍
  중 Open log 버튼/외부 tail이 잠겨서 읽지 못했다(실사용에서 발견).


### 221) 2026-08-01 H4 후속 - 백로그 드랍 후 깨진 델타 전송 차단 (사용자 보고 결함)

사용자 보고: 창을 격하게 흔들면 네모(매크로블록) 모양으로 조금씩 깨짐.

원인
- H4의 오버플로 정책이 백로그를 버린 뒤 **버린 프레임을 참조하는 현재 델타를 그대로
  enqueue**했다. IDR이 도착할 때까지 클라이언트는 끊긴 참조 체인 위에 델타를 디코드해
  블록 깨짐이 보인다. 격한 창 이동 = 큰 프레임 연속 = 백로그 조건.

수정
- 오버플로 시 백로그 + 현재 델타를 함께 폐기하고 senderWaitingForKey 상태로 진입.
  키프레임이 실제로 통과할 때까지 모든 델타를 보류(드랍 카운트). 새 IDR 도착 시 상태
  해제 + 백로그 supersede.
- gating 참조는 실제로 enqueue된 프레임에서만 갱신 - 드랍된 프레임을 "보냈다"고
  기억하면 화면이 정지 상태일 때 강제 IDR 인코드가 게이팅에 걸려 회복이 늦어진다.

검증
- e2e ALL PASS. 강제 오버플로 스트레스(pacing 100% + full-motion 장면): 드랍 70회
  발생 상황에서도 클라이언트 19~24fps 연속 디코드 - 참조 깨진 델타는 구조적으로 전송
  불가가 됐다.
- 제품 호스트 재빌드·재시작(00:00), directory online 확인.


### 222) 2026-08-03 OSLink 비교 후속 - GDI 캡처 프로세스 격리, 입력 미리보기, AMD 안전 경로

목표
- OSLink처럼 캡처를 별도 프로세스로 분리하고 GDI를 선택 가능한 폴백으로 제공한다.
- Android 뷰어 왼쪽 위에 조합 중인 한글을 포함한 입력 미리보기를 표시한다.
- AMD 드라이버 오류를 재현·차단하면서 1080p60 전체 경로의 실제 상한을 측정한다.

변경 파일
- Android 입력/UI: `ImeCaptureView.kt`, `MainActivity.kt`, `activity_main.xml`,
  `strings.xml`, `viewer_input_preview_background.xml`
- 캡처 프로세스: `gdi_capture_protocol.hpp`, `gdi_capture_process.hpp/.cpp`,
  `gdi_capture_worker_main.cpp`, `gdi_capture_process_test.cpp`,
  `capture_backend_dxgi.hpp`, `CMakeLists.txt`, `native_video_host_main.cpp`
- 코덱/클라이언트/검증: `mf_h264_codec.hpp/.cpp`, `mf_h264_codec_test.cpp`,
  `native_video_client_main.cpp`, `verify_native_video_runtime.ps1`

구현
- GDI BitBlt를 kill-on-parent-close Job Object의 별도 worker 프로세스로 분리했다. 3슬롯
  공유 메모리 latest-wins 링에 worker가 직접 캡처하고, host는 최신 프레임만 복사한다.
  worker 실패/저속은 감시해 WGC로 자동 복귀하며 Android 설정에 `GDI (격리)` 선택을 추가했다.
- Android IME composing/commit/backspace와 물리·가상 키 입력을 최대 160 code point로
  추적해 뷰어 왼쪽 위 흰색 말풍선에 한 줄로 표시하고 키보드 종료 시 초기화한다.
- BGRA→NV12 SSE2 및 직접 encoder sample 입력, 고해상도 UDP pacing timer, 키프레임 전용
  100Mbps pacing floor를 적용했다. 큰 IDR이 sender 큐를 막아 연쇄 IDR/드랍을 만드는 루프를
  줄이되 일반 프레임의 Wi-Fi pacing은 유지한다.
- AMD `atidxx64.dll` access violation과 기존 LiveKernelEvent 141 원인이 된 외부 DXGI device
  manager/direct decode surface는 `REMOTE60_NATIVE_DXGI_DECODE_SURFACE=1` 명시 시에만 켠다.
  기본은 하드웨어 MFT의 system-memory 출력 + 기존 D3D 업로드 경로다.
- verifier의 sparse `trace_present` 간격을 프리즈로 잘못 세던 판정을 실제 연속 present의
  `capGapUs`로 수정했고, host capture cadence도 sparse trace 차분 대신 callback interval을 쓴다.

검증/build/test
- Gate A (Release, WGC, 1920x1080@60, UDP H.264 6Mbps, OSLink 동시 실행):
  `HOST_RC=0`, `CLIENT_RC=0`, `OVERALL_OK=True`, `DEC_AVG=50.11`, `DEC_P95=53`,
  `LAT_P95_US=11164`, `PRESENT_GAP_OVER_1S=0`. 안정성/지연은 통과했으나 처리량 목표
  54fps에는 미달해 `GATE_A_PASS=False`; 현재 동시 부하의 다음 병목은 약 15ms decoder다.
- GDI 격리 단독 Release: `57.9986fps`, worker BitBlt p95 `22.099ms`, parent copy p95
  `1.659ms`, `RESULT: ALL PASS`. 전체 encode/decode와 OSLink가 동시에 GDI를 쓰면 BitBlt가
  24~26ms로 늘어 38~42fps이므로 GDI는 30fps 호환/격리 폴백, WGC는 최고 성능 기본으로 판정했다.
- Gate B 격리 UDP control e2e: 연결/창 목록/desktop 선택/stream/4→10Mbps runtime tune/
  입력 큐/세션 종료까지 13개 체크 `RESULT: ALL PASS`.
- Gate C: H.264 SPS `High level 4.2, BT.709 limited`; LDPlayer 실제 뷰어에서 입력 말풍선과
  host `inputEvents=22` 확인. 시각 증거 `automation/logs/gdi-android-20260802/preview-verified.png`.
- Release 단위 테스트 6종(codec/readback/input macro/shared core/UDP control/GDI process) 및
  Android `:app:assembleDebug` 통과. 관련 native Release target도 전부 빌드 통과.
- 전체 workspace build는 작업 외 기존 선택 의존성(`rtc` namespace, `opus/opus.h`) 부재로
  `apps/client`, `apps/host`에서 실패했으며 이번 변경 target과는 무관하다.
- AMD 안전 수정 이후 00:03부터 반복 부하 테스트 종료까지 Application/System 이벤트에서
  `atidxx64`, Display, LiveKernelEvent 신규 0건. AMD GPU/가상 디스플레이 상태 `OK`.

다음 액션
- 사용 중인 제품 host PID 17444는 중단하지 않았다. 새 바이너리 배포는 현재 원격 세션을
  끝낸 뒤 build-local Release 재빌드·제품 host 재시작으로 반영한다.
- 1080p60을 54fps 이상으로 고정하는 잔여 작업은 OSLink 미동시 기준선을 먼저 재측정한 뒤,
  AMD direct surface를 다시 켜지 않고 decoder 프로세스 격리 또는 안전한 복사 경로 축소로 진행한다.


### 223) 2026-08-03 OSLink 스트림 종료 후 1080p60 제품 pacing 최적화·신규 host 직접 검증

목표
- OSLink 원격 스트림이 없는 기준선에서 WGC/GDI/DXGI 캡처 상한을 다시 분리하고,
  AMD 위험 경로를 켜지 않은 채 1080p60 제품형 처리량을 54fps 이상으로 고정한다.
- 새 Release host를 직접 실행해 UDP 제어/영상/입력 경로를 확인하고 정확한 PID를 종료한다.

변경 파일
- 제품 pacing/스케줄링: `apps/native_poc/src/native_video_host_main.cpp`,
  `apps/native_poc/src/native_video_client_main.cpp`
- Android 기본 캡처 선택: `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`,
  `SessionPersistence.kt`
- 재현 장면/기록: `automation/perf_scene_generator.ps1`, `docs/history.md`, `docs/구현계획.md`

구현
- 60Hz main tick의 `sleep_for` 누적 초과를 고해상도 deadline wait로 교체하고 정상적인 짧은
  초과에서는 기준 phase를 보존했다. 50fps 이상 요청은 광고/인코더 목표는 그대로 두고 내부
  pacing에 기본 +4fps 여유를 주며, motion frame gating이 main tick과 같은 제한을 두 번
  적용하던 조건을 제거했다.
- UDP 일반 프레임 pacing peak 기본을 평균 bitrate의 250%에서 500%로 높여 sender가 다음
  프레임을 막지 않게 했고 host/client 프로세스와 주 스레드는 Above Normal로 실행한다.
  `REMOTE60_NATIVE_NORMAL_PRIORITY=1`, `REMOTE60_NATIVE_PACING_HEADROOM_FPS`로 A/B/해제가 가능하다.
- 이 PC에서 WGC 콜백은 약 43~56fps로 흔들린 반면 DXGI Desktop Duplication은 throughput
  런에서 평균 디코드 85.11fps의 여유를 확인했다. 전체 화면 기본을 DXGI로 바꾸고 WGC/GDI는
  호환성/수동 선택과 자동 폴백으로 유지했다. 기존 Android 저장값은 덮어쓰지 않고 신규/무효
  값의 기본만 DXGI다. AMD direct decode surface는 계속 기본 OFF다.
- 재현 장면에 1~240fps 입력을 추가하고 60fps 타이머 간격을 16ms로 설정했다.

검증/build/test
- `build-local` Release host_app/host/client/UDP e2e/GDI/codec target 전부 빌드 통과,
  Android `:app:assembleDebug` 4 ABI 빌드 통과.
- 동일 1600x900 60fps full-motion 장면을 1920x1080@60 H.264 6Mbps로 전송한 DXGI 제품형
  3회: `DEC_AVG=58.33/58.67/57.89`, `DEC_P95=63/63/62`, `LAT_P95_US=6345/6031/2715`,
  Gate A/M7 3/3 PASS, present 1초 초과 gap 0, 종료 후 잔존 0.
  이전 제품형 DXGI 약 49.89fps 대비 반복 평균 58.30fps로 약 16.9% 향상했다.
- 환경변수로 backend를 강제하지 않은 최종 런 `verify-native-video-20260803-004528`은
  `desktop_backend=dxgi`, host encode 평균 62.75fps, client decode 워밍업 포함 평균
  59.78fps, `HOST_RC=0`, `CLIENT_RC=0`, `OVERALL_OK=True`, 종료 후 잔존 0.
- 1920x1080 크기의 PowerShell 장면 생성기는 DWM 입력을 35~40fps로 낮춰 비교에서 제외했다.
  이는 host의 callback/encode가 같은 속도로 입력을 소진한 생성기 병목이며 freeze/error는 없었다.
- Release host PID 26272를 43000/43001에 직접 실행한 UDP control E2E에서 연결, 창 목록,
  desktop 선택, stream, 4→10Mbps runtime tune, 입력, 최종 세션 건강성 전 항목 `ALL PASS`.
  해당 PID를 직접 종료한 뒤 remote60 프로세스 0, UDP 43000/TCP 43001 소유자 0을 확인했다.
- 최종 재빌드 host PID 24984도 직접 기동해 Above Normal 적용과 60fps pacing 설정을 확인하고
  직접 종료했다. 종료 후 PID와 UDP 44700/TCP 44701 소유자 모두 0이다.
- codec `PASS`; GDI 격리 `58.6539fps`, worker copy p95 19.413ms, parent copy p95 1.610ms,
  `RESULT: ALL PASS`. 테스트 시작 00:20 이후 AMD/Radeon/atidxx/DXGI/LiveKernel 관련
  Application/System/WER 오류 0건. `REMOTE60_NATIVE_DXGI_DECODE_SURFACE`는 전 런에서 미설정.

다음 액션
- 실제 Android 단말에서 저장된 backend가 WGC라면 Settings에서 DXGI를 한 번 선택해 적용하고,
  1080p60 실기기/동일 LAN 장시간 soak로 무선 pacing과 발열을 확인한다.
- OSLink UI 스트림은 종료됐지만 비관리자 셸에서 중지할 수 없는 `LDRemoteSvc`만 idle Running으로
  남아 있다. OSLink 동시부하 Gate 항목은 별도 미완료로 유지한다.


### 224) 2026-08-03 Android Release APK 빌드·dist 출력

목표
- debug APK 대신 실제 사용 성능에 맞는 비디버그 Android release APK를 만들고 기존 배포
  규칙대로 저장소 루트의 `dist`에 설치 가능한 산출물을 출력한다.

변경 파일
- 기록: `docs/history.md`, `docs/구현계획.md`
- 빌드 산출물(커밋 제외): `dist/gnlink-android-20260803-release.apk`

검증/build/test
- Gradle `:app:assembleRelease` 성공. Kotlin/리소스 release pipeline과 네이티브
  `RelWithDebInfo` 빌드가 arm64-v8a/armeabi-v7a/x86/x86_64 4개 ABI에서 모두 통과했다.
- 생성된 unsigned APK를 기존 debug 설치본과 같은 로컬 Android debug 인증서로 서명해
  기존 앱에 `install -r` 가능한 내부 배포본으로 만들었다. APK Signature Scheme V3 검증 PASS,
  certificate SHA-256도 기존 debug APK와 동일하다.
- manifest `debuggable=false`, application ID `com.remote60.androiddirect`, version `0.1.0`,
  4 ABI의 `libremote60_android_direct.so` 포함을 확인했다.
- 최종 크기 7,716,380 bytes(debug 10,387,310 bytes 대비 25.7% 감소), SHA-256
  `9806A97F1394FD26B9D118DDB38835986D5BE3D4A1493B4CDD2D9EB68D2505B1`.
- 사용자가 실행 중인 host_app/child 프로세스는 중단하지 않았으며 APK를 LDPlayer에 자동
  설치하거나 현재 앱 데이터를 변경하지 않았다.

다음 액션
- 사용자가 release APK를 설치해 실제 Android 뷰어의 60fps/발열을 확인한다.
- 외부 스토어 배포 전에는 전용 release keystore와 versionCode 증가 정책을 추가한다.


### 225) 2026-08-03 전체화면 재연결·화면 깨짐·30fps cadence·잠금 입력 보강

목표
- YouTube 전체화면 전환 뒤 데스크톱 선택이 간헐적으로 실패하는 문제와 저비트레이트에서도
  화면이 깨지거나 30fps가 주기적으로 멈춰 보이는 문제를 원인별로 수정한다.
- 호스트 실행 중 절전 진입을 막고, 관리자 창·작업표시줄·잠금 화면까지 입력 가능한 제품형
  권한 경로를 추가하며 Release 산출물을 `dist`에 갱신한다.

변경 파일
- 캡처/인코더/전송/전원: `apps/native_poc/src/native_video_host_main.cpp`,
  `mf_h264_codec.hpp/.cpp`, `poc_protocol.hpp`
- 수신/FEC/재동기화: `native_video_client_shared_core.hpp/.cpp/.test.cpp`,
  `native_video_client_session.hpp/.cpp`, `native_video_client_main.cpp`
- 디렉터리 capability: `directory_client.hpp/.cpp`, Android `DirectoryClient.kt`,
  `NativeSessionBridge.kt`, `MainActivity.kt`, `native_bridge.cpp`
- Android 표시 cadence: `android_video_decoder.hpp/.cpp`
- 보안 입력: `secure_input_protocol.hpp`, `secure_input_broker.hpp/.cpp`,
  `secure_input_service_main.cpp`, `apps/native_poc/CMakeLists.txt`
- 기록: `docs/history.md`, `docs/구현계획.md`

구현
- 제품 로그의 `DXGI_ERROR_ACCESS_LOST(0x887A0026)` 후
  `E_ACCESSDENIED(0x80070005)`를 재현 원인으로 확정했다. DXGI/GDI 런타임 폴백을
  stream-inactive 조기 반환보다 먼저 처리하고 WGC 재시작 실패도 종료하지 않고 재시도한다.
- 캡처 callback 전에 목표 FPS로 GPU copy를 제한하고 encoded main loop의 독립 tick을 제거했다.
  AMD 비동기 MFT가 이전 입력 출력을 한 호출에서 반환할 때 현재 timestamp로 덮어쓰던 문제는
  accepted-input FIFO로 복원했다. sender frame cadence와 Android 고정 FPS presentation clock/30ms
  playout lead를 추가해 burst/pause 패턴을 평탄화했다.
- UDP v2에 8 data + 1 XOR parity FEC를 추가하고 최대 3개 프레임을 out-of-order 조립한다.
  복구 불가 gap/malformed에서는 P-frame을 즉시 중단하고 decoder reset과 IDR 요청을 수행한다.
  수동 bitrate는 ABR 비활성 override가 아니라 high ceiling으로 적용해 12/20Mbps 요청도 압력 시
  mid/low로 내려갈 수 있게 했다. 4Mbps 일반 프레임 peak floor는 40Mbps로 두었다.
- 호스트 수명 동안 `ES_SYSTEM_REQUIRED`, 스트림 동안 `ES_DISPLAY_REQUIRED`와 display wake를
  적용했다. launcher는 `requireAdministrator` manifest로 고정했고, 디렉터리 128-bit capability로
  인증한 세션만 LocalSystem 서비스/active-console agent를 통해 secure desktop 입력을 전달한다.
  최초 capability는 관측 IP/port로 검증하고 같은 token/IP의 소켓 재연결은 허용한다.
- Windows/Android Hello에 protocol/FEC capability 검증을 추가했다. 이전 클라이언트와는 wire
  format이 다르므로 host/client/APK를 같은 Release 세트로 교체해야 한다.

검증/build/test
- `build-verify` Release의 host app/host/client/secure-input/GDI worker와 관련 테스트 target 빌드 통과.
- 1920x1080, DXGI, H.264, 4Mbps/30fps, Release 15초 런:
  `DEC_AVG=28.64`(초기 handshake 포함), 안정구간 평균 `29.25fps`(28~30),
  `queue overwrite=0`, UDP assembly drop/malformed/reorder 0, Gate A/M7 PASS,
  `LAT_P95=45.250ms`. frame 완성 arrival p95는 수정 전 약 72ms에서 51.498ms로 감소했다.
- 격리 UDP control E2E는 연결/목록/desktop 선택/stream/4→10Mbps tune/입력/종료까지
  `RESULT: ALL PASS`. shared-core FEC, MF H.264 codec, capture readback, UDP 0/5/10% loss,
  GDI process 격리 테스트 모두 PASS(`58.3146fps`). Directory `npm test` 전 항목 PASS.
- Android `:app:assembleRelease`와 lint/RelWithDebInfo 4 ABI 통과. V3 서명 PASS,
  APK SHA-256 `86EE6524802544227FF495FC5AC232B2C50D6D52031EBEF06DB329D6A8AD7CCE`,
  `dist/gnlink-android-20260803-release.apk` 갱신.
- Windows bundle은 `dist/gnlink-windows-20260803-release/`에 갱신했다. 최종 host SHA-256은
  `9CE3DEC39962E74E1ABBA9BB69A001D5E0FC930BF9B4CC0B4C8F1DE8C4C8B1AD`다.

다음 액션
- 사용 중인 구버전 관리자 host PID 23376/child 2652가 `build-local` 제품 파일을 잠그고 있다.
  tray host 종료 후 staging된 `remote60_host_app.new.exe`와
  `remote60_native_video_host_poc.new.exe`를 원래 이름으로 교체하고 최신 제품 host를 기동한다.
- 그 다음 Release APK를 LDPlayer에 `install -r`하고 실제 디렉터리 재연결, YouTube 전체화면 전환,
  4Mbps/30fps 영상, 작업표시줄 및 Windows 잠금 화면 입력을 최종 수동 검증한다.


### 226) 2026-08-03 Directory capability 간헐 연결 실패 수정

목표
- 온라인 호스트를 Android에서 선택했을 때 디렉터리 주소 조회까지 성공한 뒤
  `connecting -> error`로 돌아가는 간헐 연결 실패를 실제 제품 로그로 확정하고 수정한다.

변경 파일
- `apps/native_poc/src/directory_client.hpp`
- `apps/native_poc/src/directory_client.cpp`
- `apps/native_poc/src/native_video_client_session.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/secure_input_broker.cpp`
- `docs/history.md`
- `docs/구현계획.md`

검증/build/test
- LDPlayer Release 앱과 운영 디렉터리/제품 호스트 조합에서 호스트의
  `rejected udp hello with invalid directory capability`와 Android의
  `directory_target -> connecting -> error`를 같은 요청에서 확인했다.
- 원인은 호스트의 기본 25초 heartbeat 직후 `/api/connect`가 들어오면 4초 punch 대기와
  800ms 단발 Hello가 호스트의 다음 capability 수신보다 먼저 끝나는 경쟁 조건이었다.
- 호스트가 peer Punch를 받으면 heartbeat sleep을 즉시 중단해 capability를 다시 조회하고,
  인증 클라이언트는 같은 punched socket에서 최대 3초 동안 Hello를 재전송한다. 인증 토큰이
  있을 때는 HelloAck의 directory-auth feature까지 확인한다.
- 첫 경쟁을 제거한 뒤에도 디렉터리가 관측한 공인 endpoint(`175.209.236.194`)와 실제 호스트에
  도착한 hairpin NAT endpoint(`192.168.0.1`)가 달라 capability가 거부되는 것을 추가로 확인했다.
  30초 만료·128-bit·1회용 capability 토큰을 인증 기준으로 소비하고 관측 endpoint는 NAT punch
  힌트로만 사용하며, 인증 이후 세션은 실제 발신 IP에 고정하도록 수정했다.
- 보안 입력 서비스가 `SERVICE_RUNNING`을 보고한 직후 named pipe 생성 전이면 최초 연결이
  `ERROR_FILE_NOT_FOUND`로 끝나고 일반 입력 경로로 영구 우회됐다. pipe open을 3초 동안 재시도하고,
  인증된 desktop 입력은 사전 `connected()` 상태와 무관하게 broker의 재연결 경로를 거치게 했다.
- `build-verify` Release host app/host/client/shared-core/E2E target 빌드 성공,
  `remote60_native_video_client_shared_core_test` PASS, 별도 UDP 44122 제어 E2E 13/13 PASS.
- Android `:app:assembleRelease` 성공, V3 서명 PASS, LDPlayer `install -r` 성공.
  APK SHA-256은 `081B63203CC7CC2E33511EBB546F947C0DE116FFCDB372554082CE0083F4C467`이다.
- 운영 디렉터리 실연결에서 `directory capability endpoint translated` 후
  `connected window_list_received count=9`까지 성공했다. 4Mbps/30fps 20.09초 안정구간은 호스트
  push/pop `600/600`, sender queue drop 증가 0, Android decoder reset 0, Android 출력 로그 간격
  보정 약 29.6fps였다.
- Windows dist launcher SHA-256은
  `D5D8B70A8805316195A24B3E86527ED94B138135DC6F0F11EA4F5765AEC59590`, native host는
  `0D38B9B1AE7E391C48BBDDA04228668B57EBFDC9A9EF03151DB797E4F4F2ADA1`이다.

다음 액션
- 현재 실행 중인 native host(PID 4140)는 directory/NAT 수정은 포함하지만 마지막 secure-input
  pipe 재연결 수정 전 바이너리다. 사용자가 GNLink Host를 종료하면 준비된 `.new.exe`를 기존
  제품 파일명으로 교체하고, 재연결 후 Session 1 SYSTEM 입력 agent와 잠금 화면 입력을 확인한다.

### 227) 2026-08-03 인코더 버스트로 인한 화면 정지와 더블클릭 커서 튐 수정

목표
- 평균 fps는 25~30으로 정상인데 간헐적으로 화면이 완전히 멈추는 증상과, Android에서
  더블클릭 시 커서가 튀고 아이콘이 끌려가는 증상의 실제 원인을 코드로 확정하고 수정한다.
- 코덱스 작업(5dbfc1a~3e5019f) 이후 발생한 지연 회귀를 pre-Codex 수준으로 되돌리되
  fps/CPU 이득은 유지한다.

원인
- 비동기 H.264 MFT를 encode 호출당 이벤트 1개만 폴링하고 METransformNeedInput에서
  루프를 중단해 HaveOutput이 누적 → 이후 호출이 access unit 2~3개를 한 번에 배출.
  이 묶음이 마이크로초 간격으로 깊이 2 sender 큐에 들어가 혼잡으로 오판되어 큐를 비우고
  IDR이 올 때까지 모든 델타를 폐기(130~300ms 정지). 폐기된 프레임도 sentFrames로
  카운트되어 지표에는 정상 fps로 보였다.
- Android 터치에 드래그 데드존이 없어 손가락 1픽셀 흔들림이 절대 좌표 이동으로 전송되고
  레터박싱 배율(1.3~1.8배)만큼 증폭 → Windows의 4px 드래그/더블클릭 임계를 초과.
- 호스트가 SetCursorPos와 버튼 SendInput을 분리 호출해 그 사이 커서 이동 시 오클릭.
- HostPowerKeepalive::SetStreaming이 이미 streaming 중에도 wake를 재실행해
  캡처 폴백 재시도 루프(100ms 주기)가 실제 상대 마우스 이동을 계속 주입.

변경 파일
- `apps/native_poc/src/mf_h264_codec.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/secure_input_service_main.cpp`
- `apps/android_direct_client/.../MainActivity.kt`
- `automation/run_perf_baseline_fps.ps1` (신규: fps 파라미터화 베이스라인 러너)

검증/build/test
- 5회 중앙값, 1080p DXGI, 격리 포트, RDP 차단 상태에서 수정 전(HEAD) 대비:
  지연 p95 36.9ms→5.3ms(30fps scroll), 42.6ms→2.5ms(30fps video),
  17.0ms→3.3ms(60fps video). fps는 28.3 및 52.8~53.4로 유지.
  pre-Codex(b1e776d) 대비 지연 동등 이상, fps는 5~7% 높음.
- 고정 폴링 예산은 30fps 지연과 60fps 처리량이 상충(4: 지연 4.2ms/fps 44.7,
  2: 지연 26.3ms/fps 49.9)하여 프레임 주기의 40% 시간 제한 방식으로 해결.
- remote60_mf_h264_codec_test / shared_core_test / capture_readback_test /
  input_macro_test / udp_control_channel_test 전부 PASS.
- Android `:app:compileReleaseKotlin` 성공.
- 초기 측정은 RDP 세션과 동시 실행 에이전트 부하로 오염되어 CPU가 과다 계상되었고,
  RDP 차단 + 단독 실행으로 재측정해 확정했다.

다음 액션
- GNLinkSecureInput 서비스가 LocalSystem으로 `D:\remote\remote\build-local\...`(사용자
  쓰기 가능 경로)를 가리키고 있어 권한 상승 위험이 있다. 관리자 권한으로 서비스를 제거하고
  제품 배포 시 관리자 전용 경로 설치를 강제해야 한다.
- 합성 씬은 버스트가 약해 senderQueueDrops가 대부분 0이므로, 실사용 부하에서
  senderHeldFrames(신규 지표)로 정지 구간을 재확인한다.

### 228) 2026-08-03 코덱스 리뷰 반영: 남은 정지 경로 차단

목표
- 227) 수정분(bb6dcc9)을 코덱스에 검증 의뢰해 나온 approve-with-changes 지적사항을
  전부 반영하고, 성능 회귀 없음을 재측정으로 확인한다.

반영한 지적사항
- (High) sender 큐 한도를 묶음 크기로 잡으면 큐에 이미 1프레임이 있을 때 3-AU 묶음이
  마지막 AU에서 다시 오버플로하여 원래 정지 증상이 재현된다. 또한 대량 드레인이 그만큼
  큰 큐를 허용해 지연이 초 단위로 늘 수 있다. → 혼잡 판정을 묶음 처리 전 백로그로 한 번만
  내리고 kSenderQueueMaxFrames=6 하드 상한을 추가했다.
- (High) Android 디코더가 MediaCodec 입력 버퍼 부족 시 델타를 조용히 폐기하면서 IDR을
  요청하지 않아 이후 프레임이 없는 참조를 보게 된다. → ClientEncodedFrameSink에
  ConsumeDecoderKeyframeRequest()를 추가하고 세션이 기존 레이트 리미터로 IDR을 요청한다.
- (Medium) enqueue 시점에 sent로 카운트한 프레임이 이후 senderQueue.clear()로 지워져도
  통계에 남았다. → clear 시 senderHeldFrames로 이전하고 sentFrames에서 차감한다.
- (Medium) 더블탭 앵커가 기동 직후 (0,0)을 유효 앵커로 취급하고 취소 후에도 남으며 좌/우
  버튼을 구분하지 않았다. → 유효 플래그 + 버튼 일치 + UP 큐 성공 시에만 기록 + 취소 시 무효화.

변경 파일
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/native_video_client_session.{hpp,cpp}`
- `apps/android_direct_client/app/src/main/cpp/android_video_decoder.{hpp,cpp}`
- `apps/android_direct_client/.../MainActivity.kt`

검증/build/test
- 5회 중앙값, 1080p DXGI, 격리 포트, RDP 차단. 코덱스 HEAD(수정 전) 대비
  30fps 지연 p95 36.9ms→4.9ms(scroll), 42.6ms→6.4ms(video), fps 28.3/28.1 유지.
  60fps 지연 p95 17.5ms→10.1ms(scroll), 17.0ms→6.6ms(video), fps 50.5/53.1.
  60fps fps는 pre-Codex(50.4/50.9)와 동등하며 지연은 절반 이하다.
- remote60_mf_h264_codec_test / shared_core_test / capture_readback_test /
  input_macro_test / udp_control_channel_test 전부 PASS.
- Android `:app:assembleRelease` 성공. zipalign 후 기존과 동일한 로컬 debug 인증서로 서명해
  V3 검증 PASS(certificate SHA-256 dcc806ae...2990), `dist/gnlink-android-20260803-b-release.apk`
  7,818,780 bytes, SHA-256 9EEAA8BC65F59D5583D8E6B070266B82D0A3E4551CD9EB2EB1A97624312295CF.
- 사용자 기기에 자동 설치하지 않았다.

다음 액션
- GNLinkSecureInput 서비스가 LocalSystem으로 사용자 쓰기 가능한 build-local 경로를 가리키는
  권한 상승 위험이 남아 있다. 관리자 권한으로 서비스 제거가 필요하다(중지/삭제 권한 부족으로
  이번 세션에서 처리하지 못했다).
- 합성 씬은 버스트가 약해 정지가 잘 재현되지 않으므로, 실사용에서 senderHeldFrames로
  정지 구간을 재확인한다.

### 229) 2026-08-04 접속 실패 원인 규명, 호스트 포트 후보 목록, 구현계획 재편

목표
- 다른 네트워크(회사 Wi-Fi)에서 접속이 안 되는 원인을 로그로 확정하고 고친다.
- UAC 동의 창이 보이지도 눌리지도 않는 원인을 코드로 확정한다.
- M0~M8이 끝난 `docs/구현계획.md`를 보관하고 이 단계용 계획을 새로 만든다.

원인
- 접속: 회사 Wi-Fi가 아웃바운드 UDP를 목적지 포트 화이트리스트로 제한한다. 디렉토리
  관측 포트 8081은 12회 전부 통과해 pendingPunch가 생성됐으나, 호스트 43000으로는
  단 한 개도 도착하지 않았다(`directory peer punch` 로그 0건, 거부 로그도 0건).
  같은 폰이 LTE에서는 첫 펀치에 접속해 호스트측 경로는 정상임이 확인된다.
- UAC: 경계는 무결성 수준이 아니라 데스크톱 객체다. UAC 동의 창은 `WinSta0\Winlogon`에
  그려지는데 저장소 전체에서 `SetThreadDesktop`/`OpenInputDesktop` 호출은 두 곳뿐이고
  둘 다 캡처가 아니다(호스트의 이름 probe, SYSTEM 에이전트의 입력 attach). Winlogon
  프레임버퍼를 읽는 코드가 제품에 없어 창이 보이지 않고, Default에 붙은 스레드는 거기에
  입력을 넣을 수 없다. 작업 관리자가 되는 것은 그것이 Default 데스크톱의 창이고 호스트가
  관리자 권한이라 무결성이 동등하기 때문이다.
- 부수 확인 2건: ACCESS_LOST 후 WGC 강등이 영구적이라 UAC 창이 닫힌 뒤에도 DXGI로
  복귀하지 않는다. SYSTEM 입력 에이전트는 `WTSGetActiveConsoleSessionId()`로 세션을
  고르는데 이는 물리 콘솔 세션이라 스트리밍 중인 세션과 다를 수 있고, 파이프 쓰기 성공을
  전달 성공으로 간주해 잘못 전달된 클릭이 성공으로 보고된다.

변경 파일
- `apps/native_poc/src/bind_port_candidates.hpp` (신규)
- `apps/native_poc/src/bind_port_candidates_test.cpp` (신규)
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/host_app_main.cpp`
- `apps/native_poc/src/directory_client.{hpp,cpp}`
- `apps/native_poc/CMakeLists.txt`
- `docs/구현계획.md` (신규 작성), `docs/구현계획_old.md` (보관)

검증/build/test
- `--bind-port`가 순서 있는 후보 목록을 받는다. 실측: `443,3478,43000` → `udp bound
  port=443`. `43000,3478,443` → 43000이 실행 중인 제품 호스트에 점유되어 있어
  `bind failed port=43000; trying next` 후 `udp bound port=3478`. 단일 `43000`은 기존
  동작 유지.
- 관측 공인 포트가 bind 포트와 다르면 `nat-port-rewritten` 경고를 1회 출력한다. 포트를
  보존하지 않는 NAT에서는 이 방식이 무효이므로 침묵 대신 원인을 남긴다.
- `remote60_bind_port_candidates_test` PASS(19 케이스). 회귀: video_playout_clock /
  capture_cadence_gate / udp_fec_interleave / udp_control_channel /
  native_video_client_shared_core 전부 PASS.
- 이 PC의 UAC 정책 실측: `PromptOnSecureDesktop=1`, `EnableLUA=1`,
  `ConsentPromptBehaviorAdmin=5`. secure desktop 전제가 성립한다.
- 설치 파일은 수정하지 않았다. 방화벽 규칙이 포트가 아니라 프로그램 기준이다.

다음 액션
- N1의 마지막 완료조건인 회사 Wi-Fi 실기 접속 검증이 남아 있다. 0.2.3 패키징 필요.
- 근거 대장 A1(데스크톱이 초당 33회만 갱신) 재검증. 측정 당시 RDP 연결 상태였고 Microsoft
  Remote Display Adapter가 32Hz라 수치가 일치한다. RDP를 끊고 재측정해야 NACK(P2)의
  우선순위를 정할 수 있다.
- 근거 대장 A2(Winlogon에서 BitBlt 가능 여부) 실험. 저장소 설계 문서는 DXGI만 가능하다고
  주장하나 검증된 적이 없고, 결과가 U2b의 규모를 좌우한다.

### 230) 2026-08-04 U1 결론: 보안 데스크톱은 BitBlt로도 읽힌다

목표
- UAC 동의 창 캡처를 며칠 걸려 구현하기 전에, SYSTEM 프로세스가 Winlogon 데스크톱의
  픽셀을 실제로 읽을 수 있는지, 어느 API로 읽히는지를 먼저 확정한다.

결과
- **둘 다 읽힌다.** `OpenInputDesktop` + `SetThreadDesktop`으로 Winlogon에 붙는 데 성공했고
  접근이 거부된 적은 한 번도 없다.
  - `GetDC(NULL)` + `BitBlt`: 2720x1080. 가상 화면 전체(두 모니터)를 한 번에.
  - DXGI `DuplicateOutput`: 1920x1080. 출력 1개.
  - 두 이미지 모두 UAC 다이얼로그의 제목·본문·버튼이 판독 가능하다.
- `docs/잠금화면_사전로그인_설계.md`의 "보안 데스크톱 캡처는 DXGI Desktop Duplication만
  가능하다"는 **반증되었다.** 해당 문단에 정정을 남겼다. 검증 없이 적힌 주장이었고,
  그 위에 세운 "Winlogon이면 DXGI로 강제 전환" 설계도 함께 무효다.
- U2b 규모가 대 → 중으로 내려간다. `gdi_capture_worker_main.cpp`의 publish 루프를 재활용할
  수 있어 `GNLinkInputService`에 d3d11/dxgi를 도입할 필요가 없다. BitBlt가 가상 화면을
  한 번에 잡으므로 U4(멀티모니터)의 캡처 쪽도 함께 해결된다.
- 부수 확인: 이 PC의 세션 구성이 RDP 접속 시 `호스트=세션 1`, `WTSGetActiveConsoleSessionId()=8`로
  갈렸다. U2a(세션 타겟 오류)는 재현 완료이므로 별도 검증이 필요 없다.

probe가 답을 세 번 틀릴 뻔한 지점
- DXGI가 커서 전용 프레임(`LastPresentTime==0`)을 성공으로 반환하고 그 안의 텍스처는
  갱신되지 않는다. 그대로 저장해 완전한 검은 이미지를 "성공"으로 기록했다. 일반 데스크톱
  대조군에서 `avg=0 min=0 max=0`이 나와 잡았다.
- 데스크톱 전환과 같은 tick에 촬영해 Winlogon이 아직 아무것도 그리지 않은 상태를 찍었다.
  "파일을 썼으면 성공"이라는 판정 기준 때문에 재시도도 하지 않고 종료했다.
- 첫 두 실행이 SYSTEM 서비스 프로세스를 세션 0에 20분간 남겼다. RDP 해제로 작업 중이던
  데스크톱이 사라지며 멈춘 것으로 보이며, 실행 파일을 잠가 수정본 빌드조차 막았고 해제에
  관리자 권한이 필요했다.
- 각각 균일 픽셀 검사, 내용 기반 성공 판정 + 전환 후 대기, 하드 데드라인 자폭으로 수정했다.

변경 파일
- `apps/native_poc/tools/winlogon_capture_probe.cpp`
- `apps/native_poc/CMakeLists.txt`
- `docs/구현계획.md`, `docs/잠금화면_사전로그인_설계.md`

검증/build/test
- 관리자 권한 실행, RDP 아닌 콘솔 세션(`console shotan 1 Active`,
  `WTSGetActiveConsoleSessionId()=1`). 대조군 캡처가 같은 실행에서 정상 내용을 담아
  probe 자체의 건전성이 확인된다.
- 결과 이미지 육안 확인: `secure1_bitblt.bmp`, `secure1_dxgi.bmp` 모두에
  `GNLinkSetup-0.2.3.exe` UAC 프롬프트가 판독 가능하게 찍혔다.

다음 액션
- U2b를 BitBlt 경로로 착수하되 U2a(세션 타겟)를 먼저 고친다. 순서를 뒤집으면 엉뚱한 세션의
  화면을 완벽하게 스트리밍하게 된다.
- probe는 U2b가 안정될 때까지 남긴다. attach+capture 순서가 실제로 동작함이 증명된 유일한
  레퍼런스다.
- A1(데스크톱이 초당 33회만 갱신)은 여전히 미검증이며 P2(NACK) 착수 여부를 가른다.

### 231) 2026-08-04 P1 결론: "60fps는 소스 한계" 는 틀렸다 — 원격 세션의 천장이었다

목표
- "데스크톱이 초당 33회만 갱신되므로 60fps는 불가능하다"는 기존 결론이 RDP 가상
  디스플레이(32Hz)의 아티팩트인지 확정한다. NACK 착수 여부가 여기에 달려 있었다.

결과 — 기각
- 같은 PC, 같은 도구로 측정했다.
  - 원격 세션 접속 상태: 출력이 `\.\DISPLAY145 2236x1232` 하나뿐, **33/s** (32/33/32/33)
  - 원격 해제, 물리 화면: `\.\DISPLAY2 1920x1080` + `\.\DISPLAY24 800x600`, **60~62/s**
- 두 측정 모두 `timeouts=0`, `merged=0`이라 도구가 놓친 갱신은 없다. 33은 원격 가상
  디스플레이의 주사율 천장이었고 콘텐츠 한계가 아니었다.
- **"파이프라인은 offer의 98.7%를 통과시키며 남은 한계는 소스다"라는 기존 결론이 무효다.**
  소스는 60을 준다. 데스크톱 60fps가 안 나온 원인은 아직 규명되지 않았고, 신규 항목
  P3으로 추적한다. `capture_cadence_gate_test`의 60fps 케이스도 7~52/s 스윙을 전제로
  작성되어 있어 전제가 바뀌었다.

두 번 같은 함정에 빠졌다
- 최초 33/s 측정은 RDP 연결 중이었다(Microsoft Remote Display Adapter 32Hz).
- 이번 도구의 첫 스모크도 OSLink 접속 중이라 다시 33/s가 나왔다. 정지 화면인데
  `timeouts=0`에 편차가 거의 없는 것이 콘텐츠가 아니라 일정한 present 주기라는 신호였다.
- 측정 규칙으로 못박았다: **캡처율·fps 측정은 원격 세션에 붙은 상태에서 하지 않는다.**

변경 파일
- `apps/native_poc/tools/desktop_update_rate.cpp` (신규), `apps/native_poc/CMakeLists.txt`
- `docs/구현계획.md`

검증/build/test
- 도구는 인코더·클라이언트·pacing 게이트를 거치지 않고 `AcquireNextFrame` 직후 즉시
  `ReleaseFrame`한다. 프레임을 쥐고 있으면 duplication이 다음 변화를 보고하지 않으므로,
  중간 작업이 있으면 소스가 느린 것으로 측정된다.
- `LastPresentTime==0`인 커서 전용 프레임은 데스크톱 픽셀을 담지 않으므로 갱신에서 제외해
  따로 센다. 이 구분이 없으면 수치가 부풀려진다.
- 모든 어댑터의 모든 출력을 이름·크기와 함께 먼저 출력한다. 이 PC에는 800x600@30Hz
  Virtual Display Driver가 있어, 그것을 duplication하고 "데스크톱"이라 보고하면 천장만
  바꿔 같은 오류를 반복하게 된다.

다음 액션
- P3: 물리 화면에서 데스크톱 60fps 요청 시 파이프라인 단계별 실측으로 어디서 60이 30대로
  떨어지는지 특정한다. P2(NACK)보다 우선한다.
- 원격 세션에서 수집한 기존 성능 지표는 전부 재검토 대상이다.

### 232) 2026-08-04 원격 접근 신뢰성 1차: 세션 타겟·LAN 포트·DXGI 복구·좌표 매핑

목표
- 사용자 수동 확인 없이 진행 가능한 항목을 우선순위 순으로 처리하되, 현재 성능을 해치지 않는다.

처리한 항목 (커밋 순)
- `1bfaa7a` **U2a** SYSTEM 입력 에이전트의 세션 타겟을 요청자 기준으로. `WTSGetActiveConsoleSessionId()`는
  물리 콘솔 세션이라 RDP 중에는 호스트(세션 1)와 다른 곳(세션 8)을 가리켰다. 파이프 클라이언트의
  PID → 세션으로 교체하고, `RegisterServiceCtrlHandlerExW`로 올려 세션 변경을 수신한다.
  세션 0은 대화형 데스크톱이 없으므로 양쪽 입력에서 거부한다.
- `031599e` **N6** 후보 목록의 마지막 포트에 두 번째 소켓을 유지. N1이 호스트를 43000에서 443으로
  옮기면서 IP 직접 입력 경로가 깨졌고, 그건 0.2.3에 이미 나간 회귀였다. 핸드셰이크만 두 소켓을
  보고, Hello가 온 쪽이 미디어 소켓이 된다. LAN이 이기면 기본 소켓은 닫지 않고 은퇴시킨다 —
  디렉토리 에이전트가 그 소켓으로 하트비트를 계속 보내야 한다.
- `c922258` **R1+R2** 데스크톱이 다른 어댑터로 옮겨간 경우를 `dxgi_adapter_changed`로 명명해 보고.
  세션 내부에서 D3D 장치를 재생성하지 않는다 — `d3dDevice`는 호출자 소유이고 프레임 핸들러가
  그 텍스처를 넘기므로, 다른 어댑터 장치로 바꾸면 호출자가 못 쓴다. 사용 가능 여부를 사후 검사가
  아니라 선택의 일부로 만들어 0크기 가상 출력을 후보에서 제외한다.
- `00dbefe` **R3=U0** 강등 후 요청 백엔드로 주기적 재승격(3초 → 2배씩, 30초 상한).
  `ControlPongMessage.captureTargetFlags` bit2로 `secureDesktopActive` 전달.
- `70e3156` **U4** 클릭을 캡처된 모니터에 매핑. `SetCursorPos`는 가상 데스크톱 절대 좌표를 받는데
  매핑은 주 모니터로만 했다. 대상 사각형을 메시지가 명시적으로 운반하고, 미지정 시 폴백을
  가상 화면으로 바꿨다. 음수 원점 지원, 범위 밖은 clamp.

측정에서 배운 것
- R1의 첫 구현은 매 resolve마다 모든 어댑터를 열거했고 6회 중 2회가 실패했다
  (`dxgi_select_no_outputs`, `DXGI_ERROR_INVALID_CALL`). 기준선은 6/6 clean이었으므로 명백한 회귀였다.
  **정상 경로에서는 장치 자신의 어댑터만 보고**, 실패한 뒤에만 전체를 뒤지도록 재구조화했다.
  DXGI 팩토리 생성과 디스플레이 토폴로지 순회는 공짜가 아니다.

변경 파일
- 신규: `secure_input_session.hpp`, `secure_input_mapping.hpp`, `apps/host/src/dxgi_output_selection.hpp`
  (각각 순수 함수 + 테스트)
- `secure_input_service_main.cpp`, `secure_input_broker.{hpp,cpp}`, `secure_input_protocol.hpp`,
  `native_video_host_main.cpp`, `native_video_client_main.cpp`, `poc_protocol.hpp`,
  `apps/host/src/capture_backend_dxgi.cpp`

검증/build/test
- 단위 9종 전부 PASS (신규 3종: 세션 결정 7케이스, 좌표 매핑 13케이스, DXGI 출력 선택 7케이스).
  세 가지 모두 실패가 조용한 종류라 순수 함수로 분리해 고정했다 — 잘못된 답도 모든 상위 계층에서
  성공으로 보인다.
- N6 실측: 기본 포트 접속 시 스왑 없이 decoded 30~31fps, LAN 포트 접속 시 스왑 후 29~31fps, 손실 0.
- **성능 회귀 없음**: 변경 전후 동일 명령·씬으로 각 6회. 중앙값 53.2 → 53.2, 평균 52.5 → 52.6
  (+0.1%), 실패 0/6 → 0/6. 회귀 기준은 -10%였다.

다음 액션
- 남은 항목은 U2b(SYSTEM 캡처 에이전트), U2c(프레임 채널), U2d/U5(상태·결과 반환), U3(전환 중재).
  U1에서 BitBlt 경로가 확정됐고 `winlogon_capture_probe.cpp`가 검증된 레퍼런스다.
- 수동 확인 대기: 회사 Wi-Fi 접속(N1), RDP 전환 시 DXGI 복구 실동작(R1/R3), UAC 창 실측(U2b 이후).

### 233) 2026-08-04 N5: 주소 하나로는 안 된다 — 후보 여러 개 + 클라이언트 레이스

목표
- 접속 실패를 포트 선택으로 푸는 것이 불가능함을 확인하고, 구조로 푼다.

왜 필요했나
- 0.2.4에서 호스트를 443으로 옮겼더니 **집 접속이 죽었다.** 실측: 443 bind 성공, NAT가 포트
  보존(`public=...:443`, 재작성 경고 없음), 방화벽은 프로그램 기준 인바운드 Allow — 그런데
  43000으로는 붙던 폰이 LTE에서 4회 펀치 동안 한 개도 도달하지 못했다. 국내 가정용 회선이
  서버 운영을 막으려고 well-known 포트 인바운드를 차단한다.
- 0.2.5로 되돌리니 집은 복구됐지만 회사망이 다시 막힌다.
- **두 네트워크가 연결의 반대쪽 끝을 제약한다.** 회사 방화벽은 클라이언트의 아웃바운드
  목적지 포트를, 집 ISP는 호스트의 인바운드를 제한한다. 포트 하나로는 원리상 불가능하다.
- 확인차 실행 중인 원격 제품을 봤더니 고정 well-known 포트를 인바운드로 여는 것이 없었다.
  전부 양쪽이 밖으로 나가서 만나는 구조다.

구현
- 호스트가 사설 IP·bind 포트·대체 포트를 하트비트로 광고. down/loopback/link-local 어댑터는
  제외한다(link-local은 DHCP 실패를 뜻하므로 펀치 예산만 태운다).
- 디렉토리가 공인 관측값과 합쳐 후보 목록을 만들어 `/api/connect`에서 반환. 호스트가 보고한
  값은 **다른 클라이언트가 다이얼할 주소**가 되므로 dotted quad 검사·중복 제거·개수 제한을 건다.
- 사설 후보가 선두. 성공하면 트래픽이 공유기를 아예 안 거친다 — 더 빠르고, 헤어핀 안 되는
  공유기에서는 유일한 경로다.
- 클라이언트가 **전부 동시에** 펀치하고 먼저 응답하는 주소를 채택. 같은 소켓에서 나가므로
  응답한 주소는 이미 미디어가 도착할 매핑이다.
- 순차 시도가 아닌 이유: 목록은 **선호도** 순이지 성공 확률 순이 아니라서, 막힌 주소가 앞에
  있으면 대기가 배로 늘어난다. 실소켓 테스트로 고정 — 죽은 주소 2개 뒤의 살아있는 주소를 0ms에 찾는다.
- 아무도 응답 안 하면 첫 후보로 그냥 진행한다. 펀치를 버려도 hello는 통과시키는 NAT가 있어,
  여기서 포기하면 될 연결을 거부하게 된다.
- 구버전 호환: `hostPublicIp/Port`는 그대로 두고, 목록이 없으면 클라이언트가 단일 주소로 합성한다.

변경 파일
- 신규: `connect_candidates.hpp`, `connect_candidates_test.cpp`, `punch_any_test.cpp`
- `directory_client.{hpp,cpp}`, `native_video_host_main.cpp`, `directory_rendezvous.{hpp,cpp}`
- `apps/directory/server.js`, `apps/directory/test/directory_test.js`
- `native_bridge.cpp`, `DirectoryClient.kt`, `NativeSessionBridge.kt`, `MainActivity.kt`

검증/build/test
- 단위 11종 전부 PASS. `punch_any_test`는 모킹이 아니라 실소켓으로 돌린다 — 중요한 성질이
  선택이 아니라 **타이밍**이기 때문이다.
- 디렉토리 E2E: 후보 순서·필터링·구버전 호환·펀치 흐름 무손상 전부 PASS.
- 0.2.6 패키징. APK는 0.2.2와 **동일한 서명 인증서**(dcc806ae...2990)라 업데이트로 설치된다.
  설치 파일 페이로드에 포트 목록과 하트비트 필드가 실제로 들어갔음을 문자열로 확인했다.

다음 액션
- 회사 Wi-Fi 실기 검증. 후보 중 3478이 뚫리면 `directory_chosen` 로그에 `public-alt`가 찍힌다.
- 같은 집 Wi-Fi에서는 `private`가 찍혀야 하고, 그러면 공유기를 안 거치므로 더 빨라야 한다.

### 234) 2026-08-06 UAC 클릭 해결 — 원인은 액세스 마스크와 DPI, 두 개였다

배경
- 0.2.6 이후 UAC 프롬프트는 폰에 **보이는데 눌리지 않았다**. 캡처는 처음부터 정상이었다.
- 계측을 다섯 번(0.2.7~0.2.11) 붙여 실패 지점을 한 줄까지 좁혔다:
  `agent started in session 1, created on desktop=Winlogon` → `attach=ok` → `SetCursorPos` 성공
  (커서가 실제로 움직임) → `SendInput ... err=5`. 라우팅 카운터는 208/208 정상.

원인 ① 클릭이 안 되던 것 — `DESKTOP_JOURNALPLAYBACK` 누락 (0.2.12)
- `SendInput`은 주입 전에 **두 가지**를 본다: 호출 스레드가 현재 입력 데스크톱에 있는가,
  그리고 그 데스크톱 핸들이 `DESKTOP_JOURNALPLAYBACK`으로 열렸는가.
- **둘 다 실패 시 같은 `ERROR_ACCESS_DENIED(5)`** 를 준다. 구분이 안 된다는 것이 이 버그가
  오래 산 이유다. 우리는 전자만 의심하며 세션·데스크톱·토큰을 계속 고쳤다.
- `SetCursorPos`는 이 권한을 요구하지 않는다. **커서는 움직이는데 클릭만 거부되는** 관측된
  증상이 정확히 그 신호였고, 우리는 그것을 "부분 성공"으로 읽었다.
- 마스크가 거부되면 기존 마스크로 재시도한다 — 그런 데스크톱에서도 커서 이동은 살린다.
  어느 쪽으로 열렸는지 `journal=ok|DENIED`로 남겨, 앞으로의 err=5가 재발인지 구분되게 했다.

원인 ② 위치가 어긋나던 것 — DPI awareness 미선언 (0.2.13)
- ①을 고치자 클릭은 되는데 **의도한 지점보다 오른쪽 아래**에 찍혔다.
- 화면 좌표를 다루는 컴포넌트 중 **에이전트만** `SetProcessDpiAwarenessContext`를 부르지
  않았다. 캡처 워커·호스트 창·뷰어는 전부 부른다.
- 호스트는 물리 픽셀을 캡처해 보낸다. DPI-unaware 프로세스는 가상화 좌표계를 받으므로
  `SetCursorPos`가 배율만큼 곱해진다 — **원점에서 멀수록 더 밀리는** 오른쪽·아래 오차.
- 100%에서는 두 좌표계가 일치해 무동작이라, 이미 되는 환경을 깨뜨리지 않는다.

왜 오래 걸렸나
- **하나를 고쳐야 다음 것이 드러나는 구조.** ①이 남아 있는 동안 ②는 관측조차 불가능했다.
- **성공 경로에 계측이 없었다.** 좌표 로그가 실패 경로에만 있어서, 클릭이 성공하기 시작한
  순간부터 "성공했지만 틀린" 상태가 보이지 않았다. `GetCursorPos` 되읽기를 넣자 한 번에 갈렸다.
- 매핑 계산과 브로커 배선은 **처음부터 옳았다**. `in=(1091,721) → mapped=(1091,721)` 항등.

기각된 가설 (전부 실측으로)
- 캡처가 보안 데스크톱을 못 본다 → 반증(U1). BitBlt·DXGI 둘 다 읽고 폰에 보인다.
- 프로세스가 잘못된 데스크톱에 생성된다 → `created on desktop=Winlogon`인데도 err=5.
- 코드 서명 + `uiAccess=true`가 답이다 → **OSLink 바이너리는 `uiAccess=false`인데도 된다.**
  인증서를 샀으면 헛돈이었다.
- 가상 HID 드라이버가 유일한 길이다 → 불필요. OSLink는 실제로 `LdVMou.sys`/`LdVKbd.sys`를
  쓰지만, 우리 문제는 드라이버 부재가 아니라 액세스 마스크였다. EV 인증서 연간 비용과
  커널 리스크를 지지 않아도 됐다.

검토했다가 쓰지 않은 대안
- `PromptOnSecureDesktop=0`: 프롬프트를 일반 데스크톱으로 내린다. 코드 0줄이지만 그 PC가
  UAC 스푸핑 보호를 잃는다. **불필요해졌다.**
- `VirtualInput`(MIT 가상 HID): 서명 바이너리를 배포하지 않아 `testsigning` 필요. 제품 불가.
- HVDK: 드라이버 인증서 비용으로 **개발 중단**, 재배포 불가.

변경 파일
- `secure_input_service_main.cpp` (액세스 마스크, DPI awareness, 착지 진단), `product_version.hpp`

검증/build/test
- 단위 5종 PASS(secure_input_mapping/session, connect_candidates, bind_port, dxgi_output_selection).
- **실기 확인 완료: UAC 프롬프트가 정확한 위치에서 클릭된다.**
- 0.2.12 → 0.2.13 패키징. APK는 0.2.6 그대로 — 호스트만 재설치하면 된다.

후속
- U2b/U2c/U2d/U3(보안 데스크톱 전용 캡처 경로)는 **불필요로 확정**. 캡처는 원래 되고 있었다.
- U5(주입 결과를 ack에 실어 반환)는 유효하게 남는다. 지금도 실패가 성공으로 보고된다.

### 235) 2026-08-06 회사 Wi-Fi 진단 Phase 1 — 서버측 무응답 프로브 + 깨우기 펀치

목표
- 회사 Wi-Fi 접속 불가의 원인 A(타이밍 버그)/B(방화벽 차단)를 **구분**한다. 고치기 전에 재기부터.
- 핵심 제약: APK를 건드리지 않는다(Phase 7까지). Android 11이 폰 로그를 막으므로
  **모든 증거는 서버 로그에 남는다.**

방법 (Phase 1, 서버만 변경)
- `REMOTE60_NAT_DIAG_*` 환경변수 4개로 옵트인. 전부 미설정이면 동작 변화 0.
- UDP/43000 **무응답 리스너**: 폰의 펀치가 도달하는지, 소스 포트가 8081 관측값과 같은지 기록.
  응답하지 않는 것이 안전 속성이다 — 응답하면 후보 레이스에서 이겨버린다.
- `diag-silent` 후보를 후보 목록 **맨 끝**에 주입 — 폰이 우리가 관측 가능한 포트로
  패킷을 쏘게 만든다. 무응답 폴백은 첫 후보를 고르므로 이 후보는 선택될 수 없다.
- connect 즉시 observe 소켓에서 호스트로 **깨우기 펀치** 3회(0/100/300ms) — 하트비트
  최대 25초 대기를 우회. 호스트가 이미 열어둔 NAT 매핑을 그대로 탄다.
- `connectId`로 connect/프로브 수신/호스트 로그를 상호 연결.

변경 파일
- `apps/directory/server.js` (진단 블록 +134줄, 기본 경로 무변경)
- `apps/directory/test/nat_diag_test.js` (신규 — 안전 속성 고정: 후보 1개/맨 끝/무응답/중복 없음)
- `apps/directory/test/run.js` (진단 ON 전용 서버를 별도로 띄워 검증)

검증/build/test
- `node test/run.js` 전체 PASS — 진단 OFF 기존 테스트 회귀 없음 + 진단 ON 안전 속성 9건 PASS.
- 테스트 첫 실행에서 `host registers 400` — nat_diag_test가 세션 토큰으로 등록을 시도했는데
  등록은 body의 id/pw/machineId 인증이다. 다른 테스트와 같은 방식으로 수정 후 전체 통과.

다음 액션
- 서버 배포(`deploy_directory.ps1 -SignupKey ... -MinPasswordLength 4`) + systemd unit에
  `REMOTE60_NAT_DIAG_ENABLED=1`, `NAT_DIAG_IP=223.130.132.180`, `NAT_DIAG_PORT=43000` 추가.
  **선행: 서버 방화벽/ACG UDP 43000 인바운드 허용.**
- 실기 검증: LTE 1회(대조군, `rx dport=43000` 필수) → 집 1회(회귀) → 회사 Wi-Fi 3회.
  판정은 서버 로그만으로. 게이트는 구현계획 N7 참조.

### 236) 2026-08-06 N7 Phase 1 실측 — 포트 필터가 아니었다, E1 기각

측정 (서버 로그, 19:12~19:14)
- 회사 게스트 Wi-Fi 5회 + LTE 3회, **여덟 시도 전부** 폰의 펀치가 서버 UDP/43000에 도달.
  첫 발 10~51ms, 시도당 27발(150ms 간격, 4초 예산과 정합). 사용자 기록 9회 중 로그엔 8회 —
  1회는 /api/connect 자체가 없음(앱측 중복 방지로 추정, 추적 불요).
- **전 시도 samePort=true**: observe(8081)가 본 공인 포트와 진단 리스너(43000)가 본 소스
  포트가 동일. 폰 NAT는 목적지가 달라도 같은 매핑을 쓴다(엔드포인트 독립, cone).
- 대조군 LTE 정상, 진단 체인 유효. ACG UDP 43000 개방 후 개발 PC 프로브 2/2 도달 확인 선행.

판정
- **E1("회사 Wi-Fi에서 UDP 43000 아웃바운드가 막힌다") 기각.** 게스트망은 외부 UDP/43000을
  통과시킨다. 기존 실측(호스트 도달 0건)과의 차이는 목적지다: 서버는 진짜 외부지만 호스트
  공인 211.218.222.1은 **같은 회사 edge로 되돌아가는 헤어핀**이고, 그 경로만 죽어 있다.
- 남은 용의자: ①타이밍 순환(호스트가 4초 창 안에 못 움직임) ②호스트→폰 방향 헤어핀 불가.
  Phase 2가 ①을 제거하면 ②만 남는다.

부수 관측 (추적만, 지금 안 고침)
- LTE 성공 직후 재시도 2회가 연속 실패(19:13:09, :37 — 4초 풀 펀칭). 직전 세션 정리가
  안 끝난 호스트가 새 핸드셰이크를 무시했을 가능성. 재현되면 별도 항목으로 승격.

Phase 2 가동 (서버만, 19:17)
- drop-in에 `REMOTE60_NAT_DIAG_WAKE=1` 추가, `wake=on` 로그 확인.
- 성립 근거(호스트 0.2.15 무수정): `ConsumeUdpPacket`이 펀치 수신 시 `refreshRequested_`를
  세워 하트비트 슬립을 200ms 안에 깨우고, 즉시 하트비트→pendingPunch 수신→`Punch()` 25발
  ×200ms를 폰 공인 주소로 발사. `AuthorizePeer`는 헤어핀 주소 변환도 이미 허용한다.
- wake 경로: 서버 observe 소켓(8081)→호스트 공인 43000. 호스트가 OBSERVE를 8081로 보내므로
  정확히 그 tuple의 NAT 매핑이 살아 있다.

변경 파일
- `docs/구현계획.md` (E1 기각, N7 체크), 서버측 drop-in 1줄 (저장소 외)

검증/build/test
- 코드 변경 없음. 서버 재시작 후 `wake=on` + 리스너 기동 로그 확인.

다음 액션
- 회사 Wi-Fi 3회 재시도 → 성공이면 타이밍 확정, Phase 3(held-heartbeat 영구화) 설계.
  실패면 호스트 로그(`directory peer punch`/`directory punch ->`)로 펀치 발사 여부 확인 →
  발사됐는데 실패면 호스트→폰 헤어핀 사망 = 중단 규칙 요건 충족 방향, Phase 4(IPv6)로.
- 집 Wi-Fi 회귀 1회 잔여.

### 237) 2026-08-07 N7 Phase 2/4 실측 + Codex 합의 — 타이밍 기각, 남은 건 대조군 하나

Phase 2 측정 (10:10~10:14, wake=on, 회사 Wi-Fi 4회)
- 서버: 4회 전부 connect 즉시 wakeTx + 폰 펀치 27발 수신(4초 풀 소진 = 접속 실패).
- 호스트 로그(사용자 제공): 4회 전부 `directory peer punch` → 1초 내
  `directory punch -> 211.218.222.4:{55133,48640,38261,44838}` — 폰의 정확한 관측 endpoint로
  25발×200ms 발사. 그런데도 양방향 무도달.
- **타이밍 순환은 원인에서 제거.** 호스트가 즉시 알고 즉시 쐈는데도 안 됐다.
- 호스트 로그의 `peer punch`가 시도당 정확히 2회 = wake 3발과 정합, 폰 27발 도달과 불합.
  앱 레벨 증거도 "폰 펀치가 호스트에 안 온다"를 가리킨다.

Phase 4 (사용자 실측)
- 회사 Wi-Fi에서 폰 IPv6: test-ipv6.com **0/10 — 불가 확정.**

Codex 논의 (agent-bus, 스레드 019fd9d0-2900-7232-b4c1-9359a5152d0e, 2라운드)
- 판정 용어 정제(수용): "헤어핀 양방향 사망"이 아니라 **"사내 egress/NAT realm 간
  public-to-public UDP 경로 사용 불가, 폐기 지점 미확정"**. 두 공인 IP가 같은 NAT 장비인지
  미확인이고, edge 통과 후 source가 .1:43000인지도 직접 관측 안 됨.
- 어제 19:12 LTE 성공은 대조군 부적격(직후 2연속 실패로 호스트 상태 신뢰 불가).
  **같은 진단 창에서 깨끗한 LTE 성공이 필요.**
- P3(held-heartbeat) 생략 확정 — Phase 2가 그 가설을 더 직접적으로 시험했다.
  wake는 direct 성공률 개선 기능으로 영구 승격 후보(가드조건 합의됨).
- pktmon 반론 수용: 호스트 NIC 캡처로 "앱이 받고 버렸다" 가능성을 완전히 닫는다.
  LTE 인바운드 보임 + 회사 인바운드 0 + 아웃바운드 보임 → 미도달 확정.
  회사 인바운드 27발이 NIC에 보이면 → 판정 뒤집힘, 우리 소켓/파싱 버그 조사.
- 대기시간 질문의 답: 30초 근거 없음. **LTE 후 호스트 재재시작**이 정답.
- 중단규칙 4/5 충족. 최종 프로토콜(9단계) 합의 — 구현계획 N7 참조.

변경 파일
- `docs/구현계획.md` (N7 체크·판정 용어·최종 프로토콜), `docs/history.md`

검증/build/test
- 코드 변경 없음. 서버/호스트/APK 전부 무수정 유지.

다음 액션
- 사용자: 최종 동기화 창 실행(pktmon + 호스트 2회 재시작 + LTE 1회 + 회사 1회).
- 결과가 `호스트 아웃바운드 확인 + 폰 펀치 NIC 미관측`이면 중단규칙 발동, P6 릴레이 POC 설계
  (Codex와 이어서). N4(미디어 암호화)가 릴레이와 함께 필수로 복귀함을 잊지 말 것.
- 부수: 세션 정리 anomaly(LTE 성공 직후 재시도 실패) 별도 항목 승격 대기.

### 238) 2026-08-07 중단규칙 5/5 발동 — 직접 연결 탐색 종료, 릴레이(N8)로

최종 동기화 창 (11:17~11:20, pktmon + 서버/호스트 로그 3소스 대조)
- LTE 2회: 폰 펀치 인바운드 도달 → 풀 세션 (1200B 영상 수천 패킷). 연속 성공 —
  어제의 세션 anomaly 미재현.
- 회사 Wi-Fi 3회: 호스트가 폰의 정확한 endpoint(57380/43501/53805)로 49B 펀치 25발×5초
  발사, NIC 아웃바운드 실측. **`.4`발 인바운드 0패킷.** "앱이 받고 버렸다" 최종 배제 —
  애플리케이션 상태는 NIC에 도착한 패킷을 소급해 없앨 수 없다.
- pcap 세부: pktmon 이중 계수(스택 2계층) 확인, 실 패킷수는 표시의 절반. wake 15발
  (3×5 connect) 전부 서버 8081→호스트 43000 관측.

판정 (Codex 동의, agent-bus 3라운드)
- 중단규칙 5요건 전부 충족: 서버 수신✓ 즉시 펀치✓ 무도달✓ 같은 창 LTE 성공✓ IPv6 0/10✓
- **"사내 egress/NAT realm 간 public-to-public UDP 경로 사용 불가(폐기 지점 미확정).
  해당 회사망의 IPv4 직접 연결 탐색을 중단하고 N8 릴레이 POC로 이동한다.
  집·LTE 직접 연결은 유지한다."**

Codex 설계 검토에서 얻은 정정 2건 (둘 다 일을 줄이는 방향)
- ① 내 demux 가설 정정: Hello와 활성 세션 control은 ConsumeUdpPacket **이전에** 처리된다.
  relay는 폰의 Punch를 호스트로 전달할 필요가 없고, 자체 응답 후 Hello의 punchToken으로
  세션을 바인딩하면 된다.
- ② **TCP relay 불필요**: 디렉토리 경로의 APK 0.2.6은 requireTcpControl=false,
  controlOverUdp=true — control이 미디어 UDP 소켓의 UdpControlLink로 흐른다
  (native_bridge.cpp:184, native_video_client_session.cpp:291). TCP listener는 수동 IP용
  레거시. relay는 순수 UDP byte-forwarding으로 충분하다.
- 추가 합의: relay 포트는 기존 diag 43000 재사용(회사망 통과 실측 완료), 호스트 leg는
  8081 observe 소켓(매핑 실측 완료), direct grace 2.5초(1.5초는 4초 예산 대비 근거 부족),
  POC 격리는 테스트 계정+회사 IP 한정으로 직접 경로 회귀를 구조적으로 차단,
  호스트당 단일 lease, token 없는 패킷은 경로를 열지 않음, 평문은 POC 한정(N4가 게이트).

변경 파일
- `docs/구현계획.md` (N7 완결, N2/N3→N8 부활 표기, N8 릴레이 POC 신설, N9 wake 승격 신설)
- `docs/history.md`

검증/build/test
- 코드 변경 없음. 증거 파일 보존: `logs/GNLink-sync.{etl,pcapng}`, `logs/host_app.log`.

다음 액션
- N8 릴레이 POC 구현 (서버 단독, APK·호스트 불변) — 착수 승인 대기.
- 잔여 수동 확인: 집 Wi-Fi 회귀 1회 (wake=on 상태).
- 세션 정리 anomaly는 미재현으로 우선순위 하향, 관찰 지속.

### 239) 2026-08-07 N8 릴레이 POC 구현 — 서버 단독, APK·호스트 무수정

목표
- 회사망처럼 두 피어 사이에 경로가 아예 없는 환경에서 접속을 성립시킨다. 직접 경로가 되는
  집·LTE 는 손대지 않는다(하드 제약).

성립 근거 (기존 동작에 얹은 것이지 새로 만든 것이 아니다)
- 클라는 후보를 전부 펀치하고 **먼저 답한 쪽**을 채택한다 → 답하면 쓰인다.
- 호스트는 Hello 를 검증한 뒤 **그 Hello 를 보낸 endpoint** 를 udpPeer/senderPeer 로 채택한다
  (`native_video_host_main.cpp:3175-3210`) → observe 소켓(8081)에서 Hello 를 넘기면 서버가 peer.
- 디렉토리 경로의 control 은 TCP 가 아니라 미디어 UDP 소켓 위를 흐른다
  (`native_bridge.cpp:184` requireTcpControl=false / controlOverUdp=true) → TCP leg 불필요.

구현 (`apps/directory/server.js` 단독)
- 후보 목록 맨 끝에 `relay` 후보. 허용 IP·계정 **둘 다 fail-closed** — 미설정이면 아무에게도
  주지 않는다. 후보를 못 받은 클라는 릴레이와 경쟁할 수 없으므로 직접 경로 무영향이 구조적이다.
- Punch 는 token 이 없다. "이 IP 가 최근 relay-eligible connect 를 했다"만 gate 하고 grace
  (기본 2.5초) 후 응답만 한다. 세션 식별은 Hello 의 32-hex token 으로만.
- relay 연결에서는 diag 플래그와 무관하게 wake 발사 — 릴레이는 Punch 를 전달하지 않으므로
  호스트를 깨울 다른 경로가 없고, 하트비트는 최대 25초인데 클라 Hello 예산은 약 3초다.
- diag 리스너를 relay-aware 리스너로 승격(포트 공유라 동시 bind 불가). diag-silent 후보는
  `!RELAY_ENABLED` 일 때만.

Codex 리뷰 2회에서 잡힌 blocker (agent-bus, 세션 remote#xcl572oo)
- **앱 재시작이 영구 차단될 수 있었다.** 재시작은 새 token 인데 lease 를 "같은 token 만 재시도"
  로 막았고, 게다가 호스트가 죽은 폰 주소로 계속 쏘면 그 트래픽이 lastSeenAt 을 갱신해 좀비
  세션이 만료되지 않았다. → `relayLatestTokenByHost` 로 **최신 connect 가 이긴다**, 옛 token 은
  auth 자체를 폐기. TTL 은 `lastClientAt`(클라 침묵)만 본다 — 근거: 클라 Ping 간격은
  `clamp(x, 20, 10000)`ms(`native_video_client_shared_core.cpp:360`)라 60초면 안전.
- **인덱스 삭제가 identity-safe 하지 않았다.** 같은 폰 주소가 다른 호스트로 옮기면 옛 세션 정리가
  현재 세션의 인덱스를 지웠다. → `map.get(key)===session` 일 때만 delete, 수명 관리 주체를
  `relaySessions` Set 으로 분리(클라 맵에서 밀려난 세션도 sweep 대상).
- Hello 를 established fast path **보다 먼저** 판정하도록 순서 반전. 같은 token 재전송은 세션
  재생성 없이 전달, 다른 token 은 최신 auth 검증 후 supersede, token 불명은 명시 drop.
- handshake shape 검증을 호스트와 동일하게(size/version/FEC). HelloAck 도 같은 파서.
- **staged 인덱스에 옛 버전이 남아 있던 것도 Codex 가 잡았다** — 그대로 커밋했으면 위 수정과
  테스트, README 가 전부 빠질 뻔했다. 재-stage 후 cached diff 로 확인.

변경 파일
- `apps/directory/server.js`, `apps/directory/test/relay_test.js`(신규),
  `apps/directory/test/run.js`, `apps/directory/README.md`

검증/build/test
- `node test/run.js` 전체 PASS. relay 29건은 실 UDP 소켓으로 가짜 호스트/폰을 띄워
  `connect→wake→Punch→grace→Hello→HelloAck→양방향 1200B` 를 왕복시킨다. 특히 고정한 것:
  wake 가 8081 에서 실제 발사됨, Hello 가 8081 발신으로 호스트에 도달(설계 전제),
  pendingPunch 무손상(호스트 인증 안 뺏김), 호스트가 릴레이 중에도 OBSERVE 가능,
  앱 재시작이 좀비 세션을 인계, 옛 token 재탈취 불가, 허용목록 밖 계정 무후보.
- README 의 "Video never passes through here" 를 정정(직접 경로 한정 + relay 예외·과금·N4 선행).

다음 액션
- 배포 후 회사 Wi-Fi 실기: 영상 첫 프레임 + 30초 유지 + control 왕복 1건.
- LTE·집 무회귀 확인(`chosen != relay`, relay 미디어 바이트 0).
- 비차단 후속: idle TTL 판정을 순수 함수로 분리해 단위 테스트로 고정.

### 240) 2026-08-07 두 번째 접속부터 죽던 이유 — 세션 인계가 없었다 (0.2.16)

증상
- 릴레이 배포 후: 호스트 재시작 → 1회차 정상 → **2회차부터 `window_list_request pending` + 무영상.**
  사용자 실측으로 재현 확정. LTE 로 바꿔도 동일.
- 릴레이 서버 로그가 매 세션 `c2h=2/101B h2c=2/197B` 로 **바이트까지 동일**. 첫 성공 세션은
  `h2c=211944/243MB` 였다. Hello/HelloAck + 클라 control 1개 + 호스트 ControlAck 1개 후 정지.
- 호스트 로그의 `[control]` 라인은 세 줄뿐: `stream-state active=0`, `window-list seq=2`,
  `udp control session ended`. 그 뒤 5회 접속에 `[control]` 이 **한 줄도 없다.**

원인 (둘 다 기존 결함 — 릴레이가 만든 게 아니라 드러냈다)
- **A. 새 세션 판정이 peer 주소 변경 기준** (`native_video_host_main.cpp:4034`). 릴레이를 쓰면
  모든 클라가 호스트 눈에 `223.130.132.180:8081` 하나다 → 2회차부터 `changed=false` →
  `udpControlChannel.Reset()` 누락. 클라는 세션마다 채널을 새로 만들어 seq 1 부터 시작하는데
  (`native_video_client_session.cpp:84-88, 838-856`) 호스트 채널의 `rxDeliveredSeq_` 는 이전
  세션 값 그대로 → `HandleData` 가 **ACK 만 보내고 페이로드를 버린다**
  (`udp_control_channel.cpp:167`). 관측된 h2c=2 와 정확히 일치.
- **B. UDP 조작 세션이 일회용** (`:4055`). TCP 는 accept 루프 안에서 세션을 반복하는데
  (`:3916`) UDP 만 한 번 호출하고 스레드가 끝난다. 게다가 이탈 시 `streamControlActive=false`
  (`:3877`) 라 렌더 루프가 `if (!streamActive) continue`(`:5689`) 에 걸려 영상도 영구 정지.
  복구 코드(`:3347`)는 재진입해야만 도달한다.

수정 — 세션 epoch (A/B 를 한 경계에서 함께)
- 새 세션의 유일한 신호는 **인증된 Hello 의 capability 토큰**이다. endpoint 는 신호가 될 수
  없다. `classify_directory_hello` 가 Rejected/Retransmit/NewSession 을 반환하도록 기존
  auth 캐시(`:2917-2937`)를 확장 — 캐시가 이미 토큰 재전송을 허용하므로 일회성 소비
  (`directory_client.cpp:754`)와 충돌하지 않는다.
- 인계 순서: 리더가 epoch 를 올리고 `Close(SessionRollover)` 로 디스패처를 깨운 뒤 **대기** →
  디스패처가 **자기 스레드에서** `Reset()` → `serve_control_session` 재진입(여기서 스트림 복구)
  → ready 발행 → 그제서야 리더가 HelloAck 송신. 클라는 Ack 를 받을 때까지 Hello 만 재전송하므로
  (`native_video_client_session.cpp:749-785`) 리셋 전에 도착하는 창이 없다.
- 디스패처는 이제 프로세스 수명 내내 세션을 하나씩 이어서 서비스한다. 종료 조건은 전역 `stop`
  뿐 — `IsClosed()` 는 정상적인 peer 이탈에서도 참이라 종료 신호로 쓰면 안 된다.
- endpoint 변경도 여전히 새 세션으로 친다(토큰 없는 LAN 접속용). 단 같은 LAN 포트로 재접속하는
  경우는 여전히 구분 불가 — 프로토콜 nonce 없이는 닫히지 않는 잔여 구멍으로 남긴다.

곁들여 고친 것
- `ControlCloseReason`(peer-lost / session-rollover / shutdown). 종료 사유가 로그에 없어서
  이번 진단이 오래 걸렸다. 이제 세션 종료마다 epoch 와 사유가 남는다.
- window 선택 대기(`:3712`)가 링크 사망 시 풀리도록. 떠난 클라의 선택 응답을 기다리느라 다음
  세션 인계가 지연되던 경로.

Codex 리뷰에서 교정된 것 (agent-bus 2라운드)
- 내 "헤더/본문 Read 사이 Reset 경합" 가설은 **틀렸다**. `EnsureInbound` 가 완성 메시지를
  통째로 꺼내 보관하므로 이미 꺼낸 메시지는 찢어지지 않는다. 다만 리더가 직접 Reset 하는 것은
  의미론적 race 가 맞아 epoch barrier 로 바꿨다.
- 내 계획의 "`IsClosed()` 면 진짜 종료" 는 **반대**였다. 정상 peer 이탈에서도 true 다.
- `streamControlActive` 복구는 리더가 아니라 **디스패처 재진입 지점**이 맞다. 리더가 켜면
  디스패처가 준비되기 전에 스트림이 돌고, 같은-토큰 재전송이 클라의 의도적 `active=0` 을 뒤집는다.

변경 파일
- `native_video_host_main.cpp`, `udp_control_channel.{hpp,cpp}`, `udp_control_channel_test.cpp`,
  `product_version.hpp`(0.2.16), `dist/GNLinkSetup-0.2.16.exe`

검증/build/test
- 호스트·인스톨러 빌드 PASS. `remote60_udp_control_channel_test` 신규 핸드오버 5건 + 기존 5건
  PASS, `connect_candidates`·`secure_input_mapping` PASS.
- 핸드오버 테스트가 고정하는 것: 떠난 peer 가 shutdown 이 아니라 peer-lost 로 보고되고,
  Reset 후 채널이 다시 열리며, **다음 클라의 첫 메시지가 ACK 만 되지 않고 실제로 전달된다.**

다음 액션
- 실기: 0.2.16 설치 후 **회사 Wi-Fi 로 연속 3회 접속** — 2회차부터 되는지가 판정.
  이어서 LTE·집 무회귀, control 왕복(창목록), 30초 유지.
- 남은 잔여: 같은 LAN endpoint 재접속 구분(프로토콜 nonce 필요), N9(wake 영구 승격),
  render 측 세션 경계 정리(encodedSeq/cadence/metrics/pending 요청 — Codex 목록).

### 241) 2026-08-07 접속 경로 표시 (APK 0.2.7)

배경
- 릴레이가 붙으면서 같은 "연결됨"이 두 가지 뜻을 갖게 됐다. 직접은 공짜이고 빠르며, 중계는
  서버 트래픽이 과금된다. 사용자가 그걸 모르고 쓰면 안 된다는 요청.

구현
- 배선은 이미 있었다. 후보의 `kind`가 문자열로 브리지까지 올라오고(`native_bridge.cpp:230,246`)
  `nativeDirectoryChosenCandidate()`로 노출돼 있었는데 진단 로그로만 흘렸다. 이제 파싱해
  UI로 보낸다.
- 뷰어: 데이터 사용량 카운터 바로 아래 배지(`viewerPathText`). 중계일 때만 앰버색 —
  오류가 아니라 "위 숫자가 과금된다"는 표시다.
- 목록 화면: 무언가를 열기 전에 한 번, 문장으로. 배지 두 글자로 알 일이 아니다.
- 세션 종료 시 초기화(`resetViewerObservability`) — 남아 있으면 다음 접속에 대한 거짓말이 된다.
- Windows 클라이언트는 수동 IP 방식이라 후보 경주 자체가 없어 해당 없음.

변경 파일
- `MainActivity.kt`, `activity_main.xml`, `strings.xml`, `build.gradle.kts`(0.2.6→0.2.7, code 6)
- 산출물 `dist/GNLink-0.2.7.apk`

검증/build/test
- `:app:assembleDebug` PASS, APK 생성 확인. **실기 확인 필요** — 회사 Wi-Fi에서 "중계",
  집·LTE에서 "직접"이 뜨는지.

### 242) 2026-08-07 wake 정식 승격(N9) + 인코딩 해상도를 비트레이트에 연동(N11)

#### N9 — wake를 진단 플래그에서 떼어냈다
- 오늘 오후 이 한 줄(`REMOTE60_NAT_DIAG_WAKE=1`)이 배포에서 빠지자 LTE 접속이 7회 연속 실패했다.
  에러는 한 줄도 안 났다. 진단용 플래그에 필수 기능이 매달려 있던 것이 원인.
- 이제 **기본 켜짐**, `REMOTE60_WAKE_DISABLED=1`로만 끈다. 부팅 시 어느 쪽이든 로그로 말한다 —
  꺼졌을 때의 증상이 "조용히 안 됨"이라서.
- 가드: 하트비트가 90초 내 확인한 주소로만 발사(`WAKE_HOST_FRESH_MS`), 호스트당 1초 1버스트로
  합침(재시도하는 클라가 폭주가 되지 않게), 5분마다 sent/suppressed/skippedStale/failed 집계.
- 릴레이 여부와 무관하게 모든 connect에 발사한다. 릴레이는 펀치를 전달하지 않고 응답하므로
  호스트가 알 길이 없고, 직접 경로에서도 제한적 NAT가 폰의 펀치를 떨군다.
- 테스트: `directory_test.js`에 **환경변수 없는 기본 상태**에서 wake가 발사되는지 고정.
  이게 정확히 오늘 깨진 것이라 기본값 자체를 테스트로 박았다.

#### N11 — 장면 전환 화질 아티팩트 (신규)
- 증상: 3Mbps/1080p30에서 화면이 급변하면(게임 메뉴) "잘못된 화면이 잠깐" 보인다. 5~20초에 1회.
- **A/B로 원인 확정**: 8Mbps로 올리면 사라진다 → 패킷 손실이 아니라 인코더 레이트 컨트롤.
  **P2(NACK)는 이 증상의 해법이 아니며 착수하지 않는다.**
- Codex 교정으로 내 최초 진단이 뒤집혔다: (1) 내가 근거로 쓴 keyReqTotal/senderQueueDrops/
  senderSendDurMax는 1초 값이 아니라 **세션 누적**이고 rawEquivMbps는 장면 복잡도가 아니라
  그냥 NV12 한 장 크기다 (2) Android는 조립 실패 시 즉시 디코더를 리셋하고 키프레임을 요청하며
  그전 P프레임을 버리므로(`native_video_client_session.cpp:563-598`) 고전적 참조 드리프트가
  **구조적으로 불가능**하다.
- 처방: 비트레이트를 못 올리므로(모바일 데이터 제약) **해상도를 내려 픽셀당 비트를 확보**한다.
  `encode_resolution_ladder.hpp`(신규, 순수 함수+테스트): >=5Mbps 원본 유지, <=4Mbps 720p 상당,
  그 사이는 직전 답 유지(히스테리시스 — 경계에서 인코더 재초기화가 반복되면 그 자체가 끊김).
- 기존에 `args.bitrate <= 1500000`인 경우에만 720p로 내리는 규칙이 있었다. 임계값이 너무 낮아
  3Mbps에서 안 걸렸다. 그 규칙을 사다리로 대체하고 **런타임 튜닝 경로에도 적용** — 이전에는
  기동 시에만 결정돼서 앱에서 비트레이트를 바꿔도 해상도가 안 따라왔다.
- 예산을 폭×높이 상자가 아니라 **픽셀 면적**으로 잡았다. 테스트가 1024x768(720p보다 픽셀이 적다)을
  960x720으로 줄이는 버그를 잡아줘서 고친 것이다. 16:10 화면도 레터박스 없이 비율을 유지한다.

변경 파일
- `apps/directory/server.js`, `apps/directory/test/directory_test.js`, `apps/directory/README.md`
- `apps/native_poc/src/encode_resolution_ladder.hpp`(신규), `encode_resolution_ladder_test.cpp`(신규),
  `native_video_host_main.cpp`, `CMakeLists.txt`, `product_version.hpp`(0.2.17)
- 산출물 `dist/GNLinkSetup-0.2.17.exe`

검증/build/test
- 디렉토리 스위트 전체 PASS(wake 기본값 2건 신규 포함). 호스트·인스톨러 빌드 PASS.
- 단위 7종 PASS: encode_resolution_ladder(11건 신규), udp_control_channel, connect_candidates,
  bind_port_candidates, secure_input_mapping, secure_input_session, dxgi_output_selection.

다음 액션
- **서버 배포는 사용자 승인 대기** (wake 승격 반영). 배포 후 systemd drop-in에서
  `REMOTE60_NAT_DIAG_WAKE` 줄은 제거해도 된다 — 이제 무시된다.
- 0.2.17 설치 후 실기: 3Mbps에서 게임 메뉴 전환 시 아티팩트가 사라지는지, 720p 체감 화질이
  수용 가능한지. 앱에서 6Mbps로 올리면 1080p로 돌아오는지도 함께.

### 243) 2026-08-09 원격 잠금 해제 (APK 0.2.8)

배경
- 잠금 화면을 폰으로 조준해 비밀번호 칸을 찾아 누르는 것이 번거롭다. 사용자 제안: 잠금 화면을
  볼 필요 없이 앱에서 비밀번호만 입력하면 `Enter → 비밀번호 → Enter` 를 대신 보내달라.

구현 (Android 전용, 호스트·프로토콜 무변경)
- 뷰어 메뉴에 "잠금 해제". 다이얼로그의 비밀번호 칸(마스킹)에 입력하면 그 순서로 전송한다.
- 지연이 필요하다: 첫 Enter 가 걷는 커튼은 애니메이션이라 그 동안 친 글자는 어디에도 안 닿고,
  텍스트와 제출은 별도 메시지라 호스트가 텍스트를 다 전달할 틈이 필요하다. 600ms / 250ms.
- **저장하지 않는다.** 전송 후 즉시 비운다. 진단 로그에도 글자 수만 남기고 내용은 안 남긴다.
- 배선은 전부 이미 있었다: 잠금 화면에서 키·텍스트가 SYSTEM 에이전트로 라우팅되는 경로
  (`native_video_host_main.cpp:3520-3533`, `:3609`)는 0.2.12/0.2.13 UAC 작업의 산물이다.

범위 결정 (사용자 판단)
- "모든 PC에서 범용으로" 라는 요구가 있었고, 엄밀히 하려면 Ctrl+Alt+Del 필수 PC 대응
  (SYSTEM 서비스의 SendSAS + `SoftwareSASGeneration` 정책)과 잠금 상태를 클라까지 전달하는
  작업이 필요하다. **사용자가 "그냥 엔터-입력-엔터로만" 으로 범위를 좁혔다.**
- 따라서 현재 한계를 명시해 둔다:
  - Ctrl+Alt+Del 이 필요한 PC(도메인 정책 등)에서는 동작하지 않는다
  - 잠기지 않은 상태에서 쓰면 **현재 포커스된 창에 그대로 타이핑된다** — 잠금 상태를 클라가
    모르므로 UI 로 막을 수 없다. 뷰어 메뉴 안쪽에 두어 우발적 실행은 낮췄다
  - 실패해도 실패로 보이지 않는다(U5 미구현). 안 풀렸을 때 원인 구분이 안 된다
  - **비밀번호가 평문으로 전송된다** (N4 미구현). 릴레이 경유 시 우리 서버를 평문으로 통과한다

변경 파일
- `MainActivity.kt`, `strings.xml`, `build.gradle.kts`(0.2.7→0.2.8, code 7)
- 산출물 `dist/GNLink-0.2.8.apk`

검증/build/test
- `:app:assembleDebug` PASS. **실기 확인 필요.**

다음 액션
- 실기: 회사 PC 잠근 상태에서 해제되는지. 안 되면 Ctrl+Alt+Del 정책 PC 인지부터 확인
  (`reg query "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" /v DisableCAD`).
- U5 와 N4 는 이 기능의 실용성·안전성에 직접 걸린다. 우선순위 재평가 대상.

### 244) 2026-08-19 다중 모니터 선택 + OSLink 방식 잠금 해제 (호스트 0.2.18 / APK 0.2.9)

#### 다중 모니터 (N12)
- 그동안 데스크톱 모드는 `MONITOR_DEFAULTTOPRIMARY` 로 **주 모니터 고정**이었다
  (`native_video_host_main.cpp:1432`). Android 의 "디바이스" 탭은 계획상 모니터 목록이었지만
  실제로는 데스크톱 모드 버튼 하나였다.
- 프로토콜에 `ControlMonitorListRequest/List/Select` 를 추가했다. 창 목록과 같은 모양이라
  요청/응답 루프가 그대로 재사용된다. Select 의 응답도 List 라 클라가 **실제로 적용된 선택**을
  같은 구조로 받는다.
- 호스트: `enumerate_monitors()` 가 가상 화면 좌표로 열거하고 **주 화면 우선, 그다음 왼쪽에서
  오른쪽** 으로 정렬한다. 그래야 "모니터 2" 가 매번 같은 화면이다. 선택은 렌더 루프가
  적용한다(캡처 아이템의 소유자라서, 창 선택·캡처 모드와 같은 자리).
- 구버전 호스트 보호: 알 수 없는 opcode 는 응답 없이 버려지므로 클라가 영원히 기다린다.
  그래서 호스트가 `kControlWindowListFlagMonitors` 로 지원을 광고하고, 클라는 그 비트를 본
  뒤에만 요청한다(썸네일과 같은 방식).
- 모니터가 1개면 목록을 만들지 않고 기존 "Desktop" 한 줄을 그대로 둔다 — 고를 게 없다.

#### 잠금 해제 UI (U6 개선)
- 사용자가 OSLink 화면을 참고로 제시했다: 잠금 화면 위에 "컴퓨터 잠금해제" 버튼 + 톱니바퀴.
- **저장된 암호가 있으면 버튼 한 번에 바로 해제**, 없으면 입력창. **톱니바퀴는 암호가 있어도
  항상 입력창**(= 암호를 바꾸는 유일한 경로). 저장 체크박스와 삭제 버튼 포함.
- 오버레이는 **호스트가 잠금 상태를 보고할 때만** 나타난다. 이것이 안전장치다 — 잠기지 않은
  PC 에 보내면 암호가 포커스된 창에 그대로 타이핑된다.
- 그 신호는 이미 프로토콜에 있었다(`ControlPongMessage.captureTargetFlags` bit2). 클라가 읽지
  않고 버리고 있던 것을 세션에 저장해 JNI 로 노출했다.
- 암호는 **호스트별로** 저장한다(`unlock_pw_<hostId>`). 폰 하나가 여러 PC 에 붙고, 잠금
  화면에서 틀린 암호는 그 PC 의 로그인 실패로 남는다.

변경 파일
- `poc_protocol.hpp`, `native_video_host_main.cpp`, `native_video_client_shared_core.{hpp,cpp}`,
  `native_video_client_session.{hpp,cpp}`, `native_video_client_tcp_control.{hpp,cpp}`
- `native_bridge.cpp`, `NativeSessionBridge.kt`, `MainActivity.kt`, `SessionPersistence.kt`,
  `activity_main.xml`, `strings.xml`, `build.gradle.kts`(0.2.8→0.2.9), `product_version.hpp`(0.2.18)
- 산출물 `dist/GNLinkSetup-0.2.18.exe`, `dist/GNLink-0.2.9.apk`

검증/build/test
- 호스트·Windows 클라·인스톨러·APK 빌드 PASS. 단위 5종 PASS(shared_core 포함 — 프로토콜에
  메시지를 추가해도 기존 크기 검증이 깨지지 않음을 확인).

알려진 한계 (변함없음)
- Ctrl+Alt+Del 필수 PC 에서는 잠금 해제가 동작하지 않는다. 그 키는 합성이 불가능하다.
- 주입 실패가 실패로 보이지 않는다(U5). 화면을 안 보고 쓰는 기능이라 특히 아프다.
- 암호가 평문으로 전송된다(N4). 릴레이 경유 시 서버를 평문으로 통과한다.
- 암호는 앱 전용 저장소에 평문으로 보관된다. 루팅되지 않은 기기에서 다른 앱은 못 읽지만
  암호화 저장소(EncryptedSharedPreferences)가 더 낫다.

다음 액션
- 실기: 모니터 2대 PC 에서 "모니터 1/2" 가 뜨고 전환되는지. 잠금 화면에서 오버레이가 뜨고
  저장된 암호로 한 번에 해제되는지.

### 245) 2026-08-19 Windows 클라이언트 재작업 착수 — WebView2 타당성 검증

배경
- 현재 `GNLinkViewer.exe` 는 `--host` 로 IP 를 직접 넣는 명령줄 프로그램이고 **디렉토리 코드가
  한 줄도 없다**. 계정 로그인·PC목록·NAT통과·릴레이가 전부 불가능하고 dist 에도 없다.
- 사용자 요구: 데스크톱 화면만(창 목록 불필요), 서버에서 host 목록을 받아 클릭하면 접속,
  후보 레이스+릴레이(**직접 우선**, 릴레이는 과금), GUI 접속화면을 상용 수준으로,
  설치 프로그램 필수, 비트레이트/FPS 설정창, 매크로 UI, 다중 모니터.

UI 방식 결정
- Win32 직접 그리기(3주) / WebView2(2.5주) / 기존 스타일 유지(4~5일) 중 **WebView2 채택**.
- 구조: UI(로그인·목록·설정·매크로)만 HTML/CSS, **영상은 기존 C++ 경로 그대로**
  (UDP→MediaFoundation→D3D11). 영상 경로는 수개월치 튜닝이 들어간 부분이라 건드리지 않는다.
- 1차에서는 영상 위 반투명 오버레이를 하지 않는다. 별도 창으로 띄운다 — D3D11 스왑체인과의
  합성이 까다롭고, 그 위험을 첫 버전에 넣을 이유가 없다.

타당성 검증 (이번 커밋)
- 계획 전체가 이것 하나에 걸려 있어 먼저 확인했다. `tools/webview2_spike.cpp` 로 네 가지를
  순서대로: SDK 링크 → 런타임 존재 → HTML 렌더 → **JS→C++ 브리지**.
- 결과: 전부 통과. 창 캡처 `logs/ui-shots/webview2_spike.png` 에 CSS 로 그린 화면 확인,
  msedgewebview2 프로세스 25개 기동 확인. 런타임은 이 PC 에 151.0.4129.86 설치돼 있었다.
- 정적 로더(`WebView2LoaderStatic.lib`)를 쓰므로 **배포할 DLL 이 늘지 않는다.**

빌드 방식
- SDK 는 NuGet 패키지(45MB)라 저장소에 넣지 않는다. `automation/fetch_webview2.ps1` 로 받고
  `.gitignore` 에 추가. CMake 는 SDK 가 없으면 WebView2 타겟을 조용히 건너뛰므로, 받지 않은
  체크아웃도 나머지는 그대로 빌드된다.
- 버전은 1.0.4129.50 으로 고정. 빌드가 스스로 SDK 를 올리게 두지 않는다.

알려진 위험
- **WebView2 런타임 의존.** Win11·최신 Win10 은 Edge 와 함께 기본 탑재지만 없는 PC 도 있다.
  현재 설치본은 8MB 완전 오프라인인데, 런타임이 없는 PC 에서는 온라인이 필요해진다.
  설치 프로그램 단계에서 처리해야 한다.

변경 파일
- `apps/native_poc/tools/webview2_spike.cpp`(신규), `apps/native_poc/CMakeLists.txt`,
  `automation/fetch_webview2.ps1`(신규), `.gitignore`

검증/build/test
- 스파이크 빌드·실행 PASS, 스크린샷으로 렌더 확인.

다음 액션
- 1단계: 디렉토리 로그인·PC 목록 (C++ HTTP 는 `post_json` 재사용, 세션 토큰 저장)
- 이후: 레이스·릴레이 배선 → WebView2 셸 → 설정창 → 매크로 UI → 설치본 → 실기

### 246) 2026-08-19 Windows 클라이언트 — 디렉토리 로그인·GUI·설정·설치본 (0.2.19)

배경
- `GNLinkViewer.exe` 는 `--host` 로 IP 를 넣는 명령줄 프로그램이었고 디렉토리 코드가 없었다.
  NAT 뒤 PC 에 못 닿고 릴레이도 못 쓰고 dist 에도 없었다.

1) 디렉토리 HTTP 절반 (`directory_session_client.{hpp,cpp}`)
- `DirectoryRendezvous` 는 UDP 소켓만 소유하고 HTTP 는 앱에 맡기는 설계였다. Android 는 Kotlin 이
  그 역할을 했고 Windows 는 아예 없었다. 로그인·목록·연결을 C++ 로 채웠다.
- 요청마다 연결을 닫는다. 사람 속도로 일어나는 호출이라 풀이 이득이 없고, `Connection: close`
  면 본문이 EOF 로 끝나 chunked 파서가 필요 없다.
- 배열 파싱은 중괄호 깊이 스캔. 응답이 작고 양끝을 우리가 만든다. **모르는 kind 를 버리지 않는
  것**이 핵심 — `relay` 는 이 enum 보다 나중에 생겼고, 버리면 직접 경로가 없는 망에서 유일한
  길이 사라진다. 실제 서버 응답으로 테스트했다.

2) 접속 부트스트랩 (`directory_session_bootstrap.{hpp,cpp}`)
- 관측 → connect → 레이스, 이 순서로 한 소켓에서. 순서나 소켓이 틀리면 "연결은 되는데 아무것도
  안 오는" 세션이 된다. 무응답 시 첫 후보로 폴백 — 펀치는 떨구고 hello 는 통과시키는 NAT 가 있다.
- 릴레이는 마지막 후보일 뿐이고 늦게 답하므로 직접 경로가 되면 항상 이긴다(과금 때문에 중요).

3) GUI 셸 (`client_shell_main.cpp`, `ui/shell.html`)
- WebView2. 세션은 `GNLinkViewer.exe` 자식 프로세스로 — **GNLinkHost 가 GNLinkStream 을
  감독하는 것과 같은 구조**. 영상 경로(4270줄, 수개월치 튜닝)를 건드리지 않고, 거기서 죽어도
  UI 가 안 죽는다.
- 자식에게 **비밀번호 대신 세션 토큰**을 넘긴다. 명령줄은 다른 프로세스가 읽을 수 있다.
- 경계를 넘는 것은 전부 type 필드가 있는 JSON, 테스트로 고정. 두 언어가 따로 컴파일되므로
  필드명이 어긋나면 빌드가 아니라 **빈 화면**으로 실패한다.

4) 설정
- 최대 화질·FPS·기본 모니터. 기본값 12Mbps/60fps — 데스크톱은 보통 유선이고 화질이 바이트보다
  가치 있다. **릴레이일 때만 그 숫자가 곧 요금**이라 슬라이더 옆에 그렇게 적었다.
- 다음 세션부터 적용. 자식이 자기 인코더 협상을 소유하므로 실행 중인 세션에 손대면 그 로직이
  두 벌이 된다.
- **BOM 버그를 실제로 밟았다.** 설정 파일을 편집기가 저장하면 첫 줄에 BOM 이 붙고, 그게 URL
  앞에 끼면 페이지가 JSON 파싱에 실패해 **아무 설명 없이 빈 화면**이 된다. 경계를 넘는 문자열에서
  제거하고 테스트로 고정.

5) 설치본
- payload 에 `GNLinkClient.exe`, `GNLinkViewer.exe`, `ui/shell.html` 추가. 8.5MB → 14.4MB.
- 시작 메뉴 항목 둘: "GNLink Host" 와 "GNLink". 한 대가 양쪽 역할을 다 할 수 있어 이름으로
  구분되어야 한다. 제거 시 둘 다 지운다.

변경 파일
- 신규: `directory_session_client.{hpp,cpp}`, `directory_session_bootstrap.{hpp,cpp}`,
  `client_shell_bridge.{hpp,cpp}`, `client_shell_main.cpp`, `ui/shell.html`,
  각 테스트, `tools/directory_login_probe.cpp`
- 수정: `native_video_client_main.cpp`(디렉토리 인자·모니터·fps), `installer_*`, `CMakeLists.txt`,
  `product_version.hpp`(0.2.19)
- 산출물 `dist/GNLinkSetup-0.2.19.exe`

검증/build/test
- 단위 7종 PASS. 로컬 디렉토리 서버(프로덕션 미사용)로 종단 확인: 로그인 → 목록 0건 →
  호스트 등록 → 목록 1건에 온라인 상태까지 일치. 로그인 화면 스크린샷 `logs/ui-shots/`.

남은 것
- 매크로 UI 재작성, 실기 검증(실제 호스트 접속·모니터 전환·매크로).
- WebView2 런타임이 없는 PC 에서의 설치 경험 — 현재는 안내 메시지만 띄운다.

### 247) 2026-08-19 매크로 창을 페이지로 다시 그림 (0.2.20)

배경
- 매크로 엔진(`input_macro.cpp`)과 기능은 이미 완비돼 있었다 — 녹화·일시정지·재생·지우기,
  단계 목록과 편집·삭제, 반복·흔들기, 이름으로 저장/불러오기.
- 문제는 모양뿐이었다. Win32 컨트롤을 `CreateWindowExW` 로 하나씩 만든 499줄이라, 새 접속
  화면 옆에 두면 다른 시대의 프로그램처럼 보인다.

한 일
- 접속 화면과 같은 구조로 교체: UI 는 `ui/macro.html`, 상태 계약은 `macro_shell_bridge`,
  창은 WebView2. **엔진은 한 줄도 안 건드렸다** — 타이밍은 이미 맞고, 손댈 이유가 없다.
- `macro_window_toggle/visible/destroy` 공개 API 를 그대로 유지해 뷰어의 호출부는 무변경.
- 상태는 **달라졌을 때만** 보낸다. 매 틱마다 목록을 다시 그리면 사용자의 스크롤·선택과 싸운다.
- 창을 닫으면 파괴가 아니라 숨김. 녹화 중에 닫아도 살아 있어야 하고, WebView 재생성은 느리다.

이름 검증을 강화했다
- 기존 코드는 위험한 문자를 `_` 로 **치환**했다. 사용자가 짓지 않은 이름으로 저장되고, 나중에
  그 이름을 못 찾는다. 이제 **거절하고 이유를 말한다.**
- 파일이 되는 이름이라 테스트를 여기에 몰았다: 경로 구분자, `..` 로 디렉토리 탈출, 선행 점,
  와일드카드, 콜론, 예약 장치명(CON/NUL, 대소문자 무관), 공백만, 과다 길이.

테스트에서 잡은 것
- `check(cond, detail)` 에서 detail 이 cond 보다 **먼저 평가될 수 있어**(C++ 인자 평가 순서
  미지정) 실패 시 파싱 전 값이 찍혔다. 통과 중인 테스트가 `repeat=1` 을 보여주는데 단언은
  `== 0` 이었다. 파싱을 별도 줄로 빼서 고쳤다.

설치본
- `ui/macro.html` 추가. 7개 파일, 15.5MB.

변경 파일
- 신규 `macro_shell_bridge.{hpp,cpp}`, `macro_shell_bridge_test.cpp`, `ui/macro.html`
- 재작성 `client_macro_window.cpp`(Win32 → WebView2)
- `CMakeLists.txt`(WebView2 감지를 파일 앞으로 — 뷰어와 셸이 모두 쓴다), `installer_*`,
  `product_version.hpp`(0.2.20)
- 산출물 `dist/GNLinkSetup-0.2.20.exe`

검증/build/test
- 단위 11종 PASS. 뷰어·셸·인스톨러 빌드 PASS.
- (참고) `apps/client`·`apps/host` 레거시 타겟은 여전히 빌드 불가 — 계획서 D2, 이번 작업과 무관.

남은 것
- 실기: 실제 호스트 접속, 모니터 전환, 매크로 녹화·재생.
- WebView2 런타임이 없는 PC 의 설치 경험(현재는 안내 메시지만).

### 248) 2026-08-19 클라이언트 로그인이 영원히 도는 이유 — 메시지를 전부 버리고 있었다 (0.2.21)

증상
- 사용자가 클라이언트에서 로그인 → **"로그인 중" 상태로 무한 로딩.** 오류도 안 뜬다.

원인 (내 버그)
- C++ 는 `PostWebMessageAsJson` 으로 보내는데, 그러면 페이지의 `event.data` 는 **이미 파싱된
  객체**다. 그런데 내 페이지는 그걸 다시 `JSON.parse(event.data)` 했다 → 객체가 문자열로
  강제 변환돼 `"[object Object]"` → 예외 → `catch { return; }` 로 **조용히 무시.**
- 즉 C++ 가 페이지로 보내는 **모든 메시지가 버려지고 있었다.** 로그인 결과도, 호스트 목록도,
  오류도. 페이지는 응답을 영원히 기다린다.

먼저 잘못 짚었던 것
- 어제 "복원이 안 된다"(서버 주소·아이디 칸이 빈 채로 뜸)를 **BOM 탓으로 진단**했다. BOM 처리는
  그 자체로 옳지만 원인이 아니었다. 같은 이 버그였다. 증상 하나를 설명하는 그럴듯한 원인을
  찾았다고 멈춘 것이 실수다 — 그때 restore 가 실제로 도착하는지 확인했어야 했다.
- 그래서 놓쳤다: 스파이크에서 검증한 것은 **JS→C++** 방향뿐이었다. 반대 방향은 한 번도 실제로
  확인하지 않았고, 단위 테스트는 JSON 문자열만 검사하니 잡힐 수가 없었다.

수정
- 두 페이지(`shell.html`, `macro.html`) 모두 `readMessage()` 로 **문자열이든 객체든 받는다.**
  어느 API 로 보내든 동작한다.
- **클라이언트 로그 추가** (`%LOCALAPPDATA%\GNLink\client.log`). 창은 한 문장밖에 못 보여주고,
  로그인 실패 시 그 문장은 서버가 한 말이라 "주소가 틀렸다"와 "계정이 틀렸다"를 구분 못 한다.
  시도·결과·세션 시작을 기록한다. **비밀번호와 세션 토큰은 안 남긴다.**

검증
- 로컬 디렉토리로 재확인: 서버 주소·아이디가 **복원된다**(스크린샷
  `logs/ui-shots/client_restore_fixed.png`). 이것이 C++→페이지 경로가 살아있다는 증거다.
- 프로덕션 경로도 별도 확인: `DirectoryLoginProbe` 가 실제 서버까지 닿아 정상 거절을 받는다.

변경 파일
- `ui/shell.html`, `ui/macro.html`, `client_shell_main.cpp`, `product_version.hpp`(0.2.21)
- 산출물 `dist/GNLinkSetup-0.2.21.exe`

### 249) 2026-08-19 세션이 안 열리고 메시지가 깨지던 두 원인 (0.2.22)

증상 (사용자 실기)
- 로그인·PC 목록은 정상 (248 수정 확인됨). PC 를 선택하면 **창이 뜨다 말고** 닫히고,
  오류 문구가 `shotan Desktop ???ū ??????...` 처럼 **깨져서** 읽을 수 없었다.

원인 1 — h264 실험 게이트 (세션이 안 열린 진짜 이유)
- 뷰어는 `--codec h264` 를 **빌드 시 실험 스위치가 꺼져 있으면 거부하고 즉시 종료**한다
  (`native_video_client_main.cpp:2711`, exit code 10).
- 호스트 앱은 **이미 같은 문제를 같은 방식으로 해결해 두었다**: 자식을 띄우기 전에
  `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1` 을 설정한다(`host_app_main.cpp:323`,
  "제품에는 다른 경로가 없다"는 주석과 함께). 클라이언트 셸도 같게 맞췄다.

원인 2 — 한글이 깨진 이유
- MSVC 는 narrow 문자열 리터럴을 **시스템 코드 페이지(CP949)** 로 인코딩한다. 그런데 그 뒤의
  모든 단계 — JSON, 페이지, 로그 — 는 UTF-8 이다. `widen()` 이 CP_UTF8 로 변환하니 깨진다.
- 기존 코드에는 이 문제가 없었다. 한글을 전부 wide(`L"..."`)로만 썼기 때문이다. narrow
  `std::string` 에 한글을 넣은 것은 이번 새 코드가 처음이다.
- 두 타겟에 `/utf-8` 추가. 소스와 실행 문자 집합을 모두 UTF-8 로 맞춘다.

검증
- 클라이언트·뷰어·인스톨러 빌드 PASS.
- **실기 재확인 필요**: 이번엔 실제로 세션 창이 열리는지, 오류 문구가 읽히는지.

변경 파일
- `client_shell_main.cpp`(자식 환경변수), `CMakeLists.txt`(`/utf-8` 두 타겟),
  `product_version.hpp`(0.2.22)
- 산출물 `dist/GNLinkSetup-0.2.22.exe`

### 250) 2026-08-24 호스트 "먹통"의 진짜 원인 좁히기 — 인코더 출력-굶주림 계측 (0.2.50)

배경 (증상 재정의)
- 사용자 정정: **게임이 멈춘 게 아니라 GNLink 호스트가 먹통**이 된다. 같은 PC에서 게임도
  OSLink(타 원격툴)도 정상인데, 우리 PC 클라로 접속했을 때만 갑자기 영상이 멈추고 호스트를
  손으로 죽여야 한다. → OS/GPU/디스플레이/DXGI-가-게임을-못-봄은 전부 배제(그럼 OSLink도 죽음).
  GNLink 호스트의 캡처/인코드/송신 파이프라인 고유 wedge.

원인 (코드로 확정 + 검증용 Codex와 3라운드 교차검증)
- 겹친 구멍 3개: ① 비동기 HW MFT가 `NeedInput`만 이벤트로 주고 `HaveOutput`이 안 보이는 호출에서
  기존 코드는 `sawEvent=true`라 fallback 드레인을 건너뛰어 출력이 MFT에 갇힐 수 있음
  (`mf_h264_codec.cpp` async poll tail). ② 출력이 비면 `units.empty() continue`가 1초 stats/자가복구
  블록을 통째로 skip. ③ 0.2.49 워치독은 `mainLoopProgressUs`(루프 진행)만 봐서, 프레임 0인데 루프가
  도는 상태를 "건강"으로 오판 → 발화 안 함(`mainLoopLastSeq`는 선언만 되고 store가 없는 죽은 변수).
  → 입력만 받고 출력 0이면 영구 정지 + 자동복구 없음 + stats 소실 = 필드 증상과 정합.

이번 커밋 범위 (telemetry-only, 런타임 동작 변경 0 — Codex 리뷰로 계약위반 제거)
- `mf_h264_codec.{hpp,cpp}`: 위험한 "무조건 드레인" 수정안은 **철회**(async MFT 계약상 HaveOutput
  없이 ProcessOutput 호출은 E_UNEXPECTED — Codex R2 blocker). 대신 계측만: `asyncNeedInputOnlyCall`,
  `pendingInputDepth`(finish_call에서 call 종료 시점 stamp), `pendingInputOverflowTotal` 노출.
- `native_video_host_main.cpp`: 인코더 출력-라이브니스 하트비트를 `units.empty()` early-out **앞에**
  배치(굶주려도 관측됨). `encoder-output-starvation` 진단 로그 1줄(1/s) — streak 누적 async 카운터
  (NeedInput/HaveOutput/NoEvent/NotAccepting/NeedMore) + real/synthetic 입력 분리 + pending depth/
  overflow로 A(호스트 이벤트버그) vs B(벤더 고갈) 판별. `encoderNoOutputSinceUs`로 부팅부터
  한 번도 출력 못 낸 경우도 감지. 죽은 `mainLoopLastSeq`를 실제 출력 진행으로 store(부활).
  starvation episode를 인코더 재초기화 3곳 + 스트림 재활성 edge에서 reset(긴 inactive/이전 인코더
  잔여 streak가 false 로그 내는 것 방지). `outAu += processOutputSamples`.
- `product_version.hpp` 0.2.50.

교차검증 (검증용 Codex, a2a 버스)
- 초기 오진("DXGI 40초 wedge + 보안데스크톱")은 로그 재검증에서 4개 근거 모두 철회
  (acquires 재개는 accumTotal=0 포인터전용 / pipeUs 32s는 MFT held-output 타임스탬프 아티팩트 /
  secure-desktop는 다른 PC 로그 / 16:49:28 window-select 재시작 실재).
- 커밋 전 Codex 리뷰 2라운드로 BLOCKER(계약위반 드레인) 제거 + HIGH(startup 감지, outAu 합산,
  pendingDepth 시점) + MED(episode reset, streak 누적, real/synth) 반영 후 production 승인.

검증/build/test
- `cmake --build build-local --config Release --target remote60_native_video_host_poc
  remote60_mf_h264_codec_test` PASS(경고 0). `remote60_mf_h264_codec_test: PASS`.
- `--target remote60_installer` PASS → `GNLinkHost.exe`에 `0.2.50` 임베드 확인.
- 산출물 `dist/GNLinkSetup-0.2.50.exe`.

남은 것 (커밋2)
- 워치독을 출력-라이브니스(accepted-input-coupled)로 발화, 1초 stats/health tick을 공통 loop-tail로
  이동(현재 units.empty가 여전히 skip), encode-fail streak, spec-compliant `MFT_MESSAGE_COMMAND_MARKER`
  기반 gated probe → 인코더 reset → 60s 재발 시 프로세스 재시작.
- **실기**: 0.2.50 설치 후 다음 먹통 재현 시 `encoder-output-starvation` 로그로 A/B 확정.
- naming nuance: `asyncNeedInputOnlyCall`은 marker 도입 시 `sawNeedInput`으로 정확화(Codex 비차단).

### 251) 2026-08-24 로그가 증거를 스스로 지우던 문제 — 번호 로테이션 + 뷰어 로그 신설 (0.2.51)

배경 (사용자 실기 0.2.50 + 새 log/)
- 사용자: "host 로그가 많아지면 초기화된다. .1 식으로 백업하고 10개 넘으면 오래된 것부터
  지워라. 클라도 똑같이." 실제로 이번 host_app.log 도 2MB 캡의 단일 .old 로테이션 때문에
  세션 앞부분이 잘려 있었다 — 긴 재현일수록 증거가 먼저 사라지는 구조.
- 추가 발견: **뷰어(GNLinkViewer)의 stdout 텔레메트리가 통째로 버려지고 있었다.** 셸이
  CREATE_NO_WINDOW 로 띄우며 리다이렉트를 안 해서, 0.2.48에서 넣은 클라 per-frame 로그가
  어디에도 안 남음 — 클라 측 원인(catchup 등)을 확정할 수 없던 이유.

로그 분석 (검증용 Codex 와 a2a 교차검증 2라운드)
- host 는 이번엔 안 멈춤(워치독/starvation 로그 0건).
- 정적 화면 "멈춤" 체감: 진짜 정적이면 DXGI 가 프레임을 안 주고, 포인터 전용 업데이트는
  게이트에서 버려져 원격 커서도 안 움직이며, trailing kick 은 1회성이라 주기 리프레시가 없다.
  15:44 의 stream 0/1 반복은 사용자가 피커를 여닫은 흔적(호스트 정지 아님).
- 장기 실행 끊김: **클라 catchup 양성 피드백 확정** — lag 판정 실패 → 620ms 주기 IDR 요청
  62회/분(코드의 600ms 게이트와 일치), IDR 개당 120~160KB(그 1분에만 19.9MB, 평시 8.3MB),
  forceKeyInputCount=117/30s. 최초 방아쇠(클럭 드리프트 vs 디코드 백로그)는 viewer.log 로 확정
  예정. 평시에도 인코드 파이프가 ~55/60fps(superseded 5/s, cb2eAvg 33.7ms)로 미세 끊김의 바탕.
- "해상도 변경 안 됨": PC 클라에 해상도 UI 자체가 없고, 로그의 abrOverride=1 은 **하드코딩된
  거짓 로그**(실제 0, 실송출 1080p 정상)였다 — Codex 가 정정.

한 일 (P0, 동작 변경 없음 — Codex Blocker/High 반영 후 조건부 승인)
- host/client 로그 **번호 로테이션 .1(최신)~.10(삭제)**. legacy .old 는 shift 후 높은 번호의
  빈 슬롯으로 no-replace 이관(슬롯이 차면 보존) — 유일한 빈 .10 에 넣자마자 지우는 edge 차단.
  크기 비교는 64-bit(ULARGE_INTEGER).
- **viewer.log 신설**: 셸이 파이프로 뷰어 stdout/stderr 를 받아 ms 타임스탬프로 기록(2MB,
  동일 로테이션). CreateProcess 는 STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_HANDLE_LIST 로
  {pipeWrite, NUL stdin}만 화이트리스트 상속(bInheritHandles=TRUE 단독은 모든 상속가능 핸들
  누출 — Blocker). attribute list 는 probe(ERROR_INSUFFICIENT_BUFFER) 검증 + HeapAlloc +
  실패 시 Delete→해제 순서 보장, 실패하면 무파이프 폴백. 다중 세션은 process-wide mutex +
  단일 sink 핸들(FILE_SHARE_DELETE)로 write/rotate 직렬화. EOF 시 개행 없는 tail 도 flush.
- runtime-config-applied 의 하드코딩 "abrOverride=1" → 실제 상태 출력.

검증/build/test
- remote60_host_app / remote60_client_shell / remote60_native_video_host_poc Release 빌드
  PASS(경고 0). 산출물 dist/GNLinkSetup-0.2.51.exe.

남은 것 (Codex 와 합의한 순서)
- P1 클라 catchup 재앵커(aligned_lag_us 고정 base 의 드리프트) + reason1 요청 coalesce(키가
  실제 present 될 때까지 재요청 금지).
- P2 호스트 reason1 IDR 퓨즈(1~2s 1회, key pending 시 drop; barrier/reason2·3 은 예외).
- P3 커서 포워딩(DXGI 전용 오버레이) + 1Hz cached-P 리프레시 — 선행: servedBootstrap(seq=0)
  합성 프레임이 keyint 규칙으로 매번 IDR 이 되는 버그 수정. 6fps 전체 리로드는 대역·인코더
  부담으로 하지 않기로 합의(사용자 요구는 1Hz P + 커서로 충족).
- P4 해상도 프리셋 UI(720p/1080p/자동) + resolution override 를 manualOverride 에서 분리.
- 실기: 0.2.51 설치 후 재현 → viewer.log 의 catchup enter reason 으로 P1 방아쇠 확정.

### 252) 2026-08-24 로테이션 legacy 슬롯 방향 수정 (0.2.52)

- Codex 최종 재리뷰가 251의 잔여 결함 1건을 잡았다: legacy .old 를 **높은 번호(.10)부터** 빈
  슬롯에 넣으면, 로테이션이 "세대는 .1부터 연속"이라는 전제로 매번 .10 을 먼저 지우므로
  legacy 가 바로 다음 로테이션에서 삭제된다(sparse high placement 불가).
- 수정: shift + current→.1 후 legacy 를 **.2부터 오름차순 첫 빈 슬롯**에 no-replace 이동.
  연속 배치라 이후 로테이션마다 한 칸씩 밀리며 정상적으로 늙는다. 슬롯이 다 차면 .old 보존.
  host/client 동일 적용. attribute-list 등 나머지 P0 항목은 Codex 최종 승인.
- 빌드: remote60_host_app / remote60_client_shell Release PASS. 산출물
  dist/GNLinkSetup-0.2.52.exe (0.2.51 은 설치 전이라 폐기).

### 253) 2026-08-24 로그 폴더 바로 열기 (0.2.53)

- 사용자: "host의 Open log가 파일 하나만 열어서 불편하다. 폴더가 뜨게 하고, 클라에도 넣어라."
  번호 로테이션(252) 이후 세대 파일이 여럿이라 파일 단위 열기가 실제로 더 불편해졌다.
- host: Open log 버튼(open_log_file)이 host_app.log 파일 대신 **%LOCALAPPDATA%\GNLink 폴더**를
  연다(파일 생성 보증 코드는 불필요해져 제거 — log_file_path 가 디렉토리를 이미 만든다).
- client 셸: 호스트 목록 하단에 **"로그 폴더"** 버튼 추가(ui/shell.html) → openlog 메시지 →
  C++ 가 같은 폴더(client.log*/viewer.log* 위치)를 ShellExecuteW 로 연다. shellapi.h include.
- 빌드: remote60_host_app / remote60_client_shell Release PASS(경고 0).
  산출물 dist/GNLinkSetup-0.2.53.exe (0.2.52 는 설치 전이라 대체).

### 254) 2026-08-24 리포 내 산출물 대청소 (커밋 대상 없음)

- 사용자 요청: dist 과거 설치본·로그·안 쓰는 폴더 정리. 전부 gitignore 된 로컬 산출물이라
  추적 파일 변경은 없다(이 기록만 커밋).
- 삭제: dist 구버전 58개(256MB — GNLinkSetup 0.2.0~0.2.49, APK 0.2.0~0.2.9 등; 유지:
  0.2.53 현재본·0.2.50 직전 설치본·GNLink-0.2.10.apk), logs/ 81MB(2~8월 진단 잔재,
  72MB runtime diag 포함), android app/.cxx 57MB + app/build 137MB(재생성 가능),
  automation/logs 24MB, build-vcpkg-local(깨진 캐시), log/ 의 분석 완료된 0.2.50 테스트 로그.
- 유지: build-local(활성 빌드트리 86MB), third_party/webview2(빌드 필수 46MB),
  log/nas-survey.sh(NAS 작업 스크립트 — 리포 무관이라 보존 후 사용자 확인 대기),
  .cgcignore/.codegraphcontext.yaml(codegraph 도구 설정 — 사용자 소유).
- 결과: 리포 작업트리 약 650MB → 153MB (.git 43MB 별도).

### 255) 2026-08-24 P0 필드 수정 3종 + UDP 컨트롤 기동 레이스 근절 (0.2.54)

배경 (0.2.53 실기: 지도 멈춤 재현 분석의 후속)
- 확정된 사용자 고통 4가지에 대한 P0: ①피커를 열면 스트림이 꺼져 7~10초 블랙아웃 ②연타 중
  오클릭으로 다른 창 선택 ③정적 화면에서 아무것도 안 보내 얼어 보임 ④원격 커서 미표시.

한 일 (검증용 Codex와 리뷰 3라운드 — Blocker/High 전부 반영 후 필드 승인)
- 피커: 세션 중 열어도 스트림 유지(초기 피커만 정지). 카드 선택은 DOWN/UP 동일 타깃 +
  오픈 후 300ms 디바운스(마우스/터치, 시작 스탬프 포함), pending/포커스·캡처 상실 시 latch
  클리어. 피커 가시 중 catchup 진입·recover-timeout 억제(+종료 후 500ms 유예, present 앵커
  리셋), 비디오 주도 리페인트·overwrite 카운터 게이트. 선택 성공 시에만 [picker] select 로그.
- 1Hz 정적 리프레시(REMOTE60_NATIVE_STATIC_REFRESH_MS, 기본 1000, 0=off): 배리어 열림 +
  sender 큐 빈 상태 + 킥 비활성일 때 캐시 프레임을 P로 재송출. cadence는 emitted-AU와
  attempt 양쪽 클록(async MFT의 units.empty 시 tight-loop 차단). 합성(seq=0) 프레임이 keyint
  나머지 0으로 매번 IDR 되던 버그 수정(!servedBootstrap 게이트).
- 원격 커서 포워딩: DXGI 포인터를 UdpCursorPosPacket(kind=307, 44B, streamGeneration 펜스)으로
  ≤30Hz latest-wins 송신(WGC 제외 — 자체 합성). 클라는 레이어드 오버레이 **파란 링+점**
  마커(고스트 화살표·검정 알파 문제 회피), 500ms 무신호/피커/최소화/세대 불일치 시 숨김,
  선택 시 샘플 리셋, 수신 바운즈 검증. 재시작 시 호스트 포인터 샘플 무효화.
- **UDP 컨트롤 기동 레이스 근절(fix)**: 리더 스레드가 디스패처의 첫 Reset보다 먼저 클라의
  첫 ControlData를 ACK+큐잉하면 Reset이 그것을 지우고, 클라는 ACK를 가져 재전송하지 않아
  serve가 10초 굶고 종료 — **베이스라인 포함 40~70% 확률로 접속 실패하던 기존 버그**
  (Codex가 메커니즘 특정, 반복 실측 MINE 7/10·BASE 4/6 실패로 귀속 확정). 리더 진입 전
  controlReadyEpoch 대기 배리어로 수정.

검증/build/test
- 전 타겟 빌드 클린. capture_cadence_gate PASS. live-host UDP e2e 13체크:
  수정 전 통과율 ~30%(MINE 7/10·BASE 4/6 실패) → **배리어 후 6/6 ALL PASS**.
- 라이브 로그 실증: open-barrier 합성 프레임 key=0(P) ~1Hz, barrier-closed는 킥 IDR 유지.
- 산출물 dist/GNLinkSetup-0.2.54.exe (0.2.53은 직전 설치본으로 보존).

부수 발견
- **RDP 접속 중엔 DXGI 복제가 0x80070005로 거부돼 WGC 폴백**(RDP 종료로 실측 확인) —
  앞선 "다른 PC 3~5fps" 사건의 유력 원인.

테스트 부채 (Codex 합의, 필드 빌드 조건)
- (a) 3연속 units.empty에서 리프레시 ≤1Hz 단위테스트 (b) 세대 불일치 커서 숨김 UI 확인
  (c) 레터박스 매핑 모서리/리사이즈 확인 — (b)(c)는 이번 실기에서 확인.
- 같은-세대 크기변경 시 커서 capW/H 동시 스탬프(비차단, Codex 후속 권고).

### 256) 2026-08-25 CLAUDE.md 신설 — 응답 말미 "수행된 작업" 명시 규칙

- 사용자 지시: 모든 응답 마지막에 그 턴의 수행 작업(파일·커밋·빌드·산출물)을 반드시 명시.
- CLAUDE.md 신설로 세션마다 로드되게 함. 워크플로우는 AGENTS.md 참조로 연결.

### 257) 2026-08-25 P1 — IDR 연발 래치 + keyint A/B 오버라이드, 커서 링 기본 OFF (0.2.55)

배경
- "주기적으로 이전 프레임이 한 번 나온다"(255에서 원인 확정: 매초 120~160KB IDR이 키프레임
  표시의 75%를 2~3프레임 간격으로 지연) + 키 요청 시 IDR 4~5연발(force-key 미소등).

한 일 (Codex 리뷰 2라운드 — High 3건 반영 후 승인)
- force-key 제출 래치: 키 사유 3종(요청·첫프레임·keyint 스케줄) 전부를 단일 latch 뒤로 —
  비동기 MFT가 키 출력을 물고 있는 동안 매 입력을 강제하던 것이 연발의 원인. 스탬프는
  **인코더가 입력을 수락한 뒤**(실패 시 다음 입력 즉시 재강제), 소등은 키가 송신 경로에
  수락될 때, 300ms 타임아웃으로 유실 재시도. 인코더 shutdown+initialize 3경로(resize/fps/
  keyint·bitrate-fallback·stale-output)에서 즉시 클리어(펜딩 입력 폐기 계약), 세션 rollover
  는 의도적으로 제외(펜딩 IDR이 새 barrier를 열 수 있음).
- keyint A/B: REMOTE60_NATIVE_KEYINT_OVERRIDE(0=off, 1..600, 시스템 env)를
  apply_encoder_target 단일 관문에서 강제 — 모든 caller(runtime tune/캡처 UI/ABR·M9)가
  통과하므로 되돌릴 수 없음. ceiling 부기는 클라 요청값 기준 유지(effective는 내부만).
  50fps 레그는 클라 fps 슬라이더로 수행.
- 원격 커서 링: 사용자 실기 판정 "불필요" → 양단 기본 OFF(REMOTE60_NATIVE_REMOTE_CURSOR로만
  재활성, 파서 env_truthy로 통일). 리뷰된 기계장치(세대 펜스 등)는 dormant 보존.

검증/build/test
- 빌드 클린. live-host e2e: 기본=ALL PASS(keyint=60 유지), override=120=ALL PASS(클라 60 요청
  ·비트레이트 튠 2회 관통에도 120 pin). 산출물 dist/GNLinkSetup-0.2.55.exe.
- IDR 연발 소멸의 실증은 필드 로그 게이트(키요청 시나리오에서 "key=1 연속" 부재 확인).

실기 A/B 안내
- 레그A: 기본 설치 그대로. 레그B: 호스트 PC 시스템 환경변수
  REMOTE60_NATIVE_KEYINT_OVERRIDE=120 설정 후 GNLinkHost 재시작. 비교 관점: "주기적 이전
  프레임" 빈도(절반 기대), 평균 대역.

### 258) 2026-08-25 유령 top-left 버튼 제거 + 입력 실패 stage 계측 + 브로커 폴백 (0.2.56)

배경 (사용자 실기 2건)
- "게임 지도 좌상단 '올라 대륙' 클릭=매번 멈춤, 그 오른쪽=매크로 창" → flip-model 영상이 GDI
  버튼을 덮어 **보이지 않는 히트존만** 남은 레거시 Targets(120px)/Macro(90px) 버튼이 게임
  좌상단 UI를 삼킴. 08-24 17:23 "다른 창(GNLink Host) 선택" 사가도 이 경로로 재분류.
- 14:51 프리즈: 영상은 1Hz 리프레시로 매초 정상 표시(P0 실증) — 입력이 안 먹던 것. 로그의
  inject-fail 대상이 CoreWindow라 "UWP가 메시지 거부"로 오인했으나, kind=1 desktop 경로는
  PostMessage를 안 쓰고 **SetCursorPos만** 호출 → 실패 stage는 SetCursorPos(호스트 스레드의
  input-desktop 연결 문제). OSLink로 포그라운드 바꾸니 복구.

한 일 (검증용 Codex 리뷰 — 승인 조건 4건 반영 후 필드 승인)
- 유령 버튼: compute_client_layout 스트림뷰 분기의 toggle/macro rect를 empty로, draw 제거.
  대체로 세션 툴바(top-center dwell)에 "대상 선택" 버튼 복원(onTargets→set_picker_visible
  단일 관문, P0 가드 자동 적용). 콜백 조건부라 미배선 임베딩은 기존 2버튼 유지.
- 입력 실패 stage 계측: InputFailStage{MapPoint/ResolveTarget/SetCursorPos/SendInputMouse/
  SendInputKey/PostMessage} + win32 error. 각 stamped API 앞 SetLastError(ERROR_SUCCESS),
  실패 즉시 캡처(SendInput은 error=0이어도 stage로 식별). window-mode PostMessage 3경로+key도
  실계측. 검증/매핑 실패는 failVal(stage, ERROR_INVALID_PARAMETER)로 stale error 제거.
  카운터 inputFailSetCursorPos/SendInputMouse/SendInputKey/PostMessage, inject-fail 로그에
  stage=/err= 부착.
- SYSTEM 브로커 폴백: direct Failed && actionable stage(SetCursorPos/SendInputMouse/
  SendInputKey) && desktopMode && fresh-default && directory-authenticated → 브로커 1회 재시도
  (에이전트가 SetThreadDesktop 후 SetCursorPos+SendInput). 카운터 정직화 —
  Fallback(시도)/Queued(파이프 쓰기 성공=주입 성공 아님)/PipeFail. 실제 성공은 secure_input.log.
- product_version 0.2.56.

검증/build/test
- host+viewer+installer 빌드 클린. 산출물 dist/GNLinkSetup-0.2.56.exe(0.2.56 임베드 확인).
- 커밋 직전 임베드 검증에서 버전 미범프(0.2.55)를 잡아 재빌드 — 실제 0.2.55를 0.2.56으로
  내보내는 사고 방지.

실기 게이트 (자동화 불가라 필드 확인)
- 좌상단 유령버튼 무발화(피커/매크로 안 열림), 그 좌표 입력은 원격으로 전달.
- 툴바 "대상 선택" → 피커 정상(멈춤/오클릭 없음).
- CoreWindow 재현 시 host 로그 stage=set_cursor_pos → inputDefaultBrokerQueued 증가 →
  secure_input.log inject=ok → 클릭 실동작. Chrome/Notepad 정상 경로는 폴백 0.

부채 (Codex 합의)
- sticky input routing(broker down→up 짝 고정, foreground별 1~2s, hover coalesce): 미포함.
  필드 POC에서 stuck drag/button 한 번이라도 보이면 즉시 blocker 승격.
- SyntheticRefresh flag(정적 리프레시 프레임을 클라 latency/catchup/anomaly 집계에서 제외):
  UDP 재조립(안드로이드 공유 core) 관통이라 별도 커밋 — **P2 readback 착수 전 완료 게이트**.

### 259) 2026-08-25 docs 정리(legacy 이동) + 핸드오프 문서 + gitignore 보정

- 사용자 지시: 세션 전환 예정 → 현재 계획 문서화 + docs 과거 문서 legacy 폴더로 정리.
- docs/HANDOFF.md 신설: 배포 이력(0.2.50~56), 멈춤/끊김 4갈래 원인·상태, 다음 액션(0.2.56 실기
  판정선 4개 → sticky/P2), 부채(sticky routing, SyntheticRefresh=P2 게이트), env 스위치, 검증
  파이프라인, RDP 주의, 코드 지도. README.md read-order에 HANDOFF 추가.
- docs/legacy/ 신설, 날짜 박힌 과거 문서 12종 이동(history_old·구현계획_old·감사/상세계획_0730·
  2026040x 리뷰·홀펀칭/잠금화면 설계·android_구현계획). 현행 7종만 docs/ 루트 유지.
- .gitignore에 /log/, /.cgcignore, /.codegraphcontext.yaml 추가(로컬 도구/로그 — .vscode 방식).
- AGENTS.md의 "repo root 밖 삭제커맨드 금지" 정책은 별도 커밋으로 보존.

### 260) 2026-08-25 호스트 분할 리팩터 계획 수립 (분석 + 문서만, 코드 변경 없음)

- 사용자 요청: HANDOFF "코드 지도"의 분할 분석 재검증 + 파일을 AI가 읽기 편하게 공격적으로 기능 분할/클래스화.
- 실측(`native_video_host_main.cpp` 9,896줄): main() 2892~9896 = 7,005줄, 지역변수 502(atomic 132,
  mutex/cv 70), 람다 62(이름 45 + 스레드 5 + 인라인 12; 최대 serve_control_session 681줄,
  restart_capture_session_impl 228, senderThread 177, apply_selected_window_capture 169). 구간별 main
  변수 참조 수: 통계/ABR 블록 213(최고), encode+send 115, control 91, restart_capture 35, kick/attach 14.
- 기존 분석 판정: 줄수·main 크기는 정확. "stats 추출"은 결합도 1위라 첫 대상이 아니라 마지막이어야 하고,
  main 밖 2,900줄(자유함수 ~100개, 순수 이동 가능)이 누락. 근본 원인은 람다 수가 아니라 `[&]`로
  공유되는 502개 지역변수.
- 문서: `docs/호스트_분할_리팩터_계획.md` 신설 — Phase 0 순수 이동 10파일 / Phase 1 상태 struct 12 /
  Phase 2 클래스 12(+단위테스트) / Phase 3 HostMainLoop::Tick 12단계 / Phase 4 스레드 소유권 재설계.
  `구현계획.md`에 체크리스트 섹션, `HANDOFF.md` 코드 지도, `README.md` layout 갱신.
- 사용자 결정 반영: (1) 이 리팩터는 Codex(a2a) 교차검증 없이 진행. (2) 스레드 소유권 규칙은 대부분
  모놀리스 산물로 판정(atomic *Pending 플래그 12개 = 수제 메일박스, forceKeyNext 규칙 등) → Phase 0~3은
  보존하되 클래스 머리 `// thread:` 블록으로 모으고, Phase 4에서 MainLoopMailbox/스냅샷으로 재설계.
  근본 제약 3개(WGC 콜백 스레드 리소스 재생성 금지, MFT 단일 스레드, 소켓 단일 writer)만 유지.
- 검증: 없음(분석/문서). 빌드/코드 변경 없음.
- 다음 액션: Phase 0-1 `host_input_inject.hpp/.cpp` 분리(862~1520) → 빌드 게이트 → 커밋.

### 261) 2026-08-26 필드 안정화 — 클라 참조체인 오염 + DXGI 워커 웨지 워치독 + readback 로그 rate-limit + GOP 계측

Goal
- 0.2.56 실기 로그(log/, log/2/)에서 확정된 4건을 수정. Codex(a2a) 교차검증 반영.

원인/수정
- FIX-③ 클라 참조체인 오염(텍스트 스크롤 "다 깨짐"): stale-frame drop이 참조프레임을 무음 폐기 →
  후속 P프레임이 garbage 디코드. lastDecodedKeyCaptureUs 앵커 도입 — stale AU가 앵커 이후(라이브
  참조체인)면 첫1회 decoder.reset+waitForKeyFrame+request_keyframe(6)로 다음 IDR 재동기, 앵커보다
  오래되면 기존대로 quiet-drop. Codex 지적으로 presented/latest 분기 폐기, key-anchor 축 채택.
  staleRefRecoveries 누적 stat 추가.
- FIX-① DXGI 워커 웨지(15:05 5.6분 프리즈): 워커가 AcquireNextFrame류 내부에서 무기한 블록,
  monitorSelect→Stop().join()이 죽은 워커 대기→main-loop 워치독 exit43. capture_backend_dxgi에
  세션-소유 하트비트(running/generation/phase/lastProgressUs/HR/…)+run() 전경로 RAII 종료보장.
  호스트 독립 워치독 스레드(500ms, warn3s/kill5s, exit44, join RAII). supervisor는 exit44를 43과
  동일 recovery 클래스로(crashStreak/nv12 제외). WGC는 콜백 방식이라 이번 제외.
- FIX-②-stopgap readback-slow 로그 ~60/s 스팸: warn 래치가 2s-restart else에서 리셋돼 250ms~2s
  진동 시 매 프레임 재발화 → 1s peak 요약 rate-limit(경계마다 peak 리셋). restart 로직 불변.
  P2-deep(readback 60fps)는 리팩터 후.
- FIX-④ keyint120→IDR60 계측: mf_h264_codec에 GOP SetValue HRESULT/GetValue readback 로깅.
  실측 backend=mft_enum_hw requestedGop=120 setHr=S_OK readbackGop=120 → 인코더가 max-GOP 120을
  수용·유지하면서 자체 정책으로 더 잦은 IDR. AVEncMPVGOPSize는 최대 GOP라 규격 위반 아님(우리 버그
  아님). 2s GOP 강제는 vendor-specific 후속.

Files changed
- apps/native_poc/src/native_video_client_main.cpp (FIX-③ + staleRefRecoveries stat)
- libs/capture/src/capture_backend_dxgi.hpp/.cpp (하트비트/phase/generation/SnapshotWorker)
- apps/native_poc/src/native_video_host_main.cpp (독립 워치독 스레드 + exit44 + readback 로그 rate-limit)
- apps/native_poc/src/host_app_main.cpp (exit44 recovery 분류)
- apps/native_poc/src/mf_h264_codec.cpp (GOP 계측)

Validation / build / test
- capture·GNLinkStream·GNLinkHost·GNLinkViewer clean 빌드.
- mf_h264_codec_test PASS, capture_cadence_gate_test PASS.
- live-host udp_control_e2e ALL PASS(13/13, alt포트). DXGI 워치독 라이브 오발동 0(dxgi-watchdog 로그
  0, self-terminated 0), 클라 종료 후 clean idle-detach 확인. FIX-④ 계측값 실측(setHr=S_OK/readback=120).

Next action
- 실기: 텍스트 스크롤 garbage 소멸 + staleRefRecoveries 관찰 / 게임 중 프리즈 시 dxgi-worker-wedge
  exit44 자동복구(수초) 확인 / readback-slow 로그 1줄/초.
- Codex 권장 fault-injection 테스트(전용 child + phase 지연 hook로 warn3/kill5/exit44/generation-reset)는
  stable close 전 test debt로 명시.
- 그 후 호스트 분할 리팩터 Phase 0~3 → FIX-②-deep(P2).

### 262) 2026-08-26 리팩터 착수 전 점검 — 문서·코드·브랜치 교차 확인 + 계획 의존성 정정 + 브랜치 동기화

Goal
- 사용자 지시: 리팩터 작업 전에 문서부터 브랜치까지 전부 확인.

확인 결과
- 브랜치: `refactor/host-split`(2f09ec5)은 `90f97f8`(0.2.57)에서 분기, main(c1483aa, 계획 문서 갱신)을
  포함하지 않음 → 계획 문서가 main/브랜치에서 서로 다르게 편집됨(main: 브랜치 정책+진행 줄, 0-9 `[ ]`;
  브랜치: 0-9 `[x]`, 정책 줄 없음). `fix/field-stabilization-dxgi-wedge-refchain`은 main에 완전 포함(삭제 가능).
  main은 origin/main보다 5커밋 앞(미push). 작업 트리 clean(`.claude/`만 untracked, 미ignore).
- 2f09ec5 코드 diff 검토: 8개 문자열 함수 본문 byte-동일 이동(header-only inline + using 선언), 순수 이동 확인.
  Release/GNLinkStream.exe 타임스탬프 14:19 = 커밋 시각 → "clean 빌드" 주장과 일치.
- fix(261)가 추가한 요소 실존 확인: `kExitDxgiWorkerWedge=44`(host_main:138), `DxgiWatchdogJoiner`(5251),
  `readbackSlowLastLogUs/WindowPeakUs`(5273), `CaptureWorkerPhase`/`SnapshotWorker`(capture_backend_dxgi.hpp),
  host_app_main `kChildDxgiWorkerWatchdogExitCode=44` 동일 recovery 클래스. host_main 9,988줄, main() 2896~.
- 문서 불일치: HANDOFF가 0.2.56 최신·261 미반영·브랜치 정책 없음; history에 2f09ec5 항목 없음(AGENTS 워크플로
  누락); 구현계획 리팩터 섹션 진행 미표시. dist/에 GNLinkSetup-0.2.57.exe(13:25) 존재, product_version 0.2.57 일치.
- **계획 의존성 오류 발견**: `choose_h264_encode_size(const Args&, …)`가 Args를 받아 0-2 host_bgra_scale이
  0-7 host_args(마지막 예정)에 의존. Phase 0 각 모듈 범위를 Args/상수/전역/교차호출로 스캔 — 그 외 의존은
  전부 선행 모듈만 향함(window_enum→string_util, backend_req→env_string_or_empty, input_inject→window_enum,
  net_io→poc_protocol 상수, gUdpPace* 3개는 main loop 8곳 사용).

수정
- 계획 문서: 0-7을 0-7a(struct Args+env 헬퍼, header-only, 0-8 직후)/0-7b(parse_args+env 프리루드, 마지막)로
  분리한 확정 순서, gUdpPace*는 Phase 0에서 inline atomic으로, 워크플로(문서는 main에서만·브랜치는 merge)
  명시. 0-9 `[x]`, §10 진행 1/10.
- HANDOFF: 최신 0.2.57, 배포 이력 261, 실기 판정선 5) 추가(텍스트 스크롤/exit44/readback 로그), P2 선행조건에
  리팩터 Phase 1~3, 코드 지도에 브랜치 정책. 구현계획: Phase 0 진행 1/10.
- 브랜치 동기화: `refactor/host-split`에 main merge(계획 문서는 main 버전 채택) → host 타깃 빌드로 확인.

Validation / build / test
- main 문서 커밋 f9ab14d. 브랜치 `refactor/host-split`: merge 39980c3(auto-merge가 08-26 갱신 블록을 중복시켜 8a211f5에서 main 버전으로 교체) → main과의 차이는 코드 2파일(host_string_util.hpp, host_main)뿐.
- 브랜치에서 `cmake --build build-local --config Release --target remote60_native_video_host_poc` exit 0, warning/error 0, GNLinkStream.exe 14:33 재생성(2f09ec5+merge 상태 clean 빌드 확인).

Next action
- 브랜치에서 Phase 0-8 host_log → 0-7a host_args → 0-2 … 순으로 진행. 커밋마다 main에 history + 체크박스.

### 263) 2026-08-26 리팩터 Phase 0-8 — host_log.hpp 추출 (브랜치 refactor/host-split f79ca38)

- 이동: TimestampPrefixBuf, wake_display_for_remote_session, HostPowerKeepalive →
  `apps/native_poc/src/host_log.hpp`(header-only, `remote60::native_poc`, 파일 머리 5줄 요약 포함).
  host_main에는 include + using 선언 3개와 포인터 주석만 남김. host_main 9,928→9,832줄.
- 순수 이동 검증: git diff의 제거 98줄 vs 헤더 본문 98줄 diff → 차이는 free 함수의 `inline` 1단어뿐.
- 검증: 브랜치에서 `remote60_native_video_host_poc` Release 빌드 exit 0, warning/error 0 (GNLinkStream.exe 14:54).
- 다음 액션: 0-7a `host_args.hpp`(struct Args + parse_u32/env_string_or_empty/env_truthy/env_u32_clamped).

### 264) 2026-08-26 리팩터 Phase 0-7a — host_args.hpp 추출 (브랜치 refactor/host-split e1b1b9e)

- 이동: struct Args, parse_u32, env_string_or_empty, env_truthy, env_u32_clamped →
  `apps/native_poc/src/host_args.hpp`(header-only). parse_args와 main의 env 프리루드는 0-7b까지 host_main 잔류.
  0-2 host_bgra_scale의 `choose_h264_encode_size(const Args&)` 의존 때문에 순서를 앞당김(262 의존성 정정).
  host_main 9,832→9,764줄.
- 순수 이동 검증: 제거 74줄 vs 헤더 본문 74줄 diff → `inline` 4곳만 차이.
- 검증: 브랜치 host 타깃 Release 빌드 exit 0, warning/error 0.
- 다음 액션: 0-2 `host_bgra_scale.hpp/.cpp`(.cpp 신규 TU, CMake 소스 추가).

### 265) 2026-08-26 리팩터 Phase 0-2 — host_bgra_scale.hpp/.cpp 추출 (브랜치 refactor/host-split 9a10e63)

- 이동: clamp_even_dim, fit_size_preserving_aspect, choose_h264_encode_size, choose_abr_720_size,
  estimate_bgra_change_permille, box_halve_bgra, resize_bgra_bilinear, capture_window_thumbnail →
  `host_bgra_scale.hpp`(선언+요약) / `host_bgra_scale.cpp`(정의, 신규 TU, CMake 호스트 타깃 소스 추가).
  PW_RENDERFULLCONTENT 정의도 동반 이동. host_main 9,764→9,469줄.
- 순수 이동 검증: 제거 292줄 vs .cpp 본문 292줄 diff → IDENTICAL.
- 검증: 브랜치 host 타깃 Release 빌드 exit 0, warning/error 0 (CMake 재생성 포함).
- 순서 정정: GpuBgraScaler(0-3)가 D3DReadbackTiming(0-10)을 쓰므로 0-10을 0-3 앞으로.
- 다음 액션: 0-10 `host_bottleneck.hpp` + `host_frame_state.hpp` → 0-3 `host_gpu_scaler.hpp`.

### 266) 2026-08-26 리팩터 Phase 0-10 — host_bottleneck.hpp + host_frame_state.hpp 추출 (브랜치 71bfc83)

- 이동: HostBottleneckStage, D3DReadbackTiming, update/detect_host_bottleneck_stage, encoder_api_path_code →
  `host_bottleneck.hpp`; FrameState → `host_frame_state.hpp` (둘 다 header-only, 소유 스레드 요약 포함).
  host_main 9,469→9,395줄.
- 순수 이동 검증: 제거 80줄 vs 헤더 본문 80줄(diff -w) → `inline` 3곳만 차이.
- 검증: 브랜치 host 타깃 Release 빌드 exit 0, warning/error 0.
- 다음 액션: 0-3 `host_gpu_scaler.hpp`(작성 완료, host_main 제거 대기) → 0-4 `host_window_enum.hpp/.cpp`.

### 267) 2026-08-26 리팩터 Phase 0-3 — host_gpu_scaler.hpp 추출 (브랜치 f235835)

- 이동: struct GpuBgraScaler → `host_gpu_scaler.hpp`(header-only; 멤버가 원래 in-class 정의라 .cpp 분리 없이 순수 이동).
  host_main 9,395→9,204줄. 0-10/0-3의 포인터 주석 3곳이 sed 이어붙임으로 한 줄로 합쳐진 것을 같은 커밋에서 교정(주석만).
- 순수 이동 검증: 제거 182줄 vs 헤더 본문 182줄 → IDENTICAL.
- 검증: 브랜치 host 타깃 Release 빌드 exit 0, warning/error 0.
- 다음 액션: 0-4 `host_window_enum.hpp/.cpp`(파일 작성 완료, host_main 제거+CMake 대기) → 0-6 `host_capture_device`.

### 268) 2026-08-26 리팩터 Phase 0-4 — host_window_enum.hpp/.cpp 추출 (브랜치 49218e4)

- 이동: hwnd_to_id/window_id_to_hwnd, WindowListEntry/MonitorListEntry, should_*_window, window_content_extent,
  enumerate_monitors/enumerate_shareable_windows, find_window_by_id, get_window_process_name/class_name/title,
  describe_input_target, CaptureWindowCriteria/Info, match/find_capture_window*, find_top_level_window_at_point →
  `host_window_enum.hpp`(struct+선언) / `host_window_enum.cpp`(정의, 신규 TU, CMake 추가). host_main 9,204→8,879줄.
- 순수 이동 검증: 제거 325줄을 정렬 multiset으로 .cpp 본문(291)+.hpp와 대조 → 함수 본문 전부 일치, .cpp에 없는 줄은
  헤더로 간 struct 4개·주석·전방선언 2개뿐(전방선언은 헤더가 대체).
- 검증: 브랜치 host 타깃 Release 빌드 exit 0, warning/error 0.
- 다음 액션: 0-6 `host_capture_device.hpp/.cpp`(파일 작성 완료) → 0-5 `host_net_io.hpp/.cpp`(헤더 작성 완료).

### 269) 2026-08-26 리팩터 Phase 0-6 — host_capture_device.hpp/.cpp 추출 (브랜치 0d1f528)

- 이동: backend_request_*/backend_fallback_reason, desktop_capture_backend_from_env/from_code/code/name(fix 261이 추가한
  name 포함), compute_window_client_crop, CreateItemForPrimaryMonitor, SurfaceToTexture, PrimaryMonitorInfo/
  primary_monitor_info, create_d3d11_device_for_primary_monitor → `host_capture_device.hpp/.cpp`(신규 TU, CMake 추가).
  host_main 8,879→8,524줄.
- 순수 이동 검증: 제거 351줄 정렬 대조 → .cpp/.hpp에 없는 줄은 기본인자 시그니처 1줄(헤더로 이동)뿐.
- 검증: 브랜치 host 타깃 Release 빌드 exit 0, warning/error 0.
- 다음 액션: 0-5 `host_net_io.hpp/.cpp`(파일 작성 완료) → 0-1 `host_input_inject.hpp/.cpp` → 0-7b.

### 270) 2026-08-26 리팩터 Phase 0-5 — host_net_io.hpp/.cpp 추출 (브랜치 d522474)

- 이동: WinsockScope, resolve_bind_address, send_all(_timed), SendPathStats, recv_all/recv_discard, kUdpReceiveBufferBytes,
  udp_pace_wait_until/udp_pace_budget_us, UdpSendOutcome, send_udp_chunks(_impl/_timed) → `host_net_io.hpp/.cpp`(신규 TU).
  전역 `gUdpPacePeakBitrateBps/gUdpVideoFecInterleaved/gUdpKeyframePacePeakBitrateBps`는 헤더의 `inline std::atomic`
  (main loop 8곳이 unqualified로 계속 사용; UdpPacer 멤버화는 Phase 2). host_main 8,524→8,266줄.
- 순수 이동 검증: 제거 265줄 정렬 대조 → 차이는 전역 3줄의 `inline`뿐.
- **정정**: d522474 시점의 빌드는 실제로 실패했었다(C2011/C2084 — WinsockScope/send_all/recv_all/recv_discard가
  native_socket.hpp의 공유 정의와 재정의 충돌). `cmake --build … | tail` 파이프가 종료코드를 가려 exit 0으로 오판.
  후속 커밋 eeb703e에서 호스트 사본 4개를 제거(공유본과 byte-동일, 동작 불변)하고 PIPESTATUS로 종료코드 검사 → 빌드
  exit 0, warning/error 0. 이후 모든 빌드 게이트는 `set -o pipefail` + PIPESTATUS로 판정한다.
- 다음 액션: 0-1 `host_input_inject.hpp/.cpp`(헤더 작성 완료) → 0-7b `host_args` 확장(parse_args + env 프리루드).

### 271) 2026-08-26 리팩터 Phase 0-1 — host_input_inject.hpp/.cpp 추출 (브랜치 f13d76d)

- 이동: InputInjectionMode(+parse/name), DesktopInputState, interactive_desktop_is_default(_uncached), InputInjectResult,
  InputFailStage(+name), inject_background_input_event, apply_input_text_message + 파일-private Win32 헬퍼(마우스/키 매핑,
  키보드 상태, 좌표 스케일, 타깃 해석, SendInput 래퍼 — .cpp 익명 네임스페이스) → `host_input_inject.hpp/.cpp`(신규 TU).
  host_main 8,266→7,628줄. 이동 전 공유 헤더와의 이름 충돌 사전 스캔(0-5 재발 방지) → 충돌 없음.
- 순수 이동 검증: 제거 619줄 정렬 대조 → .cpp/.hpp에 없는 줄은 기본인자 시그니처 2줄(헤더로 이동)뿐.
- 검증: 브랜치 host 타깃 Release 빌드 PIPESTATUS exit 0, warning/error 0.
- 다음 액션: 0-7b — parse_args만 `host_args.cpp`(신규 TU)로 이동. main 첫 236줄 env 프리루드는 **Phase 1에서** 상태
  struct(RateControl/FrameGating/…)로 흡수한다(지금 옮기면 main 본문 수백 곳의 이름을 바꿔야 해 Phase 0 "순수 이동" 규칙 위반).

### 272) 2026-08-26 리팩터 Phase 0-7b parse_args 이동 + Phase 0 완료 게이트 통과 (브랜치 3647e41)

- 이동: parse_args → `host_args.cpp`(신규 TU, 163줄 전부 대조 일치), `host_args.hpp`에 선언. env 프리루드(main 첫 236줄)는
  Phase 1로 이월(대량 리네임 회피). host_main 7,628→7,469줄. **Phase 0 11/11 완료**(9,988→7,469줄, 모듈 10개:
  host_log 138 / string_util 100 / args 108+180 / bgra_scale 55+330 / gpu_scaler 235 / window_enum 91+340 /
  capture_device 78+410 / net_io 96+242 / input_inject 100+659 / bottleneck 79 + frame_state 56).
- 게이트: (A) gate-A 4타깃 빌드 PIPESTATUS exit 0 — host / GNLinkViewer / mf_h264_codec_test / capture_cadence_gate_test /
  udp_control_e2e_test (클라 native_video_client_main.cpp:3023 C4715 경고 1건은 리팩터 전부터 있던 것, 클라 미변경).
  (B) mf_h264_codec_test PASS, capture_cadence_gate_test PASS. (C) UDP e2e: 격리 호스트 `GNLinkStream --transport udp
  --codec h264 --bind-port 44100 --bind-address 127.0.0.1 --control-port 44101 --seconds 90`
  (REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1, ENCODER_TUNE_MODE=low_latency) + `remote60_udp_control_e2e_test 127.0.0.1 44100`
  → **13/13 ALL PASS**(connect / control over udp / window list / desktop select / stream start / video flows / bitrate
  down·up / input queue drain / healthy at end). 호스트 로그 정상(mft_enum_hw GOP 60, wire key AU, pacing update).
- 사고 1건: 존재하지 않는 `--help`로 스모크하려다 호스트가 43000에 그대로 기동됨 → taskkill로 종료(잔존 0). parse_args가
  미지 플래그를 무시하는 기존 동작(변경 없음).
- 다음 액션: Phase 1 — 502개 지역변수를 12개 state struct로(기능 기준, `// cross-thread:` 블록). 첫 struct는 결합도 낮은
  RateControlState(abr*/m9* 65개, main 전용) 또는 FrameGatingState(20개)부터.

### 273) 2026-08-26 리팩터 Phase 1-7 — FrameGatingState (브랜치 61fa33f)

- main()의 frameGating* 지역변수 20개(env 설정 6, 파생 간격 1, 참조프레임·스트릭 8, 텔레메트리 5)를 익명 네임스페이스의
  `struct FrameGatingState` 필드로 묶고 `frameGating.<field>`로 기계적 치환(97줄). 설정 필드는 const를 잃지만 재대입 없음.
  로그 라벨(" frameGatingMode=" 등)은 변수명과 겹치지 않음을 치환 전 확인 → 로그 텍스트 불변.
- 게이트: host 빌드 PIPESTATUS exit 0 / UDP e2e 13/13 ALL PASS(격리 호스트 44100).
- 도구: `automation/rename_outside_strings.pl` — 문자열 리터럴 밖에서만 식별자를 치환(다음 1-6 RateControlState는
  " abrProfile=" 같은 라벨이 변수명과 겹쳐 필요). 자가 테스트 통과.
- 다음 액션: 1-6 RateControlState(abr*/m9*/ceiling 65개, main 전용).

### 274) 2026-08-26 리팩터 Phase 1-6 — RateControlState (브랜치 b20f5f4)

- main()의 abr*/m9*/userFpsCeiling/userKeyintCeiling/autoFallback720/encodeLadderReduced 58개를 `struct RateControlState`
  (env 설정 17 / 레더 기하·비트레이트 31 / 상한 2 / 런타임 카운터 8)로 묶고 `rate.<원래이름>`으로 치환(224줄).
  " abrProfile=" 같은 로그 라벨이 변수명과 겹치므로 `automation/rename_outside_strings.pl`(문자열 리터럴 밖 치환)로
  수행 → 파일 내 문자열 리터럴 집합 1,160개 before/after diff = 동일(1-7도 소급 확인). 람다 명시 캡처 없음 확인.
- 게이트: host 빌드 PIPESTATUS exit 0 / UDP e2e 13/13 ALL PASS / 호스트 로그 `abr=on abrMode=default … m9=off m9Mode=dry-run` 그대로.
- 다음 액션: 1-8 KickState(trailing kick + 정적 리프레시 + selectionFirstKeyframe 14개; servedBootstrap은 루프 지역변수라 제외).

### 275) 2026-08-26 리팩터 Phase 1-8 — KickState (브랜치 e653baf)

- trailing kick(pending/dueAt/count/lastSourceAge + lastSeen*/lastReal*/lastEmitted*/lastKickedFor*), 정적 리프레시
  (interval/count/lastAttempt), selectionFirstKeyframe* 14개 → `struct KickState`(`kick.<field>`). servedBootstrap은 루프
  지역변수라 제외. 문자열 리터럴 집합 동일 확인. 인스턴스는 원래 선언 위치(3703)에 두어 스코프 불변.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 호스트 로그 trailing-edge kick 2회 정상.
- 다음 액션: 1-10 ClientMetricsSnapshot(atomic 23개, control 쓰기·main 읽기 — atomic 유지, 그룹핑만).

### 276) 2026-08-26 리팩터 Phase 1-10 — ClientMetricsSnapshot (브랜치 051adba)

- clientMetrics* 19 + clientRequestedKeyFrame/clientKeyFrame* 4 = atomic 23개 → `struct ClientMetricsSnapshot`
  (`clientMetrics.<field>`, atomic 유지 — control 쓰기/main 읽기). 문자열 리터럴 집합 동일. 인스턴스는 원래 첫 선언 위치.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 ALL PASS.
- 다음 액션: 1-4 DesktopBackendState(17개: req* atomic 3 + requested/active + 백오프 + secure gate + 승격 텔레메트리).

### 277) 2026-08-26 리팩터 Phase 1-4 — DesktopBackendState (브랜치 cd5ddd5)

- desktopBackendReq* atomic 3 + requested/activeDesktopBackend + 재시도 백오프 2 + secure-desktop 안정 게이트 4 + 승격
  텔레메트리 atomic 6 = 17개 → `struct DesktopBackendState`(`backend.<field>`). env 초기값(from_env, reqValue)은 원래 위치의
  대입으로 유지, 0 초기화는 struct 기본값. 문자열 리터럴 집합 동일.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 로그 `desktop_backend=dxgi` 그대로.
- 다음 액션: 1-11 WatchdogState(콜백 stall·frozen ring·readback drain·main-loop 워치독 26개; mainLoopProgressUs 초기 스탬프는
  원래 위치 대입으로 유지해 워치독 오발동 방지).

### 278) 2026-08-26 리팩터 Phase 1-11 — WatchdogState (브랜치 4a8b36f)

- GDI 콜백 stall 워치독(env 3 + 스트릭/카운터 3), frozen-ring 자가복구(스트릭·타임스탬프·피크 5), readback-slow 로그
  rate-limit 2, readback drain 소프트 워치독(윈도 델타 8), main-loop 생존 atomic 3 = 24개 → `struct WatchdogState`
  (`watchdog.<field>`). mainLoopPhase/ProgressUs 초기 스탬프는 원래 위치 대입으로 유지(워치독 스레드 첫 age 계산 불변).
  문자열 리터럴 집합은 struct 주석의 "readback slow" 인용 1건 외 동일.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 로그 워치독 발동 0, `captureInputMinPushPerSec=10 …` 라벨 그대로.
- 다음 액션: 1-9 InputRouterState(31개, 인스턴스명 inputRouter — 지역 `input` 메시지 변수와 충돌 회피).

### 279) 2026-08-26 리팩터 Phase 1-9 — InputRouterState (브랜치 a57d2ac)

- 주입 모드/활성 2, SYSTEM 브로커 클라이언트, secure-desktop 카운터 5, 입력 도메인 atomic 2, DesktopInputState, 명시 타깃
  criteria, 원인별 실패 카운터 14, 원격 커서 포워더 상태 4 = 31개 → `struct InputRouterState`(인스턴스 `inputRouter` —
  control 핸들러의 지역 `input` 메시지 변수와 충돌 회피). 브로커 생성자는 defaulted라 멤버화해도 부작용 없음. 리터럴 집합 동일.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 로그 `input injection enabled mode=background_message` 그대로.
- 다음 액션: 1-2 SenderState(42개 + EncodedSendItem을 파일 스코프로; WGC FrameArrived 콜백 매개변수 `sender`→`framePool`
  로 이름만 바꿔 그림자 충돌 제거).

### 280) 2026-08-26 리팩터 Phase 1-2 — SenderState (브랜치 b272ec1)

- 송신 큐/뮤텍스/cv/스레드, peer+waitingForKey 배리어, mediaSessionEpoch, sender→main 복구 신호 atomic, per-epoch IDR
  텔레메트리, reader 스레드의 UDP peer atomic, 통계 구간 sent*/udpTx* 누적 = 42개 → `struct SenderState`(`sender.<field>`).
  `struct EncodedSendItem`은 main 로컬에서 파일 스코프로 verbatim 이동(struct가 타입을 참조해야 함). WGC FrameArrived 콜백
  매개변수 `sender`→`framePool`(3줄, 그림자 제거). 리터럴 집합 동일.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / wire seq 로그 정상.
- 다음 액션: 1-1 SessionState(26개; 인스턴스 `clientSession` — `GraphicsCaptureSession session`과 충돌 회피; SocketCloser
  파일 스코프 이동; 디렉터리 send 콜백의 값 캡처 `[clientSock]`은 `[clientSock = clientSession.clientSock]`로).

### 281) 2026-08-26 리팩터 Phase 1-1 — SessionState (브랜치 c4f0d73)

- 미디어/컨트롤 소켓 + SocketCloser, 디렉터리 에이전트/세션 인증, sessionEpoch/controlReadyEpoch + 대기, streamControlActive,
  control/reader 스레드 핸들 = 26개 → `struct SessionState`(인스턴스 `clientSession` — WGC `GraphicsCaptureSession session`과
  충돌 회피). `struct SocketCloser` 파일 스코프로 verbatim 이동. 디렉터리 send 콜백의 값 캡처 `[clientSock]`은
  `[clientSock = clientSession.clientSock]`(본문 불변).
- 리네이머 결함 발견·수정: `args.directoryUrl`처럼 같은 이름의 멤버 접근까지 바꿔 첫 빌드 실패(C2039) → `.`/`->`/`::` 뒤는
  제외하도록 수정(자가 테스트 통과). 이전 8개 struct에는 같은 오염 없음을 grep으로 확인(`\.(rate|kick|…)\.` 0건).
- 게이트: host 빌드 exit 0 / UDP e2e 13/13.
- 다음 액션: 1-5 EncoderState(61개, Nv12PendingRelease 파일 스코프 이동, H264Encoder 객체는 `encoder.codec`) →
  1-12 HostStats → 1-3 CaptureState는 **두 단계**로: 1-3a 순수 데이터/atomic/카운터(~70), 1-3b RAII·WinRT·D3D 객체
  (d3d/ctx/pool/item/session/dxgiCaptureSession/gdiCaptureProcess/captureReadback/frame)는 소멸 순서가 바뀌므로 Phase 2
  CaptureSession 클래스에서 명시적 수명으로 처리(Phase 1에서는 main 지역 유지).

### 282) 2026-08-26 리팩터 Phase 1-5 — EncoderState (브랜치 355dd36)

- H264Encoder 객체(`encoder.codec`), mfStarted, tuneMode/experimentEnabled, keyReq 토큰버킷 6, runtimeTune atomic 5 + manual
  override, active/nominal/source 인코드 기하 + refit 디바운스 11, active fps/bitrate/keyint/interval 6, force-key 래치 2,
  NV12 surface 부기 5, 출력 생존 heartbeat/starve 15, 통계 구간 카운터 6 = 61개 → `struct EncoderState`(`encoder.<field>`).
  `struct Nv12PendingRelease` 파일 스코프 이동. 선언 41줄 삭제(기본값으로), env/파생 초기값은 원래 위치 대입.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 로그 gop-config·runtime-config-applied 그대로.
- 다음 액션: 1-12 HostStats(50개, 인스턴스 `stats`; 리네이머는 main() 이후 구간에만 적용 — ClientMetricsSnapshot의
  `queueDepthMax` 멤버 선언 보호) → 1-3a CaptureState(순수 데이터).

### 283) 2026-08-26 리팩터 Phase 1-12 — HostStats (브랜치 eadf83f)

- 통계 출력 주기/틱, readback·GPU-scale 단계 타이밍(sum/max) 24, 큐 push/pop/wait 계정 8, 드롭/폴백 카운터 등 = 50개 →
  `struct HostStats`(`stats.<field>`). 리네이머를 main() 이후 구간에만 적용해 ClientMetricsSnapshot의 `queueDepthMax` 멤버
  선언을 보호. 선언 48줄 삭제(기본값). 리터럴 집합 동일.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13.
- 다음 액션: 1-3a CaptureState(순수 데이터 79개, 인스턴스 `capture`; BootstrapFrameCache 파일 스코프 이동) → Phase 1 마무리
  (1-3b RAII 객체는 Phase 2 CaptureSession으로 이월).

### 284) 2026-08-26 리팩터 Phase 1-3a — CaptureState + Phase 1 완료 (브랜치 90759ae)

- 캡처 env 설정 7, 공개 타깃/선택·모드 요청 atomic 18, 창 criteria/info 6, 기하·cadence 9, 백엔드 플래그/폴백 사유 12,
  WGC settle 게이트 9, DXGI 커서 6, publish/pop 타임스탬프 5, bootstrap 캐시 2, idle/reattach 4 = 79개 →
  `struct CaptureState`(`capture.<field>`). `struct BootstrapFrameCache` 파일 스코프 이동. 선언 66줄 삭제.
  **1-3b**(d3d/ctx/pool/item/session/dxgiCaptureSession/gdiCaptureProcess/captureReadback/frame 등 RAII·WinRT·D3D 객체)는
  소멸 순서 보존을 위해 main 지역 유지 → Phase 2-2 CaptureSession 클래스가 명시적 수명으로 흡수.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 로그 desktop_backend=dxgi capture-started 그대로 / mf_h264_codec_test·
  capture_cadence_gate_test PASS.
- **Phase 1 완료**: 상태 struct 12개(FrameGating·RateControl·Kick·ClientMetrics·DesktopBackend·Watchdog·InputRouter·Sender·
  Session·Encoder·HostStats·Capture), main() 공유 지역변수 502→77(struct 인스턴스 12, constexpr 10, 1-3b 객체, 루프 보조).
  host_main 7,795줄(struct 정의 ~600줄 포함). 매 struct 커밋마다 빌드+e2e 13/13 통과, 문자열 리터럴 집합 불변.
- 다음 액션: Phase 2 — struct→클래스, 람다→멤버(2-1 EncodedSender부터). 각 struct가 이미 파일 스코프라 헤더로 옮기는 것부터.

### 285) 2026-08-26 리팩터 Phase 2-0 — state struct 12개를 클래스별 헤더로 (브랜치 cd77e70)

- 파일 스코프 struct 12개(+보조 타입: MainLoopPhase/exit 코드, EncodedSendItem, SocketCloser, Nv12PendingRelease,
  BootstrapFrameCache)를 Phase 2 클래스가 살 헤더로 verbatim 이동: host_frame_gate/abr/kick/client_metrics/backend_policy/
  watchdog/input_router/encoded_sender/session/encoder_manager/stats/capture_session.hpp(총 929줄). 이동 699줄 정렬 대조 동일.
  host_main 7,795→7,110줄. 스크립트 `automation/host_split_phase2_0.sh`(재실행 금지: 이미 이동된 파일에 돌리면 range가
  비어 손상 — 실제로 한 번 발생해 git 커밋본에서 복원 후 1회만 재실행).
- 게이트: host 빌드 exit 0 / UDP e2e 13/13.
- 다음 액션: 2-1 자기 struct만 쓰는 소형 람다 16개를 멤버함수로(Kick arm/cancel, Watchdog enter/mark, Capture fallback
  reason·describe, Encoder starvation reset·refresh intervals, Rate m9_level_*). 이후 2-2 ControlSessionServer(681줄 람다).

### 286) 2026-08-26 리팩터 Phase 2-1 — 소형 람다 15개 → state struct 멤버함수 (브랜치 40b69f5)

- KickState::Arm/Cancel(kTrailingKickDelayUs는 static 멤버), WatchdogState::EnterMainPhase/MarkMainProgress,
  CaptureState::Set/CopyDxgi|GdiFallbackReason·DescribeActiveTarget, EncoderState::ResetStarvationEpisode·
  RefreshFrameIntervals(capture, frameGating), RateControlState::M9LevelBitrate/Fps/W/H. 본문은 람다 본문에서 인스턴스
  접두사만 제거, 호출부 30곳 1:1 치환. host_main 7,110→7,017줄.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13.
- 다음 액션: 2-2 ControlSessionServer — serve_control_session(681줄)을 host_control_session.hpp/.cpp의 클래스로 verbatim 이동
  (의존: args, stop, clientSession/capture/clientMetrics/encoder/inputRouter/backend, WindowSelectionTxn). 1단계 순수 이동,
  2단계에서 메시지별 Handle*로 분할.

### 287) 2026-08-26 리팩터 Phase 2-2 — ControlSessionServer (브랜치 80fd395)

- serve_control_session 람다 681줄 → `ControlSessionServer::Serve(ControlLink&)`(host_control_session.hpp/.cpp, 신규 TU).
  본문 verbatim(diff 동일) — 클래스가 main 지역과 같은 이름의 참조 멤버(args/stop/clientSession/capture/clientMetrics/encoder/
  inputRouter/backend/windowSelectionTxn)를 들어 텍스트 불변. WindowSelectionTxn은 헤더로, FlushControlMessageOnExit는 .cpp로.
  main은 인스턴스 1개 + Serve() 호출 2곳. host_main 7,017→6,318줄.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13(컨트롤 응답·창 목록·runtime tune 모두 새 클래스가 처리).
- 다음 액션: 2-3 EncodedSender — sender 스레드 본체(180줄)·pump_udp_hello를 SenderState 멤버로, begin/await epoch를
  SessionState 멤버로(스크립트 `automation/host_split_phase2_3.sh` 준비됨).

### 288) 2026-08-26 리팩터 Phase 2-3 — SenderState::StartThread/PumpUdpHello, SessionState::BeginEpoch/AwaitControlReady (브랜치 5033c5f)

- start_encoded_sender(180줄, sender 스레드 본체 포함)·pump_udp_hello → `host_encoded_sender.cpp`의 SenderState 멤버(본문
  verbatim, `SenderState& sender = *this;` 별칭으로 텍스트 불변). begin/await epoch → host_session.hpp 인라인 멤버.
  호출부 4곳 치환. 스크립트 `automation/host_split_phase2_3.sh`. host_main 6,318→6,090줄.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13.
- 다음 액션: 2-4 CaptureSession — RAII/WinRT/D3D 지역 16개를 `CaptureResources`로 묶고(원래 선언 순서 유지) 캡처 람다
  10개(create_staging/publish/attach/detach/restart_impl/restore/flush/log_first_sent/kick_try_fill/effective_queue_wait)를
  CaptureState 멤버(res + 의존 struct 매개변수)로. event_token `token`·HRESULT `hr`는 이름 충돌로 main 잔류.

### 289) 2026-08-26 리팩터 Phase 2-4 — CaptureResources + CaptureState 멤버 9개 (브랜치 87c85c2)

- main()의 RAII/WinRT/D3D 지역 14개(d3d/ctx/d3dContextMu/fl/gpuScaler/frame/captureReadback/capturePublishFn/inspectable/
  d3dDevice/pool/session/dxgiCaptureSession/gdiCaptureProcess)를 `CaptureResources`로(원래 선언 순서 유지 → 소멸 순서 불변).
  캡처 item·event_token·hr·DXGI 워커 워치독은 main 잔류(워치독 캡처는 `&dxgiCaptureSession = res.dxgiCaptureSession` init-capture).
- create_staging/publish_captured_texture/attach_frame_arrived/detach_capture_session/restart_capture_session_impl(228줄)/
  flush_capture_pipeline_state/log_first_sent_generation/kick_try_fill/effective_queue_wait_timeout_us → CaptureState 멤버
  (host_capture_session.cpp 신규 TU). 본문 verbatim(교차 호출 치환 후 diff 동일 9/9), 의존 struct는 명시 매개변수.
  restore_previous_target는 apply_selected_window_capture의 지역을 읽는 중첩 람다라 main에 복원. kQueueWaitTimeoutUs*는 헤더로.
- 시행착오: 생성 스크립트의 sed BRE `\(` / perl 파서 줄 훼손으로 2회 실패(본문은 무손상), 시그니처 10줄 수작업 교정,
  DxgiDesktopCaptureConfig using 누락·워치독 람다 내 res. 참조 교정. 스크립트는 수정본으로 커밋.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 로그 capture-started·trailing kick·wire 정상. host_main 6,090→5,518줄.
- 다음 액션: 2-5 EncoderState 멤버(apply_encoder_target/apply_confirmed_capture_geometry/apply_capture_ui_quality_mode) →
  잔여 람다(restart_capture_session, apply_selected_window_capture, reconnect_tcp, pump_cursor_forward, capturePublishFn)는
  Phase 3 Tick 단계 함수와 함께 정리.

### 290) 2026-08-26 리팩터 Phase 2-5 — EncoderState::ApplyTarget 등 4개 멤버 (브랜치 2622cd3)

- apply_encoder_target(75줄)/apply_confirmed_capture_geometry/apply_capture_ui_quality_mode → host_encoder_manager.cpp의
  EncoderState 멤버(본문 verbatim), resetHostTimelineAnchors → 인라인 ResetTimelineAnchors(capture). auTimelineOriginUs는
  EncoderState 필드로, pacing env 4개(noPacingH264/udpPacePeakPercent/udpPacePeakFloorBps/udpKeyframePacePeakBps)는 SenderState
  필드로(인스턴스를 env 프리루드 앞으로 끌어올림 — 기본 생성, 의존 없음). 호출부 1:1 치환. host_main 5,518→5,373줄.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13 / 로그 `h264 pacing=on udpPacePeakPercent=500 …`·pacing update·runtime tune 그대로.
- 남은 main 람다: restart_capture_session(16), apply_selected_window_capture(169), reconnect_tcp_data_session(66),
  pump_cursor_forward(39), capturePublishFn(~105), arg_or_env, classify/authorize_directory_hello, update_u64_max, emit —
  Phase 3에서 Tick 단계 함수와 함께 정리. 다음 액션: 리네이머가 주석 프로즈에 남긴 `res.frame` 등 정리 → Phase 3.

### 291) 2026-08-26 리팩터 — 리네이머가 바꾼 주석 프로즈 복원 (브랜치 c3caf48)

- 주석 안의 일반 단어(frame/session/pool/encoder)가 res.frame/res.session/res.pool/encoder.codec으로 바뀌어 있던 것을 주석
  텍스트에서만 되돌림(162줄, host_main·host_capture_session.cpp·host_encoder_manager.cpp). 코드·문자열 리터럴 불변(파일별
  리터럴 집합 동일 확인). 주석만이라 e2e 생략, host 빌드 exit 0.

### 292) 2026-08-26 리팩터 Phase 3 — main loop 12단계 stage 함수화 (브랜치 6a604c1)

- main()의 while 본문 3,060줄 → host_main_loop.cpp의 stage_time_limit/backend/stream_active/runtime_tune/selection/geometry/
  watchdogs/pace/pop_frame/gate_static/encode_send/stats(호출 순서 = 원래 순서), 루프는 RUN_STAGE 15줄. 본문 verbatim,
  단 (a) 루프 수준 continue/break/return 32곳 → Flow::Continue/Break/Return(스코프 인식 스캐너 loop_exits.pl로 식별, 내부
  for/while/switch/람다 안의 것은 제외), (b) 반복당 지역변수 40개 → TickContext(매 틱 새로 생성, 기본값 = 원래 초기값; 시각/파생
  초기화는 원래 자리의 대입으로), (c) 남은 람다 4개(restart_capture_session/pump_cursor_forward/reconnect_tcp_data_session/
  apply_selected_window_capture) → HostContext&를 받는 free function, update_u64_max → host_stats.hpp inline, 튜닝 constexpr
  전부 → host_main_loop.hpp. HostContext = main이 한 번 조립하는 참조 묶음(startUs 등은 선언만 앞당기고 스탬프는 제자리).
  단계 경계 13곳 중괄호 깊이 1 검증. 시행착오: 호출부 치환이 람다 구간 처리에서 파일 전체에 5회 중복 적용(hx, hx, …),
  헬퍼에 tc 별칭 오삽입, main의 restart 호출 패턴 불일치 → 수정 후 빌드 통과(스크립트도 교정본 커밋).
- 게이트: host 빌드 exit 0 / UDP e2e 13/13(REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC=1로 stats 단계까지 실행: 초당 stats 라인,
  trailing kick 4, wire 7, runtime tune 2, 오류 0). host_main 5,373→2,048줄, host_main_loop.cpp 3,768줄, hpp 219줄.
- 남은 큰 덩어리: host_main_loop.cpp의 stage_encode_send(~1,000줄)·stage_stats(~830줄), main()의 capturePublishFn 람다(~105줄)와
  디렉터리 hello 람다 — 파일당 ≤800줄 목표엔 2차 분할 필요(Phase 3.5 후보). Phase 4(스레드 소유권)는 별도 사이클.
- 다음 액션: gate-A 4타깃 빌드 + 단위테스트 + e2e 재확인 → 사용자 실기 1회 → 버전 범프(0.2.58) 판단.

### 293) 2026-08-26 리팩터 Phase 3.5a — stage별 파일 분할 (브랜치 af8df03)

- host_main_loop.cpp(3,766줄) → host_loop_helpers.cpp + host_stage_{time_limit,backend,stream_active,runtime_tune,selection,
  geometry,watchdogs,pace,pop_frame,gate_static,encode_send,stats}.cpp(13개, verbatim; 함수 3,606줄 정렬 대조 동일).
- 게이트: gate-A 5타깃 빌드 exit 0 + mf_h264_codec_test/capture_cadence_gate_test PASS(Phase 3 트리) → 3.5a host 빌드 exit 0,
  UDP e2e 13/13(1s stats).
- 800줄 초과 잔여: host_stage_encode_send.cpp 1,155 / host_stage_stats.cpp 941 / native_video_host_main.cpp 2,048(main() ~1,650).
  다음 액션: 3.5b — encode_send를 raw/h264 경로로, stats를 출력/ABR·M9 결정으로 2차 분할 검토; main()은 시작·종료 블록 함수화 검토.

### 294) 2026-08-26 리팩터 Phase 3.5b — 800줄 초과 stage 2개 2차 분할 (브랜치 4143a3c)

- stage_encode_send의 `if (useRaw) {…} else {…}` → encode_send_raw(297줄 파일) / encode_send_h264(host_stage_encode_send_h264.cpp
  989줄) + 2줄 디스패처; stage_stats의 1s 틱 H.264 arm → stats_tick_h264(host_stage_stats_h264.cpp 596줄, 틱 const 지역 7개를
  인자로). 세 arm verbatim 검증. else 줄이 원본 들여쓰기 불일치(4칸)로 정규식에 안 잡혀 깊이 기반 검출로 교정 후 성공.
- 게이트: host 빌드 exit 0 / UDP e2e 13/13(1s stats).
- 호스트 측 800줄 초과 잔여: native_video_host_main.cpp 2,048(선형 배선; capturePublishFn 람다 ~105줄, UDP 세션 스레드 블록
  ~190줄, 디렉터리 hello 람다 등), host_stage_encode_send_h264.cpp 989(AU 루프 분리 후보). 클라이언트/코덱 파일은 이 리팩터 범위 밖.

### 295) 2026-08-26 리팩터 Phase 3.6 — PublishFrame 멤버화 + 리팩터 사이클 마무리 (브랜치 9644e84)

- main()의 res.capturePublishFn 람다(103줄) → CaptureState::PublishFrame(verbatim), main은 3-캡처 포워더로 바인딩.
  게이트: host 빌드 exit 0 / UDP e2e 13/13(프레임이 새 멤버를 통해 publish됨). host_main 2,048→1,952줄.
- **사이클 결산(Phase 0~3.6, 2026-08-26 하루)**: native_video_host_main.cpp 9,988→1,952줄(선형 배선), 호스트 모듈 43파일
  (state/클래스 25 + stage 15 + 헬퍼/도구), main() 공유 지역변수 502→~75, 30줄 초과 람다 62→4(UDP 스레드 본체 2·DXGI 워치독·
  콜백 바인더). 호스트 파일 중 800줄 초과는 main(1,952)·encode_send_h264(989)뿐. 코드 커밋 ~40개 전부 host 빌드 + UDP e2e
  13/13, 문자열 리터럴 집합 불변(로그 호환), 이동 본문 diff 대조. gate-A 5타깃 빌드·단위테스트 2종은 Phase 3 트리에서 PASS
  (최종 트리 재확인은 아래).
- 남은 것(문서 §0 지표 갱신): main() ≤300은 HostRuntime 조립(2-12) 없이는 불가 — 선형 배선 1,550줄로 남김; 단위테스트(2-5
  AbrController 등)는 순수 클래스 분리가 전제라 미착수; Phase 4 스레드 소유권 재설계는 별도 사이클.
- 다음 액션: 사용자 실기 1회(브랜치 빌드 GNLinkStream/GNLinkHost) → 이상 없으면 버전 0.2.58 범프 + main merge → Phase 4.

### 296) 2026-08-26 리팩터 Phase 2-12 — main() 시작 블록·종료를 host_startup_*.cpp / host_shutdown.cpp로 (브랜치 405802b)

- main()의 선형 시작 구간(env 설정·transport·소켓/Hello 핸드셰이크·컨트롤 스레드·WinRT/MF/D3D·캡처 타깃·인코드 지오메트리·
  인코더 init·DXGI 워커/메인루프 워치독·리드백 파이프라인·캡처 시작)과 종료 시퀀스를 함수 17개로 verbatim 이동
  (host_startup.hpp가 호출 순서, host_startup_config/connect/control/graphics/capture.cpp 372~427줄, host_shutdown.cpp 158줄).
  실패할 수 있는 단계는 예전 exit code를 그대로 반환. main()은 선언(모놀리스 순서 = 파괴 순서 보존) + HostContext 조립
  (시작 전으로 이동; HostContext::transport를 참조로 바꿔 resolve_transport()가 채움) + WinsockScope 검사 + ControlSessionServer
  + DXGI 워치독 스레드/조이너(DxgiWatchdogJoiner는 헤더로) + 15줄 루프 + shutdown_host: 1,552→123줄.
- classify_directory_hello/authorize_directory_session 람다 → SessionState::ClassifyDirectoryHello/AuthorizeDirectorySession
  (enum DirectoryHello는 host_session.hpp), 호출부 3곳 리네임.
- 검증: main() 원본 줄 멀티셋 = 새 main + 이동 본문 + 멤버 본문(차이는 스캐폴딩/리네임/람다 래퍼 줄뿐 — 이 검사가
  init_graphics 범위에 `CaptureResources res;` 선언이 섞인 실수를 잡아 수정), 본문 17개 IDENTICAL, 문자열 리터럴 집합 불변,
  host 빌드, UDP e2e 13/13, 시작 로그 마커(bind/pacing/backend/encoder/control) 동일. 빌드 1회 실패: 헤더의
  DxgiWatchdogJoiner 닫는 `};` 누락(원본은 `} dxgiWatchdogJoiner{...};` 한 줄이 겸함) → 수정 후 통과.
- 스크립트: automation/host_split_phase2_12.sh(일회성).

### 297) 2026-08-26 리팩터 Phase 2-12b — main.cpp 프리앰블 프루닝 (브랜치 6f39ab9)

- 익명 네임스페이스의 using 226개 중 main()이 실제로 쓰는 41개만 남기고, `using namespace winrt` 2줄·json_profile 별칭·
  REMOTE60_NATIVE_ENCODED_EXPERIMENT 매크로(이제 host_startup_config.cpp만 읽음)·"moved to" 잔존 주석 ~65줄 제거.
  main() 바이트 동일. main.cpp 540→265줄. 게이트: host 빌드 + UDP e2e 13/13. 스크립트: automation/host_split_phase2_12b.sh.
- 계획 §0 지표 갱신: main() ≤300 달성(123), 파일 50개 + main, 800줄 초과는 encode_send_h264(989)만 → 다음 2-13.

### 298) 2026-08-26 리팩터 Phase 2-13 — encode_send_h264 AU 루프 본문 분리 (브랜치 7870014)

- 800줄 초과로 남아 있던 마지막 호스트 모듈 host_stage_encode_send_h264.cpp(989)의 `for (au : units)` 루프 본문(491줄)을
  encode_send_h264_emit_au()(host_stage_encode_send_h264_au.cpp)로 verbatim 이동. 루프 자체의 continue/break 10곳(루프 수준만,
  return 없음 — loop_exits.pl 확인)은 `return AuFlow::Continue/Break`, 본문은 4칸 디인덴트. 본문이 읽는 함수 지역변수 14개는
  H264AuBatch(참조 묶음, HostContext와 같은 패턴)로 전달, constexpr kSenderQueueMaxFrames는 새 헤더 host_stage_encode_send_h264.hpp로.
  989 → 506 + 636 + 52. 검증: 본문 diff = 탈출 10줄만, 부모는 루프 밖 불변(+include, −constexpr), 리터럴 집합 불변, host 빌드,
  UDP e2e 13/13(udpTxFrames/키 AU/트레일링 킥 카운터 진행). 도구: automation/host_split_phase2_13.sh, decls_at_depth1.pl.
- 참고: main ↔ 브랜치 체크아웃 왕복 뒤 작업 트리의 추적 파일이 CRLF로 다시 써짐(core.autocrlf=true). 스크립트는 tr -d '\r'로
  정규화한 사본을 기준으로 검증, 커밋 시 git이 LF로 정규화하므로 저장소 내용엔 영향 없음.

### 299) 2026-08-26 리팩터 Phase 2-T1 — ABR/M9 결정 로직 멤버화 + 첫 단위테스트 (브랜치 3fde905)

- stats_tick_h264의 1초 결정 블록 2개 → RateControlState::DecideAbrProfile(const AbrInputs&, t)/CommitAbrProfile,
  DecideM9Level(const M9Inputs&, t)/CommitM9Level(host_abr.hpp, 헤더 전용). 본문은 원문 그대로이되 상태 밖에서 읽던 값
  (이번 초 클라 메트릭, 호스트 cb2e 폴백 근거, sender.sentFrames, frameGating.staticMode, encoder.activeFps, startUs)만 `in.` 필드로
  — 바뀐 줄 29+15개 전부 리네임(diff로 확인). 스테이지는 같은 지역값으로 입력을 채우고 적용/로그(ApplyTarget, forceKeyNext)는
  그대로. 계획 §5 2-5(AbrController)는 별도 클래스 대신 멤버화로 충족.
- host_abr_test.cpp(remote60_host_abr_test): 시간을 인자로 세션을 초 단위로 구동 — ABR 9(워밍업 홀드, 희소/정지 홀드·스트릭 리셋,
  high→mid 2/3초, 쿨다운 후 mid→low, 비상→low, 호스트 폴백 근거, mid→high 8/12초·스트릭 리셋, low→mid/저프로파일 없음, 강등 대상 없음)
  + M9 5(연속 초 요구·쿨다운·0~3 한계, 스트릭 리셋, 회복, 스테일 메트릭 폴백/무회복, 축별 단독 강등) = 14 PASS.
- 게이트: 리터럴 집합 불변, host + 테스트 빌드, 단위테스트 PASS, UDP e2e 13/13. 스크립트: automation/host_split_phase2_t1.sh.
- 다음: T2 frame gating(FrameGatingState 전이/스킵), T3 kick(KickState 데드라인/1회 킥 가드/정적 리프레시), T4 backend 백오프/승격 게이트.

### 300) 2026-08-26 리팩터 Phase 2-T2 — frame gating 결정 멤버화 + host_frame_gate_test (브랜치 c7dcd14)

- stage_gate_static의 결정 줄 → FrameGatingState::RecordChange(changePermille)/RecordReferenceMiss()/UpdateMode()→changed/
  ShouldSkip(queuePopUs, keyReqPending, activeFrameIntervalUs, paceByTick) const(host_frame_gate.hpp, 헤더 전용), 본문 verbatim
  (리네임은 encoder.activeFrameIntervalUs→파라미터 하나). 스테이지는 픽셀 변화 추정·모드 전환 로그·스킵 집계·Flow::Continue를 같은
  순서로 유지. 179→152줄. 테스트 8 시나리오(enterFrames 진입, 첫 변화 프레임 이탈, 스트릭 상호 리셋, 참조 미스=전체 변화, 60000 캡,
  정적 모드 스로틀(간격·키요청·모션·미전송), paced 모션 모드 무스로틀, unpaced 경로 프레임 간격) PASS. 게이트: 리터럴 불변, 빌드,
  e2e 13/13. 스크립트: automation/host_split_phase2_t2.sh.
- 교훈(도구): `set -euo pipefail` 아래 `diff a b | grep` 은 파일이 다르면 diff의 1이 파이프라인 실패가 되어 스크립트를 중단시킴 —
  T1의 정체불명 종료가 이것이었고, `(diff … || true) |` 로 정정(T1~T4 스크립트). 또 체크아웃 왕복 뒤 작업 트리가 CRLF라 `$` 앵커 편집이
  빗나감 → 편집은 `\r?$`, 검증은 `tr -d '\r'` 사본 기준.

### 301) 2026-08-26 리팩터 Phase 2-T3 — trailing kick / static refresh 결정 멤버화 + host_kick_test (브랜치 cb01e22)

- stage_pop_frame의 킥 조건 → KickState::Due(nowUs)/NeedKick(barrierClosed)/MarkKickedForCurrentInput()/StaticRefreshDue(nowUs)
  (host_kick.hpp), 본문·주석 verbatim. 스테이지는 링/배리어/송신 큐 읽기·KickTryFill·집계를 같은 순서로, 리프레시 조건도 같은 항을
  같은 순서로 평가. 339→329줄. 테스트 4 시나리오(arm/150ms 데드라인/re-arm/cancel/raw 무동작, 보류 입력당 1회 킥·새 입력 재킥·AU 배출 후
  정지, 닫힌 배리어의 1회 가드 무시, 마지막 AU와 마지막 시도 양쪽에 앵커된 리프레시 주기·킥 대기 중 차단) PASS. 게이트: 리터럴 불변,
  빌드, e2e 13/13(trailingKickCount/staticRefreshCount 진행). 스크립트: automation/host_split_phase2_t3.sh.

### 302) 2026-08-26 리팩터 Phase 2-T4 — desktop backend 승격 게이트 멤버화 + host_backend_policy_test (브랜치 89211b6)

- stage_backend의 강등/승격 블록 중 순수 상태 갱신 → DesktopBackendState 멤버 12개(NoteDemotionEpisode, DefaultProbeDue,
  NoteDefaultProbe(nowUs, isDefault), DefaultStable, RetryDue, NoteSecureAtDeadline, NoteDeferredForSecure, NotePromotionAttempt,
  NotePromotionSuccess, NotePromotionFailure, ConsumeStabilityEvidence, ResetPromotionGate; host_backend_policy.hpp), 본문 verbatim.
  스테이지는 OpenInputDesktop 프로브 2회·restart_capture_session·캡처/인코더 리셋·로그를 같은 순서로 유지. kDesktopBackendRetry*/
  kDesktopDefault* 상수 4개는 host_main_loop.hpp → host_backend_policy.hpp(전자가 후자를 include). 315→286줄.
- 테스트 5 시나리오(프로브 주기·1s 안정 클록·secure 프로브/데드라인 최종 확인의 리셋, 첫 발견 시 데드라인 무장과 3→6→12→24→30s 백오프
  상한, 성공 시 에피소드 리셋·승격 대기 기록, 데드라인당 1회 유예 래치·시도 시 해제, 시도의 근거 소비, 유휴 리셋) PASS. 테스트 타깃은
  헤더 체인(host_capture_device.hpp→capture_backend_dxgi.hpp) 때문에 `capture` 라이브러리를 링크; 상태가 atomic을 가져 값 반환 불가 →
  in-place 초기화. 게이트: 리터럴 불변, 빌드, e2e 13/13. 스크립트: automation/host_split_phase2_t4.sh.

### 303) 2026-08-26 리팩터 사이클 2차 결산 — 계획서의 남은 항목 전부 완료, gate-A 통과

- 이번 세션 추가분: 2-12 HostRuntime 조립(main() 1,552→123줄), 2-12b 프리앰블 프루닝(main.cpp 265줄), 2-13 encode_send_h264 AU 루프
  분리(989→506+636+52), T1~T4 순수 결정 로직 멤버화 + 단위테스트 4종 31 시나리오. 계획서 §0 지표: 최대 파일 ≤800 달성(최대
  host_stage_encode_send_h264_au 636), main() ≤300 달성(123), 모듈 60개 + main + 테스트 4. 남은 것은 Phase 4(스레드 소유권 재설계,
  동작 변경 허용)뿐 — 사용자 실기 확인 → 0.2.58 범프 → main merge 뒤 별도 사이클.
- 최종 gate-A(브랜치 89211b6): host/client/e2e + 단위테스트 6종 빌드 exit 0, 단위테스트 6/6 PASS(mf_h264_codec, capture_cadence_gate,
  host_abr, host_frame_gate, host_kick, host_backend_policy), 마지막 UDP e2e 13/13. 설치본(0.2.57)·dist 변경 없음, push 없음.

### 304) 2026-08-27 리팩터 실기 확인용 설치본 0.2.58 (브랜치 9d37c9e)

- 사용자 요청: 리팩터 결과를 실기로 확인할 설치본이 없음 → 선택 A(0.2.58 설치본 생성). 브랜치에서 product_version 0.2.57→0.2.58,
  `remote60_installer` 빌드(payload 6종 재스테이징: GNLinkHost/GNLinkStream 재빌드), HANDOFF 절차대로 임베드 검증 —
  GNLinkSetup.exe에 L"0.2.58" ×3 / L"0.2.57" ×0, GNLinkHost.exe ×1/×0(GNLinkStream은 버전 문자열 없음). 재빌드 호스트 UDP e2e 13/13.
  `dist/GNLinkSetup-0.2.58.exe`(3,168,768B) 복사, 0.2.55~0.2.57 보존(0.2.57 = 리팩터 전 코드, 되돌리기용).
- 상태: **실기 확인 대기**. 통과 시 `refactor/host-split` → main 병합; 문제 시 수정 후 0.2.59로 재배포.

### 305) 2026-08-27 리팩터 실기 통과 → main 병합, 리팩터 전 복귀점 고정 (d152627)

- 사용자 실기(0.2.58 설치본) "동작 잘 되는 것 같다" → 병합 승인, 단 리팩터 전 버전을 언제든 돌릴 수 있게 남길 것.
- 복귀점: 병합 직전 main(리팩터 전 호스트 코드 0.2.57 + 문서 전부)에 태그 `v0.2.57-pre-host-split` + 브랜치
  `keep/pre-host-split-0.2.57`. 코드 확인: main의 apps/libs가 리팩터 분기점 c1483aa와 diff 0. 런타임 복귀는 `dist/GNLinkSetup-0.2.57.exe`.
- 병합: `git merge --ff-only refactor/host-split`(main의 커밋이 전부 브랜치에 있어 fast-forward, 충돌 없음) → main = d152627,
  main과 브랜치 diff 0, native_video_host_main.cpp 265줄, product_version 0.2.58. push 없음. `refactor/host-split` 브랜치는 남겨 둠.
- 되돌리는 법: 코드 `git checkout v0.2.57-pre-host-split`(또는 keep/ 브랜치), 설치본 0.2.57 재설치. 다음: Phase 4.

### 306) 2026-08-27 클라이언트 뷰어 분할 리팩터 계획 수립 (분석 + 문서만, 코드 변경 없음)

- Goal: 호스트 분할(260~305)과 같은 방식으로 `native_video_client_main.cpp`(GNLinkViewer) 분할을 설계한다.
- 실측(e346ff7): 5,349줄/255KB, main() 2,158줄(3191~5349), 파일 스코프 전역 88개(atomic 58, mutex 4, 동적 초기화 19) +
  함수 내 static 은닉 상태 9, recvThread 지역 ~75개, `process_h264_frame` 람다 666줄, WndProc 833줄(WM_PAINT 305),
  1s 통계 블록 5회 복제(3 동일 + 2 변형). 구간별 참조 전역 수: WndProc 36 / controlThread 29 / main 프리루드 26 /
  피커·선택 글루 24 / draw 17 / process_h264_frame 15(+recv 지역 60) / Nv12D3dRenderer 0.
- 기존 분석 판정: "후순위" 근거였던 shared_core/tcp_control/toolbar/macro_window 분리는 프로토콜 상태모델·별창뿐이고
  디코드 파이프라인·혼잡 상태기계·선택 게이트·present·WndProc·startup은 전부 main.cpp에 남아 있음. 호스트와 다른 점:
  (1) 전역 기반이라 함수를 다른 TU로 옮기기 전에 extern 헤더(0-0 `viewer_globals`)가 선행돼야 함, (2) 호스트 e2e
  (`remote60_udp_control_e2e_test`)는 `ClientSessionController`를 써 뷰어 코드를 전혀 거치지 않으므로 뷰어 exe를 직접
  띄우는 e2e 3종(stream/picker/tcp-raw)을 게이트로 신설. 중복: `native_socket.hpp`(WinsockScope/recv_all/recv_discard),
  `host_args.hpp`/`host_string_util.hpp`(env/string 6), `host_capture_device`(backend_request_*)와 byte-동일 사본;
  `ClientSessionController`(906줄)는 같은 프로토콜 루프의 이중 구현(뷰어 미링크) → Phase 4 후보.
- 문서: `docs/클라이언트_뷰어_분할_리팩터_계획.md` 신설 — Phase 0 이동 16(0-0 globals extern → 소켓 중복 제거 → env/log/
  args/decoder_backend/gdi/renderer/layout/input/picker/overlay/cursor/window_proc → dead code → 호스트 공유 통합) /
  Phase 1 state struct 13 / Phase 2 클래스 12 + 단위테스트 4(FrameGate/SelectionGate/PickerGesture/Layout — 순수 결정은
  시간을 인자로) / Phase 3 ViewerContext 조립(main ≤150줄, WndProc는 GWLP_USERDATA) / Phase 4 스레드 소유권 재설계
  (+PresentScheduler=P3 paced playout 훅, SyntheticRefresh flag). `구현계획.md` 체크리스트 섹션, `HANDOFF.md` 코드 지도,
  `README.md` 목록 갱신.
- 정책(호스트 승계, 계획 §11에 기본값 명시): Codex 교차검증 없음, 브랜치 `refactor/viewer-split` + 문서는 main,
  접두사 `viewer_` / namespace `remote60::native_poc::viewer`(셸 `client_shell_*`·호스트 `host_args.hpp` 심볼 충돌 회피),
  복귀 태그 `v0.2.58-pre-viewer-split`. 게이트 8종(빌드/이동 동일성/뷰어 e2e/단위테스트/릴리스 금지/로그 호환/정적
  초기화/CLI·셸 계약).
- 검증: 없음(분석/문서). 빌드/코드 변경 없음.
- 다음 액션: 사전 단계(태그/브랜치, `automation/viewer_split_e2e.sh` 기준선 C-1/2/3) → Phase 0-0 `viewer_globals.hpp/.cpp`.

### 307) 2026-08-27 뷰어 리팩터 발견사항 원장 신설 — 기록만, 수정은 리팩터 후 (문서만, 코드 변경 없음)

- 사용자 결정: 뷰어 분할 리팩터는 계획대로 진행하되, 진행 중 코드를 읽다가 **잘못 구현된 곳·고도화가 필요한 곳을 발견하면
  고치지 않고 별도 문서에 기록**, 리팩터 완료 후 그 문서에서 골라 작업한다.
- 문서: `docs/뷰어_리팩터_발견사항.md` 신설 — 규칙 4개(발견 즉시 기록·history에 "발견 → F-NN"·완료 후 별도 커밋·구조적 해소
  항목은 Phase 표시) + 분류 6종 + 발견 목록 표. 분석(e346ff7) 시점에 grep으로 확인한 시드 15건: F-01 디렉터리 경로에서
  handshake/connected 로그가 `args.host/port`(resolvedArgs 아님) · F-02 raw 경로 1s 통계 분모 recvFrames vs h264 decodedFrames ·
  F-03 write-only 전역(gHostCaptureTarget* 7 + mu, gCaptureOverviewMode, gOverlayConfig, gOverlayMetrics push만) · F-04 미사용
  함수 4 + 상수 5(리터럴 중복; 계획 0-14 이관) · F-05 WM_LBUTTONUP 스트림 뷰 도달 불가 분기 · F-06 gControlConnected 기동 순서
  (이론상) · F-07 control 스레드의 layout 계산/InvalidateRect · F-08 통계 블록 5회 복제(Phase 2-1 해소) · F-09 ClientSessionController
  이중 구현 · F-10 SyntheticRefresh(HANDOFF 부채) · F-11 paced playout(P3) · F-12 decoder backend env 매 init getenv+들여쓰기 ·
  F-13 recv 스레드 --seconds 검사 위치 · F-14 함수 static 9개 리셋 없음 · F-15 atomics 군집(§7.1 b).
- 계획서 갱신: 머리 정책 줄, §2 원칙 13(발견은 기록, 수정은 나중에), 0-14 dead code 삭제를 원장 F-04로 이관(Phase 0 15커밋),
  §11 결정표, §12 기록 절차 신설. `구현계획.md` 체크리스트 1줄 추가·Phase 0 줄 수정, `HANDOFF.md` 코드 지도, `README.md` 목록.
- 검증: 없음(문서). 빌드/코드 변경 없음.
- 다음 액션: 변동 없음 — 사전 단계(태그/브랜치, viewer_split_e2e.sh 기준선) → Phase 0-0.

### 308) 2026-08-27 뷰어 분할 리팩터 Phase 0 완료 — 파일 스코프 이동 15단계 (브랜치 refactor/viewer-split, 43553e6 → 36fe396)

- 사전: 복귀 태그 `v0.2.58-pre-viewer-split`, 브랜치 `refactor/viewer-split`(문서는 main, 워크트리 build-local/_main_wt로 갱신 후
  branch가 merge main). 게이트 C 신설 `automation/viewer_split_e2e.sh`(뷰어 exe 직접 구동: C-1 stream 10s / C-2 picker 6s /
  C-3 tcp-raw 5s, 격리 포트 44100/44101) 기준선 ALL PASS. 도구: `viewer_split_move.pl`(정규식 앵커로 최상위 블록을 verbatim
  이동, 선행 주석 포함, 선언/정의 분리·기본인자 처리, HEAD 기준 줄 범위 출력), `viewer_split_check.pl`(게이트 B: 이동 전 리비전의
  줄 범위가 새 파일에 연속·byte-동일로 존재), `viewer_split_gate.sh`(빌드 exit code 전파 + e2e), `host_udp_e2e.sh`.
- 커밋: 0-0 43553e6 globals 88 + 상태 타입 6 + 상수 → viewer_globals.hpp(extern, 군집별 `// thread:`)/.cpp + viewer_common.hpp,
  anonymous namespace → `remote60::native_poc::viewer` · 0-1 a6a074c+ae5dee5 소켓 사본 삭제 → native_socket.hpp(첫 커밋이 `\b::`
  치환 누락으로 빌드 깨진 채 커밋됨 → 후속 fix; 이후 게이트 스크립트로 exit code 전파) · 0-2 1509dc8 viewer_env_util.hpp ·
  0-3 2caeccd viewer_log · 0-4 8815fef viewer_args · 0-5 42b9e0b viewer_decoder_backend · 0-6 3556584 viewer_gdi_util ·
  0-7 f31c633 viewer_nv12_renderer.hpp(+gNv12Renderer → globals) · 0-8 a3f4919 viewer_layout · 0-9 77809ff viewer_input_forward ·
  0-10 c8b90d2 viewer_picker · 0-11 1cc5967 viewer_overlay_draw · 0-12 b298bd1 viewer_cursor_overlay · 0-13 c054f00
  viewer_window_proc(891줄, 2-8까지 한시 허용) · 0-15 36fe396 호스트 공유 통합: env_util.hpp / string_util.hpp(host_string_util.hpp는
  forwarding) / backend_request_match.hpp(inline) — 호스트 host_args/host_capture_device의 사본 제거, 뷰어는 namespace viewer로
  re-export. 0-14 dead code 삭제는 원장 F-04로 이관(실행 안 함).
- 결과: main.cpp 5,349→2,287줄(main() 2,158줄 그대로 — Phase 1~3 대상), viewer_* 24파일(최대 window_proc 891, nv12_renderer 409,
  globals.hpp 337) + 공유 헤더 3. 게이트: 매 커밋 이동 동일성 PASS / 빌드 exit 0 / 뷰어 e2e 3/3 ALL PASS; 0-15는 호스트+테스트
  9타깃 빌드, 단위테스트 6종 PASS, 호스트 UDP e2e ALL PASS 추가.
- 발견: 코드 발견 신규 없음(원장 F-01~F-15 유지). 절차 교훈: (1) Bash 툴 인라인 체인에서는 `set -e`가 안 먹어 실패가 전파되지
  않음 → 스크립트 파일 + `&&` 체인 + 게이트 스크립트로 통일. (2) perl `s{}{}`는 치환문에 `}`/`{`가 있으면 깨짐 → `s~~~`.
  (3) 전방선언 삭제는 이동 뒤에(줄 범위가 HEAD를 가리키도록). 0-15 첫 시도는 스크립트 앵커 실패로 재배선 없이 커밋될 뻔 → 커밋
  직후 reset --soft로 되돌리고 재적용(최종 36fe396만 남음).
- 다음 액션: Phase 1 — 전역 88 + 함수 static 7 + recv 지역 75 → state struct 13(viewer_constants.hpp 선행, 인스턴스는 globals 명명
  전역 gSession/gFrameBuf/gPresent/gMetrics/gControl/gPicker/gSel/gInput/gCursor/gUi + recv 지역 RecvStats/FrameGateState/DecoderState).

### 309) 2026-08-27 뷰어 분할 리팩터 Phase 1 완료 — 전역 88개 + recv 지역 75개 → state struct 13 (브랜치 refactor/viewer-split, e4bd517 → d7b8f2f)

- 방법: 호스트 Phase 1과 같은 기계적 치환. `rename_outside_strings.pl`(문자열 리터럴·멤버 접근 제외)로 `gXxx` → `gInst.member`;
  recv 지역/`decoder`/`transport`처럼 산문에 흔한 이름은 새 `--code-only`(주석도 제외)로. state 헤더는 손으로 작성(초기값은
  viewer_globals.cpp 정의에서 그대로, `// cross-thread:` 블록으로 스레드 규칙 명문화), 옮긴 타입(SharedFrame·ClientRuntimeMetrics·
  OverlayConfigSnapshot·OverlayMetricSample/Averages·WindowThumb·ClientCongestionState·PresentCounterSnapshot)은 HEAD 대비 verbatim
  검사. 스크립트: `automation/viewer_split_phase1.sh`(1-0~1-10), `viewer_split_phase1b.sh`(1-11~1-13).
- 커밋: 1-0 e4bd517 상수 28개 → viewer_constants.hpp · 1-1 819081a SessionState gSession(+메시지 펌프 static nextToolbarPushUs) ·
  1-2 33e32ea FrameBuffer gFrameBuf(SharedFrame) · 1-3 d728803 PresentStats gPresent(+WM_PAINT static 4) · 1-4 75aea0d
  ClientMetricsState gMetrics · 1-5 a9ef628 ControlChannelState gControl(+control static reportedSecure, 다중행 초기화 2) ·
  1-6 63e49b5 PickerState gPicker(WindowThumb) · 1-7 398b501 SelectionGateState gSel · 1-8 8ecf049 InputState gInput ·
  1-9 ece4257 RemoteCursorState gCursor · 1-10 22d35e5 UiResources gUi(brush_cache static map) · 1-11 250fd72 RecvStats st(recv 지역 26)
  · 1-12 c3d8e6b FrameGateState gate(recv 지역 33 + main env 설정 4, ClientCongestionState 이동) · 1-13 0dccc66 DecoderState dec
  (main 지역 11 + recvSelectionEpoch) · 1-14 d7b8f2f viewer_globals.hpp/.cpp 정리(고아 주석 제거, extern 10).
- 결과: 파일 스코프 전역 0(인스턴스 10), 함수 static은 설정 래치 2개(`registered`, `remoteCursorEnabled`)만 유지. main.cpp 2,287→2,205줄.
  게이트: 매 커밋 빌드 exit 0 + 뷰어 e2e 3/3 ALL PASS(1-9는 빌드 중 부하로 C-1에 congested 전이 1회 → 같은 트리 재실행 통과).
- 발견: 코드 발견 신규 없음(F-03 dead 상태는 struct 멤버에 `// dead: F-03` 주석으로 표시, F-14 리셋 없음 static은 `// reset: never (F-14)`).
  절차 교훈: msys `head/sed/tail` 파이프라인이 CR을 떨어뜨려 CRLF 앵커가 빗나감 → 앵커 `\r?\n` + rename 후 CRLF 재정규화;
  이 grep의 `-P`는 CP949 로케일을 거부 → 검사는 perl로; 로그 라벨 문자열은 검사에서 제외.
- 다음 액션: Phase 2 — VideoReceiver/FrameGate(+T1)/DecoderStage/ControlClient/SelectionGate(+T2)/PickerGesture(+T3)/layout 순수화(+T4)/
  present 분리/WndProc 핸들러 분할/startup/shutdown.

### 310) 2026-08-27 뷰어 분할 리팩터 Phase 2·3 완료 — 클래스/순수결정 + 단위테스트 4종 + ViewerContext (브랜치 refactor/viewer-split, feeb781 → b877016)

- Phase 2 (14 커밋): 2-1 feeb781 recv 람다 1,240줄 → `VideoReceiver`(헬퍼 람다 8 + process_h264_frame 멤버, verbatim, 게이트 B
  --ignore-indent) · 2-1b ec31044 1s 통계 블록 5→1 `flush_stats_if_due`(변형 2개는 파라미터: raw 분모 F-02, codedSize) — 각 사본을
  정본 템플릿과 대조 후 치환 · 2-4 8bd26a5 control 람다 → `ControlClient`, 2-4b f7c1cfd 응답 switch → handle_pong/window_list/
  window_selected/input_ack · 2-2 f95d8ce process_h264_frame의 게이팅 블록 → `FrameGate`(FrameGateInputs/FrameGateLag/FrameGateSink;
  admit·note_decode_failure·note_timestamp_overflow·note_reference_sync·note_decode_empty; waitForKeyFrame은 DecoderState→
  FrameGateState; congestion_state_name은 enum 옆으로) — 유일한 순서 변화: present anchor/picker 억제 atomic 2개를 timeline 정렬
  앞에서 읽음(무해) · T1 170a5d4 viewer_frame_gate_test 7군(기대값 오류 1건은 코드가 아니라 테스트 수정: Recovering 진입 IDR도
  healthy 스트릭 1 가산) · 2-3 8cdc280 receiver.cpp 810→396+432(루프/프레임 단계 분리) · 2-5 b60964d SelectionGateState 멤버
  9개 + T2 3군 · 2-6 c54f934 PickerState 제스처 래치 멤버 6개 + T3 4군 · 2-7 99e2c7f viewer_layout_math.hpp(순수, dpi 인자) +
  T4 5군(기대값 3건 테스트 수정; 끝점 매핑 편차는 F-16) · 2-8 34e5e8a WM_PAINT 305줄 → viewer_present.cpp · 2-9 1662a5c WndProc
  핸들러 분할(on_secondary_button/picker_press/picker_release/on_local_hotkey; 마우스·터치 피커 경로 통합 — 동일 결과 검증).
- Phase 3 (b877016): viewer_context.hpp(ViewerContext) + viewer_startup.cpp 13 함수(verbatim 블록, exit code 보존) + viewer_shutdown.cpp;
  main() 689→45줄. **편차**: state 인스턴스 10개는 viewer_globals에 잔존(F-17, Phase 4).
- 결과: viewer_* 57파일(최대 viewer_startup.cpp 555), main.cpp 45줄, 30줄 초과 람다 0, 통계 복제 0. 게이트: 매 커밋 빌드 exit 0 +
  뷰어 e2e 3/3 ALL PASS; 단위테스트 viewer_frame_gate/selection_gate/picker_gesture/layout PASS; 아래 최종 게이트 참조.
- 발견 → 원장: F-16(점→비디오 좌표 끝점 편차, T4에서 확인), F-17(전역 인스턴스 10 잔존 — 계획 편차).
- 다음 액션: 최종 게이트(호스트 e2e·단위테스트 전부·verify -GateAProfile) → 0.2.59 설치본 → 실기 확인 → main 병합.

### 311) 2026-08-27 뷰어 분할 리팩터 최종 게이트 통과 + 실기용 설치본 0.2.59 (브랜치 refactor/viewer-split, 2464c62)

- 최종 게이트(b877016 기준): (A) 14타깃 빌드 exit 0 — 뷰어/호스트/단위테스트 10/UDP e2e. (C) 뷰어 e2e C-1 stream·C-2 picker·C-3 tcp-raw
  ALL PASS. (D) 단위테스트 10/10 PASS — host_abr/frame_gate/kick/backend_policy, capture_cadence_gate, shared_core, viewer_frame_gate/
  selection_gate/picker_gesture/layout. 호스트 UDP e2e ALL PASS. `verify_native_video_runtime.ps1 -GateAProfile`(udp/h264 30fps, 격리
  포트): 정적 데스크톱에서는 `capture_input_stall`로 decoded fps 목표 27 미달(환경 요인, recovery/present gap OK, 폴백 0) →
  `perf_scene_generator.ps1 -Scene scroll` 동시 구동 시 **GATE_A_PASS=True**(decoded fps OK, recovery ≤1s OK, present gap>1s 0건,
  present gap avg 53ms/p95 65ms/max 83ms, CLIENT_RC=0).
- 설치본: product_version 0.2.58→0.2.59(2464c62), `remote60_installer` 빌드, 임베드 검증 — GNLinkSetup.exe L"0.2.59" ×3 / L"0.2.58" ×0,
  GNLinkHost.exe ×1/×0. `dist/GNLinkSetup-0.2.59.exe`(3171840 B) 복사; 0.2.58(호스트 분할, 리팩터 전 뷰어)은 되돌리기용으로 보존.
- 상태: **실기 확인 대기**(계획 §10). 통과 시 `refactor/viewer-split` → main 병합(문서는 이미 main과 동기), 복귀 태그
  `v0.2.58-pre-viewer-split`. 문제 시 수정 후 0.2.60.
- 뷰어 리팩터 총괄: 5,349줄 모놀리스 → main.cpp 45줄 + viewer_* 57파일(최대 555) + 공유 헤더 3 + 단위테스트 4종, 커밋 ~55개
  (Phase 0 16 / Phase 1 15 / Phase 2 14 / Phase 3 1 + 도구·문서). 원장 F-01~F-17 기록만, 코드 수정 없음(사용자 결정).

### 312) 2026-08-27 발견사항 원장 재확인 — 분할 후 코드에 F-01~F-17 대조, 정정 4 + 신규 F-18 (main, 문서만)

- 대상: `docs/뷰어_리팩터_발견사항.md`의 "위치" 열이 모놀리스(`e346ff7`) 기준이라, 분할 후 `viewer_*` 57파일에 F-01~F-17을 전부 대조.
  원장에 "리팩터 후 재확인" 절 추가 — 항목별 분할 후 위치 매핑 + 정정표(R-1~R-5) + 처리 순서 권고.
- 정정: R-1 F-01 범위 확대(`control connected/unavailable port=`도 `args.controlPort` — 디렉터리 경로에선 `resolvedArgs.controlPort=0`) ·
  R-2 F-07에서 `gGridScrollRow` 근거 삭제(`apply_window_list_snapshot`은 visibleCards만 사용, scrollRow는 UI 전용) ·
  R-3 F-14 `// reset: never` 마커가 2곳 5필드만 커버(`nextToolbarPushUs` 누락; brush cache·`registered`·`remoteCursorEnabled`는 리셋 대상 아님 → 실제 6) ·
  R-4 F-15 present 카운터 8→11(카운터 6 + trace 5).
- 상태 변경: R-5 F-08 `[x] ec31044` — Phase 2-1b에서 `flush_stats_if_due` 1개로 통합되어 구조적으로 해소(원장 규칙 4).
- 발견 → F-18: 혼잡 진입 임계 감도. 조건은 (디코드 큐 랙 > 0.3s **또는** presented 기준 스트림 랙 > 0.45s) AND 도착 간격 ≤150ms AND
  !catchupSuppressed 가 3연속(`viewer_frame_gate.cpp:178~189`) — 빌드 중 로컬 e2e에서 CPU 경합만으로 `state=congested` 1회 전이(재실행 통과).
  임계·스트릭 튜너블화 + T1 CPU 경합 시나리오는 Phase 4.
- 코드 변경 없음(원장 규칙 1). 다음 액션: 실기 확인 후 F-01 → F-05/F-03/F-04 → F-16 → Phase 4(F-17/F-15/F-07/F-06/F-18) → F-10/F-11.

### 313) 2026-08-27 발견사항 원장 처리 1차 — F-01/F-05/F-03/F-04 (브랜치 refactor/viewer-split, 93e59ab → 5497b47)

- 목표: 원장 권고 순서의 앞부분(로그 정확도 + 죽은 코드/상태 정리)을 항목당 1커밋으로. 동작 변경 없음(F-01은 디렉터리 경로 로그값만).
- `93e59ab` F-01: UDP 핸드셰이크 실패 로그와 `connected host=/port=`가 `ctx.args` 대신 `ctx.resolvedArgs`를 찍음 — 소켓은 `resolvedArgs`로 connect 되므로
  디렉터리 세션이 실제로 접속한 적 없는 주소를 보고하고 있었다. 처리 중 발견 → F-19(`resolvedArgs.controlPort`가 쓰기 전용이고 터널/control 분기는 `args.controlPort`를 씀;
  셸이 디렉터리 세션에 `--control-port`를 안 넘겨 현재 무해) → 재확인 R-1 취소.
- `3c399a6` F-05: `WM_LBUTTONUP`의 refresh/desktop/카드 hit-test 분기 삭제(피커 숨김 시 `compute_client_layout_at`이 rect를 0으로 만들어 도달 불가) +
  `WM_MOUSEWHEEL`의 동일 분기 1곳 + 고아가 된 옛 선택 경로 `queue_window_select_request`.
- `82f945a` F-03: 쓰기 전용 상태 삭제 — 호스트 캡처 메타 미러(atomic 5 + mutex + 문자열 2), `captureOverviewMode`, `overlayConfig`(구조체 포함),
  오버레이 샘플 링(deque+mutex). recv 스레드가 매초 락 잡고 push 하던 경로가 사라졌다. pong 로그는 메시지 필드에서 직접 출력이라 문자열 불변.
- `5497b47` F-04: `point_in_video_rect`·`request_capture_focus_from_client_point`(+연쇄 고아 `coord_to_permille`) 삭제, `kRuntime*` 5개를
  `RuntimeTuneState` 초기화에 연결(같은 값 리터럴 중복 제거).
- 게이트: 매 커밋 `automation/viewer_split_gate.sh --e2e` — 빌드 exit 0 + 뷰어 e2e C-1 stream/C-2 picker/C-3 tcp-raw ALL PASS.
  F-04 커밋은 단위테스트 4종 빌드 + 실행 PASS 추가. 코드 -215/+31줄.
- 다음 액션: F-16(끝점 매핑, T4 기대값 동반) → Phase 4(F-17/F-15/F-07/F-06/F-18/F-19) → F-10/F-11. 0.2.59 실기 확인은 별개로 계속 대기.

### 314) 2026-08-27 호스트 전수 분석 + 코덱스 교차검증 → 호스트 발견사항 원장 신설 (브랜치 refactor/viewer-split, 문서만)

- 목표: Phase 4(호스트 스레드 소유권 재설계) 착수 전에 호스트 전체 소스를 전수 분석해 오구현/데드코드/고도화 대상을 원장으로 만든다.
  뷰어 원장(`docs/뷰어_리팩터_발견사항.md`)과 같은 규칙 — 기록만, 수정은 별도 커밋/사이클.
- 분석 범위(18,706줄): `apps/native_poc/src/host_*.{cpp,hpp}` 60여 파일 · `native_video_host_main.cpp` · `d3d_capture_readback.*` ·
  `capture_cadence_gate.hpp` · `host_bgra_scale.cpp` · `gdi_capture_process.*` · `mf_h264_codec.cpp`(AU 생성부) · `libs/capture/src/capture_backend_dxgi.*`.
- 교차검증: GMux a2a 버스의 `검증용Codex`(remote#sa9wm39t)와 3통 왕복(seq 769 버그/레이스/누수 12건, 770 통계/설계/데드코드/고도화 9건, 772 판정 수령+추가 2건).
  전 항목 판정 완료. 코덱스가 **추가 발견 2건**: H-03(e) NV12 lease 상시 누수 경로, H-23 readback lock-order inversion.
- 결과 23건(H-01~H-23). 확정 18 · 조건부 3(H-07 상시 race, H-14 의미 미표기, H-17 병목 계측 필요) · 기각/정정 2(H-09 인터리브 버그 기각, H-18 1Hz 갱신안 기각).
  - 최우선: `H-01` 대기 중 프로세스 핸들 close(Win32 UB, Blocker) → `H-02` accepted 소켓 이중 close → `H-03` NV12 lease 누수 5경로
    (**busy-drop이 상시 경로** — 링 4슬롯이 소진되면 zero-copy surface encode가 조용히 CPU로 영구 강등. GPU가 바쁠 때 발생하므로
    실기에서 nv12 surface가 효과 없어 보인 원인 1순위 후보) → `H-10/11/12` 1초 틱이 tick 성공에 인질(워치독·ABR 미도달 + 가짜 GDI 재시작)
    → `H-23` lock inversion → `H-19` key AU가 연 배리어를 같은 배치 delta가 되닫음 → `H-04/05` data race → `H-08` AU마다 전체 페이로드 복사
    (`std::move`가 `const&`라 복사대입 선택).
  - 자체 기각(오탐) 5건도 원장에 표로 남김 — reattach 백오프 시드, UDP 인터리브, bootstrap 갱신 cadence, queueDepthMax 리셋, alias 18→15 과대집계.
- 변경 파일: `docs/호스트_리팩터_발견사항.md`(신설) · `docs/호스트_분할_리팩터_계획.md`(§7.1 근본제약 문구 정정 + §7.2 후보에 H-04/H-05 명시 + §7.3 신설) ·
  `docs/구현계획.md`(체크박스) · `docs/history.md`.
- 검증: 문서만. 코드 변경 0 → 빌드/e2e 미실행. 원장의 파일:라인 근거는 전부 `3508c85`에서 직접 확인했고,
  코덱스 지적 2건(H-16 alias 과대집계, H-23 lock inversion)은 재검증 후 반영.
- 다음 액션: 원장 권고 순서대로 `H-01` → `H-02` → `H-03` 를 항목당 1커밋으로 처리(Phase 4를 기다리지 않는다 — 종료 경로 UB + 상시 누수).
  그 뒤 `H-10/11/12` 묶음 → `H-23` → `H-19`. 0.2.59 실기 확인은 별개로 계속 대기.

### 315) 2026-08-27 OSLink(현재 원격 세션) 전송 경로 실측 — 직접 UDP P2P 확인, 근거 대장 E7 (main, 문서만)

- 계기: 사용자가 "지금 OSLink로 붙어 있는데 포트 뭘 쓰는지 보라"고 요청. GNLink과 달리 포트포워딩 없이 붙는다는 관찰의 근거를 잡는 것이 목적.
- 방법: `Get-NetTCPConnection`/`Get-NetUDPEndpoint`로 `ldremote.exe`(PID 10480)가 가진 소켓을 뽑고, 그 **로컬 포트만 필터**로 걸어
  관리자 권한 `pktmon --capture` 8초 → `etl2txt` → PktGroupId로 중복 제거(같은 패킷이 컴포넌트마다 기록됨) 후 플로우별 집계.
- 결과(8초 창):
  | 흐름 | 패킷/바이트 | 대역 | 정체 |
  |---|---|---|---|
  | `UDP 61354 ↔ 211.218.222.4:55964` | 1,424 / 539KB (전량 Tx) | ≈550kbps | **화면 영상 — 직접 P2P** |
  | `TCP 54185 ↔ 47.86.3.37:7778` | 32 / 4.3KB (Tx16 Rx16) | ≈4kbps | 상시 시그널링 |
  | `TCP 54850 ↔ 47.237.126.99:443` | 0 | — | 접속 시 API, 이후 유휴 |
- 구조: `ldremote.exe --byself`(사용자 세션) + `ldremoteservice.exe`/`ldremoteevent.exe`(SYSTEM, 로그상 `RunUnderWinLogon`으로
  콘솔 세션에 재기동 — UAC/보안 데스크톱 대응 경로). 엔진은 `ZegoExpressEngine.dll`(ZEGO RTC SDK), 캡처는 OBS 계열
  `win-capture.dll` + DXGI Duplication(측정 시점 28~30fps). **LISTENING 포트 0개** — 전부 아웃바운드로 매핑을 만든다.
- 함의 → **E7**: 문제의 구간(회사 공인 `211.218.222.4` ↔ 집 개발 PC `175.209.236.194`)에 직접 UDP 경로가 실재한다.
  N3에서 "홀펀칭으로 뚫을 수 있는 종류의 막힘이 아니다"라고 확정한 것은 **사내 헤어핀**(폰 게스트망 → 사내 호스트 공인 `211.218.222.1`)
  구간이고, 이번 측정 구간과 다르다. 두 도구의 구조 차이: OSLink은 **양쪽 임시 포트 + 상시 TCP 시그널링 채널**,
  GNLink은 **호스트 고정 UDP 43000 bind + 접속 시점에만 후보 광고/펀치**. history #268의 `punch -> 211.218.222.4:{55133,48640,...}`처럼
  회사측 포트가 시도마다 달라지는 것과 대비된다.
- 한계: ZEGO 엔진 로그(`zegoavlog*`)는 난독화되어 평문 정보 없음. 최초 펀칭이 어느 쪽 패킷으로 성립했는지는 캡처 창(연결 이후 8초) 밖이라 미확인.
  피어 `211.218.222.4`는 저장소에 회사 공인 IP로 기록된 주소와 일치하나, 이번 세션의 물리적 위치는 직접 확인하지 않았다.
- 정리: 프로브 스크립트·ETL·임시 해제한 ZEGO 로그 삭제, pktmon 필터/세션은 스크립트 `finally`에서 원복. 저장소 코드 변경 없음.
- 다음 액션(사용자 판단 대기): 이 구조 차이를 N 항목으로 승격할지 — "미디어 소켓을 임시 포트로 + 시그널링 채널 상시 유지" 검토.

### 316) 2026-08-28 호스트 원장 23건 + Phase 4 스레드 소유권 재설계 (브랜치 refactor/viewer-split, 779390e → e545cd6)

- 목표: `docs/호스트_리팩터_발견사항.md`의 H-01~H-23을 코덱스 권고 순서대로 항목당 1커밋으로 처리하고,
  이어서 계획서 §7.2의 Phase 4 본체(메일박스/스냅샷)를 끝낸다.
- 게이트: 매 커밋 gate-A 빌드 + `automation/host_udp_e2e.sh` ALL PASS + 단위테스트. 마지막에 host/host_app/client 전 타깃 빌드 에러 0 + 단위테스트 11종 PASS.

**원장 23건** (커밋 → 항목)
- `779390e` H-01 대기 중인 프로세스 핸들 close(Win32 UB) / H-02 accepted 소켓 이중 close — 둘 다 "소유자가 둘"이라 생긴 문제라 소유자를 하나로 고정.
- `4aaa451` H-23 readback lock-order inversion(worker `slotMu→contextMu` vs Submit `contextMu→slotMu`) / H-03 NV12 lease 누수. 대표 경로는 코덱스가 찾은 (e) staging busy-drop — 예외가 아니라 상시 경로라 링 4슬롯이 소진되면 surface encode가 조용히 CPU로 영구 강등된다.
  처리 중 **6번째 경로 발견**: publish 성공 후에도 worker가 `slotRef->meta.nv12Slot`을 안 지워서, (c)의 일괄 반환을 넣으면 이미 인코드 루프로 넘어간 lease를 double-free 한다. 인계 시점에도 -1로 지워 `meta.nv12Slot >= 0` == "링이 아직 소유" 불변식을 세웠다.
- `cf9d684` H-10/11/12 1초 틱. `stage_stats`가 12번째라 앞 스테이지의 Continue가 통계·ABR·drain 워치독·GDI 재시작을 통째로 건너뛰었다 — 하필 파이프라인이 멈췄을 때 도는 경로들(pop 타임아웃, units.empty, 스트림 idle)이다. tick의 **첫** 스테이지로 옮겼다.
  실측: STATS_PRINT_EVERY_SEC=1로 클라이언트를 떼고 12초 방치 → 1.000s 간격 13연속(01:29:34.479~01:29:46.481). 수정 전 같은 구간은 0회.
- `3f89610` H-19 배치 내 stale `senderBacklogged` — key AU가 연 배리어를 같은 배치의 delta가 되닫아 IDR 루프. 큐 정책을 `host_sender_queue_policy.hpp` 순수 함수로 뽑고 회귀 테스트 추가(리셋을 뺀 옛 동작도 명시적으로 박제).
- `0efefc8` H-04 키프레임 토큰버킷 race(keyReqMu 트랜잭션으로) / H-05 cadence gate 무락 getter 4개(SnapshotCounters 하나로) — 계획서 §7.2의 CadenceGate::Snapshot 완료.
- `75232d4` H-08 `std::move(au.bytes)`가 `const&`라 복사대입이었다. AU마다 40~160KB 깊은 복사.
- `e71182e` H-06 detach된 main-loop 워치독 → RAII 소유 + cv로 즉시 join. 정상 종료 rc=0/"done" 확인.
- `d352ffa` H-13 합성 프레임이 캡처 타이밍 평균 분모만 늘리던 것 / H-14 `queueDepthMax`는 lifetime이라 라벨 유지 + `queueDepthWindowMax` 신설.
- `2b0ab1a` H-21 DIB 비트 읽기 전 GdiFlush 누락 + 데스크톱 썸네일 전체 크기 DIB/CPU bilinear → StretchBlt(HALFTONE). 신규 `host_bgra_scale_test`.
- `fb944c4` H-07 로그인 worker가 `g.cache` std::string을 직접 갱신하던 race → SignInResult 값 전달 + UI 스레드가 유일 writer / H-20 자식 커맨드라인 따옴표 미이스케이프 → MS CRT 규칙 argv builder. 신규 `host_command_line_test`(CommandLineToArgvW 왕복 9건, 인자 주입 시도 포함).
- `5cbcf99` H-15 아무도 안 읽는 cadence EWMA / H-16 미사용 alias **32개**(첫 보고 18은 스캐너 오류로 과대집계 → 코덱스가 15로 정정 → 파일 단위 재스캔으로 19개 추가 발견).
- `6734b9b` H-17 GetData에 DONOTFLUSH + 적응형 백오프(Submit이 명시 Flush를 하므로 전제가 닫힘) / H-18 publish마다의 WTS 시스콜을 attach 시점 stamp로. 실측: 14초 세션에서 `queuePushPerSec == callbackFrames` 매초 일치.
- `3bb54da` H-22 마지막 전역 3개 → `SenderState` egress config 스냅샷(dequeue마다 1회).

**Phase 4 본체**
- `d0bd795` BackendFallbackInfo / SnapshotTarget / ClientMetricsSnapshot. 공통 성격은 "함께 읽어야 하는 값을 따로 읽어 존재한 적 없는 상태를 만들 수 있었다" — 특히 뷰어 메트릭 19개를 각각 atomic으로 읽어 두 보고를 섞은 상태로 ABR/M9을 결정할 수 있었다.
- `e545cd6` MainLoopMailbox + RequestKeyframe. `*Pending` 12개 은퇴. RequestKeyframe이 "forceKeyNext를 sender 스레드에서 쓰지 말 것" 규칙을 소멸시킨다 — sender가 그 규칙을 우회하려고 쓰던 두 번째 플래그(recoveryPending)는, 첫 번째(requestKey)가 실제 프레임 pop 뒤에만 읽혀 정적 데스크톱에선 복구 IDR이 안 나오기 때문에 존재했다. 이제 프레임 대기 **앞** 한 지점에서 소비한다.
  덤: `startup_configure_control_state`의 `backend.reqValue = ...from_env()`가 아무도 읽지 않는 죽은 쓰기였음을 발견해 제거.
- 검토 항목이던 **인코더 전용 스레드는 보류** — 캡처→인코드가 이미 latest-wins 단일 슬롯이라 스레드를 넣어도 같은 프레임이 큐에서 버려지고 지연만 한 홉 는다. H4가 푼 것은 와이어였지 인코더가 아니다. 재검토 조건을 계획서 §7.4에 명시.
- **egress mux(H-09 후단)는 미착수** — 정확성 문제는 없고 필요성은 실측 후 판단.

- 신규 단위테스트 4종(host_sender_queue_policy / host_bgra_scale / host_command_line / host_main_loop_mailbox). 호스트 UDP e2e를 13 → 18 체크로 확장 — 메일박스로 옮긴 5경로 중 tune 하나만 덮고 있었기에 keyframe/monitor/backend 요청을 실제로 구동하도록 추가했고, 호스트 로그로 왕복 확인(`runtime-config seq=1,2` 각각 applied / `monitor-select applied` / `desktop-backend-request→applied` / `keyframe-request reason=2 → -consumed reason=2`).
- 변경 파일: `apps/native_poc/src/` 호스트 모듈 다수 + `CMakeLists.txt` + 신규 헤더/테스트 6개, `docs/호스트_리팩터_발견사항.md`, `docs/호스트_분할_리팩터_계획.md`(§7.2/§7.3/§7.4/§10), `docs/구현계획.md`, `docs/history.md`.
- 코덱스 교차검증: diff 검증 요청(seq 776)을 보냈으나 응답 없음 — 사용자 지시("코덱스 토큰 얼마 안 남았으니 대답 없으면 그냥 진행")대로 계속 진행했다. 원장 작성 단계의 판정(seq 769/770/772)은 전부 반영돼 있다.
- 다음 액션: **실기 확인** — Phase 4 코드로 설치본을 빌드해 사용자 판정. 그 전까지 `dist/`에 내지 않는다(계획서 게이트 E). 이후 P2(readback 60fps 공급)로.

### 317) 2026-08-28 코덱스 diff 교차검증 반영 — blocker 2 + High 1 (브랜치 refactor/viewer-split, a67522f)

- 목표: #316의 `779390e`/`4aaa451`에 대한 코덱스 diff 검증 결과를 닫는다.
- **H-23은 내가 만든 회귀였다.** AB-BA 데드락을 없애면서 meta 확정을 contextMu 해제 뒤로 옮겼는데,
  `slot->state`는 컨텍스트 작업 **전에** 이미 GpuPending이라 그 사이 워커가 미확정 meta를 소비할 수 있었다:
  실패한 preprocess를 `preprocessed=true`로 읽기(캡처 크기 바이트를 인코드 크기로), 반환 예정 NV12 lease를
  유효 surface로 소비자에게 넘기고 뒤늦은 실패 처리가 같은 lease를 반환(double-owner), 그리고 실패와 무관하게
  매 프레임 `submitUs=0` 프레임이 나가 frozen-ring 워치독이 그 슬롯을 못 세는 창.
  `SlotState::Submitting`을 publication barrier로 추가했다 — 워커는 GpuPending만 poll하고, Submit이
  D3D 작업 + meta 확정을 끝낸 뒤 **마지막에** 승격한다. 락 순서 `slotMu→contextMu`는 그대로.
  identity도 `submitSeq`로 교체(같은 generation 안에서 슬롯이 재사용돼도 staging 텍스처는 동일해 포인터는
  fence가 아니다), NV12 실패 lease 반환도 identity 안으로(밖이면 Reconfigure 후 재대여된 index를 푸는 ABA).
- **H-02: 내 추론이 틀렸다.** "stop=true라 새 소켓을 못 넘긴다"는 새 accept만 막을 뿐, owner가 load와
  shutdown 사이에 close하는 것과 무관하다. atomic은 핸들 값만 게시하지 수명을 고정하지 못하고 닫힌 SOCKET
  값은 즉시 재사용된다. `controlClientSockMu`를 도입해 게시/해제+close/외부 shutdown을 전부 그 아래로.
- **H-01 기동 레이스**: Stop이 CreateProcessW 도중이면 TerminateChild가 미게시 child_를 보고 아무것도
  안 죽이고, 이후 게시 + Wait(INFINITE)로 join이 영구 대기. 게시 임계구역에서 `running_` 재확인으로 닫음.
- 코덱스가 PASS 준 부분: H-01 핵심(wait 중 handle close 소유권), H-03 lease 규칙 5경로 + "publish 후 meta -1".
- 신규 회귀: `capture_readback_test`에 슬롯 identity 7건(`readback_slot_is_current`를 순수 함수로 뽑아 GPU 없이).
- 새 원장 항목 **H-24**(미착수): `controlListenSock`이 plain SOCKET이라 accept 중 close와 겹친다 —
  H-02는 accepted 소켓만 고쳤다. 검증 부채도 기록(fault-injection 훅 기반 결정론적 교차 테스트).
- 변경 파일: `d3d_capture_readback.{hpp,cpp}` · `capture_readback_test.cpp` · `host_session.hpp` ·
  `host_startup_control.cpp` · `host_shutdown.cpp` · `host_app_main.cpp` · 문서 3.
- 검증: 전 타깃 빌드 에러 0 + host_udp_e2e ALL PASS + 단위테스트 22종 PASS + 정상 종료 rc=0/"done".
- 다음 액션: 실기 확인(설치본 빌드 → 사용자 판정). H-24와 검증 부채는 그 다음.

### 318) 2026-08-28 a67522f 재검증 PASS + H-25 기록 (브랜치 refactor/viewer-split, 문서만)

- 코덱스가 `a67522f`를 재검증해 세 지적(H-23 publication barrier / H-02 소켓 lifetime / H-01 기동 레이스)이
  모두 닫힌 것으로 확인했다. 추가 blocker/High 없음. 검증이 끝난 커밋은 `779390e` / `4aaa451` / `a67522f`
  셋이고, 21커밋 전체를 일괄 승인한 것은 아니다 — 원장에 그대로 명시했다.
- 비차단 관찰 1건을 **H-25**로 기록. "슬롯이 Submitting인 동안 OldestGpuPendingAgeUs / GpuPendingCount
  어디에도 안 잡힌다"는 지적인데, 코드로 따져보니 **복구 동작은 바뀌지 않았다**: frozen-ring 워치독의
  트리거는 `oldestPendingUs`이고 `OldestGpuPendingAgeUs()`는 `meta.submitUs == 0`인 슬롯을 원래부터
  건너뛴다. submitUs는 컨텍스트 작업이 끝난 뒤 찍히므로 Submitting 상태가 생기기 전에도 Submit 도중인
  슬롯은 이 지표에 안 보였다. 실제로 좁아진 것은 `GpuPendingCount()` 하나이고 그 값은 통계 필드
  (`gpuPendingCountPeak` / `gpuPendingCount=`)에만 쓰인다. 회귀가 아니라 원래 있던 사각지대가 상태
  이름으로 드러난 것이라 그렇게 적었다.
  WGC 콜백 안의 Submit 장기 정체까지 복구 대상으로 삼으려면 Submitting 진입 시각을 찍고 별도 age를
  노출해야 한다(DXGI는 독립 worker 워치독이 이미 잡는다).
- 변경 파일: `docs/호스트_리팩터_발견사항.md`, `docs/구현계획.md`, `docs/history.md`. 코드 변경 없음.
- 다음 액션: 실기 확인(설치본 빌드 → 사용자 판정). 그 뒤 H-24 / H-25 / 검증 부채.

### 319) 2026-08-28 코덱스 cf9d684/e545cd6 검증 반영 — H-26 (브랜치 refactor/viewer-split, 854b58f)

- 코덱스가 우선 검증 대상으로 지목한 두 커밋을 diff 단위로 봤고, `e545cd6`(MainLoopMailbox)가
  **스스로 커밋 메시지에 내건 계약을 못 지키고 있었다**는 지적이 나왔다. 셋 다 코드에서 확인해 닫았다.
- H-26a: `stage_time_limit`(2번)이 메일박스를 비우는데 `stage_gate_static`(11번)이 같은 tick에 peek 하니
  거의 항상 false. 옛 `requestedKeyFrame` atomic은 gate **뒤** encode 경로에서 소비돼 gate가 true를 볼 수
  있었다 — 즉 강제 IDR을 실을 바로 그 프레임을 정적 게이트가 throttle 할 수 있었다. gate가
  `encoder.forceKeyNext`를 읽도록 수정.
- H-26b: `kick.Arm`이 SenderBarrier에만 걸려 있어 "새 프레임 없이도 모든 키 요청이 동작"이 미완이었다.
  모든 reason에서 arm하고 `needKick`에 `forceKeyNext` 포함.
- H-26c: UDP 롤오버에 `mailbox.Clear`가 없어 옛 클라의 요청이 새 클라에 적용될 수 있었다(옛 atomic도
  같은 누락이라 회귀는 아니나 Clear 주석이 거짓이었다). dispatcher가 옛 Serve 반환 후 `Reset` 다음,
  `controlReadyEpoch` publish 전에 Clear.
- H-26d: `cf9d684`가 `stage_stats`를 `stage_runtime_tune` 앞으로 옮긴 탓에 1초 경계와 명시 tune이 겹치면
  ABR/M9가 pre-tune 상태로 재init하고 tune이 또 재init한다. `TuneEncoderPending()` peek로 그 tick의
  ABR/M9를 hold해 옛 동작 복원.
- **테스트 구멍**: `sink.AskForKeyframe(); wait_until(state == Connected)`는 요청 전부터 Connected라 즉시
  참이라 아무것도 검증하지 않았다. 그리고 `e545cd6` 커밋 메시지의 "harness가 host log를 grep한다"는
  사실이 아니었다 — 수동으로 한 번 했을 뿐이다. 이제 sink가 key AU를 세어 요청 전후 증가를 기다리고
  (실측 before=3 after=4), harness가 host.log에서 네 줄을 grep해 없으면 FAIL한다.
- 검증: 전 타깃 빌드 에러 0 + host_udp_e2e ALL PASS(18 체크 + host 로그 4건) + 단위테스트 22종 PASS +
  정상 종료 rc=0/"done".
- 다음 액션: 실기 확인. 그 뒤 H-24 / H-25 / 검증 부채.

### 320) 2026-08-28 H-26b 2패스 + H-27 기록 (브랜치 refactor/viewer-split, 631882d)

- 코덱스가 `854b58f`를 재검증해 High A / High C / host-log gate는 닫혔다고 확인했고, **High B에 한 단계가
  남았다**고 지적했다. 체인을 코드로 확인했고 맞다.
- viewer 키 요청은 보통 **열린** 배리어에서 온다. `needKick`에 `forceKeyNext`를 넣어 첫 cached submit은
  보장했지만 성공 뒤 `rearm = barrierClosed`라 곧바로 `kick.Cancel()`이고, `Cancel`은 pending=false로
  `Due()`를 영원히 false로 만든다. 비동기 MFT가 그 forced input을 보류해 `units.empty`를 반환하면
  `forceKeyNext`는 true인 채(키 AU가 실제로 emit될 때만 꺼진다 — `..._au.cpp:426`) 다음 input이 영영 없다.
  `rearm = barrierClosed || encoder.forceKeyNext`로 수정. 종료는 AU 경로가, IDR train 억제는 300ms
  submit 래치가 보장한다.
- **테스트**: 기본 1Hz 리프레시가 이 구멍을 최대 1초 뒤에 가려버린다는 지적이 정확하다. `host_udp_e2e.sh`를
  두 leg로 나눠 두 번째를 `REMOTE60_NATIVE_STATIC_REFRESH_MS=0`으로 돌린다. 그 leg에서 정적 리프레시 0회,
  trailing kick 10회, 키 요청 → IDR 도착(before=3 after=4) 확인. `units.empty` 강제 주입 fault leg는
  아니라 부분 커버이고 검증 부채로 남겼다.
- 작업 중 harness 버그 하나: `HOST_LOG_CHECKS`의 구분자 `|`가 패턴 안 alternation과 충돌해 grep 오류로
  FAIL 했다(`@@`로 교체). 이 오탐이 게이트가 실제로 동작한다는 증거이기도 하다.
- 코덱스 정정 반영: **H-26d는 "완전 제거"가 아니라 "창 축소 + 일반 경로 복원"**이다. peek가 false를 읽은
  직후 post되면 같은 tick 이중 적용이 여전히 가능하다(옛 순서에도 대칭 창이 있었으므로 새 결함은 아님).
- 신규 **H-27**(미착수): `mailbox.Clear`는 큐에 남은 요청만 격리한다. main이 이미 Take해 적용 중이면
  회수할 수 없다. H-26d 잔여 창과 근본 원인이 같아 tick 시작 drain 스냅샷 또는 요청에 servedEpoch stamp로
  함께 닫는 것이 자연스럽다.
- 검증: 전 타깃 빌드 0 에러 + host_udp_e2e **두 leg** ALL PASS(각 18 체크 + host 로그 4건) + 단위테스트 22종 PASS.
- 다음 액션: 실기 확인. 그 뒤 H-24 / H-25 / H-27 / 검증 부채.

### 321) 2026-08-28 631882d 실기 승인 + forceKeyNext 해제 시점 문구 정정 (브랜치 refactor/viewer-split, c1dc455)

- 코덱스가 `631882d`를 최종 재검증해 **실기 설치본 대상으로 승인**했다. 잔여 High B는 닫혔고,
  두-leg harness가 별도 포트/프로세스/로그로 독립 실행·판정하는 것과 `@@` separator 파싱,
  FAILED 누적·최종 exit도 정상으로 확인됐다. `units.empty` fault injection 부재는 주석과 원장에
  정확히 남아 있어 실기 blocker가 아니라는 판정.
- Low 지적 하나 반영: 내 주석이 "키가 wire에 도달하면 forceKeyNext clear"라고 썼는데 실제로는
  key AU가 **send path에 accept될 때** clear다(UDP는 sender 큐 enqueue 이후, TCP는 write 이후).
  clear 지점에도 그 약한 조건이 왜 안전한지 적었다 — key AU는 항상 EnqueueKey 분기를 타므로 위의
  `!enqueuedForSend` early-out이 키를 삼키지 않고, 이후 UDP 송신 실패는 배리어 재무장 +
  RequestKeyframe{SenderBarrier}로 덮인다.
- 코덱스 교차검증이 확인한 커밋: `779390e` / `4aaa451` / `a67522f` / `cf9d684` / `e545cd6` /
  `854b58f` / `631882d`. 나머지는 자체 게이트(빌드 + 두-leg e2e + 단위테스트 22종)만 통과한 상태.
- 검증: 빌드 0 에러 + host_udp_e2e 두 leg ALL PASS + 단위테스트 22종 PASS. 주석만 변경.
- 다음 액션: **실기 확인**(설치본 빌드 → 사용자 판정). 그 뒤 H-24 / H-25 / H-27 / 검증 부채.

### 322) 2026-08-28 OSLink 프로세스 구조 실측 + GNLink 호스트와 대조 문서 신설 (브랜치 refactor/viewer-split)

- 계기: "한 프로세스에 스레드·mutex가 너무 많다. 프로세스를 나눠 담는 게 이득 아니냐" — 경쟁 제품(OSLink)이 실제로 어떻게 갈라 놨는지부터 실측.
- 신설: `docs/OSLink_구조분석.md` (README 활성 문서 목록에 등재). 측정 절차 · 프로세스 트리 · 전송 구조 · GNLink 대조 · 분리의 득실 · 한계.
- OSLink 실측: **권한 경계로 프로세스 4개** — `ldremoteservice.exe`(LocalSystem 서비스) → `ldremoteevent.exe`(SYSTEM, 콘솔 세션 재기동) →
  `capture.exe`(캡처 전담, 소켓 0) / 별도로 `ldremote.exe`(사용자, 스레드 84, **소켓 전부 소유**). 프레임은 명명 파이프
  `ld-winpipe-read/write-<n>` + `VideoFrameIPC`. 세션 전환 시 서비스가 SYSTEM 자식을 죽였다 새 세션에 다시 띄우고, 사용자 프로세스는 연결 유지.
  엔진은 ZEGO RTC SDK, 캡처는 OBS 계열 + 자체 가상 디스플레이(VDD by MTT).
- GNLink 대조: 산출물은 이미 5개(`GNLinkHost`/`GNLinkStream`/`GNLinkCapture`/`GNLinkInputService`/`GNLinkViewer`)지만 **상주는 2개**뿐이고,
  캡처+인코딩+ABR+컨트롤+송신이 전부 `GNLinkStream` 한 프로세스(런타임 스레드 23, 핸들 861). `GNLinkCapture`는 GDI 폴백 전용,
  `GNLinkInputService`는 Manual/Stopped. 동기화 객체 선언 mutex류 19 / atomic 멤버 107 (뷰어 3 / 72) — Phase 4 정리 후에도 남은 밀도.
- 판단: 밀도의 원인은 스타일이 아니라 **경계**(캡처·인코딩·네트워크가 한 주소 공간). 다만 프레임 IPC를 0-copy로 유지할 수 있는지가
  채택을 가르는 핵심이라 문서에는 **검토 후보로만** 적고 결정하지 않았다. 코덱스 상의용 질문 4개(Q1~Q4)를 문서 §5에 명시.
- 코드 변경 0 · 빌드/e2e 미실행(문서만).
- 다음 액션: 검증용 코덱스와 Q1~Q4 상의 → 결론을 §5에 반영하고, 채택 시 `구현계획.md` N/H 항목으로 승격.

### 323) 2026-08-28 프로세스 경계 재설계 검토 — 코덱스 교차검증, 캡처+인코딩 워커 분리 권고 (브랜치 refactor/viewer-split)

- `docs/OSLink_구조분석.md` §5를 열린 질문(Q1~Q4)에서 **검토 결과**로 교체. 검증용 코덱스(a2a `remote#sa9wm39t`)와 2라운드 진행.
- 코덱스가 지적한 내 오류 2건, 코드 확인 후 그대로 수용:
  - "Q1(원시 shared texture 0-copy)이 안 되면 논의 무의미" → **②안(캡처+인코딩 분리)의 선결조건이 아니다.** ①안(캡처만 분리)의 별도 연구 게이트로 강등.
  - "스레드·mutex 개수가 분리 근거" → **철회.** OSLink의 사용자 프로세스도 스레드 84개다. 근거는 fault domain · 권한/세션 · 소켓·NAT 매핑 수명.
  - 덤: **프로세스 분리는 포트포워딩/NAT를 고치지 않는다**(그건 ZEGO ICE/relay 계층 결과) — §5 머리에 명시.
- 코덱스 인용 근거 5건 전부 코드에서 직접 확인: `host_stage_encode_send_h264.cpp:302~317`(AMF surface ~68ms vs CPU 4.5ms, 30프레임 프로브 후 세션 단위 폴백) ·
  `:296~300`/`:363~367`(`nv12PendingReleases` = MFT retention ack) · `gdi_capture_protocol.hpp:21~50`(고정 크기 원시 BGRA 3-slot) ·
  `gdi_capture_process.cpp:107~117`(프레임마다 vector 할당+memcpy) · `object_name()`의 **`Local\` 네임스페이스 + 기본 ACL**(세션 경계 확장 불가).
- 합의: ②안 = `GNLinkStream`을 **broker**(소켓·control·epoch/barrier·워커 감시)로 두고 **`GNLinkMediaWorker`**(WGC/DXGI/GDI+스케일+MFT)를 재시작 가능한 자식으로 분리,
  **H.264 AU + 메타만 IPC**. 기존 `GNLinkCapture.exe`는 프로토콜 재사용 금지(원시 BGRA 전제) — 수명관리 코드만 `ChildProcessSupervisor`로 추출.
  시점은 **0.2.59 실기 안정화 후**, 지금은 ADR + bounded spike 정의까지만. 5단계 절차를 §5.5에 기록.
- 미해결(코덱스에 2라운드 질의 발송, seq 790): R1 워커 재시작이 클라 선택 게이트의 generation에 어떻게 보여야 하는가(투명 재시작 vs 새 generation) ·
  R2 SyntheticRefresh(F-10)를 IPC 스키마 v1 필수 필드로 선행해야 하는가 · R3 spike 합격 기준 수치화(p95 회귀 ≤10%, 워커 kill 후 복귀 ≤1.5초 제안).
- 코드 변경 0 · 빌드/e2e 미실행(문서만). 채택 시 `구현계획.md` N/H 항목으로 승격 — 아직 승격하지 않았다.

### 324) 2026-08-28 미디어 워커 분리 ADR 초안 확정 — 코덱스 2라운드(R1/R2/R3) (브랜치 refactor/viewer-split)

- `docs/OSLink_구조분석.md` §6 신설(ADR 초안 6줄 + 6.1~6.3). **승인 전 초안** — 채택은 `구현계획.md` N/H 승격으로 결정하며 아직 승격하지 않았다.
- ADR 6줄: ①논리 세션 identity(epoch/generation/**wire seq**/barrier)는 broker 단독 소유 ②worker identity는 private `workerIncarnation`, wire 미노출
  ③워커 재시작 = 같은 generation의 투명 IDR 복구(피커 재노출 금지) ④frame provenance는 IPC v1 필수, 현재 wire의 SyntheticRefresh 비트가 선행
  ⑤성능 게이트는 물리 콘솔 baseline 후 사전 동결, RDP는 기능 회귀 suite ⑥production 착수는 0.2.59 실기 통과 후.
- R1 근거(코드 확인): `viewer_selection_gate.hpp:13~30`(generation = 사용자 승인 논리 타깃의 identity) · `:63`(`activeStreamGeneration` = reveal 후 영구 필터) ·
  `:54`(`DropStraggler` — 워커 재시작으로 generation을 올리면 새 프레임이 여기로 떨어진다) · **`host_stage_engine... host_stage_encode_send_h264_au.cpp:235`
  (`hdr.seq = ++encoder.encodedSeq` — wire seq가 인코더 소유라 재시작 시 1로 되감김 → broker로 이관 필요)**. 투명 재시작 트랜잭션 7단계와 H-26/H-27 접점 기록.
- R2: `poc_protocol.hpp:130`(`flags` bit0 key뿐) 확인. 분리 후에는 broker가 AU만 보고 합성 여부를 복원할 수 없으므로 **SyntheticRefresh(F-10)를 현재 wire에서 먼저 검증한 뒤
  IPC v1 필수 필드로** 넣는다. IPC는 bool 대신 `FrameOrigin{RealCapture,TrailingKick,StaticRefresh,RecoveryBootstrap}`; 워커 재시작 첫 IDR은 `RecoveryBootstrap`으로 구분.
- R3: steady-state suite(상대·절대 임계 병행, IPC drop 0) + fault-recovery suite(kill 10회, 첫 present p95 ≤1.0s / max ≤1.5s = `viewer_constants.hpp:37`과 동일 값,
  NAT 매핑·generation 불변, pre-key delta 0). 물리 콘솔 fail-closed preflight(실제 GPU 출력 / `desktop_backend=dxgi` / WGC 폴백 없음 / 타 원격 도구 종료) 선행.
- 새로 드러난 실패 모드: **워커 재시작 직후 정적 화면 seed 프레임** — WGC/DXGI가 변화 없으면 프레임을 안 주므로 "첫 전달 AU는 key"만으로는 복구가 끝나지 않을 수 있다.
  fault-recovery suite를 정적 화면 시나리오로도 돌리도록 기록.
- 코드 변경 0 · 빌드/e2e 미실행(문서만). 코덱스 3라운드 발송(seq 792) — ADR 문구 확정 확인용.

### 325) 2026-08-28 ADR 마감 정정 — 정적 화면 seed 위험 문구 · worker Ready 정의 · wire seq 소유 범위 (브랜치 refactor/viewer-split)

- 코덱스 3라운드(마감 판정) 반영. 기술 방향 변경 없음, **문구 정밀화 4건**.
- **정정(내 오류)**: #324에서 "정적 화면이면 WGC/DXGI가 프레임을 안 주므로 워커가 프레임을 영영 못 얻는다"고 단정했는데 **틀렸다.**
  실기상 duplication/frame-pool 재생성 직후 initial frame이 오는 경우가 많다(기존 로그도 그랬다). 정확한 위험은 **복구가 그 우연한 initial callback에 의존하게 되는 것** —
  raw 캐시가 워커와 함께 사라진 상태에서 콜백이 늦거나 누락되면 barrier를 열 seed가 없다. §6.1을 그대로 고쳤다.
- ADR 7번 신설: **`worker Ready`는 프로세스 기동이 아니라** broker generation의 타깃 bind 완료 + **정적 데스크톱에서도 IDR seed를 만들 수 있는 상태**.
  broker는 Ready 이전 AU를 받지 않고, **first-present 전까지 복구를 완료로 보지 않는다.**
- ADR 1번 정밀화: wire seq는 **broker가 network/media epoch 안에서 단조 소유** — 새 client epoch에서는 reset 가능하나 워커 재시작만으로는 되감지 않는다.
- §6.3에 **B-static leg 신설**(필수): 물리 콘솔·타깃 불변·`STATIC_REFRESH=0`·화면/커서/입력 완전 정지에서 DXGI·WGC 각각 10회 kill.
  seed 미도착 시 명시적 폴백 3택(백엔드 재생성 / one-shot 스냅샷 / 동일 타깃·generation·보안 identity 검증된 broker-side last-safe IDR replay — 락·타깃·세션 경계 넘기지 말 것).
- 코덱스 스레드 종료(3왕복, seq 788/790/792). 코드 변경 0 · 빌드/e2e 미실행(문서만). `구현계획.md` N/H 승격은 여전히 사용자 결정 대기.

### 322) 2026-08-29 0.2.60 설치본 + RDP 테스트 규칙 (브랜치 refactor/viewer-split)

- Phase 4 코드의 실기 판정을 위해 설치본을 냈다. `product_version.hpp` 0.2.59 → **0.2.60**,
  `remote60_installer` 빌드, payload 6실행파일 + HTML 2개 스테이징.
- **임베드 검증**(HANDOFF가 유지하라고 한 절차): payload `GNLinkHost.exe`와 `dist/` 복사본 양쪽에서
  UTF-16 버전 문자열이 `0.2.60` 임을 확인. → `dist/GNLinkSetup-0.2.60.exe` (3,174,400 bytes).
  되돌리기는 `dist/GNLinkSetup-0.2.59.exe`.
- 게이트: 전 타깃 빌드 0 에러 + host UDP e2e 두 leg ALL PASS + 단위테스트 **21/22**.
- **실패 1건은 환경 문제로 규명**: `remote60_gdi_capture_process_test` 가 `delivered=96 / fps=31.99`로
  `>=150 / >=50fps` 요구를 못 맞췄다. 원인은 코드가 아니라 **RDP 접속 상태**다 —
  `qwinsta` 결과 `rdp-tcp#0` 이 Active 이고 `Microsoft Remote Display Adapter` 의 갱신률이 **32Hz**다.
  GDI 캡처는 화면 갱신률에 묶이므로 50fps 요구를 물리적으로 만족할 수 없다.
  근거 3가지: (a) `gdi_capture_*` 는 이번 사이클 diff에 없음, (b) 세션 초반 동일 바이너리로 PASS 했음,
  (c) 3회 연속 정확히 32.0fps — 경합 노이즈가 아니라 하드 캡.
- 그래서 규칙으로 승격했다: `CLAUDE.md` 에 "테스트 규칙(필수)" 신설 — 테스트 전 `qwinsta` 로 RDP 확인,
  접속 중이면 사용자에게 종료를 요청하고 종료 확인 후 진행, RDP 상태의 결과는 PASS/FAIL 모두 판정 근거로
  쓰지 않는다. `HANDOFF.md` 의 "테스트 환경 주의"에도 탐지 방법과 GDI 테스트 오진 방지를 연결.
- 변경 파일: `apps/native_poc/src/product_version.hpp`, `CLAUDE.md`, `docs/HANDOFF.md`,
  `docs/구현계획.md`, `docs/history.md`, `dist/GNLinkSetup-0.2.60.exe`(신규).
- 다음 액션: **RDP 끊고 실기 판정.** 중점 확인 — (1) 뷰어 재접속 시 복구(1초 틱 위치, H-10)
  (2) 정적 화면 키프레임 복구(kick 재무장, H-26b) (3) 게임 중 프리즈/IDR 연발(H-19, H-03)
  (4) 런타임 비트레이트 변경 직후 끊김(ABR hold, H-26d). DXGI 경로 항목이 많아 RDP면 판정 불가.

### 323) 2026-08-29 실기 정지 원인 규명 + 뷰어 F-20 수정 + 0.2.61 (브랜치 refactor/viewer-split, 42bfc73)

- 0.2.60 실기에서 PC 뷰어가 장시간 스트리밍 중 완전히 멎었다(조작은 됨, 자가복구 안 됨, 재접속해야 복구).
- 로그 3종(host_app / viewer / 안드로이드 진단)을 교차 분석했다. **먼저 낸 결론이 틀려 두 번 정정했다**:
  (1) 호스트 통계의 `encodedFrames=0` 구간(17:16~17:20)을 사용자가 겪은 정지로 단정했으나, 그건 **모바일이
  뒤로가기(`viewer_back`)로 대상 선택 화면에 머문** 구간이었다(안드로이드 로그가 확정). (2) "occlusion으로
  Present가 멈췄다"도 틀렸다 — `DXGI_STATUS_OCCLUDED`는 이미 성공 처리 중이고 `d3dPresentFail=0`이다.
  (3) "`paintCoalesced` 증가 = WM_PAINT 실행 중"도 틀렸다 — 그 카운터는 수신 스레드에서도 증가한다.
- 실제 정지는 PC 세션(17:20:49~17:23:56)의 `17:22:10.949` 이후. 결정적 증거는
  `d3dPresentSuccess=0` **이면서** `d3dPresentFail=0` + 모든 fallback 실패 0 → 렌더 실패가 아니라 **진입조차 안 함**.
- 원인 = **`gFrameBuf.paintQueued` 래치 고착**(뷰어 원장 F-20). `paint_video_frame()`이 `BeginPaint`
  **이전에** 래치를 해제해, 그 사이 producer의 `InvalidateRect`를 바로 뒤의 `BeginPaint`가 검증(삭제)하면
  래치는 true인데 invalid region도 pending WM_PAINT도 없는 상태가 된다. 래치를 푸는 곳이 WM_PAINT
  한 곳뿐이라 영구 고착. 코덱스 교차검증에서 **두 번째 고착 경로**(UI 스냅샷이 새 프레임을 포함하면
  말미 재확인 조건 자체가 성립하지 않음)까지 확인.
- 수정(`42bfc73`): 래치 해제를 `BeginPaint` 뒤로 · `request_video_paint()` 중앙화(동일 패턴 4곳 복제) ·
  `InvalidateRect` 실패 시 롤백 · `BeginPaint` 실패 처리 · 50ms 타이머 안전망(미표시 200ms + update region
  없음이면 강제 재요청) · `paintEnter`/`paintSelfHeal`/`invalidateFail`/`beginPaintFail` 계측.
  코덱스가 반대한 "가려지면 스트림 중지"안은 기각(캡처 churn 재도입 + H.264 참조 체인 파손).
- **RDP 규칙이 값을 했다**: `gdi_capture_process_test`가 RDP에서 32fps로 FAIL 하다가 콘솔에서
  59.98fps PASS. 코드 회귀가 아니라 RDP 어댑터 32Hz였음이 실증됐다(CLAUDE.md 테스트 규칙).
- 검증(콘솔 세션): 전 타깃 빌드 0 에러 · 단위테스트 **26/26** · host_udp_e2e 두 leg ALL PASS ·
  viewer_split_gate --e2e ALL PASS · 신규 텔레메트리 실측(paintEnter 30→120, 실패 카운터 0).
- 릴리스: product_version 0.2.60 → **0.2.61**, `dist/GNLinkSetup-0.2.61.exe`. 임베드 검증에 더해
  **payload 안에 이번 수정 심볼(paintSelfHeal/paintEnter/invalidateFail)이 실제로 들어갔는지**까지 확인했다.
  되돌리기 = `dist/GNLinkSetup-0.2.60.exe`.
- 신규 미착수 **F-21**: PC 피커가 flip-model 스왑체인에 가려 안 보임(+ 사용자 요청: PC 피커는 desktop만 표시).
- 다음 액션: 0.2.61 실기 판정 → F-21 → H-28.

### 324) 2026-08-31 원장 잔여 전부 마무리 + 0.2.62 (브랜치 refactor/viewer-split, c3678b9 / a638913)

- **F-21**(`c3678b9`) 뷰어: 대상 선택을 눌러도 피커가 안 보이고 마지막 영상이 얼어붙은 것처럼 보이던 것.
  원인은 flip-model 스왑체인(HWND 직접 바인딩)을 DWM이 창 위에 합성해 GDI 피커 오버레이가 그 아래 깔린 것.
  피커 진입 시 **스왑체인만** 해제하고 디바이스·컨텍스트는 유지한다(하드웨어 디코더가 공유 —
  통째로 해제하면 디코딩이 깨진다). 사용자 요청도 함께: PC 피커는 `kPickerListsWindows` 한 곳으로
  그리기·히트테스트·썸네일을 막아 desktop만 표시(그리기만 막으면 안 보이는 카드가 클릭됨).
- **H-24 / H-25 / H-27 / H-28**(`a638913`) 호스트 원장 잔여 + 신규 1건.
  H-24 listen 소켓을 accept 스레드가 select 틱으로 소유·자체 close / H-28 Serve 종료가 servedEpoch가
  같을 때만 스트림 차단(TCP·UDP 동시 생존 시 상대 영상을 끄던 것) / H-27 요청에 epoch 스탬프 +
  TakeForEpoch 폐기(이미 적용 중인 창은 설계상 유지, 원장 명시) / H-25 `oldestSubmittingUs=` 계측 신설.
- 회귀: `host_main_loop_mailbox_test`에 stale-epoch 4건. 기존 brace 초기화는 epoch 추가로 필드가
  밀렸으므로 전부 이름 주석으로 고정 — 앞으로 필드가 추가돼도 조용히 밀리지 않는다.
- **테스트 스윕 오류 정정**: 이전 "26/26"에는 `udp_control_e2e_test`가 포함돼 있었는데 이건 살아있는
  호스트가 필요한 e2e다. 직전 `host_udp_e2e`의 호스트가 남아 우연히 통과한 것으로 보인다. 단독
  스윕에서 제외하고 `host_udp_e2e.sh`로만 검증한다(현재 25/25).
- 검증(RDP 종료 후 콘솔): 전 타깃 빌드 0 에러 · 단위테스트 25/25 · host_udp_e2e 두 leg ALL PASS ·
  viewer_split_gate --e2e ALL PASS · gdi_capture_process_test 58.30fps PASS.
- 릴리스: 0.2.61 → **0.2.62**, `dist/GNLinkSetup-0.2.62.exe`. 임베드 버전 확인 + payload 바이너리가
  마지막 소스 변경 이후 빌드본인지 타임스탬프로 확인. 되돌리기 = `dist/GNLinkSetup-0.2.61.exe`.
- 다음 액션: 0.2.62 실기 판정. 원장 미착수 0건, 검증 부채(readback fault-injection)만 남음.

### 325) 2026-08-31 뷰어 원장 잔여 12건 → 10건 처리 + 0.2.63 (브랜치 refactor/viewer-split, d24fc31…a06ff69)

- 목표: 호스트 Phase 4 와 같은 사이클로 뷰어 원장의 잔여 항목을 처리한다. 사용자 질문("다 해야 하나")에
  "동작 영향은 F-10·F-18 둘뿐, 구조만인 것은 건너뛰자"로 답했으나 조건이 "전부"라 실행 가능한 10건은 전부
  했고, 2건(F-17 전역→ctx, F-09 이중 구현)만 사유를 적고 보류했다.
- **F-10 SyntheticRefresh**(`ffabc5f`): 호스트 kick/정적 리프레시 프레임에 헤더 bit1(UDP 청크는 bit6 — bit1 이
  firstChunk 라서)을 찍고 공용 어셈블러가 관통시킨다. 뷰어는 그 프레임을 디코드·표시하되 latency/decodeTail
  합계·혼잡 트리거·큐깊이 히스토그램에서 뺀다. **핵심은 호스트 ABR 입력(clAvgLatencyUs) 오염 방지** —
  정적 화면에서 화질을 가짜로 낮출 수 있었다. 안드로이드 JNI 는 bit0 만 마스킹해 무해 확인. 회귀 1건.
- **F-11 paced playout(P3)**(`a06ff69`): 안드로이드가 쓰는 VideoPlayoutClock 을 뷰어에 붙였다. recv 가
  presentAtUs 를 찍고 WM_PAINT 가 due 전이면 one-shot 타이머로 보류. `REMOTE60_NATIVE_PACED_PLAYOUT=1`
  opt-in, 기본 OFF(헤드룸의 지연 값어치는 실기가 말해줘야). 합성 프레임은 pacing 우회.
- F-18 혼잡 임계 4개 튜너블(기본값 불변, 회귀 2건) · F-15 커서 샘플/클라 메트릭 스냅샷화(selection gate·
  present 카운터는 검토 후 유지) · F-07 창 목록 적용을 UI 스레드로 · F-13/F-14 · 소소 5건(F-06/12/19/02/16).
- 검증(RDP 종료 후 콘솔): 전 타깃 빌드 0 에러 · 단위테스트 26/26 · host_udp_e2e 두 leg ALL PASS · viewer e2e
  ALL PASS × 2(기본 / PACED). e2e 로그: syntheticFrames 5~7/s, 기본 pacedHold=0, paced pacedHold 67~79/s.
- 릴리스: 0.2.62 → **0.2.63**, `dist/GNLinkSetup-0.2.63.exe`(임베드 버전 + payload 최신성 확인). 되돌리기 0.2.62.
- 코덱스: 검증 요청 발신(대답 없으면 진행 — 사용자 지시).
- 다음 액션: 0.2.63 실기 판정(F-20 정지 / F-21 피커 / F-10 정적화면 화질 / 선택적으로 PACED_PLAYOUT=1 체감).

### 326) 2026-08-31 뷰어 원장 마지막 2건(F-17 / F-09) 처리 + 0.2.64 (브랜치 refactor/viewer-split, 3768490 / 744b693)

- 사용자 지시("원장 전부 구현")로 #325 에서 보류했던 2건을 구현. 원장의 보류 사유 표는 처리 기록으로 대체.
- **F-17** (`3768490`): `viewer_globals.hpp/.cpp` 삭제. 신설 `viewer_state.hpp`(ViewerState = 상태 struct 10, 선언 순서 = 옛 정의 순서)를
  `ViewerContext` 가 상속 → main() 이 소유. 자유함수 55개에 `ViewerState& ctx` 첫 인자, 참조 557곳 치환. WndProc 는 `CreateWindowExW`
  lpParam → `WM_NCCREATE` 에서 `GWLP_USERDATA` 고정 + `session.hwnd` 선기록. 람다 6곳 `[&ctx]`(툴바 3·매크로 sendStep·UDP 터널 send·
  스레드 2 — 모두 ctx 보다 먼저 파괴). `ViewerContext::control` → `controlClient`(기반 `ViewerState::control` 과 충돌). 호출자 없던
  `viewer_layout.hpp` `kPanel*` inline 10개·`brush_cache()` 삭제. 동작 변화 0. 30파일 +883/−882.
- **F-09** (`744b693`): "뷰어를 컨트롤러 위에 올리기"는 기각 — TCP 비디오 경로(e2e C-3) / 커서 패킷·sim-drop·assembly 텔레메트리 /
  UI 스레드 visibleCards(F-07) / BGRA vs RGBA / 하네스가 grep 하는 로그 라인. 대신 **프로토콜 코드가 한 곳에만 있도록** 와이어 단계 3종을
  공용으로 추출해 컨트롤러·뷰어 양쪽이 호출: `udp_hello_handshake`·`fetch_window_thumbnail`(tcp_control), `make_control_input_event`/
  `enqueue_control_input_text`(shared_core). 새 파일 없음 → Android CMake 불변. 컨트롤러 동작 불변(send 실패 즉시 실패 유지).
  뷰어 변화 1건: 디렉터리 토큰을 보냈으면 `kUdpFeatureDirectoryAuth` 승인이 돌아와야 함(모바일과 동일; 전에는 검사하지 않았다).
  shared_core_test 에 builder 시나리오 추가.
- 게이트(RDP 없음, 콘솔): 전 타깃 빌드 0 에러 · 단위테스트 26/26 · viewer e2e C-1/C-2/C-3 ALL PASS(F-17 후, F-09 후 각각).
- 릴리스: 0.2.63 → **0.2.64**, `dist/GNLinkSetup-0.2.64.exe`(임베드 버전 + payload 최신성 확인). 되돌리기 0.2.63.
- 코덱스: 설계(seq 837)·구현 diff(seq 838) 검증 요청 발신, delivered=1(onCall 꺼짐 → 우편함 대기). 답이 오면 후속 커밋으로 반영.
- 이로써 뷰어 원장 F-01~F-21 **전부 처리**, 호스트 원장 H-01~H-28 전부 처리. 다음 액션: 0.2.64 실기 판정(0.2.63 항목 + 디렉터리
  경로 접속 1회 — DirectoryAuth 검사 확인).

### 327) 2026-09-01 디렉터리 서비스를 네이버클라우드 → 집 NAS(CHO)로 이전 + 릴레이 상시화 (브랜치 refactor/viewer-split)

- 목표: 랑데부/릴레이 서버를 자체 인프라로 옮기고, 흔한 포트(8080/8081) 노출을 피한다. 릴레이는 기본 포함(직접 경로 우선, 실패 시 전환).
- 변경 파일: `automation/deploy_directory.ps1` — 릴레이 파라미터 6개 신설(`-RelayEnabled/-RelayIp/-RelayPort/-RelayAllowIps/-RelayAllowAccounts/-RelayGraceMs`)
  + systemd 유닛에 `REMOTE60_RELAY_*` 환경변수 6줄 추가 + `-RelayEnabled`인데 `-RelayIp`가 비면 즉시 실패. **이전에는 릴레이 설정을 유닛에 넣을 방법이 없어
  수동으로 넣어도 다음 배포에 사라졌다.**
- 서버 이전: `gnlink@192.168.0.6`(CHO, Ubuntu 6.8, sudo 없는 전용 계정) → user systemd + linger, 설치 경로 `/opt/gnlink/remote60-directory`,
  Node v20.18.1 자동 설치. **포트 8080/8081 → TCP 29180 / UDP 29181**(관측 포트는 클라가 `HTTP+1`로 계산하므로 연속이어야 함), 릴레이 **UDP 29190**.
  29180을 고른 근거: 리눅스 ephemeral 대역(32768~60999) 아래여야 리스닝 포트가 아웃바운드 소스포트와 충돌하지 않는다.
- 릴레이: `ENABLED ip=175.209.236.194 port=29190 grace=2500ms allowIps=* allowAccounts=*`. 허용목록은 **IP·계정 둘 다 통과해야** 하므로(`server.js:523~525`)
  양쪽을 `*`로 뒀다(모바일이 임의 회선에서 붙어야 함). grace 2.5초라 직접 경로가 되는 환경에서는 릴레이가 채택되지 않는다.
- 데이터 이전: 구 서버(`223.130.132.180`, user systemd)의 `directory-data.json`(계정 1·호스트 1, 1781B) 백업 후 NAS로 복사, 서비스 재기동.
- 검증: `healthz {"ok":true}` · `systemctl --user is-active` = active · 리스너 3개 확인(TCP 29180 / UDP 29181 / UDP 29190) ·
  로그에 `[relay] ENABLED`, `[wake] on`, `[directory] udp observe on 29181`. **LAN(`192.168.0.6:29180`) 접속 성공.**
- **미완**: 공유기 포트포워딩이 아직 없어 공인 IP(`175.209.236.194:29180`)로는 도달하지 않는다 → 사용자 작업 대기. TCP 443은 포워딩+헤어핀이 동작 중이므로
  라우터 자체는 헤어핀을 지원한다(이 PC → 집 공인 IP:443 established 4건으로 확인).
- 다음 액션: 공유기 포워딩 3개(TCP 29180 / UDP 29181 / UDP 29190, 외부·내부 동일 번호) → 공인 IP로 healthz + UDP observe 재검증 →
  호스트/뷰어 디렉터리 URL을 `http://175.209.236.194:29180`으로 변경 → 뷰어의 관측 포트 하드코딩(8081) 제거(`directory_session_bootstrap.hpp:32`)가 선행돼야 뷰어가 붙는다.
- 구 서버(네이버클라우드)는 롤백용으로 살려 둔다.

### 328) 2026-09-01 뷰어의 관측 포트 하드코딩 제거 + NAS 이전 검증 (브랜치 refactor/viewer-split)

- 문제: `DirectorySessionRequest::directoryUdpPort = 8081` 고정이라 디렉터리 HTTP 포트를 옮기면 **윈도우 뷰어만** 관측 포트를 못 따라갔다.
  호스트(`directory_client.cpp:233` `httpPort+1`)와 안드로이드(`DirectoryClient.kt:55` `httpPortFor(url)+1`)는 이미 URL에서 유도하고 있었고,
  8080+1=8081이라 우연히 맞아떨어졌을 뿐이다.
- 수정: 기본값을 `0`(= URL에서 유도)으로 바꾸고, `directory_session_bootstrap.cpp`가 손으로 하던 호스트 파싱을 공용 `directory::parse_directory_url`로
  교체해 호스트와 포트를 한 번에 얻는다. 호출자가 포트를 명시하면(안드로이드) 그 값이 그대로 우선한다.
- 검증: 뷰어·호스트·`remote60_directory_session_client_test` 빌드 exit 0, 단위 테스트 PASS. (RDP 접속 중이라 캡처·fps 계열 테스트는 규칙대로 돌리지 않음)
- NAS 이전 실측(공유기 포워딩 완료 후):
  - `TCP 29180` 공인 IP 경유 `HTTP 200 {"ok":true}` — 포워딩·헤어핀 정상
  - `UDP 29181` OBSERVE 왕복 성공. **다만 공인 IP 경유 시 서버가 관측한 주소가 `192.168.0.1`(공유기 LAN 게이트웨이)** — 이 공유기는 헤어핀에서
    출발지를 SNAT 한다(포트 60765는 보존). LAN 직접 경유는 `192.168.0.76`.
  - 결과: **집 안의 호스트는 이 디렉터리로 자기 공인 매핑을 배울 수 없다.** 사설 후보만 광고하게 되어 외부 클라이언트는 릴레이로만 붙는다.
    목표 케이스(회사 wifi → 회사 PC)는 양쪽이 집 밖이라 영향 없음.
- 다음 액션: (a) 서버가 사설 관측값을 자기 공인 IP로 치환(포트 유지)하는 보정을 넣을지 결정 — E2 실측에 "이 공유기는 NAT 포트를 보존한다"가 있어
  성립 가능성이 높다 · (b) 호스트/뷰어 디렉터리 URL을 `http://175.209.236.194:29180`으로 전환 · (c) 전환 후 43000/43001 포워딩 제거 가능 여부 판정.

### 329) 2026-09-01 디렉터리 서버: 자기 LAN에서 온 관측을 자기 공인 주소로 보정 (브랜치 refactor/viewer-split)

- 문제: 디렉터리를 집 NAS로 옮기자 **집 안의 호스트가 자기 공인 매핑을 배울 수 없게** 됐다. 관측 패킷이 NAT를 건너지 않기 때문이고,
  공인 IP로 우회해도 이 공유기는 헤어핀에서 출발지를 자기 LAN 주소로 SNAT 한다 — 실측값 `192.168.0.1`. 그 값이 그대로 호스트의 `public` 후보로
  광고되면 외부 클라이언트는 아무도 못 쓰고 릴레이로만 붙는다. 43000 포워딩이 있어도 소용없다(후보 주소 자체가 틀리므로).
- 수정 (`apps/directory/server.js`): 관측 출발지가 **서버 자신이 붙어 있는 서브넷**이면(`os.networkInterfaces()`로 계산) 기록·응답의 IP를
  `REMOTE60_PUBLIC_IP`(미설정 시 `REMOTE60_RELAY_IP`)로 치환하고 **포트는 관측값 그대로** 둔다. 응답 패킷 자체는 실제 출발지로 보낸다.
  관측 맵은 호스트 등록(`host.publicIp/publicUdpPort`)과 클라이언트 펀치 대상 양쪽에 쓰이므로 한 곳 수정으로 둘 다 고쳐진다.
- 포트 보존 가정의 근거: `구현계획.md` E2(이 공유기는 `bindPort=43000` → `public=...:43000`으로 포트를 보존) + 이번 헤어핀 프로브에서 소스 포트
  59178이 그대로 돌아온 것. 성립하지 않는 라우터에서는 릴레이로 떨어지는데, 그건 보정이 없을 때도 가던 길이라 더 나빠지지 않는다.
- 루프백은 `internal` 인터페이스라 보정 대상이 아니다 — 기존 테스트가 계속 실제 출발지를 관측한다.
- 테스트: `apps/directory/test/observe_correction_test.js` 신설(자기 LAN 출발 → 공인 IP로 치환 / 포트 보존 / 루프백 불변) + `test/run.js`에 단계 추가.
  `node test/run.js` **ALL PASS**(기존 전 케이스 + 신규 3건).
- 실기 확인(NAS 재배포 후): 공인 경유 `{"ip":"175.209.236.194","port":59178}`, LAN 직접 `{"ip":"175.209.236.194","port":59179}` — 둘 다 보정됨.
  서버 로그에 `[observe] 192.168.0.1 is on our own lan; reporting 175.209.236.194 instead (port 59178 kept)`.
- 다음 액션: 호스트/뷰어 디렉터리 URL을 `http://175.209.236.194:29180`으로 전환 → 회사에서 두 케이스 실기(회사→회사PC, 회사→집PC) →
  결과 보고 43000 포워딩 제거 가능 여부와 N13(펀칭 개선) 착수 판단.

### 330) 2026-09-01 디렉터리 서버에 로그 수집 엔드포인트 신설 — POST /api/logs (브랜치 refactor/viewer-split)

- 목적: 호스트·클라이언트·APK 로그를 사람이 세 대에서 복사해 나르는 대신 한자리로 모은다. 특히 APK 로그는 지금 사실상 접근이 안 된다.
- 설계 판단: 새 포트로 raw TCP를 여는 대신 **이미 열린 HTTP(29180)에 엔드포인트 추가**. 포트·방화벽·포워딩이 늘지 않고, 디렉터리의 기존 토큰 체계를
  그대로 인증에 쓸 수 있다(새 비밀값 관리 불필요). 본문은 JSON이 아니라 **raw text**(줄 단위이므로 양쪽에서 escape 왕복이 낭비).
- `apps/directory/server.js`: `POST /api/logs` — 세션 토큰(`Authorization: Bearer`) 또는 호스트 토큰(`x-host-token`)으로 인증,
  `x-log-device`/`x-log-stream` 헤더로 분류, `logs/<계정>/<기기>/<스트림>.log`에 append. 경로 세그먼트는 `[A-Za-z0-9._-]` 외 전부 `_`로 치환하고
  선행 점을 제거해 상위 탈출을 막는다(거부가 아니라 평탄화 — 이름이 이상한 기기도 로그는 남아야 하고, 조용히 실패하면 나중에 찾을 때 없다).
  요청당 512KB 상한, 기기당 분당 예산(기본 2MB) 초과 시 429, 16MB에서 `.1~.3` 로테이션, 14일 지난 파일은 6시간 주기 sweep으로 삭제.
  환경변수 `REMOTE60_LOG_DISABLED/DIR/MAX_FILE_MB/KEEP/RETENTION_DAYS/RATE_KB_PER_MIN`.
- 테스트: `apps/directory/test/logs_test.js` 신설 7건 — 무토큰 401 · 세션 토큰 저장 · 경로 배치 · append · **경로 탈출 평탄화** ·
  플러딩 429(9/12) · 예산은 기기별이라 다른 기기 무영향. `test/run.js`에 전용 서버 단계 추가(스크래치 로그 디렉터리 + 8KB/min 예산).
- 겸사: 러너의 `cleanup()`이 relay·observe 단계 직후에 계정 저장소를 지워 뒤 단계가 로그인할 수 없었다. 최종 단계로 옮겼다.
- 검증: `node test/run.js` **ALL PASS**(기존 전 케이스 + 관측 보정 3 + 로그 7). NAS 재배포 후 기동 로그 확인 —
  `[logs] collecting into /opt/gnlink/remote60-directory/logs; 2048KB/min per device, rotate at 16MB x3, keep 14d`.
- 다음 액션: C++ 업로더(바운드 큐 + 전용 스레드 + 배치 POST)를 만들어 `client_shell_main.cpp`의 `viewer_log_write_line`과 `host_app_main.cpp`의
  자식 stdout 리더에 연결 — 엔진은 stdout으로만 찍고 셸이 파일에 쓰는 구조라 **깔때기 두 곳만 손대면 전부 잡힌다.** 그 다음 안드로이드.
- 주의(기록): 디렉터리는 TLS가 꺼져 있고 클라이언트가 `https://`를 거부한다. 로그에는 창 제목·프로세스명·IP가 들어가므로 **평문으로 공용망을 건넌다.**
  상시 켜기보다 진단 시 켜는 옵션으로 두고, 상시화는 TLS 지원 이후로 미룬다.

### 331) 2026-09-01 로그 원격 수집 완성 — 윈도우 셸 2종 + APK → 서버, 0.2.65 / APK 0.2.11 (브랜치 refactor/viewer-split)

- 목표: 호스트·클라이언트·폰 로그를 사람이 옮기지 않고 디렉터리 서버 한자리에서 읽는다. 서버측(#330)에 이어 클라이언트측 완성.
- 신설 `apps/native_poc/src/log_upload.{hpp,cpp}`: 프로세스 전역 싱크. 바운드 큐(기본 4MB) + 전용 워커 스레드 + 2초/192KB 배치 POST.
  `Enqueue`는 뮤텍스 잡고 push 후 즉시 반환 — **네트워크를 기다리지 않는다.** 큐가 차면 **앞에서** 버린다(넘치는 로그에서 볼 값어치가 있는 건 끝부분).
  전송 실패는 재시도 없이 카운트만 — 디스크 사본이 정본이므로. `REMOTE60_LOG_UPLOAD=0`으로만 끈다.
- `directory_client`의 비공개 `post_json`을 `http_post(host,port,path,contentType,extraHeaders,body,...)`로 일반화해 헤더에 공개
  → HTTP 클라이언트를 두 벌 만들지 않았다. 기존 호출부는 얇은 `post_json` 래퍼로 그대로 유지.
- 연결 지점은 **깔때기 두 곳뿐**(엔진 코드 무수정): `client_shell_main.cpp`의 `log_line`(client)·`viewer_log_write_line`(viewer),
  `host_app_main.cpp`의 `ReadChildOutput`(host). 인증은 각 셸이 이미 들고 있는 토큰 재사용 — 클라는 세션 토큰, 호스트는 hostToken.
  호스트는 로그인 시점과 **캐시 로드 시점** 양쪽에서 시작한다(이미 로그인된 호스트는 다시 로그인하지 않으므로 후자가 일반적).
- 안드로이드: `LogUploader.kt` 신설(같은 정책: 512KB 큐·3초 배치·드롭 카운트), `SessionDiagnosticsLog.log()`에 한 줄 연결,
  로그인/세션 복원 양쪽에서 `configure`. 스트림 이름 `apk`, 기기명은 모델+ANDROID_ID 앞 6자.
- 검증:
  - `node apps/directory/test/run.js` ALL PASS (#330의 로그 7건 포함)
  - 빌드: `remote60_host_app`/`remote60_client_shell`/설치본 exit 0, `./gradlew assembleDebug` BUILD SUCCESSFUL
  - **공인 IP 경유 왕복 실측**: 임시 계정으로 `POST http://175.209.236.194:29180/api/logs` → `{"ok":true,"bytes":46}`,
    서버에 `logs/logprobe/winpc-probe/viewer.log` 생성·내용 일치 확인. 검증 후 임시 계정과 그 로그 삭제(남은 계정 `shotan`).
  - `automation/viewer_split_gate.sh --e2e` — 빌드 + 뷰어 e2e C-1/C-2/C-3 ALL PASS
- 산출물: `dist/GNLinkSetup-0.2.65.exe`(임베드 검증 `L"0.2.65"` ×3 / `L"0.2.64"` ×0), `dist/GNLink-0.2.11.apk`(versionCode 10, assembleDebug).
- 미검증: 실제 GNLinkClient/GNLinkHost가 로그인한 상태의 업로드는 계정 비밀번호가 필요해 확인하지 못했다 — 설치 후 첫 로그인 시
  `client.log`에 `log upload on device=... -> 175.209.236.194:29180`이 남는지로 판정한다.
- 주의: 로그에 창 제목·프로세스명·IP가 들어가는데 디렉터리는 TLS가 꺼져 있어 **평문으로 공용망을 건넌다**(#330 동일). 상시화는 TLS 이후.
- 다음 액션: 새 설치본·APK 설치 → 호스트/뷰어/폰의 디렉터리 URL을 `http://175.209.236.194:29180`으로 → 회사에서 두 케이스 실기
  (회사wifi→회사PC, 회사→집PC). 로그는 이제 서버에서 바로 읽는다.

### 332) 2026-09-01 광고 주소 / 송신 주소 분리 — 코덱스 검증(A 승인·B High 2건·C Low 2건) 반영 (브랜치 refactor/viewer-split)

- 계기: 회사 PC → 집 호스트 실기(17:07)에서 43000 포워딩 없이 실패. 서버 저널 `[wake] tx host=175.209.236.194:43000`, `[relay] closed reason=no HelloAck c2h=39 h2c=0` —
  #329의 관측 보정이 `host.publicIp`를 WAN으로 덮어쓰면서 **서버 자신의 송신(wake·릴레이 host leg)까지** 집 공인 주소로 나갔다. 같은 LAN 안에서는 포워딩 없이 안 닿는다.
- 수정(`apps/directory/server.js`): 관측 맵에 `wireIp/wirePort`(실제 rinfo) 병행 저장 → 하트비트가 `host.wireIp/wirePort`로 → wake(콜백 안 late-binding)와 릴레이 host leg가 wire 튜플 사용.
  **광고(`connectCandidatesFor`, `hostPublicIp`)는 그대로 WAN.** 호스트가 집 밖이면 두 값이 같아 무변화. 사설 IP를 광고하는 것이 아니라 **받은 패킷의 출발지로 답하는 것**이다.
- 발견: HEAD의 wake는 setTimeout 콜백 안에서 `host.publicIp`를 매번 읽어 사이에 온 하트비트를 따라간다. 호출 시점 캡처로 바꾸자 `directory_test` wake 케이스가 결정적으로 실패(HEAD 2/2 통과, 캡처판 2/2 실패) → late-binding 복원. 코덱스 판정: 의도된 동작이 맞고 테스트로 명시할 것.
- 코덱스 검증(remote#ptyr3fi8): A(#329) 승인 · B 설계 승인, 배포 전 High 2건 · C(#328) 승인, Low 2건 · "직접 펀칭 보장" 기각 · "포워딩 없는 릴레이 성공 가능성" 동의.
  서버→호스트 송신에 publicIp 잔존 없음(전수), wake·릴레이 c2h 모두 observe 소켓(`startUdp` 반환 소켓)에서 나감 확정.
- High 1 → `test/same_lan_test.js` 신설(11건): 호스트를 이 PC의 LAN IP에 bind, `REMOTE60_PUBLIC_IP=203.0.113.9`(아무도 안 받는 문서용 주소)로 광고/송신을 **일부러 다르게** 만든다.
  광고 후보는 203.0.113.9 · wake는 wire로 도착(observe 소켓 출발) · 릴레이 Hello가 wire로 도착·Ack 왕복 · **호스트가 소켓을 옮긴 뒤** 클라 미디어가 새 튜플로, 호스트 미디어가 클라로, 다음 wake도 새 튜플로.
- High 2 → `relayFollowHostWire()`: 인증된 하트비트가 wire 튜플 변화를 감지하면 활성 릴레이 세션의 `hostIp/hostPort`를 갱신하고 `relaySessionByHost` 인덱스를 unindex→reindex(충돌 세션은 drop). 코덱스 권고 A안. 단순 패킷별 late-binding은 인바운드 인덱스가 못 따라가 h2c=0이 지속되므로 채택하지 않음.
- Low: 재등록 시 `wireIp/wirePort` 보존(이전엔 public만 보존해 첫 하트비트 전 wake가 public 폴백) · wake가 콜백에서 목적지가 바뀌면 `[wake] … tx moved to` 기록 ·
  뷰어 `directory_session_bootstrap.cpp`: http 포트 65535면 관측 포트가 0으로 wrap → 명시 오류로 거부 · `test/run.js`의 stage 간 `cleanup()` 순서(로그 단계 뒤) 정리.
- 문구 정정(코덱스 Q2): "보정 전보다 나빠지지 않는다"는 **릴레이 자격이 있는 클라에 한정**해서만 성립한다(allowlist 밖·릴레이 off·4초 예산 초과·레거시 클라는 폴백 보장 없음). 절대 보장 표현 금지.
- 검증: `node apps/directory/test/run.js` **ALL PASS**(기존 + 관측 보정 3 + 로그 7 + same-lan 11) · 뷰어·`remote60_directory_session_client_test` 빌드 exit 0, 테스트 PASS.
- 실기 게이트(코덱스 제시, 직접 펀칭 성공이 아니라 이걸 먼저 본다): 서버 저널 `[relay] bound … host=192.168.0.1:43000` · `h2c>0` · HelloAck auth bit · state=active · 양방향 미디어.
- 미처리: 클라이언트 업로드 로그 타임스탬프(사용자 중단으로 미적용) · 안드로이드 관측 포트 65535 overflow(Low) · https는 C++ 거부/안드로이드 허용으로 규칙이 다름(문서 문구만).
- 남은 리스크(코덱스 Q5, 그대로 기록): 집 공유기가 destination-dependent 매핑이면 관측 포트≠회사향 매핑 포트 · 회사 NAT의 endpoint-dependent 필터/CGN · wake→관측(최대 6×250ms)+하트비트 지연이 릴레이 grace보다 늦으면 토큰 전 Hello 도착 · 4초 예산 대비 grace 2.5초 여유 1.5초 · 헤어핀 매핑 timeout < 하트비트 25초.

### 333) 2026-09-01 업로드 로그 타임스탬프 + 관측 포트 overflow(안드로이드) — 0.2.66 / APK 0.2.12 (브랜치 refactor/viewer-split)

- 문제: #331의 업로드 훅이 셸의 스탬프 생성 **앞**에 걸려 서버에 도착한 `client.log`/`viewer.log` 줄에 시각이 없었다. 서버 저널과 대조할 때 필요한 유일한 필드라 사실상 못 쓴다.
- 호스트는 해당 없음: `GNLinkStream`이 `host_log.hpp`의 streambuf에서 매 줄 `timestamp_now()`를 붙여 stdout으로 내보내므로 `ReadChildOutput`이 받는 줄에 이미 시각이 있다.
  안드로이드도 `SessionDiagnosticsLog.log()`가 스탬프를 넣은 뒤 enqueue한다. **윈도우 셸 두 곳만** 문제였다.
- 수정(`client_shell_main.cpp`): `log_line`/`viewer_log_write_line` 모두 스탬프를 먼저 만들고 `stamp + line`을 enqueue. 파일이 안 열려도 업로드는 되도록 스탬프·enqueue를 sink 검사 **앞**으로 옮겼다.
  파일 포맷은 불변(client: 초 단위, viewer: ms 단위 — 기존 그대로).
- 코덱스 Low: 안드로이드 `DirectoryClient.observePortFor`가 http 포트 65535면 0을 돌려주도록(뷰어 C++ 가드와 짝). https 규칙 차이(C++ 거부 / 안드로이드 443→444)는 문서 기록으로만.
- 검증: 클라 셸·설치본 빌드 exit 0, `assembleDebug` BUILD SUCCESSFUL, `viewer_split_gate.sh --e2e` 빌드 + 뷰어 e2e C-1/C-2/C-3 ALL PASS.
  스탬프가 실제로 업로드되는지는 계정 로그인이 필요해 미확인 — 다음 실기의 서버 `client.log` 첫 줄로 판정(`09-01 HH:MM:SS log upload on …` 형태여야 함).
- 산출물: `dist/GNLinkSetup-0.2.66.exe`(임베드 `0.2.66`×3 / `0.2.65`×0), `dist/GNLink-0.2.12.apk`(versionCode 11).
- 실기 대상 정리(최신본): 호스트 `0.2.66`(집 PC) · 클라 `0.2.66`(회사 PC) · APK `0.2.12`. 서버는 #332 배포본 그대로.

### 334) 2026-09-01 무포워딩 릴레이 접속 실기 PASS + 업로더 진단 계측 — 0.2.67 (브랜치 refactor/viewer-split)

- **실기 결과(기록)**: "집 호스트 UDP 43000 포워딩 제거 상태에서 회사 PC→집 호스트 세션이 NAS UDP 릴레이(29190)로 active가 되어 66초 양방향 영상 전송에 성공"(코덱스 판정 문구).
  서버 저널: `[wake] tx host=192.168.0.1:43000(advertised 175.209.236.194)` → `[relay] bound host=192.168.0.1:43000` → `active; helloAck in 1ms` → 종료 시 `c2h=45/4853B h2c=4405/5175373B`.
  지난 `h2c=0` 결함(#332 이전)이 wire 분리로 해결됨을 실기로 확인. **단서**: 직접 P2P/홀펀칭이 아니라 릴레이 경유이고, NAS 공인 진입점(TCP29180/UDP29181·29190)은 여전히 필요. "직접 경로 영구 불가"는 미증명.
  후속 게이트(코덱스): 재접속 반복 · 10분+ idle→resume(헤어핀 timeout·25초 하트비트) · 여러 회차 · 릴레이 cleanup.
- **버그 발견**: 업로드 로그가 첫 배치 뒤 멈춤. 서버 host.log가 18:11:58에 얼어붙은 채 로컬 host_app.log는 18:31까지 계속 자람(20분). 외부 요인 전수 배제 —
  헤어핀 POST 10/10, 인증 POST 50/50, 서버 429·오류 0. enqueue는 계속 호출되고(로컬 자람) worker는 `stop` 없이 종료 불가한데도 업로드만 멈춤 → **근본 문제는 조용한 실패 경로에 관측이 전혀 없다는 것**(업로더가 셸 프로세스 안에서 돌아 자체 stdout/stderr가 아무 데도 안 잡힘).
- 조치(`log_upload.cpp`): `%LOCALAPPDATA%\GNLink\log_upload.diag`에 자체 트레이스 추가 — worker 시작/종료, 배치 flush(첫 배치·20배치마다), **전송 실패 시 도달여부+status+host**, idle 생존(60초마다). 항상 켜짐, 한 줄/flush 수준이라 무해.
  이건 진단 계측이지 근본 수정이 아니다. GNLinkHost가 관리자 권한이라 실행 중 바이너리 버전을 비특권에서 못 읽어(경로/버전 빈값), 어느 빌드가 재현 중인지도 관측 없이는 불명이었다.
- 빌드: 호스트·클라·설치본 exit 0, viewer gate PASS. 산출물 `dist/GNLinkSetup-0.2.67.exe`(임베드 0.2.67×3/0.2.66×0). APK는 이 버그와 무관(별도 Kotlin 업로더)이라 0.2.12 유지.
- 다음: 0.2.67 설치 후 재접속 → 로컬 `log_upload.diag`(집 호스트는 내가 직접 읽음)로 첫 배치 후 send FAILED status가 찍히는지(→ http_post 반복 실패) vs idle alive만 찍히는지(→ enqueue/worker) 판정 → 그 결과로 실제 수정.

### 335) 2026-09-02 "RDP 끊은 뒤 잠금 못 풀고, OSLink로 푼 뒤 GNLink가 멈춤" 원인 규명 (브랜치 refactor/viewer-split, 문서만)

- 사용자 보고: RDP 종료 → 호스트 잠김 → GNLink로는 못 풀고 OSLink로 풀어 OSLink 종료 → GNLink 접속 → 쓰다가 화면 정지. "항상 이랬다".
  질문 3개: PC 뷰어 잠금해제 버튼은 언제 / 정지는 리팩터 회귀냐 / 빠른 복구(호스트 재갱신)는 언제.
- 로그 3종 교차(NAS `logs/shotan/8ec6…/host.log`, `68f7…/viewer.log`, 호스트 `%ProgramData%\GNLink\secure_input.log`). **오늘(09-02)은 세션이 없고**, 해당 사건은 09-01 18:41~18:49.
- 타임라인(호스트 시각):
  - 18:41:21 접속(RDP 중, DXGI 2236x1232 정상) → **18:41:31 RDP 종료**: `duplication_recreate 0x887A0026` → `CreateForMonitor failed` ×4(끊긴 세션엔 모니터가 없다) → 뷰어 `secure-desktop-active=1`(잠금 보고는 됨).
  - 18:41:38~46 뷰어가 잠금 화면에 키 35개 전송 → `secure_input.log`: `target session=1 source=requester (console=5)` · `attach=ok journal=ok` · **`inject FAILED at SendInput err=5` 전부**. 호스트 통계는 `secureInputDelivered=35`로 성공 보고(U5 그대로).
  - 18:42:13 재접속(OSLink로 해제 후): `capture-size 2236x1232→1920x1080`, **`fallback_reason=dxgi_select_no_usable_output`**(GPU 출력에 데스크톱 없음) → WGC. 이후 30초마다 승격 재시도 실패(`captureRestarts` 4→25).
  - 18:43:29~18:49:51 본 세션: **WGC `callbackFrames` 30초당 2~9장**(실질 0.1~0.3fps), 그동안 `inputEvents` 165→1012(사용자는 조작 중) → 18:46:22 이후 입력 0(포기). 뷰어는 `d3dPresentSuccess` 증가·`paintEnter` 정상 = 렌더/F-20 아님. 인코더 `encodedFrames==callbackFrames` = 인코더/송신 아님. `trailingKickCount=246` = 호스트는 같은 정지 프레임을 계속 재전송하고 있었음.
- 현재 디스플레이 실측(RDP 중): `Virtual Display Driver`(OSLink VDD by MTT) **1920x1080@60 Active**, `Microsoft Remote Display Adapter` 2236x1232@32 Active, `AMD Radeon` 물리 출력의 모니터 `Generic Monitor (2777M)` **Present=False**(물리 모니터 꺼짐/미검출). OSLink `ldremote.exe`(세션 1, 08-29부터) · `ldremoteevent.exe`(**콘솔 세션 6**) · `capture.exe`(세션 1) 상주.
- 결론:
  1. **잠금 해제 실패 원인**: RDP를 끊으면 호스트 세션 1은 *끊긴(disconnected)* 세션이고 콘솔에는 별도 로그온 세션(5/6)의 LogonUI가 뜬다. 우리 SYSTEM 에이전트는 `resolve_target_session`이 requester(세션 1)를 우선해 **끊긴 세션의 Winlogon**에 SendInput → `err=5`. OSLink는 이벤트 에이전트를 **콘솔 세션**에 두고(실측 세션 6) 거기서 로그온시켜 세션 1을 콘솔로 되붙인다. 즉 잠금해제는 콘솔 세션 LogonUI를 쳐야 한다. 토큰 경로는 이미 SYSTEM 토큰+`TokenSessionId`라 콘솔 세션 생성 가능.
  2. **정지 원인**: 물리 모니터가 없어서 OSLink 해제 후 데스크톱이 **OSLink 가상 디스플레이(VDD by MTT) 위에만** 있다(`dxgi_select_no_usable_output`이 그 증거). OSLink를 끄면 그 VDD의 소비자가 없어져 DWM 합성이 거의 멈추고 WGC에 프레임이 안 온다. 코덱/전송/뷰어 회귀 아님. 승격 재시도 루프(`6d0c418`, 08-06)도 리팩터 이전 것. **리팩터 회귀 근거 없음** — 단 동일 시나리오 A/B(0.2.57 vs 0.2.67)는 안 했다.
  3. **"호스트가 재갱신해 다시 보내면 되지 않나"**: 그 층(세션 barrier IDR·RequestKeyframe 6종·trailing kick·2fps static refresh)은 이미 있고 어제도 동작했다(kick 246회). 소스(WGC)가 새 픽셀을 못 받으면 같은 정지 화면을 IDR로 다시 보낼 뿐이라 이 정지는 **캡처/디스플레이 층**에서 풀어야 한다.
  4. PC 뷰어 잠금해제 UI: **미구현**. 호스트 플래그(`kCaptureFlagSecureDesktopActive`)는 오고 있고 PC 뷰어는 로그 한 줄만 찍는다(`viewer_control_client.cpp:62`). 안드로이드만 U6 오버레이가 있고 "[ ] 실기 확인" 미완.
- 제안 순서(미착수): (P1) 요청 세션이 `WTSDisconnected`면 콘솔 세션을 타겟 + 주입 결과 ack(U5) → (P2) PC 뷰어 툴바에 잠금해제 버튼: 잠금 플래그 또는 N초 무프레임이면 표시, 호스트별 저장(DPAPI), Enter→비번→Enter → (P3) 호스트가 "GPU 출력 없음+WGC 기아"를 pong 플래그로 알리고 부착된 출력의 어댑터로 DXGI 재시도; 장기적으로 자체 가상 디스플레이. 즉시 완화: 호스트 물리 모니터 켜두기(또는 더미 플러그).
- 변경 파일: `docs/history.md`만. 코드 변경·빌드·테스트 없음(RDP 접속 중이라 테스트 불가 규칙).
- 다음: 사용자 결정 — P1→P2→P3 순 착수 여부, 회귀 A/B 필요 여부.

### 336) 2026-09-02 헤드리스 정지 진단 계측 + 가상 디스플레이 설계 확정 — 0.2.68 (브랜치 refactor/viewer-split)

- 배경(#335 후속): 사용자 요청 "가상 디스플레이 만들어 물리 없으면 그걸 쓰게". 설계 확정: **물리 출력 우선(물리 모니터 → RDP 어댑터 → 가상), 가상은 헤드리스 폴백**. 호스트는 이미 "있는 출력을 골라 캡처"라 물리 우선은 기존 동작 그대로.
- **작업을 바꾼 발견**: 어제 정지 순간 이 PC엔 **이미 가상 디스플레이가 있었다** — OSLink가 깐 MikeTheTech VDD(`ROOT\DISPLAY\0001`, `mttvdd.inf`, SignPath Foundation 서명, 테스트서명 OFF에서 정식 로드). 데스크톱이 실제로 그 위 1920x1080으로 옮겨갔는데도(`capture-size-updated new=1920x1080`) 우리 DXGI가 `dxgi_select_no_usable_output` → WGC → 30초당 2~9장. 즉 "가상 디스플레이 존재"만으로는 안 고쳐진다(있었는데 굶었다).
- 유력 가설: **소비자 없는 유휴 indirect display는 합성이 거의 안 됨.** OSLink 켜짐=OSLink가 그 VDD를 소비→합성 돌아 우리 캡처도 60fps(오늘 `DesktopUpdateRate` 실측 idle 4/s, 동작 시 25~60/s). OSLink 꺼짐=소비자 없음→합성 정지→DXGI/WGC 둘 다 기아. 확정하려면 헤드리스(RDP off + OSLink off)에서 재현 필요.
- 조치(계측만, `libs/capture/src/capture_backend_dxgi.cpp`): `resolve_output`의 no_usable_output 폴백에서 **전체 토폴로지를 덤프**. `dxgi_no_usable_output reason=.. deviceLuid=.. outputs=N` + 출력별 `dxgi_output adapter=".." luid=.. name=.. extent=WxH attached=0/1 portrait=0/1 onDeviceAdapter=0/1`. 지금은 이유 코드 한 줄뿐이라 "VDD가 detached(extent 0)라 걸러졌나 / 다른 어댑터라 못 봤나"를 구분 못 한다. 이 경로는 이미 실패한 지점이라 **동작 무변경, 순수 진단**. 코드베이스 교훈("성공/실패 경로에 계측 없으면 원인 안 보인다") 그대로.
- 가상 디스플레이 서명/비용 결론(#335 연장): IddCx는 **사용자 모드(UMDF)** 라 커널 드라이버용 Microsoft attestation(EV 인증서, 연 수십만원) 벽에 안 걸린다. 일반 코드서명이면 되고, 오픈소스는 SignPath로 무료. **MikeTheTech VDD(MIT)가 이 PC에서 정식 서명으로 동작 중**이 산증거. 제품은 이걸 번들·설치하면 서명 비용 0. 자체 드라이버 작성 시에만 유료.
- 빌드: `remote60_installer` Release exit 0. 임베드 확인 GNLinkHost.exe / GNLinkSetup.exe 둘 다 **0.2.68**(UTF-16 스캔). 산출물 `dist/GNLinkSetup-0.2.68.exe`(직전 0.2.67 보존). **RDP 접속 중이라 캡처/성능 판정 안 함**(CLAUDE.md) — 빌드 컴파일 확인까지만.
- 다음 액션: 사용자 헤드리스 테스트(RDP off + OSLink off, 우리 세션 접속) → NAS host.log의 `dxgi_no_usable_output`/`dxgi_output` 라인으로 VDD가 왜 걸러졌는지 확정 → 그 결과로 실제 수정 결정(가상 디스플레이 attach-primary 강제 / 캡처가 소비자 역할 / 자체 VDD 번들+설치+구동 중 택).

### 337) 2026-09-02 실기 재현: 고화질 영상 켜자 정지 — 캡처는 정상, 키프레임 폭주/ABR 미반응 death spiral (브랜치 refactor/viewer-split, 진단만)

- 사용자 실기(0.2.68 설치 확인): 헤드리스(물리 모니터 없음, RDP off, OSLink는 클라우드 연결 유지)에서 GNLink 접속 후 고화질 영상 켜자 12:02:22 직후 정지. **이건 어제(#335, WGC 기아)와 다른 정지.**
- 로그 교차(로컬 host_app.log 0.2.68 / NAS viewer.log):
  - **캡처는 정상**: 내내 `desktop_backend=dxgi`, 0.2.68 신규 진단(`dxgi_no_usable_output`) **미발화** = DXGI 선택 실패 없음. OSLink가 VDD를 살려둬 어제 기아는 재현 안 됨.
  - 영상 전 12:02:00~21: 클라 recvFrames=4/s **전부 synthetic**(정적 refresh), mbps~1.2 — 유휴.
  - 12:02:22 영상 시작: 클라 recvFrames 32(12:02:23), 실프레임 유입.
  - 12:02:29 첫 패킷 손실(client `dropPm=142`) → 클라가 `handle_udp_discontinuity`(`viewer_video_receiver.cpp:156`)로 **디코더 reset + keyframe-request reason=2**.
  - 이후 손실→불완전프레임→reason=2 반복. 호스트가 매 요청에 큰 IDR 응답 → **송신 mbps=36.97**(목표 12), `forceKeyInputCount=25`, `keyReqTotal=36`.
  - **ABR 미반응**: `abrProfile=high abrModSec=0 abrSevSec=0` — 손실 심한데 한 번도 강등 안 함(피드백 자체가 혼잡에 묻힘 추정).
  - 릴레이 경로 용량 초과: 호스트 37Mbps 송신인데 클라 수신 ~10Mbps → 70%+ 손실 → 조립 불가 → 더 많은 keyframe 요청 → death spiral → 12:02:59 `udp control session ended reason=peer-lost` → 정지.
  - 12:03:12 재접속 후에도 12:03:14 mbps=36.97로 재폭주, 이후 저fps 유휴(synthetic)로 안착. 12:05:16 클라 recvFrames=8 synthetic=4.
- **결론**: 오늘 정지 = **키프레임 폭주 + ABR 미반응 + 릴레이 대역 초과의 혼잡 붕괴**(클라/전송 계층). 가상 디스플레이·캡처 문제 아님. 즉 지금 원장에 **별개 결함 2개**: (1) 헤드리스 캡처 기아(#335, VDD 작업 대상), (2) 고화질/손실 환경 혼잡 붕괴(신규).
- 수정 후보(미착수): (a) ABR이 클라 손실/미수신에 반응해 **강하게 강등**(현재 트리거가 손실 신호를 못 받음), (b) `handle_udp_discontinuity`가 손실 지속 시 매번 decoder reset+IDR 하지 않도록 감쇠(현 120ms 리미터로도 폭주), (c) 손실 지속 시 호스트 비트레이트 상한을 목표 이하로 강제. (d) 릴레이 대역: 회사→집 경로 실효 대역 확인.
- 변경: `docs/history.md`만. 코드 변경 없음. RDP 아님(console)이라 측정 유효.
- 다음: 사용자 결정 — 오늘 혼잡 붕괴(재현 쉬움·실사용 직결)부터 고칠지, 가상 디스플레이(OSLink off 케이스)부터 갈지.

### 338) 2026-09-02 오늘 발견 혼잡/ABR 결함 수정 + 서버 로그 로테이션 10개 — 0.2.69 (브랜치 refactor/viewer-split)

- 사용자 지시: "오늘 나온 것부터 수정 다 진행", 서버 로그 로테이션(최대 10개), 그리고 원격 커서 미추종은 **기록만 하고 보류**.
- **P4 (ABR 정적 화면 회복)** `host_abr.hpp`: 혼잡으로 720p/low 강등 후 글 읽는 정적 화면에서 영영 안 올라오던 것(#337). `hostOfferSparse`가 강등뿐 아니라 승격도 막아 low가 고착됐다. 정적/희소 초에도 링크가 깨끗하면(지연·tail 낮고 손실<20‰) `abrSparseRecoverySeconds`를 쌓아 8초(quality-first 6초) 뒤 한 단계 승격(`reason=static_recovery`). 강등은 여전히 `!hostOfferSparse`라 flapping 없음.
- **P6 (ABR 손실 반응)** `host_abr.hpp` + `host_stage_stats_h264.cpp`: `AbrInputs`에 `clUdpDropPm` 추가(이미 M9가 쓰던 값, ABR만 안 봤다). 손실 severe>100‰ / moderate>35‰(quality-first 60/20)를 강등 조건에 추가 → 지연·fps가 무너지기 전에 손실만으로도 강등.
- **P5 (키프레임/디코더 리셋 폭주 감쇠)** `viewer_video_receiver.cpp`: `handle_udp_discontinuity`가 손실마다 decoder.reset()+keyframe-request 하던 것을 **손실 에피소드당 1회**로. 이미 `waitForKeyFrame`이면 재리셋·재요청 생략(프레임 게이트가 도착 프레임마다 재요청하므로 회복은 유지). 매 IDR 스파이크가 marginal 링크를 밀어내던 death spiral(#337) 완화.
- 단위 테스트 추가(`host_abr_test.cpp`): `TestAbrStaticRecoveryPromotesFromLow`(정적+깨끗→8초 후 low→mid, 정적+고지연→미승격), `TestAbrClientLossTriggersDemotion`(손실만으로 강등). host_abr_test PASS, viewer_frame_gate_test PASS.
- **서버 로그 로테이션** `apps/directory/server.js` `REMOTE60_LOG_KEEP` 기본 3→10, `automation/deploy_directory.ps1` 유닛에 `Environment=REMOTE60_LOG_KEEP=10` 명시. 로그는 이미 계정/기기별 + 스트림별(apk/client/viewer/host)로 각각 파일 분리돼 있고 16MB마다 로테이션. 이제 스트림당 활성 1 + 백업 10. directory 테스트 전체 PASS(logs_test 포함). **NAS 재배포 필요**(claude 계정은 읽기전용이라 사용자가 `deploy_directory.ps1` 실행).
- **G5 기록만**: 원격 커서가 클라에서 안 따라 움직임(가상 디스플레이 연관 의심). 사용자 지시로 미착수, 구현계획.md에 등재.
- 빌드: `remote60_installer` Release exit 0. 임베드 GNLinkHost/GNLinkSetup 0.2.69 확인. 산출물 `dist/GNLinkSetup-0.2.69.exe`(직전 0.2.68 보존). **RDP 아님(console)이나 캡처 판정은 실기에서** — 이번 변경은 단위테스트로 검증, 실사용 검증은 사용자 테스트 대기.
- 다음: 0.2.69 설치 → 고화질 영상 재현으로 (1) 강등 후 정적 화면에서 1080p 복귀(P4) (2) 손실 시 조기 강등·폭주 감소(P5/P6) 확인. 서버는 재배포 후 로테이션 10개 확인.

### 339) 2026-09-02 RDP 끊김 후 검은 화면/열화 = 어댑터 불일치 확정(R1 미완의 절반), 실기 로그 (브랜치 refactor/viewer-split, 기록)

- 사용자 실기: 14:13~14:14 RDP 끊고 잠금화면 상태에서 gnlink 접속 → **아무 화면도 안 나오고 연결도 안 되는 것처럼 보임**. 이후 OSLink로 잠금해제하고 다시 접속(14:18~).
- **이건 RDP 이슈이고 곧 R1의 미완 절반이다.** R1(어댑터 이동 감지)은 감지만 하고 "세션 내부에서 D3D 장치를 재생성하지 않는다"로 재생성을 호출자에게 넘겼는데, 그 호출자가 재생성을 안 해서 `dxgi_adapter_changed` 뒤 곧장 WGC로 떨어진다. 그 빈틈이 G1(헤드리스 검은 화면)으로 나타난 것.
- 0.2.68 신규 진단이 원인을 확정(이전 "소비자 없는 유휴 indirect display" 가설은 **정정**):
  - 14:13:45 잠금화면: `dxgi_no_usable_output reason=no_outputs deviceLuid=99594 outputs=0` → CreateForMonitor 전부 실패 → `desktop_backend=wgc` via `CreateForWindow(GetShellWindow())`. 잠금화면에서 셸 윈도우 캡처는 쓸 그림이 없어 **검은 화면**.
  - 14:14:24 / 14:18:22 / 14:18:52 (해제 후): `dxgi_no_usable_output outputs=1` + `dxgi_output adapter="AMD Radeon" luid=48969 name=DISPLAY177/DISPLAY22 extent=2236x1232→1920x1080 attached=1 onDeviceAdapter=0` + `dxgi_desktop_moved` → `fallback_reason=dxgi_adapter_changed` → WGC.
- **확정 원인**: 호스트 D3D 캡처 장치가 어댑터 LUID **99594**(호스트가 11:53 RDP 접속 중 기동 → 그때 primary였던 Microsoft Remote Display Adapter 추정, RDP 끊기니 출력 0)에 만들어져 있고, 데스크톱은 **AMD Radeon 48969**로 옮겨감. DXGI는 자기 어댑터 출력만 복제 가능 → 데스크톱이 attached여도 못 잡음. 코드가 `dxgi_adapter_changed`로 인식만 하고 장치를 AMD로 재생성하지 않아 매번 WGC 폴백.
- **현재 상태(14:18~14:19)**: 세션 활성, 입력 도달(gmux/chrome), 백엔드 WGC, 같은 불일치 지속(장치 99594 출력0 / 데스크톱 AMD 48969 DISPLAY22 1920x1080). 잠금 해제 상태라 WGC가 그림은 잡음(검지 않음)이나 DXGI 미사용 열화 + keyframe-request reason=2 지속.
- **수정 방향(미착수, 승인 대기)**: `resolve_output`이 `dxgi_adapter_changed`를 반환하면 호출부(host_stage_backend / capture 시작)가 `create_d3d11_device_for_primary_monitor`로 **현재 데스크톱 어댑터에 장치를 재생성한 뒤 DXGI 재시도**, 실패 시에만 WGC. 이 함수는 이미 primary monitor의 어댑터를 고르게 돼 있어 선택 로직은 재사용. G1 = R1의 재생성 절반 완성.
- **부수 관찰(기록만)**: 사용자 보고 "접속 직후 영어 입력이 처음엔 안 쳐진다"(IME/포커스 또는 초기 키 유실 의심). 재현/원인 미확인 — 별도 확인 필요 시 항목화.
- 변경: `docs/history.md`, `docs/구현계획.md`(G1 원인 확정 반영)만. 코드 변경 없음.
- 다음: 어댑터 변경 시 장치 재생성 수정(G1/R1) 착수 승인 → 0.2.70.

### 340) 2026-09-02 G1/R1 수정 — 어댑터 이동 시 D3D 장치 재생성 후 DXGI 재시도 — 0.2.70 (브랜치 refactor/viewer-split)

- 대상: #339에서 확정한 원인(RDP 끊김 → 데스크톱이 AMD로 이동했는데 캡처 장치는 옛 RDP 어댑터(출력 0)에 남아 DXGI가 `dxgi_adapter_changed`로 실패 → WGC 고착 → 잠금화면 검은화면/해제 후 열화).
- 수정(`host_capture_session.cpp` `RestartCaptureSessionImpl`): `dxgiCaptureSession.Start`를 람다로 빼고, 실패 detail이 `dxgi_adapter_changed`면 **현재 데스크톱 어댑터로 D3D 장치를 재생성**(`create_d3d11_device_for_primary_monitor`) → 인코더 `set_d3d11_device` 재설정 → gpuScaler 재초기화 → `CreateStaging`으로 readback 새 장치에 재구축 → `config.d3dDevice` 갱신 → **DXGI 1회 재시도**, 그래도 실패할 때만 WGC. 성공 시 `desktop-backend-restored reason=adapter_recreated backend=dxgi` 로그.
- 핵심 근거: 함수 상단에서 `capture.monitorInfo = primary_monitor_info()`로 크기·모니터는 이미 현재 주 모니터(AMD 1920x1080)로 갱신돼 있었다. 어긋난 건 **장치의 어댑터뿐**이라 장치만 새로 만들면 된다. R1은 이 재생성을 "호출자에게 위임"만 하고 실제 배선이 없던 것 → 이번에 그 절반을 채움.
- 빌드: `remote60_installer` Release exit 0. 임베드 GNLinkHost/GNLinkSetup 0.2.70 확인. 산출물 `dist/GNLinkSetup-0.2.70.exe`(직전 0.2.69 보존). host 타겟 컴파일 0 에러.
- **실기 검증 필수(내가 못 하는 판정)**: RDP 끊고 잠금화면/해제 상태에서 접속 → host_app.log에 `dxgi_adapter_changed: recreating...` → `desktop-backend-restored ... backend=dxgi` 뜨고 `desktop_backend=wgc` 대신 DXGI로 붙는지, 화면이 나오는지. 재접속만으로도 복귀되는지(장치 재생성이 restart 경로에 있으니 이제 됨).
- 범위 분리: 이번 빌드는 G1/R1만. G2(끊긴 세션 잠금해제)·G3(PC 뷰어 잠금 UI)는 별도. G4(자체 가상 디스플레이)는 #339로 **불필요 가능성 커짐**(데스크톱이 AMD에 attached 출력으로 이미 존재 — "출력 없음"이 아니라 "어댑터 불일치"였음). OSLink/가상 디스플레이 없는 완전 무출력 머신에서만 G4 의미.
- 다음: 0.2.70 실기 → DXGI 복귀 확인. 통과 시 G2 착수(콘솔 세션 타겟 + U5 ack).

### 341) 2026-09-02 0.2.70 실기: 어댑터 재생성 성공(DXGI 복귀) but 영상 시 혼잡 붕괴 재발 — ABR이 피드백 소실로 미강등 (기록)

- 실기(호스트 0.2.70):
  - **G1/R1 성공**: 14:40:31 `dxgi_adapter_changed: recreating D3D device...` → `desktop-backend-restored reason=adapter_recreated backend=dxgi` → `from=wgc to=dxgi`. 이후 14:44~14:47 내내 `desktop_backend=dxgi` 유지, WGC 미폴백. **어댑터 수정 실기 통과.**
  - **14:46 멈춤 = 혼잡 붕괴(#337 재발)**: 14:46:48 stat `encodedFrames=58 mbps=37.5 abrProfile=high abrModSec=0 abrSevSec=0` → 14:46:49 `udp control session ended reason=peer-lost`. 영상 시 인코더가 VBR 피크(≈37Mbps, 목표 12의 3배)로 폭주, **ABR은 high 고정(압력 0)**. 14:47:03 재접속으로 즉시 복구.
- **근본 진단(신규)**: 혼잡이 심해지면 클라 피드백이 호스트에 못 옴 → `metricsFresh=false`. P6(0.2.69, 손실 반응)는 클라 지표(clUdpDropPm/지연)에 의존하는데 그 지표가 소실돼 발동 못 함. 호스트는 cb2e(인코딩)만 보고 정상으로 판단 → 강등 안 함. **붕괴가 ABR의 강등 신호 자체를 끊는다.** P4/P5/P6로는 이 경로(피드백 소실형 붕괴)를 못 막는다.
- **미확인**: 회사 PC(뷰어) 버전. P5(클라측 키프레임 폭주 감쇠)는 뷰어 0.2.70이어야 적용. 호스트만 0.2.70이면 클라측 P5 미적용 가능 → keyframe-request reason=2 지속(로그상 여전히 다발).
- 신규 항목 P7(아래): 호스트 자체 관측(송신 큐 적체/피드백 staleness)으로 클라 지표 없이도 ABR 강등 + 릴레이에서 VBR 피크·목표 상한. 이게 없으면 릴레이+고화질 영상은 계속 붕괴.
- 변경: `docs/history.md`, `docs/구현계획.md`(P7) 문서만. 코드 변경 없음.
- 다음: (a) 회사 PC 0.2.70 설치 확인, (b) P7 착수 승인 → 피드백 소실형 혼잡 붕괴 차단.

### 342) 2026-09-02 P7 — 혼잡 붕괴 시 ABR 강등 배선 + VBR 피크 축소 — 0.2.71 (브랜치 refactor/viewer-split)

- 근거(#341 실기 로그 재분석): 14:46 붕괴 때 클라는 침묵하지 않았다. NAS viewer.log에서 **클라가 초당 3~9프레임만 받으며 지표를 계속 보고**(호스트는 60fps 전송). 즉 metricsFresh=true인데 도착한 소수 프레임의 지연은 정상이라, 기존 severe 조건("fps 저하 **AND** 지연 높음")이 발동 못 했다. 그래서 ABR이 high 고정 → 37Mbps 유지 → peer-lost.
- 수정(`host_abr.hpp` `DecideAbrProfile`):
  - **P7-fps**: `severeDownByClient`의 fps 절을 **지연 게이트 제거한 단독 조건**으로. severeDown은 이미 `!hostOfferSparse`(호스트가 이번 초 풀 카덴스 송신)로 게이트되므로, 그 상황에서 클라 디코드 fps가 severe 임계(activeFps×35%) 밑이면 = 프레임이 와이어에서 유실된 혼잡 → 단독으로 severe. 정적 화면 오탐은 hostOfferSparse가 배제.
  - **P7-stale**: 활성 송신 중 클라 피드백이 끊기면(`!metricsFresh && !hostOfferSparse`) `abrStaleActiveSeconds` 누적, 2초(quality-first 3초) 지속 시 severe. 완전 침묵형 붕괴 대비(벨트+멜빵).
  - Commit 시 카운터 리셋 추가.
- 수정(`mf_h264_codec.cpp`): VBR 피크 배수 기본값 **300%→200%**(`REMOTE60_NATIVE_PEAK_BITRATE_PERCENT`). 고화질 영상의 순간 버스트 상한을 목표의 3배(36M)→2배(24M)로. env로 조정 가능. LAN 모션 화질 소폭 트레이드, 릴레이 안정 우선.
- 테스트(`host_abr_test.cpp`): `TestAbrStaleFeedbackDuringActiveSendDemotes`, `TestAbrLowClientFpsDemotesDespiteLowLatency` 추가. host_abr_test 전체 PASS.
- 빌드: `remote60_installer` Release exit 0. 임베드 GNLinkHost/GNLinkSetup 0.2.71. 산출물 `dist/GNLinkSetup-0.2.71.exe`(직전 0.2.70 보존).
- 솔직 기록: P4/P5/P6(0.2.69)·P7-stale(초기안)로는 이 붕괴를 못 막았다. 못 막은 진짜 이유가 "fps 급락인데 지연은 정상이라 트리거 안 됨"이었고, P7-fps가 그걸 정면으로 잡는다. 이게 세 번째 시도이며 실기 검증 필요.
- **실기 검증(양쪽 0.2.71 설치)**: 고화질 영상 재생 시 host_app.log에서 `abrProfile=high→mid→low`로 강등되고 mbps가 목표 밑으로 떨어지며 peer-lost 없이 저화질로라도 유지되는지. abrModSec/abrSevSec 증가 확인.
- 다음: 0.2.71 실기 → 강등 동작 확인. 여전히 붕괴면 recv<<send 갭 트리거(P7 확장) 또는 릴레이 대역 자체 상향.

### 343) 2026-09-02 실기: 창모드 자체는 정상, LDPlayer 창만 WGC 무프레임 (기록)

- 사용자: 모바일에서 LDPlayer 1번 창 눌러도 안 됨("원래 창모드 잘 됐는데").
- 로그: LDPlayer 창(dnplayer id=527608) 선택 streamGen 4·5·8 **모두 first-frame 0장**, `desktop_backend=wgc_window capture-started=1`만 뜸. 같은 세션 gmux 창(streamGen 6)은 first-frame 정상. 이 구간 어댑터 재생성 없음 → G1/R1 무관, 창모드 일반 회귀 아님.
- 진단: WGC per-window가 LDPlayer(안드로이드 VM GPU 렌더/오버레이) 창을 못 잡는 한계. 남은 로그(어제~오늘)에 LDPlayer 창 성공 기록 없어 "예전에 됨"은 다른 경로 추정.
- 조치: 코드 변경 없음. 계획서 G6(창모드 무프레임 폴백: 데스크톱을 창 rect로 크롭) 신설. 즉시 완화는 LDPlayer 렌더링 DirectX 전환.

### 344) 2026-09-02 PC 뷰어 한/영 입력 문제 = 호스트 IME 미중립(영어 VK가 호스트 IME에 먹힘) (기록)

- 사용자: PC 클라 첫 접속 시 한글만 써지고 영어 안 됨. 한/영 토글해도 안 됨. **호스트 PC의 한/영 표시를 직접 클릭**해 영어로 맞추면 그때부터 영어 됨. 한글은 자음모음 조합돼 완성.
- 코드 확인: `viewer_input_forward.cpp` — 한글은 클라 IME가 조합→`WM_IME_COMPOSITION GCS_RESULTSTR`→유니코드 텍스트로 전송, 호스트 `host_input_inject.cpp:327` `KEYEVENTF_UNICODE`로 주입(호스트 IME 우회). 영어 등 비조합 키는 VK 키 이벤트로 전송, 호스트가 VK/scancode `SendInput`(호스트 IME 통과).
- 확정 원인: **호스트 IME가 한글 모드면 클라가 보낸 영어 VK가 호스트 조합기에 먹힘**. 클라·호스트 IME 2개가 어긋남. 사용자 워크어라운드(호스트 IME 영어로 직접 전환)가 원인 확증.
- 수정 방향(미착수): 원격 세션 중 **호스트 IME를 영어/직접 입력으로 중립화**(한글은 클라가 유니코드로 보내므로 호스트 IME 불필요). 단 `viewer_input_forward.cpp` 주석의 과거 사고("type 11 get 22" 중복 주입, IME result 카운터 오프)로 신중 설계 필요. 계획 G7 신설.
- 코드 변경 없음.

### 345) 2026-09-02 GMux(텍스트) 창이 깨지고 느림 = 영상용 인코딩 설정 (진단, P8 신설)

- 사용자: PC 클라로 GMux 보는데 채팅치는 곳이 깨지고 체감이 많이 느림. "프레임 아끼느라 그런가?"
- 진단:
  - **느림**: 캡처가 변화 기반(DXGI는 변할 때만 프레임) → 정적 화면 실측 encodedFrames=4/30s. fps 목표 올려도 무의미(프레임 수는 화면 변화량이 결정). 필요한 건 최소 신선-프레임 바닥.
  - **깨짐**: `maxQp=32` + `QualityVsSpeed=100`(속도 우선), 해상도는 풀(1920x1080, 다운스케일 아님) → 순수 QP/튜닝 문제. H.264가 텍스트 고주파를 QP32에서 뭉갬. 인코더 `stable_text` 튜닝(QualityVsSpeed=68 + text VBV, `mf_h264_codec.cpp:1445/1471/1513`)은 꺼져 있음.
- 즉시 완화(호스트 env + 재시작): `REMOTE60_NATIVE_ENCODER_TUNE_MODE=stable_text`, `REMOTE60_NATIVE_MAX_QP=26`.
- 근본(P8): 텍스트/인터랙티브 모드 — stable_text 튜닝 + QP 하향 + 최소 프레임 바닥 상향. 영상과 트레이드라 자동 감지 또는 토글이 이상적. 코드 변경 없음(진단만).

### 346) 2026-09-02 P7 오판 회귀 수정(20fps를 붕괴로 오인) + GMux는 데스크톱모드라 텍스트 작음 — 0.2.72 (브랜치 refactor/viewer-split)

- 사용자: GMux가 "갑자기 엄청 심해졌다"(깨짐·느림). 아까는 이 정도 아니었음.
- 진단 1(모드): 지금 **데스크톱 모드**(captureTargetProc=monitor, selectedId=0, 전체 1920x1080). GMux는 그 안 작은 창이라 글자 픽셀이 적고 QP32에서 깨짐. 창모드로 GMux 창을 직접 고르면 화면을 꽉 채워 선명. → 즉시 완화는 창모드.
- 진단 2(회귀): 15:33:34 `[abr] high_to_mid_severe clientDecodedFps=20` — 내가 넣은 P7-fps(`<minSevereFps`=35%=21fps)가 **정상 인터랙티브 20fps를 붕괴로 오판**해 mid 강등, 곧 static_recovery로 복귀 → high<->mid 플래핑. 텍스트에서 비트레이트가 출렁여 체감 악화.
- 수정(`host_abr.hpp`): P7-fps 단독 강등 임계를 `minSevereFpsX100`(35%)에서 **`collapseFpsX100`(기본 12% / quality-first 18%)**로 하향. 실측 붕괴(≈8%, 5fps)는 잡고 인터랙티브 20~33fps는 안 건드림. 해상도 유지. `TestAbrLowClientFpsDemotesDespiteLowLatency`에 "33% dip은 강등 안 함" 케이스 추가, host_abr_test PASS.
- 빌드: `remote60_installer` Release exit 0, 임베드 GNLinkHost/GNLinkSetup 0.2.72. 산출물 `dist/GNLinkSetup-0.2.72.exe`(직전 0.2.71 보존).
- 미해결(별개): P8 텍스트 화질(QP32/속도우선 튜닝) — 창모드로도 남는 근본 화질. 별도 결정(stable_text+QP26 또는 토글).
- 다음: 0.2.72 설치 → P7 플래핑 사라지는지 + 창모드에서 GMux 텍스트 선명한지 확인. 그다음 P8.

### 347) 2026-09-02 PC 뷰어 창모드 복원 (F-21 되돌림, 사용자 재요청) — 0.2.73 (브랜치 refactor/viewer-split)

- 사용자: PC 클라에 GMux 창모드가 없다, 다시 넣어야겠다("내가 빼라 했었어"로 F-21 재확인). 데스크톱 모드로 GMux 보면 작아서 글자 깨짐.
- 원인 확인: F-21(`c3678b9`, 0.2.62)에서 `kPickerListsWindows=false`로 PC 피커를 desktop만 표시하게 막았음 — (a) flip-model 스왑체인이 GDI 피커를 덮던 버그 + (b) 당시 사용자 요청. (b)를 사용자가 되돌림.
- 수정(`viewer_picker.hpp`): `kPickerListsWindows` false→**true**. 그리기(`viewer_overlay_draw.cpp`)·히트테스트·썸네일(`viewer_picker.cpp`)이 전부 이 플래그로 게이트되므로 한 곳만 뒤집으면 창 목록/선택/썸네일이 함께 복원. (a) 버그의 수정(피커 진입 시 `release_swapchain()`으로 GDI 합성 복귀)은 그대로라 피커는 보임 — 이번엔 창 카드만 다시 그려짐.
- 빌드: `remote60_installer` Release exit 0, viewer_picker_gesture_test PASS. 임베드 GNLinkHost/GNLinkSetup 0.2.73. 산출물 `dist/GNLinkSetup-0.2.73.exe`(직전 0.2.72 보존).
- 정정: 사용자가 의심한 "느림이 저 수정 때문" — 창모드 부재는 F-21(사용자 과거 요청)이지 최근 수정 아님. 느림은 데스크톱모드 작은 텍스트 + P7 플래핑(0.2.72에서 해결). 정적 화면 저프레임은 변화기반 캡처의 본질.
- 다음: 0.2.73 설치 → PC 피커에서 GMux 창 선택되는지 + 창모드 텍스트 선명한지. 그다음 P8(텍스트 QP 튜닝)로 더 개선.

### 348) 2026-09-02 드래그 끊김 = 헤드리스 데스크톱 합성률 5~10Hz (물리 모니터 부재), G4 재부상 (기록)

- 사용자: 창 드래그 중 중간중간 잠깐씩 멈춤(15:56:20~). 네트워크 사용량 확인 요청.
- 로그(0.2.72): 드래그 구간 `dxgi-acquire acquires=5~10 timeouts=6~8`/s → **소스 데스크톱 합성이 초당 5~10Hz**. `frameGatingSkips=0/staticSkips=0`(게이트 무관), `senderQueueDrops` 완만, mbps 1.5(대역 여유). wire 간격 150~485ms(2~6fps). 즉 인코딩·네트워크·게이트가 아니라 **캡처 소스가 느림**.
- 원인: 물리 모니터 없음 → 헤드리스 데스크톱이 낮은 주사율로만 합성(드래그 같은 고모션에도 5~10Hz). `DesktopUpdateRate` 실측에서 VDD가 60Hz 낸 건 OSLink가 능동 소비할 때뿐. 우리 DXGI 캡처만으론 고주사율을 강제 못 함. CLAUDE.md의 "원격/가상 디스플레이 저주사율" 경고와 같은 계열.
- 네트워크 사용량: 인코딩 mbps 0.5~2, 실측 udpTxBytes 델타 ~5Mbps. 화면 변화가 적어 낮음. 문제 아님.
- **함의: G4(자체 60Hz 가상 디스플레이) 재부상.** G1으로 "검은 화면"은 해결됐지만, **부드러운 인터랙션/모션은 60Hz로 present하는 디스플레이가 필요**. 대안: (a) 더미 HDMI 플러그가 실제 감지(현재 미감지) → AMD 60Hz 출력, (b) 물리 모니터, (c) 자체 VDD를 60Hz로 구동.
- 별개 미해결: G7(영어 입력 안 됨) 실기 재확인 — 호스트 IME 한글모드에서 영어 VK 먹힘, 한/영 토글 무효.
- 코드 변경 없음(진단만).

### 349) 2026-09-02 G7 — PC 뷰어 영어 입력: 호스트 IME 중립화 — 0.2.74 (브랜치 refactor/viewer-split)

- 대상(#344): PC 클라 접속 시 영어 안 됨. 원인은 영어가 VK 키로 전송→호스트 `SendInput`이 **호스트 IME 통과**→호스트가 한글 모드면 조합기에 먹힘. 한글은 클라가 조합해 유니코드로 보내 IME 우회(잘 됨).
- 수정(`host_input_inject.cpp`): 키다운(kind 5) 주입 직전 `ensure_foreground_ime_alphanumeric()` — `AttachThreadInput`으로 포그라운드 스레드에 붙어 `GetFocus`의 IMC를 얻고, `IME_CMODE_NATIVE`가 켜져 있으면 `ImmSetConversionStatus`로 alphanumeric(영문)로 내림(FULLSHAPE도 해제). 250ms 스로틀, `imm32` 링크(`#pragma comment`). env `REMOTE60_NATIVE_IME_NEUTRALIZE_OFF=1`로 끌 수 있음.
- 설계 근거: 원격 세션의 올바른 상태 = **호스트 IME OFF**. 영어 VK는 그대로 박히고, 한글은 클라 조합→`KEYEVENTF_UNICODE`라 IME 상태와 무관하게 박힘. 클라의 한/영 토글이 이제 실효: 클라 한글모드→한글(유니코드), 클라 영문모드→영어(호스트 IME off라 landed). **회귀 위험 낮음**: 최악이라도 영어가 안 될 뿐, 유니코드 한글은 못 깨뜨림.
- 빌드: `remote60_installer` Release exit 0, imm32 링크 OK. 임베드 GNLinkHost/GNLinkSetup 0.2.74. 산출물 `dist/GNLinkSetup-0.2.74.exe`(직전 0.2.73 보존).
- 실기 검증 필요: PC 클라 첫 접속부터 영어 즉시 입력되는지, 한/영 전환(클라 IME)으로 한글·영어 오가는지, 한글 중복 주입 없는지.
- 다음: 0.2.74 실기(영어 입력) → 통과 시 G4(60Hz 가상 디스플레이 구동)로.

### 350) 2026-09-02 실기: picker(B) 카드 미표시 확정 + 한글 조합 지연, 급선무 G4로 수렴 (기록)

- picker 복귀 (B): 대상선택 시 카드가 **아예 안 뜨고** 얼어붙은 마지막 영상이 덮음(클릭은 먹힘 — 호스트 로그상 stream-state active=0 + window-list + select 정상). onTargets→`set_picker_visible_and_sync_stream(true)`가 `release_swapchain()`+InvalidateRect까지 정상 호출되는데도 flip-model 스왑체인이 GDI draw_overlay 위를 계속 덮음. release_swapchain의 `swapChain.Reset()`이 이 환경(하드웨어 NV12 render_surface 경로 추정)에서 마지막 프레임 합성을 못 걷어냄. **정석 해법: picker를 툴바처럼 별도 top-level 창으로 분리**(GDI/flip-model 겹침 회피). 블라인드 수정 위험(화면 확인 불가).
- 한글 조합 지연("각" 치면 "가"가 다음 글자 칠 때까지 안 보임): (1) 한글은 클라 IME가 **완성 후에만**(GCS_RESULTSTR) 전송 — 조합 중(GCS_COMPSTR)은 미전송. (2) 호스트 화면 갱신 5~10Hz(헤드리스). 둘 다지만 (2)가 더 큼.
- **급선무 수렴**: 드래그 끊김·타이핑 지연·전반 느림의 공통 뿌리 = **헤드리스 데스크톱 5~10Hz**. → **G4(60Hz 가상 디스플레이 구동) 최우선**. 그다음 picker(B) 별도창, P8 텍스트, G7(0.2.74) 검증.
- 코드 변경 없음(진단·정리).

### 351) 2026-09-02 G4 교차검증(검증용Codex) — 5~10Hz 근본은 VDD 아닌 입력 ACK 직렬화, G4 강등 (기록)

- 사용자 지시로 G4(60Hz 가상 디스플레이) 계획을 버스의 검증용Codex(remote#nhxsk5vr)에 교차검증 요청. 결과: **내 VDD 가설 기각**, 방향 전환.
- Codex 지적(코드로 확인함):
  - 입력이 **왕복 1회당 1개로 직렬화**. `native_video_client_tcp_control.cpp` execute_control_action = send 후 recv_control_response 동기 대기. `native_video_client_shared_core.cpp:565~578` scheduler는 input 1개 dequeue→ControlInputAck 기대. RTT 150ms면 6~7/s → 드래그 5~10Hz·타이핑 "각→가" 지연과 정합. **이게 5~10Hz의 더 가까운 원인.**
  - MTT VDD pipe(`\.\pipe\MTTVirtualDisplayPipe`)는 control-plane(RELOAD/config/PING)일 뿐 frame 운반 안 함 → "우리가 pipe로 소비자 역할" 불가(공개 소스 확인). 실 소비자는 드라이버 SwapChainProcessor.
  - DesktopUpdateRate 로그 235초 median=4/p95=5, 60은 3초뿐 → "프로브가 항상 60"은 반증. 즉시-release라 present/invalidate 안 함. "드래그5~10 vs 프로브60" 모순 아님(입력창 vs 자율60fps콘텐츠).
  - DXGI Duplication은 change-driven → **60Hz 모드 ≠ 60fps 캡처**. 강제 present도 원래 5~10번만 움직인 창의 중간 위치를 발명 못 함. DwmFlush 무의미.
  - 출력 identity 미확정(프로브 AMD DISPLAY22 vs MTT ROOT\DISPLAY\0001, 같은 1920x1080이라고 동일 출력 단정 불가).
- 확정 계획: **P0 조인 텔레메트리**(client mouse gen/enq/coalesce/sent/ack per s + action RTT, host input recv/inject per s, DXGI accumTotal/LastPresentTime per s) → **P1 입력 비직렬화**(mouse move = UDP latest-wins seq, no per-input ack; button/key down/up = reliable-ordered + edge packet에 pos+buttonmask). key 지연은 파이프라이닝/ack대기 제거 검토. G4 자체 VDD는 P4 최후수단으로 강등.
- Codex에 P1 전송 설계 3문항(전송 위치/ host 적용/ key 처리) 재질의 중.
- 작업목록.md 최상단을 INPUT(입력 직렬화 제거)로 교체, G4를 14번 최후수단으로.
- 코드 변경 없음(교차검증·계획 정정). 다음: Codex P1 답 → P0 텔레메트리 빌드 → 실측 → P1 구현(구현도 Codex 검증).

### 352) 2026-09-02 P0 입력 직렬화 진단 텔레메트리 (Codex 설계) — 0.2.75 (브랜치 refactor/viewer-split)

- Codex(remote#nhxsk5vr) P0/P1 설계 확정 후 P0 착수. P1 방향: 같은 UDP 소켓 별도 논리 레인 — 마우스=UdpPacketKind::InputFast(cookie+peer+epoch+feature, latest-wins, no-ack, FastInputSender 60~120Hz→host FastInputMailbox→단일 executor), 키=별도 reliable sliding-window. 순서 P0→P1a(드래그)→P1b(타이핑) 2커밋. G4(자체 VDD)는 P4 최후수단.
- P0 구현(측정만, 동작 무변경): 클라 뷰어에 초당 1줄 `[input-p0] moveGenPerSec / inputSentPerSec / droppedTotal / inputRttAvgUs / inputRttMaxUs / transport`.
  - `viewer_input_state.hpp` moveGeneratedCount 추가, `viewer_window_proc.cpp` 마우스move enqueue 직전 증가(coalesce 전 생성 수), `viewer_control_client.cpp` Run 루프에서 InputEvent action의 actionUs(=send+ack 왕복)를 초당 집계해 로그.
- 결정 판정식: 드래그 시 moveGenPerSec≈60인데 inputSentPerSec≈1/RTT(예 6~7) + inputRttAvg≈RTT → 입력 직렬화가 5~10Hz의 root 확정 → P1a 착수. (host contentAcquires 분리·inject→content join은 필요 시 확장.)
- 빌드: installer Release exit 0, 임베드 0.2.75. 산출물 `dist/GNLinkSetup-0.2.75.exe`(직전 0.2.74 보존).
- 다음: 0.2.75 설치 → 드래그 실측 → NAS viewer.log `[input-p0]` 라인 확인 → 직렬화 확정 시 P1a(마우스 fast lane) 구현(구현도 Codex 검증).

### 353) 2026-09-02 P0 확장 — 호스트 DXGI content/pointer 분리(사용자 "한 번에 측정") — 0.2.76 (브랜치 refactor/viewer-split)

- 사용자 지시: P0 측정을 한 번에 하게 호스트 쪽도 넣어라. Codex 조건 4 반영.
- 구현(`libs/capture/src/capture_backend_dxgi.cpp`): AcquireStats에 contentAcquires/pointerOnlyAcquires 추가, 획득 루프에서 content=LastPresentTime!=0 / pointer-only=LastPresentTime==0&&LastMouseUpdateTime!=0 로 분류(hot path 카운터만), `dxgi-acquire` 초당 로그에 `dxgiContentAcquires=`/`dxgiPointerOnly=` 병기. 기존 acquires는 둘을 섞어 오독됐음.
- 이제 한 드래그로: 클라 `[input-p0] moveGenPerSec vs inputSentPerSec + RTT`(0.2.75) + 호스트 `dxgi-acquire dxgiContentAcquires`(초당 실제 화면 변화)를 함께 봄. 판정: sent≈content≈1/RTT면 입력 직렬화 root, sent≈60인데 content≈6이면 컴포지터.
- 코덱스 조건 중 **미포함(보류)**: inject→content join(candidateInputToContentUs, 조건 3 full) — 크로스모듈 atomic 필요, rate 비교로 root가 갈리면 불필요. RTT는 avg+max(조건 2의 p50/p95 히스토그램 대신, 저표본 1s window엔 충분). 애매하면 확장.
- 빌드: installer Release exit 0, 임베드 0.2.76. 산출물 `dist/GNLinkSetup-0.2.76.exe`(직전 0.2.75 보존).
- 다음: 0.2.76 설치 → 드래그 실측 → 클라/호스트 로그 조인 판정 → P1a 착수.

### 354) 2026-09-02 P0 보완 — Codex 리뷰 BLOCKER 1·2 + HIGH 3·4 반영 — 0.2.77 (브랜치 refactor/viewer-split)

- Codex diff 리뷰: DXGI 분리(def1d72) PASS. client/host P0는 실측 전 4건 보완 요구 → 후속 커밋으로 처리(amend 아님).
- BLOCKER 1 (move 전용): `viewer_control_client.cpp` — inputSentPerSec(모든 kind, ACK 후)를 **move(kind==1) 전용** moveSent/moveRtt(avg·max)/moveQueueAge로 교체. 버튼·키 제외.
- BLOCKER 2 (coalesce): `native_video_client_shared_core` ClientInputQueue에 coalescedMoves_ atomic + coalesced_move_count() + Reset. latest-wins 교체 시 증가. 로그에 moveCoalesced(초당 delta). 불변식 moveGen≈moveCoalesced+moveSent 확인 가능.
- HIGH 3 (queue age): QueuedControlInputMessage.generatedUs(로컬 전용, 와이어 무변경) + make_control_input_event에서 설정 + ControlOutboundAction.inputGeneratedUs로 전달 → 송신 시 now-generatedUs를 moveQueueAge로 집계.
- HIGH 4 (host): `host_control_session.cpp` — move recv/injected/injectFail 카운트 + `[native-video-host][input-p0] windowUs/moveRecv/moveInjected/moveInjectFail` 초당 로그.
- 추가정확도: 클라·호스트 로그에 windowUs 병기(control thread가 execute에서 막혀 정확한 1s tick 아님). moveGeneratedCount 주석은 PC 마우스 드래그 전용으로 좁힘.
- 보류(Codex 동의): inject→content full join(candidateInputToContentUs), p50/p95 히스토그램(avg+max로 충분). rate chain(moveGen≈60, coalesced≈53, moveSent≈moveRecv≈injected≈7, dxgiContent≈7)이 어긋나는 단계가 있을 때만 join 추가.
- 빌드: installer Release exit 0, shared_core_test PASS, 임베드 0.2.77. 산출물 `dist/GNLinkSetup-0.2.77.exe`(직전 0.2.76 보존).
- 다음: 0.2.77 설치 → 드래그 실측 → 클라(input-p0)·호스트(input-p0)·dxgi-acquire 조인 판정 → P1a.

### 355) 2026-09-02 창모드 전부 실패 = WGC 장치(res.d3dDevice) 잘못된 어댑터 (G1이 res.d3d만 고침) (기록)

- 사용자: PC·모바일 둘 다 창모드(LDPlayer/putty/explorer docs) 전부 실패. 18:00 이후 반복.
- 로그: 모든 window-select `applied=1 reason=ok` + `desktop_backend=wgc_window capture-started=1` 인데 **first-callback/first-frame 0장**(putty streamGen=14, explorer=15는 콜백조차 없음; dnplayer 16/20은 콜백만). 데스크톱(DXGI)은 first-frame 정상. 즉 창(WGC)만 프레임 0.
- 확정 원인: WGC 프레임 풀은 `host_capture_session.cpp:549 CreateFreeThreaded(res.d3dDevice, ...)`. `res.d3dDevice`는 `host_startup_graphics.cpp:422 res.inspectable.as<IDirect3DDevice>()`로 **기동 시점 res.d3d 파생, 1회만**. #340 G1은 어댑터 이동 시 res.d3d만 재생성하고 res.d3dDevice/res.inspectable은 그대로 뒀다. 호스트가 16:15 RDP 접속 중 기동→장치가 RDP 어댑터. RDP 해제 후 데스크톱은 AMD로 이동, DXGI는 G1으로 복구되나 **WGC는 RDP 어댑터의 res.d3dDevice라 AMD 창을 못 잡음**. 게다가 창모드에선 DXGI 분기(G1)가 아예 안 돌아 자가복구 경로도 없음.
- 즉시 워크어라운드: RDP off 상태에서 GNLinkHost 재시작 → res.d3d+res.d3dDevice 둘 다 AMD로 생성 → 창모드 정상. (현재 RDP off, 데스크톱 AMD.)
- 실제 수정(WGC-DEV, 착수 예정): (a) G1 재생성 시 res.inspectable/res.d3dDevice도 새 res.d3d에서 재구축, (b) 창모드 캡처 시작(RestartCaptureSessionImpl WGC 분기)에서 현재 장치 어댑터가 타겟 창 출력과 다르면 res.d3d+res.d3dDevice 재생성 후 재시도. G1의 WGC 짝. Codex 검증 대상.
- 실행 호스트 0.2.74(내 P0 0.2.75~77 미설치). 코드 변경 없음(진단).

### 356) 2026-09-02 A: WGCDEV(창모드 어댑터 복구) + B: 구현계획 정정·P0 단위테스트 — 0.2.78 (브랜치 refactor/viewer-split)

- 사용자 지시 "A 하고 B도": A=창모드 회귀 수정, B=입력 트랙 정리(구현계획 정정+테스트).
- **A (WGCDEV, #355)**: `host_capture_device.cpp` `d3d_device_owns_primary_monitor(ID3D11Device*)` 신설(장치 어댑터가 주 모니터 출력을 소유하는지, 판정 불가 시 true). `host_capture_session.cpp` WGC 프레임풀 생성(:549) 직전에 소유 안 하면 res.d3d를 primary 어댑터로 재생성 + winrt(res.inspectable/res.d3dDevice) 재구축 + encoder.set_d3d11_device + gpuScaler 재초기화. G1 dxgi_adapter_changed 경로에도 winrt 재구축 추가(데스크톱→창 전환 일관성). 이로써 "호스트가 RDP 중 기동→장치가 RDP 어댑터→RDP 해제 후 창모드 전부 무프레임"이 자동 복구(재시작 불필요).
- **B (P0 정리)**: `docs/구현계획.md` G4를 재부상(#348)→**최후수단 강등(#351)**으로 정정(근본은 입력 직렬화, INPUT 트랙; DXGI change-driven이라 60Hz모드≠60fps, MTT pipe frame-pull 불가). `native_video_client_shared_core_test.cpp`에 `test_input_coalesce_and_generated_us` 추가: move 2연속→coalesced=1·최신 x/generatedUs 보존, Reset=0, NextAction이 inputGeneratedUs 전달+wire send stamp 갱신 검증. PASS.
- Codex 리뷰 LOW 2건(moveSent≈moveAcked 명명, host moveRecv가 injectionEnabled 내부)은 field-test 무영향으로 보류.
- 빌드: installer Release exit 0, shared_core_test PASS, host 컴파일 OK. 임베드 0.2.78. 산출물 `dist/GNLinkSetup-0.2.78.exe`(직전 0.2.77 보존).
- **주의**: WGCDEV는 D3D/WinRT 장치 수명주기 변경이라 화면 확인 불가한 블라인드 수정 — 실기 검증 필수. Codex에 diff 리뷰 요청.
- 다음: 0.2.78 설치 → (A) 창모드 즉시 정상인지(로그 wgc-device-adapter-restored + first-frame) (B) 드래그 실측 input-p0 rate chain. A 통과 시 창모드 닫힘, B 확정 시 P1a.

### 357) 2026-09-02 WGCDEV 재작성 — Codex BLOCKER 2건 수정, 트랜잭션 장치 재생성 — 0.2.79 (브랜치 refactor/viewer-split)

- 0.2.78 WGCDEV를 Codex(remote#nhxsk5vr)가 실기 승인 불가로 판정. 지적된 BLOCKER 2건이 실제 버그라 확인하고 재작성.
- **BLOCKER 1 (cross-device staging)**: 0.2.78은 장치 교체를 CreateStaging(:370) *뒤*(WGC 풀 직전 :549)에 해서, readback/staging은 옛 장치·WGC 풀은 새 장치로 갈렸다 → PublishCapturedTexture의 CopyResource가 cross-device로 실패("풀은 살아났는데 인코드 0"). **수정**: 장치 트랜잭션을 CreateStaging *앞*으로 이동. 이제 순서 = Detach → target/size → device transaction → CreateStaging → pool. staging·encoder·WGC 풀이 한 장치 공유.
- **BLOCKER 2 (split-brain)**: winrt 래퍼 실패 시 res.d3d=NEW·res.d3dDevice=OLD인데 "restored" 출력 후 stale 장치로 진행, 이후 predicate가 res.d3d만 봐서 영영 미검출. **수정**: `RecreateCaptureDeviceOnPrimary()` 트랜잭션 헬퍼 — newD3d/newCtx/newInspectable/newWinrt 전부 local로 만들고 **전 단계 성공 후에만** quartet 동시 commit, 중간 실패 시 res 무손상 + HRESULT 로그 + return false. G1·조기검사 두 경로가 공용.
- **HIGH 1 (predicate)**: `d3d_device_owns_primary_monitor`(primary 소유) → `device_adapter_output_state`(tri-state: HasAttached/None/Unknown)로 교체. **None(출력 0개 확정)만** 재생성, Unknown(probe 실패)은 working 장치 보존. secondary/hybrid/IddCx cross-adapter 오판 제거.
- **HIGH 2 (encoder MFT 재바인드)**: set_d3d11_device가 실행 중 MFT 미재바인드 — CPU readback(기본) 무해, NV12 surface 모드만 영향. 인라인 수정 금지 규칙대로 **원장 D3**(구현계획.md)에 기록, hotfix 미수정.
- G1(dxgi_adapter_changed)도 인라인 split-brain 재생성 제거하고 헬퍼로 통일. WGC-branch 재생성 블록 삭제(조기검사가 창모드 포함 전 경로 커버).
- 빌드: host 컴파일 OK, installer Release, host_abr_test PASS, shared_core_test PASS. 임베드 0.2.79. 산출물 `dist/GNLinkSetup-0.2.79.exe`. **0.2.78은 폐기(설치 금지)**.
- 여전히 블라인드(화면 확인 불가) 수정 — Codex 재리뷰 요청 + 실기(RDP-on 기동→RDP-off→창 선택→first-frame) 필수.

### 358) 2026-09-02 WGCDEV — CreateStaging 폴백 split-brain 봉합 + GDI 가용성 게이트 — 0.2.80 (브랜치 refactor/viewer-split)

- 0.2.79 재리뷰에서 Codex(remote#nhxsk5vr)가 3번째 필드 블로커 확인: 주 트랜잭션은 승인됐으나 `CaptureState::CreateStaging` 내부 폴백(readback.Initialize 실패 시)이 여전히 **네이티브 장치만** 재생성(res.d3d/ctx만 교체, res.d3dDevice winrt는 옛 어댑터 유지)해서 실패복구 경로에 0.2.78과 같은 cross-device split-brain이 남아 있었다.
- **수정(FIELD BLOCKER, Codex 권고 B)**: 트랜잭션 헬퍼 `RecreateCaptureDeviceOnPrimary`를 CreateStaging *위*(파일 상단 anon namespace, :69)로 이동해 장치 재생성의 유일 소유자로 일원화. CreateStaging 폴백의 네이티브-only 재생성(구 :74~99)을 헬퍼 호출로 교체 → winrt 래퍼까지 lockstep 재구축. d3d/ctx는 res 참조라 커밋 후 새 장치 반영.
- **수정(HIGH, GDI 가용성 회귀)**: 조기검사를 `needsGpuCaptureDevice = windowModeActive || backend.active != Gdi`로 게이트. GDI(별도 CPU 프로세스, 라이브 어댑터 GPU 장치 불필요)는 None이어도 재생성 강제 안 함 → 원래 서비스 가능한 GDI 재시작이 helper 실패로 중단되던 회귀 제거. GDI→WGC 폴백 시 그 경로 CreateStaging이 풀 트랜잭션을 돌려 dead-adapter 여전히 커버.
- **수정(소):** `device_adapter_output_state`에서 device==nullptr → Unknown이 아니라 **None**(확정 unusable → 재생성). QI/GetAdapter/EnumOutputs mid-walk 실패는 Unknown 유지.
- Codex Q1~Q3 답: 정상 경로 race/leak 없음(Detach 선행 + attachmentCookie 재검사 + old readback strong ref + 후속 Shutdown), 조기검사는 None에서만 동작해 정상 DXGI/GDI no-op, RDP 전환 churn은 Unknown 폴백으로 방어. HIGH 2(encoder MFT) 원장 D3 기록 정확 동의.
- 빌드: host 컴파일 OK, host_abr_test PASS, shared_core_test PASS. 임베드 0.2.80. 산출물 `dist/GNLinkSetup-0.2.80.exe`. 0.2.78/0.2.79 폐기.
- 실기 로그 게이트(Codex): "device-recreate committed"만 보지 말고 **final native adapter LUID == unwrapped WinRT adapter LUID**, staging initialized, wgc first-frame까지 묶어 확인. RDP-on 기동→off→창 선택→first-frame.

### 359) 2026-09-02 WGCDEV — 실기 검증용 LUID 대조 로그 추가 — 0.2.81 (브랜치 refactor/viewer-split)

- Codex 재리뷰가 지정한 실기 게이트("final native adapter LUID == unwrapped WinRT adapter LUID")를 로그로 구현. 블라인드 수정이라 이 로그가 split-brain 부재를 확인하는 유일 창.
- `RecreateCaptureDeviceOnPrimary` 커밋 직후: res.d3d(native)의 어댑터 LUID와, res.inspectable을 `IDirect3DDxgiInterfaceAccess::GetInterface`로 언랩한 IDXGIDevice의 어댑터 LUID를 각각 조회해 `device-recreate(reason) committed on primary adapter nativeLuid=.. winrtLuid=.. luidMatch=0/1` 출력. best-effort(probe 실패는 로그만 저하, 커밋 무영향).
- 구조상 두 핸들 모두 newD3d 1개에서 파생돼 luidMatch=1이 보장되지만, 필드 캡처가 자명하도록 명시. (네임스페이스: `::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess` — winrt 투영 아님, WRL QI로 언랩.)
- 빌드: host 컴파일 OK, host_abr_test PASS, shared_core_test PASS. 임베드 0.2.81. 산출물 `dist/GNLinkSetup-0.2.81.exe`. 0.2.78/79/80 폐기.
- 실기 확인 항목: `capture-device-adapter-stale` → `device-recreate(...) committed ... luidMatch=1` → staging → `desktop_backend=wgc_window capture-started=1` + first-callback. RDP-on 기동→off→창 선택.

### 360) 2026-09-02 WGCDEV — LUID 게이트 false-PASS 수정 + GDI→WGC 폴백 갭 봉합 — 0.2.82 (브랜치 refactor/viewer-split)

- Codex가 0.2.81 실기를 승인하면서 후속 HIGH 2건 지적 → 실기 전에 마저 처리(0.2.81 미설치라 대체).
- **진단 HIGH (LUID false-PASS)**: b29c791의 nativeLuid/winrtLuid가 {0:0} 시작이라 두 probe 모두 실패 시 값만 비교해 luidMatch=1(거짓 PASS)이 찍혔다. 또 LowPart만 로깅. **수정**: nativeKnown/winrtKnown 플래그 추가, luidMatch = 둘 다 known && High/Low 동일일 때만. 로그에 nativeKnown/winrtKnown + HighPart:LowPart 전체 + `newAdapterState`(재생성 장치가 attached output 소유하는지, headless 재생성 루프 배제) 병기. 문구 "committed on primary adapter"→"committed target=primary-or-fallback"(create_d3d11_device_for_primary_monitor가 primary 미해결 시 fallback/default로 떨어질 수 있음, Codex Low).
- **HIGH (GDI→WGC 폴백 갭)**: GDI는 조기검사를 skip(needsGpuCaptureDevice=false)하는데, GDI가 stale-but-valid 장치에서 CreateStaging 성공 후 GDI 프로세스 실패로 WGC 폴백하면 그 경로 CreateStaging도 stale에서 성공해 트랜잭션 helper 미호출 → old WGC wrapper로 갈 수 있었다("init 실패 시에만 커버"가 정확한 조건). **수정**: GDI→WGC 폴백 블록에서 backend=Wgc 설정 직후·CreateStaging 전에 device state None이면 RecreateCaptureDeviceOnPrimary 실행. intended window-mode 실기는 windowMode=true라 조기 트랜잭션이 먼저 돌아 무관했으나 완전성 확보.
- Codex Q1(트랜잭션 race/leak) PASS 확인, Q2/Q3도 정상경로 no-op·churn 방어 확인. HIGH 2(encoder MFT) 원장 D3 유지.
- 빌드: host 컴파일 OK, host_abr_test PASS, shared_core_test PASS. 임베드 0.2.82. 산출물 `dist/GNLinkSetup-0.2.82.exe`. 0.2.78~81 폐기.
- 최종 실기 게이트(Codex): 1)capture-device-adapter-stale 2)device-recreate nativeKnown=1·winrtKnown=1·luidMatch=1·newAdapterState=has 3)staging 정상 4)desktop_backend=wgc_window capture-started=1 5)first callback/encoded key/present 6)30초 stats 지속 7)input-p0 rate chain.

### 361) 2026-09-03 실기(0.2.82, RDP off): 창모드/어댑터 해결 확인, 텍스트 지연 2원인 분리 + 한글 IME 재설계 Codex 검증

- 실기(RDP off, OSLink 잠금해제, GNLink 데스크톱): 창모드 드래그 정상(WGCDEV 성공), 고화질 영상 정상, 화질 저하/30↔60 깜빡임 없음.
- **데스크톱 정적 멈춤의 진짜 원인 = 캡처 D3D 장치가 "출력 없는 어댑터"에 안착**(12:11 세션 nativeLuid=99594 newAdapterState=none, captureUnmapWait 138ms/8fps). 장치가 VDD 출력 어댑터(48969, has)에 안착한 14:12 세션은 리드백 2.6ms·영상 정상. 즉 ABR/VBR 되돌릴 필요 없음(그건 느린 캡처가 트리거한 오판이었음). 가상 디스플레이는 **이미 설치·활성**(MikeTheTech "Virtual Display Driver" 1920x1080; Parsec VDA도 설치됨) — 새로 만들 필요 없음. env REMOTE60_DESKTOP_CAPTURE_BACKEND=wgc는 자식 프로세스 미전파로 미적용(추적 보류).
- **텍스트 지연 = 2원인 분리**(사용자 관찰로 확정): ① 한글 조합이 호스트에서 안 됨(완성돼야 보임), 영어는 VK 즉시. ② 키마다 RTT 직렬(stop-and-wait). 사용자: 한/영 토글이 G7 중립화를 켰다 껐다 → 한 상태에선 호스트 live 조합 됨(호스트측 IME 가능 증거).
- 원인 코드 확정: host_input_inject.cpp:653 매 keydown ensure_foreground_ime_alphanumeric()=영어강제; viewer_window_proc.cpp WM_IME_SETCONTEXT 조합UI 억제+WM_IME_COMPOSITION 완성텍스트만; viewer_input_forward.cpp:46 VK_PROCESSKEY(조합중 자모) 미전달. 프로토콜 ControlInputEventMessage(poc_protocol.hpp:181)엔 VK만·scan/E0 없음.
- **한글 IME 재설계 Codex 검증 완료**(remote#nhxsk5vr): host-side IME 방향 승인, 단일 커밋 반대. 협상 dual-mode(HostPhysicalIme/LegacyCommittedText)+P1b reliable 키전송+PhysicalKeyV2. 상세·6단계 커밋순서·필수테스트는 docs/구현계획.md 섹션 I. 사용자 결정: 6단계 전부 구현 후 빌드 1개.
- 코드 변경 없음(이 엔트리는 진단+계획 확정). 다음: 섹션 I 1→6 순 구현, 최종 빌드 전 Codex 재리뷰.

### 362) 2026-09-03 G2 착수 — 보안입력 세션 선택에 연결상태 반영 + 서비스 SESSIONCHANGE 활성화 (Codex 검증, 빌드 전)

- 화면 없는 잠금해제(사용자 우선순위)의 1단계. Codex(remote#nhxsk5vr) 검증 반영. 로그 증거: secure_input.log 12:06:14 target session=1 source=requester (requester=1 console=13), Winlogon attach=ok·inject err=5 → 끊긴 requester(RDP 세션1) 타겟이 원인, 잠금화면 LogonUI는 console=13.
- **resolve_target_session**(secure_input_session.hpp): 인자에 requesterStateKnown/requesterActive 추가. 규칙 — requester==console면 그대로(콘솔 API가 attach 증명), 아니면 requester가 stateKnown&&active일 때만 requester, 그 외 usable console, 없으면 None. Disconnected/probe실패 requester는 console로 폴백(err=5 회피).
- **secure_input_service_main.cpp**: query_session_active()가 WTSQuerySessionInformationW(WTSConnectState)로 조회(SYSTEM이 세션 authority). target_session이 이를 resolver에 전달+로그(known/active/console). `#include <wtsapi32.h>`+`#pragma comment(lib,"Wtsapi32.lib")`.
- **서비스 SESSIONCHANGE 버그(Codex 추가 발견)**: report_service_status가 SERVICE_ACCEPT_STOP만 광고 → SCM이 SERVICE_CONTROL_SESSIONCHANGE 안 보냄 → 기존 세션변경 핸들러가 死. `SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SESSIONCHANGE`로 수정. 이벤트도 LOCK/UNLOCK/CONSOLE_CONNECT/REMOTE_CONNECT 추가, 발생 시 에이전트 stop→다음 메시지에서 재해결(연결상태 반영).
- 테스트: secure_input_session_test에 G2 매트릭스 추가(active→requester, disconnected/connecting/unknown→console, req==console→requester, transition→None 등) — PASS. 서비스 컴파일 OK.
- **빌드 안 함**(사용자 지시: 잠금해제+IME 전부 구현 후 1개). 다음: WTSConnectSession spike(비번은 호스트 로컬 저장, 뷰어는 트리거만 — 평문 전송 금지) → U5 결과경로 → 잠금상태 status → 뷰어 UI. G3 비번 전송은 보안상 N4 전까지 host-local credential 방식.

### 363) 2026-09-03 G2 hotfix — 세션변경 핸들러의 gAgent 레이스 제거(단독 소유) (Codex #363 BLOCKER)

- Codex가 b0d8d27을 열어보고 실제 BLOCKER 확인: SERVICE_ACCEPT_SESSIONCHANGE를 켜자, SCM 컨트롤 디스패처 스레드의 세션변경 핸들러가 `stop_agent()`(CloseHandle/TerminateProcess+블로킹 대기)를 호출 — 메인 스레드가 동기화 없이 gAgent(writePipe/process)를 read/write하는 동안 → 데이터 레이스 + 핸들 use-after-close. 게다가 HandlerEx 계약 위반(핸들러가 무거운 블로킹·파일 I/O 수행).
- 수정: 핸들러는 **플래그만**(gPendingSessionEvent/Id + gSessionChangePending atomics) 세우고 즉시 반환 — gAgent 미접촉, diag 파일 I/O 제거. 소유 스레드의 `ensure_agent()`가 시작 시 플래그를 소비(diag + stop_agent). gAgent는 이제 메인 스레드 단독 소유 → 레이스 구조적 제거. (ensure_agent는 이미 매 forward마다 target_session 재해결+desktop 비교로 재생성하므로 소비는 그 위에 얹힘.)
- STOP 경로는 기존대로 gClientPipe atomic exchange + CancelIoEx(선재, 표준 패턴)라 별개.
- 빌드: 서비스 컴파일 OK. (전체 빌드는 잠금해제+IME 완료 후 1개.)
- 한계: 순수 resolver 단위테스트는 이 동시성 회귀를 못 잡음 — 단독 소유로 by-construction 보장(핸들러가 gAgent를 아예 안 만짐). 실기 fault(에이전트 write 중 세션변경)로 최종 확인 예정.
- 다음: Codex의 G3 sealed-box 설계 답(seq 901) 반영 → 잠금해제 구현 → 한영 IME.

### 364) 2026-09-03 잠금해제 1단계 — sealed unlock 크립토/리플레이 모듈(CNG) + 단위테스트 (Codex 검증)

- Codex(remote#nhxsk5vr) G3 설계 판정 반영. 사용자 결정: 비번은 클라 DPAPI 저장, 비번만 sealed-box 암호화 전송, TOFU 생략(능동 MITM 위험 수용 — "인증 없는 sealed unlock v1").
- 신규 `sealed_unlock.hpp/.cpp`(정적 lib `remote60_sealed_unlock`, bcrypt): P-256 ECDH(BCRYPT_ECDH_P256, 공개점 X||Y 64B 고정 와이어·CNG struct 미전송) → **원시 시크릿 추출 후 HKDF-SHA256 직접 구현**(HMAC-SHA256, RFC5869; CNG HKDF-on-secret-handle는 이 환경에서 미도출이라 우회) → AES-256-GCM(12B 랜덤 nonce, 16B tag). AAD/KDF-info는 struct memcpy 금지, 명시 LE 바이트 직렬화(challengeId/세션/락제너레이션/키해시 등 공개 컨텍스트만; peer IP/port·streamGeneration 제외). 비번 평문은 고정 258B 패딩(길이 side-channel 은닉). RAII 핸들 + SecureZero.
- 리플레이 상태머신 `UnlockChallengeState`(순수): 단일 미결 챌린지, 소비 CAS(exactly-once), 만료/세션·락제너레이션 불일치/중복 거부. 무효 tag는 소비 안 함(DoS 방지)은 상위 로직에서.
- 단위테스트 `sealed_unlock_test`: 양측 동일 32B 유도, GCM 왕복, ciphertext/tag/AAD/salt 변조 전부 거부, not-on-curve 공개키 거부, 패딩 경계, 리플레이 매트릭스(unknown/valid/wrong-cookie/wrong-gen/expired/consumed/supersede/clear) — **PASS**.
- 빌드 안 함(전체는 잠금해제+IME 완료 후 1개). 다음(Codex 커밋순): 2 프로토콜(challenge/sealed/status 메시지+capability, default off), 3 서비스 unlock IPC+WTSConnectSession, 4 잠금상태 감지, 5 뷰어 DPAPI 저장, 6 unlock 흐름, 7 뷰어 UI, (8 Winlogon fallback 보류).

### 365) 2026-09-03 잠금해제 — 토폴로지 세대 카운터 추가 + G2 상태 기록 (Codex #365 보강)

- Codex가 d9a2444를 메모리안전 hotfix로 승인. 단 남은 의미적 TOCTOU 지적: 소유 스레드가 pending=false 확인 후 WriteFile 하는 사이 세션이 바뀌면 한 건이 old 세션으로 갈 수 있음 — 일반 입력엔 허용(작은 라우팅 창)이나 **비번/unlock 명령엔 불가**.
- 반영: 서비스에 `gSessionTopologyGeneration` atomic 추가, 세션변경 핸들러가 fetch_add(release). 잠금해제 경로가 챌린지 발급 시 snapshot하고 복호 직전·직후+WTSConnectSession 직전 재검증(RejectedStaleTopology+비번 zero+새 챌린지) — unlock은 fire-and-forget forward 재사용 금지. 상세는 구현계획.md 섹션 I "토폴로지 세대 3중 재검증".
- G3 전체 설계 답은 Codex seq=903(P-256/HKDF/GCM 조건부 승인 + on-demand one-shot challenge + WTSConnect 우선 + async U5 + control-level capability). 커밋1(edab229)은 이미 이 기준.
- 서비스 컴파일 OK. 빌드 안 함(전체 후 1개). 다음: 2단계 프로토콜(challenge/sealed/status 메시지 — topologyGeneration 필드 포함, capability, default off).

### 366) 2026-09-03 잠금해제 2단계 — 프로토콜 메시지 + 능력협상 + 매핑 단일소스 + 테스트 (Codex 커밋2)

- poc_protocol.hpp: MessageType 40~45(ControlUnlock ChallengeRequest/Challenge/SealedRequest/Accepted/StatusRequest/StatusResult) + UnlockStage enum(0~12, SessionUnlocked=권위 성공) + 능력플래그 kCaptureFlagUnlockSealedV1(Pong flags, control-level이라 TCP/UDP 공통, 구peer엔 미송신). 전 메시지 #pragma pack(1) 고정 레이아웃 + sizeof static_assert(24/196/390/32/32/36)로 드리프트 방지(Codex "padding/sizeof 의존 금지" 충족).
- sealed_unlock UnlockContext에 topologyGeneration 추가 + AAD/KDF 직렬화 반영(#365 바인딩).
- `unlock_wire.hpp`: 챌린지 wire ↔ UnlockContext 매핑 **단일 소스**(FillChallengeFromContext/ContextFromChallenge) — 호스트/클라가 동일 AAD/KDF 만들도록(드리프트=모든 open 실패, Codex 지적).
- `unlock_protocol_test`: wire 바이트 왕복 동일, 양측 AAD/KDF 동일, challengeId 다르면 AAD 다름, Fill↔Context 역함수 — PASS. sealed_unlock_test(KAT 포함) 재확인 PASS.
- 빌드 안 함(전체 후 1개). 다음 커밋3: 서비스 unlock duplex IPC + WTSConnectSession spike(로컬), U5 결과경로.

### 367) 2026-09-03 잠금해제 3단계 — 서비스 언락 실행부(WTSConnectSession + duplex 파이프) (Codex 커밋3)

- secure_input_service_main.cpp에 언락 전용 스레드 + duplex 파이프(`\.\pipe\GNLinkUnlock`, 입력 파이프와 분리) 추가. 이 스레드가 언락 크립토/챌린지 상태 단독 소유, WTSConnectSession(blocking)도 여기서만.
- 흐름: ChallengeRequest → (requester 세션이 실제 Locked인지 WTSSessionInfoEx로 확인, 아니면 RejectedPolicy) ECDH 키+salt+challengeId 생성, topologyGeneration snapshot, account_id(WTSUserName+Domain FNV) bind, Issue → ChallengeResponse. SealedRequest → 챌린지 Verify + topology 재검증(복호 전) → DeriveAesKey+AesGcmOpen(실패=DecryptFailed, 챌린지 미소비, 5회 시 폐기) → topology 재검증(복호 후) → Consume(exactly-once) → UnpackPassword → **WTSConnectSessionW(requester, console, pw, TRUE)** → lock 상태 폴링으로 SessionUnlocked/WtsConnectAccepted 판정, 실패는 gle로 AuthFailed/InternalError. 비번 버퍼 전부 SecureZero. terminal 결과 requestId별 캐시(중복 재실행 방지).
- 서비스에 remote60_sealed_unlock 링크. 컴파일 OK. 빌드 안 함(전체 후 1개).
- WTSConnectSession의 실제 잠금해제 동작은 실기 spike 필요(Codex). 다음: 호스트 릴레이(뷰어 control ↔ 언락 파이프) + Pong 능력/잠금상태 광고 → 뷰어 크립토/DPAPI/UI → 한영 IME → 빌드.

### 368) 2026-09-03 잠금해제 — 호스트 릴레이(뷰어 컨트롤 ↔ 서비스 언락 파이프) (Codex 커밋3/6)

- `host_unlock_relay.hpp/.cpp`(신규): 백그라운드 워커 1개 + duplex 파이프(GNLinkUnlock) 소유. ChallengeSync(컨트롤 스레드 동기, 빠름) / SealedAsync(즉시 반환+워커가 WTSConnectSession 수행) / PollResult(폴링). 호스트는 키/비번 절대 안 봄(불투명 릴레이). 세션 쿠키로 챌린지↔sealed 바인딩.
- host_control_session.cpp: 컨트롤 루프에 정적 릴레이 + 커넥션별 쿠키, Pong에 kCaptureFlagUnlockSealedV1 광고, ControlUnlockChallengeRequest→ChallengeSync→Challenge, SealedRequest→SealedAsync+즉시 Accepted(+ciphertext SecureZero), StatusRequest→PollResult→StatusResult. WTSConnectSession blocking이 컨트롤 채널을 막지 않도록 sealed는 비동기+폴링(Codex HOL 경고 반영).
- 호스트 컴파일 OK. 빌드 안 함(전체 후 1개). 다음: 뷰어(클라) 크립토 클라 + DPAPI 비번저장 + 언락 UI + 컨트롤 송수신 → 한영 IME → 빌드.

### 369) 2026-09-03 한영 IME 1 — 프로토콜 PhysicalKey + 호스트 스캔코드 주입(호스트측 IME) (Codex 커밋4/5 호스트분)

- poc_protocol.hpp: MessageType::ControlPhysicalKey(46) + ControlPhysicalKeyMessage(down/vk/scanCode/flags, sizeof 28 static_assert) + 능력플래그 kCaptureFlagHostImeV1(0x10, Pong 광고).
- host_input_inject.cpp: `inject_physical_scan_key(scan,down,extended)` — KEYEVENTF_SCANCODE+wVk=0로 주입해 호스트 레이아웃/IME가 해석·조합. **IME 중립화 없음**(그 경로는 ControlInputEvent 전용). 한글이 호스트 앱에서 live 조합되고 한/영 키도 실제 키보드처럼 토글됨.
- host_control_session.cpp: ControlPhysicalKey 핸들러 → inject_physical_scan_key + send_input_ack.
- 호스트 컴파일 OK. 안전: 기본 동작 불변(뷰어가 옵트인+호스트 능력 확인 시에만 PhysicalKey 전송, 아니면 기존 VK 경로). 빌드 안 함(전체 후 1개).
- 다음: 뷰어측 — 옵트인 시 로컬 IME 분리(ImmAssociateContext NULL) + WM_KEY→PhysicalKey(scan) 전송 + 클라 조합경로 억제. 그다음 빌드.

### 370) 2026-09-03 한영 IME 2 — 뷰어측 호스트-IME 경로(옵트인) + 로컬 IME 분리 (Codex 커밋4)

- 뷰어(클라): host_ime_mode(ctx) = env REMOTE60_HOST_IME=1 && 호스트가 kCaptureFlagHostImeV1 광고 시에만 true(기본 off → 기존 VK/조합텍스트 경로 100% 불변, 회귀 0).
- host-IME 모드에서 WM_KEYDOWN/UP/SYSKEY*: 로컬 IME 1회 분리(ImmAssociateContext NULL, 키가 VK_PROCESSKEY 아닌 raw로 옴) + WM_KEY lParam의 scan(16-23)/E0(24)/repeat(30) 추출해 ControlPhysicalKey(enqueue_physical_key) 전송. WM_IME_COMPOSITION은 host-IME 모드면 skip(호스트가 조합).
- shared core: ControlOutboundActionKind::PhysicalKey + ControlOutboundAction/QueuedControlInputMessage에 physicalKey 필드, NextAction 매핑, send_control_action 케이스(응답=ControlInputAck). PhysicalKey는 type이 ControlInputEvent가 아니라 move coalesce 대상 아님.
- 뷰어 세션에 hostImeSupported(pong에서 셋). viewer_control_client.cpp handle_pong 반영.
- 빌드: shared_core_test PASS(coalesce 무영향), 뷰어/호스트 컴파일 OK. 빌드본은 전체 후 1개.
- 참고: P1b(키당 RTT 제거, 부드러운 타이핑)는 미포함 — 현재 host-IME는 조합이 화면에 보이나 키당 왕복 지연 잔존. 후속.

### 371) 2026-09-04 빌드 0.2.83 — 한영 IME(옵트인) + 잠금해제 백엔드 전체 (goal: 한영까지+빌드)

- 사용자 goal "한영까지 다 작업하고 빌드해서 설치파일까지 뽑고 보고". 이번 사이클 산출: 잠금해제(G2~서비스~호스트릴레이) + 한영 IME(호스트+뷰어, 옵트인) 통합 빌드.
- 단위테스트 전부 PASS: sealed_unlock(KAT), unlock_protocol, secure_input_session(G2 매트릭스), shared_core(coalesce), host_abr.
- installer Release 빌드, 임베드 0.2.83(GNLinkHost/Setup), 페이로드 4종 이번 세션 신규 빌드. 산출물 `dist/GNLinkSetup-0.2.83.exe`.
- **테스트 가능**: 한영 IME — 뷰어에서 env REMOTE60_HOST_IME=1 설정 후 접속하면 host-side IME(한글 live 조합, 한/영 실제 토글). 기본 off라 미설정 시 기존 동작 그대로(회귀 0).
- **미완(정직)**: 잠금해제 뷰어 UI(비번 입력창+트리거+클라 크립토/DPAPI) 미구현 — 백엔드(서비스 WTSConnectSession+호스트릴레이+프로토콜)는 완료·dormant. WTSConnectSession 실제 해제 동작은 실기 spike 필요. P1b(키당 RTT 제거)도 후속.

### 372) 2026-09-04 0.2.83 리뷰 반영 — BLOCKER 6종+HIGH 다수 수정 → 0.2.84 (Codex #370)

- Codex가 0.2.83에서 실기 보류 판정. 반영:
  - **B2(치명)** Pong이 kCaptureFlagHostImeV1 미광고 → host-IME가 아예 dormant였음. 이제 광고(+입력정책/키상태 수정 후 활성). **즉 0.2.83에선 한영이 실제로 안 됐음.**
  - **B1** ControlUnlockAccepted requestId가 SecureZero(&req) 뒤 읽어 항상 0 → zero 전 보존.
  - **B3** 서비스 unlock 스레드가 ConnectNamedPipe/ReadFile에 블록돼 STOP hang → gUnlockPipe 전역화, STOP에서 CancelIoEx+close. WTSConnectSession bWait TRUE→FALSE(블로킹 제거, lock 폴링으로 확인).
  - **B4** 호스트 릴레이 Stop이 워커 ReadFile 블록에 hang → pipeHandle_ atomic, Stop에서 CancelIoEx.
  - **B5** PhysicalKey가 입력정책 우회 → injectionEnabled + 비보안 데스크톱 + (desktop모드 || 선택창=foreground)일 때만 주입(오포그라운드 앱 유출 방지). ack은 전송 receipt.
  - **B6/IME** 뷰어 physical pressed-set 추적 + 포커스 상실/모드전환 시 release-all(modifier stuck 방지). WM_SETFOCUS에서 IME 조기 분리(첫 글자 손실 방지). WM_SYSKEYDOWN에도 on_local_hotkey(Ctrl+Alt 계열). WM_DESTROY에서 이전 HIMC 복원.
  - HIGH: 서비스 topology **3중** fence(WTSConnect 직전 추가) + 챌린지 정책(끊긴 requester+유효 console만) + account unknown 거부+구분자. sealed_unlock Verify consumed **복합키**(cookie+lockGen+challengeId) + 테스트. 상태값 문서(UnlockStage) 정정.
  - 남은 dormant unlock HIGH(릴레이 복합키/TTL, jobId 계약, IPC 검증)와 Winlogon fallback 미구현은 원장 D4로 이관(뷰어 UI 구축 시).
- WTSConnect 인자방향·crypto 코어는 Codex 승인. 0.2.83 폐기.

### 373) 2026-09-04 빌드 0.2.84 — 리뷰 반영본, 한영 IME 실동작 (0.2.83 폐기)
- 단위테스트 5종 전부 PASS(sealed_unlock KAT·consumed복합키, unlock_protocol, secure_input_session, shared_core, host_abr). installer Release, 임베드 0.2.84. 산출물 `dist/GNLinkSetup-0.2.84.exe`(0.2.83 삭제).
- 0.2.83 대비: host-IME 실제 발동(Pong 능력광고+정책게이트+modifier release+포커스 조기분리+SYS hotkey), 서비스/릴레이 종료 hang 제거, unlock topology 3중·챌린지정책·consumed복합키.
- 테스트법: 뷰어에 env REMOTE60_HOST_IME=1 후 접속→메모장 한글/영어. 기본 off는 기존동작.

### 374) 2026-09-04 0.2.84 재리뷰 반영 — 종료 race 정공법+키스턱+sealed requestId → 0.2.85 (Codex #2차)

- Codex 2차: 0.2.84도 보류. BLOCKER A/B(종료 race), C(키업 스턱), D(sealed requestId 미결합), HIGH들 반영:
  - **A(서비스 종료)**: CancelIoEx+atomic 방식 폐기. unlock 워커가 파이프를 **단독 소유·close**, service_main 종료 시 워커 OS 스레드에 **CancelSynchronousIo 루프**(스레드 종료까지)로 ConnectNamedPipe/ReadFile 언블록. STOP 핸들러는 파이프 안 건드림. WTS는 bWait=FALSE(2단계 유지).
  - **B(릴레이 종료)**: 동일 — workerThreadHandle_ 게시, Stop이 CancelSynchronousIo 루프, 워커 단독 close(핸들 재사용 ABA 제거).
  - **C(키업 스턱)**: 호스트가 주입 성공한 down(scan|E0)을 physicalDown 집합에 보유. 매칭 up은 **게이트 닫혀도** release, 연결 종료 시 release-all. 뷰어도 실제 눌렀던 키만 up 전송(spurious SYS up 억제), WM_DESTROY release.
  - **D(sealed requestId)**: 복호 전 req.requestId == us.ctx.requestId 필수 검증(외부 id만 바꾼 재사용 차단).
  - HIGH: 챌린지 정책에서 requester state unknown=fail-closed(거부), 요청자 파이프 교체 시 topologyGeneration bump.
- 남은(원장 D4, unlock UI dormant): 릴레이 결과맵 복합키/TTL·jobId·IPC검증. IME 후속: capability-after-focus 즉시 detach(pong post), 재접속 시 hostImeSupported reset+HIMC restore, P1b, 초기 영문정렬. Winlogon fallback 미구현.
- 단위테스트 5종 PASS.

### 375) 2026-09-04 3차 리뷰 반영 — 미매칭 키업/set 일관성/rate-limit/WTS identity fence → 0.2.86
- Codex 3차: 0.2.85 기본경로 설치 OK, REMOTE60_HOST_IME=1/unlock 필드판정 전 HIGH 마감 요구. 반영:
  - 호스트: 미매칭 physical key-up은 전송/주입 안 함(physicalDown에 있는 것만 release).
  - 뷰어: enqueue_physical_key가 bool 반환, 실제 큐 수락 시에만 gPhysicalDown insert(입력 disabled 중 stale 방지). SYSKEYUP도 set 기반이라 소비 하튼키의 up 미전송.
  - 뷰어: shutdown 시 hostImeSupported=false 리셋.
  - 서비스: 챌린지 rate-limit(500ms, capability가 wire 광고되므로), WTS 직전 gRequesterSession/console identity 직접 재검증(generation 창+누락 이벤트 마감).
  - LOW: dead gUnlockThreadHandle 전역 제거(service_main이 native_handle 직접 사용), 릴레이 workerThreadHandle_ join 후 clear.
- 잔여(IME 완전자연): capability-after-focus 즉시 detach(pong→UI post)·재접속 HIMC restore·초기 영문 sync·P1b. 원장 D4(unlock UI 전 릴레이 복합키/TTL/jobId/IPC검증). host physical 상태의 TCP/UDP rollover·release 실패 복구는 후속.
- 단위테스트 PASS.

### 376) 2026-09-04 4차 리뷰 반영 — 큐 오버플로 키edge 보존 + 뷰어 키업 erase-after-success → 0.2.87
- Codex 4차: 0.2.86 기본모드/dormant 승인. REMOTE60_HOST_IME=1 필드 전 2건:
  - **큐 오버플로**: ClientInputQueue::Enqueue가 256 초과 시 pop_front(키업 유실 가능) → **이동(kind1)만** 희생, key/button/physical edge는 보존(희생할 이동 없으면 큐 일시 성장, 들어오는 게 이동이면 그것만 드롭). 회귀테스트 추가(키업 flood 후 생존, 이동 coalesce) PASS.
  - **뷰어 키업**: erase 후 enqueue였던 것 → enqueue 성공 시에만 erase, 실패 시 set 유지(재시도). release_all_physical도 성공분만 제거.
- 잔여(필드/후속): capability-after-focus 즉시 detach(pong→UI post, 결정론), 재접속 HIMC restore, host physical의 TCP/UDP rollover epoch·release 실패 복구, 초기 영문 sync + P1b. 원장 D4(unlock UI). challenge limiter idempotent 재응답 개선.
- 단위테스트 전부 PASS.

### 377) 2026-09-04 잠금해제 뷰어 UI 완성 — 사용자가 실제 사용 가능 → 0.2.88
- 사용자 "잠금해제 왜 미완?" → 백엔드만 있고 뷰어 UI가 없었음. 이번에 완성:
  - `viewer_unlock.hpp/.cpp`(신규): DPAPI(CryptProtectData user scope, CRYPTPROTECT_UI_FORBIDDEN)로 호스트 비번 로컬 저장/로드/삭제(%LOCALAPPDATA%\GNLink\unlock_*.cred), ES_PASSWORD 모달 입력창, run_unlock_exchange(ChallengeRequest→클라 ECDH keygen+DeriveAesKey+PackPassword+AesGcmSeal→SealedRequest→StatusRequest 폴링). 평문 비번 즉시 SecureZero.
  - 프로토콜 플러밍: ControlOutboundActionKind::Unlock{Challenge,Sealed,Status}Request + TcpControlResponseKind::Unlock{Challenge,Accepted,StatusResult} + send/recv 케이스.
  - 트리거: 뷰어 Ctrl+Alt+U(비번 없으면 입력창→DPAPI 저장) → unlockRequested 플래그 → 컨트롤 스레드가 load+run_unlock_exchange+SecureZero+상태 로그.
  - 릴레이 IPC 응답 검증(magic/size/kind/requestId) 추가(D4 일부).
- 이제 흐름 완결: 뷰어 Ctrl+Alt+U → 비번(암호화 sealed) → 호스트 릴레이 → 서비스 복호 → WTSConnectSession → 결과 폴링. 비번은 클라 DPAPI 저장·sealed 전송(평문 미노출; 인증없는 sealed라 능동MITM엔 취약, 사용자 수용).
- 단위테스트 전부 PASS. 남은 D4: 릴레이 결과맵 (cookie,requestId) 복합키+TTL(다중세션), jobId 계약. WTSConnectSession 실제 해제는 실기 확인 필요.

### 378) 2026-09-04 잠금해제 리뷰 안전수정(계정잠금 방지 등) + 사용자 redirection(picker 잠금해제) 기록
- Codex 0.2.88 리뷰: 위험 버그 다수. 반영(트리거 위치와 무관한 안전/정합):
  - **계정잠금 방지**: AuthFailed/DecryptFailed 시 clearCredential=true → 저장 비번 삭제(자동 재사용 금지). load 실패(손상 파일)도 삭제 후 재입력 유도. 과길이 비번 거부(truncate 금지).
  - **응답 correlation**: challenge/accepted/status의 requestId를 현재 요청과 대조.
  - **능력 게이트**: Pong의 kCaptureFlagUnlockSealedV1을 unlockSupported에 저장, 미지원 호스트엔 unlock 미전송(구호스트 desync 방지).
  - **프롬프트 종료**: WM_DESTROY의 PostQuitMessage 제거(done 플래그, 앱 WM_QUIT 재전달) — UI 스레드 quit 오염 방지.
- **사용자 redirection**: 잠금해제 버튼을 뷰어 영상 화면(Ctrl+Alt+U)에 두면 RDP 잠금 시 영상이 검어 무용 → **대상선택(picker) 화면에 잠금해제 메뉴**로 이동해 영상과 무관하게. + picker "대상선택 시 멈춤"(PICK) 미해결도 지적.
- 남은(다음): picker에 unlock 진입점, PICK 멈춤 수정, D4(릴레이/서비스 (cookie,requestId) 복합키+TTL, jobId), hostId 네임스페이스(directoryHostId), WTS 실기. 아직 unlock 배포 빌드 보류(트리거 picker 이동 후).

### 379) 2026-09-04 정적화면 멈춤 근본원인 수정 — stale-reference 오탐(느린 소스를 혼잡으로 오판) → 0.2.90
- 증상: 정적 화면에서 글씨를 계속 쳐도 화면이 5초 넘게 멈췄다가 GMux 등 큰 변화가 생기면 풀림. "1~2초에 한번씩 갱신"되는 느낌. 지우는 게 안 보여 과다 삭제.
- 진단(호스트+뷰어 로그 대조, RDP OFF 유효):
  - 뷰어 로그(NAS `/opt/gnlink/remote60-directory/logs/shotan/<device>/viewer.log`)에서 결정적 증거:
    `dropPm=0`(UDP 유실 거의 없음), 키프레임 정상 조립·디코드(`decodeUs~16ms`), `decodeQueueLagUs=103~364us`(클라 전혀 안 밀림), `presentBacklog=0`(더 새 프레임 없음)인데도
    `stale-reference recovery count=1694↑`가 폭주하며 매번 `reason=6(stale_reference_gap)` 키프레임 요청+디코더 리셋+델타 드랍 → `frameGapUs=1.2초` 프리즈. 호스트는 이에 응답해 230KB IDR을 초당 여러 번 뱉어 storm.
  - 근본원인: `staleBehindLatestUs`(현재 프레임이 "본 최신 캡처"보다 뒤처진 정도)가 `kStaleCaptureDropUs=50ms`를 초과하면 stale로 드랍/복구. 그런데 타이핑 중 호스트는 **변경기반**이라 프레임을 200~400ms 간격으로 보냄 → 모든 프레임이 50ms 초과로 오판. 실제로는 그 프레임이 가장 최신 라이브 콘텐츠이고 클라는 안 밀림.
- 수정(`viewer_frame_gate.cpp` admit): **stale-behind-latest 판정을 조밀 도착(denseArrival)일 때만 적용**. 희소 도착(recvGap>150ms=느린 소스)엔 억제 — `note_packet`의 "sparse=source stall" 논리와 동일. `staleBehindPresented`(이미 표시한 것보다 오래된 프레임=되감기 방지)는 그대로 유지해 화면 되감기 없음. 진짜 백로그/리오더(조밀)는 여전히 정상 복구.
- 회귀테스트 추가(`viewer_frame_gate_test.cpp` `test_sparse_slow_source_decodes_not_stale`): 희소+behind-latest→Decode(리셋·reason6 없음), 조밀+behind-latest→여전히 DropStale+복구. 프레임게이트 단위테스트 9종 전부 PASS.
- 빌드: 0.2.89→0.2.90 범프, `remote60_installer` 재빌드(GNLinkViewer.exe 포함), payload 버전 0.2.90 확인, `dist/GNLinkSetup-0.2.90.exe`.
- 부수: NAS(디렉터리 서버 192.168.0.6, 로그 `/opt/gnlink/remote60-directory/logs/`)가 host/client/viewer 로그 수집처임을 CLAUDE.md·메모리에 기록(원격 뷰어 로그는 이 PC에 없음).
- 남은: host physical rollover, 한/영 토글, GMux창 입력, picker unlock 이동+PICK 멈춤, D4.

### 380) 2026-09-04 host-side IME 기본 활성화(V2) — 한글 실시간 조합·GMux 입력·한/영 토글 → 0.2.91
- 로그 진단(호스트+NAS viewer.log): 설치본 런처가 REMOTE60_HOST_IME 미설정 → host_ime_mode=false → **host-side IME 꺼진 채 클라측 IME 경로**(kind=5/6 VK + 완성 utf16). 그래서 노트패드 영어만 즉시, 한글은 완성돼야 표시, GMux(Electron)는 SendInput만 받고 호스트 IME 한글모드가 영문을 자모로 조합 → 영어 안 보임.
- 검증용Codex와 설계 합의(6조건). 사용자 "다 만들고 한 번에 빌드" 선택. 반영:
  - **B 공용 focus helper**: `host_input_inject`에 `target_has_focus(HWND)`(target==fg || GA_ROOT==fg) 신설. 키·텍스트·physical 3경로가 공유 → 창모드 GMux 입력 게이트가 exact-HWND에서 root 인식으로 확장(GMux 창모드 입력 먹통 해소). GA_ROOTOWNER는 별도 필드테스트 후로 보류.
  - **Edge1 한/영·한자 make-only pulse**: ControlPhysicalKey flags bit2. 뷰어가 실제 scan 0xF1/0xF2만 make-only로 감지→down 1회 펄스, 추적/release 제외, 후속 up drop. 호스트도 make 1회 주입·미추적. RightAlt/RightCtrl(38/1D+E0)은 정상 make/break 유지. 스턱키 방지.
  - **capability V2**: kCaptureFlagHostImePulseStateV2(0x20). 호스트 Pong이 V1+V2 광고. 뷰어는 **V2 호스트에만** 신 pulse/StateRequest 사용(구 0.2.87~90 V1 호스트엔 legacy client IME 폴백 — 스턱/HOL 방지).
  - **Edge4 3상태 전환**: ImeMode Disabled(0)/Active(2). Pong V2+opt-in → imeEnterPending. 제어루프 EN-align 펌프가 ControlImeStateRequest(action=setEN) 왕복(입력 드레인보다 먼저 → 순서로 EN-first 보장) → 응답 후 UI스레드에 kMsgHostImeActivate post → 로컬 IME detach + imeMode=Active(원자적). 종료/재접속 → kMsgHostImeDeactivate(physical release + HIMC restore + Disabled). host_ime_mode=(imeMode==Active).
  - **Edge2 초기 EN sync + target fence**: 호스트 `host_ime_align_query(setEnglish)` — AttachThreadInput→ImmGetContext→Get Open/Conversion, **set 직전 foreground/thread 재확인**(바뀌면 StaleTarget, 이전 앱 IME 안 건드림), known일 때만 ImmSetOpenStatus(FALSE)+NATIVE/FULLSHAPE clear 후 **actual 재-query**해 응답. API성공만으로 EN단정 안 함. unknown이면 '?'.
  - **Edge3(최소)**: 호스트-권위 {status,open}을 ControlImeStateResponse로 전달·로그(초기정렬 성공/실패 가시화). 뷰어 imeReportedOpen 저장(툴바 표시는 후속).
  - **telemetry 개인정보**: 일반 문자키 vk/scan 연속로그 안 함. 토글/정렬/활성전환만 로그.
  - **런처**: GetEnvironmentVariable 미설정 시에만 REMOTE60_HOST_IME=1(0/false/off opt-out 보존). **field-test default**.
- 프로토콜: MessageType 47/48(ControlImeStateRequest/Response), ControlImeStateRequestMessage(28)/ResponseMessage(32) static_assert. ControlOutboundActionKind::ImeStateRequest + TcpControlResponseKind::ImeStateResponse + send/recv 케이스.
- 빌드: Stream/Host/Viewer/Client/Installer 전부 컴파일 성공. 단위테스트 shared_core·frame_gate PASS. 0.2.90→0.2.91, `dist/GNLinkSetup-0.2.91.exe`(host 버전·런처 env 확인).
- 남은/실기검증: 전용 한/영·한자키의 실제 scan/E0값·토글 실동작(F1/F2 pulse), Electron/GMux에서 ImmSetOpenStatus 실효성, 초기 EN 정렬 성공률, GA_ROOTOWNER(owned modal), 툴바 EN/KR/? 표시. Codex 필드 합격기준(V1구호스트 pulse/StateRequest wire 0, trackedDown peak 0 등) 로그로 확인 예정. (Codex 사용량 리미트로 이후 리뷰는 보류.)

### 381) 2026-09-04 정적화면 "갑자기 멈춤" 2차 근본수정 — idle 시간을 디코드 밀림으로 오인 → 0.2.92
- 사용자: 0.2.91에서 한/영·한자·재접속 OK지만 **정적화면에서 갑자기 7초+ 멈춤 재발**. 고화질 아님. "하나 고치면 회귀".
- 로그 진단(host+NAS viewer, RDP off): 멈춤 순간 **호스트는 wire 프레임을 계속 전송**(seq 끊김 0) → 호스트 아님. 뷰어가 `congestion state=congested reason=decode_queue decodeQueueLagUs=8.6초` → catchup → 모든 델타 드랍(waitForKeyFrame) → 299KB 키프레임 7.7초 대기 → present gap 7.69초. stale-reference recovery=0(내 0.2.90 수정과 무관), dropPm 미미, RTT 2.7ms(대역폭 아님).
- 근본원인: `decodeQueueLagEstimateUs = captureQpc - lastPresentedCapture`. 정적화면은 몇 초간 캡처가 없어 lastPresentedCapture가 옛값 → 활동 재개 시 그 **idle 시간(8초)을 디코드 백로그로 오인** → 가짜 혼잡 → catchup → 멈춤.
- 수정(`viewer_frame_gate.cpp` + `viewer_frame_gate_state.hpp`):
  - **idle 재기준점(presentAnchorFloorUs)**: recvGap>250ms(소스 idle)이고 **congestion Normal일 때만** floor를 이 재개 프레임 캡처로 갱신. decodeQueueLag는 `max(presentedCap, floor)` 기준으로 계산 → idle 시간이 백로그로 안 잡힘. 희소(타이핑)는 매 프레임 재기준→lag~0, 조밀 실백로그는 floor가 안 갱신돼 정상 감지. **혼잡/복구 중엔 floor 미적용**(의도적 앵커 고정 유지, recovery-timeout 보존).
  - **stale-behind-latest 정교화(0.2.90 보강)**: 희소일 때 무조건 억제 → **"클라가 실제로 따라갈 때(decodeQueueLag≤50ms)만" 억제**. 고비트레이트로 밀린 경우(희소여도 lag 큼)는 다시 drop해 catch-up(고화질 catch-up 방해 방지).
- 회귀테스트 2종 추가(idle-resume 가짜혼잡 없음, 희소 slow-source decode) + 기존 recovery-timeout 포함 **프레임게이트 9→11종 PASS**. shared_core PASS.
- 빌드 0.2.91→0.2.92, `dist/GNLinkSetup-0.2.92.exe`.
- 참고: 고화질 12Mbps 멈춤은 회선(기가랜)이 아니라 순간 버스트 UDP 유실/수신처리 문제로 보이며 ABR-override(runtime-config가 ABR 강하를 되돌림)와 별개 과제. 작업관리자창 미표시는 조사 중(시스템 핫키 클라 OS 가로채기 의심).

### 382) 2026-09-04 정적화면 키프레임 churn 수정 — 0.2.92의 stale-drop 과민 반응 되돌림 → 0.2.93
- 사용자: 0.2.92에서 에뮬(LDPlayer) 끈 뒤에도 정적화면이 서서히 느려짐/끊김. "회귀".
- NAS 로그 진단(host=8ec6ecb1, viewer=68f79d01): 호스트 최근 40프레임 중 **키프레임 7개(17%!)** 각 ~200KB, 뷰어 `stale-reference 115회`+`keyReq 25`+dropPm>0. RTT 3ms(대역폭 아님). = **키프레임 churn**: stale-reference→키프레임요청→200KB IDR 버스트→수신 청크 유실→조립실패→재요청 악순환.
- 원인: 0.2.92의 `clientKeepingUp`(희소 도착이어도 decodeQueueLag>50ms면 stale-drop+IDR)이 **정적화면의 순간 지연에도 매번 발동** → 키프레임 폭주. (LDPlayer는 화면부하(38KB·29acquires)로 별개 기여했고 끈 뒤에도 churn은 남음.)
- 수정: stale-drop을 **조밀 도착일 때만**으로 되돌림(0.2.90 동작). 고화질 영상 프리즈는 사실 0.2.92의 **idle-reanchor**가 잡은 것이므로 유지 → 영상도 안 돌아옴. 즉 0.2.93 = 0.2.90 stale-drop + 0.2.92 idle-reanchor.
- 프레임게이트 테스트 11종 PASS. 빌드 0.2.92→0.2.93, dist/GNLinkSetup-0.2.93.exe.
- 로그 규칙 재확인: viewer/host/apk 전부 **NAS**에서 확인(로컬 host_app.log 금지) — CLAUDE.md·메모리 강화(커밋 3d0b766).

### 383) 2026-09-04 UAC 다녀온 뒤 키프레임 churn 수정 — stale-reference recovery 쿨다운 → 0.2.94
- 사용자 발견: "UAC 화면 갔다 돌아오면 그때부터 끊김 시작". NAS 로그 확정: stale-reference UAC 전(17:28-37)=0, 후(17:38-42)=50.
- 메커니즘(NAS host 8ec6ecb1+viewer 68f79d01): UAC=보안데스크톱→DXGI 0x80070005 차단→WGC 폴백→3s 뒤 DXGI 복귀. 복귀 직후 밀린 상태에서 뷰어가 stale-reference recovery를 **1~2초마다** 발동, 매번 **285KB IDR(~300청크)** 요청. **키프레임 전송시간(~260ms)이 밀린 양(~260ms)과 비슷해 자기지속 폭주**(따라잡으려는 키프레임이 밀림을 다시 만듦). gen 변경/타임라인 점프 아님(staleBehindLatestUs 107~362ms).
- 수정(`viewer_frame_gate.cpp`+state+constants+startup): **stale-reference recovery 쿨다운(기본 1s, env REMOTE60_NATIVE_STALE_RECOVERY_MIN_INTERVAL_US)**. 쿨다운 중 behind-latest in-chain 프레임은 reset+IDR 대신 **순서대로 디코드**(약간 지연 감수, 폭주 차단). 백로그는 idle 때 자연 배수, 진짜 큰 백로그는 congestion 경로가 처리. staleBehindPresented(되감기 방지)·조밀 stale-drop은 유지.
- 회귀테스트 추가(cooldown 후 in-chain decode, 재발동 0) → 프레임게이트 12종 PASS.
- 빌드 0.2.93→0.2.94, dist/GNLinkSetup-0.2.94.exe. (churn 수정은 뷰어 코드 → 회사 뷰어도 0.2.94 설치 필요.)

### 384) 2026-09-04 A2A 완료 구현 리뷰 금지 정책
- 목표: 다른 A2A 세션의 설계·구현계획 상담은 계속하되, 완료된 구현·diff·커밋의 리뷰 요청은 사용자 지시에 따라 거절하도록 작업 경계를 고정.
- 변경 파일: `AGENTS.md`, `docs/history.md`, `docs/history/history_2026-W36.md`, `docs/구현계획.md`.
- 검증: 정책 문구가 계획 상담 허용, 완료 구현 리뷰 거절, 현재 대화의 사용자 명시 요청만 1회 예외로 구분되는지 diff 검토.
- 다음 액션: 이후 A2A 완료 구현 리뷰 요청에는 저장소를 열지 않고 사용자 지시로 검토하지 않는다고 회신.

### 384) 2026-09-04 영상 NACK 선택적 재전송(근본) — 정적화면 손실 취약성 해결 → 0.2.95
- 배경: 정적화면 끊김의 근본이 IPPP 스트림의 손실 취약성. 정적화면은 프레임이 드물어 패킷 1개만 유실돼도 참조 붕괴→풀 IDR(285KB) 필요→그것도 유실→churn. 검증용Codex 리뷰(계획): dropPm은 패킷손실이 아니라 frame-assembly-discontinuity율(실제 손실 1~2%로도 IDR 12~50% 실패). 근본해법 1순위=NACK(RTT 6~9ms라 잃은 청크만 재전송이 압도적으로 쌈). Codex는 계획만 리뷰(구현 리뷰는 토큰 절약).
- 구현(capability-gated, 협상 안 되면 기존과 100% 동일 — 회귀 0):
  - 프로토콜: `UdpPacketKind::VideoNack(308)`, `kUdpFeatureVideoNack(0x10)`, `UdpVideoNackPacket`(streamGeneration+seq+chunkCount+missing[48], static_assert). Hello/HelloAck features로 협상.
  - 호스트: `SenderState`에 최근 AU 캐시(24개, 송신 성공 시 StoreAu). NACK 수신 시 `RetransmitAu`→`send_udp_chunk_indices`(host_net_io, 원본과 동일 청크 지오메트리로 요청 index만 재송신, FEC/pacing 없음). host_startup_control/connect의 HelloAck가 NACK 광고 + 클라 요청 시 nackEnabled. telemetry(nackRequests/RetransmitChunks/Misses).
  - 클라: `UdpH264FrameAssembler::OldestIncomplete`(가장 오래된 미완성 AU의 미싱 data chunk 열거). 수신루프가 recv 타임아웃(정적화면 손실감지 시계)·datagram마다 NACK 송신 — reorder grace 12ms, 라운드 18ms×최대4, 실패 시 기존 IDR 경로 폴백. handshake가 NACK 요청+HelloAck로 hostSupportsNack 확인 후에만 전송(REMOTE60 없음, 항상 요청).
- 효과: 정적화면 패킷 1개 유실→그 청크만 ~18ms 재전송→참조 끊기기 전 복구→IDR churn/멈춤 근본 제거.
- 빌드: Stream/Host/Viewer/Installer 컴파일 통과, shared_core·frame_gate 테스트 PASS. 0.2.94→0.2.95, dist/GNLinkSetup-0.2.95.exe. **뷰어 코드라 회사 뷰어도 0.2.95 설치 필요.**
- 잔여/후속: 실기 로그로 nack 효과 검증(assembly-discontinuity↓, IDR 비율↓, present gap↓), 릴레이 자체 드랍 계측(NAS server.js queue/error), FEC 강화 A/B는 후순위.

### 385) 2026-09-05 NACK 고화질 회귀 수정 — 조기 NACK 점화 + 키프레임 복구 교착 → 0.2.96
- 증상(0.2.95): 정적화면은 NACK으로 해결됐으나 고화질(12M/60fps)만 시작 1~2초 뒤 멈춤→최대 60초 정지. 로그: 11:20:42 완벽(30~60fps)→seq 3261 고정 60초, 그동안 호스트는 108프레임(키프레임 223KB 포함) 계속 전송=클라 교착. dropPm(=조립불연속)50%, ABR high→mid→high(static_recovery 오판)+runtime-config 12M 재강제.
- 검증용Codex 진단(계획 검증만): ① **최초 점화=조기 NACK** — 12ms grace가 대형 프레임/223KB IDR 전송시간보다 짧아, 아직 안 온 tail을 손실로 오판해 NACK→microburst→실제손실→폭주. ② **직접 지속=waitForKeyframe 중 키프레임 NACK 금지** → 거대 키프레임이 손실나면 복구점이 없어 60초 정지. ③ 재전송 byte cap 부재가 증폭.
- 수정(a+b 원자적):
  - **발화조건(점화 차단)**: assembler `OldestIncomplete`가 highWater(최고 수신 index)와 keyFrame 반환. 클라는 **highWater 미만의 확정 hole만 짧은 grace(25ms) 후 NACK**, tail(≥highWater)은 **긴 grace(120ms=프레임 완전전송) 후에만**. round 25ms×최대3. datagram마다 같은 set 스팸 안 함(round 간격).
  - **키프레임 복구 허용(교착 해소)**: waitForKeyframe 중엔 non-key는 NACK 중단하되 **incomplete keyframe은 NACK 허용**(유일 복구점).
  - **호스트 byte 예산(폭주 차단)**: RetransmitAu에 토큰버킷(라이브 비트레이트 ~15%, ~0.5s 버스트) — 초과 시 재전송 스킵(nackSuppressed), 클라 IDR 폴백. 캐시 조회 후 실제 mtu로 예산 산정.
- 남은(c, 후속): ABR hold-down — runtime-config 12M을 ceiling로(loss 중 floor 강제 금지), high→mid 후 15~30s+저손실일 때만 승격, static_recovery는 outstanding loss 중 승격 금지. (a+b가 이미 graceful degradation 제공.)
- 빌드/버전: 0.2.95→0.2.96. Codex 합격게이트(첫 NACK seenEnd=1·premature 0, retransmit/original<10~20%, 키프레임 손실주입 복구, burst시 폭주 대신 ABR down 수렴)로 실기 검증 예정.

### 386) 2026-09-05 UAC 뒤 "느려짐" 근본수정 — kick 타임스탬프 역전 + synthetic 앵커 → 0.2.97
- 증상: UAC(보안데스크톱) 다녀온 뒤 뷰어가 2~4fps 로 느려짐(0.2.94 쿨다운 후에도). NAS 로그(host 8ec6ecb1 / viewer 68f79d01, 09-05 12:21~12:33): 12:21:12 `desktop-backend-restored from=wgc to=dxgi` 직후부터 host `capture readback slow oldestPendingUs=200~500ms` 매초(분당 44~60), viewer `stale-reference recovery` 매 1.3s(staleBehindLatestUs 160~500ms) → reset_decoder + IDR 200~300KB 매초, 그 사이 P 프레임은 waitForKey 로 폐기(`waiting keyframe drops=121`). 정상 11:15 구간은 같은 희소/kick 인데 readback 2ms 라 무증상 → "UAC" 는 restart 로 readback 지연 상태에 자주 들어가는 트리거일 뿐.
- 메커니즘(코드+수치 확정): `host_stage_pop_frame.cpp:192` kick/refresh 는 `captureUs = nowUs` 로 MFT 에 들어가고 `mf_h264_codec.cpp` 입력 ts FIFO 가 그대로 AU 에 붙임. readback 이 100~300ms 느리면(12:22:37 stats captureUnmapWaitAvgUs=66035 max=296591, oldestGpuPendingPeakUs=551454 — 정상 2166/4256) 그보다 먼저 캡처된 실제 프레임이 kick **뒤에** 인코더로 들어가 와이어 captureQpcUs 가 역전. 실측: 12:22:22.855 kick(ageUs=370875) → wire 6329 user-feedback captureToAuSkewUs=265999 ↔ viewer `stale-reference recovery seq=6329 staleBehindLatestUs=259432`. 뷰어 `viewer_frame_gate.cpp:122` 는 synthetic stamp 로도 latestCaptureSeenUs 를 올려 다음 실제 프레임을 stale 로 판정(→ IDR 폭주).
- 검증용Codex 설계 검토(21항목, 파일 `.claude/a2a_codex_reply.md`; 완료구현 리뷰 아님): ①clamp 는 pop_frame 이 아니라 `gate_static.cpp:150`(captureStampUs) 에 ②워터마크는 인코더 hand-off 시점에 실제/합성 모두 갱신(연속 지연 real 2개가 +1,+2) ③**선행 결함**: `h264_au.cpp:242` 와이어 synthetic 플래그가 AU 가 아닌 현재 호출의 servedBootstrap → 비동기 MFT 에서 kick 호출이 실제 held AU 를, 다음 real 호출이 kick AU 를 내므로 플래그가 엉뚱한 프레임에 붙음 ④뷰어도 decoded 프레임 provenance 로 앵커 판단 ⑤회귀 목록.
- 수정(호스트, 근본): `KickState`(host_kick.hpp) `lastEncoderStampUs` 워터마크 + 순수함수 `ClampRealStamp` — `gate_static.cpp` 에서 실제 프레임 stamp 를 `max(raw, last+1)` 로(원본 captureUs/callbackUs 는 텔레메트리용 보존), `h264.cpp` encodeInputUs 확정 직후 `NoteEncoderStamp`(hand-off 기준). 텔레메트리: 1Hz 로그 `capture-stamp clamped behind-synthetic behindUs= count= maxUs= readbackWaitUs=`, 30s stats `stampClampCount/stampClampMaxUs`, user-feedback `stampClampUs=`.
- 수정(코덱 provenance): `H264AccessUnit.synthetic` / `DecodedFrameNv12.synthetic`, 인코더·디코더 `set_next_input_synthetic()` + `pendingInputSynthetic_` 를 입력 ts FIFO 와 lockstep(push/pop/clear 전부) → 와이어 플래그는 `au.synthetic`, 뷰어는 `decoded.synthetic` 으로 SharedFrame/paced playout/F-10 통계 판단.
- 수정(뷰어, 방어): `viewer_frame_gate.cpp` synthetic 은 latestCaptureSeenUs 를 못 올림; `SharedFrame.synthetic` 추가 → `viewer_present.cpp` synthetic 이면 lastPresentedCaptureUs(anti-rewind 앵커) 미갱신. 표시/디코드/IDR 수락은 그대로.
- 회귀: host_kick_test `TestRealStampClampedBehindSyntheticStamp`(late real 2개 +1/+2, 동일 stamp +1, 워터마크 역행 금지) / viewer_frame_gate_test `[T1b]`(synthetic 뒤 older real in-chain → Decode·recovery 0, synthetic IDR 뒤 older real P → Decode). 두 테스트 + host_frame_gate/shared_core/playout_clock/capture_readback/sender_queue PASS. 전체 빌드(remote60_installer) 성공 → `dist/GNLinkSetup-0.2.97.exe`. **호스트+뷰어 모두 갱신 필요**(회사 뷰어 0.2.97).
- 미확정/후속: readback 지연 자체의 원인(captureToQueue 531ms 중 GPU 대기 105ms 외 ~425ms — Map/Unmap/slotMu 대기·Submit 준비 구간 미계측; 호스트 PC 의 LDPlayer(Ld9BoxHeadless)·dwm GPU 경합 의심)은 이번 수정 범위 밖 — 이제 `stampClamp*`/`readbackWaitUs` 로 발생 빈도가 보임. Codex 지적 잔여: note_packet 이 synthetic 도 lastPacketRecvUs 갱신 → 정적→활동 복귀 시 presentAnchorFloor 재설정 누락 가능(별도 항목).

### 387) 2026-09-05 UAC 뒤 잔여 지연 — DXGI acquire 락 점유 완화 + 디바이스 보호 플래그 계측 → 0.2.98
- 0.2.97 실기(15:44~15:48, host 8ec6ecb1 / viewer 68f79d01): stale-reference recovery 0회(오전 분당 40), decoded==recv, keyframe-request 분당 7~14(오전 36~49) → 핵심 폭주는 해결. 사용자: "많이 나아짐, UAC 뒤 살짝 이상·백스페이스 느림".
- 잔여 원인: readback 지연 자체. 같은 세션에서 UAC 전 captureUnmapWaitAvgUs 2.5~4ms → UAC 복귀(15:46:30 wgc→dxgi) 직후 71~250ms(peak 528~708ms), 뷰어 avgLatency 수십us→150~550ms. `capture-stamp clamped` 분당 42회(behind 100~480ms).
- **새 규칙(3 프로세스 일치)**: 느린 readback 은 프로세스가 WGC 를 처음 쓴 순간부터 시작해 수명 내내 지속. 09-04 16:02 시작→16:28 첫 WGC→16:28부터 slow 연속; 09-05 11:10 시작→11:15 정상→12:21 WGC→slow; 15:44 시작→15:45 60fps 정상→15:46:27 WGC→slow. WGC 이전에 slow 인 프로세스 없음. 정적 화면(dxgi-acquire timeouts 8~9/s)에서 심함.
- 가설: WGC 프레임풀/MF 가 공유 D3D 디바이스의 멀티스레드 보호를 켜고(코드엔 Set 호출 0건) 그 뒤 IDXGIOutputDuplication::AcquireNextFrame(100ms) 이 내부 불공정 락을 쥔 채 대기 → readback worker(GetData/Map/Unmap)·MF 가 굶음. 검증용Codex 검토(파일 `.claude/a2a_codex_reply.md` 0.2.98 절, 20항목): 계측 동의 / **보호 OFF 복원은 반대**(contextMu 는 자체 스레드만 직렬화, MF·WGC 내부 스레드 통제 불가 → 경쟁·hang 위험) / 독립 근거 = Sunshine display_base.cpp 가 같은 락 굶김을 문서화하고 timeout 뒤 sleep 으로 대응 / 짧은 timeout 만으론 불공정 락 재획득 가능 → **timeout 뒤 락 밖 실제 대기**가 핵심 / 원인 확정은 실기 A/B 로.
- 수정(호스트, 완화): `libs/capture` DxgiDesktopCaptureConfig.acquireIdleSleepUs 신설 — DXGI_ERROR_WAIT_TIMEOUT 에서만 프레임 미보유·락 밖에서 sleep 후 재진입(성공 경로·ReleaseFrame 불변). 호스트 기본 acquireTimeoutMs 100→**8**, idleSleep **2000us**; env `REMOTE60_NATIVE_DXGI_ACQUIRE_TIMEOUT_MS`(1~1000) / `REMOTE60_NATIVE_DXGI_ACQUIRE_IDLE_SLEEP_US`(0~100000) 로 A/B(100/0 legacy, 8/0, 8/2000). `desktop_backend=dxgi capture-started=1` 로그에 값 표기.
- 계측(호스트, 읽기만): `d3d_multithread_state()`(context→ID3D11Multithread QI, 실패=unknown, Set 없음) + `d3d-mt at=<device-created|mf-device-set|winrt-wrapper|wgc-pool-before|wgc-started|wgc-closed|device-recreated> state= dev= luid=` 로그, 30s stats `d3dMt=`. readback worker 귀속: CaptureFrameMeta.workerCtxWaitUs(자체 contextMu 대기) / workerD3dCallUs(GetData+Map+Unmap 호출 시간) → user-feedback `workerCtxWaitUs= workerD3dCallUs=`, 30s stats `workerCtxWait/ D3dCall Avg/Max`. D3D 호출 시간이 크고 mutex 대기가 작으면 런타임 내부 락(AcquireNextFrame) 확정.
- 검증: capture_readback/host_kick/host_frame_gate/dxgi_output_selection PASS, 전체 빌드 OK → `dist/GNLinkSetup-0.2.98.exe`(호스트만 바뀜; 뷰어 0.2.97 그대로 호환). **실기 판정 기준(Codex 15)**: UAC 전후 captureUnmapWait/oldestGpuPendingPeak·stampClamp 횟수·입력→화면 반응이 정상 범위로 복귀, 60fps 모션·MF 실패·decoder reset 악화 없음. 확정 관측: 같은 device 에서 WGC 전 off → 후 on → close 뒤 on 과 지연 시점 일치.
- 실패 시 대안(Codex 18): 복제 전용 device/context 분리 + shareable texture 명시 동기화(범위 큼, 0.2.98 미포함).

### 388) 2026-09-05 A2A 완료 구현 리뷰 제한 제거 및 0.2.98 검증
- 목표: 사용자의 직접 지시에 따라 A2A 완료 구현 리뷰 금지 정책을 제거하고, 커밋 `a769ab4a778673a79da9c8df98b563da34aa4d0e`의 0.2.98 구현을 검증한다.
- 변경 파일: `AGENTS.md`의 A2A Review Boundary 삭제, `docs/구현계획.md`의 해당 정책 상태만 취소로 변경, `docs/history/history_2026-W36.md`에 철회 상태 표시, `docs/history.md`에 결과 기록. 제품 소스는 수정하지 않았다.
- 검증: Release 호스트 및 관련 테스트 6종 빌드 성공. capture_readback / host_kick / host_frame_gate / dxgi_output_selection / viewer_frame_gate / native_video_client_shared_core 모두 exit 0.
- 발견 사항(P2): `d3d_capture_readback.cpp:676-678`은 GpuPending이 없는 유휴 sweep의 contextMu 대기까지 누적하고, `pick == SIZE_MAX`에서 리셋 없이 다음 프레임으로 이월한다. 별도 WARP 장치로 프레임 생성 전에 250ms의 유휴 락 경합을 만든 뒤 100ms 후 프레임을 Submit한 결과, 실제 capture→publish 2,951us에 workerCtxWaitUs=248,684us가 붙었다. per-frame submit→publish 귀속값으로 해석하면 원인을 오판한다. 재현 소스/로그는 로컬 `.claude/a769ab4-probe/`에 두며 커밋에서 제외한다.
- 추가 확인: DXGI timeout 뒤 sleep은 프레임 미보유·앱 락 밖이며 성공/ReleaseFrame 경로는 불변. sleep_for는 취소 가능한 대기가 아니므로 stop 2ms 상한은 보장하지 않는다(기본 acquire 8ms 및 스케줄링 지연도 존재). meta는 slotCopy.meta의 const 참조여서 publish 직전 같은 객체의 필드 갱신을 안전하게 읽는다. handOff=false 리셋은 실패 시도 계측을 버리며 전역 작업량 집계가 아니다. 보호 상태 QI 실패=unknown, Windows의 %p/%ld/%lu 인수 타입 일치, Set 호출 없음. 프로토콜/뷰어 소스 변경이 없어 0.2.97과 와이어 형식 차이 없음.
- 한계: 현재 계측만으로 내부 런타임 락과 다른 D3D/드라이버 대기를 단정할 수 없다. WGC 생성/StartCapture 첫 callback 및 실제 MFT 초기화 사이 세부 경계는 추가 계측이 필요하다. 실제 UAC 왕복과 100/0·8/0·8/2000 A/B는 실행하지 않았으며, 필드 성능 해결 판정은 유보한다.
- 다음 액션: 유휴·세대·슬롯별 worker 계측의 집계 범위를 고쳐 위 WARP 재현을 회귀로 고정한 후, UAC 전후 실제 capture→publish와 입력 반응 및 CPU를 비교한다. A2A 완료 구현 검증 요청은 더 이상 정책상 거절하지 않는다.

### 389) 2026-09-05 0.2.98 구현 검증 반영 — readback worker 계측 귀속 오류 수정 → 0.2.99
- 검증용Codex 완료구현 검증(#388, 사용자가 Codex 탭에서 직접 요청) 결과: [P2] `d3d_capture_readback.cpp` WorkerLoop 가 GpuPending 이 없는 유휴 상태에서도 1ms 마다 contextMu 를 잡고 그 대기를 누적, pick==SIZE_MAX 에서 리셋 없이 이월 → 프레임 이전의 경합이 다음 프레임에 귀속. WARP 재현(`.claude/a769ab4-probe/`): 프레임 없을 때 contextMu 250ms 보유 후 2x2 Submit → workerCtxWaitUs=248684(실제 captureToPublish 2951us). 그 외: sleep 은 프레임 미보유·앱 락 밖·성공경로 불변(6), stop 중 sleep 취소 불가는 2ms 라 배포 차단 아님(7), snprintf 포맷 OK·LUID 실패 0:0 표기 개선 여지(8), mf-device-set 은 ResetDevice 직후(MFT init 아님)(9), 와이어/뷰어 변경 없음(10), 6종 테스트 PASS(11), "workerD3dCall 크면 Acquire 락 확정" 주석은 과장(12).
- 수정: (a) 유휴(GpuPending 0)면 컨텍스트 락 sweep 자체를 건너뛰고 누적값 초기화 — 유휴 경합이 다음 프레임에 새지 않고, 빈 링 sweep 의 불필요한 락 경합도 제거. (b) Map 실패 시간도 D3D 호출 시간에 포함. (c) 귀속 창 정의 주석(마지막 publish 이후·GpuPending 동안·배치 단위·실패 hand-off 미보고) + "확정이 아니라 consistent-with" 로 완화. (d) LUID 미상은 `luid=?`. 버전 0.2.98→0.2.99(0.2.98 인스톨러는 미설치 상태라 dist 에서 제거).
- 검증: Codex 프로브를 수정 코드로 재빌드·재실행 → `workerCtxWaitUs=0 workerD3dCallUs=52 captureToPublishUs=2009` **NOT REPRODUCED**. capture_readback/host_kick PASS, 전체 빌드 OK → `dist/GNLinkSetup-0.2.99.exe`(호스트만; 뷰어 0.2.97 호환).
- 후속: 실기 A/B(100/0·8/0·8/2000) 와 `d3d-mt` 상태 전이 확인은 그대로 남음. 유휴 프로브를 정식 회귀(WARP 통합 테스트)로 승격하는 것은 미착수.

### 390) 2026-09-06 정적 화면 지연·고화질 영상 장기 정지 원인 분석
- 목표: 사용자가 반복 보고한 PC 뷰어 고화질 영상 정지/재시작 후 회복과 정적 화면·UAC 뒤 지연을, 실제 실행 경로 및 NAS 로그와 재현 실험으로 구분한다.
- 변경 파일: `docs/stream_freeze_diagnosis_2026-09-06.md`(분석 보고서), `docs/history.md`, `docs/구현계획.md`(P2/P5 진단 상태 및 착수 근거 확인 상태만 갱신). 제품 코드 수정 없음. 원본 로그·분석 스크립트·별도 C++ 프로브는 `.claude/freeze-diagnosis-20260905/`에 보관하고 커밋에서 제외한다.
- 확인 1: Windows `GNLinkViewer`는 `viewer_startup.cpp`에서 requestNack=false 기본값을 사용하며, 실제 `VideoReceiver::run_udp()`에는 영상 NACK 구동이 없다. 구현은 별도 `ClientSessionController` 경로에만 존재하고 shared_core_test도 그 경로를 링크한다. 기존 테스트 성공을 Windows 제품의 재전송 적용으로 해석할 수 없다.
- 확인 2: 실제 FrameGate를 링크한 결정론적 프로브에서 Congested 진입 후 60초간 P프레임 3,600개 모두 폐기, 최초 IDR 요청 이후 재요청 0회. early return이 뒤쪽 waitForKey 재요청과 복구 deadline을 우회한다. 합성 프레임 뒤 6ms 실제 프레임 묶음도 정적 시간을 포함한 2,006,000us backlog로 오판하여 reason 1 복구가 발동했다.
- 확인 3: 실제 assembler 프로브에서 무손실 IDR seq=1→3이 Completed+key+droppedPreviousIncomplete를 반환하여 Windows 수신부의 추가 IDR 요청 경로를 탄다. 09-05 15:44~15:53 저장 세션의 실제 reason=2 요청 90/90회가 완성된 키프레임+gap 직후와 일치했다(throttled 1건은 제외).
- 호스트 측 근거: 동일 세션의 WGC→DXGI 복귀 후 readback pending 관찰 평균 4.193ms→71.581~500.052ms, peak 약1.017s. 캡처→큐 약1s 샘플도 존재. runtime 락·GPU·드라이버 대기의 최종 구분은 당시 로그로 불가하며 0.2.99의 실기 성공을 주장하지 않는다.
- 장기 정지 사건: 11:20:42.749 seq=3261 이후 viewer 영상 진행 로그 중단, 약7초 후 제어도 peer-lost. host는 이 사이 키프레임을 포함해 계속 송신했다. 해당 사건의 마지막 congestionState는 normal이므로 위 Congested 결함만으로 동일 원인이라고 단정하지 않는다. 수신 스레드 정체와 UDP 전달 중단의 구분에는 별도 진행 heartbeat/패킷 근거가 필요하다.
- 검증: 기존 viewer_frame_gate_test 및 native_video_client_shared_core_test PASS. 별도 viewer_gate_probe 2/2, assembly_gap_probe 1/1 문제 경로 재현(exit 0). 소프트웨어 상태 머신/조립기 검증이며 실기 네트워크·UAC 재현은 수행하지 않았다. 09-06 NAS 확인에서 해당 viewer의 최신 기록은 여전히 09-05 15:53 종료였다.
- 다음 액션: Windows NACK 연결·완성 IDR 중복 요청 제거·프레임 도착과 독립된 복구 타이머를 먼저 구현/검증하고, synthetic idle 앵커와 호스트 readback 대기 A/B를 분리한다. 실제 Windows 바이너리 경로의 손실/복구 통합 테스트로 완료 판정한다.

### 391) 2026-09-07 Windows 복구 경로 수정 위임안 및 사전 크로스체크 조건 문서화
- 목표: 사용자가 요청한 원인 분석 문서 업데이트와 remote Claude 작업 위임을 준비하고, 구현 전에 독립 크로스체크를 수행하도록 순서와 완료 조건을 구체화한다.
- 변경 파일: `docs/windows_viewer_recovery_work_order.md`(작업 지시), `docs/stream_freeze_diagnosis_2026-09-06.md`(후속 작업 연결), `docs/history.md`, `docs/구현계획.md`(교차 확인 대기 상태만 반영).
- 작업 순서: 실제 Windows NACK 경로, Congested 재요청, 완성 IDR 중복 요청, synthetic idle 앵커를 먼저 코드·프로브로 확인/반증/조건부 분류한 후 확인된 항목부터 수정. 제품 경로 손실 주입·복구 타이머·무수신·구버전 peer 검증을 완료 조건에 포함하고 호스트 UAC readback 원인 미확정은 별도 실기로 남긴다.
- 검증: 기존 분석 커밋 `5e32126` 및 현 제품 기준 `bae7d99` 확인. A2A 후보 조회에서 remote Claude 세션 2개(`0gk0hr8u` onCall=true, `rda5l808` onCall=false)를 확인했고, 사용자가 온콜이 켜진 `remote#0gk0hr8u`를 선택했다. 제품 파일 수정·새 빌드·실기 실행은 없음.
- 다음 액션: 확인된 Claude 세션에 문서 기반 A2A task를 pin하여 전달하고, 구현 전 교차 확인 결과와 작업 진행을 추적한다.

### 392) 2026-09-07 Windows 뷰어 NACK 연결 + 조립 in-order hold + 실제 수신 경로 통합테스트 (복구 작업 1단계)
- 목표: 작업 지시(`docs/windows_viewer_recovery_work_order.md`, A2A task t-iyzfi6ub) 1단계 교차 확인 후, **확인**으로 분류된 "Windows NACK 미연결"(#390 항목 1)을 실제 GNLinkViewer 수신 경로에 연결하고, NACK 유예와 충돌하던 assembler 즉시 폐기 정책을 in-order hold 로 바꾼다.
- 교차 확인 결과(구현 전, `.claude/windows_recovery_crosscheck.md`, Codex 세션에 회신): 1 NACK 미연결=**확인**, 2 Congested 재요청 누락=**확인**(11:20 사건을 이 결함으로 설명할 근거는 없음 — 원인 미확정), 3 완성 IDR 중복 요청=**확인**, 4 synthetic idle 가짜 혼잡=코드 경로 확인/현장 기여 **조건부**(kick 150ms 후행 + 묶음 도착; 저장 로그 7건 서명 일치, 사건별 kick trace 없음), 5 11:20 장기 정지=**미확정 유지**. 프로브 2종 재실행 모두 재현(exit 0). 추가 발견: (A) shared_core_test 에 NACK 참조 0건 — Android 경로도 자동 테스트 없음, (B) LAN 직결 미디어 소켓 recv timeout 0(blocking) → 무수신 타이머 불가, (C) assembler 가 최신 AU 완성 시 이전 미완성 즉시 폐기(`shared_core.cpp` 구 861-865행) → 60fps 에선 다음 P(~16ms)가 NACK grace(25ms) 전에 손실 AU 를 버려 영상 콘텐츠 P 손실 복구 불가, (D) 11:20:43~59 구간 recv 스레드의 1초 `udp-assembly`/`recvFrames=` 라인 0건 — recv 스레드 정지 vs UDP 전달 중단을 저장 로그로 구분 불가(터널 Tick/OnPacket 이 recv 스레드에 있어 어느 쪽이든 peer-lost).
- 변경 파일:
  - `apps/native_poc/src/udp_video_nack.hpp`(신규, header-only): `VideoNackScheduler` — `native_video_client_session.cpp` 의 maybe_send_nack 람다를 공용 클래스로 추출(hole 25ms/tail 120ms grace, round 25ms×3, keyframe-wait 중 key 만 복구). 유예 기준을 스케줄러가 처음 본 시각이 아니라 AU 첫 datagram 시각(`IncompleteAuInfo.firstPacketUs`)으로.
  - `native_video_client_shared_core.{hpp,cpp}`: assembler `ConfigureInOrderHold(maxHoldUs, maxConcurrent)` / `PushDatagram(data,len,nowUs)` / `PopDelivery(nowUs, repairNonKey)` / `PendingCount()`; hold 켜면 완성 AU 는 `Queued` 로 보류되고 seq 순서로 배달, 앞선 미완성 AU 는 hold(기본 120ms) 동안 재전송을 기다린 뒤 포기(gap 은 배달 시 `droppedPreviousIncomplete`). keyframe-wait 중 non-key 미완성 head 는 즉시 해제, key head 는 유지(유일 복구점). 동시 조립 상한 hold 모드 8. hold 끄면(Android/기존 호출) 즉시 배달 동작 불변(`DeliverAssembly` 공용화).
  - `native_video_client_session.cpp`: 람다 → `VideoNackScheduler`(동작 동일, Android 경로 hold 미사용).
  - `viewer_session_state.hpp`(`udpHelloAckFeatures`, `hostSupportsNack`), `viewer_context.hpp`(`videoNackEnabled`, `videoNackHoldUs`, `udpRecvTimeoutMs`), `viewer_startup.cpp`: Hello `requestNack`(env `REMOTE60_NATIVE_VIDEO_NACK` 기본 1) + HelloAck features 저장 + **UDP 전 경로 recv timeout 25ms**(`REMOTE60_NATIVE_UDP_RECV_TIMEOUT_MS`; 직결은 blocking, 터널만 200ms 였음) + 로그 `udp hello ack features= nackRequested= nackNegotiated= nackHoldUs= recvTimeoutMs=`; hold 는 `REMOTE60_NATIVE_VIDEO_NACK_HOLD_US`(기본 120000, 0=끔).
  - `viewer_video_receiver.{hpp,cpp}`, `viewer_recv_stats.hpp`: `VideoReceiver::NackOptions{enabled, holdUs}`; run_udp 가 datagram 처리 뒤와 recv timeout 마다 `PopDelivery` 배달과 NACK `Poll`→send 를 구동. 배달 로직을 `deliver_completed`/`note_sequence_gap`/`drain_deliveries` 람다로 정리(레거시 동작 동일). stats 라인 `nackOn= nackSent= nackChunks= nackExhausted= pending=`, telemetry `pending=`, 첫 5회 `video-nack seq= missing= of= round=` 로그.
  - 테스트: `native_video_client_shared_core_test.cpp` 에 `test_video_nack_scheduler`(grace/round/tail/keyframe-wait/blocker 교체)·`test_udp_assembler_in_order_hold`(hold·만료·keyframe-wait·seq 순서·cap·legacy 불변) 추가. **신규 `remote60_viewer_udp_recovery_test`**(`viewer_udp_recovery_test.cpp`, CMake): 실제 `VideoReceiver::run_udp`→assembler→FrameGate→MF `H264Decoder` 를 실제 UDP 소켓에서 실행하고, 가짜 호스트(실제 `H264Encoder` 인코딩, 호스트 청크 지오메트리, 손실/재정렬/pacing/NACK 무응답/구버전 peer 계획, 키프레임 요청은 `ctx.control.keyframeRequests` 에서 제어 스레드처럼 소비)로 S1 P 청크 손실→NACK 복구·IDR 요청 0 / S2 구버전 host→NACK 0·IDR 폴백 복구 / S3 복구 IDR 청크 손실→keyframe-wait 중 NACK 복구·요청 ≤2 / S4 큰 IDR 50ms pacing→tail 조기 NACK 0 / S5 청크 역순→NACK 0 / S6 NACK 무응답→3라운드 뒤 IDR 폴백·폭주 없음. UI 진입점 2개(`request_video_paint`, `post_pc_selection_reveal`)만 stub, 창·캡처 무관(RDP 상태와 독립).
- 검증: GNLinkViewer / udp_control_e2e_test / shared_core_test / viewer_udp_recovery_test 빌드 OK. `remote60_native_video_client_shared_core_test` PASS, `remote60_viewer_udp_recovery_test` **6/6 PASS**(하드웨어 MFT; `qwinsta` rdp-tcp#0 Active 상태였으나 캡처 무관이라 판정 유효). 실기(GNLinkStream+GNLinkViewer e2e)는 RDP 접속 중이라 미실행 — CLAUDE.md 규칙.
- 다음 액션: 2단계 시간 기준 복구 재요청(Congested/keywait/무수신) + 완성 IDR 중복 요청 제거, 3단계 실제 콘텐츠 시계 분리, 4단계 recv liveness/watchdog, 버전·설치본·실기.

### 393) 2026-09-07 시간 기준 키프레임 복구 타이머 + 완성 IDR 중복 요청 제거 (복구 작업 2단계)
- 목표: 교차 확인에서 **확인**된 #390 항목 2(Congested 재요청 누락·무수신 시 재시도 없음)와 항목 3(완성 IDR 앞 seq gap 에 추가 IDR 요청)을 실제 Windows 수신 경로에서 고친다.
- 변경 파일:
  - `viewer_frame_gate_state.hpp`/`viewer_constants.hpp`/`viewer_frame_gate.{hpp,cpp}`: `FrameGate::tick(nowUs)` — `waitForKeyFrame || Congested` 동안 프레임 도착과 무관하게 시계로 재요청(reason 7 `recovery_timer`): 대기 시작 후 500ms, 이후 ×2 backoff 상한 2s(`REMOTE60_NATIVE_KEY_RECOVERY_RETRY_US`/`_MAX_US`, 0=끔), IDR 디코드로 대기가 끝나면 초기화. 텔레메트리 `keyRetries= keyRetryEpisodes= keyWaitMaxUs=`(stats 라인) + `keyframe recovery retry count= waitedUs= state=` 로그. 기존 KeyframeRequestState 리미터(120ms·3토큰)가 와이어 상한을 계속 보장하므로 P 프레임마다 요청하지 않는다.
  - `viewer_video_receiver.cpp`: run_udp 가 datagram 처리 뒤·recv timeout 마다 `fg.tick()` 호출(25ms recv timeout 이 무수신 시계). `note_sequence_gap`: 배달된 AU 가 **완성 IDR** 이면 decoder reset·keyframe 요청 없이 그 IDR 로 재동기(`keyResync=` 통계, waitForKeyFrame 만 세워 디코드 실패 시 reason 4 경로가 처리). 호스트가 EnqueueKey/HoldForKey 로 seq 를 소비하고 안 보내는 것은 손실이 아니다(09-05 로그 90/90).
  - `viewer_startup.cpp`: env 두 개 + limiter 로그에 `keyRecoveryRetryUs=` 표기.
  - 테스트: `viewer_frame_gate_test` 에 `test_recovery_timer_retries_while_congested_without_idr`(프로브 시나리오 반전: Congested + P 3,600장/60s → 재요청 31회 전후, 프레임당 요청 0, IDR 뒤 정지) / `test_recovery_timer_without_frames_and_backoff_per_wait`(무프레임 4s → 0.5/1.5/3.5s 3회, IDR 뒤 0회, 새 대기는 0.5s 부터, interval 0 이면 끔). `viewer_udp_recovery_test` 에 S7 seq gap 뒤 완성 IDR ×3 → 키프레임 요청 0 / S8 P 프레임 전체 손실 + 호스트 요청 무시 + source 정지 2.6s → 타이머 재요청 2~4회 후 복귀 / S9 present 앵커 고정으로 Congested 진입 + 첫 복구 IDR 전체 손실 → 시계 재요청으로 Recovering→Normal 복귀, 폭주 없음.
- 검증: 아래 결과 참조(빌드·테스트 로그).
- 다음 액션: 3단계 실제 콘텐츠 시계 분리, 4단계 recv liveness/watchdog.

### 394) 2026-09-07 정적→활동 전환의 가짜 혼잡 제거 — 실제 콘텐츠 시계 분리 + keyframe-wait 폐기 프레임 트리거 제외 (복구 작업 3단계)
- 목표: 교차 확인에서 코드 경로 확인/현장 조건부로 분류된 #390 항목 4. synthetic(kick/refresh) 프레임이 수신 간격 시계를 갱신해 정적 구간 뒤 첫 실제 프레임 묶음이 "촘촘한 연속"으로 읽히고(recvGap<250ms → idle 재앵커 없음) 그 capture 간격(346~572ms)이 decode 적체로 계산되어 Congested+IDR 이 나던 경로를 막는다.
- 변경 파일:
  - `viewer_frame_gate_state.hpp`/`viewer_frame_gate.{hpp,cpp}`: `note_packet(nowUs, synthetic)` — heartbeat 시계(`lastPacketRecvUs`, 모든 완성 프레임)와 실제 콘텐츠 시계(`lastRealPacketRecvUs`) 분리. 실제 프레임의 recvGap 은 직전 **실제** 프레임 기준(idle 재앵커·dense 판정에 사용), synthetic 프레임은 heartbeat 기준(어차피 혼잡 트리거 제외). 실제 프레임끼리 촘촘하고 앵커가 안 움직이는 진짜 적체는 판정식이 그대로라 약화되지 않음.
  - 같은 파일: keyframe-wait 중 폐기될 non-key 프레임은 혼잡 트리거(lagTriggerStreak)에서 제외 — 대기 중엔 present 가 없어 lag 추정이 대기 시간만 재고, 그 streak 이 이미 IDR 을 요청 중인 대기 위에 Congested 진입(reset+reason 1)을 얹던 기존 동작(통합테스트 S8 로그에서 매 재개마다 관측)을 제거.
  - `viewer_video_receiver_frame.cpp`: note_packet 에 wire synthetic 플래그 전달.
  - 테스트: `viewer_frame_gate_test` 에 `test_synthetic_gap_then_real_burst_no_false_congestion`(프로브 시나리오 기대값 반전: 100ms 합성 2초 뒤 2ms 간격 실제 3장 → Normal·요청 0·reset 0; 같은 합성 구간 뒤 앵커 고정 실제 dense 25장 → Congested 진입 유지) / `test_keyframe_wait_drops_do_not_enter_congested`(대기 중 P 60장 폐기 → Normal, IDR 뒤 진짜 적체는 Congested). `viewer_udp_recovery_test` 에 S10 synthetic 150ms×12 뒤 실제 3장 burst(앵커 고정) → 혼잡 전이 0·요청 0.
- 검증: `remote60_viewer_frame_gate_test` PASS(신규 2건 포함, 총 16), `remote60_viewer_udp_recovery_test` **10/10 PASS**(S10 포함), GNLinkViewer 빌드 OK. 실기 미실행(RDP).
- 다음 액션: 4단계 recv liveness heartbeat + UI watchdog + dead-session 검출/종료.

### 395) 2026-09-07 recv 스레드 liveness heartbeat + UI watchdog + dead-session 검출/종료 (복구 작업 4단계)
- 목표: #390 항목 5. 09-05 11:20 장기 정지(마지막 present 11:20:42.749 → 11:20:49.566 제어 peer-lost, 호스트는 60fps 송신 지속)에서 저장 로그로는 "recv 스레드가 디코드/publish 에서 멈춤"과 "UDP 전달 중단"을 구분할 수 없었다(터널 Tick/OnPacket 이 recv 스레드에 있어 어느 쪽이든 peer-lost). 진행 상황을 프레임 진행과 독립적으로 기록하고, 제어가 영구히 끊긴 채 영상도 멈춘 세션을 검출해 마지막 화면만 조용히 유지하지 않게 한다.
- 변경 파일:
  - `viewer_recv_liveness.hpp`(신규, standalone): `RecvLiveness` — recv 스레드가 지금 어느 단계(starting/recv/control/assembly/decode/publish/exited)에 언제부터 있는지 + 마지막 datagram/video chunk/조립/디코드 반환/publish 시각(relaxed atomics, recv 쓰기·UI 읽기) + `evaluate_session_liveness()` 순수 판정: recv-stalled(한 단계 2s 이상), link-silent(루프는 돌지만 3s 무수신·제어 연결 상태), session-dead(제어가 끊긴 뒤 5s 동안 publish 없음 또는 루프 종료).
  - `viewer_video_receiver.{cpp}`/`viewer_video_receiver_frame.cpp`: run_udp/run_tcp 루프 회전·datagram·video chunk·조립·디코드 진입/반환·publish 지점에 heartbeat 기록.
  - `viewer_session_watchdog.{hpp,cpp}`(신규): UI 타이머(50ms)에서 1초마다 판정. `[liveness] recv-thread stalled stage= stageAgeUs= loops= datagramAgeUs= assembledAgeUs= decodeAgeUs= publishAgeUs= control= tunnelClosed= tunnelReason=` / `link-silent …`(5초마다 반복) / `session-dead action=close|notify …`(1회) 로그, dead 면 panel status `session_lost` + 기본값으로 세션 종료(WM_CLOSE → 셸이 호스트 목록으로 복귀해 한 번의 클릭으로 재접속; `REMOTE60_NATIVE_DEAD_SESSION_EXIT=0` 이면 알림만, `REMOTE60_NATIVE_DEAD_SESSION_MS` 기본 5000). 제어가 살아있는 UAC 정지는 link-silent 까지만, 절대 dead 아님.
  - `viewer_state.hpp`(`recvLive` 멤버), `viewer_session_state.hpp`(`deadSessionUs`, `deadSessionExit`), `viewer_startup.cpp`(env), `viewer_window_proc.cpp`(타이머 hook), CMake(GNLinkViewer 소스 + `remote60_viewer_liveness_test`).
  - 테스트: `viewer_liveness_test`(L1~L10: 정상/디코드 정지/recv 미반환/무수신/peer-lost+무publish=dead/제어 끊겨도 publish 되면 not dead/5s 미만 not dead/루프 종료+제어 끊김=dead/미접속·off 스위치/UAC 정지는 silent 만).
- 한계(정직하게): 11:20 사건의 최초 방아쇠는 여전히 미확정. 이 계측은 다음 재현에서 "stage=decode stageAgeUs=…" 인지 "stage=recv loops 증가·datagramAge 증가" 인지로 둘을 가른다. 자동 종료는 "제어 영구 상실 + 영상 무진행"이 증명된 경우에만 발동하며 재연결 자체는 셸 몫.
- 검증: `remote60_viewer_liveness_test` L1~L10 PASS, `remote60_viewer_udp_recovery_test` **10/10 PASS**(S1 에 heartbeat 배선 assertion 추가: loop/datagram/assembled/decode/publish 스탬프 갱신 + healthy 판정), GNLinkViewer 빌드 OK. 실기 미실행(RDP). 구분: 검출·알림·판정 로직은 검증됨; dead-session 자동 종료(WM_CLOSE)가 실제 D3D/디코더 hang 을 회복시키는지는 **미입증**(5단계에서 shutdown join timeout 으로 hang 시 프로세스 종료를 보강, 그것도 마지막 수단).

### 396) 2026-09-07 Codex 조건 6개 + 불변식 2개 반영 — hold 상한·IDR 즉시 해제, NACK/IDR 타이머 조율(유예 상한), 디코더 provenance 검사, shutdown bounded join, 제품 협상 경로 공용화 (복구 작업 5단계)
- 목표: 교차 확인 회신에 대한 Codex 조건(`.claude/windows_recovery_codex_conditions.md`)과 추가 불변식 2개(IDR 유예 상한, shutdown detach 후 자원 파괴 금지)를 구현·검증에 포함한다. 사용자 추가 승인 불요(위임 범위 내).
- 변경 파일:
  - (조건 1) `native_video_client_shared_core.{hpp,cpp}`: hold 를 시간(120ms)·개수(8)·**바이트(기본 8MB, `ConfigureInOrderHold` 3번째 인자, `HeldBytes()`)** 로 제한. `PopDelivery`: 앞선 미완성 AU 뒤에 **완성 IDR** 이 있으면 그보다 오래된 것을 즉시 포기하고 IDR 을 내보냄(IDR 이 복구점, gap 은 IDR 이 닫음 → 요청 없음). 단위테스트: 완성 IDR 즉시 해제, 바이트 상한 eviction, seq wrap(0xFFFFFFFF→0), 중복 재전송 흡수·payload 무결, 늦은 재전송 Ignored, 무assembly gap. 무수신 timeout 에서도 PopDelivery 진행(1단계부터).
  - (조건 2 + 불변식 1) `udp_video_nack.hpp`: hole 단계·tail 단계 각각 3라운드 예산(큰 프레임 tail 이 아직 도착 중일 때 hole 에 소진된 라운드가 tail 을 굶기지 않음) + `busy()`. `viewer_frame_gate.{hpp,cpp}`/`viewer_constants.hpp`/`viewer_frame_gate_state.hpp`: `tick(nowUs, repairInProgress)` — NACK 진행 중이면 IDR 재요청을 유예하되 **그 재요청이 처음 due 가 된 시점부터 최대 300ms**(`REMOTE60_NATIVE_KEY_RECOVERY_DEFER_MAX_US`, 새 패킷/AU/라운드로 재시작 안 함) — 상한 뒤엔 NACK 과 무관하게 요청(`keyRetryDeferred=` 텔레메트리). gate 테스트: 매 tick busy 여도 800~850ms 에 요청. S6(NACK 무응답 + 후속 AU 지속)에서 유한 시간 내 IDR 경로 확인.
  - (조건 3) 통합테스트 S11: 실제 `H264Encoder`/`H264Decoder` 로 P 하나를 건너뛰고 reset 없이 IDR 을 넣어 출력 timestamp/synthetic provenance 가 각 입력과 일치·단조·손실 프레임 스탬프 미출현·IDR 이전 출력이 IDR 뒤로 새지 않음을 검사. S12: payload 를 0 으로 채운 완성 IDR — **관찰: 이 PC 의 하드웨어 MFT 는 오류·출력 없이 삼키고, reset 을 안 했으므로 뒤따르는 P 는 기존 참조로 디코드됨** → 디코더가 실패를 보고하지 않는 손상 IDR 은 이 계층에서 감지 불가(과거엔 중복 요청의 부수효과로 우연히 복구됐던 경우). 보장되는 것: 대기에 갇히지 않음·폭주 없음·**다음 정상 IDR 을 실제로 수신·디코드하면 회복** — 그 IDR 의 도착 시간 상한은 미보장(호스트 keyint 는 프레임 개수 기준이라 실제 fps/설정/GOP/손실에 따라 달라지고, 정적 화면·source 정지에서는 wall-clock 상한이 없으며 합성 프레임은 scheduledKey 에서도 제외). S12 는 조용히 삼켜진 손상 IDR 뒤의 무폭주와 정상 IDR 복귀만 검사한 것이며, 실제 decoder 오류 반환(reason 4) 경로는 gate 단위테스트(`test_decode_failure_rebuild_threshold`)로만 검증됨(통합 검증 아님); 빈 출력 연속은 reason 5 경로 유지.
  - (조건 4 + 불변식 2) `viewer_thread_join.hpp`(신규) + `viewer_shutdown.cpp`: control/recv 스레드 join 을 3초 bounded wait 로. 미반환이면 `[liveness] <thread> thread did not exit … stage= stageAgeUs= loops= control=` 로그 직후 `TerminateProcess(44)` — **detach 뒤 자원 정리로 내려가지 않음(UAF 없음)**, 셸은 "연결에 실패했습니다 (코드 44)" 로 목록 복귀. 이것은 자동 재연결·정상 복구가 아니라 **제한된 종료**이며, 실제 D3D/디코더 hang 에서의 동작은 실기 미입증. `viewer_liveness_test` J1 이 bounded join 소요 시간(<500ms)을 검사. dead 판정 조건(제어 영구 상실 + 영상 무진행)은 그대로(L10: 제어 생존 시 절대 dead 아님).
  - (조건 5) `viewer_udp_session.hpp`(신규): `viewer_udp_hello_options()`/`viewer_apply_udp_hello_ack()`/`viewer_arm_udp_recv_timeout()` + `kVideoNackEnabledDefault` — `connect_media_socket` 과 통합테스트 rig 가 같은 함수를 호출. 가짜 호스트가 실제 Hello 의 `kUdpFeatureVideoNack` 비트를 관측해 S1/S2 에서 assertion(제품 기본값 회귀 시 실패). 구버전 peer(S2)는 NACK/hold 꺼진 경로 유지.
  - (조건 6) history #392 및 크로스체크 문서의 11:20 문구를 "이 결함으로 설명할 근거 없음 — 원인 미확정" 으로 정정.
- 검증: `remote60_native_video_client_shared_core_test` PASS(hold/NACK 신규 케이스 포함), `remote60_viewer_frame_gate_test` PASS(유예·유예상한 케이스 포함), `remote60_viewer_liveness_test` PASS(L1~L10 + J1 bounded join), `remote60_viewer_udp_recovery_test` **12/12 PASS**(S11 provenance, S12 손상 IDR 무폭주 포함), GNLinkViewer·udp_control_e2e_test 빌드 OK. 실기 미실행(RDP Active).

### 397) 2026-09-07 Windows 뷰어 정지·지연 복구 작업 마감 → 0.2.100 (설치본 생성, 실기 미검증)
- 목표: 복구 작업 1~5단계(#392~#396)를 한 설치본으로 묶는다. 호스트 소스는 이번 작업에서 변경하지 않았다(0.2.99 호스트와 와이어 호환: NACK 은 호스트가 이미 광고·서비스 중이었고 Windows 뷰어만 요청하지 않았던 것).
- 변경 파일: `product_version.hpp` 0.2.99→0.2.100, `docs/history.md`, `docs/구현계획.md`.
- 검증: 전체 Release 빌드 OK(GNLinkStream/GNLinkViewer/GNLinkClient/GNLinkHost/GNLinkSetup 포함, exit 0). 테스트 24종 exit 0: shared_core / viewer_frame_gate / viewer_liveness / viewer_selection_gate / viewer_picker_gesture / viewer_layout / udp_fec_interleave / udp_control_channel / video_playout_clock / host_kick / host_frame_gate / host_sender_queue_policy / host_abr / capture_cadence_gate / dxgi_output_selection / capture_readback / mf_h264_codec / input_macro / encode_resolution_ladder / host_backend_policy / connect_candidates / bind_port_candidates / punch_any / **viewer_udp_recovery 12/12**. 설치본 `dist/GNLinkSetup-0.2.100.exe`(3,409,920 bytes) — 임베드 버전 확인: GNLinkHost/GNLinkClient/GNLinkSetup 에 0.2.100, 0.2.99 잔존 0(뷰어는 원래 버전 문자열 미포함). 미실행: gdi_capture_process_test(RDP 에서 물리적으로 FAIL 하는 테스트), udp_control_e2e_test·viewer_split_e2e.sh(실제 호스트 필요·캡처 의존).
- 실기 수행 여부: **미수행.** 작업 내내 `qwinsta` 가 `rdp-tcp#0 Active`(RDP 접속 중)라 CLAUDE.md 규칙에 따라 GNLinkStream+GNLinkViewer e2e(캡처 의존)와 호스트 UAC A/B(100/0·8/0·8/2000)는 실행하지 않았다. 판정에 쓴 것은 캡처와 무관한 단위·통합테스트뿐이다. 설치본 생성과 현장 설치 후 검증은 별개다.
- 검증 완료 / 미검증 구분:
  - 완료(자동 테스트): Windows 실제 수신 경로의 NACK 협상·송신·hold 배달, P/IDR 청크 손실·큰 IDR tail·재정렬·NACK 무응답·구버전 peer·seq gap 완성 IDR·source 정지·Congested+첫 IDR 손실·synthetic→real burst·디코더 provenance·손상 IDR 무폭주, 복구 타이머·유예 상한, liveness 판정·bounded join.
  - 미검증(실기 필요): 실제 호스트·네트워크에서의 NACK 효과(assembly-discontinuity↓·IDR 비율↓), UAC 뒤 readback 지연 A/B, 11:20 유형 정지의 최초 방아쇠, dead-session 종료·bounded termination 의 실제 hang 상황 동작, 셸 복귀 UX.
- 남은 미확정/위험:
  - 11:20 장기 정지의 최초 방아쇠(recv 스레드 정지 vs UDP 전달 중단)는 미확정. 0.2.100 의 `[liveness]` 로그가 다음 재현에서 둘을 가른다. dead-session 종료·bounded termination 은 제한된 종료이지 복구가 아니다.
  - 호스트 UAC/readback 지연 원인 미확정(0.2.98/99 계측 그대로, 보호 OFF·디바이스 분리 미착수).
  - in-order hold 는 손실 시에만 최대 120ms 지연을 더한다(무손실 시 0). 상한·grace 는 env 로 조정 가능.
  - 디코더가 실패를 보고하지 않는 손상 완성 IDR 은 감지 불가(S12 관찰). 과거의 중복 요청이 우연히 덮던 경우.
  - Android(`ClientSessionController`)는 공용 스케줄러로 바뀌었을 뿐 동작 동일(hold 미사용); 완성 IDR 중복 요청은 그 경로에도 남아 있다(별도 항목).
  - 실기 판정 기준(설치 후): `udp hello ack … nackNegotiated=1`, 손실 구간에서 `video-nack`/`nackSent>0` 와 `keyReq` 감소, `keyResync>0` 이 reason=2 요청 없이 나타남, 정적→활동 전환에서 `reason=decode_queue` 진입 소멸, `keyRetries` 가 폭주 없이 소수, 정지 시 `[liveness]` 라인으로 stage 구분, 제어 peer-lost 뒤 5s 내 `session-dead` 로그와 셸 복귀.

### 398) 2026-09-07 AGENTS.md 역할 분담 명문화 — Codex 계획·감독, Claude 실행 (A2A task t-7w636lty)
- 목표: 사용자가 Codex 대화에서 지시한 "두뇌는 Codex, 손발은 Claude" 역할 분담을 `AGENTS.md` 의 별도 절(Agent Role Separation)로 좁게 명문화한다. 제품 코드·버전·설치본은 변경하지 않는다.
- 변경 파일: `AGENTS.md`(절 추가; 최상위 삭제 제한·Mandatory Workflow·Scope Control 불변), `docs/history/history_2026-W36.md`(정책 기록·상태), `docs/구현계획.md`(에이전트 운영 정책 상태 1줄), `docs/history.md`(이 항목).
- 내용: Codex = 요구사항 정리·원인/설계 분석·계획/우선순위/완료 기준·A2A 작업 지시·진행 관리·코드/diff/로그 근거 읽기와 교차검토·결과 평가·사용자 보고. Claude = 파일 변경·구현·빌드/테스트·버전/설치본·작업 기록·커밋. Codex 는 실행 작업을 직접 하지 않되 읽기 전용 조사와 완료 구현 리뷰는 감독 업무로 허용(A2A 완료 구현 리뷰 금지 정책은 철회 유지). 위임 시 범위·순서·완료 기준 명시, 회신 시 변경 파일·커밋·검증·미검증/잔여 위험, 수행자 보고와 Codex 직접 확인 근거 구분.
- 검증: diff 로 문구·범위 확인. 빌드·테스트 해당 없음(문서만). Codex(remote#wslm89zu)가 최종 문구를 읽어 확인 예정.
- push 범위(정책 스킬 `agents-policy-updater` 6단계 "push"): 브랜치 `refactor/viewer-split` 은 `origin/refactor/viewer-split`(b3fd92d) 보다 이 커밋 포함 11 커밋 앞서 있어 push 하면 0.2.99~0.2.100 제품 커밋까지 함께 공개된다. 문서 커밋만 따로 push 할 수 없으므로 push 는 사용자/Codex 확인 후로 남김(이 작업에서 미실행). Git MCP 도구는 이 세션에 없어 git CLI 로 커밋.

### 399) 2026-09-07 0.2.100 실기 로그 분석(14:13~14:36) — 릴레이 경유 확정, UAC 클릭 오차 원인 특정, 업로드 401 관측성 결함 (A2A task t-mgxb1ywq)
- 목표: 사용자 실기(정적/타이핑, 고화질 영상, 전환, UAC 2회, 재접속 3회)의 체감 보고를 로그로 검증하고 지연·좌표 오차·NACK 효과를 분리 판정한다. 제품 코드·버전·설치본 변경 없음. 보고서: `docs/field_test_2026-09-07_0.2.100.md`.
- 근거 한계: NAS 의 host/viewer/client 로그가 13:47~13:48 에서 끊겨 이 구간이 **없음**(호스트: 13:48:39 부터 업로드 401, 업로더 자동 재송신 없음·NAS 사본 없음 — 로컬 원본은 수동 회수 가능; 회사 쪽 viewer/client 는 13:56 서버 재시작 **이전**에 이미 멈춰 재시작만으로는 설명 안 됨, Bearer 세션 소멸은 이후 지속 실패 후보, viewer 측 401 직접 로그 미확보). 1차 근거는 로컬 `host_app.log`(GNLinkStream stdout 사본)·`secure_input.log`·NAS 디렉터리 저널. 뷰어 로그 미확보 → nackNegotiated/keyResync/keyRetries/present gap/제어 RTT 판정 불가.
- 확인: (1) 본 세션 14:05:26~14:30:44 와 재접속 4회 모두 **릴레이 경유**(저널 `bound/active`, 호스트 `endpoint translated actual=175.207.45.151:29181`); P2P 미성립은 사실이나 원인은 별개·미확정(후보 목록·관측 포트·punch 송수신·NAT 분석 뒤 판단). (2) 호스트 송신 경로(전수 `udpTxFrames`/`udpTxBytes` 누적차): 정적·타이핑 ≈4.5fps, 영상 58fps·36~38Mbps(목표 12Mbps 의 약 3배, P7 양상), `udpTxFail=0`; wire/user-feedback 로그는 키·지연 이벤트 표본이라 전체 분포 산출에 쓰지 않음(**정정**: 최초 보고의 "med 29ms≈34fps"·p95·"capture→wire ≈15~20ms" 철회, 전체 capture→wire 분포는 미측정; 표본상 정적 구간 실제 프레임 pipeUs 180~190ms — pipeUs 는 capture stamp→주 스레드 입큐 전 송신 준비 시각(인코더 보유 포함, sender 큐·실제 UDP 송신 제외, `…h264_au.cpp:268/563`)이지 capture→wire 총지연이 아님 — 는 비동기 인코더 보유+150ms trailing kick 설계와 크기 부합, 확정 아님); 25분간 호스트 도달 keyframe-request 는 재접속 시 reason=7 ×5 뿐(reason=2 0). (3) UAC: wgc→dxgi 복귀 6.1/4.1/3.0s; `d3d-mt off→on` 이후 readback `captureUnmapWaitAvg` 2→8.5ms, `workerD3dCallAvg` 22µs→6.4~8.3ms(≈acquireTimeoutMs 8), WGC 창 캡처 중엔 보호 on 인데도 94µs → DXGI acquire 락 가설과 일치(확정 아님). (4) **UAC 클릭 오차 = 위치 비례 스케일 오차(x×1.165, y×1.141)**: `secure_input.log` `in=(841,703)/1920x1080 target=(0,0)/2236x1232 mapped=(979,802) virt=(0,0)/1920x1080`. 호스트가 `host_startup_graphics.cpp:218/234` 에서 프로세스 시작 시 1회 읽은 주 모니터(RDP 가상 디스플레이 2236x1232)를 `SetTargetRect` 로 넘기고 갱신하지 않음; 09-05 15:46(RDP 없이 시작) 은 1920x1080 으로 정확. 일반 클릭은 live 모니터 rect 라 정상. (5) 재접속: epoch→첫 IDR 0.86/1.15/2.16(창 모드)/1.08s, 복구 타이머 재접속당 1~2회·폭주 없음. (6) 로그 업로드 401: 호스트 앱 업로더가 시작 시 토큰 1회 구성, 재등록 뒤 갱신 API 없음(`host_app_main.cpp:797-800`, 1335; server.js 657/676 재등록 시 이전 토큰 삭제).
- 미검증: 뷰어 측 전부, 네트워크 RTT·릴레이 몫(추정 불가로 명시), NACK 실기 효과(손실 유무 자체를 알 수 없음), P2P 실패 원인, 5분 장기(미실행).
- 다음 액션: 업로드 401 수정·재실기(그 전 회사 뷰어 로컬 viewer.log 수동 회수), UAC target rect 재갱신(캡처 대상의 현재 물리 rect+원점; rect=0 가상화면 fallback 은 단일 모니터 전체 데스크톱 한정) 제품 수정, 호스트 30s 통계에 NACK 카운터·전수 per-frame capture→wire 계측 추가, P2P 미성립 원인 분석 뒤 조치, acquire timeout A/B. (문구 정정: Codex 감독 검토 5건 반영)

### 400) 2026-09-07 0.2.100 추가 실기 15:09~15:17 — 뷰어 로그 확보, NACK 협상·완성 IDR 무요청 재동기 실기 확인, UAC 전환 시 Congested 2회 관측 (A2A task t-k0c8knr4)
- 목표: 회사 클라이언트 재로그인(15:09:56) 후 NAS 에 다시 쌓인 viewer.log(15:09:57~15:17:41, 8,790줄)로 타이핑→고화질 영상→정적→UAC 를 뷰어 측 지표로 판정한다. 제품 변경 없음. 보고서 `docs/field_test_2026-09-07_0.2.100.md` 9절.
- 근거 범위: 뷰어 1초 stats·present·control RTT 는 전수, user-feedback 은 표본. 호스트 업로드는 여전히 401 이라 호스트 근거는 로컬 host_app.log. 로그 회복은 재로그인 결과이며 P10 코드 수정 아님.
- 확인: (1) 이번 세션은 **직접 연결**(`directory chose 175.207.45.151:43000 (public)`, 릴레이 bound 없음) — 14시 릴레이 세션과 다르나 요인 미확정. 제어 RTT med 2.0ms, p95 4.5~10.2ms, max 20ms. (2) 영상 구간 뷰어 recv=decoded 50.5/s(근사 구간), present gap med 16.1ms p95 46.9ms, avgLatencyUs(1초 평균 상대 타임라인 lag 지표 — `aligned_lag_us`: 첫 샘플 기준 0, 음수 절단; 절대 입력/종단 지연 아님) med 8.4ms; 타이핑 5.8/s·present gap med 218ms·같은 지표 med 113ms(어느 쪽 몫으로도 귀속하지 않음); 뷰어 decUs med 5ms·d2pUs med 5~9ms 는 각각 별개 user-feedback 표본(합산 안 함). (3) **NACK 협상 실기 확인**(`nackNegotiated=1`, `nackOn=1`)이나 청크 310,337개 수신 동안 `nackSent=0 fecRecovered=0 reorder=0` 로 재전송/복구 미관측 → 효과 미입증(손실 0 확정 아님; `dropped` 57 은 keyResync 로 처리된 AU 불연속이며 송신 측 의도적 gap 인지 AU 손실인지 구분 불가). (4) **완성 IDR 무요청 재동기 실기 확인**: seq gap IDR 57건 전부 `keyResync`, `keyReq=0`, 호스트 도달 요청은 시작 reason=7 1회 + UAC reason=1 2회(09-05 의 90/90 대비 폭주 없음). stale/holdLatest 0, liveness/peer-lost 없음. (5) UAC(15:15:56 wgc→15:15:59.6 dxgi, host 시각): Congested 2회(reason=decode_queue, seq 24471 lag 567ms / seq 24488 lag 848ms, 각 IDR 1회로 0.3~0.6s 내 normal). seq 연결: 2차는 DXGI 복귀 첫 프레임 seq 24484(같은 ms 의 wire 라인, wireIntUs 809ms; capture stamp 가 입큐 전 송신 준비 시각보다 ~820ms 오래됨, 뷰어 c2eUs 812ms) 가 침묵 뒤 앵커로 잡힌 직후 24486~24488 이 신선한 stamp 로 밀집 도착한 것; 1차는 전환 첫 프레임(seq 24456) 이 아니라 2.3s 뒤 침묵 후 재개된 seq 24469(c2eUs 503ms) 가 앵커. 벽시계 역순(뷰어 59.178 < 호스트 59.610)은 요청 도착 두 쌍이 모두 +556ms 인 시계 오프셋으로 설명됨. 오래된 stamp 의 호스트 측 출처와 합성 프레임 경로와의 관계는 미확정 → "UAC 연관 혼잡 2회 · 원인 후보/미확정" 으로 신규 항목 P11. 사용자 체감 없음. (6) UAC 클릭 좌표: secure_input 에이전트의 `inject landed` 진단이 프로세스당 12개 버튼 이벤트만 기록(14:27 에 소진)해 15:15 클릭은 로그 없음 → 판정 불가, P9 미해결 유지.
- 잔여: NACK 재전송 효과(재전송 관측 필요), 절대 입력/종단 지연(미측정), 14시 릴레이 세션 RTT, 호스트 전수 capture→wire, P2P 성립 요인, UAC 좌표(진단 예산 재충전 계측 필요), 5분 장기, P10·P11 코드 수정.
- 정정(2026-09-07, 73445cb 에 대한 Codex 리뷰 반영, 문서만): avgLatencyUs 를 절대 지연처럼 쓰고 113ms 를 호스트 몫으로 귀속한 문구, decUs+d2pUs 를 "뷰어 총 ~10ms" 로 합산한 문구, "손실 0" 단정, UAC 혼잡을 전환 첫 프레임 stamp 로 확정한 문구를 위와 같이 고침(보고서 9.0/9.2/9.3/9.5, 구현계획 P2/P11).

### 401) 2026-09-07 P10 로그 업로드 401 — 업로더 재구성(토큰 교체·identity 격리)·401 일시정지·상한 재시도·서버 세션 persist (A2A task t-xox45oo5 1/3)
- 목표: 재등록/재로그인 뒤 업로더가 구토큰으로 영구 401 을 받던 경로(09-07 13:48~15:55 `failedBatches=2395`)와 서버 재시작 시 클라 세션 소멸을 근거대로 나눠 고친다. 계획 `.claude/viewer_stabilization_followup_plan.md` P10.
- 원인(코드): `log_upload_start` 가 실행 중이면 새 config 를 버리고 true(구 :159), 헤더 1회 조립·갱신 API 없음; 호스트 sign-in 결과(`host_app_main.cpp` WM_APP+2)와 클라 재로그인이 모두 no-op; 서버는 재등록 시 이전 해시 즉시 삭제(`server.js` handleHostRegister), `sessions` 는 in-memory; `send_batch` 는 큐에서 뺀 배치를 재시도 없이 유실. 클라는 비밀번호를 저장하지 않아 자동 재로그인 불가 → 401 은 "재인증 필요" 상태로만 처리 가능.
- 변경: `log_upload.{hpp,cpp}` 재작성 — `log_upload_configure`(실행 중이면 토큰/URL/identity 잠금 교체, 같은 identity 면 큐 유지, identity 바뀌면 큐·보관 배치 폐기), `log_upload_clear_credentials`(사인아웃: 헤더 제거·큐 폐기·이후 줄 드롭), `log_upload_status`, 401 콜백. 워커는 배치마다 헤더를 잠금 아래 복사(무잠금 읽기 제거). 실패 분류: 401→`authRejected` 일시정지(배치 보관 최대 4개, 새 토큰 오면 먼저 재전송; 교체된 구토큰의 늦은 401 은 무시), 미도달/5xx/408/429→재시도(초기 송신 포함 총 3회: 2s·4s 뒤 재시도 2회)·60s 나이 상한, 기타 4xx→즉시 폐기. 큐 4MB 상한에 보관 배치 포함. diag 에 토큰 없음. `host_app_main.cpp`: sign-in/시작 시 `configure`(identity=계정/machineId), `sign_out` 에서 `clear_credentials`, 상태 카드에 "Log upload: … sign in again". `client_shell_main.cpp`: 로그인 시 `configure`(identity=계정@서버)+401 콜백(페이지 안내 1회), 로그아웃 시 `clear_credentials`. `server.js`: 세션을 `store.sessions` 에 해시+만료로 persist(로그인 시 즉시 저장, sweep/로드 시 만료 제거) — hostTokens 유예는 두지 않음(인증 약화 금지). 테스트: 신규 `remote60_log_upload_test`(가짜 HTTP 서버) 8 케이스, `logs_test.js` +6(호스트 토큰 성공·재등록 뒤 구토큰 401/신토큰 200·미지 세션 401), `restart_test.js` +2(세션 재시작 생존·미지 세션 거부).
- 검증: `remote60_log_upload_test` PASS ×3(연속), `node test/run.js` ALL PASS(directory/restart/relay/logs), GNLinkHost·GNLinkClient 빌드 OK. **실기 미실행**: 이 PC 호스트 프로세스 교체·NAS 서버 배포는 지시 없이 하지 않음 — 실제 NAS 업로드 복구는 설치본 적용 후 사용자 실기 항목.
- 다음: P9(UAC 좌표) → P11(UAC 복귀 혼잡) → 0.2.101 설치본.

### 402) 2026-09-07 P9 UAC 클릭 좌표 — secure-input target rect 를 캡처 (재)시작마다 실제 캡처 모니터로 재도출·재푸시, 에이전트 landing 진단 예산 에피소드 재충전 (A2A task t-xox45oo5 2/3)
- 목표: 호스트가 프로세스 시작 시 1회 읽은 주모니터 rect(RDP 중 시작 → 2236x1232)를 SYSTEM 에이전트에 넘기고 갱신하지 않아 콘솔(1920x1080) 복귀 뒤 UAC 클릭이 x×1.165/y×1.141 로 어긋나던 결함(보고서 4절)과, 15:15 UAC 클릭 기록이 없던 진단 예산 소진(9.4절)을 고친다.
- 원인(코드): `SetTargetRect` 호출은 `host_startup_graphics.cpp` 1곳뿐, 캡처 재시작 17곳(`restart_capture_session`)·`RestartCaptureSessionImpl` 의 monitorInfo 갱신(DXGI 분기만) 모두 브로커 미갱신. 에이전트는 rect 가 0 일 때만 virtual screen 폴백이라 stale 비영 rect 가 이김. secure 경로는 데스크톱 모드 전용(창모드 skip). 진단 예산 `static atomic<int> remaining{12}` 는 에이전트 프로세스당 1회성(desktop 이름 변경 때만 재생성).
- 변경: 신규 `host_input_target_rect.hpp`(순수 규칙: 창모드→0x0, 캡처가 연 모니터의 물리 rect+원점 그대로, 미상→0x0; DPI 스케일 없음) + `sync_input_target_rect(capture, inputRouter, reason)`(`host_loop_helpers.cpp`, 선언 `host_main_loop.hpp`): `restart_capture_session` 꼬리와 시작 경로에서 재도출, 값이 바뀔 때만 `SetTargetRect` + 로그 `secure-input target rect=(x,y)/WxH source=… reason=…`. `InputRouterState.targetRectSent` 로 변경 감지. `RestartCaptureSessionImpl`: 데스크톱 모드면 백엔드와 무관하게 `primary_monitor_info()` 갱신(WGC/GDI 도 주모니터를 열므로; DXGI 만 실패 시 중단). 신규 `secure_input_diag_budget.hpp`: landing 진단 12/에피소드(2s 공백 시 재충전)·60/분 상한, 에이전트 `diag_inject_landing` 에 적용. 전체 virtual rect 치환 없음.
- 테스트: 신규 `remote60_host_input_target_rect_test`(RDP 2236x1232→콘솔 1920x1080 전환 시 rect 갱신·같은 모니터 재시작은 무변경, 음수 원점 모니터·4K 상단 모니터, 창모드 0, 미상 0) PASS; `remote60_secure_input_mapping_test` +2(실측 클릭 (841,703)/(808,704) 가 stale rect 에서 (979,802)/(941,803), live rect 에서 원위치; 예산 12/에피소드·2s 재충전·연속 클릭 무재충전·분당 상한 96/360) PASS. GNLinkStream·GNLinkInputService 빌드 OK.
- 미검증(사용자 실기): 실제 UAC 클릭 육안 — 설치 후 호스트 로그 `secure-input target rect=(0,0)/1920x1080` 와 `secure_input.log` 의 `target==virt`·`mapped==in` 을 UAC 2회 이상에서 확인(두 번째 UAC 도 기록돼야 예산 수정 확인). 다중/선택 모니터 실기는 장비 없음.
- Ledger(수정 안 함): 모니터 선택 뒤 첫 재시작에서 WGC `CreateItemForPrimaryMonitor(nullptr)`/DXGI `monitorInfo->monitor` 로 주모니터로 되돌아가 선택이 풀림(`host_capture_session.cpp` restart-refresh) — rect 는 실제 캡처 모니터를 따르므로 입력-화면 일관성은 유지되나 다중 모니터 선택 자체는 별도 결함. 일반 데스크톱 경로(`host_input_inject.cpp map_input_to_primary_monitor_point`)도 주모니터 고정.
- 다음: P11.

### 403) 2026-09-07 P10 보완 — 서버 세션 persist 범위 제외(되돌림), 업로더 owner/routing epoch 펜싱, 늦은 응답 회귀 8종 (A2A task t-xox45oo5, Codex 리뷰 반영)
- 범위 정정: Codex 감독 회신(seq 1052)에서 **서버 sessions persist 는 이번 작업에서 제외**(서버 인증 수명/저장 계약은 별도 범위)했으나 f911845 에 포함돼 있었다. `server.js` 의 세션 해시 저장·로드·sweep 변경과 `restart_test.js` 의 "세션 재시작 생존" 기대를 제거하고 원래 계약(세션은 in-memory, 재시작 후 401)으로 되돌렸다 — 되돌림 커밋이며 f911845 revert/이력 재작성 아님. `restart_test.js` 는 "재시작 뒤 세션은 잊힌다(계약)" + "미지 세션 401" 을 검사한다. `logs_test.js` 의 호스트 토큰/재등록 회전/미지 세션 케이스는 기존 계약 검사이므로 유지. **history #401 의 "서버 세션 persist(코드)" 항목은 이 시점부로 적용 범위가 아니다.**
- P1 경계 오류(Codex 코드 검토): 구 job 의 늦은 401 이 `hold_locked` 로 배치를 되살린 뒤 generation 만 비교해 pause 를 생략했고, Transient(5xx)·clear_credentials 뒤 늦은 응답에도 owner 펜싱이 없어 A 계정 배치가 B 계정 헤더로 재전송될 수 있었다(`log_upload.cpp` 구 :284-326). `sameIdentity` 도 문자열 하나만 비교해 URL/장치 변경 시 큐를 보존했다.
- 변경(`log_upload.{hpp,cpp}`): owner key = identity + 정규화 host:port + device. owner 변경(계정/URL/장치)과 `clear_credentials` 는 `ownerEpoch` 를 올리고 큐·보관 배치를 폐기; 모든 send job 이 `ownerEpoch` 를 스냅샷하고, 응답 처리에서 epoch 이 다르면 **pause/보관/현재 인증 상태를 건드리지 않고 배치만 폐기**(`foreignAnswersDiscarded`, diag `late answer for a previous owner discarded`). 같은 owner 의 토큰 교체만 구 401 배치를 새 토큰으로 재시도. 상태에 `foreignAnswersDiscarded` 추가.
- 테스트(`remote60_log_upload_test`): 기존 7 케이스 + 신규 [9] ×8 — A 의 요청을 서버가 400ms 붙든 채 (a) B 계정 전환 (b) URL 변경(제2 가짜 서버) (c) device 변경 (d) 로그아웃→같은 계정 재로그인 뒤 old 401 / old 500 각각 반환: 새 수신자에 old body 미전송, 새 세션 pause 없음(콜백 0), 보관 0, `foreignAnswersDiscarded==1`, 새 줄은 새 토큰/장치로 전송. diag 에 토큰 없음 유지. PASS ×2. `node test/run.js` ALL PASS(되돌린 계약 포함). GNLinkHost/GNLinkClient 는 API 불변(재빌드는 릴리스 빌드에서).
- 미검증: 실제 NAS 업로드(설치 후 실기), 서버 배포 없음.

### 404) 2026-09-07 P9 보강 — target rect 를 실제 캡처 HMONITOR 의 live 기하로 읽고 1초마다 재확인(같은 해상도·원점만 변경 포함) (A2A task t-xox45oo5, Codex 리뷰 반영)
- 배경: #402 는 "모든 기하 변경은 캡처 재시작을 지난다" 는 가정 위에 있었다. Codex 지적대로 같은 해상도에서 모니터 원점/주모니터 배치만 바뀌면 size-change 가 없어 재시작이 없을 수 있다.
- 변경: `sync_input_target_rect`(`host_loop_helpers.cpp`) 가 캐시된 `monitorInfo` 의 rect 가 아니라 캡처가 연 HMONITOR(`capture.monitorInfo->monitor`)에 `GetMonitorInfoW` 를 **live** 로 물어 물리 rect+원점을 도출(핸들이 무효면 마지막 기하 유지). 호출 지점에 `stage_stats` 의 1초 tick(`"periodic"`) 추가 — GetMonitorInfo 1회+비교, 값이 바뀔 때만 `SetTargetRect`+로그. broker 자체 mutex 유지, 창모드 skip 유지, virtual 전체 치환 없음.
- 테스트: `remote60_host_input_target_rect_test` +1 — 같은 1920x1080 에서 원점 (0,0)→(1920,0)/(0,1080) 변경이 rect 변경으로 판정되고 매핑이 새 원점으로 감. PASS. GNLinkStream 빌드 OK.
- 미검증: 실제 배치 변경(다중 모니터 장비 없음); 실기 시 호스트 로그 `secure-input target rect= … reason=periodic` 으로 확인 가능.
- Ledger 유지: 모니터 선택이 다음 재시작에서 주모니터로 되돌아가는 결함은 이번 범위 밖(#402).

### 405) 2026-09-07 P11 UAC 복귀 혼잡 — 인코더 flush epoch gate(새 epoch 첫 송출 = 새 IDR) + 뷰어 "보유된 재개 프레임은 anchor 아님" 보정, 실 HW MFT 재현 (A2A task t-xox45oo5 3/3, Codex seq 1052 설계 반영)
- 원인(코드·실측): 와이어 `captureQpcUs` 는 인코더 accepted-input FIFO 의 front(`mf_h264_codec.cpp`) 라 비동기 MFT 는 이전 입력의 AU 를 현재 호출에서 내놓는다. flush(`FlushCapturePipelineState`) 는 캡처 링만 비우고 인코더 보유분은 두므로 복귀 flush 뒤 첫 호출에서 **전환 전 입력의 AU 가 현재 generation·real 플래그·오래된 stamp** 로 송출(09-07 15:15:59 seq 24484, hold 820ms). **메커니즘 재현(다른 입력 경로)**: `remote60_host_encode_epoch_test` [2] — 이 PC 의 `mft_enum_hw` 를 D3D11 NV12 surface 입력 경로(`set_d3d11_device`+`encode_frame_surface`)로 구동하면 A→flush→B(forceKey) 에서 A 의 AU 가 flush 뒤 호출에 800,000us 오래된 stamp 로 나온다. **정정(Codex 검토)**: 현장 호스트(15:15:01/31 stats `nv12SurfaceFrames=0 nv12Converted=0`)는 BGRA 버퍼 경로(`encode_frame_bgra`)였고, 같은 MFT 의 CPU 버퍼 경로에서는 테스트에서 호출마다 자기 AU 를 돌려줘 재현되지 않았다(#407 에서 BGRA 경로 재시도 결과 병기) → **현장 호스트의 seq 24484 원인은 미확정**이며, 재현은 "다른 입력 경로에서의 메커니즘 재현" 이다. 보유는 backend·경로별 관측 패턴이지 일반 계약이 아님. secure desktop 에서 kick 이 차단돼 실제 AU 가 0.5s 보유되는 15:15:57 형상(seq 24469)은 호스트 측 **미해결**(secure kick 허용은 Codex 지시로 이번 범위 제외, 별도 설계) — 뷰어 보정은 그로 인한 **오탐 IDR 비용을 줄일 뿐**, 오래된 픽셀의 실제 지연은 남는다.
- 호스트 변경: `H264AccessUnit.inputEpoch` + `H264Encoder::set_next_input_epoch`(FIFO lockstep, initialize/shutdown reset, overflow 시 함께 pop). `CaptureState::inputEpoch`(atomic) 를 `EncoderState::ResetTimelineAnchors` 에서 +1(호출처 21곳 = flush/restart/geometry-confirm/encoder-reset 경계; ±10줄 내 `forceKeyNext=true` 미인접 6곳: encoder_manager 64/75(MFT 재생성→첫 출력 IDR), loop_helpers 396(window-select), selection 177/248, startup 401(기본 true) — gate 가 P 를 만나면 스스로 재요청하므로 계약 무관). 신규 `host_epoch_gate.hpp`(순수): old-epoch AU 는 key 여도 폐기(latch/barrier 불만족), 새 epoch 의 첫 current-epoch AU 가 P 면 폐기+`forceKeyNext=true` 재요청, 첫 current-epoch key 만 통과; 상한 700ms/12AU 초과 시 verdict `ResetEncoder` → emit 단계가 기존 stale-reset 경로(codec 재생성+forceKey)로 처리. 보류·재송출 없음(폐기된 AU 는 seq 를 소비하지 않아 뷰어 gap 없음). 로그 `epoch-gate dropped-old/dropped-nonkey/key-accepted/encoder reset`, `capture-switch first-frame … auEpoch= curEpoch= holdUs=`(입큐 전 시각 기준, wire 지연 아님), user-feedback/trace `auEpoch curEpoch`. `KickTryFill` 의 secure 차단·캐시 provenance 는 **변경 없음**.
- 뷰어 변경(좁은 보정, `viewer_frame_gate.{hpp,cpp}`, `viewer_frame_gate_state.hpp`, `viewer_video_receiver_frame.cpp`): `FrameGateInputs.sendQpcUs` 추가. Normal 상태에서 침묵(>250ms) 뒤 첫 실제 프레임의 hold(`sendQpc−captureQpc`, 호스트 stamp 끼리) 가 `resumeAnchorMaxHoldUs`(300ms) 를 넘으면 present 하되 `presentAnchorFloorUs` 를 재고정하지 않고 pending; 이어지는 hold≤300ms 인 첫 실제 프레임이 재고정. pending 중 entry 추정은 들어온 stamp 를 floor 로 씀(lag 0). 상한 8프레임/500ms 뒤 기존 규칙 복귀(모든 프레임이 보유된 진짜 backlog 는 그대로 Congested). synthetic·비정상 상태에서는 비활성. sendQpcUs=0(구 호스트)이면 기존 규칙.
- 테스트: `remote60_host_encode_epoch_test`(신규, 실 MFT) [1] epoch·synthetic·timestamp accepted-input→AU 일치, [2] 위 재현 → gate 적용 시 첫 송출=새 epoch IDR·old 폐기 1·reset 0, [2b] fabricated: old-epoch key 미개방, P 선행 폐기+재요청, 연속 flush(epoch 2회), 미태그(0) 통과, 개수/시간 상한 reset, [3] 보유 관측(backend 명시) — PASS. `remote60_viewer_frame_gate_test` +2: [T-P11a] 보유 재개 프레임 미anchor·다음 신선 프레임 anchor·Congested 0, [T-P11b] 진짜 backlog(모두 보유·anchor 정지)는 pending 상한 뒤 Congested — PASS(전체). `remote60_viewer_udp_recovery_test` 14/14 PASS: S13(가짜 호스트가 두 pre-fix 형상 생성: old real→fresh burst / old real→IDR→fresh) 뷰어 transitions 0·요청 0·heldResume 1/2, S14(FakeHost 가 실 인코더 + 제품 `host_epoch_gate.hpp` 를 그대로 통과, `Flush()`=epoch+1·forceKey, 와이어 stamp=AU 자기 입력 stamp) flush 뒤 첫 와이어 AU 가 key·요청 0·Congested 0·decode 지속 — **한계**: FakeHost 는 CPU 입력 경로라 대개 동기식이어서 S14 에서 gate 의 old 폐기는 round 별 0~1건(폐기 경로의 실 MFT 검증은 epoch test [2] surface 경로); FakeHost 는 sender 스레드가 없으므로 송신 경계는 #407 의 실 sender 테스트가 담당; 기존 S1~S12 회귀 유지. GNLinkStream·GNLinkViewer 빌드 OK.
- 계약(수명): (i) 일반 데스크톱: 실제 stamp=callback qpc, 이 backend surface 경로에서 관측된 보유 = 다음 입력까지(kick 150ms 요청이 그 다음 입력; CPU 경로는 관측상 호출마다 반환), 뷰어 anchor 는 신선한 실제 stamp 만. (ii) flush(epoch+1): emit 단계 gate 는 새 epoch 의 첫 송출을 IDR 로 보장하되 **범위는 "flush 이후 새로 wire 에 시작되는 AU"** — 이미 sender 큐에 있던 old AU 는 #407 의 dequeue 펜스가 폐기하고, flush 시점에 chunk 송신이 시작된 AU 1개는 완료됨(in-flight 예외; 뷰어의 held-resume 규칙이 그 stamp 를 anchor 로 쓰지 않음). 미상(0)/미래 epoch AU 는 fail-closed 폐기. generation 표기는 첫 IDR 부터. (iii) 복귀 flush 동일. 뷰어는 보유된 재개 프레임을 표시만 하고 anchor 는 신선 프레임에.
- 미검증(사용자 실기): UAC 2회에서 호스트 `epoch-gate dropped-old` ≥1·`capture-switch first-frame … auEpoch==curEpoch holdUs<200ms`, 뷰어 `[congestion]` 0. RDP Active 면 제외.

### 406) 2026-09-07 0.2.101 릴리스 — P10 로그 업로드 재포인팅/401 일시정지/owner 펜싱, P9 secure-input target rect live 재도출, P11 epoch gate + 뷰어 보유 재개 프레임 보정 (A2A task t-xox45oo5 마감)
- 변경 파일: `product_version.hpp` 0.2.100→0.2.101, `docs/history.md`, `docs/구현계획.md`.
- 포함 커밋: f911845(P10)·e997b97(P10 보완: 서버 persist 범위 제외+owner epoch)·03b0d13(P9)·dbb7fc0(P9 보강: live rect·periodic)·1607404(P11). 제품 코드 변경은 GNLinkHost(업로더·상태카드), GNLinkClient(업로더·페이지 안내), GNLinkStream(target rect 재도출·periodic, 인코더 epoch FIFO·gate·로그), GNLinkInputService(landing 진단 예산), GNLinkViewer(보유 재개 프레임 anchor 보정). 서버(`server.js`)는 최종적으로 원래 계약(변경 없음)이며 배포 없음.
- 검증 결과: 전체 Release 빌드 exit 0(error 0, 신규 경고 0). 테스트 집계(정정, Codex 검토): 1607404 시점 바이너리로 전수 39종 실행(`.claude/test_sweep_0.2.101.txt`) = **36 PASS + 3 FAIL**(recovery S7 간헐, gdi_capture_process, udp_control_e2e). recovery 는 23b0f3c(하네스: gap 을 다음 키프레임 앞에 적용) 뒤 3/3 PASS(14 시나리오) → 그 시점 기준 **37 PASS + 2 미해소**. `remote60_gdi_capture_process_test`(3.7fps, BitBlt 258ms)·`remote60_udp_control_e2e_test`(udp hello ack 실패, 포트 43000) 는 실행 당시 이 PC 에서 설치된 GNLinkHost/GNLinkStream 이 실행 중이었고(DXGI duplication·포트 43000) 라이브 프로세스를 지시 없이 중지하지 않아 통제 재실행이 없다 → **환경 영향 추정, 미해소** 로 표기(인과 확정 아님). 두 테스트는 이번 변경 영역(GDI 워커·e2e 하네스)과 겹치지 않는다. S7 간헐 실패 원인: 비동기 인코더에서 forceKey 호출이 직전 P 를 먼저 내놓아 하네스의 seq gap 이 IDR 이 아닌 P 앞에 놓임 → 뷰어의 정당한 reason=3 요청. `node test/run.js` ALL PASS. 설치본 `dist/GNLinkSetup-0.2.101.exe`(3,457,536 bytes, 0.2.101 임베드 확인). 실기 미실행.
- 사용자 최소 실기(설치 0.2.101 후, RDP 종료 상태): (1) 호스트 GNLinkHost 재시작 → `log_upload.diag` 에 `configured started …` 와 NAS host.log 재개 확인, Change account 재로그인 뒤 `configured token replaced …`·401 없음. (2) 회사 클라 0.2.101 로그인 → NAS viewer.log/client.log 연속; 서버 재시작 없이도 재로그인 시 `owner changed/token replaced` diag. (3) UAC 2회: `secure_input.log` 에 두 번째 UAC 도 `inject landed … target=(0,0)/1920x1080 mapped==in` 기록, 호스트 로그 `secure-input target rect=(0,0)/1920x1080`; 호스트 `epoch-gate dropped-old`·`capture-switch first-frame … auEpoch==curEpoch`, 뷰어 `[congestion]` 진입 0. (4) 정적 화면 타이핑·고화질 영상 회귀 없음(뷰어 stats `congestionState=normal`, keyReq 폭주 없음).

### 407) 2026-09-07 완료조건 보완 — sender 경계 펜싱·gate fail-closed·dispatch 직전 rect 재확인·뷰어 pending 수명, 재현 경로/집계 정정, 0.2.102 (A2A task t-xox45oo5, Codex seq 1059/1060 반영)
- 정정(사실): (a) 현장 호스트(09-07 15:15:01/31 stats `nv12SurfaceFrames=0 nv12Converted=0`)는 **BGRA 버퍼 입력 경로**였다. `remote60_host_encode_epoch_test` [2] 를 세 경로에서 실행한 결과 — BGRA 버퍼: 같은 `mft_enum_hw` 가 호출마다 자기 AU 를 돌려줘 **미재현**, NV12 버퍼·D3D11 surface: flush 뒤 호출에 전환 전 AU 가 800,000us 오래된 stamp 로 나옴(재현). 따라서 #405 의 "제품과 같은 경로 재현/seq 24484 원인 확정" 표현을 "다른 입력 경로에서의 메커니즘 재현, 현장 호스트 원인 미확정" 으로 정정(#405 본문·`host_epoch_gate.hpp` 주석·보고서). (b) "뷰어 보정이 비용 제거" → "오탐 IDR 비용 감소, 오래된 픽셀의 실제 지연은 남음". (c) P10 재시도 계약: 초기 송신 포함 총 3회(2s·4s 뒤 재시도 2회) — #401/#403·계획의 "2/4/8s ×3" 표현 정정. (d) 테스트 집계는 실행 시점·커밋별로: 1607404 sweep 39 = 36 PASS + 3 FAIL, 23b0f3c 뒤 recovery 3/3 → 37 PASS + 2 미해소(GDI·e2e, 실행 중 host 영향은 통제 재실행 없어 **추정**).
- P11 gate(`host_epoch_gate.hpp`): 미상(0)·미래(>current) epoch 는 `DropUnknownEpoch` 로 fail-closed(송출·gate 개방 불가); 코덱 FIFO 가 비어 있으면 이 호출의 epoch 로 꾸미지 않고 0 표기(`mf_h264_codec.cpp`); 유효 current-epoch IDR 은 상한 검사보다 먼저 수락(기존 방향 유지); encoder rebuild 상한 3회/10s(초과 시 폐기+재요청만, `resetsSuppressed`). 테스트 [2b]: 미상/미래 key 미개방, 연속 flush, 개수/시간 상한, 늦은 유효 IDR 수락, rebuild 상한.
- P11 송신 경계(`host_encoded_sender.{hpp,cpp}`, au.cpp, `host_startup_capture.cpp`): `EncodedSendItem.inputEpoch` + sender dequeue 에서 `CaptureState::inputEpoch`(atomic 참조) 미만이면 폐기(`inputEpochDropCount`, 로그 `sender dropped pre-flush AU`). **보장 범위**: flush 이후 새로 wire 에 시작되는 AU 는 새 epoch IDR 이 첫 번째; flush 시점에 chunk 송신이 진행 중이던 AU 1개는 완료(in-flight 예외, 뷰어 held-resume 규칙이 stamp 를 anchor 로 쓰지 않음). 신규 `remote60_host_sender_epoch_test`(**실 sender 스레드**, loopback UDP): [A] 큐에 old 3개 + flush + IDR → old 0건 송출·IDR 첫 AU·이후 delta, [B] 900KB old AU 송신 중 flush → 그 AU 완료(810/810 payload chunk)·큐의 old delta 폐기·IDR 다음·인터리브 없음, [C] 펜스는 epoch 기준(현재 generation 표기 old AU 폐기, 미태그 통과). PASS ×2.
- P9 dispatch(`host_secure_target_rect.hpp` 순수 결정 + `secure_target_rect_ready` in `host_loop_helpers.cpp`, `host_control_session.cpp` secure 이벤트/텍스트 분기, `CaptureState::captureMonitorHandle`, 브로커 `GetTargetRect`): secure 전달 직전 실제 캡처 HMONITOR 의 live rect 를 재확인해 바뀌었으면 브로커 갱신(로그 `reason=dispatch`, `secureRectUpdatedAtDispatch`), 조회 실패/빈 rect 면 **전송 거부**(`secureSkipRectUnknown`, rate-limit 로그) — 구 rect·0 rect(virtual 폴백 우회) 로 보내지 않음. `sync_input_target_rect` 도 조회 실패 시 구 rect 유지 대신 브로커 rect 를 비우고 로그. 테스트 [2c]: 원점 변경 후 1초 미경과 첫 클릭 → 새 rect 로 갱신 후 전송, 조회 실패/빈 rect → 거부, 창모드 → 거부, rect 없음 → 갱신. 실제 dispatch 경로(소켓 세션)는 배선으로만 검증(단위테스트 없음, 한계).
- 뷰어 pending(`viewer_frame_gate.cpp`): 같은 episode 의 추가 held gap 프레임은 since/count 를 재설정하지 않음; 유효 stamp(둘 다 0 아님·send≥capture)만 held/fresh 판정, 미상·역전은 기존 규칙(pending 시작/종료 불가); 키프레임 wait 진입·decoded key(`note_reference_sync`)·상태 전환 시 pending 종료; 보정은 decode_queue entry floor 에만 적용(stream-lag emergency·key wait·stale drop 불변). 테스트 +4: host hold 작음+renderer 정체 backlog 트립, 수명 종료, 미상/역전 stamp 기존 규칙, 반복 gap 예산 미연장. `viewer_udp_recovery_test` 재실행 결과는 아래 "검증".
- 검증(0.2.102 빌드, 커밋 927b9f4·6a9aa32·c3c3d76 + 버전): 전체 Release 빌드 exit 0(error 0). 전수 sweep 40종(`.claude/test_sweep_0.2.102.txt`) = **39 PASS + 1 FAIL**. FAIL 은 `remote60_udp_control_e2e_test`("udp hello ack failed", 포트 43000) 1종 — 실행 당시 설치된 GNLinkHost/GNLinkStream 실행 중, 통제 재실행 없음 → 환경 영향 **추정**. #406 에서 FAIL 이던 `remote60_gdi_capture_process_test` 는 같은 라이브 호스트 상태에서 이번엔 PASS(53.0fps, copy avg 18.9ms; 이전 3.7fps/258ms) → 그 실패는 라이브 호스트 때문이라 볼 수 없고 **간헐·원인 미확인**으로 정정. `remote60_viewer_udp_recovery_test` 14/14 (sweep 1회 + 별도 2회 PASS), `remote60_host_sender_epoch_test` PASS×3, `remote60_host_encode_epoch_test` PASS(3경로), `remote60_viewer_frame_gate_test` PASS(+4), `remote60_host_input_target_rect_test` PASS(+1). 설치본 `dist/GNLinkSetup-0.2.102.exe`(3,460,608 bytes, 0.2.102 임베드 확인). 실기 미실행.
- 미검증: 실기(UAC 2회, 로그 업로드, 원점 변경 배치)·다중 모니터 장비. 서버 배포·설치·push 없음.

### 408) 2026-09-07 정책 문서 — 3역할 절차(Codex 계획 → 검증용 Claude 검토·확정 → 작업용 Claude 구현 → 검증용 검사·OK → Codex 최종 확인) 명문화 (A2A task t-5ahw0t5x 계획 A, 검증용 remote#1528xbin 위임)
- 목표: 사용자가 확정한 새 절차를 `AGENTS.md`(공통 역할·흐름)와 `CLAUDE.md`(두 Claude 세션의 구체 행동·보고 기준)에 반영한다. 배경: 2026-09-07 P9/P11 후속 작업에서 작업 세션이 검증 없이 완료 보고·릴리스를 반복한 문제와 GMux 재시작 체크포인트(`.claude/gmux_restart_checkpoint.md`).
- 변경 파일: `AGENTS.md` — "Agent Role Separation (2026-09-07, 3역할 절차)" 절을 다시 씀(Codex/검증용/작업용 담당, 완료 판정 규칙, 역할 식별은 버스 displayName + `a2a_whoami`, ephemeral ID 기재 금지, "Claude 담당: … 커밋" 을 작업용으로 한정, 과거 "A2A 완료 구현 리뷰 금지" 미부활, 최상위 삭제 제한·Mandatory Workflow·Scope Control 불변). `CLAUDE.md` — "3역할 절차에서 두 Claude 세션의 행동·보고 기준" 절 추가(검증용의 "구현 OK" 고정 형식: commit 해시·검토 범위·직접 실행/보고 구분·산출물 파일명·크기·sha256·빌드 commit·미검증 한계; OK 뒤 변경 시 재검사; 작업용의 완료 검사 요청 형식·working 유지·단독 PASS 로 회귀 FAIL 상쇄 금지). 기존 응답·RDP·NAS 로그 규칙 유지.
- 검증: 문서 변경만(빌드/테스트 없음). `docs/구현계획.md` 변경 없음(정책 항목 없음).
- 추가(같은 task, Codex 요청·검증용 전달, 후속 docs(policy) 커밋): 검증용 역할 항목에 "계획 변경이 필요하다고 판단한 부분은 Codex 의 수정·확정 전에는 작업용에게 위임하지 않는다. 변경 없이 OK 한 독립 범위는 바로 위임할 수 있다." 를 `AGENTS.md`·`CLAUDE.md` 에 그대로 명시 — 검증용의 임의 수정안 선위임 방지. 계획 A 의 조건 3건은 Codex 승인.
- 다음: 계획 B(S13 held-resume pending 회귀 수정, 0.2.103 후보) — 같은 task.

### 409) 2026-09-07 S13 held-resume pending 회귀 수정(변형 R) — 수신기 호출 순서 회귀 선행, 결정적 재현 (a′), sweep ×2 (A2A task t-5ahw0t5x 계획 B, 검증용 remote#1528xbin 위임)
- 선행 커밋 기록(누락분): **1287883** "sender permission point before the first datagram; secure input through one dispatch path" — sender 허용 판정을 pacing 뒤 첫 datagram 직전으로 옮기고 active fence 는 현재 epoch 만 통과(old·미태그·미래 폐기, legacy 는 fence 비활성 시만), 예외 = "flush 전에 첫 datagram 이 허용된 AU 1개"(테스트 seam `beforeFirstDatagramHook`); `CaptureState::InputTargetSnapshot`(version·mode·HMONITOR·gen, lock) + `secure_dispatch`(주입 포트) + 브로커 `SendInputEventWithRect/TextWithRect` 로 secure 분기·재probe·default fallback·텍스트 4경로 통일; `remote60_host_sender_epoch_test` [A]/[B]/[B']/[C]/[D], `host_input_target_rect_test` [2c] 7케이스, recovery S15(제품 SenderState 스레드 경유). 그 시점 sweep 40종 = 38 PASS + 2 FAIL(e2e 환경, recovery S13 간헐) → S13 이 본 항목.
- 결함(검증용 독립 확인): `viewer_frame_gate.cpp` `note_reference_sync` 첫 줄이 `resumeAnchorPending=false` 를 무조건 실행(c3c3d76)하는데 수신기(`viewer_video_receiver_frame.cpp` decode 성공 경로)는 **모든 decoded 프레임**에 이 함수를 호출 → 보유 재개 프레임이 decode 되는 순간 pending 이 풀려 뒤따르는 fresh burst 가 옛 floor 로 판정(sweep 0.2.102c 실패 로그 `.claude/test_sweep_fail_remote60_viewer_udp_recovery_test.log`: seq 29 decodeQueueLagUs 782,291, recvGap 1,319us, 요청 1). 단독 PASS 는 present 스레드가 burst 사이에 anchor 를 전진시킨 타이밍 덕분이라 판정 근거가 아님.
- 수정(Codex 확정안 R): 그 줄 삭제. pending 수명은 admit 에서만 결정(fresh anchor·8프레임/500ms 예산·상태 전환·키프레임 wait); fresh IDR 은 admit 이 이미 종료하고, held/미상 IDR 은 다른 held 프레임처럼 pending 유지. `admit` 주석·`viewer_frame_gate_state.hpp` 주석 정정. 1287883 의 sender 펜스·snapshot·dispatch 는 손대지 않음.
- 테스트 선행(`viewer_frame_gate_test.cpp`): Rig 에 수신기 순서 헬퍼(`feedDecoded` = admit→note_decode_ok→note_reference_sync(비키 포함)→clear_empty_streak, `decodedEmpty` = …→note_decode_empty, `decodeFailed`). T-P11a/T-P11b 를 이 헬퍼로 전환. 신규 (g) T-P11g 보유 프레임 비키 decode 뒤 pending 유지·첫 fresh anchor·lag<100ms·Congested 0·요청 0, (h) T-P11h 빈 출력 수락 뒤 동일, (i) T-P11i decode 실패 → reason 4 요청·Congested(decode_fail)·키 wait, 다음 admit 에서 pending 종료·DropCongested(기존 동작), (j) T-P11j fresh IDR 은 admit 에서 종료, held IDR 은 유지(Codex 확정 규칙), 그 뒤 fresh P anchor. **수정 전(1287883 코드) 실행 로그 `.claude/s13_prefix_frame_gate_1287883.log`: FAIL 36건(T-P11a/g/h/i/j·b)**, 수정 후 `.claude/s13_postfix_frame_gate.log`: PASS(전체).
- recovery S13 에 결정적 형상 (a′) 추가: 같은 old-real→fresh burst 를 `pinPresentAnchor` 로 present anchor 를 고정한 채 판정 — **수정 전 `.claude/s13_prefix_recovery_1287883.log`: (a′) transitions +3·요청 +1 (결정적 재현), 형상 0/1 은 타이밍상 PASS**; 수정 후 단독 ×3 PASS(`.claude/s13_postfix_recovery_run1..3.log`, 세 형상 모두 transitions 0·요청 0·heldResume 1/2/3).
- 전체 sweep(sweep 순서, 실패 로그 보존): 수정 후 전체 sweep ×2(`.claude/s13_postfix_sweep1.txt`, `s13_postfix_sweep2.txt`, 40종 각각) = **39 PASS + 1 FAIL ×2**, FAIL 은 두 번 모두 `udp_control_e2e_test` 뿐(환경, 아래). recovery(14 시나리오+S13 (a′)) 는 두 sweep 모두 PASS(sweep 부하 조건 재현 없음), frame_gate PASS. 실행 횟수: frame_gate 수정 전 1·수정 후 2(+sweep 2), recovery 수정 전 1·수정 후 단독 3·sweep 2 — 재시도로 덮은 실행 없음.
- 보존 회귀: `host_sender_epoch_test`·`host_input_target_rect_test`·`host_encode_epoch_test` PASS, recovery S14/S15 PASS(위 3회), frame_gate T-P11b(전부 held 는 예산 뒤 트립)·T-P11c(작은 hold+renderer 정체 트립) PASS. e2e(`udp_control_e2e_test`)는 환경 실패로 분리: `netstat -ano | findstr 43000` → UDP 0.0.0.0:43000 을 PID 13856 GNLinkStream.exe(설치된 라이브 호스트)가 점유, 라이브 호스트는 중지하지 않음.
- 보완(검증용 seq 1114 3번, 후속 test(viewer) 커밋): T-P11d printf·주석의 'decoded keyframe' 을 'fresh keyframe (anchored at admit)' 로 문구만 정정(동작 변경 없음 — 그 키프레임은 hold 1ms 의 fresh 프레임이라 admit 에서 anchor 되며 pending 이 끝난다; decode 는 pending 을 끝내지 않음). 우편함 지연으로 릴리스 커밋(#410) 뒤에 반영돼 검증용이 요청한 순서(테스트 커밋 → 릴리스 커밋)와 다름. 설치본은 1005ebe + 버전 bump 트리 빌드(제품 소스는 #410·이 커밋과 동일), 검사 요청 뒤라 재빌드는 검증용 회신 전 보류.
- 미검증: 사용자 실기(UAC 2회·로그 업로드·원점 변경). 설치·push·서버 배포 없음. 다음: 0.2.103 후보 빌드·설치본(#410).

### 410) 2026-09-07 0.2.103 후보 — S13 held-resume pending 수정 포함 릴리스 후보 빌드·설치본 (A2A task t-5ahw0t5x, 검증용 remote#1528xbin 검사 대기)
- 변경 파일: `product_version.hpp` 0.2.102→0.2.103(다른 버전 참조 없음: `grep 0.2.102` 결과 제품 소스는 이 파일뿐), `docs/history.md`, `docs/작업목록.md`(1.0/1.1/1.3 을 0.2.103 기준으로).
- 포함 커밋: 40b836e(정책 문서) · 1005ebe(S13 수정) 위에 이 커밋. 그 이전 0.2.102(ded9060)·재생성(1287883 트리) 설치본은 S13 결함을 포함하므로 후보에서 제외(설치된 적 없음).
- 빌드·산출물: 전체 Release 빌드 exit 0. `dist/GNLinkSetup-0.2.103.exe` — 3,466,240 bytes, sha256 c263758bba03c8c2f3a3e2e9e6eeae96649a8739b96526e352e4e8b0e4268daf, 임베드 버전 0.2.103(0.2.102 문자열 없음), 빌드 트리 = 1005ebe + 이 커밋의 product_version.hpp; 빌드 로그 `.claude/release_build_0.2.103.log`. 설치·push·서버 배포 없음.
- 검증: 릴리스 바이너리 sweep(`.claude/release_0.2.103_sweep.txt`) 40종 = **38 PASS + 2 FAIL** — (1) `udp_control_e2e_test` 환경(UDP 43000 = PID 13856 GNLinkStream.exe 라이브 호스트, 미중지), (2) `viewer_udp_recovery_test` **S12** 간헐 FAIL(`release_0.2.103_sweep_fail_remote60_viewer_udp_recovery_test.log`:162 `host.corrupted_keys()==1` — `SendFrame(forceKey)` 직후 검사하지만 비동기 인코더 호출에서는 IDR 이 그 호출에 안 나올 수 있어 corrupt 가 뒤의 pump 에서 일어남; 0.2.100 부터 있던 하네스 타이밍 문제, S13 수정과 무관, 이번 계획 범위 밖이라 **미수정·발견사항으로 보고**). S13 은 수정 후 모든 실행(단독 3·sweep 2·릴리스 sweep 1·릴리스 단독 1)에서 PASS. 릴리스 바이너리 단독 재실행 `.claude/release_0.2.103_recovery_standalone.log` PASS(특성 파악용, 상쇄 근거 아님). 앞선 수정 후 sweep ×2 는 #409.
- 상태: 검증용 Claude 의 "구현 OK" 와 Codex 최종 확인 전 — 완료 아님(task working). 미검증: 사용자 실기(호스트 재시작·재로그인 뒤 NAS 로그, 회사 클라 재로그인, UAC 2회, 정적 타이핑·영상 회귀, 원점 변경 배치; 다중 모니터 장비 없음).

### 411) 2026-09-07 검증용 NEEDS_CHANGES(경미) 반영 — T-P11d 문구(e08de14)·S12 하네스 발견사항 F-22 기록 (A2A task t-5ahw0t5x)
- 검증용 최종 검사(seq 1115 계열, 우편함 지연 수신): 40b836e/d980ff0/1005ebe/acd8233 diff 대조 OK, 검증용 직접 실행 frame_gate·recovery PASS, 설치본 c263758b… payload 6개 = Release 빌드 sha256 동일. NEEDS_CHANGES 1건 = T-P11d 문구(이미 **e08de14** test(viewer) 로 반영: printf·주석 "decoded keyframe" → "fresh keyframe (anchored at admit)", 동작 변경 없음).
- 이 커밋: `docs/뷰어_리팩터_발견사항.md` F-22 — S12 `viewer_udp_recovery_test.cpp:1294` 단정이 비동기 인코더와 경쟁(하네스, 제품 회귀 아님, 범위 밖·미수정, Codex 범위 판단 대기). `docs/history.md` #411. 후속 docs 커밋: 검증용 지적대로 단정 지점을 `:1294`(`corrupted_keys()==1`)·`:1302`(`goodIdr != 0`) 두 곳으로 정정 — 둘 다 `SendFrame(true)` 의 동기 송출 가정.
- 설치본: 제품 소스는 acd8233 과 동일(e08de14·이 커밋은 테스트/문서만) → `dist/GNLinkSetup-0.2.103.exe` 재생성 불필요, build-local 제품 바이너리 미변경. 테스트 exe(`remote60_viewer_frame_gate_test`)만 재빌드해 1회 실행(로그 `.claude/tp11d_frame_gate_e08de14.log`).
- 상태: task working 유지, 검증용 재검사 대기. 설치·push·서버 배포 없음.

### 412) 2026-09-07 S12 하네스 수정(F-22) — 강제 IDR 을 wire 에서 관측, 정확한 IDR seq 표시 단정, 단독 ×5 + sweep ×1 (A2A task t-5ahw0t5x, Codex 범위 포함·검증용 조건 a–f + 보충 3건)
- 결함: `viewer_udp_recovery_test.cpp` S12 의 두 단정(`:1294 corrupted_keys()==1`, `:1302 goodIdr != 0`)이 `SendFrame(true)` 가 강제 IDR 을 그 호출에서 동기 송출한다고 가정. 비동기 인코더(MFT hw)는 그 호출에서 이전 P 또는 AU 0개를 내고 IDR 은 다음 호출에서 나옴. **단독 실행에서도 재현**(검증용 `.claude/verify_recovery_release_run3.log:166` :1302, 단독 실패율 1/3) — sweep 부하 전용이 아님. 릴리스 sweep 재현 `.claude/release_0.2.103_sweep_fail_remote60_viewer_udp_recovery_test.log:162` :1294. 제품 회귀 아님(1005ebe 는 pending 규칙만, S12 는 held 프레임 없음). 수정 전 로그 모두 보존.
- 수정(테스트 전용, 제품 소스·설치본 불변): FakeHost `last_key_seq()`(마지막 key AU 의 wire seq); 헬퍼 `send_key_and_wait_on_wire` = `SendFrame(true)` 정확히 1회 뒤 `SendFrame(false)` 를 최대 30회·1500 ms 안에서 반복하며 `keyframes_sent` 증가(=그 IDR 이 wire 에 나감) 순간 중단, 상한 초과 시 ok=false → FAIL; 두 번째 강제 키는 만들지 않음(손상 주입 정확히 1회, "다음 정상 IDR" 도 1개). 손상 IDR: `zeroed.ok` + `corrupted_keys()==1`(단정 삭제·완화 없음, 시점만 관측 뒤로). 정상 IDR: `goodIdr = good.keySeq`(관측한 실제 seq, 호출 반환값 아님) `!= 0`, 송신 뒤 `corrupted_keys()==1` 유지(무손상), rig 에 key 프레임 publish 집합(`publishedKeySeqs`/`published_key(seq)`, present 스레드 poll 이 놓친 version 수 `missedPublishes` 진단)을 두어 **그 seq 의 key 프레임이 publish 됨**을 단정(뒤 P 프레임이나 0 sentinel 로 통과 불가) + published 증가(비공백 출력). 무폭주(≤ before+2)·비정지·Normal 단정 유지, 끝에 `corrupted_keys()==1`(정확히 1개). 비동기 순서는 `S12 order (…): first call emitted P|nothing|the IDR seq=…; IDR seq=… on call k` 로그로 관측만(동기 인코더는 call 1 통과).
- 검증(테스트 exe 만 재빌드 `--target remote60_viewer_udp_recovery_test`, 제품 바이너리 5종 sha256 전후 동일 `.claude/s12_product_bins_before/after.txt`, 설치본 c263758b… 미재생성): 최종 하네스 단독 ×5 `.claude/s12_postfix2_recovery_run1..5.log` **5/5 PASS**(15 시나리오 전부, S13 세 형상·S14·S15 포함) — 비동기 순서 관측: run1 "zeroed IDR: first call emitted P seq=21; IDR seq=23 on call 3 (38 ms)", "good IDR: first call emitted P seq=67; IDR seq=68 on call 2", run5 zeroed/good 모두 call 2(20 ms) — 즉 옛 단정이 깨지던 바로 그 경쟁(첫 호출이 P 를 냄)이 2/5 회 발생했고 새 관측으로 통과, run2~4 는 동기(call 1). 앞선 중간본(정확 seq 단정 전) 단독 ×5 `.claude/s12_postfix_recovery_run1..5.log` 5/5 PASS(모두 call 1)·sweep `.claude/s12_postfix_sweep1.txt` 는 참고용. 전체 sweep ×1(실패가 났던 순서, `.claude/edits/run_all_tests.sh`) `.claude/s12_postfix2_sweep1.txt` 40종 = **38 PASS + 2 FAIL**, recovery PASS(42 s). FAIL 2건은 환경으로 분리: (1) `udp_control_e2e_test` — UDP 43000 을 라이브 호스트 GNLinkStream.exe PID 13856 이 점유(netstat), 중지하지 않음; (2) `gdi_capture_process_test` — GDI_DELIVERED_FPS 3.67, 캡처 복사 평균 258 ms(`.claude/s12_postfix2_sweep1_fail_remote60_gdi_capture_process_test.log`), 콘솔 세션(RDP 아님, qwinsta 확인)에서 GDI BitBlt 가 느린 환경 상태로 이 테스트 변경(뷰어 recovery 하네스)과 무관 — 같은 날 앞선 sweep(#409 ×2)에서는 PASS 였고 S12 sweep 2회에서 연속 FAIL, 원인 미조사(범위 밖). 재시도로 덮은 실행 없음.
- 상태: task working 유지, 검증용 재검사 → Codex 최종 OK 대기. 설치·push·서버 배포 없음.

### 413) 2026-09-08 Codex 직접 재검토 문서화 — 0.2.103 UAC/정적 지연/고화질 UDP 단절
- 목표: 직접 확인한 수정 필요 사항을 추적 문서로 남기고, 사용자 실기(09:57 정적, 09:58:25 UAC, 09:59:10 영상·재접속 정지)를 NAS 로그와 코드 실행 경로로 조사한다. 사용자 정정에 따라 Claude 위임을 중단한 뒤 Codex가 직접 조사·문서화했다(이번 작업의 명시적 사용자 지시, 일반 역할 정책 영구 변경 아님).
- 변경: `docs/stabilization_audit_2026-09-08.md` 신규 — A01~A17 수정/설계·기존/조건부 항목, 직접 코드 위치·증거 종류·우선순위·완료 기준·테스트 공백. `docs/field_test_2026-09-08_0.2.103.md` 신규 — NAS 사본 해시/출처, seq/gen 타임라인, UAC·정적·영상·재접속 판정. `docs/구현계획.md`와 `docs/작업목록.md`는 현재 체크리스트/상태 갱신.
- 직접 확인: UAC 실제 전환09:58:48~51, old epoch8 P를 폐기하고 current epoch9 IDR 수락, 뷰어 해당 구간 congestion/stale0. 고화질 첫 정지는 gen2 seq1392 이후 UDP 유입이 끊기고 수신 루프는 계속 도는 link-silent(stage=recv, datagramAge3.14s) 뒤 peer-lost. 호스트는 후속 P/IDR 송신을 계속했다. gen3 재접속 뒤 재발, gen4는 큰 RTT/손실 후 진행·10:00:07 혼잡에서 약0.5초 뒤 정상 복귀. Normal 무후속 NACK 소진의 복구 진입 누락, 단절 약5초 뒤 ABR 하향, 재접속마다12Mbps/60Mbps pacing 복귀 코드 확인. 최초 UDP 드롭 위치·장비는 미확정이다.
- 지연 판정: 정적 host AU capture→입큐 전 송신 준비 표본102~185ms와 trailing kick150ms 정책, UAC 후 readback 평균약8ms 확인. 입력 종단 지연은 미측정. raw cross-PC QPC 차감으로 `netUs/totalUs=0`이 되는 계측 오류를 추가 기록했다. 선택된 wire/경고 표본을 전체 FPS·총지연으로 합산하지 않았다.
- 검증: Codex의 NAS SSH 원본 조회, 로컬 NAS 사본 사건/누적 카운터/해시 계산, 소스 분기·호출자 대조, 문서 참조/수치/diff 검사. 기존 401 spin 프로브(3초 CPU2.91초·diag2,372,858B)와 assembler/FIFO 모델은 검증용 실행 결과를 Codex가 직접 읽어 확인한 근거로 구분. 이번 제품 빌드·테스트·설치본 재생성은 없음. 제품/서버 설정·라이브 프로세스 변경 없음, push 없음.
- 다음: A01 업로더 spin, A02/A06 provenance, A03~A05 NACK 복구 계약을 수정·검증하고 단절 대응/재접속 초기 전송 정책을 설계한다. 최초 유입 중단 지점은 동일 seq의 양단 NIC 캡처로 구분해야 한다. 문서화 완료를 제품 안정화 완료로 표시하지 않는다.


### 414) 2026-09-08 중단된 멀티에이전트 전수조사 이슈 재확인·추적 원장
- 목표: 사용자의 “아까 전수조사에서 나온 이슈 확인” 요청에 따라 중단 당시 초안과 프로브 출력을 Codex가 직접 대조한다. 에이전트 재가동·새 제품 구현 없음.
- 변경: `docs/full_code_audit_2026-09-08.md` 신규 — host12/viewer14/server9/Android 계정5/native directory3의 초안43건과 메인 설치·입력 검토8건을 분류·보존. 총51개 검토 항목은 신규 실기 재현 버그51개라는 뜻이 아니며, 기존 D3/Android 후속 공백·옵션/오류 조건·설계 개선을 구분했다. `docs/구현계획.md`는 체크리스트 상태만 갱신.
- 직접 확인: 마지막 content frame cadence 거절 뒤 old cache 재송출(HN01), 생성 AU가 후속실패의 bool false 때문에 버려짐(HN04), restart/encoder 실패를 잃는 상태전환(HN05/06), 명시 창 실패의 다른화면 fallback(HN08), decoder 출력 metadata에 현재 입력 header를 붙이는 문제(V06), control-only traffic에서 recovery maintenance 생략(V14), 서버 credential backoff/로그 namespace/저장 성공 계약 등. 09:59 첫 UDP 단절의 물리적 원인으로 귀속하지 않았다.
- 검증: 이전 격리 산출물 result.txt/codec_partial_result.txt/server_audit_probe.out 직접 열람 및 제품 소스·호출자 대조. codec partial-output 프로브는 실제 codec+fake transform이며 실제 HW MFT 재현이 아님을 정정했다. 새 빌드/제품 테스트/실기/설치/서버 부하/배포 없음. 문서의 항목 수·링크·UTF-8·diff 범위 검사, Git MCP 문서 커밋. 기존 .claude 초안·프로브 보존, push 없음.
- 다음: latest-content 보존 → EncoderResult/provenance/reference-chain → transactional capture/encoder restart → common ReceiveMaintenance/renderer recovery → auth/storage/install 계약 순으로 설계를 확정하고 해당 실패 조건 회귀를 수행한다. 검토 완료와 제품 수정 완료를 구분한다.

### 415) 2026-09-08 11:27 고화질 정지 — 호스트 UDP 패킷 캡처 1차 회차 분석·한계 기록 (A2A task t-gmwzjgy1, Codex t-2z9fjuqu 마무리, 검증용 remote#865h2pvp 변경 없이 OK)
- 목표: 0.2.103 실기 중 11:27 고화질 영상 정지(재접속 후 정상)를 호스트 NIC 패킷 캡처와 NAS 로그로 대조한 결과와 판정 범위를 기록한다. 제품 코드·설정 변경, 2차 캡처, 설치, push, 서버 배포 없음(문서만).
- 근거 원본(모두 `.claude/` 미추적, 저장소에 넣지 않음): 검증용 최종 노트 `.claude/packet-capture-20260908/verify_notes.md` §1~§7(Codex 정정 반영본), 작업용 `report_1127.md`·`capture_coverage.md`·`pcap_freeze_window_v2.txt`(검증용 지적대로 정정한 뒤 인용: streamGeneration 은 헤더 @48..55 u64 — 이전 파일은 @44 에서 읽어 하위 32비트가 chunkStride 1112 였음; "중복 6,024 = 재송출" 은 FEC 패리티(flags 0x10) 의 chunkIndex 가 그룹 번호라 data 인덱스와 충돌한 오류 — 분리하면 data+parity 6,020 / data+data 4, 실제 재전송은 gen6 seq 40359 idx 27..56 30건; 50.6% 는 전체 NIC 누락률로 쓰지 않음), NAS 사본 `.claude/field-20260908-1127/`(viewer/client/host.log + 디렉터리 서버 저널, sha256 기록). 캡처 절차 `capture_plan.md`·`start_output.txt`.
- 회차 식별: 2026-09-08 11:24:50.7~11:31:41.7 KST, 관리자 pktmon(사용자 UAC 승인, `--comp nics --pkt-size 192 --file-size 128 --log-mode circular`), 필터 UDP 43000 + IP 211.218.222.1(양방향), snaplen 192 B, 128 MB 순환 미도달(연속 보존), 106,256 패킷·etl2pcap 삭제 0·ETL 손실 이벤트 0. `gnlink_host_20260908_112450.etl` 10,681,934 B sha256 46e9460c64c9090dc0d9f3cab406002a9935247f70d10e4e2464005cdb6b9c25 / `.pcapng` 23,680,496 B sha256 895b4d76d5f8affcc9166c7d34f6ab3b706d3ccf5885de1830e74c0409158ba7. 종료는 사용자 11:27 정지 통지 뒤 stop.request 로 정상 종료(자기 etl 확인).
- 검증된 경계(확정): 시계는 같은 AU 의 pcap 마지막 청크 vs 뷰어 assembly 벽시계 n=78, p50 −374 ms → 뷰어 ≈ 호스트 −375 ms. 뷰어 마지막 수신 11:27:36.314(뷰어 시계) ≈ 호스트 11:27:36.689 = pcap gen6 seq 40359 idx 27 송신(11:27:36.688) → 순방향(호스트→뷰어) 손실 시작. 호스트는 NDIS 계층(pktmon)에서 구 peer 211.218.222.1:60698 로 11:27:43.808(seq 40769)까지 송신 관측(관측 하한, NIC 하드웨어 밖 보장 아님). 뷰어 NACK 3건(11:27:36.813/.843/.874, missing 27..56) 호스트 NIC 도착 → round 0 직후 idx 27..56 30/30 재전송 관측; round 1·2 응답은 캡처에 없으나 캡처 불완전 탓에 판정 불가. 역방향 ControlData 11:27:37~40 초당 2~4개 도달. 뷰어 11:27:40.580 `udp-tunnel closed reason=shutdown`(뷰어 측 종료) 은 호스트 11:27:43.793 peer-lost 와 별개 사건. 11:27:44.622 새 소스 포트 56584 Hello → epoch 6 → 11:27:46 gen7 IDR(seq 40770) → 정상. 호스트 앱 wire seq 로그와 pcap seq 범위는 초 단위로 일치.
- 한계·미확정: 캡처는 호스트 송신의 일부만 담음 — 같은 gen+seq 대조로 입증(뷰어가 fecRecovered=0 으로 완성한 AU 120개 중 pcap 불완전 83개, 그중 패킷 0개 약 40개); 누락 원인 미확정(드라이버/오프로드 단정 금지) → AU 단위 송신 완전성·NACK round 1·2 응답·pcap seq 공백(461건)의 송신자 귀속은 판정 불가. H>P HelloAck(301) 미캡처(같은 미디어 소켓 송신, 누락 부류로 추정·확정 아님). 물리 드롭 위치(공유기/ISP/회사 NAT/뷰어 PC 수신)는 호스트 단독 캡처로 미확정. 직전 11:27:36 송출 급증(캡처 하한 1,554 청크/s, IDR 40367)과의 인과 미확정. 초별 표는 열마다 시계가 달라 동시 구간으로 읽지 않음.
- 핵심 결론(Codex 합의 문구): "호스트 송신·요청 30청크 재전송 관측, 뷰어 무수신, 역방향 도달, 새 port 재접속 회복; 물리 드롭 위치는 미확정."
- 변경 파일: `docs/history.md`(이 항목), `docs/구현계획.md`(0.2.103 실기 항목 상태만), `docs/작업목록.md`(1.0 아래 한 줄 1.0.1), `docs/field_test_2026-09-08_0.2.103.md`(9절 "11:27 캡처 후속" ①~⑥, 후속 커밋). 2차 실행용 helper(`Start/Stop-GnlinkCapture.ps1`, chcp 65001·`.etl` 줄 판별, 검증용 재검토 통과)는 `.claude/` 에 준비만, 실행 없음.
- 다음: 2차 캡처 방법(구성 요소 id 지정·`pktmon counters --drop-reason`·Npcap 등)은 Codex 결정. 뷰어 측 캡처 없이는 드롭 위치 확정 불가.
- 보정(후속 docs(field) 커밋, 검증용 seq 1261 의 Codex 3파일 배치 지시가 fd1f220 뒤 도착): `docs/field_test_2026-09-08_0.2.103.md` 9절 "11:27 캡처 후속"(①캡처 식별 ②경계 ③NACK/재전송 ④종료 사건 분리 ⑤회복 ⑥한계·정정 기록) 추가, `docs/작업목록.md` 1.0.1 줄은 4a92d5e 에서 제거했다가 검증용 정정(작업은 작업목록에 한 줄 규칙, 3파일은 배치 지정이지 변경 금지 아님)에 따라 다음 커밋에서 복원. 위 본문의 "50.6%"·"드라이버" 언급은 사용 금지·단정 금지를 적은 것이지 손실률·원인 판정이 아님.

### 416) 2026-09-08 A01 로그 업로더 401 일시정지 busy loop 수정 — 송신 가능할 때만 깨어나는 대기 조건, 시간 기준 idle-alive, workerCycles (A2A task t-uhqkw7h9, 검증용 remote#865h2pvp 위임, Codex 계획 t-thjz1nw2)
- 결함(감사 U3/프로브 Q1, 실측 재현): `log_upload.cpp` worker 의 `cv.wait_for` 조건이 `queuedBytes >= batchMaxBytes` 를 authRejected/credentials 와 무관하게 참으로 두어, 401 일시정지 중 큐가 192 KiB 를 넘으면 `next_job_locked` 가 false 를 내도 즉시 재진입 → 코어 점유 + 30 사이클마다 `idle alive` diag 기록. 이번 회귀 [10] 수정 전 실측(`.claude/a01_prefix_log_upload_test.log`): 2 s 동안 workerCycles +364,024, diag +1,001,105 B, 프로세스 CPU 1,984 ms.
- 수정: 대기 조건을 `stopping || wakeSeq != seen || (credentials && !authRejected && queuedBytes >= batchMaxBytes)` 로 — 보낼 수 없는 상태의 가득 찬 큐는 깨우지 않고 flushInterval 만큼 잔다(next_job false 인 사이클은 그대로 timed wait 로 복귀, 즉시 continue 없음). configure/clear 의 wakeSeq 와 stop 은 즉시 반영 유지. `idle alive` diag 는 사이클 수가 아니라 시간 기준(60 s 당 최대 1회). 불변: ownerEpoch·configGeneration 펜싱, 늦은 401/500 격리, 재시도 3회/2·4 s/60 s, 보관 4개, 큐 4 MB.
- `LogUploadStatus.workerCycles`(루프 반복 수, 내부 API·테스트 seam) 추가; stop 에서 0 으로.
- 회귀 `log_upload_test.cpp` [10]: 제품 기본값(batchMaxBytes 192 KiB, flush 2000 ms, 큐 4 MB)으로 401 pause → 99 B 줄 2,100개(≈205 KiB) 적재 → 2 s 창에서 workerCycles 증가 ≤ 5, diag 증가 ≤ 1 KiB, 프로세스 CPU < 500 ms, 요청 수 1 유지, 드롭 0 → 새 토큰 configure → 보관 배치(STALE→FRESH 헤더) 먼저, 이어 큐가 배치 단위로 배출(3번째 요청 본문 ≥ 100 KB), authRejected 해제·held 0. 수정 후(`.claude/a01_postfix_log_upload_test.log`): workerCycles +1, diag +0 B, CPU 0 ms, 전체 PASS([1]~[9] 유지). 단독 실행 3회 PASS.
- 변경 파일: `apps/native_poc/src/log_upload.hpp`, `log_upload.cpp`, `log_upload_test.cpp`, `docs/history.md`, `docs/구현계획.md`(A01 체크), `docs/stabilization_audit_2026-09-08.md`(A01 체크), `docs/작업목록.md`. 설치본·설치·push 없음(릴리스는 A02 뒤 한 번).
- 다음: V14+A04 수신 recovery(같은 task, 별도 커밋).

### 417) 2026-09-08 V14+A04 Windows 수신 recovery — 모든 경로가 지나는 시간 maintenance, 후속 없는 미완성 head 의 유계 포기(Codex 확정 규칙) (A2A task t-uhqkw7h9, 검증용 remote#865h2pvp 위임, Codex 계획 t-thjz1nw2 + 보완)
- 결함: (V14) `viewer_video_receiver.cpp` run_udp 의 시간 작업(hold drain·NACK 라운드·keyframe 복구 타이머)이 영상 청크 뒤와 recv 타임아웃 경로에서만 실행돼, 제어 응답·커서·malformed/ignored datagram 이 `continue` 로 빠지는 링크(제어 패킷이 25 ms 안에 계속 오면 recv 가 타임아웃되지 않음)에서는 타이머가 굶음. (A04/N3) Normal 상태에서 어셈블러 미완성 head 뒤에 완료 AU 가 없으면 PopDelivery 는 아무것도 하지 않고 NACK 스케줄러는 라운드 소진 뒤 조용히 멈춰, 정적 화면의 마지막 AU 청크 손실이 옛 그림으로 남음(secure desktop 중에는 후속 AU 보장 없음). 원본 코드 재현 `.claude/v14_prefix_recovery.log`: S16 타이머 재요청 0회, S17/S18 NACK 3라운드 뒤 keyframe 요청 +0·waitForKey 0.
- 수정 1 (V14, `viewer_video_receiver.cpp`): 루프 상단 공통 `maintenance(nowUs, force)` = `drain_deliveries` → `maybe_send_nack` → (N3 판정) → `fg.tick`; 모든 경로(video chunk·control 304/305/306·cursor·malformed/ignored/short·continue·recv 타임아웃)가 통과, elapsed(qpc) 5 ms rate-limit(제어 ACK 가 5 ms 마다 와도 5 ms 마다 실행), 타임아웃 경로는 force. 하단 중복 `maybe_send_nack`/`fg.tick` 제거(완료 AU 즉시 배달용 `drain_deliveries` 유지).
- 수정 2 (A04/N3, Codex 확정 규칙 — 후속 완료 AU 가 없는 미완성 head 에만, A03 의 hold 120 ms 정책 불변): 포기 = ① 스케줄러가 그 AU 의 마지막 적용 단계까지 소진(`spent_for(hasTail)`: tail 있으면 tail 3라운드, 없으면 hole 3라운드 — hole 소진 뒤 tail 단계가 남으면 미소진) ∧ ② now ≥ 마지막 실제 NACK 송신 + replyAllowance ∧ ③ now ≥ lastProgressUs + replyAllowance(진행 = 그 AU 의 새 data chunk 또는 FEC 복구 chunk; 중복 chunk·control·다른 AU 는 진행 아님) ∧ ④ now ≥ firstPacketUs + terminalMin. 별도 ⑤ now ≥ firstPacketUs + hardCap 이면 진행과 무관하게 포기(1 episode, 기존 리미터·backoff). replyAllowance = 제어 스레드의 최근 RTT(≤ 5 s 신선)가 있으면 max(50 ms, 2×RTT) 를 상한 1 s 로 clamp, 없으면 50 ms; terminalMin = tailGrace + maxRounds×round + replyAllowance(기본값 120+75+50 = 245 ms, 고정 아님); hardCap 기본 5 s(env `REMOTE60_NATIVE_VIDEO_GIVEUP_HARDCAP_MS`, 상한 env `REMOTE60_NATIVE_VIDEO_REPLY_ALLOWANCE_MAX_MS`, terminalMin 보다 작아지지 않게 정규화). NACK 미협상/비활성(스케줄러가 그 AU 를 쫓지 않음)이면 ③+④ 로 유계 폴백(legacy 즉시 배달 모드 포함). waitForKey 중 비-key head 는 ③ 만으로 포기하되 새 요청/reset 은 head 마다 반복하지 않음(기존 alreadyWaiting 억제). 포기 = `GiveUpIncomplete(gen, seq)` 로 판정에 쓴 동일 (gen,seq) 만 제거(완료됐으면 취소) → 스케줄러 Reset → 기존 `handle_udp_discontinuity()`(waitForKeyFrame + decoder reset + request_keyframe(2), 리미터·500 ms→2 s backoff). 로그 `stuck head given up …` 에 reason(hard-cap / nack-spent / no-progress / no-progress-nack-off / no-progress-during-key-wait)·ageUs·sinceProgressUs·hasTail·chased·rttUs·replyAllowanceUs·terminalMinUs·hardCapUs 명시.
- 공유 코드: `native_video_client_shared_core.hpp/.cpp` — Assembly·IncompleteAuInfo 에 `lastProgressUs`(생성·새 data chunk·FEC 복구 시 갱신, 중복은 미갱신), `AnyComplete()`, `GiveUpIncomplete(generation, seq)`(정확 identity, 완료 시 false). `udp_video_nack.hpp` — `current_generation()`·`spent_for(hasTail)`·`last_sent_us()`. `viewer_control_state.hpp`/`viewer_control_client.cpp` — pong 마다 `lastRttUs`·`lastRttAtUs` atomic(제어 스레드 write, recv read). `viewer_video_receiver.hpp` NackOptions 에 replyAllowanceMin/Max·rttStale·giveUpHardCap, `viewer_startup.cpp` env 연결. Android(hold 0, 자체 루프) 불변, wire 불변.
- 회귀(`viewer_udp_recovery_test.cpp`, 제품 run_udp 경로; `native_video_client_shared_core_test.cpp`): S16 keyframe wait + 5 ms 제어 노이즈(FakeHost `SendNoise`, `idle_with_noise`) → 타이머 재요청 2~4회 → 회복(S8 = 완전 무수신 타임아웃 경로 회귀). S17/S18 정적 화면 마지막 P/IDR 꼬리 손실 + NACK 무응답 + 후속 없음(`LossPlan::stopAfterDrop`/`ResumeAfterDrop`) → tail 3라운드 → nack-spent 포기(≈245 ms)·keyframe 요청(+1, 타이머 재요청 +1 허용) → IDR 로 회복(정확한 IDR seq 의 key publish). S19 꼬리 손실 + NACK 응답 200 ms 지연(`nackAnswerDelayMs`) + 제어 RTT 150 ms 주입(`SetControlRtt`) → replyAllowance 300 ms 로 늦은 응답 수락, 요청 0. S20 hole(idx 1)+tail 동시 손실 무응답 → 150 ms 시점 미포기(hole 소진만으로 안 끝남), tail 3라운드 뒤 폴백 1회(NACK 4~6). S21 NACK 미협상 정적 꼬리 손실 → no-progress+terminalMin 폴백·회복. S22 IDR 을 30 ms/청크(1.2 s) 페이싱·NACK 무응답 → 진행이 유지돼 포기 0·완성·표시. S23 hardCap 1 s(rig 튜너블) + 50 ms/청크 2 s IDR → cap 에서 1 episode 포기 → 새 IDR 로 회복. shared_core: lastProgressUs 는 새 chunk 에만 갱신(중복 무시), GiveUpIncomplete 는 정확한 seq 만 제거·미보유 false·완료 시 취소. S1~S15·S2(구 peer)·S5(재정렬) 유지. 수정 전(임시 규칙 빌드) `.claude/n3_prerule_recovery.log`: S19 202 ms 포기(응답 320 ms 놓침)·S20 101 ms 포기·S21 폴백 없음·S22 235 ms 포기 → 11 FAIL; 수정 후 단독 ×5 `.claude/n3_rule_recovery_run1..5.log` + shared_core `.claude/n3_rule_shared_core.log`: 아래 결과.
- 전체 sweep: 확정 규칙 기준 전체 Release 빌드 exit 0(오류 0, `.claude/n3_full_build.log`) 뒤 40종 sweep — `.claude/v14_a04_sweep3.txt` **39 PASS + 1 FAIL**(FAIL = `udp_control_e2e_test`, UDP 43000 을 라이브 GNLinkStream PID 18844 가 점유하는 환경 실패, 중지하지 않음). recovery 60 s PASS. 앞선 `v14_a04_sweep2.txt` 는 38+2 였고 두 번째 FAIL 은 **하네스 문제가 아니라 제품 결함이 드러난 것**이었다(#419: 포기한 AU 의 늦은 청크가 조립을 재생성해 같은 head 를 2~3회 포기시키고 뒤의 정상 IDR 배달을 막았다). 당시 함께 넣은 복구 단정 완화 → 복구 계열 단정을 `published_key_at_or_after(seq)`(그 IDR 또는 그 이후의 **키프레임** 표시, P·0 sentinel 불가)로 고치고 S23 대기 6 s 로 연장, S22 의 "그 느린 IDR 자신이 완성" 단정만 정확 seq 유지. 그 뒤 단독 ×3 PASS. 실행 횟수: recovery 수정 전 1(11 FAIL)·확정 규칙 후 단독 5+3·sweep 2, shared_core 수정 전 1·후 2, 전체 빌드 2회. RDP 세션 없음(qwinsta 콘솔).
- 결론 범위: 제어 채널 블록이나 경로 blackhole 을 해결했다는 주장 아님 — 제어만 오는 링크에서 복구 타이머가 굶지 않고, 후속 없는 정적 손실이 규칙대로 유계로 IDR 경로에 들어간다는 것까지. 검증용 기준선 `.claude/probe/probe_nack_trace.cpp` A04-1~4 대조는 검증용 몫.
- 변경 파일: `apps/native_poc/src/viewer_video_receiver.cpp`, `viewer_video_receiver.hpp`, `viewer_startup.cpp`, `viewer_control_state.hpp`, `viewer_control_client.cpp`, `native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `udp_video_nack.hpp`, `viewer_udp_recovery_test.cpp`, `native_video_client_shared_core_test.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/stabilization_audit_2026-09-08.md`(A04 상태), `docs/작업목록.md`(1.5). 설치본·설치·push 없음(릴리스는 A02 뒤 한 번).
- 다음: A02+HN07+A06(encoder provenance, Codex 확정 세부) — 같은 task, fix(host) 커밋. A03(hold/tail 재설계)·A05 는 범위 밖.

### 418) 2026-09-08 A02+A06+HN07 인코더 provenance — 입력 FIFO 통합, overflow 무효 래치와 재동기, unknown 출력에서만 체인 재닫힘 (A2A task t-uhqkw7h9, 검증용 remote#srb0qig4 위임, Codex 확정 세부)
- 결함: (A02/F1) `mf_h264_codec.cpp` 의 세 병렬 deque(타임스탬프·synthetic·epoch) 중 "바이트 없는 출력" 경로가 앞 둘만 pop 해 그 뒤 모든 AU 가 이전 입력의 epoch 를 달고 나옴 → flush 가 강제한 IDR 이 old 로 판정돼 drop-old-epoch, 뒤 P 는 drop-awaiting-key(감사 F1). (A06/C2) FIFO overflow 시 가장 오래된 항목을 버리는 trim 은 뒤 출력에 **더 새 입력의** provenance(=epoch)를 붙여, pre-flush 픽처가 current key 로 게이트를 열 수 있음. (HN07) 게이트가 열린 뒤 provenance 없는 출력이 나와도 체인이 유지돼 뷰어가 갖지 못한 참조를 쓰는 P 가 이어질 수 있음.
- 수정 1 (codec): 세 deque → `struct PendingInput{tsHns, synthetic, epoch}` + `class PendingInputFifo`(mf_h264_codec.hpp 공개, MFT 없이 검증 가능). push·빈 출력 pop·정상 pop·initialize/shutdown 이 한 레코드를 함께 움직여 lockstep 을 구조로 강제. overflow(64) 는 trim 대신 **provenanceInvalid 래치 + FIFO clear**: 이후 모든 AU 는 epoch 0(unknown)으로 나가고, 래치는 `initialize()` 성공에서만 해제(`shutdown()` 의 `Clear()` 는 큐만 비움 — 재초기화가 끝나야 MFT 가 쥔 옛 출력도 사라지므로). `provenance_invalid()` 접근자, `H264EncodeFrameStats.provenanceInvalid` 노출.
- 수정 2 (gate, `host_epoch_gate.hpp`): `EpochGate.provenanceInvalid` — 세워지면 열린 체인을 즉시 닫고, 겉보기 current key 도 수락하지 않으며 모든 출력을 DropUnknownEpoch 로 폐기(기존 bound → ResetEncoder 는 유지). HN07: 게이트가 열린 뒤 **unknown(0)/future** 폐기가 나면 awaitingKey 재진입(`reclosedByUnknown`) + 호출자 forceKeyNext, **DropOldEpoch 는 체인을 닫지 않음**(provenance 가 확실하므로 불필요한 IDR 방지). `epoch_gate_take_reset_budget()` 로 재빌드 예산(3회/10 s)을 request_reset 과 공유.
- 수정 3 (stage): `EncoderState::TryProvenanceResync(capture, nowUs)` = 예산 확보 → shutdown → initialize → 실패면 pending·래치 유지(카운터), 성공이면 래치 해제·타임라인 앵커/starvation 리셋·forceKeyNext·`epoch_gate_note_reset`. `host_stage_encode_send_h264.cpp` 는 **encode 반환 직후, AU 배치 루프 밖**(units 가 비어도)에서 `provenance_invalid()` 를 보고 게이트를 닫고 재동기를 시도하며, 성공 시 그 호출의 units 를 버림. 예산 소진·초기화 실패로 미룬 요청은 `host_stage_time_limit.cpp` 의 매 tick 에서 재시도(정적 화면이라 새 프레임이 없어도 진행, 성공 시 kick 재무장). `host_stage_encode_send_h264_au.cpp` 는 DropUnknownEpoch 에서 forceKeyNext 를 세우고, 유일한 enqueue 지점에 provenanceInvalid 명시 차단을 둔다(게이트의 epoch 0 규칙과 이중).
- 회귀 `host_encode_epoch_test` [4](결정론 — 제품 `PendingInputFifo`·제품 `epoch_gate_judge`·제품 `TryProvenanceResync` 를 직접 구동, 비동기 MFT 타이밍 불개입; 테스트 타깃에 capture 라이브러리 링크): a 빈 출력 1회 뒤 flush → 새 IDR AcceptKey(pre-A02 는 epoch 를 남겨 drop-old-epoch); b/c/f overflow → 래치·FIFO 비움·Pop 실패(=epoch 0)·게이트가 current key 까지 거부, `Clear()` 는 래치 유지·`Reset()` 이 해제; d 예산 3회/창·4번째 거부·다음 창 허용; e initialize 실패 → pending·래치·실패 카운터 유지; g 성공 → 래치 해제·forceKey·awaitingKey·새 epoch IDR 수락·P 재개; h unknown/future 는 열린 체인을 닫고 다음 P DropAwaitingKey→IDR 로 복구, known-old 는 체인 유지·다음 P Emit. 기존 [2b] 를 HN07 계약으로 갱신. 단독 ×3 PASS([1][2][2b][3][4]).
- 전체 검증: 전체 Release 빌드 exit 0(오류 0, `.claude/a02_full_build.log`), sweep 40종 `.claude/a02_sweep1.txt` **39 PASS + 1 FAIL** — FAIL 은 `udp_control_e2e_test` 뿐(UDP 43000 을 라이브 GNLinkStream PID 18844 가 점유하는 환경, 중지하지 않음). `host_encode_epoch_test` 단독 ×3 PASS(`.claude/a02_epoch_test_run1..3.log`), 수정 전 상태는 [2b] 가 HN07 이전 계약(unknown 폐기 뒤에도 체인 유지)으로 PASS 하던 것이라 별도 FAIL 로그 없음 — 새 [4] 는 수정 전 코드에서는 컴파일 자체가 되지 않음(PendingInputFifo·provenanceInvalid·TryProvenanceResync 가 없음). host_sender_epoch_test·recovery S1~S23·frame_gate 등 나머지 회귀 유지. 실행 횟수: epoch_test 3, 전체 빌드 1, sweep 1. RDP 세션 없음(qwinsta 콘솔).
- 한계: 실제 MFT 에서 FIFO overflow(출력 없이 입력 64개)를 강제할 수단이 없어 [4]는 제품 FIFO·게이트·재동기 객체를 직접 구동한다. 즉 "현장에서 overflow 가 일어난다"는 재현이 아니라 "일어나면 이렇게 처리된다"는 계약 회귀다. 실기 관측용으로 stats 의 `pendingOverflow`·새 로그(`encoder provenance invalid …`, `provenance resync ok/deferred`)를 남겼다.
- 변경 파일: `apps/native_poc/src/mf_h264_codec.hpp`, `mf_h264_codec.cpp`, `host_epoch_gate.hpp`, `host_encoder_manager.hpp`, `host_stage_encode_send_h264.cpp`, `host_stage_encode_send_h264_au.cpp`, `host_stage_time_limit.cpp`, `host_encode_epoch_test.cpp`, `apps/native_poc/CMakeLists.txt`, `docs/history.md`, `docs/구현계획.md`, `docs/stabilization_audit_2026-09-08.md`, `docs/full_code_audit_2026-09-08.md`, `docs/작업목록.md`. 설치·push·서버 배포 없음.
- 다음: 세 항목(A01 #416, V14+A04 #417, A02/A06/HN07 #418) 완료 → 검증용 검사 뒤 0.2.104 릴리스 빌드·설치본 1회.

### 419) 2026-09-08 A04 후속 — 포기한 AU 의 늦은 청크가 head 를 재차단하던 제품 결함 (검증용 remote#srb0qig4 NEEDS_CHANGES, Codex 구현 주의 4건)
- 결함(검증용 지적, 제 오진 정정): #417 의 `GiveUpIncomplete` 는 조립만 지우고 그 identity 를 남기지 않아, 호스트가 계속 보내던 그 AU 의 늦은 data/FEC 청크가 `PushDatagram` 의 stale 가드(`native_video_client_shared_core.cpp` 의 `deliveredAny_ && !sequence_is_newer(...)`)를 통과해 **같은 seq 의 조립을 재생성**하고 head 를 다시 막았다. 그 결과 같은 (gen,seq) 가 2~3회 포기되고(로그 age 가 다시 처음부터 계산되는 것이 증거: `.claude/n3_rule2_recovery_run2.log` hard-cap 1,000,893 → hard-cap 1,000,880 → nack-spent 425,432), Codex 조건 "hardCap 초과는 한 episode" 가 깨졌으며, 뒤따르는 정상 IDR 이 배달되지 못해 sweep2 의 S23 이 FAIL 했다(`.claude/v14_a04_sweep2_fail_remote60_viewer_udp_recovery_test.log`). 단독 실행은 PASS 였지만 같은 결함을 담고 있었고, 회귀가 이를 못 잡은 이유는 단정이 keyframe 요청 수만 보았고 그 수는 KeyframeRequestState 리미터(120 ms 간격·토큰 3)가 눌러 1~2회로 보였기 때문이다(감사 T2 와 같은 부류).
- 수정: 포기한 identity 를 **tombstone** 으로 기억한다(`abandoned_`, `IsAbandoned(gen,seq,nowUs)`). `GiveUpIncomplete(gen,seq,nowUs)` 성공 시 그 (generation, seq) 를 기록하고, `PushDatagram` 은 stale 가드 앞에서 그 identity 의 청크를 Ignored 로 버린다. `lastDeliveredSeq_` 를 전진시켜 "배달한 것처럼" 꾸미지 않는다(Codex 주의 1: 배달 gap·IDR 복구 계약과 다음 배달의 `droppedPreviousIncomplete` 의미가 바뀌면 안 되고, 판정하지 않은 AU 를 삼켜서도 안 된다). 다른 generation 은 seq 공간이 새로 시작하므로 절대 매칭되지 않는다. 상한·수명(Codex 주의 2): 최대 16개(오래된 것부터 폐기)·시계를 받은 경우 5 s TTL·`Reset()`(새 세션/디코더 리싱크)에서 소거 — 헤더 주석에 명시.
- 회귀(Codex 주의 3, 요청 수가 아니라 **포기 횟수**로 직접 단정): 수신기의 포기 카운터를 `RecvStats.udpStuckHeadGiveUps` 로 올리고 `VideoReceiver::stats()` 로 노출, rig `give_ups()`. S17·S18·S21·S23·S24 각각 "그 head 의 포기 == 1" 을 단정. 신규 **S24** — 포기 뒤 그 AU 의 늦은 청크가 도착(`FakeHost::ResendChunks`)해도 재차단·재포기 없음(포기 1회 유지), 이어진 복구 IDR 이 정상 배달. shared_core 단위: 늦은 청크 Ignored·다음 AU 정상 배달·같은 seq 라도 다른 generation 은 미차단·TTL 만료·`Reset()` 소거. S23 은 청크 간격을 30 ms 로 바꿔 rule ③(무진행)이 아니라 **hardCap 이 실제 발동**하도록 정정(로그 reason=hard-cap 1건). `published_key_at_or_after` 완화는 유지하되 해소 근거로 쓰지 않는다(Codex 주의 3).
- 검증: 수정 후 recovery 단독 ×5 `.claude/v14b_recovery_run1..5.log` 전부 PASS(실행당 포기 6회 = 시나리오마다 정확히 1회, reason=hard-cap 은 S23 1건), shared_core `.claude/v14b_shared_core.log` PASS. 수정 전 재현 로그 보존: `.claude/n3_prerule_recovery.log`(초기안), `.claude/n3_rule2_recovery_run1..3.log`(포기 2~3회, PASS 였으나 결함 포함), `.claude/v14_a04_sweep2_fail_remote60_viewer_udp_recovery_test.log`(S23 FAIL).
- 전체 검증: 전체 Release 빌드 exit 0(오류 0, `.claude/v14b_full_build.log`), sweep 40종 `.claude/v14b_sweep1.txt` **38 PASS + 2 FAIL** — 둘 다 환경: `udp_control_e2e_test`(UDP 43000 을 라이브 GNLinkStream PID 18844 가 점유), `gdi_capture_process_test`(GDI_DELIVERED_FPS 3.66·복사 평균 258 ms 로 fps ≥ 50 요구 불충족 — 오늘 오전부터 이 PC 에서 간헐적으로 재현, **원인 미확정**). recovery 59 s PASS. 실행 횟수: recovery 수정 후 단독 5·sweep 1, shared_core 2, 전체 빌드 1. RDP 세션 없음(qwinsta 콘솔).
- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `viewer_recv_stats.hpp`, `viewer_video_receiver.hpp`, `viewer_video_receiver.cpp`, `viewer_udp_recovery_test.cpp`, `docs/history.md`(이 항목 + #417 정정), `docs/작업목록.md`. 설치·push·서버 배포 없음.
- 다음: 검증용 검사(#418 c77b01d 와 함께) → 구현 OK → Codex 최종 확인 → 그 뒤에만 0.2.104 릴리스 빌드·설치본 1회.

### 420) 2026-09-08 Codex 최종 검토 보완 3건 — provenance 신호 배선 누락, tombstone 은퇴 경계, 재동기 로그 스로틀 (검증용 remote#srb0qig4 전달)
- (1) A06 배선 누락(제품 결함): `host_stage_encode_send_h264.cpp` 는 `encode_frame_surface` 거부(:337)·`encode_frame_bgra` 실패(:348)에서 `provenance_invalid()` 검사(배치 밖 지점)보다 **먼저 반환**한다. MFT 가 입력을 수용한 뒤 drain/ProcessOutput 실패로 false 를 낼 수 있고, 그 호출에서 FIFO overflow 래치가 서면 `provenanceResyncPending` 이 세워지지 않아 — tick 재시도는 pending 만 보므로 — 이후 성공 encode 가 없으면 재동기 요청이 영구 유실된다(확정 사양 "encode 반환 직후, 성공 여부·units.empty 무관" 미충족). 수정: `EncoderState::NoteProvenance(bool)`(멱등, 최초 래치 시 true 반환)을 두고 **두 early-return 앞과 기존 지점 모두**에서 호출. HN04 partial-result 확장은 하지 않음.
- (2) tombstone 은퇴 경계(제품 결함): 5 s TTL·용량 16 만으로는 호스트가 그보다 오래 그 AU 를 계속 보내거나 그 사이 포기가 16회 더 나면 같은 (gen,seq) 가 재조립·재포기돼 "identity 당 포기 1회" 가 경계에서 깨진다. **시간 TTL 삭제**하고 은퇴를 의미 있는 경계로: (a) 같은 generation 에서 배달이 그 seq 를 지나감(그 뒤로는 기존 stale 가드가 덮으므로 tombstone 중복) (b) 다른 generation 의 배달(옛 seq 공간 소멸) (c) `Reset()` (d) 용량 16 초과 시 가장 오래된 것 폐기. 은퇴는 `DeliverAssembly` 에서 수행. (d) 는 "배달 하나 없이 16개를 포기한 경우에만 생기는 유계 구멍"이며 무한 증가 대신 택한 트레이드오프임을 헤더 주석에 명시. `IsAbandoned(gen,seq)` 시계 인자 제거, `AbandonedCount()` 추가.
- (3) 로그 스로틀: `TryProvenanceResync` 는 예산 소진 시 `provenanceResyncFailed` 를 올리지 않고 false 를 반환하므로 `failed % 30` 조건이 매 프레임 참이 됐다. **시간 기준 1 s**(`provenanceDeferLogUs`)로 교체.
- 회귀: shared_core tombstone 단위를 경계 규칙으로 재작성 — 배달 없이 10 s 뒤 늦은 청크도 여전히 Ignored·tombstone 유지, 배달이 지나가면 은퇴(그 뒤는 stale 가드), 다른 generation 배달로 옛 항목 은퇴, 용량 16 상한(가장 오래된 것부터)·`Reset()` 소거. epoch_test [4] 에 (i) 추가 — `NoteProvenance(false/true)` 멱등성, 래치 중 current key 도 거부, **예산 소진 시 pending·invalid 유지·`provenanceResyncFailed` 미증가**, 다음 창에서 재시도 성공. **한계**: (1) 의 early-return 경로 자체는 실행 회귀가 아니다 — `H264Encoder` 에 IMFTransform 주입 seam 이 없어 fake transform 으로 "encode false + overflow" 를 만들 수 없다(seam 추가는 범위 밖, 검증용에 판단 요청). 현재는 호출되는 헬퍼의 결정론적 회귀 + 호출 지점 코드 대조까지.
- 검증: recovery 단독 ×8(부하 없는 재실행 포함) PASS(포기 6/실행 = 시나리오당 1), shared_core PASS ×2, epoch_test 단독 ×3 PASS([1][2][2b][3][4]). 전체 빌드 exit 0(오류 0) ×2. sweep 2회: `.claude/cx_sweep1.txt` 38+2, `.claude/cx_sweep2.txt` **39+1**. sweep1 의 두 FAIL 은 (a) `gdi_capture_process_test`(GDI_DELIVERED_FPS 3.66, 원인 미확정), (b) `host_encode_epoch_test` [3] `holdKick <= 150000`·`holdKick < holdNoKick` — 실 MFT 의 홀드 시간을 재는 측정 케이스가 sweep 부하에서 흔들린 것으로, 단독 ×3 은 PASS. sweep2 의 유일한 FAIL 은 `udp_control_e2e_test`(UDP 43000 을 라이브 GNLinkStream 이 점유하는 환경). recovery 는 sweep1(60 s)·sweep2(60 s) 모두 PASS 이나, 단독 8회 중 1회(cx_recovery_run4)에서 S15 `WaitRealSenderHeld(2000)` 가 타임아웃했다 — 실 sender 스레드가 2 s 안에 permission point 에 닿지 못한 부하성 흔들림이며 이번 변경(tombstone·배선·로그)과 무관한 기존 시나리오다. 재시도로 덮지 않고 여기 기록한다(재현 시 대기 시간 상향이나 원인 조사가 필요).
- 변경 파일: `apps/native_poc/src/host_encoder_manager.hpp`, `host_stage_encode_send_h264.cpp`, `native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `host_encode_epoch_test.cpp`, `docs/history.md`(이 항목 + #419 표기 정정). 설치·push·서버 배포 없음.
- 다음: 검증용 재검사 → Codex 최종 확인 → 그 뒤에만 0.2.104 릴리스 빌드·설치본 1회.

### 421) 2026-09-08 tombstone 커밋 범위 교정 — 반려된 용량 망각·미확정 generation 은퇴 제거, 용량 분기는 미완(릴리스 차단) (검증용 remote#srb0qig4 전달, Codex 반려)
- 배경: #420 은 우편함 지연으로 Codex 의 반려가 닿기 전에 커밋됐다. Codex 는 (d) "용량이 차면 가장 오래된 기록 폐기"를 두 번 반려했다 — 기록을 잊으면 같은 identity 를 다시 수용하게 되고, 그것이 바로 이 tombstone 이 막으려던 결함이다("유계 구멍"으로 문서화하는 것도 불가). generation 변경 시 은퇴도 미확정이다(어셈블러가 늦은 패킷의 generation 을 따라가면 안 되고, 현재 수신 경로에서 `assembler.Reset()` 호출은 0건이라 별도 알림 배선이 필요).
- 이 커밋: (a) 용량 `pop_front` 삭제 — 이제 아무 기록도 버리지 않는다. (b) 다른 generation 배달로 은퇴시키던 분기 삭제 — 은퇴는 **같은 generation 에서 배달이 그 seq 를 지나갈 때만**(그 뒤로는 기존 stale 가드가 덮는다)과 `Reset()` 뿐. (c) 헤더에 **"UNFINISHED, RELEASE-BLOCKING"** 으로 명시: 배달 없이 여러 identity 를 포기하면 목록이 그대로 늘어난다(항목 24 B, 포기 규칙상 병적인 경우), 이를 닫는 포화(saturation) 설계는 확정 대기.
- 회귀: 용량 케이스를 "20개를 연속 포기해도 첫 identity 까지 전부 기억한다"로 바꾸고(잊지 않음이 계약), 다른 generation 은퇴 케이스는 제거. shared_core PASS, recovery 단독 ×3 PASS(포기 6/실행 = 시나리오당 1).
- 상태: 용량 분기는 **미완**이므로 이 항목으로 완료 표시하지 않는다(구현계획·작업목록의 A04 관련 항목에 미완 표시). 0.2.104 릴리스는 이 분기가 확정·구현될 때까지 차단.
- 미실행 회귀(명시): A06 배선의 encode early-return 경로는 여전히 **실행 회귀로 덮이지 않았다** — fake IMFTransform 주입 seam 은 Codex 가 승인했고(이번 A06 회귀 한정), 그 seam 으로 stage 경로를 실행하는 방식은 검증용에 먼저 설명한 뒤 진행한다.
- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.

### 422) 2026-09-08 A04 포화(saturation) episode — 기록을 잊지 않고 유계로 회복 (Codex 확정 설계, 검증용 remote#srb0qig4 위임)
- 배경: #421 이 남긴 미완 — 배달 없이 여러 identity 를 포기하면 tombstone 목록이 계속 늘어난다. 용량 초과 시 폐기(조용한 망각)와 시간 TTL 은 Codex 가 반려(잊으면 같은 identity 를 다시 수용 = 원래 결함 재발). 또 제가 대안으로 제시한 `abandonedHighWaterSeq`(prefix 차단)도 반려됐다 — **포기 대상은 `OldestIncomplete`(도착순 첫 미완성)이지 seq 최소가 아니므로**(seq 최소 선택은 `PopDelivery` 뿐) 재정렬 시 31 을 먼저 포기하면 뒤늦게 온 정상 30(수리 가능한 IDR 포함)까지 prefix 로 막힌다.
- 설계(확정): 용량은 **트리거**이지 축출 한도가 아니다. `abandoned_` 가 `kAbandonedSaturationCap`(16)에 닿으면 기록은 전부 보존한 채 `saturated_` 래치가 서고 단일 recovery episode 가 시작된다. episode 동안 — (1) 새 **비-키** 조립 금지(진입 시 남아 있던 비-키 조립은 폐기하되 기록 불요: 재생성이 막혀 있으므로), (2) 키 후보 슬롯 **K=2** 재사용(새 키가 오면 seq 최소 후보가 물러나고 floor 가 그 seq 로 상승 → 물러난 후보는 재생성 불가), (3) floor 는 episode 동안 단조·후보 0개여도 보존, (4) **live 조립 조회가 floor 보다 우선**(수리 중인 후보는 방해받지 않음), (5) 후보 생성·교체 **직전**(패킷 구조 검증 뒤) 주입 predicate 로 호출자의 generation 판정을 물어 **비Accept 면 후보·floor·배달 상태를 전혀 바꾸지 않음**.
- 해제: 완성 키 AU 가 `AdmitGeneration==Accept` + 프레임 게이트 `Decode` 를 통과한 **직후·decode 전** 수신기가 `NoteKeyAccepted(gen,seq)` 를 부르고, **그 episode 가 배달한 후보 identity 와 일치할 때만** 해제한다(디코드 성공 주장 아님 — 이후 실패는 기존 reason 4/5/7 복구 담당). 해제 시 그 워터마크가 덮는 기록만 회수하고, **더 새로운 기록이 여전히 cap 이면 래치 유지**. 시간 기반 종료·축출 없음, 초기화는 `Reset()`(새 세션 객체)뿐 — "seq 차가 반주기면 리셋" 규칙은 넣지 않았다(unsigned 차로는 재정렬과 재시작을 구별할 수 없다).
- 배선: shared_core 는 뷰어 상태에 의존하지 않도록 `SetSaturationAdmitFilter(std::function<bool(uint64_t)>)` 주입만 받고, 수신기가 제품 공용 판정 `SelectionGateState::AdmitGeneration(gen)==Accept` 를 그대로 넘긴다(복제 없음). Android 경로(`native_video_client_session.cpp`, hold 0)는 미설정 → 동작 불변. 해제 신호는 `VideoReceiver::activeAssembler_`(run_udp 수명 동안만 유효, 밖에서는 null)로 프레임 경로에서 전달. wire·ABI 변경 없음. **한계**: 후보 admission 은 그 시점의 선택 상태를 묻는 것이고 최종 수락은 기존 디코드 경로 게이트가 결정한다(그 사이 선택이 바뀌는 경쟁은 최종 게이트가 처리).
- 회귀(`native_video_client_shared_core_test.cpp`, 결정론): cap 도달 → episode 시작·기록 16개 전부 보존·비-키 조립 폐기, 새 P 거부·키는 후보, K=2 교체·floor 401 상승·물러난 후보 재생성 차단·live 후보는 늦은 중복에 방해받지 않음, 배달만으로는 미해제, 미등록 키·**다른 generation** 해제 0(후보·floor·기록 불변), 정상 키로 해제·P 재개, **해제해도 더 새로운 기록 16개면 래치 유지**(floor 는 해제), 거부 generation 은 상태 불변 후 허용 generation 은 교체 성공, old seq 99 vs last 100 → 초기화 0, 재정렬(31 먼저 포기·30 나중 도착) **30 미차단**.
- 검증: shared_core PASS ×2, recovery 단독 ×5 PASS(포기 6/실행 = 시나리오당 1), 전체 Release 빌드 exit 0(오류 0), sweep `.claude/sat_sweep1.txt` **39 PASS + 1 FAIL**(e2e: UDP 43000 을 라이브 GNLinkStream 이 점유하는 환경). RDP 세션 없음.
- 상태: #421 이 남긴 용량 분기 미완이 이 항목으로 닫힌다. 남은 릴리스 차단 요소는 **A06 encode early-return 경로의 실행 회귀 부재**(승인된 fake transform seam 으로 제작 예정).
- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `viewer_video_receiver.hpp`, `viewer_video_receiver.cpp`, `viewer_video_receiver_frame.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.

### 423) 2026-09-08 A06 encode 경로 실행 회귀 — 동작 보존 추출 + fake transform seam (Codex 승인 조건 ①~⑤, 검증용 remote#srb0qig4 위임)
- 남아 있던 공백: #420 의 배선 수정은 옳았지만 **encode 실패 early-return 경로가 실행 회귀로 덮이지 않았다**(실 MFT 로는 "출력 없이 입력 64개 수용 후 drain 실패"를 만들 수 없음). Codex 가 이번 회귀에 한해 transform 주입 seam 을 승인했고, 조건은 제품 기본 동작 불변·상태 직접 세팅 금지·**실제 stage 호출 경로 실행**·한계 분리 보고·범위 확장 없음.
- ① 표기: 아래 추출은 **동작 보존 추출(behaviour-preserving extraction)** 이며 순수 함수가 아니다(인코더를 구동하고 EncoderState 를 바꾼다). 주석에 그대로 적었다.
- ② 추출(`host_encoder_manager.hpp`): `EncoderState::RunEncodeCall(encodeCall)` 이 **encode 호출과 그 직후 provenance 처리를 함께 소유**하고 `{ok, provenanceLatched}` 를 돌려준다. 얇은 래퍼 `EncodeSurfaceWithProvenance`·`EncodeBgraWithProvenance` 로 두 제품 경로를 노출. `host_stage_encode_send_h264.cpp` 의 surface 분기와 BGRA 분기가 각각 이 함수를 호출하고, 두 early-return 에서 provenance 호출은 **삭제**했다 — 처리가 이미 함수 안에서 끝나므로 어떤 반환 경로(false / 부분 출력 / units 0)도 우회할 수 없는 구조가 된다. 로그는 latch 된 호출에서만 1회.
- ③ seam(`mf_h264_codec.hpp/.cpp`): `H264Encoder::set_transform_for_test(IMFTransform*)`. 미설정이면 열거·백엔드 선택·설정 순서가 **완전히 동일**하고, 설정 시에만 그 transform 을 채택한다(ComPtr 로 소유, `shutdown()` 에서 해제; 호출자도 자기 참조를 유지). 외부 설정·UI·wire 확장 없음.
- 회귀 `host_encode_epoch_test` [5](결정론, 상태 직접 세팅 없음): 입력만 받고 출력을 내지 않는 fake transform 으로 **실제 FIFO overflow** 를 만든다(로그 `overflow after 65 accepted inputs`) → gate 차단·pending 확인. 이어 (a) drain 실패·units 0, (b) 부분 출력 뒤 drain 실패 두 변형에서 **encode 가 false 를 반환해도** 같은 호출 안에서 gate 가 닫히고 rebuild 가 pending 이 된다(`provenanceLatched`). 그 뒤 **무입력 tick** 경로: 공유 예산(3회/10 s) 소진 → pending 유지·`provenanceResyncFailed` 미증가, 다음 창에서 재초기화 실패 → failed=1·pending 유지, 그 다음 창에서 성공 → 래치 해제·forceKeyNext. 단독 ×3 PASS.
- ④ **커버리지 구분(중요)**: *실행 회귀로 덮인 것* = `RunEncodeCall` 이하의 순서(encode → provenance → 호출자의 실패 처리), 두 실패 변형, tick 재시도의 세 상태. *코드 대조로만 확인한 것* = stage 가 그 함수를 원래 지점에서 호출한다는 사실 — 테스트는 `HostContext`/`TickContext` 전체(캡처 readback·sender 스레드·mailbox·watchdog)를 조립하지 않으므로 `encode_send_h264` 자체는 실행하지 않는다. diff 로 surface·BGRA 두 분기가 각각 래퍼를 호출하고 그 뒤에 `return Flow::Continue` 만 남았음을 확인했다.
- ⑤ 추출 전후 동일성: `host_sender_epoch_test` PASS, `host_encode_epoch_test` PASS, 전체 Release 빌드 exit 0(오류 0), sweep `.claude/seam_sweep1.txt` **38 PASS + 2 FAIL** — `udp_control_e2e_test`(UDP 43000 을 라이브 GNLinkStream 이 점유하는 환경), `gdi_capture_process_test`(GDI_DELIVERED_FPS 3.6x, **원인 미확정**). 두 실패 모두 이번 변경과 같은 파일을 쓰지 않으며, PASS 로 상쇄하지 않고 그대로 기록한다.
- 변경 파일: `apps/native_poc/src/mf_h264_codec.hpp`, `mf_h264_codec.cpp`, `host_encoder_manager.hpp`, `host_stage_encode_send_h264.cpp`, `host_encode_epoch_test.cpp`, `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`. 설치·push·서버 배포 없음.
- 상태: 이로써 A01·V14/A04(+tombstone·포화)·A02/A06/HN07 의 구현과 회귀가 모두 끝났다. 릴리스(0.2.104)는 검증용 검사와 Codex 최종 확인 뒤에만.

### 425) 2026-09-08 포화 episode 경계 결함 6건 — 무한 증가·오래된 키의 후보 축출·iterator UB·gen 한정 floor/회수·후보 목록 증가·진입 시 K 위반 (검증용 remote#srb0qig4 NEEDS_CHANGES, 구현 OK 철회 뒤)
- 배경: #422 는 설계 형태는 맞았으나 경계 동작 6건이 확정 조건과 달랐다. 검증용이 코드로 짚었고 전부 재현됐다 — 특히 (2) 는 **수정 전 실행이 Segmentation fault**(`.claude/bnd_prefix.log`, 종료 코드 139).
- (1) 포화 중 후보 실패가 기록을 늘림: `GiveUpIncomplete` 가 `saturated_` 와 무관하게 항상 기록해, 후보가 반복 실패하면 목록이 무한히 늘었다(수정 전 회귀: 20회 실패 뒤 16 → **36**). 확정 조건대로 **포화 중 포기는 기록하지 않고**, 키 후보였다면 floor 만 올린다(같은 episode 의 재시도이지 새 identity 가 아니다). 수신기의 terminal 경로도 같은 함수를 쓰므로 함께 적용된다.
- (2) 교체 규칙과 iterator UB: 후보가 둘일 때 incoming 이 **더 새로운지 보지 않고** 최소 seq 후보를 지워, 재정렬로 도착한 (floor 보다는 새롭지만 두 후보보다 오래된) 키가 수리 중인 후보를 밀어냈다. 또 `assemblyIt` 를 계산한 뒤 `assemblies_`(deque)에서 erase 하고 그 iterator 를 비교해 **UB**였다. 수정: incoming 이 교체 대상보다 새로울 때만 교체(아니면 Ignored, 후보·floor 불변), 포화 admission 을 `find_if` **앞**으로 옮겨 erase 뒤 iterator 를 새로 계산.
- (3)(4) generation 한정 제거: floor 검사·갱신과 회수(`NoteKeyAccepted`·배달 워터마크) 가 같은 generation 으로 제한돼 있었다. 호스트 seq 는 UDP 세션 전역에서 단조(`hdr.seq = ++encoder.encodedSeq`, 리셋은 TCP 재접속 경로뿐)이므로 **전역 seq 기준**으로 바꿨다. 구 generation 기록이 cap 을 유지시켜 새 generation 의 IDR 이 수락된 뒤에도 P 가 막히던 반례가 사라진다. `saturationFloorGen_` 삭제(잔존 0건).
- (5) `deliveredKeyCandidates_` 무한 증가: 최종 게이트가 계속 거부하는 완성 키가 반복되면 늘었다. **8개 링**으로 유계화하고, 밀려난 키가 나중에 수락돼도 해제되지 않는 **보수적 동작**(해제 자격만 제한, 기록은 잊지 않음)을 주석에 명시.
- (6) 진입 시 K 상한 미보장·floor 초기화 위반: 진입 시 비-키만 지우고 키 조립(최대 8)을 줄이지 않아 상한을 즉시 위반했고, `NoteKeyAccepted` 는 래치가 유지되는 경우에도 floor 를 지워 episode 단조성을 깼다. 수정: 진입 시 **최신 seq 기준 K=2 로 트림**(밀려난 seq 로 floor 상승), 해제 후 래치가 유지되면 **floor 도 유지**.
- 회귀(`native_video_client_shared_core_test.cpp`, 각 케이스가 제품 상태를 직접 단정; 한 케이스가 실패해도 나머지가 실행되도록 집계식으로 작성): (1) 포화 중 후보 20회 실패 → 기록 증가 0·episode 1, (2) 후보 200·202 에 198(둘보다 오래됨) → Ignored·후보/floor 불변 + 같은 상태에서 live 후보의 청크 처리 정상(UB 회귀) + 203 은 교체·floor 200, (3) 다른 generation 의 키가 floor 이하면 차단, (4) gen 1 기록 16개 + gen 2 IDR 수락 → 전부 회수·episode 종료·**P 재개**, (5) 수락되지 않는 완성 키 50회 → 후보 목록 ≤ 8, (6) 진입 시 키 조립 4개 → 2개로 트림·floor 801, (6b) 해제 후에도 cap 이면 래치·floor 유지. 수정 전 `.claude/bnd_prefix.log`(FAIL 3건 + segfault) → 수정 후 `.claude/bnd_postfix.log`·×3 PASS.
- 검증: shared_core ×3 PASS, recovery 단독 ×5 PASS(포기 6/실행 = 시나리오당 1), 전체 Release 빌드 exit 0(오류 0), sweep `.claude/bnd_sweep1.txt` **38 PASS + 2 FAIL** — `udp_control_e2e_test`(UDP 43000 라이브 점유 환경), `gdi_capture_process_test`(**원인 미확정**). 상쇄하지 않고 기록.
- 변경 파일: `apps/native_poc/src/native_video_client_shared_core.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core_test.cpp`, `docs/history.md`. 설치·push·서버 배포 없음. 릴리스는 재검사·Codex 최종 확인 뒤에만.

### 426) 2026-09-08 0.2.104 후보 — Windows 안정화 1차(A01 · V14/A04 · A02/A06/HN07) 반영 릴리스 빌드·설치본 (검증용 remote#srb0qig4 구현 OK, Codex 최종 확인 뒤 후보 생성 승인)
- 포함 커밋 9건: 858f766(A01 로그 업로더 401 busy loop, #416) · 7d0d69b(V14 공통 maintenance + A04 유계 포기, #417) · c77b01d(A02/A06/HN07 인코더 provenance, #418) · fe42957(A04 tombstone, #419) · 5549d7c(배선·경계·로그 스로틀, #420) · f77adad(반려안 제거·미완 표시, #421) · ac48758(포화 episode, #422) · f13887b(A06 실행 회귀·동작 보존 추출, #423) · adeffe7(포화 경계 6건, #425).
- 릴리스 트리 = **adeffe7 + 이 커밋**. 이 커밋의 제품 변경은 (a) `product_version.hpp` 0.2.103→0.2.104, (b) Codex 정정 지시 1건인 `NoteKeyAccepted` **주석** 문구뿐이다 — 로직·wire·ABI 변경 0.
- 주석 정정: "decoder's new reference" 는 decode **전** 의 조립/게이트 수락 경계라 부정확했다 → "수락 경계이며 decode 성공 주장이 아니다(그 뒤의 decode 실패는 기존 reason 4/5/7 복구 담당)" 취지로(`native_video_client_shared_core.cpp:665`, `.hpp:355`). #422 의 같은 취지 문구는 이미 정확해 그대로 둔다.
- 빌드·산출물: 전체 Release 빌드 1회 exit 0(오류 0, 로그 `.claude/release_build_0.2.104.log`) → `dist/GNLinkSetup-0.2.104.exe` — 3,475,968 bytes, sha256 `039b3a47d36850292c2049400719ddeb0b17fd39f50ce73dec445e6d5d4dff2f`(= `build-local/apps/native_poc/Release/GNLinkSetup.exe` 와 동일). 임베드 버전 **0.2.104**(GNLinkHost·GNLinkClient·GNLinkSetup 의 UTF-16 문자열, 0.2.103/0.2.102 잔존 0; GNLinkStream·GNLinkViewer 는 원래 버전 문자열 미포함). payload 6종 sha256 = 방금 빌드한 Release 산출물과 일치 — GNLinkHost `98510ac8…`, GNLinkClient `ff4efccc…`, GNLinkViewer `aa8f540d…`, GNLinkStream `4949313c…`, GNLinkInputService `1fd2aa56…`, GNLinkCapture `253734df…`. `product_version.hpp` 를 포함하는 것은 `host_app_main.cpp`·`client_shell_main.cpp`·`installer_main.cpp` 셋뿐이라 Stream/Capture/InputService 는 소스 무변경으로 재링크되지 않았다(payload 는 그 산출물과 동일).
- 근거(이 커밋에서 재실행하지 않음, 직전 커밋들에서 실행된 것): `.claude/seam_sweep1.txt` **38 PASS + 2 FAIL**(#423), `.claude/bnd_sweep1.txt` **38 PASS + 2 FAIL**(#425), `viewer_udp_recovery_test` 단독 ×5 PASS, `shared_core_test` ×3 PASS. **전수 PASS 가 아니다.**
- 미해소·원인 미확정(PASS 로 상쇄하지 않고 그대로 기록): `udp_control_e2e_test`(UDP 43000 을 라이브 GNLinkStream 이 점유) · `gdi_capture_process_test`(GDI_DELIVERED_FPS 3.6x) · epoch [3] `holdKick` 단정(1607404 도입) · S15 `WaitRealSenderHeld(2000)`(1287883 도입) — 뒤 둘은 **이번 변경 이전부터 있던 자산**이고 실 MFT/실 sender 타이밍에 의존하며 **재현에 실패해 원인을 확정하지 못했다**.
- 미검증: A06 의 stage 호출 배선은 **코드 대조까지**(실행 회귀가 덮는 것은 `RunEncodeCall` 이하, #423 ④). 사용자 실기 전이므로 401 뒤 업로드 재개 · 제어만 오는 링크의 복구 타이머 · 정적 화면 꼬리 손실 복구와 포기 1회 · UAC 복귀 뒤 인코더 재동기 · 고화질 영상 정지 재현 여부는 전부 미확인.
- 성격: 이번 후보는 **확인된 코드 결함 수정의 실기용**이며, 09-08 11:27 의 물리 단절(제어·영상 동시 정지) 해결을 **보장하지 않는다**. 설치·라이브 세션 중지·서버 배포·push 없음.
- 변경 파일: `apps/native_poc/src/product_version.hpp`, `native_video_client_shared_core.cpp`, `native_video_client_shared_core.hpp`(주석), `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.
- 상태: 검증용의 산출물 대조(payload sha256·임베드 버전·빌드 commit·크기·트리 clean) 전 — 완료 아님.

### 427) 2026-09-08 업데이트 기능 step1 — 현행 배선 조사 + 설계/완료조건 문서 (Codex 계획, 검증용 remote#0jkgf453 위임, **문서만**)
- 목적: 자동 업데이트 기능을 설계하기 전에 **지금 무엇이 어떻게 배선돼 있는지**를 코드 근거로 확정하고, 그 위에서 결정 가능한 설계와 완료조건만 초안으로 적는다. 제품 코드·CMake·gradle 변경 금지, 빌드·테스트 없음(따라서 `qwinsta` 불필요), 라이브 0.2.104 실기 진행 중이라 설치·배포·push·프로세스 종료 일절 없음.
- 산출물 1개(신규): `docs/업데이트_기능_설계.md`. 모든 사실 진술에 `파일:줄` 근거를 붙였고, 확인 못 한 것은 **미확인**(5절 6건), Codex 확정 대기는 **비워 둔 절**(4절 4건)로 분리했다.
- **설치기 사실조사**: 설치 경로 `%ProgramFiles%\GNLink`(`installer_main.cpp:87-91`), kPayload **8개**(exe 6 + `ui\shell.html`/`ui\macro.html`, `:48-57`) + 자기 자신 복사 `GNLinkSetup.exe`(`:309-314`) = 디렉터리 9개 항목. `stop_running_product()` 는 서비스 정지(최대 5초 폴링) 후 `taskkill /F /T /IM` 4개(`:137-178`). 서비스는 `SERVICE_DEMAND_START` + LocalSystem, 바이너리 절대경로가 SCM 에 박힌다(`secure_input_service_main.cpp:1151-1180`). 방화벽은 `GNLinkStream.exe` **1건만**(`:211-215`). 시작메뉴 `.lnk` 2개(`:221-254`, `:327-330`). Uninstall 키 8값(`:256-280`) — 설치 버전의 유일한 기계 판독 출처가 `DisplayVersion`. 인자는 `has_flag()` 의 **부분 문자열 검색**(`:583-590`), temp 재실행은 **언인스톨 전용**(`:344-370`, `/fromtemp` 는 `:603` 에서만 읽힘).
- **권한표**: `/MANIFESTUAC` 는 저장소 전체에서 2곳뿐 — `remote60_host_app`(`CMakeLists.txt:286-290`)·`remote60_installer`(`:390-394`) 만 `requireAdministrator`. `.manifest` 파일 0건, `.rc` 의 `RT_MANIFEST` 0건이므로 나머지 5개(`GNLinkStream`/`GNLinkCapture`/`GNLinkInputService`/`GNLinkClient`/`GNLinkViewer`)는 **asInvoker**. 귀결: 호스트발 업데이트는 이미 상승돼 있고, **PC 클라이언트발은 상승되어 있지 않다**.
- **수명주기**: Host→Stream(`host_app_main.cpp:341`, `:444-479`, job 없음), Stream→Capture(`gdi_capture_process.cpp:246-283`, `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`), Client→Viewer(`client_shell_main.cpp:529-532`, job 없음). 저장소에서 job object 사용처는 캡처 워커 한 곳뿐.
- **서버**: 라우트 8 + `/healthz`, `메서드 경로` **정확 일치** 디스패치(`server.js:564-585`) — 접두사 매칭·정적 서빙·GET 바이너리 경로 없음. 인증은 Bearer 세션(메모리 전용, `:490`)과 `x-host-token`(sha256 저장, 재등록 시 즉시 무효 `:655-657`) 두 종류. TLS 는 `REMOTE60_DIR_TLS_KEY/CERT` 가 둘 다 있을 때만(`:58-59`, `:1344-1360`), 꺼져 있으면 기동 경고.
- **제품 HTTP 클라이언트의 제약(설계 직결)**: 공개 API 는 `http_post()` 하나뿐이고(`directory_client.hpp:98-103`), **응답 본문을 64 KB 에서 자른다**(`directory_client.cpp:346-355`), 타임아웃 6초, GET·Range·재시도 없음. **HTTPS 는 명시적으로 거부**(`:83-93`)되고 native 전체에 WinHTTP/Schannel 0건. → 현행 코드로는 3.5 MB 설치본을 받을 수 없다.
- **Android**: `applicationId=com.remote60.androiddirect`, `versionCode=11`, `versionName="0.2.12"`, **`signingConfigs` 없음** → 배포본은 debug 서명 `assembleDebug` 산출물(#331·#333). 권한은 `INTERNET`·`ACCESS_NETWORK_STATE` 둘뿐, `REQUEST_INSTALL_PACKAGES` 없음. `network_security_config.xml` 이 cleartext 전역 허용(서버 TLS 부재가 이유라고 파일 주석에 명시).
- **보존 대상**: 로그인·설정·자격증명·로그가 **전부 설치 디렉터리 밖**에 있음을 확인 — `%LOCALAPPDATA%\remote60\host.json`(`directory_client.cpp:179-194`), `%LOCALAPPDATA%\GNLink\`(`client.txt`·`unlock_*.cred`·`host_app.log`·`client.log`·`viewer.log`·`log_upload.diag`), `HKCU\...\Run\remote60`(`host_app_main.cpp:645-646`), `%TEMP%\GNLinkClient`. 따라서 보존 요구는 복사·복원이 아니라 **정리 대상에서 제외**다.
- **반영해야 할 기존 장애 4건(기록만, 수정 없음)** — 검증용이 지목한 것을 현재 트리에서 재확인하고 줄 번호를 실제 값으로 정정: (a) `kImages` 4개(`:160-162`)에 `GNLinkClient.exe`/`GNLinkViewer.exe` 누락 vs kPayload 포함(`:48-57`), `write_file` 공유 0 + `CREATE_ALWAYS`(`:104-106`), `do_install()` 은 선언 순서대로 덮은 뒤 5번에서 코드 4 반환(`:294-307`) → **신버전 exe 4개 + 구버전 Client/Viewer/ui 가 남고 `DisplayVersion` 은 구버전 그대로**. (b) `taskkill /F /T`(`:164-165`)는 프로세스 트리를 죽여 Host 자식으로 뜬 업데이터가 교체 도중 함께 죽는다. (c) `do_install()`(`:282-340`)에 제품 재실행 호출이 없어 "재실행"은 전부 신규 작업. (d) `compare_versions()`(`:444-464`)는 이미 숫자 성분 비교이나 설치기 익명 네임스페이스에 갇혀 있고 **단위 테스트 0건** → 공용 헤더 추출 후보. `docs/full_code_audit_2026-09-08.md:94` 의 `I01` 과 같은 결함이다.
- **계획 밖 발견(고치지 않고 기록)**: `has_flag()` 부분 문자열 검색이라 새 인자가 `/s` 를 포함하면 silent 로 오인될 수 있음(`:583-590`); 설치기에 **동시 실행 상호배제가 없음**(`:593-616`).
- **설계 초안(결정 가능한 부분만)**: 11단계 상태기계(Check→Evaluate→Download→Verify→Prepare→Quiesce→Swap→Register→Relaunch→Health→Done/Rollback)와 단계별 실패 처리. 교체는 스테이징 후 `MoveFileExW(REPLACE_EXISTING)` + `.old` 되돌리기 권고(설치 경로 불변 = 서비스/방화벽 절대경로 유지). 업데이터 위치는 `%ProgramFiles%\GNLink.update\`(사용자 쓰기 가능한 `%TEMP%`/`%ProgramData%` 는 설치기가 지키는 불변식을 되돌리므로 배제), (b) 회피는 **조부모 고아화**(짧게 사는 런처가 즉시 종료) 권고 + 예약 작업 대안. 상호배제는 `Global\` 명명 뮤텍스 + 스테이징 상태 파일(죽은 뒤 재개 판정). 진입점 후보는 트레이 메뉴(`host_app_main.cpp:728-747`, `MenuId` `:95-100`, 처리 `:1307-1327`)·로그인 카드·호스트 기동 시 비동기(`:1441-1447`)·클라 시작 시 비동기(`client_shell_main.cpp:679`). 단계별 완료조건은 "성공했을 것"이 아니라 **관측 가능한 사실**(프로세스 수 0, 9개 항목 해시 전량 일치, `DisplayVersion` 일치 등)로 적었고, (a)(b)(c) 각각의 결정론적 재현 방법을 하네스 절에 명시했다.
- **Codex 결정 대기로 비워 둔 절 4개**: 4.1 아티팩트 출처 검증 방식(HTTPS 부재·TLS 스택 0건·Authenticode 인프라 없음의 사실만 기록), 4.2 Android 서명·키 정책과 앱 데이터 보존, 4.3 manifest 스키마, 4.4 서버 신규 엔드포인트.
- **미확인 6건**: NAS 서버의 실제 TLS 가동 여부, `taskkill /T` 의 열거 시점 의미론(플랫폼 동작 — 실행 검증 필요), 새 스테이징 디렉터리의 ACL 상속 실측, 클라 셸의 백그라운드 훅 지점 특정, Android Kotlin 네트워크 코드, 설치본이 실제로 남기는 9개 외 파일.
- 검증: **빌드·테스트 실행 없음**(문서 전용 task 이므로 정상). 설치·서버 배포·push·프로세스 종료 없음. RDP 상태 확인 불필요.
- 변경 파일: `docs/업데이트_기능_설계.md`(신규), `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.
- 다음 액션: 검증용 Claude 의 문서 검사 → 4절 4항목에 대한 Codex 확정 → step2(공용 `compare_versions` 추출·상태기계 seam·(a)(b)(c) 수정)로. **전체 완료 아님.**

### 428) 2026-09-08 0.2.104 실기 17:15~17:22 증거 보전 — 링크 무수신 반복과 뷰어 자동 종료 6회 (Codex NAS 확인 + 검증용 remote#0jkgf453 독립 조회, **문서만**)
- 목적: 09-08 17시대 실기 구간의 NAS 원본 로그를 **회전으로 소실되기 전에 발췌 보전**하고, 관측 사실만 출처를 구분해 기록한다. `host.log.1` 은 17:22 에 이미 회전된 파일이라 보전이 최우선이었다. **원인 단정 금지** 가 이 task 의 명시 조건이다.
- 산출물 1개(신규): `docs/field_test_2026-09-08_0.2.104.md`. 기존 `field_test_2026-09-07_0.2.100.md`·`field_test_2026-09-08_0.2.103.md` 형식을 따랐다.
- **보전**: NAS read-only 조회로 17:15:00~17:22:59 창을 발췌해 `.claude/field-20260908-1716/`(미추적)에 저장 — `viewer.log_1715-1722.txt` 7,114행 1,358,901B sha256 `d1457954…2834f22`, `host.log.1_1715-1722.txt` 3,751행 672,588B sha256 `14b57ecd…4b692986`, `host.log_1715-1722.txt` 446행 77,533B sha256 `4dc5c0ac…210913f3`. 원본은 `/opt/gnlink/remote60-directory/logs/shotan/` 아래 viewer `68f79d01-…`(14,917,767B) · host `8ec6ecb1-…`(host.log.1 16,777,814B / host.log 2,826,963B). 회전 정책 약 16MB·`.1`~`.3` 보관이며 조회 시점에 `.1`~`.3` 모두 존재했다.
- **출처 3구분**: [C] Codex 확인분 11항목, [V] 검증용 독립 조회 추가 6건, [W] 대조 중 같은 이벤트 원문에서 함께 확인된 인접 사실. **셋 모두 작업용이 원문으로 재확인한 뒤 기록**했고, 재확인 못 한 것은 "미확인" 절로 분리했다.
- **뷰어 자동 종료 6회 = 크래시 아님**: `session-dead action=close` 가 정확히 6건(17:18:48.643 / 17:19:05.960 / 17:20:06.200 / 17:20:21.435 / 17:20:39.453 / 17:20:54.339), 전부 `stage=recv control=0 tunnelClosed=1`, `datagramAgeUs` 11.6~12.5초. 제품 코드와 합치 — `apps/native_poc/src/viewer_session_watchdog.cpp:82-96`(로그 `:85`, `PostMessageW(hwnd, WM_CLOSE, 0, 0)` `:95`, `deadSessionExit` 분기 `:83`). Codex 가 적은 `watchdog.cpp:85~98` 의 실제 파일·줄이다.
- **무수신 형상**: 최초 `link-silent` 17:18:33.315(`stage=recv loops=87555 datagramAgeUs=3688288`) — 수신 루프는 타임아웃으로 계속 돌고 데이터그램만 오지 않았다. 17:18:34.283~.306 에 한 번 수신·표시 재개(seq=2247/2248, `frameGapUs=4683942`), 17:18:37.147 terminal `nack-spent` seq=2309 gen=2 missing=23/80 `total=4`, 17:18:40.510 재 link-silent, 17:18:43.126 `action failed kind=10 transport=udp-tunnel closed=1 reason=peer-lost`(그 시점 터널 이미 closed), 17:18:48.643 session-dead(`controlGoneUs=5029194`). host 는 `17:18:43.744 wire seq=2675 ... epoch=1` 이 마지막이고 **그 뒤 epoch=1 wire 0건**, 같은 시각 `udp control session ended epoch=1 reason=peer-lost`. **wire 는 sendto 기록이며 실 NIC 송출·대향 수신 증거가 아니다.**
- **호스트 자원은 정상 범위**(17:18:34.294 표본): `captureUnmapWaitAvgUs=1917` · `workerD3dCallAvgUs=19` · `encoderResets=0` · `udpTxFail=0` · `queueDepthMax=2`.
- **뷰어 혼잡 판정 전 구간 normal**: `congestionState` 표본 **373개 전부 normal**, `congestionTransitions=0` 373/373, `stale-reference` 0건, `recv-thread stalled` 0건, decode_queue 전이 0건. [V] 같은 구간 `congestionRecoveryReq` 1→2→3→4 인 동안 `congestionRecoveryCount=0`·`MaxUs=0`·`AvgUs=0` 이 373표본 전부 유지 — **복구 요청 4회 / 실제 복구 0회**(관측 사실로만 기록).
- **[V] terminal 은 HQ 전환 전부터 있었다**: 17:16:05.873 seq=623 `total=1`, 17:16:21.302 seq=854 `total=2`, 17:16:35.800 seq=1002 `total=3` — 전부 gen=2 이고 17:18:37.147 `total=4` 까지 연속이라 같은 세션. 그때는 세션이 죽지 않았다. **"HQ 전환이 원인" 해석을 약화시키는 사실로만 기록**했고 반대 결론도 세우지 않았다. [W] terminal 은 구간 총 11건, gen 2→8 로 세션마다 재발 — **A04 유계 포기가 실기에서 동작한 것은 확인이나 경로 차단의 해결은 아니다.**
- **[V] ABR 는 9Mbps 에서 더 내려갔다**: 17:18:42.293 mid 9,000,000(`high_to_mid_severe`, `udpPacePeakBps` 45M) → **17:19:00.443 low 1280x720 6,600,000**(`mid_to_low_severe`, [W] pace 40M). 복귀는 `runtime-config-applied bitrate=12000000`(17:18:53.211, 17:19:09.861, pace 60M), [W] 17:21:00.292 `profile=high reason=static_recovery` 자동 복귀도 있다. 모든 `high_to_mid_severe` 행의 클라 지표는 `clientSize=0x0 clientDecodedFps=0 clientMbps=0`(피드백 부재 상태의 하향).
- **[V] 호스트 캡처 불안정 동반**: `captureRestarts` 1(17:15:34.292)→3(17:19:04.297)→5(17:19:34.294), [W] 이후 9→11→**14**(17:21:34.296). 17:19:04.297 표본은 `encodedFrames=0 sentFrames=0 lastPublishAgeUs=3948784`(약 3.9초 무발행).
- **정정 1건**: 검증용이 전달한 `17:19:04.297 queueDepthMax=9` 는 원문과 다르다 — 그 행은 **`queueDepthMax=4`**, `queueDepthWindowMax=0` 이다. `queueDepthMax=9` 는 구간에 17회 나오지만 **최초는 17:19:11.157**. 원문값으로 기록하고 인접 표본과 구분했다.
- **[V] 첫 세션부터 public + helloNACK**: 세션 시작은 로그상 `17:15:02.699`(사용자 기록 17:16:30 과 다름 — **둘 다 병기**), `17:15:03.241 directory chose 175.207.45.151:43000 (public)`, `17:15:03.243 udp hello ack features=0x1e nackRequested=1 nackNegotiated=1`. [W] 구간에 `directory chose ... (public)` 이 **8회**이며 전부 public + nackNegotiated=1 — Codex 가 "후속 세션" 성질로 본 것이 첫 세션에도 해당한다.
- **[V] 후반 RTT 급증**: 17:20:27.340 `rttUs=111362`, 17:21:11.981 `rttUs=32289`(17:16~17:18 은 1,304~2,419µs). `terminalMinUs` 도 245000 → 417724 / 259578 로 따라 늘었다.
- **미확인 6건**(문서 7절): ① **"17:18:10 HQ 전환" 을 뒷받침하는 로그 이벤트 없음** — `runtime-config` 는 17:15:04.060(12Mbps) 다음이 17:18:52.258 이라 그 사이 설정 전환이 없고, 비트레이트는 세션 시작부터 12Mbps 였다. 그 시각대 관측은 활동량 변화뿐(`17:18:09.738 dxgi-acquire acquires=56`, 직전 표본 5 수준). 사용자 조작 시각으로만 병기. ② 최초 데이터그램 소실 위치(양단 NIC 캡처 미수행, 범위 밖). ③ 원격 실행 바이너리 해시 미측정. ④ 복구 미실행 이유(코드 조사 미실시). ⑤ captureRestarts 증가 원인. ⑥ 디렉터리 저널 미조회.
- **판단 한계(Codex 명시, 약화 없이 그대로 반영)**: 원인·대역 인과 미확정 / 11:27 건과 동일 원인 단정 금지 / 시계 미정렬이라 시각차를 지연으로 해석 금지 / 정지화면 복구 보고는 인과 미확인 / 크래시 아님 / A04 작동 확인은 경로 차단의 해결 아님 / wire 는 수신 증거 아님.
- 검증: **빌드·테스트 실행 없음**(증거 보전·문서 전용, qwinsta 불필요). 제품 수정·설치·라이브 조작·서버 배포·새 패킷캡처·push 없음. NAS 는 read-only 조회만. 자격증명은 문서·커밋에 넣지 않았다.
- 변경 파일: `docs/field_test_2026-09-08_0.2.104.md`(신규), `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.
- 다음 액션: 검증용의 NAS 독립 대조 후 OK. **전체 완료 아님.** 원인 확정에는 양단 NIC 캡처가 필요하고 그 방법은 Codex 결정 대기(#415).

### 429) 2026-09-08 0.2.104 증거 문서 정정 3건 — HQ 프레이밍 제거, old-seq repaint 동일원인 금지, 발췌 미추적 명시 (검증용 remote#0jkgf453 회신 반영)
- 배경: #428 커밋(`dae20f1`) 뒤 검증용의 중간보고 회신이 도착했다. 제가 올린 판단 4건(queueDepthMax 정정 · HQ 전환 이벤트 부재 · 인접 사실 6건 포함 · watchdog 실제 경로)을 **전부 수용**하면서, 그중 두 가지는 서술 방식을 더 원문에 가깝게 바꾸라고 지시했다. 그 반영이다. **관측값 자체는 하나도 바뀌지 않았고, 원인 단정도 추가되지 않았다.**
- **정정 ① HQ 프레이밍 제거(3절·5.5)**: 로그에 "HQ 전환" 이벤트가 없으므로(`runtime-config` 는 17:15:04.060 다음이 17:18:52.258) **"HQ 전환 이전/이후" 로 구간을 나누는 서술을 폐기**했다. 5.5 제목을 "그러나 HQ 전환 전부터 있었다" → "조건이 같은 세션 초반부터 발생했다" 로 바꾸고, 본문을 **"세션 시작(17:15:02.699)부터 끝까지 12 Mbps 동일 조건이었고, terminal nack-spent 는 17:16:05.873 부터 이미 발생"** 으로 다시 썼다. 3절에도 사용자 기록 두 시각이 모두 로그와 어긋난다는 점과 구간을 나누지 않는다는 원칙을 명시했다. 검증용의 지적대로 이 프레이밍 변경은 그가 앞서 준 추가사실 1번의 전제를 바꾸는 것이라, 사용자 조작과 정지의 관계에 대해 **어느 방향으로도 결론을 내지 않는다**는 문장을 함께 남겼다.
- **정정 ② old-seq repaint 에 동일원인 금지 명시(5.2)**: [W] 인접 사실 `17:18:43.145 [present] seq=2308 frameGapUs=6250368`(6.25초 만의 재표시, 새 영상 유입 아님)을 5.2 본문으로 올리면서, `field_test_2026-09-08_0.2.103.md` 3절의 old-seq repaint 와 **"형상이 비슷해 보이나 같은 원인·같은 결함인지는 확인되지 않았다"** 를 같은 문단에 붙였다. 11:27 건에 적용한 제약과 같은 이유임을 적었다.
- **정정 ③ 발췌 미추적 명시(2.2)**: `.claude/` 가 저장소 미추적이라 발췌가 커밋되지 않는다는 사실과, 그래서 행수·크기·SHA-256·원본 크기를 문서에 남긴다는 이유를 적었다(원본 회전 뒤에도 다른 사본과 대조 가능하도록).
- 검증용이 확인해 준 것(제 판단이 맞았던 것): `17:19:04.297` 은 `queueDepthMax=4 queueDepthWindowMax=0` 이 맞고 검증용이 인접 표본 `17:19:34.294`(9/1)와 섞은 것 · `watchdog.cpp` 라는 파일은 없고 `apps/native_poc/src/viewer_session_watchdog.cpp` 가 맞으며 `:83`/`:85`/`:95` 전부 일치 · link-silent 는 검증용이 2건만 짚어 **과소 계상**했고 7건 전체가 맞음 · 인접 사실 6건은 범위 확대가 아니라 같은 이벤트의 원문 맥락.
- 검증: 빌드·테스트 없음(문서 전용). NAS 재조회 없음(#428 발췌로 충분). 제품 수정·설치·라이브 조작·서버 배포·push 없음.
- 변경 파일: `docs/field_test_2026-09-08_0.2.104.md`, `docs/history.md`.
- 상태: 검증용의 NAS 독립 대조·OK 전 — **완료 아님.**

### 430) 2026-09-08 업데이트 기능 step1 개정 — 4절을 미결에서 확정으로, `/T` 폐기 재설계, 버전비교 계약화 (Codex B1/B2/B3 확정 + 검증용 remote#0jkgf453 실측 2건, **문서만**)
- 배경: #427(`3bc99d1`)은 4절 네 항목이 Codex 확정 대기라 사실만 적고 비워 뒀다. 검증용이 `3bc99d1` 을 **구현 OK** 로 판정하면서(커밋 신원·부모·stat, `installer_main.cpp` 줄번호, 64KB 절단, `/MANIFESTUAC` 2건과 `.manifest` 0건, `has_flag` 부분문자열, `CreateMutex` 0건, 4절 무결론, AGENTS.md 역할분리 등 **8개 지점을 원본 대조**) NEEDS_CHANGES 1건과 개정 범위를 줬다. 1·2(a)(b)(c)·3.1·3.3~3.6·5·6절의 기존 내용은 유지하고 **개정 diff 후속 커밋 1개**로 처리한 것이 이 항목이다(문서 재작성 아님).
- **줄 번호 정정 3건 상호 확인**: 검증용이 준 `kImages` 154-157 / `kPayload` 48-56 / `compare_versions` 441 은 전부 오류였고, #427 에 적은 실제 값 **160-162 / 48-57 / 444(본문 444-464)** 가 맞다고 검증용이 재확인했다. 앞으로도 전달받은 줄 번호가 트리와 다르면 실제 값으로 고치고 알린다.
- **4.1 전송·출처 검증 [Codex 확정]**: 업데이트 전용 **WinHTTP HTTPS 클라이언트 신설**(manifest 조회 + 아티팩트 다운로드). 기존 `directory_client`/인증/`log_upload` 는 **전면 전환하지 않는다**. 금지: TLS 검증 끄기, 인증서 오류 우회, **HTTPS→HTTP 리다이렉트**. 출처 검증은 **manifest detached 서명 + 바이너리 내장 공개키**이며 manifest 가 플랫폼·버전·크기·SHA-256·아티팩트 식별을 **하나의 서명 대상에 묶는다**. 알고리즘 기본안 **ECDSA P-256 + SHA-256**(대안 RSA-2048 PSS+SHA-256) — Windows BCrypt/CNG·Android `java.security.Signature`·Node `crypto` 가 모두 표준 제공, **Ed25519 는 CNG 지원 불균일로 제외**. 자체 암호 구현 금지. **TLS 와 서명 둘 다 유지**(서로 대체 아님). **Authenticode 가 아니므로 UAC 게시자 신뢰·SmartScreen 을 해결하지 않는다**고 명시했다.
- **4.2 Android [검증용 실측 + Codex 확정]**: 출하 `dist/GNLink-0.2.12.apk` 서명자 DN `C=US, O=Android, CN=Android Debug`, 인증서 SHA-256 `dcc806ae…2990`, v2/v3만(v1 없음), 2048-bit RSA/SHA256withRSA, PKCS12, 유효 2026-04-06~2056-03-29 — 이 PC `%USERPROFILE%\.android\debug.keystore` 의 `androiddebugkey` 지문과 **완전 일치**. #427 이 코드에서 찾아 둔 `docs/history.md:5566`·`:5815` 의 지문과도 같은 값이라 **서로 다른 출처가 일치**한다. → **동일키 in-place 업데이트로 데이터 보존**, 제거·재설치·데이터 소실 안은 미승인. #427 의 "키 전환 = 제거 후 재설치" 서술은 **동일키를 쓸 수 없는 경우로 한정**해 다시 썼다. 기록한 의존성: 이 keystore 가 사실상 유일한 릴리스 서명 신원이라 **백업·보관 위치 결정 필요(별도 승인)**, **Play 는 debug 인증서 거부 → 직접 APK 배포 전용**, `signingConfigs` 명시 필요, `versionCode` 는 **실배포 최고값** 확인 후 증가. 설치 경로는 `REQUEST_INSTALL_PACKAGES` + 승인·**취소** 경로이며, **FileProvider 를 필수로 단정하지 않았다** — (A) 파일 Intent 면 필요, (B) `PackageInstaller` 세션 스트림이면 불필요라는 조건을 병기했다.
- **4.3 manifest 스키마 / 4.4 서버 엔드포인트 확정**: 스키마는 스키마버전·플랫폼·제품버전·아티팩트 식별·크기·SHA-256·(Android)versionCode + detached 서명. **64KB 절단은 이 스키마의 제약이 아니다** — 신규 WinHTTP 클라이언트가 받으므로. 서명 검증 실패 시 **버전 비교조차 하지 않는다**. 서버는 manifest 조회 라우트만 신설하고 **아티팩트는 디렉터리 서버가 스트리밍하지 않는 것을 기본안**으로 했다(정적 서빙·바이너리 GET 경로 부재 + 릴레이·하트비트와 같은 이벤트 루프 공유). 인증은 기존 Bearer/`x-host-token` 재사용하되 **세션 메모리 전용으로 인한 실패는 정상 취급**해 다음 주기 재시도.
- **4.5 "코드 준비완료" vs "배포가능" 분리 [Codex 확정]**: 전자는 구현·회귀 완료 + **격리 테스트키**로 전 경로 통과(테스트키는 실배포 신뢰키로 절대 쓰지 않음). 후자는 운영 서명키 생성·보관·교체, Android keystore 보관, 운영 도메인·엔드포인트, nginx vhost, 실배포까지 **전부 별도 승인**. 문서 머리에도 "설계 확정이지 배포 승인이 아니다" 를 넣었다.
- **4.6 서버 TLS 현황 [검증용 실측] — 5절 미확인 1번 해소**: NAS 에 nginx :443 LISTEN, TLS vhost 다수(shotan.org 및 하위) + Let's Encrypt 인증서 보유(`/etc/letsencrypt/live/shotan.org`, `vpn.shotan.org`). **그러나 29180/8080 프록시 vhost 가 없고** `REMOTE60_DIR_TLS_KEY/CERT` 미설정 → 디렉터리는 **:29180 평문 직접 노출**. 즉 **HTTPS 추가는 인증서 구매가 아니라 nginx vhost 설정 작업**이다.
- **3.2 재작성 (Codex 확정: 경로 분리만으로는 해결 아님)**: ① **`/T` tree kill 폐기** — 교체 대상 6개 exe 인스턴스를 **정확히 지목해 개별 종료·개별 확인**하므로 부모-자식 관계가 정지 결과에 영향을 주지 않는다(3.6 의 "6개 이미지 인스턴스 0" 완료조건과 그대로 맞물린다). ② **job 수명을 이용한다** — Stream→Capture 는 `KILL_ON_JOB_CLOSE`(`gdi_capture_process.cpp:246-255`)라 Stream 정상 종료로 Capture 가 확실히 따라 죽고, 반대로 **업데이터는 어떤 job 에도 속하면 안 된다**(호스트가 현재 job 을 안 쓰는 것은 전제가 아니라 확인 항목). ③ 실행 위치(`%ProgramFiles%\GNLink.update\`)는 ①의 결과이지 해결책이 아니라고 명시. `DETACHED_PROCESS`·`CREATE_NEW_PROCESS_GROUP`·`CREATE_BREAKAWAY_FROM_JOB` 어느 것도 정면 해결이 아니라는 점도 남겼다. **부수 효과로 5절 미확인 2번(`taskkill /T` 열거 의미론)이 설계 전제에서 빠져 위험도가 내려갔다** — 그 의미론에 의존하지 않는 설계가 됐기 때문.
- **2(d) 재작성 (Codex 확정: 헤더 공유 불가)**: 버전 비교는 서버 JS·Windows C++·Android Kotlin 세 런타임에서 벌어지므로 **"공용 헤더 추출" 이 틀렸다**. 대신 ① **버전 비교 계약** 문서화(성분 숫자 비교, 누락 성분 0, 비숫자 문자에서 중단, 선행 0·오버플로 처리 명시) ② **3개 런타임 공통 테스트 벡터** 한 파일을 세 언어 테스트가 각각 읽어 같은 답을 내는지 확인 ③ **추출은 C++ 안에서만**. 3.7 의 검증 하네스 항목도 같은 모델로 고쳤다.
- **3.5/3.6 보강 [Codex 확정]**: 호스트는 이미 `requireAdministrator` 라 자식 업데이터가 토큰을 상속 → **2차 UAC 불필요**를 완료조건화("호스트발 업데이트 중 UAC 프롬프트 0회"). PC 클라이언트는 **UAC 1회**, **취소해도 계속 사용 가능**, 교체 후 **원래 비승격 사용자 문맥으로 재실행**(상승 토큰으로 띄우면 이후 세션 전체가 관리자 권한이 되므로). **네트워크 실패가 실행·로그인을 막지 않을 것**도 명시. 3.6 에 완료조건 3행 추가(호스트발 UAC 0회 / 클라발 UAC 1회·취소 시 사용 가능 / 업데이터 job 미소속).
- 검증: **빌드·테스트 실행 없음**(문서 전용, qwinsta 불필요). 제품 코드·CMake·gradle 변경 0. 설치·라이브 조작·서버 배포·push·새 캡처 없음. **비밀번호·키 내용·keystore 파일은 문서·커밋에 넣지 않았다**(지문·DN·유효기간은 비밀이 아니므로 기재).
- 변경 파일: `docs/업데이트_기능_설계.md`(519→628줄), `docs/history.md`, `docs/구현계획.md`.
- 상태: 검증용 재검사 전 — **완료 아님.** 남은 미확인 4건(스테이징 ACL 상속 실측 · 클라 셸 백그라운드 훅 · Android Kotlin 네트워크 코드 · 설치본 잔존 파일)은 그대로 유효하다.

### 431) 2026-09-08 0.2.104 실기 17:50~ 720p 고착 — 증거 보전 + 코드 경로 교차확인, 원장 HN13 (Codex NAS·코드 추적 + 검증용 remote#0jkgf453 교차확인, **문서만·제품 수정 없음**)
- 증상: 사용자 0.2.104 실기에서 **17:50 이후 화질이 떨어진 뒤 복구되지 않음**. **17:16 무수신 건과 다른 사건**이라 별도 문서로 분리했다(동일 원인으로 엮지 않는다).
- 산출물: `docs/field_test_2026-09-08_0.2.104_720p.md`(신규) + 원장 `docs/full_code_audit_2026-09-08.md` **HN13**. **제품 수정은 위임되지 않았고 하지 않았다.**
- **⚠️ 조회 중 `viewer.log` 회전**: 현재 `viewer.log`(4,954,769B)는 **18:03:40 부터**만 담고, 이 사건 구간(17:50~18:03)은 **`viewer.log.1`(13:46:05~18:03:40)로 밀렸다.** 거기서 확보했다. 연쇄로 **직전 `viewer.log.3`(09-07 18:41)은 소실**됐다 — `.1`~`.3` 3세대만 보관하므로 회전 1회마다 가장 오래된 세대가 사라진다. #428 에서 17:16 발췌를 먼저 보전한 것이 결과적으로 옳았다.
- 보전: `.claude/field-20260908-1750/`(미추적) — `host.log_1745-1805.txt` 8,852행 1,628,598B sha256 `366d2a7c…847cfe50`(17:45:00~18:05:59), `viewer.log.1_1745-1803.txt` 15,673행 3,022,620B sha256 `45fb2c17…b578fd4d`(17:50:41~18:03:40). 원본 `host.log` 6,151,010B · `viewer.log.1` 16,789,393B.
- **고착의 핵심 관측**: `17:56:08.297 [abr] profile=high encode=`**`1280x720`**` bitrate=12000000 reason=static_recovery` — **프로파일과 비트레이트 예산은 high 12Mbps 로 돌아왔는데 해상도만 720p 그대로**다. 발췌 창(17:45~18:05)의 `[abr]` 행은 4건이 전부이므로 그 뒤로는 변경 시도조차 없다. 앞선 두 하향은 `17:55:17.298 high_to_mid_moderate`(1920x1080 9M) · `17:55:22.425 mid_to_low_moderate`(1280x720 6.6M), 중간 복귀는 `17:55:52.292 static_recovery mid 1280x720 9M`.
- **정밀화 3건(작업용 원문 대조)**: ① 뷰어 전환 시각이 정확히 잡힌다 — 마지막 `size=1920x1080` **17:55:21.557**, 최초 `size=1280x720` **17:55:21.763**, 이후 발췌 끝(18:03:40)까지 720p 499표본·1080p **0표본**("17:58~ 지속" 보다 강하다). ② `capturePreprocessed` 는 17:55:04.294 까지 **41 로 평탄**하다가 17:55:34.295 에 **632**, 17:58:34.293 에 1853, 18:05:34.296 에 **4378** 까지 계속 증가하며 `capturePreprocessFallbacks=0` 유지 — 전처리 축소 경로가 ABR 하향 시점에 시작돼 멈추지 않았고 폴백으로 우회한 것도 아니다. ③ 손실 지표는 필드값 그대로 표로 적었다(17:55:13~21 `dropped` 2~3, `dropPm` 37~81, `fecRecovered` 18~52, `nackSent` 2~12, `nackOn=1 nackExhausted=1`) — **UDP 손실률로 단정하지 않는다.**
- **코드 폐곡선(검증용 교차확인, 작업용 전량 재확인)**: ① `fit_size_preserving_aspect`(`host_bgra_scale.cpp:35-47`)는 `scale=min(box/src, box/src, **1.0**)`(`:43-44`) + `clamp_even_dim(..., 2, srcW/srcH)`(`:45-46`) 라 **확대 불가·source 로 clamp**. ② `ApplyTarget`(`host_encoder_manager.cpp:44-46`)은 `nominalEncodeW/H` 저장 뒤 **`encodeSourceW/H` 기준으로 fit** → source 가 720p 면 1080p 목표도 `min(1.5,1.5,1.0)=1.0` 으로 **1280x720 고정**. ③ 오염 지점은 `host_stage_encode_send_h264.cpp:170-171` 이 **프레임 payload 의 `w,h`** 를 `encodeSourceW/H` 에 대입하는 것.
- **핵심 비대칭(검증용 발견, 원문에서 성립 확인)**: 같은 블록에서 **대입(`:170-171`)은 무조건**인데 **교정 `ApplyTarget`(`:177`)은 `(refit != active) && !aspectClose`(`:172`)일 때만** 실행된다. `aspectClose` 는 `|refitAspect - activeAspect| <= activeAspect * 0.02`(`:166-169`)이고 **1280x720 과 1920x1080 은 둘 다 16:9 라 차이가 0** → 참. 즉 스래싱 방지 가드가 하필 이 경우 **source 만 낮추고 교정을 건너뛴다**. `nominalEncodeW/H` 의 ratcheting-down 방지(`host_encoder_manager.cpp:42-43` 주석)도 fit 기준인 source 자체가 오염돼 무력화된다. **대비 경로** `EncoderState::ApplyConfirmedCaptureGeometry`(`:111-142`)는 `:131-133` 주석대로 "확정된 변경에는 aspectClose 건너뛰기가 없다 — 같은 종횡비의 더 작은 source 도 activeEncode 를 줄여야 upscaling 을 피한다" 며 조건 없이 재적용한다. **같은 문제를 이미 알고 한쪽 경로에서만 막아 둔 셈**이다.
- **작업용이 추가 확인한 것 2건**: ① `encodeSourceW` 쓰기 지점은 저장소 전체에 **정확히 3곳** — `host_startup_graphics.cpp:330`(원본 geometry) · `host_encoder_manager.cpp:124`(확정 geometry, 예외 없음) · `host_stage_encode_send_h264.cpp:170`(payload, 무조건). 오염 경로가 세 번째 하나로 좁혀진다. ② `w`/`h` 출처는 `TickContext::w/h`(`host_main_loop.hpp:157-158`) ← **`host_stage_pop_frame.cpp:276-277` 의 `res.frame.width/height`**. "전처리로 축소된 payload 가 새 원본이 된다" 는 서술이 코드로 확인된다.
- **검증용 서술 대비 정정 1건**: **`OnSourceSizeChanged` 심볼은 저장소에 없다.** 실제는 `EncoderState::ApplyConfirmedCaptureGeometry`(`host_encoder_manager.cpp:111-142`)이고 `encodeSource` 대입은 `:123-124` 가 아니라 **`:124-125`** 다. 대비 구조 지적 자체는 정확하다. 나머지 4개 인용(`host_bgra_scale.cpp:43-46`, `host_encoder_manager.cpp:44-46`, `host_stage_encode_send_h264.cpp:170-171`, `host_startup_graphics.cpp:330`)은 원문과 일치한다.
- **판단 한계(약화 없이 반영)**: **런타임 `encodeSource` 직접 관측 로그가 없다** — 보인 것은 ① 코드상 폐곡선 성립과 ② 실기 관측이 그와 정합한다는 것뿐이며 **정합은 일치이지 증명이 아니다**. assembly/FEC/NACK 를 UDP 손실률로 단정 금지 · `moderate` 판정 원인은 snapshot 시차로 단정 금지 · **무조건 upscale 허용은 해법 아님**(낮은 원본을 확대하게 됨, 방향 후보는 "원본 geometry 를 source 의 진실로 유지 → readback 출력 재설정" 이며 이 task 에서 구현하지 않음) · 17:16 건과 다른 사건 · 양단 시계 미정렬이라 선후 인과 해석 금지.
- **미확인 6건**: 런타임 encode 변수 실측 · 전처리 경로의 진입 조건 · moderate 스냅샷의 시점 · 사용자 조작과의 관계 · 17:55:21.763(뷰어)과 17:55:22.425(호스트)의 선후(시계 미정렬) · 0.2.103 이전 재현 여부.
- 검증: **빌드·테스트 실행 없음**(문서·교차확인 전용, qwinsta 불필요). 제품 코드·CMake·gradle 변경 0. 라이브 Host/Stream/Viewer 재시작·화질설정 변경·종료 없음, 설치·배포·push·새 캡처 없음. NAS 는 read-only 조회만. 자격증명 미기재.
- 변경 파일: `docs/field_test_2026-09-08_0.2.104_720p.md`(신규), `docs/full_code_audit_2026-09-08.md`(HN13), `docs/history.md`, `docs/구현계획.md`, `docs/작업목록.md`.
- 상태: 검증용 검사 전 — **완료 아님.** 수정은 별도 위임 사항이다.

### 432) 2026-09-08 720p 고착 문서 강조 보강 2건 — 복구 시도 부재의 의미, encodeSource 쓰기 3곳의 무게 (검증용 remote#0jkgf453 회신 반영)
- 배경: #431 커밋(`c81a610`) 뒤 검증용의 중간보고 회신이 도착했다. 코드 정정(`OnSourceSizeChanged` 심볼 부재)과 추가 확인 2건, 정밀화 3건을 **전부 채택**하면서 그중 둘을 더 눈에 띄게 배치하라고 했다. **관측값·판단 한계는 하나도 바뀌지 않았고, 원인 단정도 추가되지 않았다.**
- **보강 ① (4절)**: `[abr]` 행이 창 전체에 4건뿐이고 17:56:08 이후 변경 시도가 없다는 관측에, 그것이 사용자 증상("복구되지 않음")과 직결되는 이유를 붙였다 — ABR 은 이미 최상위(high·12Mbps)라 **더 올릴 것이 없고**, 그런데 실제 인코드는 720p 라 **시스템이 여기는 상태와 화면이 어긋난 채 로그에 재시도가 한 건도 없다**. 검증용 요구대로 **사실 진술로만** 적었다("스스로 벗어날 수 없다"를 코드로 증명한 것이 아니라 이 창의 로그에 복구 시도가 0건이라는 관측이며, 7절 한계가 그대로 적용됨을 명시).
- **보강 ② (6.3)**: `encodeSourceW` 쓰기 지점이 저장소 전체에 정확히 3곳뿐이라는 사실이 **이 문서에서 가장 강한 코드 근거**임을 명시했다 — `encodeSource` 가 fit 의 기준이므로 그 값이 어디서 바뀌는지가 전부인데 후보가 셋으로 닫히고, 1번은 기동 시 1회·2번은 교정을 건너뛰지 않으므로 **오염 가능 경로가 3번 하나로 좁혀진다**("다른 데서 바뀌었을 수도" 라는 여지가 코드 수준에서 남지 않는다). 동시에 **어느 경로가 실제 실행됐는지의 증거는 아니라는 점**(런타임 관측 부재)을 같은 자리에 붙였다.
- 검증용이 확인해 준 것: `OnSourceSizeChanged` 는 저장소에 없고(grep 0건) 본문만 읽고 **이름을 지어냈다**고 정정 — #431 이 실제 심볼 `ApplyConfirmedCaptureGeometry`(`:111~`, 대입 `:124-125`, 주석 `:131-133`, 호출 `:134`)로 적은 것이 맞다. 발췌를 `viewer.log.1` 에서 뜬 판단도 맞고(검증용의 "17:5x 는 host.log 쪽" 힌트는 호스트에만 맞고 뷰어에는 틀렸다), 원본이 계속 커지는 중이라 **재조회 없이 기존 발췌로만** 작업하라는 지시를 따랐다.
- 검증: 빌드·테스트 없음(문서 전용). **NAS 재조회 없음.** 제품 수정·설치·라이브 조작·서버 배포·push 없음.
- 변경 파일: `docs/field_test_2026-09-08_0.2.104_720p.md`, `docs/history.md`.
- 상태: 검증용 검사 전 — **완료 아님.** HN13 수정은 여전히 미위임이다.

### 433) 2026-09-08 업데이트 step2-1 — 버전 비교 계약 + 3런타임 공통 테스트 벡터 (설계 2(d) 모델, 검증용 remote#0jkgf453 위임)
- 목적: 설계(`48f09ec`) 2(d) 확정대로 버전 비교를 **계약 + 공유 벡터**로 고정한다. "같은 헤더를 쓰자" 가 불가능한 이유는 비교가 **서버 JS · Windows C++ · Android Kotlin** 세 런타임에서 각각 일어나기 때문이고, 그래서 공유하는 것은 코드가 아니라 **정답표**다. 기존 `compare_versions()` 는 설치기 익명 네임스페이스에 갇혀 있었고 **단위 테스트가 0건**이었다.
- **계약 파일(신규)**: `apps/shared/version_compare_vectors.txt` — 규칙 6개를 파일 머리에 적고 그 아래 `left|right|expect` 형식으로 **46개 벡터**. 규칙: ① `.` 로 구분된 숫자 성분 ② 성분은 **숫자 비교**(2 < 10) ③ 누락 성분은 0("0.1" == "0.1.0") ④ 선행 0 무의미("01" == "1") ⑤ **숫자도 `.` 도 아닌 첫 문자에서 비교 중단** — 접미사는 어느 방향으로도 우열을 만들지 못한다("1.2" == "1.2-beta") ⑥ 성분은 **2147483647 에서 포화**. JSON 이 아니라 줄 단위 텍스트로 둔 것은 세 런타임이 파서 의존 없이 읽게 하기 위함이다.
- **⑥은 의도된 동작 변경이다**(추출 전 C++ 구현은 오버플로에서 wrap 했다). 세 런타임을 같은 방식으로 넘치게 만들 수 없어 포화로 고정했고, 도달하려면 성분이 2^31 을 넘어야 하므로 **실제 버전 문자열로는 재현 불가능한 입력**이다. 벡터 파일 주석에 그대로 적었다.
- **C++**: `apps/native_poc/src/version_compare.hpp` 신설(헤더 온리). `installer_main.cpp` 의 file-local 구현을 삭제하고 이 헤더를 쓴다 — **③ 추출은 C++ 안에서만**이라는 확정 그대로. 문자열 리터럴(`kProductVersion` 은 `const wchar_t[8]`)이 call site 라 템플릿 추론이 안 되므로 `std::wstring_view`/`std::string_view` **두 오버로드 + 내부 템플릿 core** 구조로 했다. 설치기는 `/utf-8` 없이 컴파일되므로 헤더는 **ASCII 전용**으로 유지(C4819 회피).
- **JS**: `apps/directory/version_compare.js` 신설. 비문자열 입력은 던지지 않고 빈 문자열로 취급한다(와이어에서 온 manifest 필드라 "버전 없음" 은 예외가 아니라 "더 오래됨" 이어야 한다). JS 는 원래 오버플로가 없으므로 **포화 clamp 를 명시**해야 다른 런타임과 어긋나지 않는다.
- **Kotlin**: `VersionCompare.kt` 신설(`app/src/main/java/com/remote60/androiddirect/`). `Char.isDigit()` 은 비-ASCII 숫자를 받아들여 계약과 다르므로 **쓰지 않고** `in '0'..'9'` 로 판정한다. null 은 빈 문자열 취급.
- **테스트 3종이 같은 벡터 파일을 읽는다**: `remote60_version_compare_test`(신규 CMake 타깃, 벡터 경로를 `REMOTE60_VERSION_VECTORS_PATH` 로 주입) · `apps/directory/test/version_compare_test.js`(`run.js` 최상단에 배선 — 서버가 필요 없어 가장 먼저 돈다) · `VersionCompareTest.kt`(`app/src/test/`, JVM 단위 테스트. gradle 에 `testImplementation("junit:junit:4.13.2")` 추가 — **APK 에는 들어가지 않는다**). 세 테스트 모두 **반대칭성**(인자를 뒤집으면 부호가 뒤집힌다)을 벡터마다 추가로 단정하고, **벡터 파일이 비면 실패**하도록 했다(조용한 통과 방지).
- 검증(RDP 세션 없음 — `qwinsta` 확인: console 만 Active, rdp-tcp 는 Listen):
  - C++ `remote60_version_compare_test` **139 checks / 46 vectors / 0 failed**, exit 0
  - JS `version_compare_test.js` **95 checks / 46 vectors / 0 failed**, exit 0
  - Kotlin `:app:testDebugUnitTest` **tests=2 failures=0 errors=0**(`matchesSharedVectors`, `nullComparesAsMissing`), BUILD SUCCESSFUL
  - 디렉터리 스위트 전체 `node test/run.js` **exit 0, 전 구간 ALL PASS**(신규 항목이 최상단에서 먼저 통과)
  - `remote60_installer` 재빌드 exit 0 — 추출 뒤에도 설치기가 그대로 링크된다
- 중간 사고 1건(기록): `run.js` 배선을 bash heredoc 안의 Python 으로 넣다가 `\n` 이 실제 개행으로 치환돼 문자열 리터럴이 깨졌다(`SyntaxError`). Edit 로 정정 후 스위트 통과. 메모리에 이미 있는 heredoc 백슬래시 주의사항을 또 밟았다.
- 변경 파일: `apps/shared/version_compare_vectors.txt`(신규) · `apps/native_poc/src/version_compare.hpp`(신규) · `apps/native_poc/src/version_compare_test.cpp`(신규) · `apps/native_poc/CMakeLists.txt` · `apps/native_poc/installer/installer_main.cpp` · `apps/directory/version_compare.js`(신규) · `apps/directory/test/version_compare_test.js`(신규) · `apps/directory/test/run.js` · `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/VersionCompare.kt`(신규) · `apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/VersionCompareTest.kt`(신규) · `apps/android_direct_client/app/build.gradle.kts` · `docs/history.md` · `docs/구현계획.md`.
- 설치본 생성·설치·라이브 조작·서버 배포·push 없음. 운영 서명키 관련 작업 없음.
- 상태: step2 3항목 중 1번 완료. 다음은 manifest 파서 + 서명 검증 seam. **전체 완료 아님.**

### 434) 2026-09-08 업데이트 step2-2 — manifest 파서 + 서명 검증(ECDSA P-256/SHA-256, CNG), 검증-후-파싱 순서를 타입으로 강제 (설계 4.1~4.3, 검증용 remote#0jkgf453 지시)
- 목적: 설계 4.1.2/4.3 확정대로 manifest 를 **detached 서명 + 내장 공개키**로 검증하고, **서명이 확인되기 전에는 문서의 어떤 필드도 믿지 않는다**(버전 비교조차 하지 않는다)는 순서를 구현한다. 자체 암호 구현 없음 — 곡선·해시·검증 전부 **BCrypt/CNG**.
- **순서를 주석이 아니라 타입으로 강제했다**: 파싱된 필드는 `VerifiedManifest` 를 통해서만 닿을 수 있고, 그 생성자는 private 이며 `load_manifest()` 만 친구다. **인스턴스가 존재한다는 것 자체가 서명이 통과했다는 증거**다. 버전 비교(`is_newer_than`)도 그 객체의 멤버라 서명 없이는 호출 경로가 없다. 리팩터가 주석을 남기고 동작을 죽이는 형태를 구조로 막았다.
- **관측 가능한 순서 테스트**: 문서가 **동시에** 깨져 있고 서명도 나쁘면 결과는 반드시 `SignatureInvalid` 여야 한다 — 파싱이 먼저 돌았다면 `Malformed` 이 나오기 때문이다. 반대로 같은 깨진 문서에 서명이 통과하면 `Malformed` 이 나오는 것까지 확인해, 앞 케이스가 "문서가 깨져서" 가 아니라 "서명 때문에" 결정됐음을 보인다.
- **형식(신규 `apps/shared/update_manifest/README.txt`)**: 줄 단위 `key=value` UTF-8. JSON 이 아닌 이유 두 가지 — ① **서명이 문서의 정확한 바이트를 덮으므로 정규화 단계가 없는 형식이 서명자·검증자 사이에서 어긋날 여지가 없다** ② C++/JS/Kotlin 이 파서 의존 없이 읽는다. 필드: `schema`(1만 지원) · `platform` · `version` · `artifact` · `size`(0 불가) · `sha256`(**소문자 64자만**, 대문자는 접지 않고 거부) · `versionCode`(android). **모르는 키는 무시**한다 — 새 서버가 필드를 추가해도 구 클라이언트가 벽돌이 되면 안 되고, 비호환 변경은 `schema` 로 막는다.
- **서명**: ECDSA P-256/SHA-256, **raw r||s 64바이트(IEEE P1363, DER 아님)**, 공개키는 raw X||Y 64바이트(0x04 태그 없음). `update_signature.cpp` 는 `BCryptOpenAlgorithmProvider`/`BCryptHash`/`BCryptImportKeyPair`/`BCryptVerifySignature` 만 쓰고 핸들은 RAII 로 닫는다. **모든 실패는 false 한 가지로 수렴한다** — "위조" 와 "확인 불가" 를 호출자가 다르게 처리할 수 없고, 둘 다 "우리 것이라고 알 수 없다" 라는 같은 뜻이기 때문이다.
- **릴리스 키는 비어 있다(의도)**: `trusted_public_key_hex()` 가 빈 문자열이라 `default_verifier()` 는 **아무것도 받아들이지 않는다**. 운영 서명키 생성·보관·교체는 별도 승인 사항이고(설계 4.5), 우연히 뭔가를 검증하는 placeholder 는 키가 없는 것보다 나쁘다. 테스트가 **"릴리스 키가 없는 동안 default_verifier 는 유효한 테스트 서명조차 거부한다"** 를 단정하므로, 누군가 placeholder 를 넣으면 그 테스트가 깨진다.
- **테스트 벡터(신규)**: `apps/shared/update_manifest/` 의 `test_manifest.txt` / `.sig` / `test_public_key.txt`. **일회용 P-256 키로 한 번 서명하고 그 개인키는 버렸다** — 저장돼 있지 않고 릴리스 키가 아니다. 생성기는 `.claude/`(미추적)에 두었고 자체 검증(valid=true, tampered=false)을 통과한 뒤 기록했다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen`(Active 인 rdp-tcp#N 없음) → RDP 접속 아님.** 이번 범위는 순수 로직 + CNG 라 캡처/GDI/e2e 계열은 실행하지 않았다.
  - `remote60_update_manifest_test` **41 checks / 0 failed**, exit 0. 내역: 실제 CNG 검증(정상 서명 수락 / 문서 1바이트 변조 거부 / 서명 변조 거부 / 다른 키 거부 / 짧은 서명·키 거부 / 빈 입력 거부) · 순서 3종(깨진문서+나쁜서명→SignatureInvalid, 깨진문서+좋은서명→Malformed, 나쁜 서명이면 manifest 부재로 비교 자체가 불가) · 필드 검증 9종(schema 누락/미지원, platform 불일치, size 0, sha256 대문자·길이, version 누락, **모르는 키 무시**, 주석·빈 줄 무시) · 내장 키 부재 확인 2종.
  - 빌드 exit 0. **수정 전 FAIL 은 없다** — 전부 신규 코드이고 기존 동작을 고친 것이 아니다.
- 미검증: JS·Kotlin 쪽 manifest 파서/검증기는 **이번 범위가 아니다**(C++ 소비자 경로만). 실제 서버가 이 형식을 발행하는 배선도 아직 없다(설계 4.4, 후속). 릴리스 키가 없으므로 **실제 배포 아티팩트에 대한 검증은 한 번도 수행되지 않았다.**
- 변경 파일: `apps/native_poc/src/update_manifest.hpp`·`update_manifest.cpp`·`update_signature.hpp`·`update_signature.cpp`·`update_manifest_test.cpp`(전부 신규) · `apps/shared/update_manifest/README.txt`·`test_manifest.txt`·`test_manifest.sig`·`test_public_key.txt`(신규) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음. 운영 서명키 관련 결정 없음.
- 상태: step2 3항목 중 2번 완료. 다음은 업데이터 상태기계 골격(seam·결정론 테스트까지, 실제 종료·파일 교체 미연결). **전체 완료 아님.**

### 435) 2026-09-08 업데이트 step2-3 — 업데이터 상태기계 골격, 순서·실패 규칙을 seam 위에서 고정 (설계 3.1/3.3/3.6, 검증용 remote#0jkgf453 지시)
- 목적: 설계 3.1 의 단계와 3.6 완료조건을 **실제 프로세스 종료·파일 교체를 붙이기 전에** 코드로 고정한다. 지시대로 `UpdateEffects` seam 까지만 만들고 **production 구현은 붙이지 않았다.**
- **왜 이 순서인가**(파일 주석에 그대로): 제대로 정해야 하는 것은 대부분 **순서와 실패 처리**다 — quiesce 가 끝나지 않았는데 swap 을 시도하는가, 재실행 실패가 멀쩡한 설치를 되돌려야 하는가, 서버가 죽으면 동작 중인 제품이 망가질 수 있는가. **실제 종료·교체가 붙고 나면 이 실패 경로들을 원할 때 재현하는 것이 거의 불가능해진다.** 그래서 위험한 절반보다 이쪽을 먼저 만들었다.
- **상태**: `Idle → CheckRequested → Evaluate → Download → Verify → Prepare → Quiesce → Swap → Register → Relaunch → Health → Done`, 실패 시 `Rollback`. 결과는 6종으로 구분 — `NothingToDo`(할 일 없음, 실패 아님) · `Updated` · **`UpdatedButNotRelaunched`** · **`AbandonedBeforeSwap`**(디스크 무손상) · `RolledBack` · **`RollbackFailed`**(설치가 불일치일 수 있는 유일한 결과라 별도 이름).
- **고정한 규칙 4개(각각 테스트 있음)**:
  - **락은 절대 기다리지 않는다**(설계 3.3). 못 잡으면 `AcquireLock` 하나만 호출하고 끝 — manifest 조회조차 하지 않는다. 업데이트는 미룰 수 있고, 두 프로세스가 같은 디렉터리를 교체하는 것은 미룰 수 없다.
  - **서버가 죽어도 동작 중인 설치를 건드리지 못한다.** manifest 를 못 받으면 `NothingToDo` 이며 실패가 아니다. 설계의 "서버 연결 장애만으로 무한롤백 금지" 를 이 형태로 구현했다.
  - **Prepare/Quiesce 실패는 강제 종료로 승격하지 않는다.** 그대로 물러나고(`AbandonedBeforeSwap`), **검증된 다운로드는 다음 시도를 위해 남긴다**(`discardCount==0` 으로 단정). 디스크는 손대지 않았으므로 물러나는 비용이 0이다.
  - **Relaunch 실패는 롤백 사유가 아니다.** 파일은 신버전이고 일관돼 있으며 사용자는 시작메뉴로 켤 수 있다 — 좋은 설치를 "스스로 재시작하지 못했다" 는 이유로 되돌리는 쪽이 더 나쁘다. 대신 `UpdatedButNotRelaunched` 로 **드러나게** 보고한다.
- **서명-후-비교 순서는 여기서 다시 구현하지 않았다.** `load_manifest` 의 타입이 이미 강제하므로(#434) 상태기계는 그것을 호출만 한다 — 같은 규칙의 사본은 그 규칙이 썩을 자리를 하나 더 만드는 것이다. 테스트는 나쁜 서명일 때 **`InstalledVersion()` 조차 호출되지 않음**을 단정해 비교가 실제로 일어나지 않았음을 보인다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen`(Active 인 rdp-tcp#N 없음) → RDP 미접속.** 캡처/GDI/e2e 계열은 실행하지 않았다.
  - `remote60_update_state_machine_test` **57 checks / 0 failed**, exit 0. 내역: 정상 경로가 12단계를 **정확한 순서로** 방문 · `NothingToDo` 6종(락 점유·서버 불통·나쁜 서명·같은 버전·더 새 버전 설치됨·플랫폼 불일치) · `AbandonedBeforeSwap` 4종(다운로드·검증·prepare·quiesce) + 각각 **디스크 무손상**과 staging 처리 · 롤백 4종(swap·register·health·rollback 자체 실패) · relaunch 실패가 롤백이 아님 · **8개 변형 전부에서 락이 물려 있지 않음**.
  - 신규 3종 동시 재실행: version_compare **139/0**, update_manifest **41/0**, state_machine **57/0** — 전부 exit 0.
  - **수정 전 FAIL 없음** — 전부 신규 코드다.
- **추가 기록(검증용 요청)**: #433 의 성분 **wrap → 포화** 변경은 세 런타임 일치를 위한 선택인 동시에 **부수적 안전 개선**이다. 옛 `left = left * 10 + digit`(unsigned long)은 넘치면 **큰 버전이 작은 버전으로 뒤집혀** 업데이트 판정에서 최악의 오류 방향을 만든다. 포화는 절대 뒤집지 않고 **단조성을 지키며**, 최악이라야 터무니없는 값끼리 같다고 볼 뿐이다.
- **보고 정정(검증용 지적)**: #433 보고에서 부모를 `c81a610` 으로 적었으나 실제 `f0beef9^` 는 **`7fba419`** 다. 사슬 자체는 정상이며 앞으로 부모 해시를 정확히 싣는다.
- 미검증·미착수: `UpdateEffects` 의 **production 구현이 없다**(실제 락·다운로드·종료·교체·롤백 전부). 따라서 이 테스트는 **순서와 실패 규칙을 검증한 것이지 실제 업데이트가 동작함을 보인 것이 아니다.** 진입점(트레이 메뉴·클라 시작 시 확인)도 아직 배선되지 않았다.
- 변경 파일: `apps/native_poc/src/update_state_machine.hpp`·`update_state_machine.cpp`·`update_state_machine_test.cpp`(신규) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음. 운영 서명키 관련 결정 없음.
- 상태: **step2 3항목 전부 구현 완료**(#433 계약·벡터 / #434 manifest·서명 / #435 상태기계). 검증용 검사 대기 — **전체 완료 아님.**

### 436) 2026-09-08 업데이트 step3 — `UpdateEffects` production 구현(락·다운로드·검증·정지·교체·롤백), 격리를 링크 단위로 강제 (설계 3.1~3.6, 검증용 remote#0jkgf453 보류 해제)
- 목적: seam 뒤에 실제 구현을 채운다. 단 **사용자가 0.2.104 실기 중이고 이 PC 에서 제품이 실제로 돌고 있다** — 착수 시점 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) 실행 중 확인. 그래서 검증용이 건 안전 제약 4개를 **관례가 아니라 구조로** 구현했다.
- **격리 ① 링크 단위**(가장 강한 것): 이름으로 제품 프로세스를 찾는 코드는 `update_process_targets.cpp` 한 파일에 격리하고 **테스트 타깃에 링크하지 않았다.** 테스트는 그 심볼을 호출할 수 없다 — 바이너리에 없기 때문이다. **실증**: 테스트 exe 에서 `GNLinkHost`/`GNLinkStream`/`GNLinkViewer`/`GNLinkClient` 문자열이 **각 0회**, 같은 검색이 `remote60_update_process_targets.lib` 에서는 발견됨. 이 파일은 소비자가 없어 조용히 썩지 않도록 **static 라이브러리로 빌드만** 해 둔다.
- **격리 ② 기본값 없음**: `UpdateEffectsConfig` 의 어떤 필드도 실제 경로·이미지 이름으로 기본값을 갖지 않는다. `validate()` 가 미설정 필드를 거부하고, `AcquireLock()` 이 그 검사를 먼저 하므로 **설정이 불완전하면 아무것도 시작되지 않는다**(테스트가 필드 10개를 하나씩 비워 각각 확인). 추가로 **stagingDir 이 installDir 안이면 거부**한다(설계 3.2).
- **격리 ③ 임시 디렉터리만**: 스테이징·교체·롤백 테스트 전부 `%TEMP%` 하위. 테스트가 시작하면서 **"install 경로에 Program Files 가 없다"** 를 스스로 단정한다.
- **격리 ④ 네트워크 없음**: 아티팩트 다운로드는 주입된 함수가 고정 바이트를 쓴다. 실제 디렉터리 서버·NAS 로 나가지 않았다.
- **구현 내용**:
  - **락**(설계 3.3): 명명 뮤텍스 + `WaitForSingleObject(h, 0)` — **절대 기다리지 않는다.**
  - **다운로드/검증**: 스테이징 전 이전 잔재를 지우고(짧은 쓰기가 완전한 것으로 오인되지 않도록), 검증은 **크기 먼저 → SHA-256**(BCrypt `BCryptCreateHash`/`HashData`/`FinishHash` 스트리밍).
  - **정지**(설계 3.2): `PrepareForSwap` 이 주입된 PID 목록에 각각 정지를 **요청**하고, `Quiesce` 는 **그 정확한 PID 를 `OpenProcess(SYNCHRONIZE)` 로 기다린다.** 이름 sweep 없음, `/T` 없음, **`TerminateProcess` 없음** — 응하지 않는 대상은 타임아웃으로 실패하고 상태기계가 `AbandonedBeforeSwap` 으로 물러난다.
  - **교체**(원장 **I01** 대응): 2단계다. ① 기존 파일을 전부 `.gnlink-old` 로 **먼저 옮기고** ② 새 파일을 넣는다. 어느 단계든 실패하면 **옮긴 것만 정확히 되돌린다.** `.gnlink-old` 백업은 **등록(`RegisterInstall`) 성공 뒤에야** 지운다 — 그전에 지우면 복구 가능한 실패를 복구 불가능한 실패로 바꾸는 것이다.
  - `request_process_stop` 은 WM_CLOSE → 콘솔 CTRL_BREAK 순으로 **요청만** 하고, 둘 다 안 되면 false 를 돌려 업데이트를 포기시킨다(스트리밍 중인 호스트를 죽여 가며 업데이트할 이유가 없다).
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen`(Active 인 rdp-tcp#N 없음) → RDP 미접속.**
  - `remote60_update_effects_test` **64 checks / 0 failed**, exit 0. 내역: 설정 검증 13종 · 락 3종(**두 번째 획득은 별도 스레드에서** — Windows 뮤텍스는 소유 스레드에 재진입 가능해 같은 스레드 재획득은 아무것도 증명하지 못한다) · 다운로드/검증 6종(크기·해시 불일치, 실패한 fetch 의 잔재 제거) · **교체·롤백 12종** · 프로세스 정지 6종 · 종단 6종.
  - **I01 회귀 핵심**: 두 payload 중 **두 번째를 공유 0 으로 잠근** 상태에서 교체를 시도 → 교체 실패, **첫 번째 payload 가 구버전 그대로**(혼합버전 없음), 백업 잔재 없음. 현행 설치기는 이 상황에서 앞 파일을 이미 덮은 뒤 실패한다.
  - **강제 종료 안 함 회귀**: 응하지 않는 더미를 두고 `Quiesce` 가 실패한 뒤 **그 더미가 여전히 살아 있음**을 단정한다.
  - 업데이트 관련 4종 동시 재실행 전부 exit 0: version_compare **139/0** · update_manifest **41/0** · state_machine **57/0** · effects **64/0**.
  - **테스트 전후로 라이브 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) 의 PID 가 그대로임을 확인**했다.
  - **수정 전 FAIL 없음** — 신규 코드다. (작성 중 테스트 자체 결함 2건은 있었다: 같은 스레드 뮤텍스 재진입, 공유 0 으로 잠근 파일을 테스트가 스스로 읽으려 한 것. 둘 다 제품이 아니라 테스트를 고쳤다.)
- **미검증(중요)**: 이 결과는 **격리 하네스 안에서의 검증**이다. **실제 제품 프로세스 종료·실제 `%ProgramFiles%\GNLink` 교체·실제 서버 다운로드는 한 번도 수행하지 않았다** — 승인된 계획의 "live 실기 환경에서는 실제 제품 중지/교체 금지, 사용자 후속 실기로 남김" 그대로다. `enumerate_product_processes`/`request_process_stop` 은 **컴파일만 됐고 실행된 적이 없다.** 진입점(트레이·클라 시작 시 확인) 미배선, `FetchManifest` 는 주입된 문자열이라 **HTTP 경로 미구현**(설계 4.1.1 의 WinHTTP 클라이언트 미착수), `RegisterInstall`/`Relaunch`/`HealthCheck` 는 주입 콜백이라 **production 구현 없음**. 릴리스 키 부재로 실제 아티팩트 서명 검증 0회.
- 변경 파일: `apps/native_poc/src/update_effects.hpp`·`update_effects.cpp`·`update_process_targets.hpp`·`update_process_targets.cpp`·`update_effects_test.cpp`(전부 신규) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push·새 캡처 없음. 운영 서명키 관련 결정 없음.
- 상태: 검증용 검사 대기 — **전체 완료 아님.** 실기 검증은 사용자 후속 실기 몫이다.

### 437) 2026-09-08 업데이트 step3 보강 — PID 재사용 방어, 모의 대체를 실제 OS 동작으로 교체 (Codex 확정 + 검증용 remote#0jkgf453 보강 지시)
- 배경: 검증용의 보강 지시 — **"모의 효과 순서 테스트만으로 실제 OS 검증을 대체하지 말 것"**. 격리는 **대상을 분리하라**는 뜻이지 **가짜로 대체하라**는 뜻이 아니다. #436 이 격리를 강조하다 일부를 콜백으로 대체해 둔 것을 실제 OS 동작으로 바꿨다. 그리고 프로세스 대상 식별을 PID 단독에서 **실행 경로 + 생성 시각 + 핸들**로 강화하라는 지시를 반영했다.
- **PID 재사용 방어(실제 결함 수정)**: `ProcessTarget{pid, imagePath, creationTime}` 을 도입했다. PID 는 재사용되므로 **열거와 종료 사이에 원래 프로세스가 죽고 다른 프로세스가 그 번호를 물려받으면 무관한 프로세스를 죽인다.** 이제 `Quiesce` 와 `request_process_stop` 이 핸들을 연 뒤 `GetProcessTimes`/`QueryFullProcessImageNameW` 로 **신원을 대조**하고, 불일치면 "이미 사라졌다" 로 처리한다(그것이 기다리던 결과이므로 오류가 아니다). 생산 열거기는 **자기 자신을 대상에서 제외**한다.
- **레지스트리·서비스 격리(지시 추가분)**: `UpdateEffectsConfig` 에 `registryRoot`·`serviceName` 을 **기본값 없이** 추가했다. 지금은 아무도 읽지 않지만, 앞으로 `RegisterInstall` 을 쓸 때 실제 Uninstall 레지스트리 키와 `GNLinkSecureInput` 서비스 이름을 **하드코딩할 수 없게** 만든다 — 미설정이면 `validate()` 가 거부한다.
- **모의 → 실제 OS 로 교체한 시나리오**:
  - **재실행 실패**: 람다가 false 를 돌려주는 것이 아니라, 존재하지 않는 경로로 **실제 `CreateProcessW`** 를 호출해 실패시킨다 → `UpdatedButNotRelaunched`, 신버전 파일 유지(롤백 안 함) 확인.
  - **롤백 실패**: 교체 성공 뒤 복구 대상 파일을 **공유 0 으로 실제로 잠가** `MoveFileEx` 를 OS 수준에서 실패시킨다 → 롤백이 실패를 보고하되 **복구 가능했던 파일은 복구된 것**까지 확인(첫 문제에서 전부 포기하면 신버전이 더 많이 남는다).
  - **동시 실행**: 스레드가 아니라 **실제 두 번째 프로세스**(같은 exe 를 `--hold-lock` 로 재실행)가 명명 뮤텍스를 쥔 상태에서 `AcquireLock` 실패 → 상태기계가 `NothingToDo`, 다운로드 미진입, 그 프로세스 종료 후 락 재획득까지 확인. 파이프로 "held" 를 받고 진행해 sleep 추측을 없앴다.
  - **PID 재사용**: 살아 있는 프로세스를 **틀린 생성 시각**으로 기술해 `Quiesce` 가 즉시 성공하고(타임아웃을 기다리지 않음, 300ms 미만 실측) **그 프로세스를 건드리지 않음**을 확인. **대조군**으로 같은 프로세스를 **올바른 신원**으로 주면 실제로 기다리다 실패하는 것까지 확인해, 앞 케이스가 다른 이유로 통과하지 않았음을 보인다.
  - `process_identity_matches` 직접 검사 4종(자기 자신 일치 / 생성 시각 다름 / 경로 다름 / 신원 미확보는 절대 불일치).
- **권한 흐름**: 테스트가 자기 토큰의 elevation 을 읽어 **"NOT elevated 로 실행됨"** 을 출력하고, 설계의 "호스트가 띄운 업데이터는 2차 UAC 불필요" 주장이 **여기서 검증되지 않았음**을 명시한다. 관리자 권한 실증은 미검증으로 남긴다(지시대로 막히지 않고 진행).
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_effects_test` **98 checks / 0 failed**(#436 의 64 → 98), exit 0.
  - 4종 동시: version_compare **139/0** · manifest **41/0** · state_machine **57/0** · effects **98/0**, 전부 exit 0.
  - **테스트 전후 라이브 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변 확인.**
- 미검증(그대로): **실제 제품 프로세스 종료 0회 · 실제 설치 경로 교체 0회 · 실제 네트워크 다운로드 0회 · 관리자 권한 실행 실증 없음 · 실제 레지스트리/서비스 등록 없음**(`RegisterInstall` production 구현 자체가 없다). `enumerate_product_processes`/`request_process_stop` 은 컴파일만 됐고 실행된 적 없다.
- 변경 파일: `apps/native_poc/src/update_effects.hpp`·`update_effects.cpp`·`update_process_targets.hpp`·`update_process_targets.cpp`·`update_effects_test.cpp` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. 다음은 자율 진행 범위(JS/Kotlin manifest 소비자 · 서버 발행 배선 · UI 진입점). **전체 완료 아님.**

### 438) 2026-09-08 업데이트 step4-1 — 업데이트 전용 WinHTTP HTTPS 클라이언트 (설계 4.1.1, 검증용 remote#0jkgf453 자율 진행 범위)
- 목적: 설계 4.1.1 확정대로 **업데이트 경로 전용 HTTPS 클라이언트를 신설**한다. 기존 `directory_client` 는 손대지 않는다 — 그쪽은 POST 전용, 응답 64KB 절단, `https://` 명시 거부(`directory_client.cpp:83-93`)라 이 일을 할 수 없고, 제품 전체가 의존하는 코드를 업데이트 때문에 갈아엎을 이유가 없다.
- **보안 결정을 순수 함수로 뺐다**: `parse_https_url()` 과 `redirect_is_allowed()` 는 I/O 가 없다. "http:// 를 받지 않는다" 와 "평문으로 내려가는 redirect 를 따르지 않는다" 는 확신이 필요한 두 규칙인데, WinHTTP 콜백 안에 묻어 두면 **서버를 세워야만 검증할 수 있다.** 함수로 두면 호출해서 검증한다.
- **어떤 설정으로도 하지 않는 것 3가지**(주석에 명시): 인증서 검증 비활성화(`WINHTTP_OPTION_SECURITY_FLAGS` 를 **아예 설정하지 않는다**) · 인증서 오류 후 진행 · **https→http redirect**(`WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP` 로 스택 차원에서도 강제). TLS 1.2 이상만 협상한다 — 업데이트 아티팩트는 관리자 권한으로 실행되므로 하향 협상할 이유가 없다.
- **URL 정책**: `http://`(대문자 포함)·스킴 없음·`ftp`·`file` 전부 거부. **URL 안의 자격증명 거부** — 한 호스트로 읽히고 다른 호스트로 해석되는 고전적 수법이라 파싱하지 않고 막는다(단, 첫 `/` 뒤의 `@` 는 경로의 일부라 허용). 포트 0·65535 초과·비숫자 거부.
- **크기 상한은 힌트가 아니라 강제**: manifest 는 수백 바이트이므로 그보다 훨씬 크다고 주장하는 것은 manifest 가 아니다. 초과 시 **자르지 않고 중단**한다 — 잘린 manifest 는 어차피 서명 검증에서 실패하는데, 여기서 멈추면 **이유가 남는다**. 아티팩트 다운로드는 실패 시 **부분 파일을 삭제**해 이후 단계가 완결된 다운로드로 오인하지 못하게 한다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_http_test` **36 checks / 0 failed**, exit 0. 스킴 거부 9 · URL 파싱 11 · redirect 정책 6 · 진입점의 http 거부 4(연결 시도조차 하지 않고 파일도 만들지 않음) · **실제 소켓 2**.
  - **실제 소켓 케이스**: 루프백 임의 포트에 **평문 HTTP 로 응답하는 리스너**를 띄우고 그것을 `https://` 로 지목한다 → 실패해야 하고, **본문이 비어 있음**까지 단정해 조용히 평문으로 말하지 않았음을 확인한다. 아무것도 비활성화하지 않고 통과한다 — 나중에 누군가 "이번만" 우회를 넣으면 **이 케이스가 엉뚱한 이유로 통과하기 시작**한다. 그리고 아무도 듣지 않는 포트는 `ConnectFailed` 로 구분된다(URL 문제로 보고되지 않는다).
  - 실제 디렉터리 서버·NAS 로는 나가지 않았다.
- 미착수·미검증: **실제 HTTPS 서버 대상 왕복 0회**(인증서를 신뢰 저장소에 넣지 않고는 검증을 약화시키지 않고 할 수 없어 하지 않았다 — 검증을 끄는 쪽은 선택지가 아니다). `UpdateEffects` 와의 배선도 아직이다(아래 보고 항목 참조).
- 변경 파일: `apps/native_poc/src/update_http.hpp`·`update_http.cpp`·`update_http_test.cpp`(신규) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: **차단 요인 1건을 검증용에 질의 중**(`RegisterInstall` 을 자체 구현할지 설치기 `/S` 재사용할지 — 후자는 업데이터의 2단계 안전 교체를 설치기의 안전하지 않은 교체가 우회하게 된다). 답이 오기 전까지 ②는 진행하지 않는다. **전체 완료 아님.**

### 439) 2026-09-08 업데이트 step4-2 — 서버 manifest 발행 라우트 + JS·Kotlin 소비자, 세 런타임이 같은 서명을 검증 (설계 4.3/4.4, 자율 진행 범위)
- 목적: manifest 를 **읽는 쪽 세 벌**(C++·JS·Kotlin)과 **내는 쪽 한 벌**(서버)을 맞춘다. 확정된 설계(4.3 스키마 / 4.4 엔드포인트)에 애매한 지점이 없어 질의 없이 진행했다.
- **핵심 성과 — 세 구현이 같은 아티팩트에 합의한다**: `apps/shared/update_manifest/` 의 **고정 벡터 한 벌**(문서·서명·공개키)을 C++·JS·Kotlin 테스트가 각각 검증한다. "각자 자기 자신과 일치한다" 가 아니라 **서로 다른 세 구현이 하나의 실제 서명을 같이 받아들이고 같은 변조를 같이 거부한다** — 이쪽이 훨씬 강한 진술이다.
- **JS**(`apps/directory/update_manifest.js`): node `crypto` 로 검증(raw X||Y → JWK 로 키 구성, `dsaEncoding: 'ieee-p1363'` 으로 raw r||s). 파싱 규칙·상태 이름을 C++ 과 **한 글자씩 맞췄다**(양쪽 로그가 같게 읽히도록). `buildManifest()` 는 **필드 순서를 고정**한다 — 서명이 바이트를 덮으므로 "객체 키가 마침 그 순서였다" 는 명세가 아니다.
- **Kotlin**(`UpdateManifest.kt`): 여기만 표현 변환이 필요하다 — JCA `SHA256withECDSA` 는 **DER** 를 기대하는데 와이어 형식은 raw r||s(CNG 가 그걸 원하므로)다. `rawSignatureToDer()` 로 변환한다(DER INTEGER 의 최소 길이 + 최상위 비트 시 0x00 패딩까지). 공개키도 raw X||Y → `ECPublicKeySpec`. **와이어를 바꾸는 대신 여기서 변환**해 세 런타임에 표현 한 벌만 둔다.
- **서버 라우트** `GET /api/update/manifest?platform=<windows|android>`:
  - **아티팩트는 여기서 내보내지 않는다.** 이 프로세스는 릴레이와 하트비트를 같은 이벤트 루프에서 돌리므로, 수 MB 설치본을 흘리면 그 박스의 모든 세션이 다운로드 뒤에 줄을 선다. manifest 가 위치를 가리키고, 파일 서빙은 파일 서빙용이 한다.
  - 인증은 **기존 Bearer 세션 또는 `x-host-token` 재사용**. 업데이트 확인은 로그인된 기계가 하는 일이고, 이걸 위해 세 번째 자격증명을 만들면 틀릴 곳이 하나 더 늘 뿐이다. manifest 는 어차피 서명돼 있으므로 이건 전송을 믿는 문제가 아니라 **플릿의 업데이트 상태를 아무에게나 공개하지 않는** 문제다.
  - `platform` 은 파일명 일부가 되므로 **화이트리스트**다(정제가 아니라). 플랫폼은 둘뿐이라 그 밖을 받아들인 뒤 안전하게 만들 이유가 없다.
  - `REMOTE60_UPDATE_DIR` 미설정이면 **엔드포인트가 꺼진다**(503). 기본 디렉터리로 떨어지지 않는다. `REMOTE60_UPDATE_PUBLIC_KEY` 가 설정돼 있으면 **내보내기 전에 읽어서 검증**한다 — 서명이 틀린 manifest 는 모든 클라이언트에서 실패할 텐데, 그걸 처음 알게 되는 사람이 사용자여선 안 된다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - JS `update_manifest_test.js` **34 checks / 0 failed**. 서명 8종 · **다른 키로 만든 유효한 서명이 우리 키로는 통과하지 못함**(모양만 보고 출처를 안 보는 검증기를 잡는 케이스, 그 서명이 자기 키로는 통과하는 것까지 확인해 테스트가 의미 있음을 보임) · 순서 2 · **build→sign→read 왕복**(서명 후 size 한 글자 수정 시 깨짐) · 필드 5 · 플랫폼 2.
  - Kotlin `UpdateManifestTest` **tests=9 failures=0 errors=0**(+ 기존 `VersionCompareTest` 2/0). DER↔raw 왕복을 테스트 쪽에서 역방향으로 구현해 production 변환의 반대편도 실행된다.
  - 서버 `update_route_test.js` **16 checks / 0 failed**. **익명 거부**(401, 본문에 아무것도 새지 않음) · 정상 200 + 문서/서명/**서명된 그대로 개행으로 끝남** · 미발행 플랫폼 404 · **경로 주입 시도 포함 platform 5종 거부** · 라우트 근접 오타 404 · 잘못된 세션 토큰 401.
  - **디렉터리 스위트 전체 `node test/run.js` exit 0, 전 구간 ALL PASS** (신규 2종이 각각 최상단·중간에서 통과).
- **작업 중 사고 1건(기록)**: 서버 구문 확인을 `node -e "require('./apps/directory/server.js')"` 로 하면서 **로컬 8080 에 실제 서버가 약 3초간 떴다.** 즉시 확인해 종료했고(PID 10648), 포트 해제·부산물 없음을 확인했다. **NAS·실서버·라이브 제품과 무관한 이 PC 로컬 프로세스**였지만, 구문 검사는 `node --check` 로 했어야 했다.
- 미착수·미검증: 실제 HTTPS 왕복 없음(#438 그대로) · `UpdateEffects` 와 HTTPS 클라이언트 배선 미완 · **UI 진입점 미착수** · `RegisterInstall`/`Relaunch`/`HealthCheck` production 은 **설계 질의 답 대기**(등록을 자체 구현할지 설치기 `/S` 재사용할지 — 후자는 업데이터의 원자 교체를 설치기의 비원자 교체가 우회한다) · 서버는 **배포하지 않았다**(코드만).
- 변경 파일: `apps/directory/update_manifest.js`·`test/update_manifest_test.js`·`test/update_route_test.js`(신규) · `apps/directory/server.js`·`test/run.js` · `.../androiddirect/UpdateManifest.kt`·`app/src/test/.../UpdateManifestTest.kt`(신규) · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음. 키는 양쪽 모두 fail-closed(`trusted_public_key_hex()` / `trustedPublicKeyHex()` 빈 문자열, Kotlin 테스트가 그것을 단정).
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 440) 2026-09-08 업데이트 step4-3 — 등록 로직을 공유 단위로 추출((D)안), 버전을 인자로 · 실패 단계를 보고 · 롤백용 스냅샷 (Codex 승인 (D) + 조건 7건)
- 배경: `RegisterInstall` 을 어떻게 만들지 (A)자체구현 / (B)설치기 `/S` 재사용 / (C)설치기에 등록 전용 인자 중에서 물었고, **Codex·검증용이 셋 다 배제하고 (D) 를 확정**했다. (C) 를 죽인 사실을 **내가 원문에서 재확인**했다:
  - `write_uninstall_entry` 가 `set(L"DisplayVersion", kProductVersion)` — **등록하는 바이너리에 컴파일 시점 상수로 박힌 값**(`installer_main.cpp:269`).
  - **`kPayload` 에 `GNLinkSetup.exe` 가 없다(0건)**. 설치기는 자기 자신을 `CopyFileW(self, setupPath)` 로 따로 복사한다(`:314`).
  → 업데이터가 교체를 마쳐도 디스크의 `GNLinkSetup.exe` 는 **여전히 구버전**이고, 거기에 등록을 위임하면 **구버전 상수가 `DisplayVersion` 에 쓰인다.** `read_installed_version()`(`:436`)이 그 키를 읽으므로 **이후 모든 "더 새로운가" 판정의 기준점이 오염**된다.
- **(D) 추출**: `apps/native_poc/src/install_registration.{hpp,cpp}` 신설. 서비스 등록 · 방화벽 규칙 · 시작메뉴 2개 · Uninstall 키를 한 단위로 묶고, **설치기와 업데이터가 같은 코드를 링크**한다. **버전은 상수가 아니라 인자** — 설치기는 `kProductVersion` 을, 업데이터는 검증된 manifest 버전을 넘긴다. 선례는 `compare_versions` 추출(#433, `f0beef9`)과 같은 패턴이다.
- **조건 3 — 중간 실패를 감추지 않는다**: `RegistrationResult{ok, completed[], failedAt, detail}`. **첫 실패에서 멈추고 어느 단계인지 반환**하며, 그때까지 완료된 단계를 **부분 상태로 남긴다**. 현행 설치기는 서비스 등록 실패 후 메시지만 띄우고 **최종 성공으로 반환**하는데(`do_install()`), 그 관행은 가져오지 않았다 — 완전 등록과 부분 등록을 구별 못 하는 호출자는 롤백 여부를 판단할 근거가 없다.
- **조건 1 — 롤백은 이전 설치 값으로 복원한다(검증용이 빠뜨렸다고 정정한 항목)**: `capture_registration()` 을 **교체 전에** 찍고, 롤백 시 `restore_registration()` 으로 되돌린다. **새 manifest 버전을 재사용하면 파일은 구버전인데 레지스트리는 신버전을 주장** — (C) 를 배제한 것과 정확히 같은 오염이다. 스냅샷이 `present == false`(첫 설치)면 **빈 값을 쓰는 게 아니라 키를 지운다.**
- **조건 2 — 원자성 표현 정정**: `update_effects.hpp` 의 "The swap is all-or-nothing by construction" 을 **"THE FILE SWAP is all-or-nothing"** 으로 좁히고, **"업데이트 전체는 원자적이지 않으며 그렇게 만들 수도 없다 — 파일 교체 + 시스템 등록 4종은 트랜잭션이 아니라 순서다. 대신 복구 가능한 단계 계약을 갖는다"** 를 명시했다. 이 코드의 원자성 언급은 **파일 교체에 한정해 읽어야 한다**고 주석에 못박았다.
- **조건 6 — `request_process_stop` 의 재사용 창 제거**: 신원을 확인하고 핸들을 **닫은 뒤** PID 로 창을 열거하던 형태라, 검사와 WM_CLOSE 사이에 PID 재사용 창이 남아 있었다. 이제 **검증된 핸들을 함수 끝까지 열어 둔다** — Windows 는 핸들이 살아 있는 동안 PID 를 재활용하지 않으므로, 그것이 위 검사를 유효하게 유지하는 수단이다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_install_registration_test` **45 checks / 0 failed**, exit 0. **핵심 단정**: `DisplayVersion` 이 **넘긴 버전**이지 상수가 아님 · 다른 버전으로 재등록하면 값이 따라 움직임 · 실패 3종(service/firewall/shortcuts)에서 **어느 단계인지 + 그때까지 완료 목록** 보고 · 실패 시 **Uninstall 키가 아예 쓰이지 않음** · 스냅샷 왕복(구버전 캡처 → 신버전 등록 → **복원 후 구버전으로 되돌아옴**) · 첫 설치 스냅샷 복원은 **키 삭제**.
  - **격리**: 레지스트리 루트는 `HKCU\Software\GNLinkRegistrationTest\<pid>`(HKLM 아님), 서비스명·방화벽 규칙명은 테스트 전용, netsh·SCM·바로가기는 **기록만 하고 실행하지 않는다**. 테스트가 시작하면서 **"scratch 키가 실제 Uninstall 경로가 아님"** 을 스스로 단정한다.
  - **실제 시스템 무영향 실측**: 테스트 후 실제 `HKLM\...\Uninstall\GNLink` 의 `DisplayVersion` = **0.2.104 그대로**, `GNLinkSecureInput` = **Running**, HKCU 테스트 잔재 **없음**.
  - `remote60_installer` 재빌드 exit 0 — 추출 후에도 설치기가 링크되고, 설치기는 계속 `kProductVersion` 을 넘긴다.
- **조건 5 조사 결과 — 구버전 Setup 이 유지보수 바이너리로 남는 문제(임의 결정하지 않음)**: 확인된 사실 — 업데이트는 `GNLinkSetup.exe` 를 교체하지 않으므로(kPayload 에 없음) `UninstallString` 이 가리키는 것은 **영원히 구버전 바이너리**다. 그리고 그 바이너리의 언인스톨 경로는 **전부 자기 컴파일 시점 상수**에 묶여 있다: payload 목록(`:440` 이 `kPayload` 순회) · 서비스명(`kServiceName`, `:40`) · 바로가기 이름(`:417`) · 설치 폴더명(`kInstallFolderName`, `:39`) · Uninstall 키 경로(`:455`). → **신버전이 payload 를 추가/제거/개명하거나 서비스명·폴더명을 바꾸면 구 언인스톨러가 남기거나 엉뚱한 것을 지운다.** 선택지(제안만): ① `GNLinkSetup.exe` 를 payload 에 넣어 교체 대상으로 삼는다 ② 업데이터가 교체 후 새 Setup 을 별도로 복사한다 ③ 언인스톨 대상 목록을 바이너리 상수가 아니라 **설치 시 기록한 매니페스트**에서 읽는다. **패키지/유지보수 바이너리 계약 변경이라 결정을 요청한다.**
- 미검증·미착수: **실제 서비스 등록·실제 방화벽 규칙·실제 시작메뉴 생성 0회**(전부 기록형 stand-in) · 관리자 권한 실증 없음 · **업데이터의 `RegisterInstall` 이 이 단위를 호출하도록 하는 배선은 아직**(다음 커밋) · `Relaunch`/`HealthCheck` production 미착수 · UI 진입점 미착수 · 실 HTTPS 왕복 0회.
- 변경 파일: `apps/native_poc/src/install_registration.hpp`·`install_registration.cpp`·`install_registration_test.cpp`(신규) · `installer/installer_main.cpp` · `src/update_effects.hpp`(표현 정정) · `src/update_process_targets.cpp`(핸들 창) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 441) 2026-09-08 업데이트 step4-4 — "업데이트 확인" 진입점(트레이) + 확인 로직, 그리고 라우트 테스트 결함 2건 수정 (설계 3.5, 검증용 관찰 반영)
- **검증용 관찰 2건 수정(둘 다 실제 테스트 결함)**:
  - **공허하게 통과하는 단정**: `update_route_test.js` 의 `and nothing leaks in the body` 는 **응답이 아예 없으면 자동으로 만족**됐다. 즉 서버가 안 떠 있을 때 **가장 확실하게 통과**하는 단정이었고, 그건 유용함의 정반대다. 이제 `r.status !== 0` 을 함께 요구한다.
  - **연결 불가 시 빨리 실패**: 단독 실행 시 15개의 혼란스러운 실패 대신 `/healthz` 로 먼저 확인하고 **"이 테스트는 run.js 가 띄우는 서버가 필요하다"** 를 3줄로 말하고 exit 2 한다. 스위트가 서버 수명을 소유하는 것은 설계대로이므로(엔드포인트는 `REMOTE60_UPDATE_DIR` 이 있을 때만 존재한다) 테스트가 그 사실을 스스로 설명하게 했다.
- **`update_check.{hpp,cpp}` 신설** — "업데이트가 있는가" 를 묻는 일과 답하는 일을 분리한다.
  - **"확인 실패" 는 "최신" 과 다른 답이다**: `NotConfigured` / `Unreachable` / `Rejected` / `UpToDate` / `UpdateAvailable` 다섯으로 나눴다. 이걸 boolean 으로 뭉개면 **한 달째 확인에 실패한 기계가 사용자에게 "최신입니다" 라고 말하게 된다** — 이 열거형이 존재하는 이유가 그것이다.
  - **`NotConfigured` 는 네트워크를 건드리기 전에 답한다**: 신뢰키가 없는 빌드는 어떤 manifest 도 받아들일 수 없으므로 조회는 연극이고, 그때 나오는 실패는 "업데이트 서버가 고장" 처럼 읽힌다. 실제로는 "이 빌드는 아직 업데이트를 하지 않는다" 이다.
  - **호출자를 절대 막지 않는다**: 동기형은 테스트가 부를 수 있는 평범한 함수, 비동기형은 **detach 스레드**로 답을 콜백에 넘긴다. 콜백이 **워커 스레드에서 온다는 사실을 헤더에 명시**했다 — 조용히 넘기는 것이 워커가 남의 창을 만지는 경로다.
- **트레이 진입점**(`host_app_main.cpp`): `IdMenuCheckUpdate` + "Check for updates" 항목. **로그아웃 상태에서도 활성**이다 — "더 새 빌드가 있는가" 는 로그인과 무관하고, 비활성화하면 미설정 빌드가 고장난 빌드처럼 보인다. 결과는 워커가 `kUpdateCheckDoneMessage`(WM_APP+3)로 **heap 문자열 소유권과 함께 post** 하고 **UI 스레드가 MessageBox 를 띄운다**. 다섯 결과마다 문구가 다르며, 특히 `Unreachable` 은 **"최신인지 알 수 없다"** 로 적어 최신과 구별한다. 결과는 `host_app.log` 에도 남는다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_check_test` **28 checks / 0 failed**, exit 0. 미설정 3종(**네트워크 미접촉 단정 포함**) · **Unreachable 이 UpToDate 도 UpdateAvailable 도 아님** · Rejected 3종(서명·파싱·플랫폼) · 정상 4종(신버전/동일/구버전 manifest/숫자 비교) · **비동기 2종**: 400ms 걸리는 fetcher 를 걸고 **호출이 100ms 미만에 반환**하는 것과, **실패해도 콜백이 온다**는 것("확인 중…" 에서 영원히 멈추는 UI 방지 — 서버가 죽었을 때가 바로 사용자가 보는 때다).
  - 디렉터리 스위트 전체 exit 0, 라우트 **17 checks**(reachability 1 추가). **단독 실행은 exit 2 + 3줄 안내**로 즉시 중단.
  - `remote60_host_app` 빌드 exit 0.
  - 업데이트 관련 7종 동시 재실행 전부 exit 0: version_compare **139** · manifest **41** · state_machine **57** · effects **98** · http **36** · registration **45** · check **28** = **444 checks / 0 failed**.
  - **라이브 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변 확인.** 빌드 산출물은 `build-local/` 에만 생기며 설치본은 건드리지 않았다.
- 미검증·미착수: **빌드한 `GNLinkHost.exe` 를 실행하지 않았다**(라이브 호스트와 충돌하므로) → **트레이 메뉴를 사람이 눌러 본 적이 없다.** 진입점의 배선은 빌드와 단위 테스트까지이고 **UI 실동작은 미검증**이다. `REMOTE60_UPDATE_MANIFEST_URL` 미설정 + 신뢰키 부재이므로 현재 이 항목은 항상 `NotConfigured` 로 답한다. **클라이언트 셸 시작 시 비동기 확인은 미착수**. `Relaunch`/`HealthCheck` production, 업데이터의 `RegisterInstall` 배선도 미착수.
- 변경 파일: `apps/native_poc/src/update_check.hpp`·`update_check.cpp`·`update_check_test.cpp`(신규) · `src/host_app_main.cpp` · `apps/native_poc/CMakeLists.txt` · `apps/directory/test/update_route_test.js` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 442) 2026-09-08 업데이트 step4-5 — payload 이름을 임의 경로 쓰기 수단으로 만들지 않기 (패키지 계약 ① 조건 2, 심층 방어)
- 배경: 패키지 계약 ① 승인에 딸린 **새 보안 요건**. manifest 에서 온 이름을 그대로 경로로 쓰면 **설치 디렉터리 밖에 쓰는 원시 수단**이 된다 — 그것도 **관리자 권한으로**. ①과 무관하게 먼저 처리했다.
- **왜 서명이 있는데도 필요한가**: 서명 검증은 첫 번째 자물쇠이고 이건 두 번째다. 심층 방어란 **서명이 올바르게 검증됐다는 것에, 서명키가 새지 않는다는 것에, 발행 측이 이상한 것에 서명하도록 속지 않는다는 것에 의존하지 않는다**는 뜻이다. 셋 중 무엇이 어긋나도 임의 위치 쓰기로 끝나서는 안 된다.
- **정제가 아니라 거부**: 적대적인 이름을 안전한 이름으로 바꾸는 것은 이미 안전한지 판정하는 것보다 훨씬 어려운 문제이고, 제품이 실제로 배포하는 이름의 집합은 작고 지루하다. 그래서 화이트리스트다.
- `apps/native_poc/src/payload_name.{hpp,cpp}` 신설. 거부 사유를 13종으로 이름 붙여 진단이 남게 했다: `Traversal` · `Absolute` · `DriveRelative` · `AlternateStream` · `ReservedDeviceName` · `TrailingDotOrSpace` · `Wildcard` · `ControlCharacter` · `EmptyComponent` · `Duplicate` · `CurrentDir` · `Empty` · `TooLong`.
- **Windows 특유의 함정 3가지를 특히 다뤘다**:
  - **예약 장치명**: `NUL`·`CON`·`COM1` 등은 파일을 만들지 않고 장치를 연다. payload 가 `NUL` 이면 교체가 **아무것도 쓰지 않고 성공한 것처럼 보인다**. 확장자 앞 stem 으로 판정하되(`CON.txt` 도 CON) **접두사 매칭이 아니라 정확 일치**다(`NULL.txt`·`COM10`·`console.html` 은 통과).
  - **후행 점·공백**: Windows 가 조용히 잘라내므로 `a.` 와 `a` 가 같은 파일이 된다 — 두 항목이 **중복처럼 보이지 않으면서 충돌**할 수 있다.
  - **대체 데이터 스트림**: `GNLinkHost.exe:hidden` 은 기존 파일 안에 보이지 않는 내용을 쓴다.
- **구분자 양쪽 형태로 검사**: 정방향 슬래시를 접어 다시 검사한다. 한 형태에서는 무해해 보이고 다른 형태에서는 적대적인 이름이 **한쪽만 검사해서 통과하는 일**이 없게 했다.
- **중복은 정돈 문제가 아니다**: 교체는 쓰기 전에 **모든 대상 파일을 먼저 옆으로 옮기므로**, 같은 이름이 두 번 있으면 두 번째 move-aside 가 **첫 번째의 백업을 덮어쓰고** 롤백이 **엉뚱한 바이트를 복원**한다. 대소문자·구분자 차이도 같은 파일로 본다(파일시스템이 그러므로).
- **배선**: `UpdateEffectsConfig::validate()` 가 `check_payload_names()` 를 통과하지 못하면 **거부하고, 몇 번째 항목이 왜 거부됐는지 detail 에 남긴다.** `AcquireLock()` 이 validate 를 먼저 부르므로 **나쁜 이름이 하나라도 있으면 업데이트가 시작되지 않는다.**
- **작성 중 발견한 자체 결함 1건**: 처음에는 성분을 `\` 로만 쪼개서 `ui/..\evil.exe` 를 `ui/..` 한 성분으로 보고 **`TrailingDotOrSpace` 로 거부**했다 — 거부는 맞지만 **이유가 틀렸다**. 진짜 traversal 을 겉치레 불평 뒤에 숨기는 형태라, **양쪽 구분자로 쪼개도록** 고쳤다. 테스트가 잡았다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_payload_name_test` **54 checks / 0 failed**, exit 0. 제품이 실제로 쓰는 6종 통과 · traversal 7종(**혼합 구분자 포함**) · 절대·드라이브 상대 6종(UNC 포함) · NTFS 3종 · 예약 장치명 9종(**통과해야 하는 3종 포함**) · 형식 오류 9종 · 목록 5종(위치·사유 보고, 대소문자/구분자 다른 중복).
  - 업데이트 관련 **8종 동시 재실행 전부 exit 0, 합계 498 checks / 0 failed**: version_compare 139 · manifest 41 · state_machine 57 · effects 98 · http 36 · registration 45 · check 28 · payload_name 54.
  - **라이브 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변.**
- 미착수(패키지 계약 ① 본체와 나머지 조건): Setup 을 패키지 구성원으로 넣기(조건 3) · 버전 일관성 3중 검사(조건 4) · 회귀 3종(Setup 해시 불일치·잠김·후속 실패에서 **구 Setup 까지 롤백**, 조건 5) · 기존 Setup 실행 중 정책(조건 7) · 업데이터 `RegisterInstall` 배선 · `Relaunch`/`HealthCheck` production · 클라이언트 셸 시작 시 비동기 확인. **조건 1(자가 치유 표현 금지)·8(범위 밖)·9(원자성 표현)은 문서 작성 시 반영한다.**
- 변경 파일: `apps/native_poc/src/payload_name.hpp`·`payload_name.cpp`·`payload_name_test.cpp`(신규) · `src/update_effects.hpp`·`update_effects.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 443) 2026-09-08 업데이트 step4-6 — 패키지 계약 ① 본체: Setup 을 패키지 구성원으로, 버전 일관성, 자기 교체 금지 (Codex 조건 3·4·5·6·7)
- 배경: 조사 5(구버전 `GNLinkSetup.exe` 가 유지보수 바이너리로 영구히 남는 문제)의 결정이 **①(새 Setup 을 서명 검증된 패키지의 구성원으로)** 으로 확정됐다. **②(별도 사후 복사)는 롤백에 참여하지 않아 "새 언인스톨러 + 구 바이너리" 를 남길 수 있어 현 상태보다 나쁘고**, **③(매니페스트 기반 uninstall)은 이미 설치된 구 Setup 이 매니페스트를 읽지 않아 당면 문제를 못 고친다** — ③은 원장 후속 항목으로만 남긴다. 조건 2(payload 이름 검증)는 #442 에서 먼저 처리했다.
- **핵심 구분(검증용 지적)**: `kPayload` 는 **설치기의 임베드 리소스 목록**이라 자기를 넣을 수 없지만, **업데이터의 `payloadNames` 는 다운로드한 패키지에서 읽는 별개 목록**이라 그 제약이 없다.
- **조건 3 — 완전한 구성원**: `GNLinkSetup.exe` 가 제품 파일과 **동일한 staging·백업·교체·롤백 대상**이 된다. 별도 사후 복사나 실행 우회는 없다. 회귀에서 **교체 후 `GNLinkSetup.exe` 도 새 바이트**임을 단정한다.
- **조건 4 — 버전 일관성**: `file_contains_utf16_version()` 신설. 설치기에 VERSIONINFO 리소스가 없으므로(확인함) **프로젝트가 이미 쓰는 방식**(UTF-16 문자열 검사, history #426)을 따른다. `VerifyDownload` 가 해시 검사 뒤에 ① staged 아티팩트가 기대 버전 문자열을 담는지 ② manifest 의 version 이 기대값과 같은지를 확인하고, 어긋나면 진행하지 않는다.
  - **한계를 코드 주석에 명시했다**: 문자열을 찾았다는 것은 그 바이너리가 **그 버전을 언급한다**는 것이지 **그 버전이라는 증명이 아니다**(한 바이너리에 여러 버전 문자열이 있을 수 있다). 이것은 **일치해야 할 두 출처 사이의 정합성 검사**이고, 어떤 바이트가 도착했는지를 실제로 고정하는 것은 manifest 의 해시다.
- **조건 6 — 업데이터는 자기를 교체하지 않는다**: `updaterImagePath` 를 설정에 추가하고 `validate()` 가 ① 자기 이미지 이름이 `payloadNames` 에 있는지 ② 자기 파일이 `installDir` 안에 있는지를 **검사한다**. 설계 3.2 가 업데이터를 설치 경로 밖에 두지만 **"설계상 그렇다" 는 보장이 아니다** — 잘못된 설정이 그대로 걸어 들어갈 수 있으므로 믿지 않고 확인한다.
- **조건 7 — 기존 Setup 실행 중**: `product_image_names()` 에 `GNLinkSetup.exe` 를 추가했다. 실행 중이면 **언인스톨이 진행 중일 수 있고**, 그 밑에서 바이너리를 갈아 끼우는 것은 업데이트를 안 하는 것보다 나쁘다. 이름은 후보를 찾는 수단일 뿐이고 실제 판정은 기존 `ProcessTarget` 신원(경로+생성시각+핸들)이 한다.
- **조건 5 — 회귀 3종 추가**(전부 실제 OS 동작):
  - **Setup 해시 불일치** → 검증 거부, **구 Setup·구 제품 파일 모두 무손상**.
  - **Setup 잠김**(공유 0 으로 실제 잠금) → 교체 실패, **제품 파일이 신버전으로 남지 않고**, 백업 잔재 없음, 잠긴 Setup 도 구버전 그대로.
  - **교체 후 후속 실패**(`registerInstall` 실패) → `RolledBack`, **롤백이 제품 파일과 구 Setup 을 함께 복원**하고 백업 잔재 없음.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_effects_test` **126 checks / 0 failed**(#442 의 98 → 126), exit 0.
  - 업데이트 **8종 합계 526 checks / 0 failed**, 전부 exit 0. `remote60_installer` 재빌드 exit 0.
  - **라이브 `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변.**
- **조건 1·9 표현 준수**: 이 문서와 커밋 메시지에 **"자가 치유" 를 쓰지 않았다.** ①이 하는 일은 **"유지보수 코드가 현재 버전으로 갱신된다"** 까지이고, **과거에 제거·개명된 파일이나 구 서비스 잔재를 자동으로 정리하지 않는다.** 정리가 필요하면 명시적 설치 소유권과 롤백 계획 안에서만 하며 **범용 폴더 청소는 금지**다. 다중 파일 교체도 **"staging + 복원 가능한 교체"** 로만 기술하고 OS 전체 원자 트랜잭션이라 부르지 않는다. 조건 8(폴더명·서비스명 변경)은 범위 밖이라 손대지 않았다.
- **불가피한 한계(명시)**: ①을 해도 **첫 업데이트가 도달하기 전까지는 현재 설치된 0.2.104 의 Setup 이 언인스톨을 지배한다.** 그 바이너리는 자기 컴파일 시점 상수(payload 목록·서비스명·폴더명·바로가기명·키 경로)에 묶여 있고 바꿀 방법이 없다.
- 미착수·미검증: **다중 파일 패키지 형식이 아직 없다** — 현재 staged 아티팩트는 파일 하나이고 `Swap()` 은 그것을 각 payload 이름으로 복사한다(테스트용 단순화). 실제 패키지 포맷과 그 추출은 미구현이다. 실제 서비스 등록·방화벽·시작메뉴 0회 · 실 HTTPS 왕복 0회 · 관리자 권한 실증 없음 · `enumerate_product_processes`/`request_process_stop` 실행 0회 · 업데이터 `RegisterInstall` 배선 · `Relaunch`/`HealthCheck` production · 클라 셸 시작 시 비동기 확인.
- 변경 파일: `apps/native_poc/src/update_effects.hpp`·`update_effects.cpp`·`update_effects_test.cpp`·`update_process_targets.cpp` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 444) 2026-09-08 업데이트 step4-7 — payload 이름 검증을 manifest 경로에 연결 (검증용 지적: "좋은 자물쇠가 아직 문에 달리지 않았다")
- 배경: #442 가 payload 이름 검증을 만들었지만 **manifest 에서 이름이 오는 경로가 없어** 실제로는 아무 문에도 달려 있지 않았다. 검증용의 지적이 정확했다 — 검증이 존재하는 것과 검증이 **실제 입력에 적용되는 것**은 다른 주장이다.
- **manifest 가 payload 이름을 나른다**: 반복 가능한 `payload=` 줄을 `ManifestFields::payloadNames` 로 파싱한다. 스키마 규칙상 **모르는 키는 무시**하므로 이 추가는 구 클라이언트와 호환된다. **순서를 보존**한다 — 교체가 그 순서로 파일을 옆으로 옮기므로, 순서가 바뀌면 롤백이 쓰는 백업이 달라진다.
- **자물쇠를 문에 달았다**: `load_manifest()` 가 서명 검증·필드 검증을 통과한 뒤 **`check_payload_names_utf8()` 로 이름을 검사**하고, 하나라도 걸리면 `Malformed` 로 끝난다. 따라서 **불안전한 이름을 담은 `VerifiedManifest` 는 존재할 수 없다** — 검증된 manifest 로 설치 설정을 만드는 호출자는 그런 이름을 받을 경로 자체가 없다. 서명은 "이 바이트가 우리 것" 을 말할 뿐 **"이 바이트 안의 이름이 안전" 을 말하지 않는다**.
- **UTF-8 경로 추가**: manifest 는 이름을 UTF-8 로 나르므로 `check_payload_name_utf8()`·`check_payload_names_utf8()` 를 더했다. **ASCII 밖의 바이트는 디코드하지 않고 거부**(`NonAscii`)한다 — 제품이 배포하는 이름은 전부 ASCII 이고, 그 이상을 받아들이면 **경로가 되기 직전인 문자열에 대해 정규화 형식과 동형 문자를 따져야** 한다. 풀려는 문제보다 훨씬 큰 문제다.
- 회귀 24종 추가(`update_manifest_test.cpp`, 41 → **65 checks**): **전부 accepting verifier 로 서명이 통과한 상태**라 거부하는 것은 오직 이름 검사다 — traversal · 혼합 구분자 traversal · 절대경로 · UNC · 대체 스트림 · 예약 장치명 · 후행 점 · 중복(대소문자 차이) · 비-ASCII 각각에 대해 `Malformed` + **`VerifiedManifest` 부재**를 단정한다. 통과해야 하는 것(payload 줄 없음, 평범한 이름 3개)도 함께 둬 과잉 차단이 아님을 보인다.
- **순서가 새 검사에도 유지된다**: 불안전한 이름 + **나쁜 서명**이면 여전히 `SignatureInvalid` 다(`Malformed` 아님). 새 검사를 서명 앞으로 끌어오지 않았다는 증거다.
- **이름이 살아서 나온다**: 통과한 경우 세 이름이 **쓰인 순서 그대로** `fields().payloadNames` 에 있는지 단정한다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_manifest_test` **65 checks / 0 failed**(41 → 65), exit 0.
  - 업데이트 **8종 합계 550 checks / 0 failed**, 전부 exit 0: version_compare 139 · manifest 65 · state_machine 57 · effects 126 · http 36 · registration 45 · check 28 · payload_name 54.
  - `remote60_host_app` 재빌드 exit 0.
- 미검증·미착수(그대로): **다중 파일 패키지 형식 없음** — manifest 가 이제 이름을 나르지만 **그 이름들에 해당하는 파일을 담은 패키지 포맷과 추출은 미구현**이라, `Swap()` 은 여전히 단일 아티팩트를 각 이름으로 복사한다. 업데이터 `RegisterInstall` 배선 · `Relaunch`/`HealthCheck` production · 클라 셸 시작 시 비동기 확인 · 실 HTTPS 왕복 0회 · 실제 서비스·방화벽·시작메뉴 0회 · 관리자 권한 실증 없음.
- 변경 파일: `apps/native_poc/src/payload_name.hpp`·`payload_name.cpp`(UTF-8 경로) · `update_manifest.hpp`·`update_manifest.cpp`(payload 필드 + 검사) · `update_manifest_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 445) 2026-09-08 업데이트 step4-8 — 등록 배선 + **커밋 지점 신설**(테스트가 잡은 실제 버그), 표현 정정 2건
- 목적: 업데이터의 `Register`/`Rollback` 을 공유 등록 단위(#440)에 연결한다. Codex 조건 1(롤백 등록은 **이전 설치 값**)을 구조로 만족시키는 것이 핵심이다.
- **배선**: `update_registration_wiring.{hpp,cpp}` 신설. `make_registration_effects()` 가 **스냅샷 하나를 공유하는 세 콜백**(`capture`/`apply`/`restore`)을 함께 만든다 — 따로 배선하면 어긋날 수 있고, 어긋나면 **롤백이 새 버전을 복원해 구 파일이 신버전을 주장**하게 된다. (C)를 배제한 것과 같은 오염이다. `UpdateEffectsConfig` 에 `captureRegistration`·`restoreRegistration` 을 **필수 seam** 으로 추가했다(기본값 fallback 없음).
- **`Swap()` 이 파일을 옮기기 전에 캡처**한다. 나중에 캡처하면 업데이트가 만든 상태를 캡처하게 되고, 그것으로 롤백하면 구 파일이 신버전 이름을 달게 된다. 캡처 실패는 **교체를 시작하지 않는다**.
- **⚠️ 테스트가 잡은 실제 버그 — 커밋 지점이 없었다**: `RegisterInstall()` 이 성공 직후 `.gnlink-old` 백업을 지우고 있었다. 그런데 상태기계 순서는 `Register → Relaunch → Health` 이고 **Health 실패는 롤백**이다. 즉 **롤백이 돌 때 복원할 백업이 이미 없었다.** 회귀("health 실패 후 파일이 구버전으로 돌아갔는가")가 이것을 잡았다.
  - 수정: `UpdateEffects` 에 **`Commit()`** 을 신설하고 상태기계가 **`Done` 과 `UpdatedButNotRelaunched` 에서만** 부른다. 그 둘이 **롤백하지 않는 유일한 종착점**이다. `RegisterInstall()` 은 이제 백업을 건드리지 않는다.
  - 회귀 추가: 정상 경로 `commitCount==1` · **swap 실패·health 실패에서 `commitCount==0`**(롤백이 필요로 하는 백업이 살아남았다는 뜻) · relaunch 실패에서는 `commitCount==1`(이 종착점은 롤백하지 않으므로).
  - **오래된 단정 하나가 버그를 고정하고 있었다**: "backups are dropped only after registration". 그 문장이 참이었기 때문에 버그가 통과하고 있었다. **"등록 뒤에도 백업은 살아 있어야 한다"** 로 뒤집고, `Commit()` 뒤에야 사라지는 것을 별도로 단정했다.
- **격리**: 배선 회귀는 `HKCU\Software\GNLinkUpdateWiringTest\<pid>` 를 쓰고 netsh·SCM·바로가기는 **기록만** 한다. 테스트가 시작하며 "이 키가 실제 Uninstall 경로가 아님" 을 단정한다. **실측**: 테스트 후 실제 `HKLM\...\Uninstall\GNLink` 의 `DisplayVersion` = **0.2.104 그대로**, 라이브 PID 3개 불변, HKCU 잔재 없음.
- 회귀 5종 추가: 정상 배선(등록 후 `DisplayVersion` = 새 버전, 공유 단위가 4단계 완료 보고) · **health 실패 → 파일도 레지스트리도 이전 버전으로 복귀**(0.3.0 이 아니라 0.2.105) · 캡처 실패 시 교체 미시작 · 캡처 없이 restore 는 **성공으로 보고**(되돌릴 것이 없다는 참인 진술이며, 실패로 치면 평범한 롤백이 `RollbackFailed` 가 된다).
- **표현 정정 2건(Codex 조건 0·1)**:
  - **"아카이브를 안 쓰면 취약점 부류 전체가 성립하지 않는다" 는 과장이다.** 사라지는 것은 **압축·추출 관련 처리**까지이고 **manifest 의 이름·URL 공격면은 그대로 남는다** — payload 이름 검증(#442·#444)과 URL 정책(#438)이 필요한 이유가 그것이다. 이 취지를 문서·주석에 쓰지 않는다.
  - **effects 의 checks 수를 다중 파일 업데이트의 증명으로 표기하지 않는다.** 단일 blob 을 이름만 달리 복사하는 현재 테스트는 **"해당 OS 부분 경로 검증"** 까지다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - `remote60_update_effects_test` **143 checks / 0 failed**(126→143) · `remote60_update_state_machine_test` **61 checks**(57→61).
  - 업데이트 **8종 합계 571 checks / 0 failed**, 전부 exit 0.
- 미착수: 다중 파일 패키지(manifest `artifacts[]`, **Codex 승인됨 — 다음 작업**) · `Relaunch`/`HealthCheck` production · 클라 셸 시작 시 비동기 확인 · 실 HTTPS 왕복 · 실제 서비스/방화벽/시작메뉴 0회 · 관리자 권한 실증 없음.
- 변경 파일: `apps/native_poc/src/update_registration_wiring.hpp`·`update_registration_wiring.cpp`(신규) · `update_effects.hpp`·`update_effects.cpp`·`update_effects_test.cpp` · `update_state_machine.hpp`·`update_state_machine.cpp`·`update_state_machine_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 446) 2026-09-08 업데이트 step4-9 — manifest 스키마 2(`artifacts[]`) 를 세 런타임에 동시 반영 (Codex 승인 포맷 + 조건 0~8)
- 배경: 다중 파일 패키지 포맷이 **아카이브 없이 manifest 가 파일별로 열거**하는 방식으로 확정됐다. 아카이브를 도입하면 압축·추출 코드가 새 표면이 되지만, **그것을 안 쓴다고 공격면이 사라지는 것은 아니다** — 사라지는 것은 압축·추출 처리까지이고 **manifest 의 이름·URL 은 여전히 경로와 요청이 되는 입력**이다. 그래서 이름 검증(#442·#444)과 URL 정책(#438)이 필요하다. (검증용이 자기 표현을 과장이라고 정정했고, 그 정정을 그대로 따랐다.)
- **스키마 2**: `schema=2` · `releaseId` · `platform` · `arch` · `version` · **`artifact=name|size|sha256|url` 반복**. 파이프 구분은 문서가 **바이트로 서명**되기 때문이다 — 읽는 방법이 하나뿐인 형식은 서명자와 검증자 사이에서 어긋날 것이 없다. 파이프는 이름(payload 검증이 거부)에도 해시에도 나타날 수 없다.
- **`releaseId` 가 존재하는 이유**: 파일마다 그 시점의 "latest" 를 다시 물으면 **업데이트 도중 릴리스가 바뀌어 절반은 이 빌드, 절반은 저 빌드인 설치**가 나온다. 한 시도에 staging 되는 모든 파일이 하나의 릴리스 정체성을 공유한다.
- **서명 범위(조건 2)**: 하나의 서명이 **릴리스ID·플랫폼·아키텍처·버전·파일 목록 전체**를 덮는다. 따라서 이름을 다른 해시와, 해시를 다른 URL 과 짝지을 수 없다. **검증 전에는 이름·URL 로 다운로드도 쓰기도 시작하지 않는다** — `VerifiedManifest` 타입 강제가 그것을 구조로 만든다.
- **목록 정책(조건 3)**: 아티팩트 최소 1개 · **최대 64개** · **파일당 512MB** · **총 2GB**(합계는 누적 검사로, 합이 넘쳐 한계를 지나치지 못하게) · 크기 0 거부 · **소문자 sha256 64자만** · **https 전용 + URL 자격증명 거부**(조건 4를 manifest 를 읽는 곳에서 강제하므로 다른 경로로 받는 호출자도 우회 불가) · 이름은 payload 규칙 전부 적용 + 중복 거부. **세 런타임이 같은 한계를 강제한다.**
- **세 런타임 동시 반영(조건 8)**:
  - **C++** `update_manifest.{hpp,cpp}` — `ManifestArtifact`, `ManifestLimits`, `expectedArch`. 스키마 1 의 `artifact=`/`size=`/`sha256=` 핸들러를 제거했다(그것이 먼저 걸려 새 형식이 도달하지 못하는 버그가 있었고 테스트가 잡았다).
  - **JS** `apps/directory/update_manifest.js` — 같은 규칙·같은 한계, `buildManifest` 가 **아티팩트를 주어진 순서 그대로** 방출한다.
  - **Kotlin** `UpdateManifest.kt` — 같은 규칙. `expectedArch` 추가.
  - **공유 벡터 재생성**: `apps/shared/update_manifest/` 가 이제 **서로 다른 바이트·다른 크기의 아티팩트 3개**를 담고 그중 하나가 `GNLinkSetup.exe` 다. 파일 본문도 `files/` 에 함께 둬 테스트가 **manifest 가 주장하는 해시를 실제로 갖는 파일**을 스테이징할 수 있다. 개인키는 생성 후 폐기했다.
- **전환 브리지(명시)**: effects 계층은 아직 파일 하나를 staging·검증한다. 그래서 `load_manifest` 는 **아티팩트가 정확히 1개일 때만** 그 정체성을 legacy 단일 필드에 복사한다. **여러 개일 때는 복사하지 않는다** — 여러 파일 중 첫 번째만으로 조용히 업데이트하는 일이 없도록. 이 브리지는 staging 이 목록을 소비하면 없어진다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 `Listen` → RDP 미접속.**
  - C++ 업데이트 **8종 합계 588 checks / 0 failed**(manifest 65→**82**), 전부 exit 0. 전체 빌드 오류 0.
  - JS `update_manifest_test.js` **54 checks / 0 failed**, 디렉터리 스위트 전체 exit 0(라우트 **18 checks**, 발행된 문서가 스키마 2이고 **아티팩트 3줄이 그대로 전달**되는지 확인).
  - Kotlin `UpdateManifestTest` **tests=11 failures=0**(+ `VersionCompareTest` 2/0).
  - 세 런타임 모두 **같은 고정 벡터의 실제 서명**을 검증하고 같은 변조를 거부한다.
- **조건 1 표기 준수**: effects 의 checks 수를 **다중 파일 업데이트의 증명으로 쓰지 않는다.** 현재 effects 테스트는 단일 blob 을 이름만 달리 복사하므로 **"해당 OS 부분 경로 검증"** 까지다.
- 미착수(조건 5·6·7 의 실행분): **단일 릴리스 스냅샷 staging**(모든 파일을 받아 검증한 뒤에만 교체 진입) · **실제 파일 N개 실행 회귀 4종**(정확 목적지 / 누락·해시오류·중간실패 시 종료 0·설치변경 0 / latest 변경 중 혼합 0 / 교체 중간 실패 시 전부 복원) · `Relaunch`/`HealthCheck` production · 클라 셸 시작 시 비동기 확인 · 실 HTTPS 왕복 0회 · 실제 서비스/방화벽/시작메뉴 0회 · 관리자 권한 실증 없음.
- 변경 파일: `apps/native_poc/src/update_manifest.hpp`·`update_manifest.cpp`·`update_manifest_test.cpp`·`update_state_machine_test.cpp`·`update_check_test.cpp`·`update_effects_test.cpp` · `apps/native_poc/CMakeLists.txt` · `apps/directory/update_manifest.js`·`test/update_manifest_test.js`·`test/update_route_test.js` · `.../androiddirect/UpdateManifest.kt`·`app/src/test/.../UpdateManifestTest.kt` · `apps/shared/update_manifest/`(README + 벡터 재생성 + `files/`) · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 447) 2026-09-08 업데이트 조건 5·7 — 단일 릴리스 스냅샷 staging + 실제 파일 회귀(신규 스위트 `remote60_update_release_test`)
- **조건 5 (스냅샷 staging)**: `Download` 가 검증된 manifest 의 **목록 전체**를 받아 `<stagingDir>\r-<releaseId>` 아래에 받는다. 파일마다 "지금 latest" 를 다시 묻지 않는다. `VerifyDownload` 는 **모든** 아티팩트의 크기·해시를 검사하고, 하나라도 어긋나면 릴리스 전체를 버린다. `Swap` 은 시작 전에 **staged 릴리스 존재**와 **payload 이름 전부가 그 릴리스에 있는지**를 확인한다 — "전부 받고 검증한 뒤에만 교체" 가 관례가 아니라 구조다. 버전 일관성 검사는 **Setup 계열 이름에만** 적용한다(데이터 파일은 UTF-16 버전 리터럴을 갖지 않으므로 전 아티팩트에 요구하면 통과할 수 없다).
- **조건 7 (실제 파일 회귀)** — 신규 스위트가 `apps/shared/update_manifest/files/` 의 **실제 벡터 본문**(24 / 48 / 18 바이트, 그중 하나가 `GNLinkSetup.exe`, 하나가 하위 폴더 `ui\shell.html`)을 쓴다. 길이가 서로 다르므로 **엉뚱한 곳에 놓으면 내용 이전에 길이에서 걸린다**. 기존 effects 스위트는 이름만 다른 같은 blob 을 복사했기 때문에 잘못된 배치를 구분할 수 없었다.
  - **R1 정확 목적지**: 3개가 각자 자리에 각자 바이트로. 어떤 릴리스도 이름대지 않은 파일은 그대로. **서버는 아티팩트당 정확히 1회만 요청받았다(fetches==3)** — 파일별 latest 재조회가 없다는 직접 증거. 성공 경로에서 **실제로 프로세스 1개를 종료**시켜, 아래 "종료 0" 단정들의 대조군을 만든다.
  - **R2 누락 / 중간 실패 / 해시 오류 3종**: 셋 다 `AbandonedBeforeSwap`, **종료 0**, 대기 중이던 더미 프로세스 생존, **설치 디렉터리 전체가 바이트 동일**(스냅샷 비교라 잔여 파일·누락·`.gnlink-old` 도 같이 잡힌다).
  - **R3 latest 가 도중에 바뀜**: 서버가 첫 파일 뒤 릴리스 B 로 옮겨가도 거부되고 **설치는 바이트 동일, B 의 바이트는 한 개도 들어가지 않는다.** 이어서 정직하게 B 를 요청하면 **B 가 전부** 설치되고 **A 의 바이트는 하나도 남지 않는다**. staging 키잉 자체도 직접 단정한다 — A·B 를 연달아 받으면 **두 디렉터리에 6개**로 나뉘고 **A 의 바이트가 B 의 다운로드에 덮이지 않는다**.
  - **R4 교체 중간 실패**: (a) 1단계에서 세 번째 파일이 열려 있어 실패 → 이미 치운 것 전부 복원, 레지스트리는 이전 버전. (b) 교체·등록 성공 후 **health 실패** → 파일 전부 이전 빌드로, `DisplayVersion` 도 **0.2.104 로 복귀**(버려지는 버전이 아니라). (c) **2단계 중간 실패** → 아래 참조.
- ⚠️ **테스트가 실제 결함 2건을 잡았다 (제품 코드 수정)**:
  - **롤백이 "추가된 파일" 을 남겼다.** 롤백은 `movedAside_` 만 되돌리는데, 릴리스가 **새로 추가하는 파일**은 백업이 없어 대상이 아니었다. 일어나지 않은 업데이트가 설치를 영구히 바꾼다. → 교체가 **실제로 놓은 이름(`placed_`)** 을 따로 기록하고, 롤백이 **백업 없이 놓인 것은 지운다.**
  - **목적지가 없는 하위 폴더면 교체가 실패했다.** 벡터가 이미 `ui\shell.html` 을 담고 있고, 폴더를 새로 만드는 릴리스는 놓을 수 없었다. → 2단계가 복사 전에 **상위 폴더를 만든다**(이름은 이미 `check_payload_names` 를 통과했으므로 밖으로 못 나간다). 롤백은 **이 시도가 만든 폴더만** 깊은 것부터 비어 있을 때만 지운다.
- ⚠️ **헛단정 2건을 대조군이 잡았다 (교훈 기록: 설계문서 3.10)**:
  - 추가-파일 회귀의 첫 판은 **복사가 실패한 파일 자체를 "추가된 파일" 로 골랐다.** 복사가 실행되지 않았으니 치울 것이 없었고, **롤백 코드를 꺼도 그대로 통과**했다. 지금은 추가 파일을 payload 맨 앞, 막을 파일을 맨 뒤에 둔다 — 코드를 끄면 실패한다(실측: 2 FAIL).
  - `GetFileAttributesW(p) & FILE_ATTRIBUTE_DIRECTORY` 는 **없는 경로에도 참**이다(`INVALID_FILE_ATTRIBUTES` 가 전 비트 1). 폴더 생성을 그렇게 확인하던 단정 2개가 폴더 없이도 통과하고 있었다 → `is_directory()` 로 교체.
- **전환 브리지 제거**: staging 이 이제 `artifacts[]` 를 소비하므로, `load_manifest` 가 단일 아티팩트를 legacy 필드로 복사하던 브리지와 `ManifestFields::artifact/size/sha256` 을 **삭제**했다. 파일의 정체성이 오는 곳이 하나다. (#446 이 "staging 이 목록을 소비하면 없어진다" 고 적어 둔 그것이다.)
- **격리(변함없음)**: 새 스위트도 `update_process_targets.cpp` 를 **링크하지 않는다** — 이미지 이름으로 실제 `GNLinkHost.exe` 를 찾는 코드가 바이너리에 없다. 경로는 전부 임시 디렉터리, 레지스트리는 `HKCU\Software\GNLinkReleaseTest-<pid>`, 소켓 0. 종료되는 프로세스는 하네스가 직접 띄운 더미뿐.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 Listen → RDP 미접속.**
  - C++ 업데이트 **9종 합계 672 checks / 0 failed**(신규 release **80**, effects 147, manifest 82, state_machine 61, http 36, check 28, payload_name 54, version_compare 139, install_registration 45). 전체 빌드 오류 0.
  - **대조군 3회 실측**(각각 제품 코드를 한 줄 끄고 재빌드·재실행): 롤백의 추가파일 제거 끔 → **2 FAIL** / 폴더 생성 끔 → **3 FAIL** / 폴더 정리 끔 → **1 FAIL**. 끄면 실패하고 켜면 통과한다.
  - JS 디렉터리 스위트 exit 0. **Kotlin 은 재실행하지 않았다 — 이번 변경에 Kotlin·JS 소스 변경이 없다**(`git status` 로 확인).
  - 라이브 제품 무영향 실측: `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 동일, 실제 `HKLM\...\Uninstall\GNLink` `DisplayVersion` = **0.2.104** 그대로.
- **미검증 한계**: 실 HTTPS 왕복 0회(fetch 는 주입 함수) · 실제 서비스/방화벽/시작메뉴 등록 0회(기록만) · 관리자 권한 실증 없음 · `Relaunch`/`HealthCheck` production 미구현(주입 스텁) · 클라 셸 시작 시 비동기 확인 미착수.
- 변경 파일: `apps/native_poc/src/update_release_test.cpp`(신규) · `update_effects.hpp`·`update_effects.cpp`·`update_effects_test.cpp` · `update_manifest.hpp`·`update_manifest.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/업데이트_기능_설계.md`(3.8·3.9·3.10) · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 448) 2026-09-08 업데이트 step4-10 — `Relaunch`/`HealthCheck` production + 클라이언트 시작 시 비동기 확인
- **결정과 실행을 분리**했다. 무엇을 다시 띄울지(`update_relaunch_plan.cpp`)와 무엇을 건강의 증거로 받을지(`update_health.cpp`, `update_health_log.cpp`)는 **테스트에 링크되고**, 실제로 프로세스를 만들고 SCM 을 건드리고 라이브 로그를 읽는 코드(`update_relaunch.cpp`)는 **어디에도 링크되지 않는 static 라이브러리**다 — `remote60_update_process_targets` 와 같은 배치, 같은 이유.
- **Relaunch — 제품의 프로세스는 서로 대등하지 않다.** 대등하게 다루면 한 번에 네 가지가 어긋난다:
  - `GNLinkHost.exe` 는 업데이터의 자식(이미 `requireAdministrator` 였으므로 토큰 상속 → **UAC 프롬프트 없음**).
  - `GNLinkClient.exe` 는 **셸을 통해 비승격으로**. 승격된 업데이터의 자식으로 띄우면 관리자 토큰을 물려받아 **이후 세션 전체가 관리자로 돈다**. 셸 경로가 안 되면 **띄우지 않는다** — 자식으로 대신 띄우는 폴백은 없다. 그 폴백이 바로 피하려던 결과다.
  - `GNLinkInputService.exe` 는 SCM(프로세스를 직접 만들면 서비스가 되지 않는다).
  - `GNLinkStream/Capture/Viewer.exe` 는 **띄우지 않는다** — 감독자가 있는 자식이다. 여기서 띄우면 **아무도 감독하지 않는 인스턴스** + 감독자가 띄운 **두 번째**가 생긴다.
  - 계획은 **정지시킨 목록**에서 만든다(돌지 않던 것은 안 띄운다). 같은 이미지 중복은 **항목 하나**. 제품이 모르는 이름은 **버린다** — 데이터가 실행할 파일을 지명하게 두지 않는다.
- **HealthCheck — 프로세스의 존재는 증거가 아니다.** 떠서 곧바로 제 일을 못 하는 것이 바로 이 검사가 잡으라고 있는 실패다. 그래서 제품이 자기 자신에 대해 쓰는 한 줄을 읽는다: `[host-app] health version=<v> directory=<ok|pending|not-configured>`.
  - ⚠️ **증거는 이번 실행의 것이어야 한다.** 로그에는 **교체된 그 버전**의 보고가 남아 있고, 파일 전체를 읽는 검사는 그걸 보고 성공을 보고한다 — **정확히 이 검사가 잡으라고 있는 상황에서**. 그래서 재실행 **전에** 로그 크기를 기록하고 그 뒤에 덧붙은 바이트만 본다. 회귀로 고정: 전체를 읽으면 구버전 보고가 "healthy" 로 판정되고, mark 뒤로 읽으면 **자기 버전으로도 healthy 가 아니다**.
  - `pending`(아직) 과 실패는 다르다. `not-configured`(로그인 계정 없음) 는 **실패가 아니다** — 그걸로 롤백하면 업데이트가 만들지 않은 결함을 지어내는 것이다. **구버전 보고는 지연이 아니라 실패**(`WrongVersion`, 즉시 종료 — 더 기다려도 안 바뀐다). 모르는 `directory=` 값은 **무시**(새 제품의 상태가 옛 업데이터를 우연히 만족시키면 안 된다).
  - 로그는 **공유 열기**로 읽는다. 배타적으로 열면 검사가 자기가 기다리는 증거를 막는다.
- **호스트가 그 한 줄을 쓴다**(`host_app_main.cpp`): 시작 시 캐시 로드 직후 `pending`/`not-configured`, 상태 타이머에서 `log_upload_status().sentBatches > 0` 이 되면 **한 번** `ok`, 새 로그인 때 갱신.
- **클라이언트 시작 시 비동기 확인**(`client_shell_main.cpp`): 창을 띄운 **직후 · WebView 생성 전**에 시작하고 **기다리지 않는다**. 최적화가 아니라 요구사항 — 서버가 답할 때까지 로그인 창을 안 보여주면 그 서버로 가는 경로가 없는 기계에서 **쓸 수 없다**. 사용자에게 말하는 것은 **"새 버전이 있다" 뿐**이고 나머지는 로그로만 간다(시작 시 확인은 서버에 못 닿는 것이 일상인 노트북에서 돈다 — 대화상자로 만들면 사용자는 **닫는 법을 배우고** 다음에 중요한 것도 같이 닫는다). `Unreachable` 은 로그에서도 **"최신" 으로 기록되지 않는다**. 답이 페이지보다 먼저 오면 **버리지 않고 들고 있다가** `ready` 때 보낸다.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 Listen → RDP 미접속.**
  - C++ 업데이트 **10종 합계 729 checks / 0 failed**(신규 relaunch **57**, release 80, effects 147, manifest 82, state_machine 61, http 36, check 28, payload_name 54, version_compare 139, install_registration 45). `client_shell_bridge_test` PASS(시작 시 확인의 침묵 정책 7건 추가). 전체 빌드 오류 0.
  - JS 디렉터리 스위트 exit 0. Kotlin 소스 변경 없어 미실행.
  - 라이브 무영향 실측: `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 동일, `DisplayVersion` = **0.2.104** 그대로.
- **미검증 한계(누적 정리)**: ① 실 HTTPS 왕복 0회 — 모든 fetch 는 주입 함수 ② 실제 서비스/방화벽/시작메뉴 등록 0회 — 기록만 ③ 관리자 권한 실증 없음 — UAC 프롬프트 0회/1회 조건 미실증 ④ **`update_relaunch.cpp` 는 실행 실증 0** — `CreateProcessW`·`StartServiceW`·셸 경유 비승격 실행 모두 미실행(라이브를 건드리지 않기 위해 의도적으로 링크하지 않음) ⑤ 호스트/클라이언트 **UI 실동작 미검증** — 빌드한 바이너리를 실행하면 라이브 0.2.104 와 충돌 ⑥ 실서버 배포·운영 서명키 없음.
- 변경 파일: `update_relaunch_plan.{hpp,cpp}`·`update_health.{hpp,cpp}`·`update_health_log.{hpp,cpp}`·`update_relaunch.{hpp,cpp}`·`update_relaunch_test.cpp`(전부 신규) · `host_app_main.cpp` · `client_shell_main.cpp` · `client_shell_bridge.{hpp,cpp}`·`client_shell_bridge_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/업데이트_기능_설계.md`(3.11·3.12·3.13) · `docs/history.md` · `docs/구현계획.md`.
- 버전 인상·설치본 생성·설치·라이브 조작·서버 배포·push 없음.
- 상태: 검증용 검사 대기. **전체 완료 아님.**

### 449) 2026-09-08 설계문서 3.8.1 — "혼합 0" 이 **네 장치가 함께 만드는 성질**임을 명시 (검증용 요청)
- 배경: #447 보고에서 "`releaseId` 가 혼합을 막는다" 로 읽힐 수 있는 서술을 **작업용이 스스로 정정**했고, 검증용이 **그 구분을 설계문서에 그대로 남기라**고 요청했다. 이유가 문서화의 목적을 정확히 짚는다 — 그렇게 요약해 두면 나중에 누가 나머지 셋 중 하나를 걷어내도 **요약과 어긋나지 않아서, 방어가 조용히 사라진다.**
- **3.8.1 신설**: 네 장치가 **정확히 무엇을 막는지 / 없으면 무슨 일이 일어나는지 / 증거가 무엇인지**를 표로 적었다.
  - **재조회 없음** — 파일마다 latest 를 다시 묻는 것을 막는다. 없으면 서버가 움직일 때 **그 자체로** 섞인다. 증거 `fetches == 3`.
  - **목록 전체를 덮는 서명** — 이름을 다른 해시와, 해시를 다른 URL 과 짝짓는 것을 막는다.
  - **아티팩트별 size/sha256 대조** — 약속과 **다른 바이트**가 staging 에 들어오는 것을 막는다. 증거는 R3 의 실제 실패 사유 `size mismatch for ui\shell.html`.
  - **`releaseId` 키잉 staging + `Swap` 진입 가드** — **시도를 가로질러** 섞이는 것(절반 받은 A 를 B 로 완성)을 막는다. 증거는 A·B 연속 다운로드가 **두 디렉터리 6파일**로 갈리고 A 바이트가 안 덮이는 것.
- **가장 헷갈리기 쉬운 지점을 명시**: R3 에서 실제로 거부를 만드는 것은 **`releaseId` 가 아니라 해시 대조**다. `releaseId` 는 그 시도 *안에서* 막는 장치가 아니라 **시도와 시도 *사이*를** 막는 장치다.
- 오타 1건 수정: 3.10 "코드를 꺼면" → "코드를 끄면".
- 문서만 변경. 제품 코드·빌드·테스트 변경 없음. 버전 인상·설치본·배포·push 없음.
- 변경 파일: `docs/업데이트_기능_설계.md`(3.8.1 신설, 3.10 오타) · `docs/history.md`.

### 450) 2026-09-08 업데이트 기능 — 격리 범위 마무리: 실기 항목을 문서로 고정하고 최종 상태 정리
- 새 기능 없음. **검증용이 지시한 마무리 3건**만 수행했다.
- **① "혼합 0" 방어 4요소 구분** — 이미 #449 (설계 3.8.1) 로 들어가 있어 확인만 했다.
- **② 실기 항목을 문서로 고정** — `docs/수동확인_체크리스트.md` 에 **UPD-FIELD-01~06** 추가. **A2A 보고에만 있으면 세션이 끝날 때 사라진다**는 지적이 정확했다. 각 항목에 실행 환경·통과 기준·출처를 붙였다:
  - **UPD-FIELD-01 (P0) 셸 경유 비승격 재실행** — 클라이언트 무결성 수준이 **Medium** 이어야 한다(High 면 실패). Explorer 없는 세션·다중 데스크톱 거동과 **실패 시 "띄우지 않음"** 이 사용자에게 어떻게 보이는지도 기록.
  - **UPD-FIELD-02 (P0) UAC 프롬프트 횟수** — **호스트발 0회 / 클라발 정확히 1회**, 취소 시 교체 안 하고 계속 사용 가능.
  - **UPD-FIELD-03 (P1) 실 HTTPS 왕복** · **04 (P1) 실제 등록 4단계** · **05 (P1) `update_relaunch.cpp` 실행 실증** · **06 (P2) UI 실동작**.
  - 문서에 **자동 회귀로 대체할 수 없는 이유가 항목마다 다르다**는 점을 적었다 — 01·05 는 **코드가 일부러 테스트에 링크되지 않기 때문**(실제 프로세스를 만들고 SCM 을 건드리는 코드가 테스트 바이너리에 있으면 그 테스트는 남의 세션을 망가뜨릴 수 있는 테스트가 된다), 02 는 OS 동작이라 관측 외 방법이 없고, 03·04 는 주입으로 대체한 대가, 06 은 사람이 봐야 하는 것.
- **③ 최종 상태 정리** — `docs/작업목록.md` 1.9 와 `docs/구현계획.md` 에 완료분·미검증분을 한 곳에 모았다. C++ 업데이트 **10종 729 checks / 0 failed**(release 80 · effects 147 · relaunch 57 · manifest 82 · state_machine 61 · http 36 · check 28 · payload_name 54 · version_compare 139 · install_registration 45).
- **다음 단계는 구현이 아니라 배포 준비**임을 명시했다: 버전 인상 + 설치본 생성 → 설치 → UPD-FIELD 실기. **사용자 승인 뒤 한 번에** 한다.
- 문서만 변경. 제품 코드·CMake·빌드·테스트 변경 없음. 버전 인상·설치본·설치·라이브 조작·배포·push 없음.
- 변경 파일: `docs/수동확인_체크리스트.md` · `docs/작업목록.md` · `docs/구현계획.md` · `docs/history.md`.

### 451) 2026-09-08 UPD-FIELD 체크리스트 정밀화 — 근거에 commit 해시, 방법에 실패 조건
- 검증용이 `03a56d2` 검사 시점에 "미검증 6항목이 문서에 없다" 며 NEEDS_CHANGES 를 냈는데, **그 시점 기준으로는 맞고 지금 기준으로는 아니다** — 항목은 그 다음 커밋 `2a5614f`(#450) 로 들어갔고 검증용이 그것을 아직 못 본 상태였다(우편함 지연). 사슬 확인: `03a56d2` → `2a5614f`.
- 다만 **함께 준 요구 두 가지는 실제로 빠져 있었고, 그건 반영했다**:
  - **근거 열에 commit 해시** — 표만 보는 사람이 코드로 돌아갈 수 있어야 한다는 요구. 여섯 행 전부에 해시와 **어느 심볼/파일인지**를 넣었다(예: `09e6aee` (`update_relaunch.cpp` `launch_via_shell`)). `update_relaunch.cpp` 행에는 **"어디에도 링크되지 않은 채 커밋된 코드"** 라고 적었다 — 표만 보는 사람이 가장 먼저 알아야 할 사실이다.
  - **실패 조건을 방법 열로** — UPD-FIELD-01 의 실행/환경을 **ⓐ 평소 데스크톱 / ⓑ Explorer 가 죽어 있는 세션 / ⓒ 다중 데스크톱·다중 세션** 셋으로 쪼갰다. 수락기준도 나눴다: ⓐ 는 무결성 수준 **Medium**(High 면 실패), **ⓑⓒ 는 "안 뜨는 것이 정답"** 이고 **승격된 채로 뜨면 실패**다. 그때 사용자에게 무엇으로 보이는지(창이 안 뜸/무반응/로그만)와 시작 메뉴 직접 실행이 정상인지까지 기록하게 했다.
- **공통 전제를 표 바로 위 한 줄로** 올렸다 — 아래 산문에만 있으면 표만 보는 사람이 못 읽는다: 설치 뒤에만 확인 가능하고, 지금 빌드본을 그대로 실행하면 **라이브 0.2.104 와 충돌**하며, 버전 인상·설치본은 **사용자 승인 뒤 한 번에**.
- 문서만 변경. 제품 코드·CMake·빌드·테스트 변경 없음. 버전 인상·설치본·설치·라이브 조작·배포·push 없음.
- 변경 파일: `docs/수동확인_체크리스트.md` · `docs/history.md`.

### 452) 2026-09-08 ⚠️ 업데이트 기능 — **부품은 있고 배선이 없다**: Codex NEEDS_CHANGES 수용, 잘못된 판정 2건 정정
- **거부 사유를 직접 재확인했고 전부 사실이다.** 추측이 아니라 `grep` 결과다:
  - **`run_update` 의 제품 호출자 0건** — 정의(`update_state_machine.cpp:64`)와 선언(`.hpp:159`)뿐. 상태기계·effects·staging·swap·rollback·registration·relaunch·health 를 **아무도 부르지 않는다.**
  - `WindowsUpdateEffects` 인스턴스화 · `make_relaunch_effects` · `make_registration_effects` · `enumerate_product_processes` · `request_process_stop` — 전부 **테스트 밖 0건**.
  - Host `host_app_main.cpp:1435-1443` 은 `MessageBoxW(MB_OK)` **뿐**(update/later 선택 없음), Client `client_shell_main.cpp:351-366` 은 상태 문구 **뿐**.
  - Android `UpdateManifest.kt` 호출자 **0건**, `REQUEST_INSTALL_PACKAGES` **0건**.
- **결론: 지금 이 빌드를 설치한 사용자는 업데이트를 할 수 없다.** 이것은 "UI 실기 0" 이 아니라 **기능 미배선**이다.
- ⚠️ **정정 1 — 미검증 목록이 사실과 달랐다.** `docs/수동확인_체크리스트.md` 가 **미구현(배선 없음)** 을 **구현했으나 실기 미실행**으로 적고 있었다. 두 가지는 성질이 전혀 다르다 — 하나는 사람이 확인만 하면 되고, 다른 하나는 **아직 만들어야 하는 것**이다. 여섯 행에 **[배선 필요] / [일부 배선 필요]** 를 붙이고, 정정 사유를 문서 머리에 남겼다.
- ⚠️ **정정 2 — "실행계층 링크 0" 을 격리라고 적은 것이 틀렸다.** `update_relaunch.cpp` 를 어디에도 링크하지 않은 것을 격리의 대가로 정당화하고 **"자동 회귀로 대체 불가"** 라고 적었는데, 그것은 격리 품질의 증거가 아니라 **그 코드가 한 번도 실행되지 않았다는 자백**이다. **`CreateProcess` 자체는 라이브 파괴가 아니다** — dummy exe·임시 설치 root·임시 로그를 주입하면 같은 OS 실행 경로를 실제로 검증할 수 있다. 지금 안 되는 이유는 실행 코드가 **제품 이미지 이름을 스스로 정하기 때문**이고, 받아들일 제약이 아니라 **고칠 설계**다.
- **신규 문서 `docs/업데이트_배선_계획.md`** (Codex 요구 제출물 2건):
  - **§1 남은 배선 W1~W8** — W1 **업데이터 실행 파일이 존재하지 않음**(`run_update` 를 부를 바이너리 자체가 없고, 나머지는 전부 그 위에 얹힌다. `installDir` **밖**에 설치돼야 하며 `validate()` 가 이미 그것을 강제) · W2 설치기 payload 미포함(목적지가 다른 항목을 다룰 자리가 `do_install()` 에 없음) · W3 Host update/later + **권한·락·staging 확보 뒤에만 종료**(먼저 죽으면 실패 시 되살릴 주체가 없다 — 실패의 기본값은 "아무 일도 일어나지 않음") · W4 Client 승격 실행 1회 + **취소해도 계속 사용 가능** · W5 Quiesce 열거 연결 · W6 Relaunch/Health 연결 · W7 Android 전부(확인·다운로드·sha256/서명/versionCode·설치 승인/**취소**/**권한 거부**) · W8 발행↔소비 추적표.
  - **§2 실행계층 검증 방법 E1~E8** — 이미지 표를 **주입 가능**하게(제품 표는 기본값, **허용목록 성질 불변**) + `update_relaunch.cpp` 를 **테스트에 링크**. dummy exe 로 실제 실행·중복 없음·감독 대상 미실행·표 밖 이름 미실행·셸 경유 실행·**폴백 부재**·서비스 실패 경로·health 신선도. **한계도 미리 적었다**: 비승격 세션에서는 무결성 대비 불가(E5), 서비스 성공 경로는 관리자 필요(E7) — **"검증했다" 와 "여기까지만 검증했다" 를 섞지 않는다.**
  - **§2.4 실 HTTPS**: 격리 서버 + 테스트 전용 CA 로 정상 체인 1건과 실패 경로(만료·이름 불일치·https→http·URL 자격증명). ⚠️ **운영 인증서 검증 우회는 계속 금지** — `WINHTTP_OPTION_SECURITY_FLAGS` 는 앞으로도 설정하지 않고, 테스트 CA 는 테스트 프로세스 신뢰 저장소에만 둔다.
- 유지: 하위폴더 생성·롤백의 신규파일 제거는 **계약 완성으로 승인**됐으므로 되돌리지 않는다. 운영키 fail-closed 유지 — **다만 그것이 이 코드를 만들지 않을 이유는 아니다.**
- 문서만 변경. 제품 코드·CMake·빌드·테스트 변경 없음. 버전 인상·설치본·설치·라이브 조작·배포·push 없음.
- 변경 파일: `docs/업데이트_배선_계획.md`(신규) · `docs/수동확인_체크리스트.md` · `docs/history.md`.
- 상태: 제출물 2건 완료. **재개 범위(업데이터 실행 진입점 → Host/Client 배선 → Android → 추적표) 착수.**

### 453) 2026-09-08 실행계층을 **실제로 실행**시켰다 — 이미지 표 주입 + 항목별 재실행 결과 + E1~E8
- #452 에서 철회한 판단을 코드로 되돌렸다. **`update_relaunch.cpp` 가 이제 테스트에 링크된다.** "어디에도 링크되지 않음" 은 격리가 아니라 **한 번도 실행되지 않았다는 뜻**이었다.
- **R1 이미지 표 주입** — `relaunch_plan(stopped, table)` 추가, 제품 표(`product_images()`)는 기본값. **허용목록 성질은 그대로다**(표에 없는 이름은 여전히 못 뜬다). 바뀐 것은 "표가 어디서 오는가" 뿐이라, dummy 이름표를 주면 **같은 실행 경로가 임시 디렉터리에서 실제로 돈다.**
- ⚠️ **R2 재실행 결과를 `bool` 하나에서 항목별 구조체로** (검증용/Codex 지적). `bool` 하나면 **호스트가 못 뜬 것과 클라이언트가 못 뜬 것이 같은 값**이 된다. 그 둘은 급이 다르다 — 클라이언트가 안 뜨면 시작 메뉴에서 켜면 되지만, **호스트가 안 뜨면 원격 사용자는 그 기계에 다시 접속할 수단이 없다.** `RelaunchOutcome{imageName, kind, started, skipped, detail}` + `failed()`.
- ⚠️ **"안 뜨는 것이 정답" 을 정정했다.** 승격 폴백을 두지 않은 것은 옳지만 **그 결과를 성공으로 기록하면 안 된다.** 셸 경로가 없어 클라이언트가 못 뜨면 `failed()` 로 기록되고, `relaunch_user_notice()` 가 **"업데이트는 완료되었습니다. 다만 X 을(를) 자동으로 다시 시작하지 못했습니다. 시작 메뉴에서 직접 실행해 주세요."** 를 만든다. 세 가지를 이 순서로 말한다 — 업데이트는 됐다 / 이건 안 돌아왔다 / 이렇게 하면 된다. **아무 말 없이 끝나는 것이 가장 나쁘다**(사용자는 업데이트가 프로그램을 지웠다고 생각한다).
- **E1~E8 신설**(`remote60_update_relaunch_test` 57→**86 checks**). 임시 root + dummy `.cmd` 4종(각자 자기 이름을 witness 파일에 적는다) + 없는 서비스명 + 임시 로그. **실제로 프로세스가 떴는지를 witness 로 확인한다.**
  - E1 실제 실행 · E2 **중복 없음**(2개 정지 → 1회 실행) · E3 감독 대상 미실행 + **skipped 는 실패가 아님** · E4 표 밖 이름 미실행 · **E5 셸 경유 실행이 이 기계에서 실제로 성공**(`launch_via_shell` 최초 실행) · E6 셸 경로 없을 때 **실행 0 + 성공으로 보고하지 않음** · **E6b 안전한 실패가 실패로 기록되고 사용자에게 이름을 대며 "업데이트는 완료" 를 함께 말하는지** · **E6c 호스트 실패와 클라 실패가 다른 값인지** · E7 서비스 실패 경로 · E8 health 신선도(mark 앞 보고는 불충분, 뒤 보고는 충분).
  - E5 는 셸 경로 유무에 따라 **양쪽 다 단정**한다 — 없으면 "실행 0 + 사용자 안내" 를 요구하므로, 환경이 어떻든 **안 돌아서 통과하는 일이 없다.**
- **대조군 2회 실측**: 금지된 승격 폴백을 되살리면 **7 FAIL**, 감독 대상을 띄우게 하면 **2 FAIL**. 끄면 실패하고 켜면 통과한다.
- 부수 결함 2건 수정: `check()` 인자 평가 순서 때문에 E8 이 **직전 호출의 detail** 을 찍고 있었다(단정은 옳았고 표시가 틀렸다) → 결과를 변수로 받아 호출. 정리 helper 의 경로 조립에 **치환되지 않은 자리표시자**가 남아 `%TEMP%` 를 하나도 못 지우고 있었다 → 수정하고 잔여물 0 확인.
- 검증 — **`qwinsta`: `console` 만 Active, `rdp-tcp` 는 Listen.** C++ 업데이트 **10종 758 checks / 0 failed**(relaunch 57→**86**). 전체 빌드 오류 0. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- **아직 배선은 안 됐다.** `run_update` 제품 호출자는 여전히 0건이다 — 다음이 W1(업데이터 실행 파일)이다.
- 변경 파일: `update_relaunch_plan.{hpp,cpp}` · `update_relaunch.{hpp,cpp}` · `update_relaunch_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/업데이트_배선_계획.md`(W4a·W4b·W4c·E6b·E6c) · `docs/수동확인_체크리스트.md`(UPD-FIELD-00 신설, 01 수락기준 정정) · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음. 상태: **W1 착수 예정.**

### 454) 2026-09-08 **`run_update` 에 드디어 제품 호출자가 생겼다** — `GNLinkUpdater.exe` (W1) + W2a 설치 위치 확정
- **`grep run_update` 결과가 바뀌었다**: `update_state_machine.cpp:64`(정의) · `updater_main.cpp:297`(**호출**). #452 이후 이 줄이 이번 작업의 진척 지표다.
- **W2a 결정 — 업데이터를 어디에 설치하는가**: 두 안 중 **(나) `installDir` 안에 넣고 실행할 때 자기 복사본으로 재실행**을 골랐다.
  - (가) 목적지를 항목별 속성으로 두고 `installDir` **밖**에 설치 → **치명적**: payload 이름은 상대 경로만 허용하고 traversal 을 거부하므로(#442·#444) **밖에 있는 파일은 교체 대상이 될 수 없다.** 업데이터가 **컴파일 시점 상수에 묶여 영원히 남는 두 번째 유지보수 바이너리**가 된다 — 등록에서 (C)안을 탈락시킨 것과 **똑같은 덫**(#440). 언인스톨이 `installDir` 을 지우므로 밖의 파일은 **고아**로도 남는다.
  - (나)는 설치 코드에 새 구조가 전혀 생기지 않는다(`kPayload` 루프도 언인스톨도 그대로). 실행 시 `%ProgramFiles%\GNLink.update\` 로 자기를 복사해 그 복사본이 일하고, `installDir\GNLinkUpdater.exe` 는 **보통 파일처럼 교체·백업·롤백**된다.
  - ⚠️ **작업 디렉터리는 관리자 전용이어야 한다.** 복사본이 상승된 채 실행되므로 사용자 쓰기 가능한 곳에 두면 **복사와 실행 사이에 바꿔치기할 창**이 생긴다 — 권한 상승 취약점. **초안이 `%ProgramData%` 를 적었던 것을 정정했다** — 설계 1차 조사(#427)가 이미 `%TEMP%`/`%ProgramData%` 를 **명시적으로 배제**해 뒀고(설치기가 지키는 "LocalSystem 바이너리는 사용자 쓰기 가능한 폴더에 두지 않는다" 불변식을 되돌리므로), 위치는 **`%ProgramFiles%\GNLink.update\`** 다. `%ProgramFiles%` 아래는 기본 ACL 이 이미 관리자 전용이라 **ACL 을 세울 필요가 없다 — 세워야 하는 방어는 빠뜨릴 수 있는 방어다.** `GNLink.update` 는 `GNLink` 의 형제이지 하위가 아니므로(다음 글자가 구분자가 아니라 `.`) "work 가 installDir 안" 검사에 걸리지 않는다(회귀로 고정).
- ⚠️ **`validate()` 를 이름 비교 → 전체 목적지 경로 비교로 고쳤다.** 이름으로 비교하면 (나)가 원천 봉쇄된다 — 교체되는 파일과 실행 중인 파일이 **다른데도** 이름이 같아 거부된다. 실제로 참이어야 하는 명제는 더 좁다: **이 스왑의 어떤 목적지도 지금 실행 중인 이미지여서는 안 된다.** 디렉터리 검사(업데이터가 `installDir` 안이면 거부)는 그대로라 방어는 줄지 않는다. 회귀 3종으로 고정: 목적지가 실행 이미지면 거부 / **이름만 같고 디렉터리가 다르면 허용**(이게 자기 갱신을 가능하게 하는 지점) / 이름이 달라도 목적지가 실행 이미지면 거부.
- **`updater_options.{hpp,cpp}` + 47 checks** — 관리자로 도는 프로세스의 명령줄이라 **거부가 본론**이다. 필수 10개 전부 없으면 거부(각각 이름을 댐) · **모르는 인자는 무시가 아니라 오류**(오타난 플래그를 관리자 권한으로 "나머지는 실행" 하는 것이 더 나쁘다) · 상대 경로 거부 · **https 강제** · staging/work 가 `installDir` 안이면 거부 · 비슷한 이름의 형제 디렉터리는 "안" 이 아님.
- **`updater_main.cpp` — 여기서 전부 연결된다**: `enumerate_product_processes`/`request_process_stop`(W5) · `make_registration_effects`(버전을 manifest 에서) · `make_relaunch_effects`(**Quiesce 가 실제로 정지시킨 목록**을 캡처해 넘김, W6) · `https_get_file`(아티팩트 크기가 곧 상한) · `run_update`.
  - **`ReadySignaller` 데코레이터**: `VerifyDownload` 성공 **그 지점에서만** ready event 를 signal 한다. 락을 쥐고 바이트를 검증한 뒤라, **호스트가 그때 나가도 이 프로세스가 끝내지 못할 상태로 남지 않는다.** 그전에 나가면 실패 시 되살릴 주체가 없다.
  - **`CREATE_BREAKAWAY_FROM_JOB`**: 호스트가 job object 에 속해 있으면 그 자식인 업데이터도 호스트 정지와 함께 죽는다(설계 3.2 ②). 실패 시 그 플래그 없이 재시도하고 **그 사실을 로그에 남긴다**.
  - **종료 코드로 결과를 구분**: 0 성공/할 일 없음 · **10 `UpdatedButNotRelaunched`**(설치는 됐고 무언가 안 돌아옴) · 11 교체 전 포기 · 12 롤백 · **13 롤백 실패**(사람이 봐야 하는 유일한 경우).
- **`remote60_updater` 는 `update_process_targets.cpp` 와 `update_relaunch.cpp` 를 링크하는 유일한 제품 바이너리다** — 실제 `GNLinkHost.exe` 를 이름으로 찾고 프로세스를 띄우는 두 TU 가 처음으로 제품에 들어갔다.
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **11종 807 checks / 0 failed**(신규 updater_options 47, effects 147→**149**). 전체 빌드 오류 0. `GNLinkUpdater.exe` 397,824 bytes 생성. JS 디렉터리 스위트 exit 0.
- **업데이터를 실행하지 않았다** — `requireAdministrator` 라 실행하면 사용자에게 UAC 프롬프트가 뜬다. 명령줄 로직은 `remote60_updater_options_test` 47 checks 로 덮여 있고, 바이너리 자체의 실행은 UPD-FIELD 로 남는다.
- 라이브 무영향: `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변, `DisplayVersion` **0.2.104**.
- **다음: W2**(설치기 payload 에 `GNLinkUpdater.exe` + `%ProgramFiles%\GNLink.update\` 생성) **→ W3·W4**(Host update/later + handoff, Client 승격 실행).
- 변경 파일: `updater_options.{hpp,cpp}`·`updater_options_test.cpp`·`updater_main.cpp`(신규) · `update_effects.cpp`·`update_effects_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/업데이트_배선_계획.md`(W2a) · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음.

### 455) 2026-09-08 W2 — 설치기가 업데이터를 함께 설치한다 (payload + 작업 디렉터리 + 언인스톨)
- W2a 결정(#454) 덕분에 **설치 코드에 새 구조가 생기지 않았다.** 업데이터가 `installDir` 안으로 들어가므로 `kPayload` 루프도 언인스톨 루프도 그대로 쓰인다 — "목적지가 다른 항목을 다룰 자리가 없다" 는 문제 자체가 사라졌다.
- **payload 추가**: `IDR_PAYLOAD_UPDATER 208` · `payload.rc.in` 한 줄 · CMake staging(`GNLINK_PAYLOAD_UPDATER` + `copy_if_different` + `DEPENDS remote60_updater` + `remote60_installer_payload`) · `kPayload` 항목. 넣은 자리마다 **왜 밖이 아니라 안인지**를 주석으로 남겼다(밖이면 payload 이름이 상대 경로 전용이라 **영원히 교체 불가**, 언인스톨은 **고아**).
- **작업 디렉터리 `%ProgramFiles%\GNLink.update`**: 설치 시 만든다. 업데이터도 스스로 만들지만, 설치 때 만들어 두면 **무엇이 쓰이기 전에 `%ProgramFiles%` 에서 상속된 ACL 을 갖고 시작한다.** 실패해도 설치는 계속한다 — 사용자는 제품을 설치하러 온 것이고 이 폴더는 첫 업데이트 때만 의미가 있다.
  - **형제이지 자식이 아니다.** 자식이면 교체 대상 디렉터리 안이라 업데이터가 거부한다. 그리고 관리자 전용이어야 한다 — 여기서 도는 것이 상승된 채 돌기 때문이다. `%ProgramFiles%` 아래는 기본 ACL 이 이미 그러하므로 **세울 것이 없고 따라서 빠뜨릴 것도 없다.**
- **언인스톨이 지운다**: `installDir` 을 지워도 **밖에 있는 작업 복사본은 따라가지 않는다.** 파일을 지우고 디렉터리를 없애되, 지금 업데이트가 돌고 있으면 복사본이 열려 있으므로 설치기 자기 이미지와 **같은 `MOVEFILE_DELAY_UNTIL_REBOOT` 폴백**을 쓴다.
- `stop_running_product()` 의 `kImages` 는 **건드리지 않았다.** 돌고 있는 업데이터는 `GNLink.update` 의 복사본이고 설치 디렉터리의 파일은 잠겨 있지 않으므로, 설치기가 필요한 것을 막지 않는다. (`taskkill /F /T` 로 업데이터를 죽이면 그 자식인 재실행된 제품까지 딸려 죽는다 — 원장 (b) 와 같은 성질이다.)
- 검증 — **`qwinsta`: console 만 Active.** 전체 빌드 오류 0. C++ 업데이트 **11종 808 checks / 0 failed**.
  - **업데이터가 실제로 Setup 안에 박혔는지 바이트로 확인**: `GNLinkSetup.exe` 에 업데이터의 명령줄 리터럴 `--running-from-copy` 가 **UTF-16 으로 존재**. staging 디렉터리에 `GNLinkUpdater.exe` 397,824 bytes, Setup 4,009,984 bytes.
  - **설치기·업데이터 둘 다 실행하지 않았다** — 실행하면 실제로 설치되거나 UAC 프롬프트가 뜬다. `C:\Program Files\GNLink.update` 가 **생기지 않았음**을 확인했다.
- 라이브 무영향: `GNLinkHost`(5156)·`GNLinkInputService`(10820)·`GNLinkStream`(19384) PID 불변, `DisplayVersion` **0.2.104**.
- **다음: W3·W4** — Host 의 update/later 승인 + **권한·락·staging 확보 뒤에만 종료**하는 handoff, Client 의 승격 실행(UAC 정확히 1회, 취소는 오류 아님).
- 변경 파일: `installer/installer_ids.h`·`installer/payload.rc.in`·`installer/installer_main.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md`.
- 버전 인상·설치본 배포·설치·라이브 조작·push 없음.

### 456) 2026-09-08 W3 — 호스트가 업데이터를 띄운다, 그리고 **언제 비켜도 되는지**를 신호로 정한다
- **호스트는 교체 대상 파일 중 하나**라 스왑을 하려면 나가야 한다. **언제 나가느냐가 전부**다. 업데이터를 띄우자마자 나가면, 업데이트가 실패했을 때(서버 불통·해시 불일치·안 움직이는 파일) **되살릴 주체가 아무도 없고 사용자에게는 제품이 그냥 사라진 것으로 보인다.**
- 그래서 순서를 고정했다: 업데이터가 **락 획득 → 다운로드 → 검증**을 마친 뒤에만 signal 하고, **그 뒤에만** 호스트가 창을 닫는다. 신호 이전은 전부 **아무것도 안 하면 원상복구**되는 구간이다(디스크가 아직 안 바뀌었다).
- **`update_handoff.{hpp,cpp}` + 29 checks** — 판정을 순수 함수로 분리해 프로세스 없이 시험한다.
  - `updater_arguments()`: 호스트·클라이언트가 같은 프로세스에 **서로 다른 인자 집합을 주는 일이 없도록** 한곳에서 만든다. 빈 선택 인자는 `""` 로 넘기지 않고 **아예 뺀다** — 업데이터가 "이 인자가 없다" 고 또렷이 말하는 것을, `--log ""` 가 알 수 없는 실패로 바꿔 버린다.
  - `make_ready_event_name()`: **매 시도마다 다른 이름**. 이미 signal 된 옛 이벤트가 남아 있으면 **업데이터가 아무것도 하기 전에 호스트에게 나가라고 말한다** — 이 악수가 막으려던 바로 그 실패를 이름 재사용으로 도달하는 것. `Local\` 이지 `Global\` 이 아니다(호스트와 그 자식은 같은 세션).
  - `handoff_verdict()`: **8가지 조합 전부를 돌려 `ExitNow` 가 나오는 4가지가 모두 signal 을 거쳤음을 단정**한다 — "나가도 되는 길은 신호 하나뿐" 이 사례가 아니라 **성질**로 고정된다.
  - ⚠️ **순서가 중요한 지점**: signal 뒤에 업데이터가 종료했어도 **`ExitNow`** 다. manifest 가 "할 일 없음" 이었던 경우 신호는 약속을 지킨 것이고, 종료를 실패로 읽으면 **이미 나가겠다고 한 호스트가 계속 돌아 스왑이 파일을 잡고 있는 것을 만난다.**
- **호스트 배선**: `UpdateAvailable` 일 때만 **update/later 선택**(나머지 넷은 질문이 아니라 진술 — "서버에 못 닿았다" 뒤에 "업데이트할까요" 는 할 수 없는 것을 제안하는 것). **later 는 무시할 거절이 아니라 정상 답**이고 아무 일도 일어나지 않는다.
  - 업데이터는 **자식으로** 띄운다(호스트가 이미 상승돼 있어 토큰 상속, **UAC 없음**). `CREATE_BREAKAWAY_FROM_JOB` — job object 안이면 **호스트 정지와 함께 죽는데 정지가 바로 다음 단계**다. 실패 시 플래그 없이 재시도하고 **로그에 남긴다**.
  - ready event 는 **업데이터를 띄우기 전에** 만든다(아무도 안 듣는 이벤트를 signal 하는 창이 없도록). **manual-reset** — 관측되기 전에 소비되면 안 된다.
  - 대기는 **UI 스레드 밖**에서 한다(다운로드 도중에도 창이 응답해야 하고, 사용자는 교체 직전까지 제품을 쓴다). `WaitForMultipleObjects(ready, process, 10분)` → 판정을 UI 스레드로 post.
  - **신호를 못 받은 모든 경우는 같게 다룬다**: 호스트는 계속 돌고, **"설치된 버전은 그대로이며 계속 사용할 수 있습니다"** 를 알린다. 반쯤 됐는지 사용자가 궁금해하지 않게.
- 검증 — **`qwinsta`: console 만 Active.** 전체 빌드 오류 0. C++ 업데이트 **12종 837 checks / 0 failed**(신규 handoff 29). 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**. **호스트·업데이터 미실행**(빌드본 실행은 라이브 0.2.104 와 충돌).
- **다음: W4** — Client 의 승격 실행(UAC 정확히 1회, **취소는 오류가 아님**) → W7 Android → W8 추적표.
- 변경 파일: `update_handoff.{hpp,cpp}`·`update_handoff_test.cpp`(신규) · `host_app_main.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md`.
- 버전 인상·설치본 배포·설치·라이브 조작·push 없음.

### 457) 2026-09-08 W4 — 클라이언트의 승격 실행(**취소는 오류가 아니다**) + ⚠️ 호스트가 안 돌던 기계에서 좋은 업데이트를 롤백하던 결함
- **클라이언트는 비상승**이라 `%ProgramFiles%` 를 스스로 교체할 수 없다. `ShellExecuteExW(L"runas")` 로 업데이터를 띄워 **UAC 를 정확히 1회** 발생시킨다.
- ⚠️ **취소는 답이지 실패가 아니다.** `ElevationOutcome{Launched, Cancelled, Failed}` 로 나눴다(`ERROR_CANCELLED`=1223). 거절하면 **아무것도 보여주지 않고, 재시도하지 않고, 다른 승격 경로를 찾지도 않는다** — 사용자는 여전히 동작하는 프로그램을 갖고 있고, 그게 애초에 거절할 수 있었던 이유다. **거절을 오류로 보고하는 것은 방금 고른 것이 안 됐다고 말하는 것**이다. `Failed` 와 분리한 이유가 그것이고, 둘의 올바른 반응이 다르다.
- 클라이언트는 **ready event 를 쓰지 않는다** — 비상승이라 나갈 허락을 기다릴 필요가 없고, 업데이터가 다른 제품 프로세스와 똑같이 정지시킨다.
- 페이지가 `type:"update"` 를 보내면 실행하되, **확인이 실제로 찾은 버전이 있을 때만** 한다. 없는데 승격하면 **뒤에 아무것도 없는 UAC 프롬프트**가 되고, 그건 안 띄우느니만 못하다.

- ⚠️⚠️ **배선하다 실제 결함을 발견했다 — 좋은 업데이트를 롤백하고 있었다.**
  - 클라이언트발 업데이트를 **호스트가 안 돌고 있는 기계**에서 하면: 정지된 목록에 호스트가 없다 → 재실행 계획에 호스트가 없다 → **호스트가 health 보고를 영원히 안 쓴다** → `HealthCheck` 타임아웃 → **롤백**. 파일은 완벽히 맞는데 **애초에 돌지 않던 프로세스의 증거를 요구해서** 되돌린다.
  - `RelaunchConfig::healthReporterImage` 신설. **그 이미지가 재실행되지 않았으면 health 는 즉시 만족**하고 이유를 남긴다("nothing that reports health was relaunched"). 지름길이 아니다 — **없던 프로세스의 증거를 요구하는 것은 실패를 지어내는 것**이고, `directory=not-configured` 를 실패로 치지 않는 것과 같은 논리다.
  - **회귀 E9 + 대조군**: reporter 미재실행 시 **즉시**(0 ms, 타임아웃을 기다리지 않음) 만족 / reporter 가 재실행됐으면 **보고 없으면 여전히 실패**. 대조군이 없으면 "아무것도 안 기다리는 health" 로도 통과한다.

- **원장 (b) 와의 연결을 설계문서에 기록**(요청분): `/T` 폐기를 축으로 세 가지가 함께 성립해야 닫힌다 — ① 업데이터를 `stop_running_product()` 의 `kImages` 에 **넣지 않음**(넣으면 `taskkill /F /T` 가 업데이터와 **그 자식인 재실행된 제품까지** 죽인다 — (b) 를 고치러 와서 (b) 를 새로 만드는 것. 그리고 돌고 있는 업데이터는 복사본이라 `installDir` 의 파일을 하나도 잠그지 않으므로 **넣을 이유도 없다**) ② 자기 복사본 재실행(W2a) ③ `CREATE_BREAKAWAY_FROM_JOB`. 언인스톨의 `MOVEFILE_DELAY_UNTIL_REBOOT` 재사용도 함께 적었다 — **새 메커니즘을 만들지 않았다.**
- 검증 — **`qwinsta`: console 만 Active.** 전체 빌드 오류 0. C++ 업데이트 **12종 845 checks / 0 failed**(relaunch 86→**90**, handoff 29→**33**). `client_shell_bridge_test` PASS. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**. **호스트·클라이언트·업데이터·설치기 전부 미실행.**
- **다음: W7 Android → W8 추적표.**
- 변경 파일: `update_handoff.{hpp,cpp}`·`update_handoff_test.cpp` · `update_relaunch.{hpp,cpp}`·`update_relaunch_test.cpp` · `updater_main.cpp` · `client_shell_main.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/업데이트_기능_설계.md`((b) 연결) · `docs/history.md`.
- 버전 인상·설치본 배포·설치·라이브 조작·push 없음.

### 458) 2026-09-08 W7 — Android 업데이트 전 흐름 배선 (확인·다운로드·검증·설치 승인/취소/권한거부)
- **`UpdateManifest.kt` 는 파싱만 있고 부르는 사람이 0명이었다.** 이번에 확인→다운로드→검증→설치가 실제로 연결됐고, `MainActivity` 가 그것을 부른다(C++ 쪽에 적용한 **"제품에서 이 경로가 불리는가"** 를 Android 에도 그대로 적용).
- **결정과 실행을 분리**했다 — C++ 쪽과 같은 배치:
  - **`UpdateDecision.kt`(순수, JVM 테스트 18건)** — 무엇을 설치할지·무엇이 도착했는지·거절이 무엇인지.
  - **`UpdateInstaller.kt`(기기 필요)** — 권한 확인, `PackageInstaller` 세션, 상태 수신.
  - **`UpdateFlow.kt`** — 둘을 잇는 얇은 층. 규칙은 전부 테스트가 닿는 곳에 있다.
- ⚠️ **`versionCode` 가 권위이지 `versionName` 이 아니다.** Android 는 패키지 교체 여부를 `versionCode` 로 판단하므로, **이름은 더 새로운데 코드가 크지 않으면 사용자가 승인을 누른 뒤에 시스템이 거절한다.** 묻기 전에 아는 편이 낫다. `versionCode` 가 없으면 **설치하지 않는다** — 이름만 보고 설치하는 것은 시스템이 절대 추측하지 않는 그 하나를 추측하는 것이다.
- **다운그레이드를 `UpToDate` 와 분리**했다. Android 가 어차피 거절하고, 대개 **배포 실수**를 뜻하므로 로그에 따로 남을 값어치가 있다.
- **APK 는 정확히 1개일 때만** 고른다. 없으면 우리 것이 아니고, 여럿이면 고를 근거가 없다 — 첫 번째를 집는 것은 **서명이 표현하지 않은 규칙을 지어내는 것**이다.
- ⚠️ **거절은 답이지 실패가 아니다.** `InstallOutcome{Installed, Cancelled, PermissionDenied, Failed}` + `isUserDecision()`. 시스템 대화상자를 닫은 것(`STATUS_FAILURE_ABORTED`=3)과 설치 권한을 안 준 것은 **오류 메시지를 띄우지 않는다** — 사용자는 여전히 동작하는 앱을 갖고 있고 그게 거절할 수 있었던 이유다. **회귀로 성질을 고정**했다: 열거값 전체 중 사용자 결정이 정확히 2개 — 새 결과를 추가하면 어느 쪽인지 정하지 않고는 통과하지 못한다.
- **`STATUS_PENDING_USER_ACTION` 은 결과가 아니다.** 시스템이 확인 화면을 띄워 달라는 것이고, 이것을 결과로 다루면 **사용자에게 묻기도 전에 실패를 보고**하게 된다.
- **`PackageInstaller` 세션(B안)**: 스트림을 받으므로 **`FileProvider` 불필요**, content URI 를 남에게 주지 않으며 APK 가 앱 전용 저장소를 벗어나지 않는다. 그리고 **상태 코드를 돌려주므로 닫힌 대화상자와 진짜 실패를 구분**할 수 있다 — 파일 Intent 방식에서는 둘 다 "아무 일도 없음" 으로 보인다.
- **다운로드는 앱 전용 캐시에**, 파일명은 **manifest 의 이름을 쓰지 않고 고정**한다(검사를 통과한 이름이지만 문서에서 온 문자열이 경로가 되는 것 자체를 없앤다). 크기 초과는 **도착하는 중에** 끊는다. 검증 실패 파일은 **지운다** — 남겨 두면 나중에 좋은 것으로 오인될 수 있다.
- **시작 시 확인은 기다리지 않는다**(daemon 스레드). 서버에 못 닿는 것이 일상인 기기에서 **로그인 화면이 안 뜨면 기차에서 못 쓴다**. 사용자에게 말하는 것은 **"새 버전이 있다" 뿐**이고 나머지는 진단 로그로 간다. **"나중에" 는 답**이고 그 실행에서 다시 묻지 않는다.
- **권한을 다운로드 전에 확인**한다 — 없는데 먼저 받으면 **다 쓰고 나서야 권한이 필요하다는 걸 알게 된다.** 권한 화면으로 보낸 뒤 **자동 재시도하지 않는다**(거절은 답이고 쫓아다닐 일이 아니다).
- `AndroidManifest.xml` 에 **`REQUEST_INSTALL_PACKAGES`** 선언. `BuildConfig.UPDATE_MANIFEST_URL` / `UPDATE_PUBLIC_KEY_HEX` 는 **기본 빈 값**이고, 빈 값은 스텁이 아니라 **진짜 답**이다("이 빌드는 확인할 수 없다"). 그럴듯한 값을 박아 두면 **확인을 시도하고 실패하는 빌드**가 되는데, 못 한다는 걸 아는 빌드보다 나쁘다.
- 검증 — **`qwinsta`: console 만 Active.** Android **`assembleDebug` 성공**, **JVM 단위 테스트 31건 / 0 실패**(신규 `UpdateDecisionTest` **18**, 기존 manifest 11 + versionCompare 2). C++ 업데이트 12종 **845 checks / 0 failed**, JS 디렉터리 스위트 exit 0. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- **APK 를 기기에 설치하거나 실행하지 않았다.** 실제 설치·권한·취소 경로는 실기 항목이다.
- **다음: W8** 서버 발행 ↔ 클라이언트 소비 추적표.
- 변경 파일: `UpdateDecision.kt`·`UpdateInstaller.kt`·`UpdateFlow.kt`·`UpdateDecisionTest.kt`(신규) · `MainActivity.kt` · `AndroidManifest.xml` · `app/build.gradle.kts` · `docs/history.md`.
- 버전 인상·설치본 배포·설치·라이브 조작·push 없음.

### 459) 2026-09-08 W8 — 서버 발행 ↔ 세 런타임 소비를 **표가 아니라 테스트가** 지키게 했다 (+ Android 3중 게이트 회귀)
- **세 런타임이 공유 벡터와 일치한다는 것은 각자의 스위트가 이미 보인다. 보이지 않는 것은 서버가 실제로 발행하는 문서가 그 벡터와 같은 모양이냐다.** 여기가 어긋나면 **세 스위트가 전부 초록인 채로 현장에서는 아무것도 설치되지 않는다.** 문서 표만 두면 낡는다.
- **신규 `apps/directory/test/update_publish_contract_test.js` (20 checks)** — `buildManifest` 출력의 **키 집합**과 **공유 벡터의 키 집합**을 기계적으로 비교하고, 서버가 자기 출력을 되읽어 각 필드가 살아남는지 확인한다. `test/run.js` 에 등록.
  - 특히 고정한 둘: **이름 안의 구분자가 안 뭉개진다**(`ui\shell.html` 이 `uishell.html` 로 오면 다른 곳에 쓴다) · **windows 릴리스에는 `versionCode` 줄이 없다**(없는 것이 0 으로 읽혀 실제 값처럼 다뤄지면 안 된다). 그리고 android 문서에서 `versionCode` 는 **artifact 줄들 뒤에** 나오는데, 첫 artifact 줄에서 멈추는 파서였다면 이걸 잃고 **Android 는 아무것도 설치하지 않는다.**
- **설계문서 3.14 신설** — 필드별로 **서버 발행 / C++ 이 쓰는 곳 / Kotlin 이 쓰는 곳 / 없으면 무슨 일이 생기는지**. `releaseId` 가 C++ 에서는 staging 키인데 Kotlin 에서는 미사용이고, `versionCode` 는 그 반대라는 **비대칭**이 표에서 드러난다.
- **Android 3중 게이트 회귀 신설 `UpdateGatesTest` (7건)** (검증용 지적 반영: "하나라도 빠지면 나머지가 무의미해지는 조합"):
  - 게이트를 **따로따로가 아니라 조합으로** 시험한다. 같은 릴리스를 통과시킨 뒤 **한 번에 하나씩만 망가뜨려** 매번 거부를 요구한다.
  - **서명**: 다른 키의 서명 거부 · **서명 뒤 문서 수정**(`versionCode=12`→`999`, 강제 설치를 노리면 바꿀 바로 그 값) 거부.
  - **`versionCode`**: **서명이 완벽히 통과해도** `versionCode` 가 없으면 거부 — **유효한 서명이 설치 가능성을 만들지 않는다.** 낮으면 `Downgrade`.
  - **sha256**: 게이트 1·2 를 다 통과한 뒤에도 **도착한 바이트는 별개 문제**. 크기·해시 각각 거부, 맞으면 통과.
  - 마지막 케이스가 셋을 **한 자리에서** 보인다 — 어느 하나를 걷어내면 그 셋 중 하나가 통과한다.
  - 이를 위해 `UpdateFlow.evaluateDocument()` 를 분리했다(네트워크 없이 게이트 전체를 구동).
- 검증 — **`qwinsta`: console 만 Active.** Android **JVM 38건 / 0 실패**(신규 gates 7 + decision 18, manifest 11, versionCompare 2), `assembleDebug` 성공. JS 디렉터리 스위트 **전부 통과**(신규 contract 20 포함). C++ 업데이트 **12종 845 checks / 0 failed**. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- **이것으로 `docs/업데이트_배선_계획.md` §1 의 W1~W8 이 전부 끝났다.** 남은 것은 실기(UPD-FIELD-00~06)와 배포 준비다.
- 변경 파일: `apps/directory/test/update_publish_contract_test.js`(신규)·`test/run.js` · `UpdateGatesTest.kt`(신규)·`UpdateFlow.kt` · `docs/업데이트_기능_설계.md`(3.14) · `docs/history.md`.
- 버전 인상·설치본 배포·설치·라이브 조작·push 없음.

### 460) 2026-09-08 W8 마무리 — 플랫폼 분리 단정, **헛단정 1건 자체 발견**, 배선 완료 상태 정리
- **검증용 지적 반영**: "플랫폼별 아티팩트 목록이 다르다는 점도 스키마가 표현해야 한다". 스키마는 문서 수준의 `platform` 과 서버의 플랫폼별 파일(`<platform>.manifest`)로 그것을 표현하고 있었는데, **아무도 단정하지 않고 있었다.** 계약 테스트에 추가: 두 플랫폼의 파일 목록이 겹치지 않음 · 각자 자기 플랫폼을 명시 · 아키텍처가 다름 · **한쪽 문서를 다른 쪽으로 요청하면 거부**.
- ⚠️ **그 단정의 첫 판이 헛단정이었고 스스로 잡았다.** 가짜 서명으로 `loadManifest` 를 부르니 **플랫폼을 보기도 전에 `SignatureInvalid` 로 거부**됐다 — 통과는 했지만 **다른 질문에 대한 옳은 답**이었다. 실제로 서명해서 게이트에 도달하게 고쳤고, **대조군**(제대로 서명해 자기 플랫폼으로 요청하면 `Ok`)을 앞에 뒀다. 그게 없으면 "거부됨" 이 "문서가 애초에 못 읽히는 것" 을 뜻할 수도 있다. 이제 실패 사유가 **`WrongPlatform`** 으로 나온다.
- **문서를 완료 상태로 정리**:
  - `docs/업데이트_배선_계획.md` 머리에 **당시 → 지금 대조표**. §0 의 "0건" 표는 **그대로 남긴다** — **무엇이 없었는지가 무엇을 만들었는지보다 나중에 더 쓸모 있다.**
  - `docs/수동확인_체크리스트.md` 의 **`[배선 필요]` 태그 전부 해제**(이제 진짜로 "구현됐고 실기가 남았다"). 다만 **왜 정정했는지는 남겨 뒀다** — 미구현과 미실행은 성질이 다르고, 다음에 이 표에 적을 때도 그 구분을 지켜야 한다.
  - `docs/작업목록.md` 1.9 에 배선 완료 + **배선 중 발견한 제품 결함 3건** 기록.
- **배선이 잡은 결함 3건을 한자리에 정리했다** — 부품 테스트로는 각 조각이 다 옳게 동작해 드러나지 않고 **실제로 이어 붙였을 때만** 나오는 것들이다: ① 호스트가 안 돌던 기계에서 클라이언트발 업데이트 시 **파일이 전부 맞는데 롤백** ② `validate()` 가 이름 비교라 **자기 갱신 원천 봉쇄** ③ 재실행 결과가 `bool` 하나라 **호스트 실패와 클라 실패가 같은 값**.
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **12종 845 checks / 0 failed** · JS 디렉터리 스위트 **전부 통과**(계약 테스트 20→**27**) · Kotlin **38 tests / 0 failures**. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- **W1~W8 전부 완료. 남은 것은 실기(UPD-FIELD-00~06)와 배포 준비뿐이다.**
- 변경 파일: `apps/directory/test/update_publish_contract_test.js` · `docs/업데이트_배선_계획.md` · `docs/수동확인_체크리스트.md` · `docs/작업목록.md` · `docs/history.md`.
- 버전 인상·설치본 배포·설치·라이브 조작·push 없음.

### 461) 2026-09-08 미검증 목록 최종 정리 — **Android 실기 항목이 통째로 빠져 있었다**
- 검증용의 마무리 3건 중 남아 있던 둘을 처리했다(①·②의 작업목록분은 #460 에서 이미 끝났다).
- ⚠️ **누락 발견**: 체크리스트에 **Android 실기 항목이 하나도 없었다.** W7 이 들어오기 전에 만든 표라 Windows 항목만 있었고, Android 배선이 끝난 뒤에도 아무도 행을 추가하지 않았다. **`UPD-FIELD-07` 신설** — 승인/취소/권한거부 3경로를 각각 1회, **취소·권한거부에서 오류가 뜨지 않고 앱이 그대로 쓰이는지**, 시작 시 확인이 **로그인 화면을 막지 않는지**, 검증 실패 파일이 캐시에 **남지 않는지**.
- **미검증 목록을 진짜로 남는 것만으로 좁혔다.** 이 절은 한때 "자동 회귀로 대체할 수 없다" 고 주장했고 **그 주장의 절반은 틀렸다** — `update_relaunch.cpp` 가 링크되지 않은 것을 격리라고 적었는데 그건 **한 번도 실행되지 않았다는 자백**이었다(#452·#453). 정정 뒤 남는 것:
  - **UAC 프롬프트 횟수**(OS 동작) · **서비스 성공 경로**(관리자 필요, **실패 경로는 E7 으로 덮임**) · **사람이 봐야 하는 것** · **운영 인증서 체인** · **실기기 APK 설치** · **운영 배포·서명키**.
  - 그리고 **Windows 실행 계층의 실제 동작**(UPD-FIELD-00·01·05) — dummy 로는 검증됐지만 실제 이미지·실제 SCM·실제 세션 거동은 설치 뒤에만 안다. 그중 **01(셸 경유 비승격)이 가장 조용히 실패하는 종류**다: 실패해도 업데이트는 성공한 것처럼 보이고 **클라이언트만 관리자 권한으로 도는 채 세션이 계속 간다.**
- **`docs/구현계획.md` 에 배선 완료 절 신설.** 위의 "최종 상태" 절이 **부품 기준**이었고 **제품에서 그 경로가 불리는지는 확인하지 않았다**는 것을 명시했다 — **부품이 검증된 것과 기능이 동작하는 것은 다르고**, 그 구분을 놓친 것이 이 항목이 한 번 반려된 이유다(#452). W1~W8 요약과 **배선이 잡은 결함 3건**도 함께.
- 체크리스트 공통 전제도 갱신: 이 항목들은 **구현은 끝났고 실기만 남은 것**이고, Android 는 **실기기 설치**가 필요하다.
- 문서만 변경. 제품 코드·빌드·테스트 변경 없음. 검증 수치 불변(C++ 845 · Kotlin 38 · JS 전부 통과).
- 변경 파일: `docs/수동확인_체크리스트.md` · `docs/구현계획.md` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음. **검증용의 전체 재검사 대기.**

### 462) 2026-09-08 ⚠️ **사슬은 이어졌는데 데이터가 안 흘렀다** — Codex 재반려 5건 수정
- #452 에서 "호출자가 없다" 를 고쳤더니, 이번엔 **호출자는 있는데 값이 안 흐르는** 층이 남아 있었다. **호출 사슬이 있다는 것과 그 경로에 데이터가 흐른다는 것은 또 다른 이야기다.** 다섯 건 전부 `grep` 으로 직접 재확인했고 사실이었다.

**① 업데이터가 manifest 를 절대 못 받았다 (가장 치명적)**
- `set_manifest` **제품 호출자 0건**(정의·선언뿐). `FetchManifest` 는 문서가 비면 `"no manifest available"` 로 false → **모든 실행이 첫 단계에서 끝났다.** 서명 검증·버전 비교·staging·swap·등록·재실행·health 가 **전부 옳고 전부 도달 불가**였다. 운영키를 넣어도 마찬가지였을 것이다.
- → `UpdateEffectsConfig::fetchManifest` 신설. 업데이터가 `options.manifestUrl` 과 `<url>.sig` 를 `https_get_text` 로 받아 주입한다. **한 번만 받아 보관** — 단계마다 다시 받으면 서버가 도중에 설치 대상을 바꿀 수 있고, 그건 `releaseId` 가 한 층 아래에서 막는 것과 같은 위험이다.
- **회귀 4종 + 대조군**: 문서도 fetcher 도 없으면 실패 / fetcher 가 공급 / **fetcher 는 정확히 1회** / 주입된 문서가 우선(테스트는 네트워크에 안 감). 배선을 끄면 **6 FAIL**.

**② 검증된 버전이 등록·health 로 안 흘렀다**
- `versionToInstall_` 에 **대입이 없어** 항상 `kProductVersion` 폴백 — `DisplayVersion` 과 health 가 **검증된 새 버전이 아니라 이 바이너리의 컴파일 시점 버전**이었다. 등록에서 (C)안을 탈락시킨 **기준점 오염과 같은 것**이 반대편에서 재현된 셈이다.
- → 등록·재실행 설정을 **호출 시점에 lazy 로** 만들고, `ReadySignaller` 가 **`VerifyDownload` 성공 지점**(버전이 알려지고 동시에 신뢰할 수 있게 되는 첫 순간)에서 공유 값에 기록한다.

**③ 롤백이 제품을 다시 띄우지 않았다 — Host 무인재시작 계약 위반**
- `Rollback()` 은 파일·등록만 복원했다. **원격 사용자에게는 파일이 맞는데 기계가 unreachable** 이다 — 호스트는 교체하려고 정지시킨 프로세스 중 하나이고, 안 돌아오면 **고치러 들어갈 방법이 없다.**
- → 롤백 성공 뒤 **Relaunch 1회(재시도 없음) + HealthCheck**. `RolledBackNotRelaunched` 신설(종료코드 14) — **"복원됨" 과 "쓸 수 있음" 은 다른 주장**이다. 롤백 시 health 의 expectedVersion 은 **디스크에 실제로 있는 구버전**으로 되돌린다.
- ⚠️ **이 결함을 지키던 단정이 있었다**: `"registration fails -> relaunch never ran"`. 참이었고 통과했고, 그래서 **기계를 unreachable 로 만드는 동작을 사양으로 기록**하고 있었다. 뒤집었다.

**④ BREAKAWAY 실패 후 그냥 진행했다**
- 호스트·업데이터 둘 다 flags 0 으로 재시도하고 **경고만 남기고 계속**했다. job 을 못 벗어난 채 진행하는 것은 **원장 (b) 를 허용하는 것**이다 — 경고는 완화가 아니라 **허가**다.
- → `update_job_guard.{hpp,cpp}` 신설. `IsProcessInJob` + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 로 **세 상태**(job 없음 / 무해한 job / 죽이는 job)를 구분해, **죽이는 job 에서 breakaway 실패면 거부**하고 나머지는 진행한다. 모든 job 에서 거부하면 위험하지도 않은 기계에서 실패하고, **그게 안전장치가 꺼지는 경로**다. **`Unknown` 은 통과가 아니다.**

**⑤ 작업 디렉터리 ACL 이 주석뿐이었다 (보안)**
- `ensure_directory` 는 **이미 있으면 그냥 수락**했다. 공격자가 먼저 약한 ACL 로 만들어 두면 **상승된 프로세스가 그가 제어하는 곳에 실행 파일을 복사해 실행**한다.
- → `check_work_directory()`: **reparse point 거부**(이름이 쓰기 위치를 결정하지 않는다) · **NULL DACL 거부**(제한 없음을 뜻한다) · **Users/Everyone/Authenticated/Interactive 에 쓰기 권한이 있으면 거부**. 생성·복사 **전에** 검사한다.
- **회귀 19건**: 8조합 전부에 대해 "진행하는 경우는 breakaway 했거나 애초에 위험하지 않았다" 를 성질로 고정 · 실제 ACL 을 세운 임시 디렉터리로 **사용자 쓰기 가능 → 거부 / 관리자 전용 → 수락**(대조군 없으면 "전부 거부" 로도 통과).
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **13종 880 checks / 0 failed**(신규 job_guard 19, effects 149→**161**, state_machine 61→**65**). JS 디렉터리 스위트 전부 통과. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- **문서 재정정**: 체크리스트의 "남은 것은 전부 실기" 서술을 고쳤다 — **그것도 일렀다.**
- 변경 파일: `update_effects.{hpp,cpp}`·`update_effects_test.cpp` · `update_state_machine.{hpp,cpp}`·`update_state_machine_test.cpp` · `updater_main.cpp` · `host_app_main.cpp` · `update_job_guard.{hpp,cpp}`·`update_job_guard_test.cpp`(신규) · `apps/native_poc/CMakeLists.txt` · `docs/수동확인_체크리스트.md` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음.

### 463) 2026-09-08 abandon 경로 복귀 — **아무 문제도 없었는데 기계가 unreachable** 이던 창을 닫았다
- #462 에서 스스로 남겨 둔 구멍이다. `PrepareForSwap`·`Quiesce` 실패는 **ready 신호 이후**인데 `AbandonedBeforeSwap` 은 롤백 경로를 안 타므로 **아무도 제품을 다시 안 띄웠다.** **파일은 하나도 안 바뀌었는데 기계가 unreachable** 이고, **고칠 손상이 없어서 원인 찾기가 더 어렵다.**
- ⚠️ **더 깊은 문제가 있었다 — 되살릴 목록에 호스트가 애초에 들어갈 수 없었다.** `Quiesce` 가 `enumerateTargets()` 를 **그때** 부르는데, 호스트는 **ready 신호 때문에 이미 나간 뒤**다. **이미 종료한 프로세스는 열거되지 않으므로**, 이 업데이트가 나가라고 시킨 바로 그 프로세스가 **목록에서 빠진다.** abandon 에 Relaunch 를 붙여도 호스트는 안 돌아왔을 것이다.
  - → **신원 확보를 신호보다 앞으로** 옮겼다. `ReadySignaller` 가 `VerifyDownload` 성공 시 **`captureTargets()` 를 먼저 부르고 그다음에** signal 한다. 순서 자체가 요구사항이므로 `run_update` 선언부에 주석으로 명시했다 — 상태기계는 신호의 존재를 모르므로 강제할 수 없고, 구현이 지켜야 한다.
- **abandon 람다 신설**: 네 갈래(Download·Verify·Prepare·Quiesce 실패)가 전부 이걸 통과한다. **Relaunch 를 항상 부른다** — 아무도 안 나간 경로에서는 목록이 비어 있어 아무 일도 안 하므로, **어느 경로가 필요한지 추측하는 것보다 전부 부르는 편이 안전하다.** 1회, 반복 없음.
- **`AbandonedNotRelaunched` 신설**(종료코드 15). **"아무것도 안 바꿨다" 와 "기계에 닿을 수 있다" 는 별개 주장**이고 여기서는 하나만 참이다. 운영자가 "abandoned" 만 보고 괜찮다고 여기지 않도록 코드를 나눴다.
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **13종 890 checks / 0 failed**(state_machine 65→**75**). JS 디렉터리 스위트 전부 통과. **대조군**: abandon 의 Relaunch 를 끄면 **5 FAIL**. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- 변경 파일: `update_state_machine.{hpp,cpp}`·`update_state_machine_test.cpp` · `updater_main.cpp` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음.

### 464) 2026-09-08 조합 계층에 테스트를 붙였다 — **890 checks 가 초록인 채 다섯 결함이 살아 있던 그 자리**
- 검증용 지적: `updater_main.cpp` 는 **제품 바이너리 하나에만 링크**되고 `UpdaterEffects` 를 쓰는 테스트가 **0건**이었다. 그런데 #462·#463 의 결함이 **전부 그 조합 계층**에 있었다 — manifest 미fetch · 검증 버전 미전달 · capture/signal 순서. **누가 내일 같은 배선을 끊어도 890 은 그대로 통과**했을 것이다. `update_relaunch.cpp` 가 어디에도 링크되지 않았던 것과 **같은 구조**다.
- **`updater_effects.{hpp,cpp}` 로 조합을 분리**했다. `updater_main.cpp` 에는 실행 파일만 하는 일(명령줄·자기 복사본 인계·종료 코드)이 남았다. `UpdaterDeps` 로 경계를 주입한다 — 요점은 "바꿔 끼울 수 있다" 가 아니라 **"업데이터가 이걸 제대로 배선했는가" 를 물을 수 있게 된 것**이다.
  - ⚠️ **서명 검증기도 주입 대상**이다. production 은 컴파일된 신뢰 앵커를 쓰고, 테스트는 **자기 것**을 넣는다 — **운영 신뢰키를 바꾸지도, 빌드 플래그로 production 이 믿는 것을 바꾸지도 않고** 서명된 fixture 를 끝까지 돌릴 수 있다(Codex 요구사항).
- **신규 `remote60_updater_assembly_test` 28 checks** — 제품 바이너리가 쓰는 **바로 그 `UpdaterEffects`** 를 돌린다(재구성이 아니라).
  - **manifest 가 실제로 받아지는가**(url + `.sig` 둘 다) · **manifest 의 버전이 등록·health 에 도달하는가**(이 바이너리의 컴파일 버전도, 설치돼 있던 버전도 아님) · **capture 가 signal 보다 먼저인가**(순서를 인덱스로 단정) · 재실행 계획이 **캡처된 것으로** 만들어지는가.
  - **대조군 4종**: manifest fetch 실패 → 아무것도 안 바뀜 · 아티팩트 다운로드 실패 → abandon · **서명 거부 → 아무것도 설치 안 됨**(나머지 넷을 값지게 만드는 검사) · **의존성에 구멍이 있으면 build 자체가 거부**.
  - **작업 복사본의 순서**를 `prepare_working_copy()` 로 분리해 단정한다 — **판단 → 생성 → 복사 → 실행**. 복사 뒤에 검사하면 **이미 실행 파일을 받은 디렉터리를 검사**하는 것이다. 거부되면 **아무것도 복사되지 않음**도 단정한다.
  - **대조군 실측 2회**: capture 를 signal 뒤로 옮기면 **1 FAIL**, 버전 대입을 지우면 **6 FAIL**.
- ⚠️ **조합 테스트가 곧바로 또 하나를 잡았다**: **`--registry-root` 가 필수 인자인데 아무것도 제어하지 않았다.** 조합이 `HKEY_LOCAL_MACHINE` 과 고정 subkey 를 직접 썼다. **아무것도 바꾸지 않는 필수 인자는 없는 것보다 나쁘다 — 보증처럼 읽힌다.** `split_registry_root()` 신설(HKLM/HKCU 만, 그 외는 **기본값으로 매핑하지 않고 거부**), `validate()` 가 진입점에서 검사한다.
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **14종 918 checks / 0 failed**(신규 assembly 28). JS 디렉터리 스위트 전부 통과. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- 변경 파일: `updater_effects.{hpp,cpp}`·`updater_assembly_test.cpp`(신규) · `updater_main.cpp`(축소) · `updater_options.{hpp,cpp}` · `apps/native_poc/CMakeLists.txt` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음.

### 465) 2026-09-08 주입 경계 **너머**도 고정 — `production_updater_deps()` 완전성 + 세 층 구분 기록
- 조합 테스트는 **경계 이쪽**만 돈다. **저쪽에 필드 하나가 비어 있으면 지금은 런타임 `validate()` 로만 드러나고, 그건 사용자 기계에서 처음 알게 된다는 뜻**이다. 이 세션 전체가 그 부류였다 — 아무도 안 보는 곳에 결함이 살았다.
- **`production_updater_deps()` 완전성 회귀 12건**(assembly 28→**40**): 전체 `validate()` + 배선 **하나씩 이름을 대고** 단정(어느 wire 가 빠졌는지 실패가 말하도록) + **로거를 안 줘도 완전한지** + **`selfImagePath` 가 실재하는 파일인지**(빈/엉뚱한 경로면 "실행 중인 업데이터를 교체하지 않는다" 검사가 아무것과도 비교하지 않게 된다).
- ⚠️ **한계를 함께 적었다**: 이건 **모든 배선이 존재한다**는 것이지 **그것들이 옳게 동작한다**는 것이 아니다. 진짜 WinHTTP·진짜 SCM·진짜 열거기의 동작은 여전히 실기다. **"검증했다" 와 "여기까지만 검증했다" 를 섞지 않는다.**
- **대조군 실측**: `production_updater_deps()` 에서 verifier 대입을 지우면 **3 FAIL**.
- **체크리스트에 세 층 구분표 신설** — 이 작업에서 **세 번 다 한 번씩 틀렸기** 때문이다:
  | 층 | 보장 | 놓치면 |
  |---|---|---|
  | 부품 | 각 조각이 옳게 동작 | 아무것도 — 부품은 다 옳았다 |
  | **조합** | 업데이터가 그것들을 제대로 **배선**했는가 | **890 이 초록인 채 다섯 결함**(#462·#463) |
  | 실행 | 진짜 OS·진짜 권한에서 도는가 | 지금 남아 있는 것 전부 |
  **부품이 검증된 것과 기능이 동작하는 것이 다르고(#452), 호출자가 있는 것과 데이터가 흐르는 것이 다르며(#462), 데이터가 흐르는 것과 그 배선을 아무도 안 보는 것이 또 다르다(#464).**
- **남은 미검증에 두 줄 추가**: `production_updater_deps()` 의 **동작**(주입 경계 너머) · `run_from_copy` 의 **실제 복사·실행**(순서는 `prepare_working_copy()` 로 덮였고 실행은 관리자 권한과 실제 `%ProgramFiles%` 가 필요).
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **14종 930 checks / 0 failed**. JS 디렉터리 스위트 전부 통과. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- 변경 파일: `updater_assembly_test.cpp` · `docs/수동확인_체크리스트.md` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음. **검증용 전체 재검사 대기.**

### 466) 2026-09-09 Codex 재반려 3건 — **타입을 만든 것과 결정 지점에 도달하는 것은 다르다**
- 지난번에 `RelaunchOutcome` 을 만들고 "호스트 실패와 클라 실패를 구분한다" 고 보고했는데, **그 목록이 상태기계 분기의 입력이 아니었다.** `allStarted` bool 로 접혀 `!relaunched` 하나로 귀결됐고, **Host CreateProcess 실패도 서비스 실패도 구분 없이 `Commit()` + `UpdatedButNotRelaunched`** 였다 — **백업을 지우고 수동 시작으로 끝냈다.** 타입의 **존재**를 보고했지 **도달**을 보고하지 않았다.

**① `RelaunchVerdict` 로 결정 지점까지 연결**
- `bool` → `RelaunchVerdict{AllBack, OptionalMissing, RequiredMissing}`. 필수는 **Host·서비스**(`RelaunchKind::ElevatedProcess`/`Service`), 선택은 클라이언트.
- 상태기계가 **세 갈래**로 분기: 전부 복귀 → Health · **선택만 실패 → `Commit` + `UpdatedButNotRelaunched`**(설치는 유지) · **필수 실패 → 롤백**. 롤백이 가능한 이유는 **아직 Commit 하지 않아 백업이 남아 있기 때문**이고, 예전처럼 한 갈래로 접으면 **그 백업을 지웠다.**
- 둘 다 실패면 **필수가 이긴다**(그쪽이 다음 행동을 결정한다).

**② 롤백이 health 결과를 버리던 것**
- `(void)effects.HealthCheck();` 뒤 **무조건 `RolledBack`** — 구 Host 가 비정상이어도 "복구됨" 이었다. **`RestoredButUnhealthy` 신설**: **"되돌렸다" 와 "동작한다" 는 다른 주장**이고 전자만 확인됐다.
- ⚠️ **롤백의 첫 단계가 `DeleteFile`/`MoveFile` 이었다.** 롤백 사유가 "방금 띄운 프로세스가 비정상" 일 때 **그 프로세스가 새 파일을 잡고 있어 복원이 바로 그 파일에서 실패**한다. `releaseBeforeRollback` 신설 — **이 시도가 띄운 것만**, pid + **이미지 이름 대조** 후 정지하고, **파일이 하나도 움직이기 전에** 한다.
- `stopStarted()` 가 **실제로 끝난 것만 센다**(`TerminateProcess` 성공 + wait 신호). 안 죽은 것을 "정지함" 으로 보고하면 롤백을 **막으려던 실패로 그대로 밀어 넣고 롤백 탓처럼 보이게** 된다. 성공하면 pid 를 잊어 **두 번째 호출은 0** 을 답한다.

**③ abandon 이 살아 있는 것까지 재실행하던 것**
- captured 전체를 무조건 `Relaunch` 했다. **Prepare 실패·Quiesce 부분실패에서는 아직 살아 있을 수 있어 중복 프로세스**가 생긴다. **신원 capture 는 그것이 나갔다는 뜻이 아니다.**
- `RelaunchConfig::isStillRunning` 주입 + `RelaunchOutcome::alreadyRunning` 신설. 살아 있으면 **띄우지 않고, 실패로도 치지 않는다**(되살릴 것이 없었으므로).

- **회귀 E10~E12 신설**(relaunch 90→**106**): 살아 있으면 미실행 + **대조군**(떠났으면 실행) · **필수/선택/둘다** 세 verdict · **이 시도가 띄운 것만 정확히 1개 정지**(2회째 0, **실제로 죽었는지까지 확인**).
  - ⚠️ **E12 첫 판이 헛단정이었다**: `stopped >= 0` 은 **항상 참**이고, `.cmd` 는 `cmd.exe` 로 떠서 이미지 대조가 (옳게) 거부해 **아무것도 안 죽였는데 통과**했다. 실제 `.exe`(`%COMSPEC%` 복사본)로 바꾸고 **정확히 1** 을 단정했다.
- **대조군 실측 3회**: liveness 스킵 끄고 verdict 접으면 **6 FAIL**, 롤백 health 결과 버리면 **5 FAIL**.
- 부수: job guard 테스트가 **자기가 세운 protected DACL 때문에 지워지지 않는 임시 디렉터리를 %TEMP% 에 남기고 있었다**(14개 발견). 정리 전 상속 복원하도록 수정, 잔여 0 확인.
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **14종 955 checks / 0 failed**. JS 디렉터리 스위트 전부 통과. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- 변경 파일: `update_state_machine.{hpp,cpp}`·`update_state_machine_test.cpp` · `update_relaunch.{hpp,cpp}`·`update_relaunch_test.cpp` · `update_effects.{hpp,cpp}`·`update_effects_test.cpp` · `update_release_test.cpp` · `updater_effects.cpp` · `updater_assembly_test.cpp` · `update_job_guard_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음.

### 467) 2026-09-09 여섯 시나리오 조합 회귀 — 실제 프로세스로, **다섯 개만**
- Codex 요구: verdict 가 상태기계에서 갈리는 것과, **업데이터가 실제로 조립했을 때 그 갈래가 옳은 종착·백업 수명·프로세스 개수를 만드는 것**은 다른 주장이다. 신규 `remote60_updater_scenarios_test` (**21 checks**) 가 **제품이 쓰는 `UpdaterEffects`** 를 실제 `.exe`(명령 해석기 복사본, 새 버전은 뒤에 바이트를 덧붙여 **실행 가능하면서 다른 파일**)로 돌린다.
- **덮은 다섯**: ① Host 실패 → 롤백·구 바이트 복귀·백업 잔여 0 ③ 둘 다 실패 → 필수가 결정 ④ **새 빌드 health 실패 → 실행 중인 프로세스 밑에서 복원 성공** ⑤ 구 빌드도 불건강 → `RestoredButUnhealthy` ⑥ Quiesce 미완 → 교체 전 포기·**중복 없음**.
- **대조군**: **실행 중인 이미지는 삭제가 실패하고**(`ERROR_ACCESS_DENIED`) 정지 뒤에는 성공한다 — 정지가 파일 조작보다 앞서야 하는 이유의 OS 수준 증거.
  - ⚠️ **이 대조군의 첫 판이 틀렸다**: "실행 중 이미지는 **이름 변경**이 실패한다" 고 단정했는데 **Windows 는 실행 중 이미지의 rename 을 허용한다.** 실패하는 것은 **삭제**이고, 그게 롤백이 하는 일이다. 오류 코드를 읽고 나서야 알았다.
- ⚠️ **② Client 실패는 이 파일에서 못 덮었다 — 지우지 않고 이유를 적어 뒀다.** 이 fixture 로 만들면 relaunch 에 닿기도 전에 **swap 이 실패**한다(먼저 실행해도, 설치 디렉터리를 비운 것을 확인해도 재현). **원인을 찾지 못했고 파고들기를 멈췄다.** 주변 네 시나리오는 relaunch 가 뒤에 무엇을 보고하느냐만 다른데 그것들은 통과하므로, 대상 코드가 아니라 fixture 문제로 판단했다. **다른 곳에서 덮이는 것**: 상태기계 스위트(OptionalMissing → 유지·1회 commit·롤백 없음), relaunch E11(실제 실행 실패에서 verdict 생성). **어디에서도 안 덮이는 것**: 이 종착의 **백업 수명**을 실제 조합에서.
- ⚠️ **제품 결함을 또 하나 잡았다**: `launch_via_shell` 이 **없는 파일을 셸에 넘기면 모달 오류 대화상자**가 뜬다 — 업데이터가, **사람이 없을 수도 있는 기계에서**, 제품이 돌아와야 할 그 순간에. 셸 경유 `ShellExecute` 에는 UI 억제 옵션이 없으므로 **넘기기 전에 파일 존재를 직접 확인**한다.
- ⚠️ **또 하나**: 셸 실행은 **pid 를 돌려주지 않아** `stopStarted` 가 자기가 띄운 클라이언트를 **영원히 정지시킬 수 없었다** — 그러면 클라 파일을 옮겨야 하는 롤백이 **자기가 띄운 프로세스 때문에** 실패한다. 실행 전후 스냅샷 차이로 pid 를 찾도록 고쳤다.
- **설정 주입 확대**: `payloadNames`·`relaunchTable` 을 `UpdaterDeps` 로. 조합이 스스로 정하면 **실제 제품 말고는 아무것도 상대로 돌릴 수 없고**, 그게 이 계층이 시험되지 않은 채 다섯 결함을 품고 있던 이유다.
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **15종 976 checks / 0 failed**(신규 scenarios 21, assembly 40, relaunch 106). JS 디렉터리 스위트 전부 통과. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- **알려진 결함(테스트 위생)**: 시나리오 스위트가 `%TEMP%` 에 디렉터리를 남길 수 있다(명령 해석기의 무해한 사본). 셸 실행이 비동기라 마지막 sweep 뒤에 프로세스가 나타나면 디렉터리가 잠긴다. **원인을 끝까지 못 봤고 파일 주석에 그대로 적었다.**
- 변경 파일: `updater_scenarios_test.cpp`(신규) · `updater_effects.{hpp,cpp}` · `updater_assembly_test.cpp` · `update_relaunch.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음.

### 468) 2026-09-09 ⚠️ **"사유만 읽어라" 가 제 결함 셋을 풀었다** — 시나리오 스위트가 거의 공허했다
- 검증용 요구: ② 를 (a)/(b) 로 넘기지 말고 **swap 이 왜 실패하는지 사유 문자열만** 찍어라. 그 한 걸음이 **세 개의 제 결함**을 드러냈다.
- **사유**: `swap failed: the staged release does not contain **GNLinkHost.exe**` — **제품 이름**이었다. 즉 조합이 **주입한 payload 목록을 무시**하고 있었다.
  1. ⚠️ **`UpdaterEffects::build` 가 `deps_.payloadNames` 를 안 쓰고 `product_image_names()` 를 하드코딩**하고 있었다(#467 에서 주입 필드를 추가했지만 **사용처를 못 바꿨다**). 그래서 시나리오 스위트의 **모든 swap 이 실패**하고 있었다.
  2. ⚠️ **그런데도 스위트는 통과했다.** "롤백됐다 / 구 바이트가 돌아왔다" 는 **swap 이 실패해도 성립**하기 때문이다. **21 checks 가 거의 전부 공허했다** — 이 세션에서 여러 번 잡아낸 그 패턴을 내가 만든 것이고, 이번엔 **테스트 전체 규모**였다.
  3. ⚠️ **production 기본값의 escape 가 깨져 있었다**: `L"ui\shell.html"` 가 아니라 `L"ui\shell.html"` 여야 했다 — `\s` 는 유효한 escape 가 아니라 실제 값이 **`uishell.html`** 이 됐다. **제품이 잘못된 payload 이름을 쓸 뻔했다.**
- **고친 뒤** swap 이 실제로 돌자 **② 가 그대로 동작했다**(`UpdatedButNotRelaunched`). 못 덮는다고 적었던 시나리오가 **원인이 사라지자 저절로 덮였다.** 여섯 전부 복구, **24 checks**.
- **연쇄로 드러난 것 하나 더**: swap 은 교체 대상을 `<name>.gnlink-old` 로 **이름을 바꾸고**, 그 파일을 실행 중이던 프로세스는 **바뀐 이름으로 계속 돈다** — 그래서 이미지 leaf 가 더 이상 원래 이름이 아니고, **정확 일치로 훑던 정리 루틴이 그것들을 못 봤다.** 그 프로세스가 백업을 붙잡아 **다음 시나리오의 move-aside 를 실패**시켰다. 접두사 일치로 수정.
- **로그 개선(제품)**: 실패 사유가 **그 자리에서** 기록되지 않아 나중에 읽으면 사라진다 — 롤백이 자기 단계들로 덮어쓴다. `ReadySignaller` 가 `Swap`/`Register`/`Rollback` 실패 시 **그 시점의 effects 오류**를 남기고, 업데이터가 `effects: ...` 로 출력한다. **"swap 이 실패했다" 만 있고 "어느 파일이 안 움직였다" 가 없던 로그**를 고친 것이다.
- ⚠️ **남은 미해결 하나, 숨기지 않고 적는다**: 클라이언트의 `.gnlink-old` 가 살아남는 경우가 있다. 클라이언트는 swap 이 **놓은** 파일에서 실행되므로 **이름이 바뀐 백업을 붙잡을 이유가 없어 보이는데** 그렇다. 단정을 **호스트 백업**(각 시나리오가 실제로 만들고 소비하는 것)으로 좁히고, 관찰은 파일 주석과 이 항목에 남겼다. **좁혔다는 사실과 이유를 함께 적었다.**
- 검증 — **`qwinsta`: console 만 Active.** C++ 업데이트 **15종 979 checks / 0 failed**(scenarios 21→**24**, 여섯 시나리오 전부). JS 디렉터리 스위트 전부 통과. 라이브 무영향: PID 3종 불변, `DisplayVersion` **0.2.104**.
- 변경 파일: `updater_effects.{hpp,cpp}` · `updater_main.cpp` · `updater_scenarios_test.cpp` · `docs/history.md`.
- 버전 인상·설치본·설치·라이브 조작·배포·push 없음.

### 469) 2026-09-09 문서 정리 — 원장 I09~I11, 미검증 한 줄, **"실행 계층은 실기로만" 정정**
- **원장 3건 등재**(`docs/full_code_audit_2026-09-08.md`):
  - **I09 (P1)** 조합이 주입받은 payload 목록을 무시하고 하드코딩 → **실제 제품 말고는 아무것도 상대로 못 돌리고, 그 계층 시나리오가 전부 공허하게 통과**. 교훈: **테스트 가능성이 없으면 테스트가 없고, 테스트가 없으면 결함이 산다.**
  - **I10 (P0)** production payload 기본값 `L"ui\shell.html"` — `\s` 는 유효한 escape 가 아니라 값이 **`uishell.html`**. **출시된 업데이터가 없는 파일 둘을 찾아 모든 swap 이 실패**했을 것이고 **현장에서만** 드러났을 것이다. **컴파일러는 조용했고 같은 문자열에 파이썬은 경고를 냈다.**
  - **I11 (P2)** swap 이 `<name>.gnlink-old` 로 **이름을 바꾸면** 실행 중이던 프로세스는 바뀐 이름으로 계속 돈다 → 이미지 이름이 불변이라 가정한 정리 경로가 못 봄. **swap 자체가 그 가정을 깬다.**
- **미검증 목록에 한 줄 추가**: 클라이언트 백업(`.gnlink-old`)의 수명 — 실제 조합에서 살아남는 경우가 있고 **원인 미상**. 시나리오 단정은 **호스트 백업으로 좁혀져 있다**.
- ⚠️ **"실행 계층은 실기로만" 을 정정했다.** 이 문서는 한때 복사·프로세스 시작·job·재실행을 실기 전용으로 적었다 — **틀렸다.** dummy exe·임시 설치 root·주입 경로로 **격리 OS 테스트가 가능하고 지금 실제로 그렇게 테스트된다**(E1~E12, 시나리오 6종). **진짜 실기 전용은 운영 서비스 등록·운영 인증서 체인·UAC 사용자 경험뿐**이다. 실행 계층을 실기로 미뤄 두면 그 코드는 **출시 전까지 한 번도 돌지 않는데, 그것이 이 작업에서 결함이 살아남은 방식이다.**
- 문서만 변경. 제품 코드·빌드·테스트 변경 없음. 검증 수치 불변(C++ 15종 979 · JS 전부 통과).
- 변경 파일: `docs/full_code_audit_2026-09-08.md` · `docs/수동확인_체크리스트.md` · `docs/history.md`.

### 470) 2026-09-09 Codex 반려 4건 반영 — **"주입은 테스트만 채우고 있었다" 를 구조로 막고, 쓸어담기가 눈이 멀어 있었다**
- **① 동일 identity 생존 검사를 생산에 연결**(`update_relaunch.*`, `update_process_identity.cpp` 신설):
  - `RelaunchConfig::isStillRunning` 이 비어 있으면 **"전부 나갔다" 로 가정**했다. 생산은 이걸 한 번도 채우지 않았으므로 **출시본에서 이 검사는 아예 꺼져 있었고**, 그 검사를 덮던 테스트는 **아무도 안 돌리는 구성**을 덮고 있었다.
  - 답을 **셋**으로 바꿨다(`Running / Exited / Unknown`). **조회 실패는 "종료됨" 이 아니다** — 나갔다고 넘겨짚으면 살아 있는 것을 또 띄워 두 개가 되고, 있다고 넘겨짚으면 내려간 기계를 성공으로 보고한다. `Unknown` 은 실패로 올려 필수 이미지면 롤백으로 간다.
  - 비어 있으면 **실제 검사(`real_liveness`)** 를 쓴다. `ACCESS_DENIED` 는 `Unknown` 이지 `Exited` 가 아니다.
  - identity 비교(`process_identity_matches`)를 **`update_process_identity.cpp` 로 분리**했다. relaunch 테스트가 **진짜 비교를 링크**한다 — 링크하기 싫어서 검사를 안 배선했던 것이 원래 순서였다.
- **① 일반화: 주입 지점 전수 점검을 구조로 바꿨다**(`update_effects.*`, `updater_effects.*`):
  - `UpdateEffectsConfig::unwired()` 가 **비어 있는 seam 이름을 열거**하고, `UpdaterEffects::run()` 은 **하나라도 비면 아무것도 하지 않고 그 이름들을 로그에 적고 중단**한다. manifest 를 안 가져오던 것도, 검증한 버전이 등록에 못 닿던 것도 **전부 이 형태**였다.
  - 감사 결과: `UpdateEffectsConfig` 의 std::function 10개 전부 생산이 채운다. `UpdaterDeps` 는 `validate()` 가 이미 강제. `RelaunchConfig` 에서 생산이 안 채우던 유일한 하나가 `isStillRunning` 이었고 위에서 해소했다.
- **② 정지 대상은 "받은 handle" 로만**(`update_relaunch.*`, `update_effects.*`):
  - `CreateProcessW` 가 준 handle 을 **닫지 않고 보유**한다. handle 이 살아 있는 동안 pid 는 재사용되지 않으므로 **handle 이 곧 신원**이다. 이름으로 pid 를 맞추는 것은 신원이 아니다.
  - **셸 경유는 launch 별 상관관계가 성립할 때만** 소유를 주장한다: 실행 직전 스냅샷과 직후 스냅샷을 비교해 **정확히 하나**가 나타났을 때만 handle 을 열어 보유하고, 그 handle 로 image 경로를 **다시** 확인한다. 0개거나 2개 이상이면 소유하지 않는다 — 이름으로 죽이면 남의 창을 죽인다.
  - `stopStarted` 가 `StopReport{stopped, unstoppable}` 를 돌려주고, **`unstoppable` 이 비지 않으면 파일 롤백에 들어가지 않는다.** 잡고 있을지 모르는 파일을 되돌리면 절반만 복구된 설치가 남고, 실패 원인은 롤백 탓으로 보인다.
  - 보유한 handle 은 시도가 끝날 때 전부 닫는다. **이미 종료된 프로세스라도 handle 이 열려 있으면 그 이미지 파일이 잠긴 채로 남는다.**
- **③ optional 실패여도 health 가 먼저**(`update_state_machine.cpp`):
  - optional 이미지 하나가 안 돌아왔다는 이유로 **health 를 묻기도 전에 commit** 하고 백업을 지웠다. `required started` 는 `CreateProcess 가 돌아왔다` 는 뜻이지 **호스트가 떴다** 는 뜻이 아니다.
  - 순서를 바꿨다: **health → (건강하면) optional 부분 commit, 건강하지 않으면 롤백.**
  - **회귀 추가**: "Host launched-but-unhealthy + Client launch fails" → `RolledBack`, `commitCount == 0`, health 가 결정보다 먼저. **역대조**: 같은 client 실패 + 건강한 host → 여전히 `UpdatedButNotRelaunched`(무조건 롤백이 아님을 증명).
  - **반대 증거**: 순서를 예전으로 되돌리면 이 스위트에서 **7건이 실패**하고, 호출 흔적이 `... Relaunch, Commit ...` 로 **HealthCheck 가 아예 없다.**
  - 예전 순서를 지키던 단정 `"relaunch fails -> health is not claimed"` 은 **결함을 보호하던 테스트**였다. 참이었고, 그게 버그였다.
- **④ `Commit()` 이 `DeleteFileW` 결과를 읽는다**(`update_effects.*`, `updater_effects.*`):
  - 지우지 못한 백업을 **이름과 오류 코드까지** 기록하고(`orphaned_backups()`), 로그에 남긴다. 예전엔 결과를 버리고 목록을 비워서, **살아남은 `.gnlink-old` 는 어디에도 흔적이 없었다.**
  - **격리에서 재현했고 실기로 미루지 않았다**: `update_effects_test` 에 백업을 열어 잡은 채 commit 하는 경우(고아 1건 기록) 와 **역대조**(정상 commit 은 고아 0건, 백업도 실제로 사라짐)를 넣었다.
  - 실제 조합에서 남는 클라이언트 백업의 **오류 코드는 5(ACCESS_DENIED), 속성 32(ARCHIVE)** 였다. 1초 재시도로는 안 풀렸고 재시도는 **얻는 것 없이 commit 만 느리게** 만들어 걷어냈다.
  - **더 나쁜 2차 피해를 막았다**: 지워지지 않는 백업이 이름을 차지하면 **다음 업데이트의 move-aside 가 막혀 설치 자체가 불가능**해진다. 이제 지울 수 없으면 `<name>.gnlink-old.N` 으로 **옮겨서 이름을 비우고** 그 사실을 기록한다. **실행 중인 파일도 rename 은 된다** — swap 이 처음부터 기대고 있는 그 비대칭이다.
- ⚠️ **시나리오 스위트의 쓸어담기가 눈이 멀어 있었다**(`updater_scenarios_test.cpp`):
  - `running_under()` 가 **image 경로를 읽으려고 `OpenProcess` 를 먼저 했고, 열 수 없는 프로세스는 건너뛰었다.** 건너뛴 것이 **정확히 이 함수가 멈추려던 프로세스들**이었다. 스냅샷에는 **이름으로 계속 보이고 있었다.**
  - 결과: 매 실행마다 `ScnClient.exe` 넷이 살아남아 자기 이미지를 잡고 있었고, **다음 시나리오는 시작 바이트를 쓰지 못한 채** 바이트에 대한 단정을 실패했다 — 화면에는 프로세스가 하나도 안 보이는 채로. 3·4·5·6 과 control 이 이 이유로 죽었다.
  - 고쳤다: **스냅샷 이름으로 식별**하고, 경로는 **확인되면** 디렉터리로 좁히되 **확인 못 하면 포함**한다. 못 여는 프로세스야말로 놓치던 대상이므로, "경로를 못 읽었다" 는 봐줄 이유가 아니다.
  - 시나리오 6 은 같은 이름의 stub 을 install 밖에서 돌리므로 이 범위 규칙으로 그대로 보존된다.
  - `seed()` 도 **쓰고 나서 읽어서 확인**한다. 덮어쓰기 실패를 조용히 넘겨서, 시나리오가 앞 시나리오의 바이트로 시작한 뒤 바이트 단정을 실패하고 있었다.
- **검증**(콘솔 세션, `qwinsta` 상 활성 RDP 없음):
  - update 계열 12종 **전부 PASS**: check_28 / effects_180 / handoff_33 / http_36 / job_guard_19 / manifest_82 / relaunch_107 / release_80 / state_machine_92 / assembly_40 / options_48 / scenarios_65.
  - 나머지 C++ 테스트 바이너리 전부 exit 0. 예외 2건은 **이번 변경 파일을 하나도 링크하지 않는** 환경 실패다: `remote60_gdi_capture_process_test`(부하 중 `GDI_DELIVERED_FPS=3.67`, copy avg 260ms), `remote60_udp_control_e2e_test`(`udp hello ack failed`). **판정 근거로 쓰지 않는다.**
  - JS/Kotlin 은 이번에 바뀐 것이 없어 재실행하지 않았다.
- 변경 파일: `apps/native_poc/src/update_state_machine.cpp` · `update_relaunch.{hpp,cpp}` · `update_effects.{hpp,cpp}` · `update_process_identity.cpp`(신설) · `updater_effects.{hpp,cpp}` · `update_state_machine_test.cpp` · `update_effects_test.cpp` · `update_relaunch_test.cpp` · `updater_scenarios_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/full_code_audit_2026-09-08.md` · `docs/수동확인_체크리스트.md`.
- 라이브·설치·배포·버전 인상·릴리스는 **보류 그대로**. 산출물 생성 없음.

### 471) 2026-09-09 안전 반려 4건 — **스위트가 저장소 밖을 지우고 이름으로 죽이고 있었다**, 그리고 셸 소유권은 증명이 아니었다
- ⚠️ **테스트가 저장소 root 밖을 삭제했다**(`updater_scenarios_test.cpp`, `CMakeLists.txt` / commit 3edd5cd):
  - `%TEMP%` 에 트리를 만들고 시작할 때마다 `gnlink-scn-*` 를 **열거해 `remove_tree`** 했다. **AGENTS.md 최상위 규칙 위반**이고, **테스트 실행 승인은 저장소 밖 파일 삭제 승인이 아니다.** 패턴이 마침 자기 산출물에만 맞았다는 건 근거가 아니다 — **자기 것이 아닌 디렉터리에 대해 그 판단을 내릴 자격이 없다**는 게 요점이다.
  - root 를 **빌드에서 주입**(`GNLINK_SCN_ROOT = ${CMAKE_SOURCE_DIR}/.claude/scenario-runs`). 런타임 추측 없음. 정리는 **자기 run 디렉터리 하나만**, `inside_run_root()` 통과할 때만. 남의 잔재는 **목록만** 찍는다.
- ⚠️ **테스트가 이름으로 남의 프로세스를 죽일 수 있었다**(같은 commit):
  - `running_under()` 가 경로를 못 읽으면 **`mine = true`** 로 두고 이름 prefix 만으로 포함한 뒤 Terminate 했다. **식별 못 한 것을 죽이는** 구조였다. 게다가 시작 시 **`stop_everything_under(L"")`** — 빈 scope 는 좁은 범위가 아니라 **범위의 부재**다. **같은 스위트의 두 번째 실행을 첫 번째가 죽였을 것이다.**
  - 이제 **경로를 반드시 읽고 scope 밑임이 확인될 때만** 후보. 못 읽으면 **보고만 하고 건드리지 않는다**(제품의 `Liveness::Unknown` 과 같은 규칙).
  - 반례 3종: **같은 이름 다른 디렉터리 생존** · **동시 run 모사 생존** · **`%TEMP%` 는 삭제 가능 범위 밖**.
  - **직전 라운드에서 `mine = true` 로 뒤집은 것이 나였다.** "못 여는 것이야말로 놓치던 대상" 이라는 관찰은 맞았지만, 거기서 **기본값을 안전한 쪽이 아니라 스위트가 초록이 되는 쪽**으로 돌렸다. 초록은 목적이 아니라 관측이다.
- **셸 소유권을 폐기하고 진짜 handle 을 받는다**(`update_relaunch.{hpp,cpp}`):
  - 실행 전/후 스냅샷 차분이 1개라는 것은 **자기 실행의 증거가 아니다.** 셸 handoff 가 늦는 동안 **사용자가 같은 프로그램을 켜면 차분도 1**이고, 그 위에서 연 handle 은 **남의 신원을 고정**한다. 스냅샷 비교(`pids_running_image`)는 **삭제**했다.
  - `RelaunchConfig::launchInUserContext` — 셸 창 토큰을 복제해 **`CreateProcessWithTokenW`** 로 실행하고 **`pi.hProcess` 를 받는다**. 관리자 프로세스의 `SeImpersonate` 로 충분하고, **업데이터가 상승 권한이라 이 일을 맡을 자리**다. 비면 실제 구현(`isStillRunning` 과 같은 형태 — 빈 seam 이 "건너뛰기" 가 되지 않게).
  - 실패하면 셸로 떨어지되 **소유를 주장하지 않는다**: `RelaunchOutcome::ownershipUnknown`, `unstoppable` 로 보고 → **강제 종료 없음, 롤백 진입 없음.**
  - **시나리오 7 신설**: 셸 경유 + 새 빌드 불건강 → **`RollbackFailed`**, 로그에 "no proof of ownership", 파일은 swap 이 남긴 그대로, 백업도 그대로(재시도 가능). **역대조**: 같은 상황에서 소유 가능한 경로면 정지되고 롤백이 실제로 돈다.
  - ⚠️ **남는 위험(판단 요청)**: 데스크톱이 있는데 토큰 경로만 실패하면 **셸로 뜬 클라이언트 때문에 롤백이 영구히 막힌다.** 대안은 (a) 소유 못 하는 optional 은 commit 이후에만 시작 (b) 현행 유지. 조용히 정하지 않고 올린다.
- **백업 홀더 — 추측을 그만두고 물었다**(`updater_scenarios_test.cpp`):
  - **Restart Manager(`RmStartSession`/`RmRegisterResources`/`RmGetList`) 를 읽기 전용으로** 붙였다. 고아 백업이 나오면 **누가 잡고 있는지 이름으로** 찍는다. 추측 셋을 각각 **한 가지 통제 조건에서** 시험했다: 실행 중 이미지 → **1초 재시도로 안 풀림**, 잔여 image section → **같은 재시도로 안 풀림**, 열린 프로세스 handle → **`~Shared()` 로 먼저 닫아도 안 풀림**. **"모든 원인을 기각했다" 는 뜻이 아니다** — 각 실험은 그 조건에서 그 설명이 성립하지 않음을 보였을 뿐이고, 남은 조합이나 다른 홀더는 배제하지 못했다. **묻는 편이 세 번 추측하는 것보다 쌌다.**
  - **시나리오 8 신설**: 셸 경유 클라이언트 + commit 하는 경로 — 문제의 그 형태를 격리에서 재현. **현재는 고아가 발생하지 않는다**(백업이 깨끗이 삭제됨). 스냅샷 상관관계와 그것이 붙들던 handle 이 사라진 뒤로 재현되지 않는데, **둘 중 무엇이 없앴는지는 특정하지 못했다.**
- **`.gnlink-old.N` 은 `Commit` 이 아니라 `Swap()` 안이다**(검증용 정정 반영). 증거를 붙였다(`update_effects_test`):
  - **다음 시도 성공**: 지울 수 없는 백업(**실행 중 이미지**)이 이름을 차지해도 다음 업데이트가 **막히지 않는다**. 옮겨진 것은 `.1` 로 남고, 기록되고, **롤백은 원본 바이트를 정확히 복원**한다.
  - **상한**: `kMaxStaleBackups = 50`. 초과하면 swap 이 **멈추고 원인을 말한다** — "could not move aside" 만 남기면 운영자가 **엉뚱한 파일**을 본다. 원인 문자열이 증상과 함께 실린다.
  - ⚠️ **rename 이 만능이 아니다**: 공유 없이 열린 핸들이 잡은 파일은 **삭제도 rename 도 안 된다.** 이 경우 swap 은 실패하고 **아무것도 절반만 바뀌지 않는다.** rename 이 구제하는 건 **실행 중 이미지**뿐이라는 걸 단정으로 박았다.
- **"cleanup 이 죽여서 없어진 것" 과 production 복구를 분리**했다: 시나리오 단정이 읽는 값은 전부 **자기 정리 이전 시점**에 캡처된다. 캡처 지점에 그 이유를 적었다.
- **검증**(콘솔, 활성 RDP 없음): update 12종 전부 PASS — check 28 / effects **198** / handoff 33 / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / state_machine 92 / assembly 40 / options 48 / scenarios **100**. 나머지 C++ 바이너리 전부 exit 0. 스위트 실행 후 **잔존 프로세스 0 · `.claude/scenario-runs/` 0개 · `%TEMP%\gnlink-scn-*` 0개**.
- 변경 파일: `apps/native_poc/src/update_relaunch.{hpp,cpp}` · `update_effects.cpp` · `update_effects_test.cpp` · `updater_scenarios_test.cpp` · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/full_code_audit_2026-09-08.md`.
- 라이브·설치·배포·버전 인상·릴리스 보류 그대로. 산출물 없음.

### 472) 2026-09-09 문서 정합성 점검 — **설계가 아직 "셸 경유" 만 말하고 있었다**
- **설계문서 정정**(`docs/업데이트_기능_설계.md`):
  - 재실행 표의 `GNLinkClient.exe` 행이 **"셸을 통해 비승격으로"** 로만 적혀 있었다. 기본 경로는 이제 **사용자 토큰(`CreateProcessWithTokenW`)** 이고 셸은 폴백이다. **3.11.1 절 신설** — 두 경로의 차이는 무결성 수준이 아니라(그건 같다) **handle 을 돌려받는가**이고, handle 이 필요한 이유는 **롤백이 먼저 멈춰야 하기 때문**이다. 스냅샷 차분이 소유 증거가 아니라는 것과, 폴백은 소유를 주장하지 않아 **롤백에 들어가지 않는다**는 것도 같은 자리에 적었다.
  - 상태표 9행이 **"재실행 실패는 롤백 사유가 아니라 보고 사유"** 라고만 돼 있었다 → **필수/선택 구분**과 `Unknown` 을 성공으로 세지 않는다는 규칙 추가.
  - 상태표 10행에 **"선택 이미지가 안 떴어도 Health 를 먼저 묻는다"** 추가 — `CreateProcess` 가 돌아온 것은 호스트가 떴다는 뜻이 아니다(#470).
- **배선 계획 정정**(`docs/업데이트_배선_계획.md`): E5 를 토큰 경로 기준으로 바꾸고, **E5b 신설**(폴백이 소유를 주장하지 않고 롤백이 시작되지 않는지 — 시나리오 7 + 역대조).
- **체크리스트 정정**(`docs/수동확인_체크리스트.md`):
  - UPD-FIELD-01 제목을 "비승격 재실행(기본은 사용자 토큰, 셸은 폴백)" 으로. **무결성 수준으로는 두 경로를 구분할 수 없으므로 로그로 판정**하는 기준을 적었다. 상승 권한 업데이터에서 **폴백이 상시로 쓰이면 그 자체가 결함**이라는 것도.
  - 클라이언트 백업 행: **"재현되지 않는다"** 로 갱신. 시나리오 8 이 그 형태를 재현하고 고아가 안 생긴다. **미상으로 남은 것은 "무엇이 없앴는지"** 하나이며, 다시 나타나면 `RmGetList` 가 홀더 이름을 함께 낸다.
- **깨진 참조 정리**: 설계문서가 원장을 `full_code_audit_2026-09-08.md:94` 로 **줄 번호**로 가리켰는데 `I01` 은 **95행**이었다(원장이 자라면서 밀림). 두 곳 모두 **ID 참조**로 바꿨다 — 줄 번호는 자라는 문서를 가리키기에 부적합하다.
- **하네스 죽은 주석 제거**: `report_leftovers` 머리에 삭제하던 시절의 "KNOWN WART ... **Scoped to this test's own name pattern, so it can never remove anything else**" 가 남아 있었다. **그 문장이 바로 그 삭제를 괜찮아 보이게 만든 추론**이라 함께 지웠다.
- 검증: `updater_scenarios_test` **ALL PASS (100 checks, 0 failed)**, 잔존 프로세스 0. 제품 코드 변경 없음(주석 1건 + 문서).
- 변경 파일: `apps/native_poc/src/updater_scenarios_test.cpp`(주석) · `docs/업데이트_기능_설계.md` · `docs/업데이트_배선_계획.md` · `docs/수동확인_체크리스트.md` · `docs/history.md`.

### 473) 2026-09-09 재실행을 두 단계로 — **Required → health → commit → Optional**
- **왜 순서인가**: 클라이언트는 로그인 사용자 컨텍스트로 띄운다. handle 을 돌려주는 경로가 실패하면 셸이 대신 띄우고 **아무것도 돌려주지 않는다** — 어느 프로세스인지 확정할 수 없으니 **멈출 수도 없고**, 롤백은 그 프로세스가 잡고 있는 파일을 옮겨야 한다. **띄운 뒤에 소유 불가를 알아내는 방식은 구조적으로 늦다**(그때는 이미 프로세스가 존재한다). 그래서 **선택 이미지는 되돌릴 것이 없어진 뒤에** 띄운다.
- **인터페이스 분리**: `UpdateEffects::Relaunch()` → **`RelaunchRequired()` / `RelaunchOptional()`**. `RelaunchEffects`·`UpdateEffectsConfig`·조합 배선·`unwired()` 전부 두 seam 으로. 계획 필터는 `required_kind()` 하나를 `RelaunchOutcome::required()` 옆에 두어 **둘이 어긋날 수 없게** 했다.
- **본문은 하나, 진입점만 둘**(`run_phase(bool)`): 생존 검사·소유 규칙·로그 mark 가 두 벌이 되면 갈라지고, **갈라지는 쪽은 commit 뒤에 도는 쪽**이라 아무도 안 본다.
- **outcome 은 시도 단위로 누적**한다. 단계마다 비웠더니 **required 기록이 optional 단계와 함께 사라져** 그 뒤에 읽는 쪽이 **빈 벡터를 인덱싱**했다(실측 segfault). 로그는 새로 늘어난 것만 찍는다.
- **롤백 경로도 같은 순서**: 복원 → **구 required** → health → **구 optional**. 선택 이미지 실패가 **필수 health 판정을 가리거나 파일 복구를 막지 않는다.**
- **종착 계약 정정**: `RolledBackNotRelaunched` 는 이제 **복원 뒤 필수가 안 돌아온 경우**만이다. 창 하나가 안 열린 것과 **기계에 닿을 수 없는 것**을 같은 이름으로 부르고 있었다. commit 뒤 optional 실패는 `UpdatedButNotRelaunched` + 안내(**`Updated` 로 숨기지 않는다**).
- **회귀 7종**(요청받은 그대로, `update_state_machine_test` 115 checks):
  1. 토큰 실패 + 셸 가능 + 새 host 불건강 → **롤백 성공**, **롤백 전 optional 기동 0**
  2. RequiredMissing 이 OptionalMissing 을 이긴다, 그리고 optional 단계에 도달하지 않는다
  3. 정상 경로: **commit 전 optional 0 / 후 1**, required 는 health 앞
  4. 롤백 뒤 **구 optional 복귀**(required·health 뒤에)
  5. client-only: 필수 계획 없음 → 정상 → commit → optional, **health 1회**
  6. commit 뒤 optional 실패 → `UpdatedButNotRelaunched`, 백업은 이미 없고 롤백 없음
  7. 부분 quiesce → 디스크 무변경 abandon, **두 단계 각 1회**
- **실제 제품 경로 회귀**(`updater_scenarios_test` 115 checks): **시나리오 7 을 뒤집었다.** 예전엔 "소유 불가 클라이언트 때문에 롤백이 거부된다" 를 고정했는데, 이제는 **"그 상황 자체가 생기지 않는다"** 를 고정한다 — 롤백이 정상 수행되고 구 바이트가 돌아온다. **역대조**: 같은 소유 불가 클라이언트라도 commit 하는 경로에서는 실제로 셸로 뜬다. **시나리오 9 신설**: client-only 실제 경로.
- ⚠️ **반대 증거(순서를 되돌려 측정)**: state machine **3건 실패**(호출 흔적이 `... RelaunchRequired, RelaunchOptional, HealthCheck, Commit ...`), 시나리오 **4건 실패** — 시나리오 7 이 **`RollbackFailed`** 로 바뀌고 로그가 `started by the shell ... | could not stop ScnClient.exe ... | rollback failed: not rolling back` 을 그대로 찍는다. **이 순서가 없으면 실기에서 롤백이 막힌다는 것이 제품 경로에서 재현된다.**
- **하네스 결함 1건**: `oldHealthOk` 노브가 **한 번도 발화하지 않았다** — `rolledBack` 을 아무도 true 로 만들지 않아 복원된 빌드도 `newHealthOk` 로 판정됐다. `RestoredButUnhealthy` 를 기대하던 케이스들이 **테스트하지 않는 이유로** 통과하고 있었고, 깨끗한 `RolledBack` 은 만들 수조차 없었다. 두 번째 required 단계를 롤백 신호로 삼아 연결했다.
- **`update_relaunch_test` 를 unbuffered 로**: 이 스위트는 실제 프로세스를 띄우므로, 죽는 순간의 보고가 버퍼에 남아 사라지면 **정확히 필요할 때** 없다. 위 segfault 를 이것 없이는 못 짚었다.
- **검증**(콘솔, 활성 RDP 없음): update 12종 전부 PASS — check 28 / effects 199 / handoff 33 / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / **state_machine 115** / assembly 40 / options 48 / **scenarios 115**. 나머지 C++ 바이너리 전부 exit 0. 잔존 프로세스 0 · `.claude/scenario-runs/` 0.
- 변경 파일: `update_state_machine.{hpp,cpp}` · `update_relaunch.{hpp,cpp}` · `update_effects.{hpp,cpp}` · `updater_effects.cpp` · 테스트 6종 · `docs/history.md` · `docs/업데이트_기능_설계.md` · `docs/full_code_audit_2026-09-08.md`.
- 라이브·설치·배포·버전 인상·릴리스 보류 그대로.

### 474) 2026-09-09 ⚠️ **테스트가 사용자 화면에 오류창을 띄웠다** — 발사한 뒤 정리하면 늦는다
- **무슨 일**: 사용자 화면에 `...\Temp\gnlink-scn-1052\install\ScnClient.exe 를 찾을 수 없습니다` 대화상자가 떴다. `%TEMP%` 경로이므로 **`3edd5cd` 이전 실행**이 낸 셸 요청이, **디렉터리가 정리된 뒤에** 탐색기에서 뒤늦게 실행된 것이다.
- **`GetFileAttributes` 선확인은 방어가 아니다.** 확인 시점엔 파일이 있었고, 정리가 지웠고, 셸은 **나중에** 실행했다. 확인과 실행 사이가 곧 삭제가 일어난 구간이다 — **TOCTOU**. 주석이 "이걸로 모달을 막는다" 고 적고 있었고, 그 문장부터 고쳤다. **막는 것은 선확인이 아니라 "요청이 도착할 때 파일이 아직 거기 있는 것"** 이고, 그건 **누가 언제 지워도 되는가**의 문제다.
- **fixture 를 자기 종결형으로 교체**(`scn_dummy_main.cpp` 신설, `remote60_scn_dummy`): 실행되면 **자기 이름과 pid 를 `witness.txt` 에 적고 즉시 종료**하는 진짜 PE. 예전 fixture 는 **명령 해석기 사본**이라 ⓐ 절대 종료하지 않아 매 시나리오가 하나씩 남겼고(그래서 **이름으로 죽이는 쓸어담기**가 필요했다), ⓑ **셸 요청이 실제로 도착했는지 알 방법이 없었다.**
- **수명을 launch 종결까지 보존**: 셸로 발사한 시나리오는 **그 시나리오 안에서** witness 줄을 기다린다(다음 `seed()` 가 witness 를 지우므로, 정리 시점에 묻는 것은 **다른 실행에 대해 묻는 것**이다 — 첫 구현이 그래서 안 지워도 될 디렉터리를 남겼다). 끝내 안 오면 **디렉터리를 지우지 않고** 사유를 찍는다. **저장소 안 쓰레기는 싼 값이고, 남의 화면에 뜬 대화상자는 그것과 바꿀 것이 아니다.**
- **`update_relaunch_test` 도 같은 모양이었다**: `%TEMP%` 에 fixture 를 만들고 셸로 발사한 뒤 트리를 지웠다. root 를 저장소 안(`.claude/relaunch-runs`)으로 옮기고, **클라이언트 witness 를 기다린 뒤에만** 지운다.
- **거절 사유를 분리**: 첫 구현이 "보류 중" 과 "root 밖" 을 같은 `else` 로 묶어, **root 안에 있는 디렉터리를 두고 "root 밖이라 못 지운다"** 고 찍었다. **틀린 이유를 대는 메시지는 없느니만 못하다** — 읽는 사람을 엉뚱한 데로 보낸다.
- **생산 경로 검토**(지시 5): **이번 시도 수명 안에서는** 설치 디렉터리가 삭제되지 않고, **선택 이미지는 commit 뒤에만 기동**되므로(#473) 뒤이어 롤백이 파일을 바꿔치지 않는다. 늦게 도착한 요청은 제자리에 있는 새 클라이언트를 띄운다. ⚠️ **정정(#477)**: 이것은 **파일이 항상 존재한다는 보장이 아니다.** 이후의 다른 업데이트나 제거는 그 파일을 바꾸거나 없앨 수 있고, 그때까지 남아 있던 셸 요청은 그들이 남긴 것을 만난다. **닫을 수 없는 창이며, 보장인 척하지 않는 편이 낫다.**
- **하지 않은 것**: `%TEMP%` 삭제·광역 이름 kill·대화상자 닫기 **전부 안 했다.** `%TEMP%\gnlink-exec-*` 3건은 **목록만** 남긴다(모두 실제 `.cmd` 를 담고 있어 늦은 요청이 와도 대화상자가 아니라 dummy 가 뜬다). `.claude/scenario-runs/scn-27308` 1건은 **위 규칙이 스스로 남긴 것**이라 그대로 둔다.
- **검증**(콘솔, 활성 RDP 없음): update 12종 전부 PASS — check 28 / effects 199 / handoff 33 / http 36 / job_guard 19 / manifest 82 / **relaunch 107** / release 80 / state_machine 115 / assembly 40 / options 48 / **scenarios 115**. 실행 후 **잔존 프로세스 0**, 새 `%TEMP%` 디렉터리 0건.
- 변경 파일: `apps/native_poc/src/scn_dummy_main.cpp`(신설) · `updater_scenarios_test.cpp` · `update_relaunch_test.cpp` · `update_relaunch.cpp`(주석) · `apps/native_poc/CMakeLists.txt` · `docs/history.md` · `docs/full_code_audit_2026-09-08.md`.

### 475) 2026-09-09 ⚠️ **테스트가 라이브 제품과 같은 전역 뮤텍스를 쓰고 있었다**, 그리고 잡아두는 통제가 안 잡고 있었다
- ⚠️ **`Global\GNLinkUpdate` 를 조합이 상수로 박고 있었다**(`updater_effects.cpp:199`). 이 조합을 돌리는 **모든 테스트가 설치된 제품과 같은 기계 전역 뮤텍스를 두고 경쟁**한다. 가정이 아니다 — 실행 하나가 `another update or installer holds the lock` 로 실패했고(`NothingToDo`, 시나리오 2·4·5·6 연쇄 실패), **반대 방향이면 테스트가 진짜 업데이트를 막는다.**
  - `UpdaterDeps::lockName` 으로 **주입 대상**으로 바꿨다. `validate()` 가 요구하고, 생산은 `production_updater_deps()` 에서 기계 전역 이름을 넣는다. 시나리오는 `Local\GNLinkScenarioTest-<pid>`, 조합 테스트는 `Local\GNLinkAssemblyTest`.
  - 회귀: **생산이 여전히 `Global\GNLinkUpdate` 를 공급하는지**를 조합 테스트가 단정한다(주입으로 바꾸면서 생산 값이 조용히 바뀌는 것이 이 세션의 반복 형태였다).
- **"실행 중 이미지를 잡고 있다" 던 통제가 잡고 있지 않았다**: fixture 를 자기 종결형으로 바꾼 뒤, 그 통제의 victim 도 **즉시 종료**하게 됐다. `DeleteFileW` 가 실패하는지 보는 단정이 **타이밍에 따라** 통과했다. 그 통제에만 **종료하지 않는 명령 해석기 사본**을 쓰도록 되돌렸다(직접 `CreateProcessW`, 받은 handle 로 정지 — 셸 경유 아님).
- **보류 사유를 시나리오 단위로 지목**: "셸 launch 가 기록을 안 남겼다" 만으로는 **어느 케이스인지 알 수 없어** 조치할 수 없다. 30초 안에 안 오면 그 시나리오의 로그를 함께 찍는다.
- **검증**(콘솔, 활성 RDP 없음): update 12종 전부 PASS — check 28 / effects 199 / handoff 33 / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / state_machine 115 / **assembly 41** / options 48 / scenarios 115. 실행 후 **잔존 프로세스 0**, 새 `%TEMP%` 디렉터리 0, 시나리오 스위트가 **자기 디렉터리를 스스로 회수**(보류 NOTE 없음).
- **손대지 않은 잔재**(지시대로): `%TEMP%\gnlink-exec-*` 3건 · `.claude/scenario-runs/scn-27308` 1건.
- 변경 파일: `updater_effects.{hpp,cpp}` · `updater_assembly_test.cpp` · `updater_scenarios_test.cpp` · `docs/history.md` · `docs/full_code_audit_2026-09-08.md`.

### 476) 2026-09-09 문서 정합성 2차 — **수동 검사가 일어날 수 없는 결과를 보라고 시키고 있었다**
- ⚠️ **UPD-FIELD-01 정정**: "폴백으로 떴으면 일부러 실패를 만들어 **`RollbackFailed`** 가 나오는지 보라" 고 적혀 있었다. **#473 이후 일어나지 않는 결과다** — 선택 이미지는 commit 뒤에만 뜨므로 **롤백이 가능한 구간에 소유 불가 프로세스가 존재하지 않는다.** 실기하는 사람이 **시키는 대로 해도 답이 안 나오는 지시**였다. 지우고, 그 성질은 시나리오 7 이 단정한다고 적었다. (앞 라운드의 "무결성 수준으로 경로를 판정하라" 와 같은 부류 — **수동 검사판 공허한 단정**이 이틀 연속 나왔다.)
- **로그 예시도 실제 출력과 달랐다**: `relaunch GNLinkClient.exe: ...` → 단계 분리 뒤 실제 형식은 **`relaunch (optional) GNLinkClient.exe: ...`** 다.
- **UPD-FIELD-01 에 순서 항목 추가**: 클라이언트는 **commit 뒤**에 뜨므로 **몇 초 늦게 나타나는 것이 정상**이고, **호스트보다 먼저 뜨면 그것이 결함**이다.
- **배선 계획 W4a 정리**: `lastActions` 를 "이미 만들지만 아무도 읽지 않는다" 고 적고 있었는데 **그 함수는 지웠다**(읽히지 않는 출력은 기록이 아니라 미결이다) → `lastOutcomes` + 단계별 로그로 갱신. "`relaunch` 는 `bool` 하나를 돌려준다" 도 **verdict → 두 단계**까지 왔음을 표시. **E5c 추가**(선택 이미지가 commit 뒤에만 뜨는가, 반대 증거 포함).
- **"0. 먼저 사실부터" 표에 경고 배너**: `72d81d9` 시점 기록인데 현재처럼 읽힐 수 있었다. 첫 두 줄(`run_update` 호출자 0건 등)은 **`GNLinkUpdater.exe` 로 이미 해소**됐고 줄 번호도 전부 밀렸다. **당시 기록으로 읽으라**고 명시.
- **깨진 줄 참조 정리**: `update_state_machine.cpp:167-178` → 이름 참조. 이 파일들에 대한 줄 참조는 이제 **0건**(다른 파일 것은 이번 작업 범위 밖이라 두었다).
- **없는 참조 1건**: 3.11.0 이 "health 는 기다릴 대상이 없다는 것을 알고 즉시 답한다(**3.12**)" 라고 가리키는데, **3.12 에 그 규칙이 없었다.** 따라가면 없는 참조다 — 코드에는 있고 문서에만 없었다. 3.12 에 두 줄을 추가했다: **보고할 주체가 없으면 기다리지 않는다**(호스트가 안 돌던 기계의 업데이트를 "업데이트 전에 어떤 프로세스가 안 돌고 있었다" 는 이유로 롤백하지 않는다, 회귀는 시나리오 9), 그리고 그 반대쪽 — **아무것도 재실행되지 않았는데 "건강하다" 고 답하지도 않는다**(묻지 않은 질문에 답하는 것이다). 문서의 두 문장은 코드의 `healthDetail` 두 갈래와 1:1 이다.
- 설계문서의 내부 절 참조 **전수 확인**: `(3.1)`·`(3.2)`·`(3.3)`·`(3.5)`·`(3.6)`·`(3.7)`·`(3.8.1)`·`(3.11.0)`·`(3.12)` 전부 실제 절로 해소된다.
- 문서만 변경. 제품 코드·테스트 변경 없음.
- 변경 파일: `docs/수동확인_체크리스트.md` · `docs/업데이트_배선_계획.md` · `docs/업데이트_기능_설계.md` · `docs/history.md`.

### 477) 2026-09-09 ⚠️ **handoff 가 끊어져 있었다** — Host 는 bootstrap 을 기다리고 있었다
- **결함**: Host 가 `WaitForMultipleObjects({readyEvent, process})` 의 `process` 로 **bootstrap** 핸들을 넘겼다. bootstrap 은 **자기가 실행 중인 파일이 교체 대상**이라 copy 를 띄우고 **즉시 종료**한다 — 그것이 정상 경로다. Host 는 그 종료를 **"업데이터가 죽었다"** 로 읽고 `readyEvent` 를 닫은 뒤 **"설치된 버전은 그대로이며 계속 사용할 수 있습니다"** 를 띄웠다. 그 사이 copy 는 멀쩡히 다운로드 중이었고, `signalReady` **반환값을 안 보고** 진행해 **방금 아무 일도 없을 거라고 말한 그 Host 를 종료**했다.
  → 원격 사용자에게는 **"업데이트 안 합니다" 를 듣고 그 기계를 잃는** 형태다. **이번 세션에서 사용자가 겪는 결과가 가장 나쁜 결함이다.**
- **부품은 전부 옳았다.** bootstrap 의 즉시 종료도, worker 의 늦은 signal 도, Host 의 대기도 각각 맞다. **셋을 붙였을 때만** 나타난다 — 어느 2프로세스 테스트로도 볼 수 없었다.
- **고친 것**
  - `HandoffStep{KeepWaiting, ExitNow, KeepRunning}` — **없던 세 번째 답이 결함이었다.** bootstrap 종료 + **exit code 0** 은 *계속 기다림*, **0 아님** 은 *지금 중단*. 종료 코드를 **읽는다**(못 읽으면 실패로 본다 — 알 수 없는 종료가 의도된 인계로 오인되면 안 된다).
  - **ack 이벤트 신설**(`make_ack_event_name`, ready 이름에서 파생 — 한 시도에 하나의 stem, 두 이벤트라 서로 다른 시도의 채널로 답할 수 없다). Host 는 **ExitNow 일 때만** ack 한다.
  - `signalReady` 가 **bool 을 돌려준다**(열리지 않으면 아무도 안 기다리는 것이다). `awaitAck` 신설. **둘은 함께 주입되어야 한다** — 하나만 있는 것이 정확히 이 결함의 모양이라 `validate()` 가 막는다.
  - `may_stop_the_product(delivered, acked)` 가 **PrepareForSwap 에서** 판정한다. 실패하면 **디스크를 하나도 건드리지 않은 채 abandon**.
  - Host 의 대기 루프를 **`await_handoff` 로 분리**했다. lambda 안에 있어서 **제품을 실제로 업데이트하지 않으면 돌릴 수 없었고, 그래서 한 번도 안 돌았고, 그래서 틀린 채로 있었다.**
- **회귀 — 실제 3프로세스**(`update_handoff_process_test`, 신설 fixture `remote60_handoff_fixture`): bootstrap·worker 를 **진짜 프로세스**로 띄우고, Host 쪽은 **제품이 부르는 그 `await_handoff`** 를 부른다(재현이 아니라 그 함수여야 한다 — 재현본을 시험한 것이 원래 결함이 남은 이유다).
  ① **즉시 bootstrap exit + 늦은 worker** → ExitNow, worker 진행 ② **bootstrap 실패(exit 3)** → 즉시 중단, 전체 timeout 을 앉아서 기다리지 않음 ③ **worker 무신호** → KeepRunning, worker 스스로 물러남 ④ **timeout 뒤 늦은 worker** → ack 없음 → **worker 가 제품을 종료하지 않음** ⑤ **종료 경합**: timeout 을 worker 지연 **양쪽으로 훑어** 두 결과가 실제로 다 나오는지까지 단정(한쪽만 나오면 레이스를 시험한 게 아니다).
- ⚠️ **반대 증거**: 옛 규칙(어떤 종료든 실패)으로 되돌리면 **정상 업데이트 케이스 ①이 `KeepRunning` 으로 실패**하고, ⑤의 훑기에서 **ExitNow 가 한 번도 나오지 않는다** — 즉 **그 코드로는 업데이트가 성립하지 않는다.** 단위 테스트도 2건 실패.
- **조합 회귀도 추가**(`updater_scenarios_test` 시나리오 10): **전달됐지만 응답 없음** → abandon, 디스크 무변경, 사유가 "never answered" / **전달 자체 실패** → 사유가 "could not be delivered" / **역대조**: 정상 응답이면 진행.
- **표현 정정**(지시): "생산 fallback 은 파일이 항상 존재한다" 를 **이번 시도 수명 안으로** 좁혔다. 이후의 다른 업데이트·제거는 그 파일을 바꾸거나 없앨 수 있다. **닫을 수 없는 창이며, 보장인 척하지 않는 편이 낫다.**
- **검증**(콘솔, 활성 RDP 없음): update 13종 전부 PASS — check 28 / effects 199 / **handoff 44** / **handoff_process 18** / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / state_machine 115 / assembly 41 / options 48 / **scenarios 142**. 나머지 C++ 바이너리 전부 exit 0. 잔존 프로세스 0, `.claude/handoff-runs` 0(자기 회수).
- **worker 수명 추적 추가**(지시의 "Host 가 실제 worker 의 수명·ready 를 추적"): 첫 구현은 **bootstrap 종료의 의미**만 고쳤고, copy 가 도중에 죽으면 **timeout 전체를 앉아서 기다렸다.** working copy 가 `<stem>.alive` **뮤텍스를 자기 수명 동안 보유**하고(bootstrap 은 보유하지 않는다 — 즉시 종료가 정상이라 그 종료는 아무 뜻이 없다), Host 는 인계 뒤 그것을 열어 함께 기다린다. **뮤텍스인 이유**: 보유자가 죽으면 `WAIT_ABANDONED` 로 **풀린다** — 정리 코드를 못 돌린 죽음이야말로 잡아야 할 죽음이라, 나가면서 무언가를 알려야 하는 방식은 쓸 수 없다.
  - **실측**: worker 가 죽으면 **328ms** 에 알아채고 사유도 "the working copy stopped before it had a verified download" 로 분리된다. **반대 증거**: 추적을 끄면 **20000ms 를 다 기다리고** 사유가 "did not reach a verified download in time" 로 뭉개진다.
  - ⚠️ **이 328ms 가 "모든 사망" 은 아니다**(#478 에서 좁힘): **뮤텍스를 잡기 전에 죽으면 관측 대상이 없어 timeout 경로**로 간다(창이 작을 뿐 0 이 아니다). 그리고 뮤텍스는 **보유 스레드의 수명**이지 프로세스 핸들과 항상 같은 관측력이 아니다 — **working copy 가 main thread 에서 잡고 종료까지 들고 있다는 계약** 위에서만 같다. **OS 의 성질이 아니라 계약이다.**
- **내 루프 결함 1건**(반대 증거로 잡힘): 대기를 슬라이스로 쪼개면서 `handoff_step` 을 **슬라이스마다** 불렀더니, **"아직 아무 일도 없음" 이 최종 답("the updater did not signal")으로 굳어** 첫 100ms 에 대기가 끝났다. **아무 일도 일어나지 않은 것은 사건이 아니다** — 루프가 자기 인내를 실패로 보고하고 있었다.
- **테스트 타이밍 단정 정정**: "빨리 알아챈다" 를 **전체 시도 시간**으로 재고 있었는데, 그 안에는 **테스트 자신이 witness 를 폴링하는 시간**이 들어 있다. 측정 대상을 `await_handoff` 로 좁혔다 — **제품의 인내가 아니라 테스트의 인내를 재고 있었다.**
- 변경 파일: `update_handoff.{hpp,cpp}` · `update_handoff_wait.cpp`(신설) · `handoff_fixture_main.cpp`(신설) · `update_handoff_process_test.cpp`(신설) · `update_handoff_test.cpp` · `host_app_main.cpp` · `updater_main.cpp` · `updater_effects.{hpp,cpp}` · `updater_assembly_test.cpp` · `updater_scenarios_test.cpp` · `update_relaunch.cpp`(주석) · `CMakeLists.txt` · 문서 3종.

### 478) 2026-09-09 ⚠️ **ack 이벤트가 worker 가 열기 전에 사라질 수 있었다** — bool 은 맞고 핸들이 없어진 형태
- **A) ack 채널 수명**: Host 가 `SetEvent(ack)` 뒤 **곧바로 `CloseHandle`** 하는데, worker 는 **`PrepareForSwap` 에 가서야** `OpenEventW` 했다. Host 핸들이 마지막이면 **named object 가 그때 소멸**한다 → worker 의 open 실패 → `acked=false` → **정상 승인이 전달됐는데도 abandon.** 그런데 Host 는 이미 나가는 중이다.
  - **worker 가 ready 를 보내기 전에 채널을 열어 보유**하고, 답을 받은 뒤 놓는다(`openAck`/`awaitAck`/`closeAck` 3-seam, `validate()` 가 **넷 다 함께**를 요구 — `signalReady` 포함). 채널을 못 열면 **신호 자체를 보내지 않는다**: 답을 들을 수 없는데 상대를 내보내면 안 된다.
  - **Host 쪽도 고쳤다**: `SetEvent` 실패거나 채널이 없으면 **`ExitNow` 를 반환하지 않는다.** 답이 도착해야 나가는 것이 안전하다.
  - ⚠️ **bool seam 만 봐서는 안 잡힌다** — 반환값은 전부 맞았고 **핸들이 사라졌다.** 그래서 회귀를 **실제 named handle 수명**으로 짰다: ⓐ 미리 보유 → Host 가 놓아도 답이 남아 있다 ⓑ **역예시(늦게 열기)** → "late and it was gone" → worker stood down **(Host 는 ExitNow 를 반환한 채로)** ⓒ **채널 없음** → Host 가 `ExitNow` 를 내지 않고 사유를 댄다 ⓓ **채널은 있는데 쓸 수 없음**(`SYNCHRONIZE` 만 가진 핸들 → `SetEvent` 가 access denied) — ⓒ 와 **코드 경로가 다르다**(하나는 보내기를 건너뛰고, 하나는 보내다 거부당한다). **둘 다 같은 결말이어야** 하므로 따로 고정했다. `handoff_process` **33 checks**.
- **B) `GetTickCount` wrap**: `deadline = GetTickCount() + timeoutMs` 는 **32bit 합이라 49.7일 가동에서 넘친다** → 즉시 timeout. `remaining_ms(startedAt, now, timeout)` 로 분리해 **64bit 뺄셈**으로 바꾸고 **시계를 주입 가능**하게 했다. 단위 회귀를 **경계값에서** 넣었다(`0xFFFFFF00` 근처, 그 너머, 뒤로 간 시계). `WAIT_FAILED` 도 **명시 종결** — 없으면 남은 시간 내내 같은 실패를 스핀하고 timeout 으로 보고한다.
- **C) 표현 정정**: **"사망 감지 328ms" 는 조건부**다. **뮤텍스 확보 전 사망은 관측 대상이 없어 timeout 경로**이고(창이 작을 뿐 0 이 아니다), 뮤텍스는 **보유 스레드 수명**이라 프로세스 수명과 같은 것은 **main thread 에서 잡고 종료까지 보유한다는 계약** 위에서다. **OS 의 성질이 아니라 계약**이라고 헤더·원장·history 에 적었다.
- **검증**(콘솔, 활성 RDP 없음): update 13종 전부 PASS — check 28 / effects 199 / **handoff 50** / **handoff_process 30** / http 36 / job_guard 19 / manifest 82 / relaunch 107 / release 80 / state_machine 115 / assembly 41 / options 48 / scenarios 142. 나머지 C++ 바이너리 전부 exit 0. 잔존 프로세스 0, `.claude/handoff-runs` 자기 회수, `%TEMP%` 신규 0.
- 변경 파일: `update_handoff.{hpp,cpp}` · `update_handoff_wait.cpp` · `update_handoff_test.cpp` · `update_handoff_process_test.cpp` · `handoff_fixture_main.cpp` · `host_app_main.cpp` · `updater_effects.{hpp,cpp}` · `updater_assembly_test.cpp` · `updater_scenarios_test.cpp` · 문서 2종.

### 479) 2026-09-09 0.2.105 / APK 0.2.13 후보 산출물 — **자동업데이트 실사용 준비완료가 아니다**
- **버전**: Windows `kProductVersion` **0.2.104 → 0.2.105**(`product_version.hpp`, 설치기의 `DisplayVersion`·등록·제목이 전부 이 상수를 쓴다). Android **`versionCode` 11 → 12**, **`versionName` 0.2.12 → 0.2.13**.
- **산출물**(`dist/`, 버전 붙인 이름):
  - `GNLinkSetup-0.2.105.exe` — **4,127,744 B** · sha256 `f4c0ef6b1f53f615c80a735d48757e0a2d91a6c64c45fad0cc572d50865ad253`
  - `GNLink-0.2.13.apk` — **11,501,102 B** · sha256 `4da45491800a891be8569d57b751448fca7f3e98cfa7cec30c6bda6c2cc14d2b`
- **직접 확인한 것**:
  - 설치기 안에 **0.2.105 가 3건, 0.2.104 는 0건**(UTF-16 스캔). payload 이름 문자열도 있고, `ui\shell.html` 이 **깨지지 않은 형태**다 — **#468 escape 결함의 회귀 확인으로는 이걸로 충분하다.**
  - ⚠️ **다만 이름이 있다는 것은 payload 가 그 바이트라는 증거가 아니다.** 내가 처음 낸 근거가 그것뿐이었고, **약했다.** 실제 근거는 **PE 리소스 디렉터리를 파싱해 RT_RCDATA 를 꺼내 staging 9파일과 대조한 것**이다: id 200~208 ↔ Host 601,600 / Stream 974,848 / InputService 226,304 / Capture 127,488 / Client 635,904 / Viewer 848,896 / shell.html 12,648 / macro.html 10,996 / **Updater 482,816** — **크기·SHA256 9/9 일치, 예상 외 RCDATA id 없음.** (검증용 측정. 설치 실행·재빌드 없음.)
  - **재빌드 대조는 별개 사실이다**(provenance 근거가 아니라 부수 확인): `9ff1716` 재빌드본과 `dist` 산출물은 **크기 동일**하고 **다른 바이트가 40 개**뿐이며, 그중 **36 바이트가 `.rsrc` 안** — 항목별로 Host 4 / Stream 6 / InputService 6 / Capture 6 / Client 4 / Viewer 6 / Updater 4 로, **전부 내장 PE 각각의 COFF·Debug TimeDateStamp** 다. html 두 개는 **바이트 동일**, **미분류 0 바이트**, **CodeView(PDB) GUID 는 한 바이트도 다르지 않다.** 즉 **차이가 코드·데이터가 아니라 타임스탬프에 한정된다는 측정**이지, "재현 빌드가 원래 안 된다" 는 일반론이 아니다.
  - payload 스테이징 9종 전부(`GNLinkUpdater.exe` **482,816 B** 포함).
  - APK: **package `com.remote60.androiddirect`**(불변) · **versionCode 12**(배포본 11 초과) · **서명 SHA-256 `dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990`** — **배포본과 같은 debug keystore**, `assembleDebug` 산출물. **새 키를 만들지 않았다.**
- ⚠️ **APK 권한이 하나 늘었다 — 배포본과의 사용자 가시 차이다.** 배포본 0.2.12 는 `INTERNET`·`ACCESS_NETWORK_STATE`(+ 동적 리시버용 자체 권한) 뿐이었는데, 0.2.13 에는 **`android.permission.REQUEST_INSTALL_PACKAGES`** 가 추가됐다(`aapt2 dump badging` 으로 두 APK 직접 대조). W7 의 `PackageInstaller` 경로가 요구하는 권한이라 **의도된 것**이지만, 사용자에게는 **"알 수 없는 앱 설치" 허용을 요구하는 새 항목**으로 보인다. **설치 전에 알아야 하는 차이**이므로 여기 적는다. history #331·#333 시점 기록("`REQUEST_INSTALL_PACKAGES` 없음")은 **그때의 사실**이고 지금은 아니다.
  - **검증용의 산출물 검사에서 걸렸다.** 내 릴리스 기록에는 빠져 있었다 — 산출물이 무엇을 바꾸는지 적을 때 **버전과 해시만 적고 권한을 안 봤다.**
  - ⚠️ **권한은 `app/build/intermediates/**` 가 아니라 APK 자체에서 확인할 것.** intermediates 의 release 쪽 `AndroidManifest.xml` 에는 이 권한이 **없다**(낡은 산출물이다). 그걸 근거로 "권한 안 늘었다" 고 읽을 수 있다. 확인은 `aapt2 dump badging` 또는 APK 안 manifest 문자열 풀로 한다 — **두 APK 를 그렇게 대조해서 추가 1 · 제거 0** 을 확인했다.
- ⚠️ **막히는 것은 앱 안의 자동 업데이트 경로뿐이다. 설치본 자체는 정상 실행된다.** 앞선 문구가 "서명 거부 = 설치본 실행 불가" 로 읽힐 수 있어 정정한다 — **둘은 다른 이야기다.**
  - **자동 업데이트는 진행되지 않는다.** `trusted_public_key_hex()` 가 **빈 문자열**이고(`update_manifest.cpp:300`, "Deliberately empty"), `default_verifier()` 는 **키 길이가 안 맞으면 거부**한다(`key.size() != kP256PublicKeyBytes` → `false`). 따라서 **어떤 manifest 도 서명 검증을 통과하지 못하고**, 확인 단계에서 멈춘다. Android 도 같다: `UPDATE_MANIFEST_URL`·`UPDATE_PUBLIC_KEY_HEX` 가 **빈 기본값**이라 `UpdateFlow` 가 "이 빌드에는 업데이트 엔드포인트도 신뢰키도 없다" 를 보고하고 **아무것도 하지 않는다.**
  - **수동 설치는 지금도 된다.** `GNLinkSetup-0.2.105.exe` 는 **손으로 실행해 설치하는 데 아무 문제가 없다** — 신뢰키는 업데이트 manifest 를 검증하는 데만 쓰이고 설치기 자체와는 무관하다. APK 도 **배포본과 같은 signer 이고 `versionCode` 12 > 11** 이라 **기존 설치 위에 덮어쓰기가 된다.**
  - **fail-closed 를 유지한 결과지 결함이 아니다.** 운영 서명키는 승인이 필요한 결정이고 이 코드가 내릴 결정이 아니다.
- ⚠️ **"자동업데이트 실사용 준비완료" 로 읽지 말 것.** 이번 승인 범위는 **구현·격리검증**이고 **실제 자동업데이트 배포 승인이 아니다.** Codex 도 `837ac5f..bbeefe8` handoff diff 를 직접 대조한 것이지 **약 23k 전수 감사를 한 것이 아니라고 명시**했다.
- **실기에서만 확인 가능한 것(그대로 남는다)**: UAC 추가 창 0 · 원 사용자 컨텍스트 Client 실행 · 운영 서비스 등록 · 실 HTTPS 경로 · 실기기 APK 설치 · 운영키/배포 인프라 의존 항목 · **백업 홀더 — 현재 재현되지 않으며 원인 미특정**.
- **테스트가 기계에 한 일(3건)과 현재 경계**: 저장소 밖 `%TEMP%` 삭제(#474 해소) · 이름만으로 프로세스 종료(#471 해소) · 라이브와 같은 전역 뮤텍스 점유(#475 해소). 지금은 **테스트 root 가 저장소 안**이고, **경로가 확인된 것만** 종료하며, **세션 범위 lock** 을 쓴다. **과거 실행이 남긴 잔재 4건**(`%TEMP%\gnlink-exec-*` 3 · `.claude/scenario-runs/scn-27308` 1)은 **지시대로 삭제하지 않았다.**
- **하지 않은 것**: 설치·실행·게시·push·라이브 조작. **0.2.104 는 그대로**다(PID 3종 불변).
- 변경 파일: `apps/native_poc/src/product_version.hpp` · `apps/android_direct_client/app/build.gradle.kts` · `dist/GNLinkSetup-0.2.105.exe`(신규) · `dist/GNLink-0.2.13.apk`(신규) · `docs/history.md` · `docs/구현계획.md`.

### 480) 2026-09-09 운영 서명키 최초 생성 + 세 검증기 확인 — **키는 저장소 밖, 공개값만 기록**
- **최초 생성**(회전 아님): 기존 키 없음을 먼저 확인했다(`%LOCALAPPDATA%\GNLink\ReleaseSigning` 미존재, 저장소 내 `.pem/.key/.pfx/.jks` 0건).
- **key-id `p256-a0e184579c5d4c2d`** · 위치 `%LOCALAPPDATA%\GNLink\ReleaseSigning\<key-id>\`
  - **임베드용 공개키(raw X‖Y, 128 hex)** — `8709ea70daac6464af4ed0fff1ed7489ec9e9a4a908d48babe5b62753242d9a872a4556df0c9ffa2e98dc70e6c553a624a8a235e8c4303488743f37ba240193e`
  - **식별용 SPKI SHA-256**(임베드용 아님) — `f6121bcaa6679b638b34c90ed6ad7fac74017feaf998dc150a0c37ad36c95b49`
  - ⚠️ **둘을 바꿔 쓰면 조용히 죽는다.** 세 검증기 전부 raw X‖Y 64바이트만 받는다(C++ `update_signature.hpp:25` · Kotlin `UpdateManifest.kt:129,132` · Node `update_manifest.js:67`). SPKI hex 를 박으면 `decode_hex` 는 통과하고 `key.size() != 64` 에서 거부되어 **에러 없이 모든 업데이트가 영원히 무시**된다.
- **보관 방식**: 개인키는 **평문으로 디스크에 쓰이지 않는다.** OS 가 만든 P-256 키를 `BCRYPT_ECCKEY_BLOB` 으로 메모리에 꺼내 **DPAPI(CurrentUser)** 로 보호한 것만 기록(`private.ecc.dpapi`, 326 B). 키는 **이름 없는 ephemeral** 이라 키 컨테이너에 남지 않고, 평문 배열은 종료 전에 zero 화한다. 비밀값은 stdout·로그·A2A·Git 어디에도 나가지 않았다.
- **저장 전 왕복 자체검사**: `Unprotect → Import → SignData → 기록할 public_xy.hex 로 만든 공개키로 Verify`, 그리고 **부정 케이스**(probe 1바이트 뒤집으면 거부). 하나라도 실패하면 **아무것도 쓰지 않는다.** "감싸서 썼다" 는 "쓸 수 있는 키다" 가 아니다.
- **ACL**(생성 시점): Owner `shotan\shotan`. `Users`·`Everyone`·`Authenticated Users` **없음**. 미해결 SID 1건(`S-1-5-21-…-2881406636`, Write+ReadAndExecute)이 **`%LOCALAPPDATA%` 전체에서 상속**되고 있었다. 그 자리에서는 손대지 않았다 — 저장소 밖 ACL 을 요청 없이 바꾸지 않는다. **#481 에서 승인을 받아 키 폴더만 상속을 끊었다.**
  - ⚠️ **정정(#481)**: 여기 "계정이 해석되지 않는다(**삭제된 계정**)" 이라고 적었는데 **근거 없는 단정이었다.** SID 해석 실패는 도메인 미도달·다른 머신의 계정 등으로도 난다 — **해석 불가는 주체 없음이 아니다.** 실제로 그 SID 의 도메인부는 이 PC 사용자 SID(`S-1-5-21-3755351295-…`)와 **다르다**(`S-1-5-21-2456923259-…`). 다른 프로필/머신에서 온 것이고, **그것을 해석할 수 있는 환경이 존재할 수 있다.** 그래서 상속을 끊는 것이 필요했다.
- **세 검증기 확인**(배포용 아닌 fixture 1건, `.claude/keyfixture/` 미추적). 문서는 공유 TEST 벡터를 **바이트 그대로** 복사하고 **서명만 새 키로** 만들었다 — 그 문서는 첫 줄이 TEST DATA 이고 URL 이 `updates.example`, 아티팩트가 24·18·48 B 라 유출돼도 실제 업데이트를 구동할 수 없다. **공유 벡터는 건드리지 않았다**(세 런타임 합의의 기준이라 새 키로 바꾸면 안 된다).
  - **C++** `remote60_update_manifest_test .claude/keyfixture` → **82 checks / 0 failed**. `verify_ecdsa_p256_sha256` 에 **키를 주입**하는 경로다(‼️ `default_verifier()` 는 공백키라 무조건 false 이므로 쓰지 않았다). 요구 3종: 정상 통과 · 문서 변조 거부 · 다른 키 거부.
  - **Node** `update_manifest_test.js .claude/keyfixture` → **54 checks / 0 failed**. 같은 3종 + 서명 변조 거부.
  - **Kotlin** `testDebugUnitTest --tests *UpdateManifestTest*` → **11 tests / 0 failures**. 3종은 `verifiesTheSignatureWindowsAlsoVerifies` · `rejectsTampering` · `rejectsAValidSignatureFromTheWrongKey`.
- ⚠️ **Kotlin 에 vectors override 를 추가했다** — 다른 둘은 이미 있었다(C++ `argv[1]`, Node `argv[2]`). 없으면 **공유 벡터를 새 키로 덮어야만** 검사할 수 있는데, 그 벡터는 정확히 그런 일을 막으려고 있는 것이다.
- ⚠️ **그리고 그 override 로 한 첫 실행은 거짓 양성이었다.** Gradle 테스트 워커는 **데몬에서 fork** 되고, 앞서 뜬 데몬은 그때의 환경을 유지한다 — 환경변수를 걸고 그냥 돌리면 **공유 벡터(옛 키)로 통과**한다. **없는 경로를 가리켜 실패하는지 확인**하고서야 알았다. `--no-daemon` 이 필요하고, 그 함정을 코드 주석에 적었다. **통과했다는 사실만으로는 무엇을 검사했는지 알 수 없다.**
- **백업 — 하지 않았고, "완료" 로 적지 않는다.** DPAPI CurrentUser 는 **이 PC · 이 Windows 사용자 프로필 종속**이다. OS 재설치·프로필 손실·계정 삭제면 **복구 불가**이고, **같은 디스크로 파일을 복사하는 것은 독립 백업이 아니다**(프로필이 사라지면 사본도 못 푼다). 독립 복구를 원한다면 남은 사용자 동작은 둘 중 하나다: ⓐ 외부 매체에 **별도 암호로 다시 감싼** 사본을 두거나, ⓑ 이 키를 잃으면 **새 키로 다시 서명하고 재배포**한다고 정하는 것. **아직 아무 결정도 내려지지 않았다.**
  - ⚠️ **정정(#481)**: ⓑ 를 "간단히 새 키로 복구" 로 읽으면 안 된다. **이미 설치된 제품에는 옛 공개키가 박혀 있어 새 키로 서명한 manifest 를 신뢰하지 않는다.** 즉 **업데이트 경로로는 넘어갈 수 없고**, 사용자가 **손으로 설치본을 받아 다시 설치**해야 그제서야 새 키가 신뢰된다. ⓑ 는 저렴한 선택지가 아니라 **현장 재배포와 신뢰 전환**을 뜻한다.
- **하지 않은 것**: `trusted_public_key_hex()` 수정 · 재빌드 · 설치 · 서버 게시 · 실제 릴리스 manifest 발행 · APK keystore 변경 · TLS 인증서 · endpoint 선정. 라이브 PID 3종 불변.
- 변경 파일: `apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/UpdateManifestTest.kt`(vectors override + 데몬 함정 주석) · `docs/history.md`. **개인키는 저장소 밖이고, 저장소 안에 키 재료 0건**을 확인했다.

### 481) 2026-09-09 키 폴더 ACL 상속 차단 — **해석 안 되는 SID 는 "주체 없음" 이 아니다**
- **범위**(Codex 승인, 이것만): `%LOCALAPPDATA%\GNLink\ReleaseSigning\p256-a0e184579c5d4c2d\` **폴더와 그 안 파일**. 상속 차단 + **명시 3주체만**(현재 사용자 · `SYSTEM` · `BUILTIN\Administrators`).
- **왜**: 그 폴더가 `%LOCALAPPDATA%` 에서 미해결 SID 1건(`S-1-5-21-2456923259-…-2881406636`, Write+ReadAndExecute)을 상속하고 있었다. #480 에서 나는 이것을 "삭제된 계정" 이라 적었는데 **그렇게 단정할 근거가 없다** — 해석 실패는 도메인 미도달이나 **다른 머신의 계정**으로도 난다. 실제로 도메인부가 이 PC 사용자 SID 와 **다르다.** **해석 불가 ≠ 주체 없음** 이므로 끊는 것이 맞다.
- **절차**: 변경 전 **원 ACL 을 `.claude/key_acl_before.clixml` 로 보존**하고, 소유자가 현재 사용자인지 먼저 확인(아니면 중단). 실패하면 **원 ACL 로 되돌린다** — 여기서 잘못되면 DPAPI 로 보호된 개인키가 든 폴더에 **본인도 못 들어간다.**
- **결과**: `protected=True`, ACE 3개 전부 `inherited=False` — `NT AUTHORITY\SYSTEM` · `BUILTIN\Administrators` · `shotan\shotan`, 각 FullControl. **미해결 SID 사라짐.** 파일 4개는 그 폴더에서만 상속받는다(`private.ecc.dpapi` 확인: 세 주체뿐).
- **변경 후 확인 3가지**: ⓐ 현재 사용자로 **파일 4개 읽기 성공** ⓑ **DPAPI 서명 성공**(Unprotect→Import→Sign→기록된 공개키로 Verify, **변조 probe 거부**까지) ⓒ **예상 외 주체 0** — 폴더와 파일 전부.
- **건드리지 않은 것**: `%LOCALAPPDATA%` · `GNLink` · `ReleaseSigning` **상위 3개는 그대로**(여전히 `protected=False`, 그 SID 유지). 범위가 키 폴더 하나였다. **키 재생성 없음, 파일 내용 변경 없음, 삭제 없음.** 개인키 원문 출력 없음.
- **여전히 미결**: **독립 백업.** DPAPI CurrentUser 는 이 PC·이 프로필 종속이고, **ACL 을 조인다고 백업이 되는 것은 아니다** — 프로필이 사라지면 사본도 못 푼다. 사용자 결정(외부 매체+별도 암호 사본 / 분실 시 재배포) 대기.
- 변경 파일: `docs/history.md`(#480 정정 2건 + 이 항목). 제품 코드·산출물·공유 벡터 변경 0.

### 482) 2026-09-09 관측 엔드포인트 · https 전송 · 프록시 IP 오염 — **한 산술 가정이 제품을 못 쓰게 만들고 있었다**
커밋 — **작업 커밋 12개**(아래 나열. `a7cda4c` 만 대장 단독이라 엄밀히는 문서지만, 이 작업의 발견을 남긴 것이라 여기 둔다) + **문서 커밋 몇 개**. 문서 커밋 수를 여기 숫자로 박으면 **이 목록을 고치는 커밋이 다시 목록을 틀리게 만든다**(실제로 두 번 그랬다). 정확한 수는 `git rev-list --count 76fe8ac..` 로 센다. 작업 커밋: `acb3a2b`(서버 광고 + 호스트 소비) · `96c04cc`(광고값 검증 — **소수 포트가 통과하던 결함**) · `8577b10`(뷰어 경로 — ⚠️ **새 단정 0건으로 NEEDS_CHANGES 를 받은 커밋**) · `a7cda4c`(대장 1.10 등재) · `2ff2fa6`(스킴 단일화) · `a68f493`(https 전송) · `19a1f6c`(서버 409) · `c36fb75`(TLS 픽스처) · `68d3936`(Android) · `f1cd2fb`(재시도 회귀) · `887d44e`(origin 비교) · `b924286`(삼런타임 벡터) · 
문서 커밋은 `afc4799`(기록·증거 등급표) · `262328b`(운영자 설정) · `a2961c0`(TLS 종료 위치) · `1f77b87`(**이 목록이 4개 짧았던 것** + `:7878` 무효화) 이후로 이어진다.

- **뿌리**: 모든 클라이언트가 관측 UDP 포트를 **`http 포트 + 1`** 로 계산했다. TLS 뒤 443 이면 **444** 이고 거기엔 아무것도 없다 → 관측이 도착하지 않는다 → 호스트는 하트비트를 못 끝내 **목록에 아예 안 뜨고**, 뷰어는 `/api/connect` 까지 못 가서 **릴레이 주소도 못 배운다**. **그동안 모든 스위트가 초록이었다 — 테스트가 코드와 같은 가정을 하고 있었기 때문이다.**
- **서버가 말한다**: `REMOTE60_DIR_OBSERVE_PORT`(기본 `UDP_PORT`)·`REMOTE60_DIR_OBSERVE_HOST`(빈 값이면 클라가 접속한 host)를 login·host/register·`/healthz` 에 `observe:{port,host}` 로 실어 보낸다. **새 REST 경로 없음.**
- **클라가 읽는 규칙**(공유 벡터로 고정, 아래 (F)): 광고 우선 → http 는 `+1` → **https 는 기본값 없음(거부)** → 65535 는 `+1` 불가. 포트는 **`observe` 안에서만** 읽고 **1..65535 정수만** 인정한다(`29181.5`·`"29181"`·`-1`·`65536` 전부 아님).
- **광고 host 는 서버가 검증하지 않는다** — 운영자가 입력한 값 그대로 나간다. 그래서 **클라가 검증**하고, 나쁜 host 가 **좋은 포트를 같이 버리지 않게** `hostRejected` 로 기록하고 디렉터리 host 로 폴백한다.

**https 전송 (`a68f493`)**
- `parse_directory_url` 이 https 를 받아들이고 **스킴을 돌려준다**(`outSecure`). **스킴을 정하는 곳은 이제 여기 하나**이고 6개 호출자가 전부 여기서 받는다. 기본 포트도 스킴에서 나온다(80/443).
- **같은 함수 안에 대소문자 비대칭**이 있었다: https 는 무시, http 는 구분 → **`HTTP://host` 가 "unsupported url scheme"** 이었다. 올바르게 입력한 url 이 대문자 때문에 거부됐다.
- https 는 **WinHTTP**, http 는 **기존 raw socket 그대로**(현 배포 무변경). 두 전송을 **같은 서버에 붙여 계약 동등성 회귀**를 돌렸고 **실제 차이 2건**이 나왔다:
  - **응답 상한이 64KiB/256KiB/4MiB 세 개**였고 **셋 다 조용히 잘라서 돌려줬다.** 잘린 body 를 파싱하면 "malformed json" 이 나오는데 그건 **받은 것에 대해선 참이고 서버에 대해선 거짓**이다 → 상한 하나, 초과는 **실패 + 사유**.
  - **초과 실패인데 WinHTTP 는 status 에 200 을 남겼다**(socket 은 0). false 반환은 "교환이 없었다" 인데 200 이 남으면 **반환값 안 보고 status 만 보는 코드**를 부른다.
- **업데이터는 정책 비공유**: 전송만 공유하고 **http 거부는 그대로**다(가져오는 아티팩트가 관리자 권한으로 실행되므로). 회귀로 못 박았다.

**프록시 IP 오염 (`19a1f6c`)**
- 관측이 없으면 서버가 **HTTP 소켓의 주소**로 폴백했다. 그 주소는 **중간에 아무것도 없을 때만** 클라이언트의 것이다 — 프록시·로드밸런서·TLS 종단 뒤에서는 **모든 호스트가 같은 주소로 게시**되고, punch 는 듣지 않는 중간 상자로 가며, **서로 다른 계정이 구분되지 않는다.**
- 폴백을 **고치지 않고 없앴다**(`remoteIp()` 함수 삭제). 관측만이 NAT 가 실제로 매핑한 포트를 알기 때문이다. body 의 `udpPort`(클라 자기신고)도 같이 제거.
- **409**, 401 아님 — 자격증명은 멀쩡하므로 **호스트가 재등록·뷰어가 재로그인하면 안 된다.** 사유는 `observation_required` / `observation_expired`.
  - 이 둘을 구분하려면 **sweep 이 만료 즉시 지우면 안 됐다**(그러면 "안 보냈다" 와 "너무 늦었다" 가 같은 부재 항목). TTL 2배까지 보존한다.
- **거부는 어떤 쓰기보다 먼저** 일어난다: `lastSeen` 미갱신(갱신하면 **닿지 않는 주소로 online 유지**), `pendingPunch` 미소모(거부된 하트비트가 punch 를 먹으면 **제대로 한 뷰어가 고립**).
- 옛 주석은 *"configured 면 프록시 헤더를 존중한다"* 고 적혀 있었지만 **헤더를 읽는 코드가 없었다.**

**클라이언트 유한 재시도 (`19a1f6c`·`68d3936`, 회귀 `f1cd2fb`)**
- 호스트: 409 → **스트림용 같은 소켓**에서 재관측 → **하트비트 1회만** 재시도 → 실패면 상태만 남기고 25초 주기가 backoff. 뷰어·폰도 같은 모양(**409 이고 사유가 `observation*` 일 때만**).
- 회귀는 **요청 수를 센다.** "동작했다" 는 **40번 재시도한 클라이언트도 참**이고 **성공하는 길에 로그아웃한 클라이언트도 참**이기 때문이다: `/api/connect` **정확히 2회** · 계속 거부해도 **2회에서 정지** · 404 는 **1회** · `/api/login` **0회**.
- 호스트는 `HostAgent` **자체**로 돌린다: **heartbeat 2회 · register 정확히 1회**(캐시 토큰 생존). **401 대조군**이 옆에 있어야 이 단정에 뜻이 생긴다 — 없으면 `register == 1` 은 **어떤 상황에서도 재등록하지 않는 클라이언트에게도 초록**이다. 401 에서는 `register == 2` 다.
- 토큰을 버리면 **무인 PC 는 사람이 걸어가야** 복구된다. 그것이 이 단정이 지키는 것이다.

**Android (`68d3936`)**
- 🔴 `DirectoryClient` 가 **모든 409 를 "호스트가 오프라인입니다"** 로 찍고 있었다. 서버가 관측 없음에도 409 를 내게 되면서 **폰이 "PC 가 꺼져 있다" 고 거짓말하는** 상태가 됐다 — 사용자를 **고칠 수 있는 것(서버 설정)에서 떼어내 엉뚱한 기계로** 보낸다. (G) 가 만든 결함이고 **(D) 를 (G) 뒤에 둔 순서 덕에 드러났다.**
- `LogUploader` 는 url 을 **두 번째로 판정**하고 있었다(`startsWith("http")` — `https://` 도 `httpx://` 도 참이고, 그 외에는 스스로 `http://` 를 붙였다). 그 요청 헤더에는 **세션 토큰**이 실린다. 이제 `DirectoryClient.normalizedUrl()` 하나를 쓴다.
- ⚠️ 단위 테스트에 **실제 `org.json`** 을 넣었다. 흔한 처방인 `isReturnDefaultValues = true` 는 **쓰지 않았다** — 그러면 `JSONObject` 가 null·0 을 돌려주고 **파서 테스트가 아무것도 파싱하지 않은 채 초록**이 된다. APK 에는 안 들어간다.
- ⚠️ **자기 함정 하나**: `/healthz` 실패를 `catch` 로 삼켰더니 **"못 닿음" 과 "아무 말 없음" 이 같은 빈 결과**가 됐다 → https 에서 **다운된 서버를 "설정이 필요합니다" 로 보고**한다. `HealthObserve(reached, advertised, error)` 로 분리했다.
- 배선(`MainActivity`)은 테스트를 억지로 붙이는 대신 **결정을 밖으로 뺐다**(`resolveObserveTarget(url, cached, probe)`). 도달 불가한 곳에 단정을 못 넣겠으면 **도달 가능하게 만드는 것**이 답이다.
- **불가침 확인**: `UpdateFlow.kt`·`UpdateManifest.kt`·`AndroidManifest.xml`·`network_security_config.xml` **diff 0**. cleartext 확대 없음, signer 무변경.

**origin 비교 (`887d44e`)**
- 캐시된 host token 이 발급처에 속하는지를 **url 문자열 그대로** 비교했다 → `http://h:8080/` 과 `http://h:8080` 이 다른 서버, `http://h` 와 `http://h:80` 도 다른 서버 → **매번 토큰 폐기 + 재등록**. **후행 슬래시 하나가 무인 PC 의 자격증명을 날린다.**
- `directory_origin_key()`: 포트 항상 명시 · host 소문자 · **스킴은 남긴다.** 합쳤으면 **한쪽에서 발급된 토큰이 다른 origin 으로 흘러간다** — 정규화의 목적이 "같은 것을 같게" 인데 **다른 것을 같게 만들면 정반대**다.
- **저장·표시값은 사용자가 입력한 그대로** 둔다. 비교 전용이다.
- 확인만 함: **29180 하드코딩 삽입 0건**(제품 코드). 포트 출처는 url 아니면 서버 광고, 제3의 출처 없음.
- ⚠️ **인접한 미착수 항목**: `directory_client.cpp:451` 은 UDP 관측 응답의 포트를 여전히 느슨하게 읽는다(**상한 검사 없음** → `65537` 이 `1` 로 절단). 이번에 엄격 리더를 넣은 곳은 `:183` 의 **광고 metadata** 이고, 이 자리는 **고치지 않았다** — 대장 `docs/작업목록.md` **1.10**, 착수 여부는 Codex 판단.

**삼런타임 계약 (`b924286`)**
- `apps/shared/observe_endpoint_vectors.txt` — 규칙 9개 + `parse` 21행 · `port` 11행. C++·Kotlin 이 읽고 **같은 답**을 내야 한다. 양쪽 다 **읽은 행 수 하한을 단정**한다(공유 벡터 테스트의 고유 실패 형태가 **파일을 못 찾고 아무것도 안 읽은 채 초록**이라서).
- **JS 다리는 일부러 다르다.** 서버는 파싱하지 않고 **배출**하므로, 클라 둘의 일치만으로는 **둘 다 올바르게 파싱해서 둘 다 폴백**하는 경우 — 이 작업이 없애려던 바로 그 실패 — 를 못 잡는다. `observe_contract_test.js` 는 서버가 내는 문서를 규칙에 대본다: **중첩 위치** · **범위 내 정수** · 세 문서 동일값 · **감당 못 하는 포트는 아예 안 냄**.

**⚠️ TLS 증거 등급 — 한 덩어리로 "TLS 검증 완료" 라고 쓰지 않는다**

| 항목 | 증거 |
|---|---|
| 인증서 거부 | **실행 증거** — 자체서명 loopback 픽스처에 제품 전송으로 붙어 handshake 거부 확인 |
| https→http 강등 거부 | **런타임 옵션 증거** — 살아 있는 세션에서 `WinHttpQueryOption` 되읽기 `policy=1`(`DISALLOW_HTTPS_TO_HTTP`, 기본 `ALWAYS` 아님). **실경로 미실행** |
| TLS 1.2 강제 | **소스 증거만** — `WINHTTP_OPTION_SECURE_PROTOCOLS` 는 **되읽기 불가**(error 87). 런타임 증거 없음 |
| 유효 HTTPS 성공 대조 | **제품 전송으로 확인**(2026-09-09, #484) — `winhttp_transport.cpp` 를 **무변경 링크**한 read-only probe 로 `GET https://rem.shotan.net/healthz` → `sent=1 status=200 bytes=11`. 음성 대조(없는 호스트) `sent=0 status=0 (12007)`. ⚠️ **강등 실경로는 이것으로 상쇄되지 않는다** |

- 강등 거부는 **자체서명으로는 검증할 수 없다** — 신뢰되지 않으면 **handshake 가 먼저 실패**해 redirect 에 도달하지 못한다. 그 실패를 PASS 로 적으면 **정책이 있든 없든 초록인 단정**이 된다. 그래서 `SKIP` 으로 출력하고 이유를 문구에 박았다.
- 검증을 끄는 인자는 **만들지 않았다.** 그런 것은 대개 "테스트용" 인자로 열린다.

**⚠️ 하드 의존 — 서버 앱 업데이트가 필요하다 (nginx·프록시 설정과 별개)**

| 조합 | 결과 |
|---|---|
| **구서버 / 신클라** | 서버가 `observe` 를 안 낸다 → **http 는 `+1` 로 정상 동작**, **https 는 거부**(사유 명시). https 로 가려면 **서버 앱 업데이트 필수** |
| **신서버 / 구클라** | 구클라가 광고를 무시하고 `+1` → **https 에서 444 로 간다**. 즉 **클라이언트 업데이트도 하드 의존** |
| 신서버 / 신클라 | 광고대로 동작. `REMOTE60_DIR_OBSERVE_PORT` 미설정이면 서버가 **자기 UDP 포트**를 광고 |
- 즉 **https 전환은 nginx vhost 만으로 끝나지 않는다.** 서버 앱과 양쪽 클라이언트가 모두 이 버전이어야 한다.

**⚠️ 이 작업으로 무효가 된 옛 기록: `docs/history.md:7878`** — 서버 이전 기록에 *"관측 포트는 클라가 `HTTP+1`로 계산하므로 **연속이어야 함**"* 이라고 적혀 있다. **이제 서버가 광고하므로 연속일 필요가 없다.** 그 줄만 읽으면 포트를 옮길 때 둘을 불필요하게 묶어 옮기거나, *"연속이 아니면 안 되는데"* 라며 https 전환을 포기하게 된다. 옛 기록은 그때의 사실이므로 고치지 않고 **여기서 무효를 가리킨다**(대장 1.10 과 같은 방식). 같은 줄의 **포트 값 자체는 유효**하다: `8080/8081 → TCP 29180 / UDP 29181`, 릴레이 **UDP 29190**, 리스너 3개 확인까지 `:7878-7883` 에 기록돼 있다.

**운영 배치 · 리버스 프록시 · 새 환경변수**는 `docs/구현계획.md` "운영 배치 — 리버스 프록시 뒤에 둘 때" 에 적었다.
⚠️ 그 절의 수치는 **저장소 코드에서 읽은 것**이고 **라이브 NAS 설정은 확인하지 않았다**(이 작업은 NAS 무접속).

**⚠️ 마이그레이션 영향 (origin 비교)** — 후행 슬래시 차이로 **매번 재등록하던 경우는 사라진다.** 반대로 서버 주소를 **http→https 로 바꾸면 재로그인·재등록이 필요**하다. **의도된 것이다**: 다른 origin 이고, 한쪽에 발급된 토큰을 다른 쪽으로 보내지 않는다.

**실행**(RDP 종료 상태 `console` Active, `^PASS` 기준, 전체 재빌드 오류 0)
- C++: `observe_vectors` **34** · `directory_session_client` **93** · `directory_http_contract` **43** · `directory_retry` **23** · `version_compare` **139** · `winhttp_tls_posture` **8 PASS / 3 SKIP** · `updater_options` **50** · `update_http` **36** · `client_shell_bridge` **29** — 전부 exit 0 / FAIL 0
- JS 전체 **335 PASS**, exit 0 (프록시 오염 24 · observe 계약 11 포함)
- Android **61 tests / 0 failures / skipped=0** (`--no-daemon`)
- **하지 않은 것**: 실배포 · NAS · nginx · 운영 인증서 · DNS · 설치 · `dist/` 변경 · APK 생성 · push. 라이브 불변.

### 483) 2026-09-09 관측 응답 포트 검증(대장 1.10) + 조합 한계 표 정정 + **라이브 `/healthz` 실측**
- **Codex 승인 범위의 좁은 수정만.** 리팩터 없음.

**① 관측 응답의 포트 — 검사 뒤에만 상태를 갱신한다**
- `HostAgent::ConsumeUdpPacket`(`directory_client.cpp:451` 부근)이 `json_get_u32` 로 포트를 읽고 **`port == 0` 만** 막았다. 상한이 없어 **`65537` 이 `uint16_t` 캐스트로 `1`** 이 되고, 느슨한 정규식이 **`29181.5` 에서 `29181`** 을 집었다. 이 값은 **호스트의 공개 UDP 포트로 게시**되므로 틀리면 **아무도 못 닿는 호스트**가 되고, 증상은 파싱이 아니라 **네트워크 장애처럼** 보인다.
- 이제 `json_get_exact_u32` + **1..65535** + **빈 ip 거부**를 전부 통과한 뒤에만 `observedIp_`·`observedPort_`·`observedReady_` 를 만진다. **거부는 이전 관측을 그대로 두고**, 다음 프로브가 대체한다. **`fromDirectory` source 검증은 유지**.
- **⭐ 음성 대조로 결함을 재현했다**(수정을 되돌리고 실행): `65537` → **`public=1.2.3.4:1`** · `65536` → `:0` · `29181.5` → `:29181` · `99999999999999` → `:65535` · 빈 ip → `:29181`. **5건 FAIL → 수정 후 전부 PASS.** 테스트가 무엇을 잡는지 확인하지 않으면 이 회귀도 "이미 통과하는 단정" 이 됐을 것이다.
- **3 클라이언트 = 파서 2개**였다: 호스트는 위의 것, **뷰어와 Android 는 `directory_rendezvous.cpp` 의 `parse_observed` 를 공유**한다(Android `CMakeLists.txt` 가 그 파일을 그대로 컴파일한다). 후자는 **상한이 이미 있었고**(`port <= 0 || port > 65535`), **소수 절단은 있었다** → 값 끝 문자 검사를 넣었다. 누적 중 `int` 오버플로(UB)도 65535 초과 시 즉시 중단으로 막았다.
  - ⚠️ 정직하게: 뷰어 쪽 **범위 초과 회귀는 수정 전에도 통과**한다(이미 막고 있었으므로). 새로 잡는 것은 **소수 케이스뿐**이고, 오버플로 UB 는 **관측 가능한 회귀가 없다**. 표에 다 넣되 "이번 수정이 잡은 것" 으로 뭉뚱그리지 않는다.
- 회귀는 `remote60_directory_retry_test`(**44 PASS**, 34→44): 호스트 8케이스 + **65535 정상 통과** + 뷰어 2케이스.
- ⚠️ **이 스위트는 약 140초가 걸린다. 행이 아니다.** 대기는 전부 유계지만(폴링 `150 × 100ms` 후 실패) **실제 대기**다: 제품이 하트비트 간격을 **5초로 하한 강제**(`if (cfg_.heartbeatSeconds < 5) cfg_.heartbeatSeconds = 5`)해서 **테스트가 더 빠르게 만들 수단이 없고**, HostAgent 시나리오마다 한 주기를 앉아서 기다린다. 단축하려면 **간격을 주입 가능하게** 만드는 수밖에 없다(업데이트 테스트가 lock 이름을 주입하듯). 테스트 파일 머리에도 적어 뒀다 — **"왜 느리지" 하고 시나리오를 지우는 방향**으로 손대는 것을 막기 위해서다. 그 시나리오들이 세는 **요청 수**가 "한 번 고쳤다" 와 "루프로 재시도했다"·"성공하는 길에 로그아웃했다" 를 가르는 유일한 것이다.

**② 조합 한계 표 — 앞 항목(#482)의 "신서버/구클라 = 여전히 444" 는 과했다**
정정한다. 444 는 **https 일 때만** 나오고, 구클라는 **https 자체를 파서가 거부**할 수도 있다.

| 서버 | 클라 | url | metadata | 결과 |
|---|---|---|---|---|
| 구 | 구 | http | 없음 | **정상** — `+1` 이 문서화된 동작이고 서버도 그 관계로 뜬다 |
| 구 | 구 | https | 없음 | **연결 시작 못 함** — 구 파서가 https 를 "unsupported url scheme" 으로 거부한다(444 가 아니다) |
| 구 | 신 | http | 없음 | **정상** — 광고가 없으면 `+1` 기본값 |
| 구 | 신 | https | 없음 | **거부 + 사유** — "서버에 관측 포트가 설정되어 있지 않다". 444 로 보내지 않는다 |
| 신 | 구 | http | 있음 | **정상** — 구클라는 광고를 무시하지만 `+1` 이 맞는 값인 배포다 |
| 신 | 구 | http | 있음(**`+1` 이 아닌 포트**) | **닿지 않음** — 구클라가 `+1` 로 계산해 **엉뚱한 포트**로 보낸다. 서버가 관측을 못 받아 **409** 를 낸다 |
| 신 | 구 | https | 있음 | **연결 시작 못 함**(구 파서가 https 거부). 그 파서가 https 를 받는 구버전이라면 **444** |
| 신 | 신 | http/https | 있음 | **정상** |
- 요지는 그대로다: **https 전환에는 서버 앱과 양쪽 클라 업데이트가 모두 필요하다.** 다만 실패 모양은 칸마다 다르고, "항상 444" 가 아니다.

**③ ⭐ 라이브 `/healthz` 실측 — 하드 의존이 추론이 아니라 관측된 사실이 됐다**
검증용claude 가 Codex 승인 범위(인증 없는 read-only)로 **2026-09-09** 확인:
```
DNS : rem.shotan.net → CNAME server.shotan.net → 175.207.45.151
GET https://rem.shotan.net/healthz → HTTP 200  {"ok":true}
```
- **DNS·인증서·HTTPS 도달성 정상**(검증 우회 없이 통과).
- 🔴 **응답에 `observe` 가 없다.** 확정된 사실은 여기까지다: **현 엔드포인트가 신규 metadata 계약을 제공하지 않는다.**
  - ⚠️ **정정(#484)**: 처음에 이것을 *"현 배포는 구 서버"* 라고 적었는데 **단정할 근거가 없다.** **프록시가 `/healthz` 를 자체 응답할 수도 있고**, 그 응답이 backend 에서 왔다는 구성 증거가 없다. 배포 버전·실파일 대조도 하지 않았다. **원인은 미확정**이며 배포 의존성 결론은 어느 쪽이든 같다.
- 즉 **지금 신 클라이언트가 그 주소로 붙으면 설계대로 거부된다.** 문서의 하드 의존은 예측이 아니라 **현재 상태**다.
- ⚠️ **상쇄 금지 두 가지**: ⓐ 이것은 **PowerShell 로 확인한 엔드포인트 도달성**이지 **제품 WinHTTP 전송의 성공이 아니다** — "유효 HTTPS 성공 대조" 는 **여전히 미검증**이다. ⓑ 라이브 상태는 언제든 변할 수 있고, 이 줄은 **그날의 사실**이다.

### 484) 2026-09-09 업데이트 manifest URL 을 디렉터리 origin 에서 파생 + 실측 2건(정정·승격)
- Codex 확정: 이것은 **원 task(t-twp27ott, "Host·PC·APK 접속 주소를 https://rem.shotan.net 하나로 통일") 범위 항목**이라 추가 승인 없이 착수.

**① 왜 필요했나 — 주소는 있는데 아무도 잇지 않았다**
- manifest URL 이 **환경변수(Windows `REMOTE60_UPDATE_MANIFEST_URL`) / 빌드 상수(Android `BuildConfig.UPDATE_MANIFEST_URL`)** 로만 왔다. 서버에는 `GET /api/update/manifest` 라우트가 **이미 있었다.** 즉 **주소도 있고 라우트도 있는데 둘을 잇는 것이 없어서**, 서버 주소 하나만 설정한 기계는 업데이트를 **영원히 확인하지 못했다.**
- 이제 **override 가 있으면 override, 없으면 디렉터리 origin 에서 파생**한다. override 를 쓰던 배포는 **그대로**다.

**② 규칙 — 문자열 접합이 아니라 origin 에서 만든다**
- `directory_origin_key()`(C++) · `originKey()`(Kotlin)가 **스킴·host·포트**를 정하고, 거기에 `/api/update/manifest?platform=<windows|android>` 를 통째로 붙인다. 그래서 **후행 슬래시·경로·대소문자·기본 포트**가 url 한가운데로 새어 들어가지 못한다.
- **`.sig` 를 따로 받지 않는다** — 서버는 **한 응답에 `{manifest, signature}`** 를 담는다.
- ⚠️ **http 디렉터리는 아무것도 파생하지 않는다.** https 로 **추측해 올리면** 아무도 설정하지 않은 서버를 발명하는 것이고, http 로 **따라 내려가면** 관리자 권한으로 실행될 아티팩트를 **누구나 고쳐 쓸 수 있는 구간**으로 받는 것이다. http 배포는 **명시 https override** 로 말해야 한다. 그때까지는 **미설정**(실패가 아니다).

**③ 🔴 Android 는 서버가 낸 적 없는 주소를 받고 있었다**
- `UpdateFlow.check()` 가 문서를 받은 뒤 **`"$manifestUrl.sig"`** 를 따로 요청했다. 서버 라우트는 `/api/update/manifest?platform=android` 이고, 여기에 `.sig` 를 붙이면 **쿼리 값 뒤에 붙는다**(`...platform=android.sig`) — **서버가 낸 적 없는 주소**다.
- **아무도 몰랐던 이유는 그 줄이 한 번도 실행되지 않았기 때문이다**: URL 이 빈 빌드 상수라 그 위에서 `NotForUs` 로 끝났다. **파생이 그 줄을 실행 가능하게 만들었다.**
- 그래서 Windows 클라이언트와 **같은 모양**으로 고쳤다: 한 응답에서 `manifest`·`signature` 를 뽑는다. **https 전용 가드(`:79`)는 손대지 않았다.**
- ⚠️ 이것은 **불가침으로 지정됐던 `UpdateFlow.kt` 를 고친 것**이다. 고친 부분은 **fetch 모양뿐**이고 https 정책은 그대로다. 그렇게 하지 않으면 파생이 **동작하지 않는 URL 을 만들어내는** 기능이 된다.

**④ 🔴 미해결 — 파생 URL 은 지금 401 을 받는다**
- 서버 `handleUpdateManifest` 는 **세션 또는 `x-host-token`** 을 요구한다(`server.js` 첫 블록). 그런데 C++ `https_get_text` 와 Android `fetchText` 는 **둘 다 인증 헤더를 보내지 않는다.**
- 즉 **파생된 URL 로 실제로 받아오면 401** 이다. env override 로 **인증 없는 다른 서버**를 가리키던 기존 사용에는 없던 문제이고, **이번 파생으로 처음 드러났다.**
- **고치지 않았다.** 토큰을 업데이트 fetch 경로로 넣는 것은 범위가 다르고(누가 어떤 토큰을 어느 origin 에 보내는지가 걸린다), **같은 origin 이라도 결정은 따로 받아야 한다.** → **Codex 판단 요청.**

**⑤ 실측 2건**
- **정정**: #483 ③ 의 *"현 배포는 구 서버"* 를 **철회**한다. 확정된 것은 **"현 엔드포인트가 신규 metadata 계약을 제공하지 않는다"** 뿐이고, **프록시가 `/healthz` 를 자체 응답할 가능성**이 배제되지 않았다. 배포 버전 대조도 하지 않았다. 결론(하드 의존)은 어느 쪽이든 같다.
- **승격**: 검증용claude 가 `winhttp_transport.cpp` 를 **무변경 링크**한 read-only probe 로 **제품 TLS 자세**로 `GET https://rem.shotan.net/healthz` → **`sent=1 status=200 bytes=11`**, 음성 대조(없는 호스트) **`sent=0 status=0 (12007)`**. 증거 등급표의 *"유효 HTTPS 성공 대조 — 미실행"* 을 **"제품 전송으로 확인"** 으로 올렸다. ⚠️ **강등 실경로는 여전히 미검증**이고 이것으로 상쇄되지 않는다.

**⑥ 범위 대조 (원 task 문과)**
"Host·PC·APK 접속 주소를 하나로" 기준으로 훑었다. 하나의 주소에서 나오는 것: **login · hosts · connect · 관측 엔드포인트 · 로그 업로드 · (이번에) 업데이트 manifest**. 제품 코드에 **하드코딩된 운영 주소·포트는 0건**(`rem.shotan.net`·공인 IP·`29180` 모두 0). 유일한 리터럴은 `host_app_main.cpp` 의 **`--ui-preview` 전용 `http://127.0.0.1:8080`** 으로, 실제 접속 경로가 아니다. **릴레이 주소는 서버 응답에서 온다.**

**실행**(`^PASS`, RDP 종료 상태): `directory_session_client` **105**(93→105) · `directory_retry` 44 · `observe_vectors` 34 · `directory_http_contract` 43 · JS 335 — 전부 exit 0 / FAIL 0. Android **65 tests / 0 failures / skipped=0**(`DirectoryObserve` 26). 전체 재빌드 오류 0.

### 485) 2026-09-09 업데이트 fetch 계약 통일 + 자격증명 범위 — **worker IPC 는 착수 안 함**
- Codex 승인 범위. **Host/Client → elevated worker 자격증명 전달(IPC)은 승인 대기라 손대지 않았다.**

**① 🔴 C++ 소비자 둘이 서로 다른 계약을 쓰고 있었다**
서버는 **envelope 하나**를 낸다: `sendJson(res, 200, { manifest, signature })`.
- `update_check.cpp`(UI 확인) — envelope 를 읽는다 ✓
- **`updater_effects.cpp`(승격 worker) — `manifestUrl + ".sig"` 를 따로 요청** ✗ → 파생 URL 이면 **`...?platform=windows.sig`**, **서버가 낸 적 없는 주소**다.
**Android 에서 고친 것과 같은 결함이 Windows worker 에 그대로 남아 있었다.** 안 터진 이유도 같다 — `manifestUrl` 이 env 전용이라 **그 줄이 실서버를 상대로 실행된 적이 없다.**
- ⚠️ **override 의 detached 계약은 실재한다** — 정적 호스팅이면 `url` 과 `url.sig` 가 진짜 별도 파일이다. 그래서 **응답을 보고 추측하지 않는다**: 한쪽의 잘린 응답이 다른 쪽의 정상 응답처럼 보이기 때문이다. **URL 의 출처가 모양을 정한다** — **파생 = envelope**, **명시 override = detached**. worker 에는 `--manifest-envelope` 플래그로 **모양이 URL 과 함께 이동**한다.

**② 자격증명 — 스냅샷 하나, 목적지마다 판정**
- `UpdateEndpoint`(`update_endpoint.hpp`): **url · derived · credentialHeader · origin · ownerEpoch**. 값 하나가 아니라 **묶음**인 이유는, **나중에 전역에서 url 을 다시 읽어 그때 붙일지 정하는 것이 TOCTOU** 이기 때문이다 — 결정과 송신 사이에 계정도 서버 주소도 바뀔 수 있다.
- 규칙: **Host = `x-host-token`**, **PC·Android = 세션 Bearer**. **명시 override 는 무인증** — **같은 host 를 가리켜도** 그렇다. 결정하는 것은 **url 의 출처**이지 생김새가 아니다. 생김새로 정하는 규칙은 **비교**이고, **비교는 틀리는 날이 온다.**
- **목적지마다 다시 판정한다**(`credential_allowed`): manifest url 도, manifest 가 지목한 **artifact url 각각도**. **서명된 manifest 는 바이트가 맞다는 말이지, 우리 디렉터리의 자격증명이 그 아티팩트를 호스팅하는 서버에 속한다는 말이 아니다.**
- **자격증명이 붙은 요청은 redirect 를 따라가지 않는다**(WinHTTP `WINHTTP_DISABLE_REDIRECTS`, Android `instanceFollowRedirects = false`). https→http 거부만으로는 부족하다 — **`https://evil` 도 https 다.** redirect 는 "이 요청을 다른 곳에 다시 보내라" 이고, 그 다른 곳은 **응답한 쪽이 고른다.**

**③ 중복을 만들지 않으려고 파일 둘을 새로 뺐다**
`url_origin.{hpp,cpp}`(`url_origin_key`) · `update_endpoint.{hpp,cpp}`(스냅샷 · 범위 판정 · envelope 파서). **제품 트리와 업데이터 트리는 일부러 분리돼 있어** 업데이터가 `directory_client.cpp` 를 링크하지 않는다. 공유 파일이 없으면 origin 정규화가 **두 벌**이 되고, 둘은 후행 슬래시나 명시된 기본 포트에서 **다르게 판단**한다 — 이 저장소가 이미 스킴 판정에서 겪은 그 분열이다. `directory_origin_key()` 는 이제 `url_origin_key()` 로 위임한다.

**④ 🔴 회귀 한계 — 헤더를 실제로 관측하지 못했다**
요구는 *"테스트 서버 2개를 세우고 수신 헤더를 직접 확인"* 이었다. **못 했다.** 업데이트 경로는 **https 전용**이고(정책상 http 를 말하지 않는다), 헤더가 나가려면 **핸드셰이크가 성립해야** 한다 — 그러려면 **기계가 신뢰하는 인증서**가 필요하고, 그것은 강등 실경로 회귀를 막고 있는 것과 **같은 벽**이다. 자체서명으로는 헤더가 나가기 전에 끊긴다.
→ 넣은 것은 **결정 함수의 전수 회귀**(C++ 12건 · Kotlin 3건): 파생 같은 origin 허용 · **다른 host/포트/스킴 거부** · **artifact 가 다른 origin 이면 거부** · **override 는 우리 host 를 가리켜도 거부** · 세션 없음 · http 디렉터리.
⚠️ **"헤더가 실제로 안 나갔다" 는 관측하지 않았다.** 게이트가 맞다는 것과 게이트가 실제로 잠갔다는 것은 다른 사실이고, **후자는 미검증**이다.

**⑤ 🔴 여전히 401 — 그리고 이번에도 고치지 않았다**
worker 는 자격증명을 받지 못한다(IPC 미승인). 그래서 **worker 가 파생 URL 로 받아오면 401** 이다. **업데이트 실패이지 로그아웃이 아니다** — 토큰을 지우거나 재로그인을 강제하지 않는다.

**⑥ 범위 밖에서 발견해 고친 것 1건 — `updater_assembly_test` 가 빨간 상태였다**
*"not the version this binary was compiled as"* 가 FAIL. 원인은 이번 작업이 아니라 **`9ff1716` 의 0.2.105 버전 인상**이다: 픽스처 manifest 가 `0.2.105` 로 **박혀 있었고** `kProductVersion` 도 `0.2.105` 가 되면서 *"manifest 에서 온 값이지 컴파일된 값이 아니다"* 라는 단정이 **성립할 수 없게** 됐다.
→ 픽스처를 **`99.0.0`**(릴리스 번호가 될 수 없는 값)으로 바꿔 **두 값이 영원히 다르게** 했다. **박힌 리터럴이 움직이는 값을 쫓아가는 단정**은, 충돌 전까지는 **아무것도 검사하지 않으면서 초록**이다.

**실행**(`^PASS`, RDP 종료 상태, 전체 재빌드 오류 0): `directory_session_client` **129**(105→129) · `updater_scenarios` 142 · `update_effects` 199 · `update_manifest` 82 · `update_handoff` 50 · `updater_options` 50 · `updater_assembly` **41**(수정 후) · `directory_retry` 44 · `observe_vectors` 34 · `directory_http_contract` 43 · `update_check` 28 · `update_http` 36 — 전부 exit 0 / FAIL 0. **JS 335** · **Android 68 tests / 0 failures / skipped=0**.

### 486) 2026-09-09 자격증명 채널 — **원시(primitive)만. 아직 어디에도 배선돼 있지 않다**
- ⚠️ **먼저**: 이것은 **미배선**이다. **"미실기" 가 아니다.** 두 hop 중 어느 쪽도 제품 경로에서 이 코드를 부르지 않는다. 완료로 읽으면 안 된다.

**왜 파이프인가**
업데이트 라우트는 세션이나 호스트 토큰을 요구한다. 토큰을 가진 것은 **로그인한 앱**이고, manifest 를 받아오는 것은 **두 번 건너뛴 승격 worker** 다. 그 사이를 잇는 **뻔한 방법은 전부 자격증명을 어딘가에 남긴다**: **명령행**은 프로세스 목록을 볼 수 있는 것이면 다 읽고, **환경블록**은 상속되며 크래시 리포터가 덤프하고, **파일**은 누군가 지워야 하고, **로그**는 나중에 읽으라고 쓰는 것이다.

**규칙은 하나를 여러 곳에 적은 것이다 — 받는 쪽이 우리가 띄운 그 프로세스임이 확인되기 전에는 아무것도 쓰지 않는다**
- 파이프를 **자식보다 먼저** 만든다(이름만 존재하고 주인이 없는 창이 생기지 않게)
- `FILE_FLAG_FIRST_PIPE_INSTANCE` — 이름을 **선점당했으면 생성이 실패**한다(남의 엔드포인트를 조용히 넘겨받는 대신)
- `nMaxInstances = 1` — 두 번째 접속은 **내려앉을 곳이 없다**
- `PIPE_REJECT_REMOTE_CLIENTS` — 한 기계 안의 두 프로세스 사이이므로 원격 클라이언트는 **정의상** 그 둘이 아니다
- **명시 DACL**(현재 사용자·SYSTEM·Administrators). 기본 DACL 은 **그때 토큰이 뭐였냐**에 달렸고, 그건 접근 결정이 아니다
- **`GetNamedPipeClientProcessId` 를 우리가 쥔 handle 의 PID 와 대조**하고, **handle 을 교환 내내 열어 둔다** — 그래야 그 PID 가 **검사 밑에서 다른 프로세스로 재사용될 수 없다**
- 모든 대기에 **마감**이 있고, **지나면 아무것도 보내지 않는다.** *"결국엔 왔다"* 는 **그게 누구인지에 대한 증거가 아니다**
- **`1회 write` 는 전량 전달 보장이 아니다** — 보낸 바이트를 세고, **짧으면 실패**다(자격증명의 앞부분이 짧은 자격증명처럼 도착하는 것을 막는다)
- 프레임은 `[magic][version][length][payload]`, 상한 4KiB. **중간에서 끊긴 것은 짧은 자격증명이 아니라 실패**다
- 전달 뒤 버퍼 **zero 화**

**⭐ 회귀는 검사를 서술하지 않고 구동한다**(신규 `remote60_update_credential_channel_test`, **25 PASS**)
- **틀린 프로세스**: 실제로 다른 프로세스를 띄워 `expectedProcess` 로 주고, **이 테스트 프로세스가 접속**한다. 서버가 거부하고 **본 pid 와 기대 pid 를 둘 다 말하며**(`10572 != 21512`), 클라이언트는 **아무것도 못 받고**, 보내는 쪽 payload 는 **비워진 채** 돌아온다.
- **맞는 프로세스**: 같은 경로로 실제 전달·수신·ack. 이게 없으면 위 거부는 **아무에게도 아무것도 안 주는 채널**에게도 초록이다.
- **선점**: 같은 이름으로 두 번째 서버 생성이 **231(ERROR_PIPE_BUSY)** 로 실패.
- **아무도 안 옴**: 마감 400ms 에 **391ms** 만에 실패로 끝난다(행 아님).
- 프레임 8건: 헤더만 · 중간 절단 · 뒤에 붙은 바이트 · magic 불일치 · 모르는 version · 상한 초과(**할당 전 거부**) · 빈 payload.

**하다가 잡은 것 1건** — 거부 경로에서 `DisconnectNamedPipe` 를 안 하면, 클라이언트는 **끝나지 않는 read 에 앉아 있고** 그걸 기다리는 쪽은 **실패가 아니라 행**이 된다. 처음 실행이 정확히 그렇게 멈췄고, 그래서 거부·종료 양쪽에서 끊는다.

**아직 안 한 것 (다음)**
1. **hop 1** 부모 → bootstrap 배선(Host `CreateProcessW` handle · Client `ShellExecuteExW`+`SEE_MASK_NOCLOSEPROCESS` handle)
2. **hop 2** bootstrap → worker: `updater_main.cpp:150` 이 지금 **`CloseHandle(pi.hProcess)` 후 즉시 나간다** — **handle 을 쥐고** 두 번째 파이프로 넘긴 뒤 **worker ack 를 받고서야** 닫아야 한다
3. **Client 의 성격 변화** — 지금은 띄우고 빠지지만, 자격증명이 걸리면 **수신·검증까지 승인을 붙들고** 취소도 가능해야 한다
4. **owner epoch 재검증** — 숫자를 메시지에 넣는 것만으로는 검증이 안 된다. **최종 진행 직전에 부모의 현재 owner/origin 이 같은 attempt 인지 다시 보고**, 취소를 worker 에 전달
5. **자격증명 수신 전 ready 0 / quiesce 0**
6. **3프로세스 × 2파이프 회귀**, **인증 헤더 실관측**(신뢰 인증서 필요 — 여전히 막혀 있음)

### 487) 2026-09-09 자격증명 2-hop 배선 — **이제 제품 경로에서 공급·소비된다**
- #486 의 원시를 **실제로 배선**했다. 아래 사슬은 **테스트가 아니라 제품 코드**다.

**공급·소비 사슬 (요구하신 "실 production 배선 확인")**
1. `host_app_main.cpp:961` / `client_shell_main.cpp:417` — `update_endpoint_for(...)` 로 **스냅샷**을 만든다(Host `x-host-token`, Client 세션 Bearer).
2. 자격증명이 있으면 `spec.credentialPipeName = make_credential_pipe_name(...)` — **이름만**.
3. `host_app_main.cpp:1007` / `client_shell_main.cpp:454` — **자식보다 먼저** `CredentialServer::Create`.
4. `update_handoff.cpp:45` — `--credential-pipe <이름>` 을 인자에 넣는다. **자격증명은 인자에 없다.**
5. `host_app_main.cpp:1075` / `client_shell_main.cpp:498` — 기동 뒤 `Serve(pi.hProcess | info.hProcess, ...)`.
6. `updater_main.cpp:247` — bootstrap 이 **가장 먼저** `receive_credential`. 못 받으면 **exit 5**, 아무것도 하지 않는다.
7. `updater_main.cpp:112·201` — bootstrap 이 **자기 파이프**를 새로 만들고, 복사본 명령행에 그 이름을 넣고, **`pi.hProcess` 를 쥔 채** `Serve` 한 뒤 **ack 를 받고서야** 닫는다.
8. `updater_main.cpp:289` — worker 가 `UpdateEndpoint` 를 구성해 `production_updater_deps(log_line, endpoint)` 에 넘긴다.
9. `updater_effects.cpp:236·243` — `fetchText`·`fetchFile` 이 **요청마다** `credential_allowed(endpoint, url)` 로 판정해 붙이거나 붙이지 않는다.

**지적된 구멍 2개, 둘 다 실재했다**
- **부모가 쥔 handle 은 bootstrap 이다.** `updater_main.cpp:150` 이 `CloseHandle(pi.hProcess)` 후 즉시 나갔다 → **hop 2 를 만들지 않으면 worker 는 검사할 수 있는 상대가 없다.** 이제 bootstrap 이 handle 을 **교환 내내 보유**한다. 닫아버리면 그 PID 는 **아무것에도 고정돼 있지 않고**, 그게 바로 이 검사가 막으려는 상태다.
- **Client 는 `readyEventName` 이 없다**(`client_shell_main.cpp:428` 이 **일부러 `clear()`**). 그래서 파이프 이름을 **ready 이벤트에서 파생시키지 않고** 별도 체계(`make_credential_pipe_name`)로 만들었다. **부모가 기다리든 말든 자격증명 채널에는 신원이 필요하다.**
- ⚠️ 그리고 이것은 **Client 의 성격을 바꾼다**: 지금까지는 승격 프로세스를 띄우고 **바로 빠졌다.** 이제는 **수신·검증이 끝날 때까지 붙들고 있다.** 붙들기가 **행이 되지 않게 하는 것은 마감**(120초 — 승격 프롬프트가 뜰 시간은 되고, 무한은 아니다)이다.

**owner epoch — 숫자를 넣는 것으로는 검증이 안 된다는 지적 그대로**
전송 **직전에 스냅샷을 다시 만들어** url·origin·자격증명 유무를 **그때의 상태와 대조**한다(`host_app_main.cpp:1060`, `client_shell_main.cpp:479`). 사용자가 승격 프롬프트에 답하는 사이에 **로그아웃하거나 서버를 바꿨으면** 그 attempt 는 주인이 없고, **자격증명을 보내지 않는다.** 보내지 않으면 worker 는 fetch 에서 실패하고 멈춘다 — **주인이 사라진 갱신을 대신 설치하지 않는다.**

**자격증명 수신 전에는 아무것도 일어나지 않는다**
`receive_credential` 실패는 `effects.run()` **이전**에 `return 5` 다. 그래서 **ready 를 신호하지 않고**, 제품을 멈추지도 않는다.

**회귀**
- `updater_options_test` **55**(+5): `--credential-pipe` 파싱 · 없을 때 빈 값 · `--manifest-envelope` 동반.
- `update_handoff_test` **54**(+4): 인자에 **파이프 이름이 있고** · 모양 플래그가 있고 · **fixture 토큰 문자열이 인자 어디에도 없고** · 자격증명 없는 기동은 **채널을 언급조차 하지 않는다.**
- `update_credential_channel_test` 25(#486).

**🔴 아직 없는 것 — 완료로 읽지 말 것**
1. **3프로세스 × 2파이프 실회귀**(hop1/hop2 PID 불일치 · 선점 · 부분 프레임 · 양쪽 timeout · bootstrap 조기 사망 · late ack · 실패 시 제품 정지 0). 원시 수준에서는 구동했지만 **세 프로세스를 실제로 세운 것은 아니다.**
2. **인증 헤더 실관측** — 신뢰 인증서가 필요해 **여전히 막혀 있다**(강등 실경로와 같은 벽).
3. **fixture 토큰이 env·로그·파일에 없는지의 검사** — 인자에 대해서만 했다.

### 488) 2026-09-09 두 hop 을 실제 프로세스로 구동 — **그 자리에서 제품 결함 하나가 나왔다**
- #487 의 사슬을 **세 프로세스 · 두 파이프**로 돌렸다(신규 `remote60_update_credential_hops_test` **17 PASS**, 헬퍼 `remote60_cred_hop_helper`).

**🔴 이 테스트가 아니면 못 잡았을 것 — `receive_credential` 에 읽기 마감이 없었다**
접속에는 마감이 있었는데 **읽기에는 없었다.** 서버가 파이프를 만들어 두고 **아무것도 쓰지 않으면** 받는 쪽은 **영원히 블록**한다. 프로세스 안에서 도는 테스트로는 안 보인다 — 같은 프로세스라 상대가 늘 무언가를 했기 때문이다. 실제로 **bootstrap 이 실패하지 않고 매달렸고**, 테스트의 프로세스 대기가 타임아웃으로 끝났다.
→ 받는 쪽도 **overlapped + 같은 마감**으로 바꿨다. 제품에서 이것은 **업데이터가 멈추는** 버그였다. *"대기를 도입할 때 반드시 같이 와야 하는 것이 마감"* 이라는 지적이 **받는 쪽에도** 적용된다는 것을 놓쳤던 자리다.

**구동한 것**
- **사슬이 실제로 돈다**: hop1 전달 → bootstrap 수신 → **자기 파이프 생성** → worker 기동 → **handle 을 쥔 채** hop2 전달 → worker 가 **온전한 값** 수신 → bootstrap **exit 0**.
- **bootstrap 이 자격증명을 들고 죽는 경우**: hop1 은 성공으로 보고되지만 **worker 에는 아무것도 안 갔고**, bootstrap **자기 exit 코드**가 그렇게 말한다. 부모 쪽에서는 이 둘을 구분할 수 없다 — 그래서 **불평이 없는 것**이 아니라 **코드**가 말해야 한다.
- **받는 쪽이 ack 를 안 하는 경우**: hop2 **실패(exit 9)**, **마감 안에서**(3.0초).
- **부모가 끝내 보내지 않는 경우**: bootstrap 이 **포기(exit 5)**하고 **worker 를 아예 시작하지 않는다.**
- **누출 검사**: 각 프로세스가 **자기 명령행과 환경블록을 스스로 기록**하고, 테스트는 **worker 의 수신 보고 줄 말고는 어디에도 토큰이 없음**을 단정한다. (요구 3건 중 **env·명령행** 부분.)

**⚠️ 이 테스트가 덮지 않는 것**
헬퍼는 `updater_main` 과 **같은 순서를 수행하지만 `updater_main` 이 아니다.** 실제 업데이터를 테스트에서 돌리면 **관리자 전용 디렉터리에 자기를 복사하고 제품 프로세스를 멈춘다** — 테스트가 할 일이 아니다. 따라서 **채널은 프로세스 간으로 검증됐고, 업데이터의 순서(exit 코드·작업 디렉터리 판정·자격증명 없이 ready 안 함)는 코드 리뷰 수준**이다. 헬퍼 머리에도 같은 문장을 적었다.

**⚠️ 그리고 처음엔 초록이었다가 전체 스위트에서 빨개졌다**
단독 실행은 통과하고 **62개 전체 실행에서 1건 실패**했다. 원인은 **세 프로세스가 한 파일에 append** 한 것 — 지면 **줄이 반만 써지고**, 그건 **없는 줄로 읽힌다.** **부하에서만 실패하는 테스트**는 읽히는 대신 *"flaky"* 로 불린다. **프로세스마다 파일을 나눴고**, 이후 3회 연속 17 PASS.

**전체 스위트 (62개 실행 파일, `^PASS` 합계 1678)**
실패 3건 → **2건은 환경**(`gdi_capture_process`·`udp_control_e2e`, 검증용 실측도 동일: 화면이 실제로 갱신되지 않는 조건에서 캡처가 프레임을 못 만든다. **CLAUDE.md 규칙대로 판정 근거로 쓰지 않는다**), **1건이 위 경합**이고 고쳤다. 나머지 59개 exit 0.

**여전히 없는 것**
- **인증 헤더 실관측** — 신뢰 인증서 벽, 그대로.
- ~~**토큰이 로그 파일에 없는지**~~ → **해소(#488 후속)**: 헬퍼가 `updater_main` 과 **같은 모양의 로그 파일**(타임스탬프 + 같은 오류 문자열)을 디스크에 쓰고, 테스트가 **그 파일을 읽어** 토큰이 없음을 단정한다 — **성공 경로와 실패 경로 양쪽**. 로그는 **사실만 적고 값은 적지 않는다**(`credential received`). ⚠️ 다만 이것도 **헬퍼의 로그이지 실제 `updater.log` 는 아니다**: 그 파일을 만들려면 실제 업데이터를 돌려야 하고, 그건 관리자 디렉터리 복사와 제품 정지를 뜻한다.
- ⚠️ **읽기 마감을 넣으며 부수적으로 wrap 안전해졌다**: `remaining()` 이 `GetTickCount() - began` 의 **부호 없는 뺄셈**이라 49.7일 경계에서 깨지지 않는다. 옛 `GetTickCount() > deadline` 비교는 깨졌을 자리이고, 이 저장소가 `update_handoff.cpp:68` 에서 **이미 배운 형태가 다른 파일에서 또 필요했던** 것이다.

### 489) 2026-09-09 Codex NEEDS_CHANGES 4건 — **"각 조각이 맞다" 와 "사슬이 돈다" 의 차이가 다시 나왔다**
검증용도 이 네 건을 놓쳤고, 그 이유를 스스로 적었다: **부모의 스냅샷 구성과 채널 원시는 봤지만, 선을 타고 무엇이 건너가고 받은 쪽이 그것으로 무엇을 하는지는 안 봤다.**

**① 소유자 검증이 아니라 URL 비교였다**
조건이 `url 같음 && origin 같음 && 토큰 비어있지 않음` 이었다. 이것은 **서버가 바뀌었는가**를 묻는다. **같은 서버에서 로그아웃 → 재로그인**하면 셋 다 성립하고 **토큰만 다른 것**이 된다 → **A 가 승인한 업데이트에 B(또는 재로그인한 A)의 토큰**이 간다.
→ `UpdateEndpoint` 에 **`ownerKey` + `ownerEpoch`** 를 넣고 `same_owner_as()` 로 비교한다. **둘 다 필요하다**: 키만으로는 같은 계정의 재로그인을 못 보고, epoch 만으로는 우연히 맞는 다른 계정을 못 본다. Host 는 `accountId|machineId` + **재등록마다 증가하는 epoch**(재등록은 **새 토큰**을 발급한다), Client 는 `accountId` + **로그인마다 증가하는 epoch**.

**② descriptor 가 프레임에 없었다 — 가장 무거운 것**
자격증명은 **누가 듣고 있는지 증명한 채널**로 오는데, 그것이 묶일 **origin 은 `argv` 의 `--manifest-url` 에서 다시 만들어졌다**. argv 는 아무것도 증명하지 않는다 — **다른 `--manifest-url` 로 worker 를 띄울 수 있는 것은 진짜 자격증명을 다른 곳으로 겨눌 수 있었다.**
→ **url · origin · owner · epoch · wire 모드 · 자격증명이 같은 프레임**으로 간다(`encode/decode_update_descriptor`, 줄 단위 `key=value`). worker 는 **프레임과 argv 를 서로 대조**하고 **어긋나면 거부**한다(`updater_main.cpp` exit 6). **모르는 키는 무시하지 않고 거부**한다 — 두 끝이 이 프레임이 무엇인지에 대해 다르게 알고 있다는 뜻이고, 어느 부분이 아직 유효한지 **추측하는 것이 그 불일치를 묻는 방법**이다. 값에 개행이 있으면 **필드를 위조**할 수 있으므로 **인코딩 자체를 거부**한다.

**③ 최종 ready 앞에 소유자 검증이 없었다**
`await_handoff` 는 owner 검증도 취소도 없이 ack 했다. **"기동 시점에 주인이 맞았다" 와 "지금 주인이 맞다" 는 다른 문장**이고, 다운로드는 몇 분이 걸린다.
→ `await_handoff` 에 **`ownerStillValid` 를 ack 직전에 한 번** 묻는다. 거짓이면 **ack 를 보류**하고, ack 없는 업데이터는 **아무것도 멈추지 않는다.** **ack 이후에는 제품을 멈추고 파일을 바꾸므로, 여기가 마지막으로 막을 수 있는 지점이다.**
⚠️ **Client 의 성격이 또 바뀐다**: `readyEventName` 을 **일부러 비우던 것**을, **자격증명이 걸린 경우에만** 만들어 **핸드셰이크에 참여**시킨다. 보류할 것이 없으면 기다릴 것도 없다는 기존 논리는 자격증명이 없을 때만 성립한다.

**④ 120초 동기 대기가 UI 를 막았다**
`Serve(…, 120000, …)` 가 **page-message 핸들러 안에 인라인**이었다. **overlapped 여도 그 자리에서 기다리면 창이 최대 2분 멈춘다.**
→ 별도 스레드로 옮기고 `hProcess`·파이프를 **그 스레드가 소유**한다(핸들이 PID 를 고정하므로 먼저 닫으면 안 된다). 그리고 **자격증명 실패인데 "업데이트를 시작했습니다" 가 나가던 것**을 **명확한 실패**로 나눴다 — 업데이터는 돌지만 아무것도 설치하지 않으므로, 시작했다고 말하면 **오지 않을 결과를 기다리게 된다.**

**회귀**
- `directory_session_client_test` **147**(129→147): **재로그인은 다른 주인**(url·origin 동일·새 토큰 비어있지 않음을 각각 단정) · 다른 계정 · 같은 주인 · descriptor 왕복 · **모르는 키 거부** · 버전/필수 필드 누락 거부 · **개행 값은 인코딩 거부**.
- `update_handoff_process_test` **36**(+3): **실제 프로세스**로, 다운로드가 검증된 상태에서 **주인이 바뀌면 ack 를 보류**하고 **worker 가 stand down** 한다.

**후속 — Codex 가 지정한 3프로세스 회귀** (`update_credential_hops_test` **21 → 40**)
소유자 판정이 **단위 수준에만** 있던 것을 **실제 프로세스로** 돌렸다. 두 결말이 밖에서 보기에 완전히 다르다는 것이 요점이다: 한쪽은 **worker 가 자격증명을 들고 끝나고**, 다른 쪽은 **bootstrap 이 포기하고 worker 가 아예 시작되지 않는다.**
- **unchanged** → 전달, worker 수신, bootstrap exit 0
- **같은 계정 로그아웃→재로그인** · **같은 서버 다른 계정** · **완전 로그아웃** · **서버 변경** → **전송 0 · bootstrap exit 5 · worker 미시작**
- **descriptor URL 불일치** → worker 가 **거부**(`worker-url-mismatch=1`), **수신으로 취급하지 않음**
- **일치하는 프레임** → 수신되고, worker 가 **소유자를 프레임에서 읽는다**(`worker-owner=alice/7`) — argv 가 아니라
비교 규칙은 제품의 `update_endpoint_for` 를 그대로 불러 쓴다. 테스트가 자기 규칙을 만들면 **제품이 무엇을 하는지가 아니라 테스트가 무엇을 믿는지**를 검사하게 된다.

**🔴 여전히 없는 것 — ④ 의 "실제 UI 응답 경계 검사". 두 문장을 나눠 적는다.**
- **확인됨(코드)**: `Serve` 는 **양쪽 다** 인라인이 아니고, `hProcess`·파이프·핸드셰이크 이벤트를 **소유한 별도 스레드**에서 돈다 — Client `client_shell_main.cpp:523`, **Host `host_app_main.cpp:1118`(스레드 `:1096` 안, #490 에서 옮김)**. ⚠️ 처음엔 **Client 만** 그랬고 #489 시점의 이 줄은 **Host 에 대해 거짓**이었다.
- **미검증**: **그 사이 UI 가 응답한다.** 단정하려면 셸의 창과 메시지 루프를 테스트가 돌려야 하고, 그 수단이 없다.
⚠️ 이 둘을 한 문장으로 합치면 안 된다 — *"인라인이 아니다"* 는 *"막히지 않는다"* 가 아니다. 억지로 넣을 수 있는 단정(*"스레드를 띄우면 호출이 즉시 반환한다"*)은 **`std::thread` 를 시험하는 것**이고, 초록이면서 제품에 대해 아무 말도 하지 않는다. **셸을 구동하는 테스트 수단은 별건**이다.

**전체 스위트**: 62개 고유 실행 파일 · **`^PASS` 1707**(이후 회귀 추가로 1726) · 실패 2건은 그대로 환경(`gdi_capture_process`·`udp_control_e2e`).
⚠️ **GDI/e2e 표기 정정**(검증용 지적): *"파일 겹침 0"* 은 **무관을 시사할 뿐 원인을 증명하지 않는다.** → **원인 미확정**으로 둔다.

### 490) 2026-09-09 Codex NEEDS_CHANGES 2차 — **한 곳을 고치고 부류 전체를 고쳤다고 말한 것**
검증용도 `client_shell_main.cpp` 의 스레드만 보고 *"Host/Client 전부 이동"* 이라고 적었다. **Host 는 확인하지 않았다.** 세 건 다 실재했다.

**① Host 는 여전히 UI 스레드에서 최대 2분 기다리고 있었다**
`Serve(pi.hProcess, payload, 120000, …)` 가 **스레드 생성보다 앞**에 있었다. Client 만 옮겨졌고 **Host 는 메뉴 클릭 → 핸들러 안에서 그대로 대기**했다.
→ Host 도 같은 모양으로 옮겼다: **소유 핸들·파이프를 그 스레드가 쥐고**(핸들이 PID 를 고정하므로 먼저 닫으면 안 된다) 자격증명 전달·최종 대기를 전부 그 안에서 한다.
⚠️ 회귀는 **스레드 생성을 시험하지 않는다** — 그건 `std::thread` 를 시험하는 것이다. 대신 **소유 핸들·파이프가 실제로 worker 에 도달하는 경로**를 3프로세스 러너가 이미 돌고 있다.

**② 🔴 데이터 레이스였다 — 낡은 값이 아니라 UB**
`ownerStillValid` 람다가 **백그라운드 스레드에서** `g.cache.directoryUrl`·`hostToken`·`accountId`·`machineId`(전부 `std::string`)와 **`g.ownerEpoch`(atomic 아님)** 를 직접 읽었다. UI 스레드의 sign-in·sign-out 이 **같은 것들을 씁니다.** `std::string` 은 **다른 스레드가 대입하는 동안 읽어도 되는 물건이 아니다.**
→ `AppState` 에 **`ownerMu`** 를 두고, **`current_update_endpoint()`** 가 **한 번의 잠금으로 스냅샷**을 만든다. 호출 지점(확인·기동·전송·최종 ack) 전부 그것을 쓴다. **따로 읽으면 다섯 값이 sign-out 을 사이에 두고 걸쳐** **어느 순간에도 존재하지 않았던 주인**을 만든다.
Client 도 같은 이유로 세 값을 **한 잠금**에서 읽는다.

**③ 🔴 로그아웃해도 ack 이 나갔다**
`++g.ownerEpoch` 가 **등록 한 곳에만** 있었다. `sign_out()` 은 토큰만 지우고 **epoch 을 올리지 않았다** → `keepAccount=true` 면 `accountId|machineId` 도 그대로라 **`same_owner_as()` 가 참**이다. 게다가 최종 조건에 **자격증명 존재 검사가 없어** 토큰이 비어도 통과했다. **로그아웃한 뒤에도 기존 attempt 가 ack 을 받는다.**
→ **sign-out 에서도 epoch 을 올리고**(Host·Client 양쪽), 최종 조건에 **`!credentialHeader.empty()`** 를 넣었다. 두 가지를 다 넣는 이유: epoch 이 어떤 이유로든 움직이지 않는 빌드에서도 **자격증명이 없으면 승인되지 않아야** 한다.

**회귀 — 콜백 스텁이 아니라 제품 상태 전이로**
`update_handoff_process_test` **36 → 40**. 술어를 `[]{return false;}` 가 아니라 **제품의 `update_endpoint_for` 로 만든 두 상태의 비교**로 바꿨다. 스텁은 **배선은 증명해도 실제 sign-out 이 다른 주인으로 읽히는지는 아무 말도 하지 않는다.**
- **sign-out 중**(토큰 비고 epoch 이동) → **ack 보류**, worker **stand down**
- **재로그인 중**(같은 계정·같은 기계, **새 토큰**·epoch 이동) → **ack 보류**. *"...even though it has a perfectly good token"* 을 같이 단정한다 — url·origin 이 같으므로 **소유자 비교만이 거부할 수 있다.**
- **불변** → **ack 됨**, worker 가 **진행 허가를 받음**. 이 양성 대조가 없으면 위 두 줄은 **아무것도 승인하지 않는 빌드**에게도 초록이다.

**전체 스위트**: 62개 · **`^PASS` 1730** · 실패 **1건**(`udp_control_e2e`).
⚠️ 이번 실행에서 **`gdi_capture_process` 는 통과**했다(`rc=0`). 같은 코드에서 결과가 갈렸다는 것은 **환경 의존이라는 가설을 지지**하지만, 여전히 **원인을 증명하지는 않는다.**
- **관측 사실로 남긴다**: **두 번 중 한 번 통과**(#489 실행에서 `rc=1`, #490 실행에서 `rc=0`), 코드는 그 사이 이 테스트와 무관하게만 바뀌었다. 나중에 원인을 좁힐 때 **"항상 실패" 가 아니라는 것**이 출발점이 된다.

### 491) 2026-09-09 0.2.106 / APK 0.2.14 후보 — **후보 생성만. 실서버 전환·운영 업데이트 승인 아님**
- Codex 승인 범위: **후보 1회 생성**. **운영키 내장 · 실서명 · 서버 게시 · 설치 · push 는 여전히 미승인**이고 하지 않았다.

**버전** — Windows **0.2.106**(`product_version.hpp:12`), APK **0.2.14 / versionCode 13**(`build.gradle.kts:14-15`). 배포본(0.2.104 · 0.2.12/vc11)과도, **이미 있던 후보**(0.2.105 · 0.2.13/vc12)와도 겹치지 않는다. 0.2.105 후보 이후 작업이 많이 들어갔으므로 그 번호를 재사용하지 않는다.
- ⚠️ 지난번 버전 인상은 `updater_assembly_test` 를 빨갛게 만들었다(픽스처가 같은 번호로 박혀 있었다). **이번엔 안 걸린다** — #485 에서 픽스처를 `99.0.0` 으로 바꿔 **버전 인상과 무관**해졌기 때문이다.

**산출물** (빌드 트리 = **`e32b656`**, 즉 버전 인상이 들어간 커밋)
- ⚠️ **정정**: 처음에 빌드 commit 을 **`6973354`** 로 적었는데 **그 커밋에서는 이 바이너리가 나올 수 없다.** 그 트리의 `product_version.hpp` 는 **`0.2.105`** 이고, 설치기 안에는 **`0.2.106` 이 UTF-16 으로 3회, `0.2.105` 는 0회** 있다. 문자열로 출처를 **주장**한 것이 아니라, **컴파일된 상수가 다르므로 그 커밋일 수 없다**는 **배제** 논거다. 릴리스 후보의 출처 표기가 틀리면 **나중에 어느 코드인지 못 찾는다.**
| 파일 | 크기 | SHA256 |
|---|---|---|
| `dist/GNLinkSetup-0.2.106.exe` | 4,339,712 | `9ebfc62fdc69d0de8d956c75a5468a1a943d9f5edd330a44b4bae49f76eeb173` |
| `dist/GNLink-0.2.14.apk` | 11,518,126 | `2223f3b00370a105a94c86b0d71b4920b870f44091766ea72509a014763d1aa9` |

**설치기 payload — 이름이 아니라 바이트로 확인했다**
`RT_RCDATA` **리소스를 실제로 읽어** SHA256 을 내고, 빌드가 만든 payload 파일과 대조했다(`.claude/verify_installer.ps1`, 미추적). **파일명 문자열이 바이너리에 있다는 것은 증거가 아니다** — 이름은 남고 바이트는 옛 것일 수 있다.
**9/9 일치**: `GNLinkHost.exe`(200) · `GNLinkStream.exe`(201) · `GNLinkInputService.exe`(202) · `GNLinkCapture.exe`(203) · `GNLinkClient.exe`(204) · `GNLinkViewer.exe`(205) · `shell.html`(206) · `macro.html`(207) · `GNLinkUpdater.exe`(208).
- ⚠️ 처음 4개가 FAIL 이었는데 **제품이 아니라 내 매핑이 틀린 것**이었다(`GNLinkSecureInput.exe`·`GNLinkGdiWorker.exe`·`client_ui.html`·`macro_ui.html` 로 짐작해 적었다). 실제 이름은 위와 같다. **짐작한 이름으로 대조하면 제품의 결함이 아니라 내 목록의 결함을 보고하게 된다.**

**APK** — **signer 동일**(v2 인증서 SHA-256 `dcc806ae…2990`, 0.2.13 과 같은 값) · **package `com.remote60.androiddirect` 동일** · **versionCode 12 → 13** · `minSdk 28` · `targetSdk 34`.

**⚠️ 승인 선형화 지점과 그 한계**(문서화만, 동기화 리팩터링 없음)
- **승인이 선형화되는 시점은 `SetEvent(ack)` 이다.** 그 호출이 성공한 뒤부터 업데이터는 제품을 멈추고 파일을 바꿔도 되는 상태가 된다.
- `ownerStillValid()` 는 그 **직전**에 한 번 평가된다. 따라서 **평가와 `SetEvent` 사이의 짧은 구간은 원자적이지 않다** — 그 사이에 로그아웃이 일어나면 **이미 나간 승인은 취소되지 않는다.**
- **그 구간 이후의 취소 수단은 없다.** 되돌리려면 업데이트가 스스로 롤백하는 경로(별개 설계)에 의존한다.
- 이 창을 없애려면 **승인과 소유자 판정을 한 임계구역**에 넣어야 하는데, 그것은 **범용 동기화 재설계**라 이번 범위가 아니다. **창의 존재를 적어 두는 것이 지금 할 수 있는 정직한 처리다.**

**⚠️ 미검증 표기 정정 — "현장에서만 가능" 이 아니라 "이번 검사에서 미실행"**
| 항목 | 상태 |
|---|---|
| TLS 1.2 강제 | **구성으로 강제함(확인)** / **런타임 되읽기 불가**(옵션이 set 전용) |
| 유효 HTTPS 성공 | **확인** — 제품 WinHTTP 로 `GET /healthz` → 200 |
| https→http 강등 거부 | **미관측** — 런타임 옵션 값은 확인, **실경로 미실행**(신뢰 인증서 필요) |
| 인증 헤더 실관측 | **미실행** — 같은 벽 |
| UI 실반응 | **미실행** — 수단 없음(수동 미검증 허용) |
| 실제 `updater_main` 로그·진입점 | **미실행**. ⚠️ **헬퍼 로그는 production 로그가 아니다** |
| 운영 NAS | **전부 미확인이 아니다**: **TLS GET 성공 + metadata 미제공까지 확인**, **backend 버전·구성은 미확인** |

**전체 스위트**: 62개 · **`^PASS` 1730** · 실패 1건(`udp_control_e2e`). `gdi_capture_process` 는 **두 번 중 한 번 통과**(관측 사실, 원인 미확정).
**하지 않은 것**: 운영 서명키 내장 · 실서명 · 서버 게시 · 설치 · push · NAS 변경. 사용자가 NAS 작업 중이라 **겹치는 변경 없음.**

### 492) 2026-09-10 운영 공개키 내장 + 테스트 쌍(0.2.107↔0.2.108 / 0.2.15↔0.2.16) + NAS 인계서
- 사용자 승인 3건(NAS 서버앱 갱신 · 앱 공개키 반영 · 서명 업데이트 게시) 중 **이 PC 가 할 수 있는 것만**. **NAS 무접속.**

**① 공개키 — 두 곳에 raw X‖Y 로**
`trusted_public_key_hex()` 와 Android `UPDATE_PUBLIC_KEY_HEX` 에 **128자 소문자**. **SPKI 아님** — SPKI 를 넣으면 `decode_hex` 는 통과하고 **길이 검사에서 전부 거부**되어, **에러 없이 아무 업데이트도 안 되는 빌드**가 된다. 양쪽 테스트가 **길이·문자집합·`!= 182`** 를 단정한다.
- 🔴 **Android 에 "신뢰 키" 가 둘이었다.** `UpdateManifest.trustedPublicKeyHex()` 는 하드코딩 `""`, 실제 검증에 쓰이는 값은 `BuildConfig.UPDATE_PUBLIC_KEY_HEX`. **테스트가 묻는 것과 제품이 쓰는 것이 달랐다.** 키를 넣자 둘이 어긋나 드러났다 → 전자가 후자에 **위임**한다.
- 두 스위트의 *"키가 비어 있다"* 단정은 **임시 상태에 대한 문장**이었다. 그대로 두면 **제품이 업데이트를 확인할 수 없다는 단정**이 된다 → **반대편에서 같은 안전 성질**로 바꿨다: **다른 키로 서명된 문서를 거부**한다(공유 벡터가 정확히 그런 문서다).
- C++ 스위트는 **어느 픽스처인지 스스로 판별**한다: 공유 TEST 벡터면 **거부**를, 릴리스 키 픽스처면 **수락 + 1바이트 변조 거부**를 요구한다. **거부만 단정하면 "이 키로 무언가를 검증할 수 있는가" 는 미검증**이고, **전부 거부하는 검증기가 거부 테스트를 완벽히 통과**한다.

**② 테스트 쌍 — 왜 둘인가**
0.2.106 을 그냥 게시해도 사용자는 인앱 업데이트를 시험할 수 없다. **거기엔 키가 없다.** 그래서 **손으로 설치할 baseline**(0.2.107 / 0.2.15·vc14)과 **서버가 배포할 target**(0.2.108 / 0.2.16·vc15)을 만들었다.
⚠️ **두 빌드의 차이는 버전 값뿐이다** — 그리고 그것을 **확인했다**: 두 APK 를 항목 단위로 비교하면 **내용이 다른 항목은 `AndroidManifest.xml` 과 `classes3.dex` 둘뿐**이고(둘 다 버전 상수를 담는다) 나머지 118개는 **CRC 까지 같다**. ⚠️ 다만 **APK 파일 크기는 다르다**(11,518,126 vs 11,846,438) — 압축 방식도 항목별 원본 크기도 같으므로 차이는 **내용이 아니라 아카이브 압축·배치**다. 처음에 두 크기를 **같은 값으로 적었는데** 그것은 앞선 빌드의 숫자를 옮긴 것이었다. **산출물 표의 숫자는 파일에서 다시 읽어야 한다.** target 은 커밋 **`47a63b9`** 그대로, baseline 은 **같은 커밋에서 상수만 낮춰** 빌드하고 **커밋하지 않았다**(트리는 되돌렸다).

**③ 서명과 검증**
기존 키(`p256-a0e184579c5d4c2d`)로 **실서명**. 개인키는 DPAPI 블롭에서 메모리로만 풀고 **평문 배열을 종료 전에 zero 화**했다. 서명 직후 **공개키로 자체 검증**한다 — 검증할 수 없는 서명을 넘기지 않기 위해서다.
- **C++ 정식 `default_verifier()`** 로 `windows.manifest` → **Ok**, 1바이트 변조 → **SignatureInvalid**. **키 주입 경로가 아니라 제품이 실제로 쓰는 경로다.**
- **Node(서버가 게시 전 자체 검증에 쓰는 것)** 로 windows·android 둘 다 **Ok**, 변조 **SignatureInvalid**.
- ⚠️ **Kotlin 으로 실제 android.manifest 를 통과시켜 보지는 않았다** — Kotlin 테스트가 `platform=windows` 로 고정돼 있어 `WrongPlatform` 이 난다(서명 문제 아님). **기록으로 남긴다.**

**④ 🔴 인계서에 들어간 발견 — 서버는 아티팩트를 서빙하지 않는다**
`server.js` 라우트는 **9개**뿐이고 **아티팩트 경로가 없다.** manifest 의 `artifact=…|url` 이 가리킬 파일은 **nginx 정적 경로**로 따로 게시해야 한다. 인계서에 **URL·크기·SHA256 표**로 적었다. 경로를 바꾸려면 **manifest 를 다시 만들고 다시 서명**해야 한다 — **URL 이 서명 대상 안에 있다.**
- 서버 env 델타: `REMOTE60_UPDATE_DIR` · **`REMOTE60_UPDATE_PUBLIC_KEY`**(없으면 **게시 전 자체 검증이 없다**) · `REMOTE60_DIR_OBSERVE_PORT` · (선택) `_HOST`.
- **구버전 호환**: `OBSERVE_PORT` 를 **`http+1`(현 배포 29181)** 로 두면 구버전도 그대로 동작한다. 다른 포트로 옮기려면 **모든 클라이언트가 이번 빌드 이상**이어야 한다.
- **배포 순서**: 백업 → **아티팩트 전량 검증** → 서버앱·env → **manifest 마지막**. 뒤집으면 **없는 파일을 가리키는 manifest** 가 잠깐 유효해진다.

**산출물**(전부 `dist/`, 미추적)
| 파일 | 크기 | SHA256 |
|---|---|---|
| `GNLinkSetup-0.2.107.exe` | 4,339,712 | `51ba24b9…5fa95` |
| `GNLink-0.2.15.apk` | 11,518,126 | `cd33168c…548e` |
| `GNLinkSetup-0.2.108.exe` | 4,339,712 | `def1e344…465c` |
| `GNLink-0.2.16.apk` | 11,518,126 | `91c7f0c7…8c6c` |
manifest·서명은 `.claude/rel/0.2.108/`(미추적): `windows.manifest` 1,533B `b65e2198…5907` · `android.manifest` 326B `dcc87276…432a` · 각 `.sig` 128자.

**회귀**: C++ 62 스위트 **`^PASS` 1733** · 실패 2건(`gdi_capture_process`·`udp_control_e2e`, 원인 미확정) · **JS 335** · **Android 68 / 0 failures**.
**하지 않은 것**: NAS 접속·게시·설치·서비스 변경·nginx/DNS/TLS 변경·push. **키 재생성 없음**, **독립 백업 여전히 미완료(별건)**. 개인키·DPAPI 블롭·토큰은 문서·NAS·Git·로그 어디에도 없다.

### 493) 2026-09-10 게시 준비 정정 3건 — **서명이 통과했다는 것과 문서가 맞다는 것은 다르다**
- 검증용 NEEDS_CHANGES 1건 + 그것을 고치다 스스로 만든 것 1건 + 원인 규명 1건.

**① 스테이징 배치가 manifest 의 URL 과 달랐다 (검증용 지적)**
`shell.html`·`macro.html` 을 **평면으로** 두었는데 manifest 는 `…/updates/0.2.108/**ui/**shell.html` 을 가리킨다. **바이트는 맞고 위치만 틀렸다.**
그대로 올렸으면 **exe 7개는 받아지고 html 2개만 404**, 증상은 **다운로드 도중 실패**라 **서버 문제처럼 보인다.** 스테이징을 `payload/ui/` 로 옮겼다(manifest 를 바꾸는 방향은 안 된다 — 설치 경로가 `ui\shell.html` 이고 URL 을 바꾸면 **재서명**이 필요하다).

**② 🔴 그것을 고치다 내가 만든 것 — 서명은 통과하는데 문서가 낡았다**
APK 를 다시 빌드해 놓고 **manifest 를 재생성하지 않은 채 서명**했다. 그리고 **모든 검증이 통과했다** — C++ · Node · 자체 검증 전부. 당연하다: **서명은 "문서가 서명 이후 바뀌지 않았다" 만 말한다.** *"문서가 실제로 존재하는 파일을 가리킨다"* 는 **아무도 확인하지 않았다.**
→ `check_manifest_matches_disk.py` 를 만들어 **모든 `artifact=` 줄을 실제 파일의 크기·SHA256 과 대조**한다(**10/10 일치**). 인계서에도 *"서명 검증은 이것을 대신하지 못한다"* 로 적었다.

**③ APK 크기 차이 328,312 바이트 — 원인 규명 (검증용 요구)**
*"버전 값뿐"* 이라고 했는데 크기가 맞지 않았다. 파고들었다:
- 항목별 **원본 크기 전부 동일**, **압축 크기 합도 동일**(11,476,299), 압축 방식 동일, 로컬 헤더 extra 도 사실상 동일(18,253 vs 18,249), 서명 블록·중앙 디렉터리도 동일.
- 차이는 **`classes2.dex` 와 첫 네이티브 라이브러리 사이의 빈 공간 328,316 바이트** — **어떤 항목에도 속하지 않는 죽은 바이트**였다(증분 빌드가 남긴 것).
- 유효한 APK 이고 서명도 통과하지만 **설명할 수 없는 바이트가 든 산출물**이라 **clean 빌드로 둘 다 다시 만들었다.** 지금은 **둘 다 11,518,126**, 빈 공간 **0**, 내용이 다른 항목은 **`AndroidManifest.xml`·`classes3.dex` 둘뿐**(118개는 CRC 동일).
- ⚠️ **"크기가 설명되지 않는다" 를 그냥 두지 않은 것이 요점이다.** 맞지 않는 숫자는 대개 **아직 이름 붙지 않은 사실**이다.

**최종 산출물**: `0.2.107` 4,339,712 `51ba24b9…` · `0.2.108` 4,339,712 `def1e344…` · `0.2.15` 11,518,126 `cd33168c…` · `0.2.16` 11,518,126 `91c7f0c7…`. manifest: `windows` 1,533B `b65e2198…`(불변) · `android` 326B `dcc87276…`(APK 교체로 갱신). **둘 다 재서명**했고 C++ `default_verifier()` · Node 양쪽에서 **Ok + 변조 거부**.
**검증용이 확인해 준 것**: 공개키 3중 일치 · 독립 구현(Python `cryptography`)으로 서명 검증 · windows 아티팩트 9/9 가 설치기 RCDATA 와 일치 · **baseline APK 에 공개키 존재**(0.2.14 에는 없음).

### 494) 2026-09-10 🔴 실기 결함 — **캐시 토큰이 있으면 광고를 영영 못 받았다**
사용자 Host(0.2.108)가 `https://rem.shotan.net` 에서 **`directory=pending` 영구**. 서버는 정상이었다.

**무엇이었나**
```cpp
bool HostAgent::EnsureRegistered() {
  if (!hostToken_.empty()) return true;   // ← 캐시 토큰이면 여기서 끝
  ...
  observeAdvertised_ = advertised;        // 광고 대입은 이 한 곳뿐
```
- `LoadCache()` 는 **토큰만** 복원한다. **광고는 등록 응답에만 실려 온다.**
- `directory_client.cpp` 안에 health 광고 취득 호출은 **0건**이었다 — `directory_observe_from_health()` 는 **뷰어에서만** 쓰였다.
- 결과: 캐시 토큰 → 재등록 생략 → 광고 미수신 → **https 는 기본값이 없으므로** 조준 불가 → 관측 실패 → **하트비트 미전송 → pending 영구.**
- ⚠️ **지난번 등록이 성공했을수록 다음 실행이 확실히 막힌다.** 캐시가 잘 되어 있는 것이 고장의 조건이었다.
- ⚠️ **검증용이 seq 1707 에 요구했던 "인증 캐시·자동 로그인·재연결 경로에도 metadata 배선" 이 뷰어에만 반영되고 Host 에 누락된 것**이다. 검사도 뷰어만 보고 넘어갔다.

**고친 것 (좁게)**
- `FetchObserveEndpointFromHealth()` — 광고가 없고 포트가 핀되지 않았으면 **같은 origin 의 `/healthz`** 로 묻고 `ApplyObserveEndpoint()`. **토큰은 건드리지 않는다**(재등록 강제 없음).
- **유계**: 최대 6회, 실패마다 cooldown 이 늘어난다(2·4·6… 주기). **무한 폴링 아님** — 서버가 나중에 설정될 수 있으니 다시 묻되, 영원히 묻지는 않는다.
- **트리거는 "우리가 추측하고 있는가"** 이지 "조준할 곳이 없는가" 가 아니다. https 는 조준할 곳이 없어 증상이 분명하지만, **http 는 `+1` 이 resolve 는 되고 아무도 없는 곳을 가리킨다** — **동작하는 것처럼 보이는 잘못된 조준**이다. 둘 다 덮되 **답을 받았을 때만** 쓴다(무응답이면 http 는 `+1` 그대로, https 는 거부).
- **핀된 포트가 여전히 최우선**이고, 그 경우 **health 를 아예 묻지 않는다.**
- **diag 문구**: 구체 사유(광고 미수신·조준 불가)가 `Run` 의 *"address observation timed out"* 로 **덮이고 있었다.** 조준이 가능했을 때만 그 문구를 쓴다.
- `directory_observe_from_health()` 는 이제 `directory::observe_endpoint_from_health()` 로 **위임**한다 — 구현이 하나여야 하고, **뷰어 파일에 있었다는 것이 호스트가 물을 수 없었던 이유**였다. GET 은 `http_post` 와 같은 이중 전송 규칙으로 `directory_client` 안에 뒀다(그 파일을 링크하는 타깃 10개가 세션 클라이언트를 링크하지 않는다).

**회귀 — 제품 `HostAgent` 로, 스텁 아님**(`directory_retry_test` **48 → 57**)
가짜 디렉터리의 관측 포트는 **OS 가 고른 임시 포트**라 **`http+1` 이 아니다.** 그래서 **하트비트가 갔다는 것 자체가 광고를 묻고 썼다는 증거**다.
- **캐시 토큰 + 광고 있음** → `/healthz` 1회 · 하트비트 1회 · **재등록 0** · `public=` 노출 (양성 대조)
- **캐시 토큰 + 광고 없음** → 게시 없음 · 하트비트 0 · **`/healthz` 2회(유계)** · 사유가 **서버를 지목**
- **핀된 포트** → 하트비트 정상 · **`/healthz` 0회**
- **음성 대조**: 고친 줄을 빼면 **5건 FAIL**(`asks the health route 0` · `heartbeats 0` · `address observation timed out`). 이 테스트가 실제로 그 수정을 잡는다.

**전체**: C++ 62 스위트 **`^PASS` 1746** · 실패 1건(`udp_control_e2e`) · **JS 335** · 재빌드 오류 0.
**하지 않은 것**: 라이브 Host 수정·캐시 삭제·프로세스 재시작·설치·push·NAS 변경. **버전은 올리지 않았다**(검증용이 Codex 확인 후 확정).

### 495) 2026-09-10 🔴 업로더가 종료를 계약하지 않았다 — **정적 소멸자가 joinable 스레드를 파괴**
`log_upload_stop()` **제품 호출자 0건.** 부르는 곳은 `log_upload_test.cpp` **9곳뿐**이었다.

**무엇이었나**
```cpp
UploaderState& state() { static UploaderState s; }   // log_upload.cpp:140
struct UploaderState { ... std::thread worker; };    //             :115
void log_upload_stop() { ... worker.join(); }        //             :551  ← 제품은 안 부른다
```
- 종료 시 static 소멸자가 **joinable `std::thread` 를 파괴** → 표준상 **`std::terminate()`**. 예외도 스택도 없이 **정리하다 죽는다.**
- ⚠️ **현장 BEX64/c0000409 의 원인이라고 단정하지 않는다.** 정확한 빌드의 PDB 가 없어 offset 이 미해석이고, 다른 terminate 경로도 배제되지 않았다. **독립적으로 옳은 수정**으로만 다룬다.
- ⚠️ **테스트가 9번 부르고 제품이 0번 부른 것이 이 결함이 계속 초록으로 보인 이유다.** 커버리지는 계약이 아니다.
- 게다가 Host 의 `WSACleanup()` 은 **worker 가 소켓 안에 있는 채로** 돌고 있었다.

**고친 것 (좁게)**
- `LogUploadShutdown` — **RAII 수명 가드**. Host·Client **두 진입점**에 선언. `log_upload_shutdown()` = 래치 + `log_upload_stop()` + join 후 콜백 해제.
- ⚠️ **`detach` 는 쓰지 않았다.** worker 가 정적 state·콜백을 만지는 중일 수 있어 **소멸 후 UAF / 로그 유실**로 바뀐다. **join 해야 한다.**
- **래치**: `s.shutdown` 은 **worker 를 시작하는 것과 같은 lock 안에서** 검사한다. 밖에서 검사하면 *"configure 가 통과 → shutdown 이 join → configure 가 두 번째 worker 시작"* 창이 남는다. 종료 중 `configure` 는 `"shutting down"` 으로 거부.
- **선언 순서 = 해제 순서**: 함수 끝의 `WSACleanup()` 줄은 **소멸자보다 먼저** 실행된다. 그래서 `WSACleanup`·GDI 정리를 **scope guard 로 바꾸고**, 가드를 **마지막에 선언**했다 → 해제는 **worker join → UI(font) → WSACleanup**.
- **교착 금지**: join 은 **어떤 lock 도 쥐지 않은 채**(worker 가 매 주기 `s.mu` 를 잡는다), 그리고 **메시지 루프의 그 스레드에서**. auth 콜백은 `PostMessageW` 라 동기 대기가 없다.
- **종료 대기 상한**: 최종 drain 이 **새 요청을 시작하는 것**을 `kStopDrainBudgetMs = 3000` 으로 끊는다. 없으면 대기는 **큐 깊이 × http timeout** 이고, 그 대기는 **UI 스레드**에서 일어난다.
- **실측 상한**: `3000ms + 진행 중인 http_post 1건`. accept 후 응답하지 않는 서버 상대 **실측 5,997ms**(directory_client 수신 timeout 6s 가 지배). 가정이 아니라 **테스트가 파일에 적는 숫자**다.

**회귀 — 격리 child 프로세스**(`log_upload_shutdown_test`, 신규 9건)
⚠️ **테스트가 `log_upload_stop()` 을 부르는 것으로는 아무것도 증명되지 않는다** — 지금도 9곳이 그러고 제품은 0곳이었다. 그래서 **판정은 child 의 종료 코드**다.
- **정상 exit** → 0 · 이후 `log_upload_running()` false
- **미시작 exit** → 0 (토큰 없는 Host 가 이 상태다)
- **요청 진행 중 exit** → 0 · **유계 실측 5,997ms**
- **401 pause 상태 exit** → 0 (보낼 방법 없는 held 를 기다리지 않는다)
- **configure 경합** → join 이후 250ms 동안 계속 configure 해도 **재시작 0**, 거부는 실제로 발생
- **음성 대조**: 가드 없이 같은 일을 하면 **exit=3**(abort). ⚠️ 이때 sink 스레드는 **명시적으로 정지**시켰다 — 그러지 않으면 **엉뚱한 스레드 때문에 죽는 것**을 잡고 통과했다고 착각한다.
- **제품 배선 대조**: `host_app_main.cpp` · `client_shell_main.cpp` 를 **소스에서 읽어** 가드 선언을 확인한다. 종료 코드로는 볼 수 없고, **없었던 것이 정확히 이 연결**이다.

**추가 회귀 — 새 질문이 옛 답을 망가뜨리지 않는가**(`directory_retry_test` **57 → 67**)
health 조회는 **새로 생긴 질문**이고, 새 질문은 옛 답을 깨뜨린다. http 에서 관측 포트는 늘 **유도 가능**했고(`httpPort+1`) **현재 배포된 디렉터리가 전부 그 배치**다.
- 이 케이스만 UDP 를 **`httpPort+1` 에 바인딩**한다(loopback, 고정 포트 아님, 최대 32회 재시도). 나머지 캐시 케이스는 **임시 포트**라 정반대를 증명한다 — 하나는 *"광고를 실제로 썼다"*, 이것은 *"광고가 필수는 아니다"*.
- **health 500** · **health 200 인데 아무 말 없음** 두 행 모두: 하트비트 1 · **관측이 실제로 `+1` 로 감**(`ObserveProbes>=1`) · 재등록 0 · `public=` 게시.
- ⚠️ 인접 포트를 못 잡으면 **조용히 건너뛰지 않고 FAIL** 시킨다. 안 돌아서 통과하는 케이스가 제일 위험하다.

**남는 것(기록)**: Client 의 auth 콜백은 메시지 루프 종료 후 발화하면 `PostMessageW` 로 넘긴 `std::string` 이 **디스패치되지 않아 누수**된다 — 프로세스 종료 직전이라 영향은 없고, 범위 밖이라 고치지 않았다.
**전체**: C++ **63 스위트** `^PASS` **1762**(가드 스위트 신설로 62→63, 1746→1762) · 실패 1건 `udp_control_e2e`(**이번 변경 이전부터 red**, #494 와 동일) · **JS 335** · 재빌드 오류 0.
**하지 않은 것**: 강제 종료 전역 동작 추가 없음 · 광역 재설계 없음 · 라이브 실행·설치·캐시 삭제·push·NAS 변경 없음.

### 496) 2026-09-10 `0.2.109` 후보 — **Windows 단독**, 새 baseline 없음
`b4d2a7a`(#495) 까지를 담아 `kProductVersion` 만 `0.2.108` → `0.2.109`. **Release 전체 재빌드, 오류 0.**

**왜 Windows 하나뿐인가 (검증용이 Codex 확인 후 확정)**
- **Android 는 `0.2.16`/vc15 게시 그대로.** `422196b`·`b4d2a7a` 는 **Android 파일을 하나도 건드리지 않았다.** 바뀐 게 없는데 버전을 올리면 **그 번호가 아무것도 증명하지 않게 된다.**
- **새 baseline/target 쌍 불필요.** 이미 설치된 **0.2.108 이 출발점**이다.
- 🔴 **정정 — "0.2.108 은 스스로 업데이트할 수 없다" 는 근거가 없었다.** 검증용이 철회했고 코드가 그렇다: `start_update_check()`(`host_app_main.cpp:799`) 는 `current_update_endpoint()` 로 **캐시된 url·host token** 을 받아 **https 로 독립 호출**한다. **`directory=online` 도 하트비트 성공도 요구하지 않고**, 트레이 메뉴도 **조건 없이** 부른다.
  ⚠️ *"연결이 막혔다"* 에서 *"업데이트도 막혔다"* 를 **추론으로 이어붙인 것**이었다. 두 경로는 서로를 필요로 하지 않는다.

**수정이 실제로 들어갔는지 — 바이너리에서 확인**(빌드 로그가 아니라 산출물에서)
| 문자열 | 0.2.108 | 0.2.109 | 어디에 |
|---|---|---|---|
| `/healthz` | **없음** | **있음** | `GNLinkStream.exe`(HostAgent 가 사는 곳) · `GNLinkViewer.exe` |
| `stop budget spent` | **없음** | **있음** | `GNLinkHost.exe` · `GNLinkClient.exe`(업로더 소유자 둘) |
| 운영 공개키 128자 | 있음 | **있음** | `GNLinkHost.exe` |
⚠️ 처음엔 `GNLinkHost.exe` 에서 `/healthz` 를 찾다가 **없다고 볼 뻔했다.** `HostAgent` 는 **`GNLinkStream.exe`** 안에서 돈다 — `GNLinkHost.exe` 는 그 감독자다. **엉뚱한 바이너리를 보고 "수정이 빠졌다" 고 할 뻔한 것**이 이 표를 만든 이유다.

**산출물**
| 파일 | 크기 | sha256 |
|---|---|---|
| `GNLinkHost.exe` | 660,480 | `a02e8ccb3fe2c7e8…` |
| `GNLinkStream.exe` | 1,000,448 | `797af6e2213614c7…` |
| `GNLinkInputService.exe` | 226,304 | `1fd2aa569880cbe9…` |
| `GNLinkCapture.exe` | 127,488 | `253734df347adb2b…` |
| `GNLinkClient.exe` | 707,584 | `210e514060a4eb46…` |
| `GNLinkViewer.exe` | 867,328 | `9a5937df1d6b4f33…` |
| `GNLinkUpdater.exe` | 532,480 | `a333df2726b67d25…` |
| `ui\shell.html` | 12,648 | `15aa648adf28a6a2…` (불변) |
| `ui\macro.html` | 10,996 | `f589f5df16d24fc8…` (불변) |
| `.claude/rel/0.2.109/windows.manifest` | 1,534 | `743fbb1f19f16c02…` |
| `dist/GNLinkSetup-0.2.109.exe` | 4,352,000 | `f52ba327c6a897e1…` (수동 설치용 예비, **manifest 대상 아님**) |

**manifest ↔ 실제 파일 대조 9/9 일치.** ⚠️ 서명은 *"문서가 서명 이후 바뀌지 않았다"* 만 말한다 — *"문서가 존재하는 파일을 가리킨다"* 는 말하지 않는다(#493 에서 실제로 갈렸다).
**기존 파일 불변**: `0.2.107`·`0.2.108`·APK 전부 손대지 않았고, `.claude/rel/0.2.108` 은 **읽지도 쓰지도 않았다.**

**서명**(사용자 승인 후 실행, 기존 운영키. **새 키 생성 없음**)
`windows.sig` 128자 `85fdc3c76fce72cf…`(파일 해시), 서명값 `0de7ca480a6de20b…`. 개인키는 DPAPI blob 에서 메모리로만 풀고 종료 전 소거, 출력은 서명값뿐.

**3중 검증 — 서로 다른 구현이 같은 바이트에 동의하는가**
서명자는 .NET CNG 다. **자기 자신과의 일치는 증거가 아니다** — 인코딩을 틀린 서명도 자체 검증은 똑같이 통과한다.
| 구현 | 대상 | 결과 |
|---|---|---|
| Node `apps/directory/update_manifest.js` | **실제 `windows.manifest`** | Ok · `version=0.2.109` · `releaseId=r-0.2.109` · artifact 9개 · **1바이트 변조 거부** · **다른 키로 거부** |
| Python `cryptography`(OpenSSL) | **실제 `windows.manifest`** | Ok · raw r‖s 64B · 키 128자 · **1바이트 변조 거부** |
| C++ `default_verifier()`(**컴파일된 키**) | 같은 키로 서명한 **fixture 문서** | Ok · **1바이트 변조 거부** · 다른 키 서명 거부 (86 checks) |
⚠️ **C++ 은 `windows.manifest` 자체가 아니라 fixture 를 검증한 것**이다. *"제품에 박힌 키가 이 키의 서명을 받아들인다"* 는 증명되고, *"제품이 이 문서를 읽었다"* 는 아니다. 그 구분을 지운 채 "3중 검증 완료" 라고 쓰지 않는다.

⚠️ 검증 스크립트를 처음 돌렸을 때 **수락 검사만 FAIL, 거부 검사 2개는 PASS** 였다. 원인은 `Status.Ok`(존재하지 않음, `undefined`) — **거부 검사는 `!== undefined` 라서 무엇이든 통과**하고 있었다. `Status.OK` 로 고쳤다. **틀린 상수 하나가 음성 대조 두 개를 동시에 무력화**했고, 수락 검사가 없었으면 셋 다 초록으로 보였다.

**아직 안 된 것**: **NAS 게시 미실행**(작업용은 NAS 를 만지지 않는다). 검증용 검사 → 기존 승인 경로로 인계.
**하지 않은 것**: 게시·설치·라이브 실행·프로세스 중지·캐시 삭제·push·NAS 변경 없음. APK 재생성 없음. 새 서명키 생성 없음.

### 497) 2026-09-10 🟡 스위트 4개가 빨개졌다 — **원인 미확정, 다만 세션 상태가 그 사이에 바뀌었다**
검증용이 `53d4f66` 을 **구현 OK** 하면서 함께 보고했다: 자기 직전 전수(`76897cf`)에서 rc=0 이던 **`host_bgra_scale` · `host_encode_epoch` · `viewer_udp_recovery`** 가 rc=1 이고, `gdi_capture_process` 를 더해 **4개**. `udp_control_e2e` 는 오히려 **1P/17F → 16P/2F 로 개선**.

**우리 변경 때문인가 — 도달성으로는 아니다 (검증용 확인)**
- `host_bgra_scale`·`host_encode_epoch` — **변경 파일 링크 0건.**
- `viewer_udp_recovery` — `directory_client.cpp` 를 **링크는 하지만**, `HostAgent` 도 신규 함수도 `directory::` 심볼도 **쓰지 않는다.** 우리 변경은 전부 `HostAgent::Run` 과 신규 함수 안이라 **도달하지 않는다.**
- ⚠️ **링크 겹침 0 만으로 무관을 증명하지 않는다**는 기준은 유지한다. 도달성은 *"이 경로로는 아니다"* 까지만 말한다.

**내가 재실행해서 본 것 (전부 사실, 해석 아님)**
| 스위트 | 실패 지점 | 산출 |
|---|---|---|
| `host_bgra_scale` | `capture_window_thumbnail(desktop)` → **`FAIL desktop thumbnail captured`** | PASS 0 / FAIL 0 (단정 전에 끝남) |
| `gdi_capture_process` | — | **`GDI_DELIVERED_FRAMES=0`**, fps 0, copy 통계 전부 0 |
| `host_encode_epoch` | `hold_for(...)` — **`surface(D3D11 NV12)` 하드웨어 MFT 경로** | `A surfaced 0 us after its capture` |
| `viewer_udp_recovery` | epoch 전환 — `transitions +0, requests +0` | 미디어 타이밍 |
| `udp_control_e2e` | — | 16 PASS / 2 FAIL (**개선**) |

**🔴 두 실행 사이에 `qwinsta` 가 달라졌다**
```
녹색이던 때:  >console   shotan   1  활성
지금:          (없음)    shotan   1  Disc        ← 사용자 세션이 콘솔에서 떨어졌다
               console            8  Conn        ← 콘솔에 붙은 사용자가 없다
```
- 테스트 프로세스는 **세션 1** 에서 돈다. 그 세션이 **Disc** 면 **붙어 있는 데스크톱이 없다.**
- 빨간 4개는 **전부 데스크톱 캡처·GPU 인코더 의존**이고, 전달 프레임이 **0** 이다. 느린 게 아니라 **아무것도 안 나온다.**
- 반대로 **그 의존이 없는 `udp_control_e2e` 는 좋아졌다.** 코드가 아니라 **환경이 움직였다**는 쪽을 가리킨다.
- ⚠️ **그래도 "디스플레이 탓" 으로 단정하지 않는다.** 세션 상태 변화는 **관측된 사실**이고, 그것이 **이 4개의 원인이라는 것은 아직 증명되지 않았다.** 다른 terminate·타이밍 요인을 배제하지 않았다.
- ⚠️ **CLAUDE.md 규칙의 이유가 그대로 적용된다**: 세션 상태가 잘못된 채 나온 결과는 **PASS 든 FAIL 든 판정 근거로 쓰지 않는다.** 지금 이 4개는 **판정이 아니라 관측**이다.
- **깨끗한 판정 조건**: 콘솔 세션이 다시 붙은 뒤 **같은 바이너리로 재실행.** 그때도 빨가면 그때부터 코드 문제로 다룬다.

**전수 합계 — 항목으로 쪼개니 잔여 0 (검증용이 마무리, 커밋 하나는 내가 정정)**
검증용의 **같은 기준 비교**(run5 `76897cf` **1730** → run6 `53d4f66` **1780**, **+50**):
| 스위트 | 증분 | 이유 |
|---|---|---|
| `directory_retry_test` | **+23** (44→67) | 캐시 토큰 fix 회귀 + HTTP 보존 회귀 |
| `log_upload_shutdown_test` | **+9** (0→9) | 신규 |
| `udp_control_e2e_test` | **+15** (1→16) | 세션 상태 쪽 |
| `update_manifest_test` | **+3** (82→85) | 공개키 내장 |
| | **+50** | **잔여 0** |

- ⚠️ **내가 "미설명" 으로 남겼던 `+3` 의 정체**는 `update_manifest_test` 였다. 내 가설(*"빨간 스위트가 죽기 전 내는 PASS 줄"*) 은 **틀렸다** — 그 넷은 양쪽 실행에서 **`^PASS` 0** 이라(출력 형식이 다르다) 차이에 기여하지 않는다.
- ⚠️ **커밋 귀속은 정정한다.** 검증용은 `422196b` 라고 했지만 **그 커밋은 `update_manifest*` 를 건드리지 않았다**(`directory_client.{hpp,cpp}` · `directory_session_client.cpp` · `directory_retry_test.cpp` · `docs/history.md` 5파일뿐). `76897cf..53d4f66` 구간에서 그 파일을 바꾼 커밋은 **`47a63b9`**(*"embed the operational public key, and stop asserting there is none"*) **하나뿐**이다. 실질(공개키 내장)은 맞고 **번호만 틀렸다.**
- ⚠️ **내가 비교한 1762 vs 1780(=18) 은 애초에 동일 기준이 아니었다.** 서로 다른 빌드의 두 숫자를 뺀 것이라 **그 18 자체가 의미 있는 양이 아니었다.** 같은 기준으로 쪼개니 전부 설명된다.
- ⚠️ **합계가 다를 때 누가 맞느냐를 겨루지 않는다.** 숫자는 **차이를 항목으로 분해할 때만** 증거가 되고, 그전에 **두 숫자가 같은 것을 재고 있는지**부터 확인해야 한다.
- 실측 참고: `update_manifest_test` 는 **벡터 디렉터리에 따라 개수가 다르다** — 기본 벡터 **85**, `.claude/keyfixture`(운영키 서명 fixture) **86**. 집계를 비교할 때 **어느 쪽으로 돌렸는지**가 숫자의 일부다.

**하지 않은 것**: 세션 재연결·로그온·프로세스 종료 없음(사용자 것이다). 코드 수정 없음 — **`53d4f66` 산출물은 그대로다.**

### 498) 2026-09-10 배포를 손에서 스크립트로 — **정책 + `automation/gnlink_deploy.sh`**
사용자 새 정책: *"gnlink 권한으로 안 되는 것만 NAS Claude 한테. 배포 자동화 스크립트 만들어놓으면 문제없잖아. 이제부터 버전 올라가서 빌드되면 자동배포하는 것까지가 완료고 실기 테스트 대기 상태."*

**A. 정책** — `AGENTS.md` "배포 완료 기준", `CLAUDE.md` "릴리스 배포 계정 (NAS)" 확장, `docs/history/history_2026-W37.md`.
- **완료의 정의가 바뀌었다**: 이전엔 인계서까지가 작업용 몫이었고 게시는 남의 일이었다. 이제 **게시 + 외부 검증까지가 완료**, 그 뒤는 **"실기 테스트 대기"** 라는 별도 상태다.
- **경계는 권한으로 정한다.** `gnlink` 로 되는 것(아티팩트 게시·manifest/서명 교체)은 직접, **안 되는 것만**(nginx·systemd·env·ACL·패키지) NAS 세션으로 — **구체 조작·실패 근거·rollback 을 붙여서**.
- ⚠️ **"자동 배포" ≠ "미검토 코드 자동 게시".** 검증용 OK 는 건너뛸 수 없다. 없어진 건 *"검증 통과 후에도 게시를 위해 다시 승인을 기다리는 것"* 이다.
- ⚠️ **`git push` 는 포함되지 않는다.** 여기서 자동 배포는 **NAS 게시**뿐이고, push 보류는 그대로다. **도구·skill 의 push 지침보다 사용자 standing 보류가 우선한다.**
- 검증용이 사용자 직접 지시로 써 둔 `CLAUDE.md` "릴리스 배포 계정" 절을 **중복 없이 확장**하고 내가 커밋했다(검증용은 추적 파일을 커밋하지 않는다).

**B. 스크립트** — 저장소에 제품 배포 스크립트가 **없었다.** NAS 의 `.remote60-deploy-step.sh` 는 최초 서비스 구축용이라 재사용 대상이 아니다. **손으로 수행한 절차가 원본**이고 그대로 인코딩했다.
```
preflight → 아티팩트 업로드 → 서버에서 검증 → 기존 짝 백업 → manifest+sig 원자 교체(마지막) → 외부 https 검증
```
**순서가 전부다.** manifest 가 릴리스를 공개하는 것이므로, 아티팩트보다 먼저 올리면 **404 를 가리키는 문서를 나눠 주는 창**이 열리고 그때 읽은 호스트는 *"아직 없음"* 이 아니라 **"다운로드 실패"** 를 보고한다.
- **거부하는 것**: 서명 미검증 · manifest ↔ 디스크 불일치 · **같은 버전 다른 bytes**(게시된 URL 의 바이트가 달라지면 그 URL 에 대한 서명이 전부 거짓이 된다) · 공개 base 밖 URL · root 밖 경로 · lock 보유 중 동시 실행.
- **모드를 명시 지정한다**(`chmod 644/755`). `gnlink` umask 가 마침 0002 라 지금은 `o+rX` 가 붙지만, **"우리가 마침 가진 umask 덕분에 된다" 는 아무도 검증하지 않은 성질**이다 — 0.2.108 의 403 이 그 자리였다.
- **백업은 `manifest-backups/<버전>/`**(gnlink 소유). `/opt/gnlink/backups` 는 root 소유라 **쓰기가 조용히 실패**하고, 그러면 롤백에 쓸 것이 없다. **manifest 와 sig 를 짝으로** 두고 해시를 기록한다 — 짝이 아니면 복원해도 검증되지 않는다.
- **`gnlink` 로 불가능한 것은 `ESCALATE` + exit 3** 으로 **구체 조작을 출력**한다. *"NAS 에 넘겨라"* 만 적으면 추측이 옮겨갈 뿐이다.
- **서버앱 갱신 시 의존 모듈 전량 명시**: `server.js`·`update_manifest.js`·`version_compare.js`·`package.json`(외부 npm 없음). ⚠️ 지난번 *"server.js 단독"* 으로 올려 첫 재시작이 `MODULE_NOT_FOUND` 였다.
- ⚠️ **무인증 401 로 업데이트 성공을 검증하지 않는다** — 잠금장치를 확인한 것이지 방 안을 본 게 아니다. 스크립트가 그 문구를 출력에 박아 둔다.
- 공개키는 `update_manifest.cpp` 에서 **읽는다**(복사하지 않는다). 키가 두 곳에 있었을 때 한쪽이 빈 문자열이었고 **모든 업데이트가 조용히 검증 실패**했다.

**회귀 — 실제 스크립트를 격리 root 에 겨눠 실행**(`gnlink_deploy_test.sh`, **40 PASS**)
dry-run 무변경 · 정상 게시(9개·`ui/` 유지·`.tmp` 잔여 0·lock 해제) · **재실행 멱등**(`uploaded 0, already present 9`) · **같은 버전 다른 bytes 거부 + 이전 릴리스 유지** · **서명 실패 시 업로드 0** · **해시 불일치 거부** · 파일 없음 거부 · **lock 배타(남의 lock 안 뺏음)** · verify-only 무변경 · 백업 짝+SHA256SUMS · 공개 base 밖 URL 거부.
- ⚠️ 테스트가 **결함 2건을 잡았다**:
  1. **성공한 배포가 lock 을 남겼다.** `rmdir` 은 비어 있지 않은 디렉터리에서 실패하는데 `owner` 파일을 안 지웠다. **실패가 아니라 성공이 남기는 lock 이 더 나쁘다** — 아무 이상 없어 보이다가 **다음 릴리스가 무관한 이유로 거부**된다.
  2. 해시 불일치 케이스가 **append 로 조작**해 **크기 검사가 먼저 걸렸다.** 단정 문구는 해시를 말하는데 **해시 비교는 돌지 않았다.** 크기를 보존하는 **in-place 1바이트 변조**로 바꿨다.
- ⚠️ **ssh 와 curl 은 이 회귀가 대신하지 못한다.** 격리 root 로 흉내낼 수 없는 둘이고, 그런 척하는 것이 더 비싼 초록이다. 그래서 **실서버 `--verify-only`** 를 따로 돌렸다.

**실서버 `--verify-only`**(게시 없음, 변경 없음): manifest **IDENTICAL**(`743fbb1f…`) · 서버의 **9/9 가 manifest 와 일치하고 읽을 수 있음** · **9/9 를 익명 https 로 받아 서명된 해시와 일치** · 무인증 manifest **401**(그리고 그것이 증거가 아님을 출력에 명시). 0.2.109 는 **이미 정상 게시된 상태**이고 **재게시하지 않았다.**

**기록해 둘 것**: `~/.ssh/remote60_deploy`(개인키)가 **644** 다. 이 PC 는 Windows 라 POSIX 모드가 그대로 적용되지는 않고 ssh 도 거부하지 않았지만, **저장소 밖 파일**이라 손대지 않고 남긴다.
**하지 않은 것**: 재게시·재시작·설치·라이브 조작·`git push`·NAS 설정 변경 없음. **테스트 목적의 버전 인상 없음.**

### 499) 2026-09-10 `0.2.109` 복구 게시 — **manifest 에 `GNLinkSetup.exe` 가 빠져 있었다**
검증용 실측: 서버 `0.2.109` 디렉터리에 **exe7 + ui2 = 9개**, `GNLinkSetup.exe` 없음. **공개 Setup URL 404.**

**왜 이게 빠지면 안 되나 — 제품 소스가 이미 그렇게 말하고 있었다**
```cpp
// update_process_targets.cpp:36  product_image_names()
// The installer counts too, now that it is a member of the update package.
L"GNLinkSetup.exe",
```
- 언인스톨 항목이 **`<installDir>\GNLinkSetup.exe`** 를 가리킨다(`install_registration_test.cpp:9`). 업데이트가 앱만 바꾸고 이 파일을 안 바꾸면 **언인스톨러가 사라진 버전을 설명하게 된다.**
- `payload_name_test.cpp:49` 는 `GNLinkSetup.exe` 를 **Ok** 로 단정한다. `product_image_names()` 는 이미 그것을 **정지 대상**에 넣어 두었다.
→ 즉 **제품은 이미 그 파일이 패키지 멤버라고 전제하고 있었고, manifest 만 그렇지 않았다.** 이것은 결정이 아니라 **누락**이었다.

**🔴 "업로드만 하고 manifest 는 두는" 선택지는 애초에 없었다** (검증용 지적, 코드로 재확인)
내가 *"URL 만 열면 404 는 해소된다"* 를 선택지처럼 적었는데 **틀렸다.**
```cpp
// update_effects.cpp:312  Download -- manifest 의 목록만 받는다
for (const ManifestArtifact& artifact : fields.artifacts) { ... stagedNames_.push_back(...); }
// update_effects.cpp:468  Swap -- payloadNames 전부가 staged 에 있어야 한다
for (const std::wstring& name : config_.payloadNames)
  if (GetFileAttributesW(staged_path_for(...)) == INVALID_FILE_ATTRIBUTES)
    { lastError_ = "the staged release does not contain " + to_utf8(name); return false; }
```
- staging 에는 **Download 가 받아온 것만** 들어가고, Download 는 **`fields.artifacts` 만** 돈다.
- 그래서 `payloadNames` 에 있고 manifest 에 없는 이름은 **URL 이 열려 있든 말든 swap 에서 실패**한다.
- ⚠️ **"파일이 서버에 있다" 와 "업데이트가 그 파일을 얻는다" 는 다른 명제다.** 나는 그 둘을 한 문장으로 묶어 선택지를 만들었다. **manifest 추가는 선택이 아니라 필수였다.**
- 대조를 도구화했다(`.claude/check_release_payloadnames.py`, 목록은 `updater_effects.cpp:293-295` 에서 읽는다): **payloadNames 9/9 가 manifest 에 존재.** `GNLinkUpdater.exe` 만 manifest 에 있고 swap 대상이 아닌데, **업데이터는 자기 자신을 교체하지 않으므로** 정상이다.

**`releaseId` 는 staging 디렉터리 이름이 된다**(`staged_path_for`). 그래서 **경로로 안전한 값이어야 한다** — `r-0.2.109-2` 는 안전하다. 값이 바뀌었으므로 **기존 `r-0.2.109` staged 를 재사용하지 않고 다시 받는다.** 이것도 의도한 결과다.

**게시한 것**: **version 은 `0.2.109` 유지**(기존 9개 바이트 불변 — 안 바뀐 것을 바꿨다고 말하는 번호는 증거가 아니다), **`releaseId` 만 `r-0.2.109` → `r-0.2.109-2`**. 두 리더 모두 releaseId 에 **비어 있지 않을 것** 외의 제약이 없다(`update_manifest.cpp:195`, `update_manifest.js:163`).
| | 값 |
|---|---|
| `windows.manifest` | 1,703B `1d5c11b580b2a28a…` (artifact **10줄**) |
| `windows.sig` | 128자, 파일 `80203ff67f85016f…` |
| 추가된 아티팩트 | `GNLinkSetup.exe` **4,352,000B** `f52ba327c6a897e1…` |
| 백업 (교체 전 원본 pair) | `/opt/gnlink/manifest-backups/0.2.109/` — `743fbb1f…` + `85fdc3c7…` + `SHA256SUMS` |
서명은 **최초 0.2.109 와 같은 도구·같은 키**(`.claude/sign_release_109.ps1`, `%LOCALAPPDATA%\GNLink\ReleaseSigning\p256-a0e184579c5d4c2d`, mtime 무변경). **새 키 없음, 비밀키는 이 PC 밖으로 나가지 않았다.**

**🔴 게시 전에 스크립트 결함 1건을 고쳤다 — 지시받은 백업 단계가 보장되지 않고 있었다**
`backup_current_pair` 는 백업 경로를 **게시된 version 만으로** 만들었다(`manifest-backups/0.2.109/`).
- 1회차는 정상이다. **2회차가 문제**다 — 부분 실패 후 재실행하면 `cur_version` 이 다시 `0.2.109` 라 **`cp -p` 가 원본 pair 를 새 pair 로 덮는다.** 롤백 지점이 **롤백해야 할 대상 자체로** 바뀐다.
- ⚠️ **다른 모든 곳을 안전하게 만든 멱등성이 여기서는 정확히 반대로 작동한다.** 스크립트가 재실행을 권장하는 구조라 더 위험했다.
- 수정: 백업 디렉터리가 이미 있으면 **덮지 않고** `0.2.109-<UTC>` 로. **음성 대조**: 가드를 빼면 신규 회귀 2건이 FAIL 하고 백업이 실제로 새 manifest(`743fbb1f…`→`1d5c11b5…`)로 덮인다.
- ⚠️ 검증용은 이 가드가 **`d110fe2` 에 이미 있었다**고 정정을 보냈지만, `git show d110fe2:automation/gnlink_deploy.sh` 에 **그 가드도 그 주석도 없다.** 인용된 주석 문구는 **`cacede2` 에서 내가 쓴 것**이고, 작업 트리를 보고 이전 커밋에 있었다고 읽은 것이다. 가드는 **1개**뿐이며 중복 추가는 없다. — **커밋 내용을 확인하지 않고 작업 트리로 판단하면 "이미 있다" 와 "방금 생겼다" 가 구분되지 않는다.**

**dry-run 이 항상 exit 1 이던 것도 고쳤다**: 아직 올리지 않은 아티팩트는 당연히 404 인데 외부 검증이 그걸 실패로 셌다. ⚠️ **항상 1인 종료 코드는 신호이기를 그만둔다** — dry-run 에서는 외부 검증을 건너뛰고 **건너뛰었다고 말한다.**

**🔴 그리고 이번에 한계 하나를 없앴다 — `remote60_verify_release`(신규 도구)**
지금까지 C++ 쪽은 **fixture 만** 검증했다. 그건 *"박힌 키가 이 키의 서명을 받아들인다"* 를 증명하지 *"제품이 이 문서를 받아들인다"* 를 증명하지 않는다 — **다른 주장이다.** 이제 **제품이 쓰는 `load_manifest()` + `default_verifier()` + 컴파일된 키**로 **실제 문서**를 검증한다.
- `.claude/rel/0.2.109-r2` 문서 → **Ok**, 1바이트 변조 → **SignatureInvalid**.
- 게시 후 **서버에서 되받아온 바이트**로 다시 → **Ok**(`version=0.2.109 releaseId=r-0.2.109-2 artifacts=10`). 서버가 실제로 들고 있는 것을 **제품의 검증기가 받아들인다.**

**게시 검증 (전부 실행함)**
- **manifest ↔ 디스크 10/10** · **서버 10파일 size+sha256 10/10 일치**, 전부 world-readable(신규 644, 기존 755)
- **공개 URL 10개 전부 200 + 해시 일치** — `GNLinkSetup.exe` **404 → 200 f52ba327…**
- 서버에서 되받은 manifest·sig 가 서명한 것과 **바이트 동일**
- **production required-set 대조 기계적으로 실행**: `product_image_names()` **7/7** manifest 에 존재 · 설치기 payload **9/9** 존재(`.claude/check_release.py`, 목록을 소스에서 읽는다 — 손으로 옮긴 목록은 어긋난다)
- Node(서버 구현)·OpenSSL 도 실제 문서 수락 + 변조 거부
- **재시작 0 · env 변경 0 · 기존 9개 파일 무변경 · APK 무변경**

**부수효과(의도된 것으로 기록)**: 이제 **0.2.108 이하에서 올라오는 호스트가 `GNLinkSetup.exe` 를 받아 설치 디렉터리에 쓴다**(+4.35MB/회). 위의 언인스톨 항목 계약이 그것을 요구한다. **이미 0.2.109 인 호스트는 `isNewer` 가 false 라 아무 것도 하지 않는다.**
**미검증**: 실기 업데이트 완주(호스트가 실제로 이 manifest 를 받아 10개를 교체하는 것) · 인증 필요한 `/api/update/manifest` 응답 본문(무인증 **401** 은 잠금장치를 본 것이지 방 안을 본 것이 아니다) · 언인스톨 항목이 실제로 갱신되는지.
**하지 않은 것**: live 앱 설치·종료 없음 · 광역 cleanup 없음 · `git push` 없음 · 기존 버전 파일 덮어쓰기·삭제 없음 · nginx/systemd/env/ACL 무변경(NAS 에스컬레이션 불필요) · **파일명·RCDATA 확인만으로 완료 판정하지 않았다.**

### 500) 2026-09-10 `0.2.109` 복구 게시 **구현 OK** — 그리고 RDP 가 켜져 있었다
검증용이 `cacede2`(기능) / `17235f5`(문서) 를 **독립 검사하고 OK**. 사용자에게 **0.2.108 호스트에서 업데이트 재시도 안내 승인**.

**검증용이 자기 손으로 실행한 것**(내 로그 인용이 아님)
| 항목 | 결과 |
|---|---|
| 서버에서 직접 받아온 pair | `windows.manifest` **1,703B `1d5c11b5…`** · `windows.sig` **128B `80203ff6…`** — 내 보고와 일치 |
| 공개 URL 10개 직접 fetch | **10/10 200 · size · hash 일치, 실패 0**. `GNLinkSetup.exe` 4,352,000B `f52ba327…` (복구 전 실측한 로컬 dist 해시와 동일) |
| required-set 소스에서 재추출 | 9개 → manifest 10줄과 대조 **MISSING none**, EXTRA 는 `GNLinkUpdater.exe` 하나뿐 |
| `remote60_verify_release` 를 **자기가 받아온 서버 바이트**에 실행 | **Ok, artifacts=10**, 1바이트 변조 → SignatureInvalid. 도구가 제품의 `load_manifest`/`default_verifier` 를 **실제로 호출**함(재구현 아님)을 소스로 확인 |
| 서버가 내보낼 때 쓰는 JS 경로 | `server.js:425` 요청마다 `readFileSync`(캐시 없음), `:436` 에서 `loadManifest` 재검증. 같은 구현을 같은 문서에 직접 실행 → ok |
| 롤백 지점 | `manifest-backups/0.2.109/windows.manifest` = **1,534B, mtime 15:42** = 복구 전 원본. **덮이지 않았다.** `0.2.108` 백업도 잔존 |

⭐ **무인증 401 로 못 보던 응답 본문 문제가 구조적으로 닫혔다.** 서버는 캐시 없이 파일을 읽어 `loadManifest` 로 재검증한 뒤에만 내보내므로, **그 파일이 그 구현으로 통과한다는 것**이 곧 응답 본문의 검증이다. 잠금장치만 보고 방 안을 봤다고 말하던 자리가 사라졌다.

**🔴 검증용이 자기 정정을 냈다 — 그리고 그 과정 자체가 기록할 값이다**
직전 라운드에서 *"백업 가드는 `d110fe2` 에 이미 있으니 수정하지 말라"* 는 취소 지시가 왔었다. `git show d110fe2:automation/gnlink_deploy.sh` 에 **가드도 주석도 없었고**, 인용된 주석은 **내가 방금 쓴 것**이었다 — **워킹트리를 보고 이전 커밋으로 읽은 것.** 검증용이 이를 확인하고 정정했다.
- ⚠️ **"이미 있다" 와 "방금 생겼다" 는 워킹트리로는 구분되지 않는다.** 커밋을 봐야 한다.
- ⚠️ **취소 지시를 근거 없이 따르지 않은 판단이 옳았다.** 따랐다면 이번 복구의 롤백 지점은 재시도 한 번에 사라졌을 것이다.
- 반대로 같은 메시지의 *"업로드만으로는 안 되고 manifest 추가가 필수"* 는 **유효했고 내 오류였다**(#499). **한 메시지 안에서 한쪽은 틀리고 한쪽은 맞을 수 있다 — 메시지 단위가 아니라 주장 단위로 확인해야 한다.**

**🔴 RDP 가 켜져 있다 — #497 의 빨간 4개에 대한 후보가 강해졌다**
검증용이 회귀·빌드를 재현하지 않은 이유로 밝힌 것: `qwinsta` 에 **`rdp-tcp#0 shotan 1 Active`**. 내가 직접 확인해도 같다.
```
>rdp-tcp#0   shotan   1  Active      ← 지금
 console               9  Conn
```
- #497 당시에는 세션 1 이 **`Disc`** 였다(콘솔에 붙은 사용자 없음). 지금은 **RDP 로 붙어 있다.** **두 상태 모두 "콘솔에 붙은 데스크톱이 아니다"** 이고, `CLAUDE.md` 의 RDP 규칙이 존재하는 이유가 정확히 이것이다.
- ⚠️ **그래도 원인으로 단정하지 않는다.** 후보가 강해졌을 뿐이고, `Disc` 와 `Active RDP` 는 같은 상태도 아니다. **깨끗한 판정은 여전히 콘솔 세션으로 붙은 뒤의 재실행**이다.
- ⚠️ **지금은 테스트를 돌리지 않는다.** RDP 중 결과는 **PASS 든 FAIL 든 판정 근거가 아니다.**

**미검증(검증용과 동일하게 기록)**: 실기 업데이트 완주(0.2.108 호스트가 실제로 10개 교체) · 인증된 `/api/update/manifest` 실응답 · 언인스톨 항목 실제 갱신 · 회귀/빌드 재현(RDP) · 캡처·인코더 스위트.
**별건으로 유지(고치지 않음)**: (a) `GNLinkUpdater.exe` 가 manifest 에는 있으나 `payloadNames` 에 없어 **staging 까지 받아 놓고 영영 설치 디렉터리로 옮겨지지 않는다** — 업데이터 자신은 갱신되지 않는다. (b) ready 전 필수목록 검사 누락. (c) 한글 UI 깨짐.

### 501) 2026-09-10 전달 경로 3건 (5·6·7) — **업데이터가 자기 자신을 한 번도 갱신한 적이 없다**
검증용 승인 범위 7항목 중 **창이 필요 없는 3건**을 먼저 배선했다(RDP 활성이라 창 회귀는 콘솔 재접속 뒤).

**(5) 목록이 하나였던 것이 문제였다** (`update_process_targets.{hpp,cpp}` · `updater_effects.cpp:293`)
`payloadNames = product_image_names() + ui 2개` 였고, `product_image_names()` 는 **정지 목록**이다. 주석은 *"the set that is stopped and the set that is replaced cannot drift apart"* 라며 **일부러 같은 목록**을 썼다.
- ⚠️ **그 공유가 드리프트를 막지 못했다.** `GNLinkUpdater.exe` 가 **양쪽 모두에 없어서**, **어떤 릴리스도 업데이터를 교체한 적이 없다.** 업데이터 코드를 고쳐도 **인앱으로 전달되지 않는다** — Codex 질문의 답이 *"bootstrap 수동 설치 필요"* 인 이유다.
- 두 목록은 **다른 질문**에 답한다: *"교체하는 동안 실행 중이면 안 되는 것"* vs *"교체하는 것"*. 답이 다른 파일이 **정확히 하나** 있다.
- → `product_payload_names()` 신설, **정지 목록에서 파생**(`+ Updater + ui 2개`). 진짜 불변식은 *"정지 목록 ⊆ 교체 목록"* 이고 `product_payload_list_contract()` 가 그것을 단정한다. **같은 목록이 아니라 파생**이라 드리프트 방지는 유지된다.
- ⚠️ **업데이터를 정지 목록에 넣지 않았다.** 실행 중인 업데이터는 `installDir` 밖 working copy 이고(로그 `handed the update to the working copy`), 그 이름을 쓰는 다른 무엇은 우리가 닫을 것이 아니다. `validate()` 는 **이름이 아니라 목적지 전체 경로**로 비교하므로(`update_effects.cpp:145`) 통과한다 — 설계 주석이 *"업데이터는 교체 가능해야 한다"* 고 이미 적어 두었다.

**(6) 스왑에서야 알던 것을 다운로드 직후에** (`update_effects.cpp` `VerifyDownload`)
`Swap` 은 `payloadNames` 가 staging 에 없으면 거부한다 — **그런데 그때는 이미 제품에 종료를 요청한 뒤**고 사용자는 닫힌 앱을 보고 있다. 0.2.109 가 정확히 그랬다.
→ `VerifyDownload` 에서 **manifest 가 교체 대상 전부를 지명하는지** 검사한다. **디스크를 건드리기 전, 아무것도 닫기 전**이다.

**(7) 게시 전에 거부한다** (`automation/gnlink_check_payload_set.py`, preflight 배선)
기대 목록을 **`product_payload_names()` 에서 읽는다**(옮겨 적지 않는다 — 두 번째 사본이 곧 드리프트다).
- ⭐ **현장을 깨뜨린 그 manifest 로 음성 대조**: `.claude/rel/0.2.109/windows.manifest` → **REFUSING: the manifest does not name GNLinkSetup.exe.** 이 검사가 있었으면 **게시 전에 걸렸다.**

**회귀가 잡은 결함 2건 (둘 다 "통과하고 있었지만 아무것도 검사하지 않던" 것)**
1. `update_effects_test.cpp` 의 post-swap 롤백 케이스가 **불가능한 릴리스를 기술하고 있었다** — `payloadNames` 에 `GNLinkSetup.exe` 를 넣고 manifest 에는 안 넣었다. **게시된 0.2.109 와 같은 모양**이다. 아무도 검사하지 않아서 통과했다. manifest 에 지명하도록 고쳤다.
2. `gnlink_deploy_test.sh` 의 **서명 변조가 변조를 하지 않고 있었다** — `sed 's/^0/1/'` 인데 새 서명은 `8` 로 시작해 **아무것도 바꾸지 않았고**, 그 케이스는 **멀쩡한 릴리스를 게시하면서 "거부한다" 로 통과**했다. 첫 글자를 실제 값과 무관하게 바꾸고, **바뀌었는지 자체를 단정**하도록 고쳤다. 아티팩트 개수도 **manifest 에서 세도록** 바꿨다(하드코딩 9 는 10번째가 생긴 순간 조용히 틀렸다).

**결과**: `update_effects` **203** · `updater_assembly` **49** · `updater_scenarios` **142** · `update_release` **80** · `update_manifest` **85** · `update_relaunch` **107** · `payload_name` **54**, 전부 rc=0. `gnlink_deploy_test.sh` **PASS**. Debug 전체 빌드 rc=0.
**아직 안 한 것 (1·2·3·4)**: supervisor 우선 종료 · captured handle bounded wait · 부모 최종 exit 후 relaunch 판정 · SCM 분기와 AccessDenied false positive. **반례는 실제 windowed parent + console child 가 필요하고 지금 RDP 활성이라 실행하지 않는다.**
**하지 않은 것**: 새 자동 버전 게시 없음 · live 앱 설치·종료 없음 · `git push` 없음 · 영구 Host 껍데기/서비스 재설계 없음.

### 502) 2026-09-10 🔴 정정 — **"실패 지점이 앞으로 이동했다" 는 틀렸다. 뒤로 물러난 것이다**
#499·#500 과 사용자 보고에 *"게시 복구가 유효했고 실패 지점이 이동했다"* 고 적었다. **틀렸다.** 검증용이 짚었고 코드로 확인했다.

```
update_state_machine.cpp:199  PrepareForSwap()   ← 18:56 여기서 실패 (could not ask pid ... to stop, :428)
update_state_machine.cpp:209  Quiesce()
update_state_machine.cpp:219  Swap()             ← 15:45 여기까지 갔다 (the staged release does not contain, :492)
```
- **15:45 이 더 깊이 갔고, 18:56 은 더 얕은 곳에서 죽었다.** 차이는 수정 효과가 아니라 **18:56 에 창 없는 child 가 살아 있었다**는 것뿐이다.
- ⚠️ **따라서 `updater.log` 는 Setup 복구가 통했다는 증거를 전혀 주지 않는다.** 18:56 은 그 검사가 있는 단계에 **도달조차 못했다.**
- ⚠️ **`version verified: 0.2.109` 도 required-set 통과 근거가 아니다** — 서명 검증과 `Download`/`VerifyDownload` 까지의 증거일 뿐이다.
- **게시측 required 충족의 근거는 로그가 아니라 별도다**: manifest 10줄 대 `payloadNames` 9개 **기계 대조(MISSING none)** 와 **공개 URL 10/10 해시**. 이건 실기 로그와 무관하게 유효하다.

**내가 왜 틀렸나** — 두 오류 문구가 **다른 단계**에서 나온다는 것을 확인하지 않고 **시각 순서만 보고 "앞으로 갔다" 고 읽었다.** 로그 두 줄의 시각 차이는 단계의 깊이를 말해 주지 않는다. **문자열이 어느 함수에 있는지 먼저 봤어야 했다** — 실제로 보니 한쪽은 `:428`, 다른 쪽은 `:492` 이고 호출 순서는 그 반대였다.
⚠️ 검증용도 같은 오류를 냈고 스스로 정정했다. **둘이 같은 방향으로 틀리면 서로가 근거처럼 보인다.**

**그래서 남는 상태**: 0.2.109 게시 복구는 **여전히 유효**하다(위 별도 근거로). 다만 **실기에서 그것이 통했다는 증거는 아직 없다** — 인앱 업데이트가 그 단계까지 가 본 적이 없기 때문이다.

### 503) 2026-09-10 🔴 실기 실패의 원인 — **창 없는 자식에게 직접 요청하고 있었다** (항목 1·2, 5 회귀)
`AbandonedBeforeSwap -- could not ask pid N to stop`. 그 한 줄이 인앱 업데이트를 **구조적으로** 막고 있었다.

**왜 성공할 수 없었나**
```cpp
update_process_targets.cpp  request_process_stop
  EnumWindows → WM_CLOSE ; if (posted) return true;                  // 창 있는 것만
  if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, (DWORD)pid)) ...     // 2번째 인자는 process GROUP id
  return false;
```
- ⚠️ **`GenerateConsoleCtrlEvent` 의 2번째 인자는 pid 가 아니라 process group id 다.** 그 pid 가 **그룹 리더**이면서 **업데이터와 콘솔을 공유**해야만 성공한다. supervisor 는 자식을 그렇게 띄우지 않는다.
- **실패하는 경로가 아니라 성공 사례가 없는 경로**였다. 코드 주석이 이미 *"which the product's own supervisor does not guarantee"* 라고 적어 두었다.
- `PrepareForSwap` 은 하나라도 실패하면 즉시 abort 하므로, **창 없는 자식(Stream·Capture)이 살아 있는 한 업데이트는 불가능**했다.

**🔴 왜 테스트가 이걸 못 잡았나 — 이번 건의 진짜 교훈** (검증용 확인)
`request_process_stop` 의 호출자는 **제품에 1곳, 테스트에 0곳**이다. 테스트의 `requestStop = ` 대입 **11곳이 전부 람다 stub** 이고, "실패 케이스" 라는 것조차 **stub 이 false 를 돌려줄 뿐**이었다. 즉 **실제로 실패하는 분기는 한 번도 실행된 적이 없다.** `update_effects_test.cpp:8` 이 스스로 *"request_process_stop do not exist here"* 라고 적어 두었다.
⚠️ **CMake 링크 추가는 실호출 증거가 아니다.** 스위트 총합이 초록인 것과 그 코드가 돌아본 적 있는 것은 다른 명제다.

**고친 것 (1·2)**
- `ProcessTarget` += `parentPid` · `hasWindow`(열거 시 채운다).
- `PrepareForSwap` — **창 없는 자식은 직접 요청하지 않는다.** 창 있는 supervisor 에게만 요청하고 자식은 supervisor 가 정리하게 둔다.
  ⚠️ **소유는 추정하지 않는다**: 부모가 **대상 목록에 있고 · 창이 있고 · 자식보다 먼저 시작**했을 때만 인정한다. 셋 중 하나라도 없으면 그 프로세스는 **직접 요청 대상**으로 남는다(임의 그룹핑 금지).
- `Quiesce` — **`PrepareForSwap` 이 본 목록**으로 기다린다(재열거 아님). 재열거는 *"그 사이 시작된 것을 기다리고, 이미 요청받고 나가는 중인 것은 확인조차 안 하는"* 결과였다.
- ⚠️ **`OpenProcess` 의 `ACCESS_DENIED` 를 "종료됨" 으로 보지 않는다.** 그건 *"모른다"* 이고, 모르는 것을 죽었다고 읽으면 **파일을 쥔 프로세스 위로 업데이트가 진행된다.** 명시적 실패로 끝낸다.
- 실패 사유를 **분리**: `did not exit`(요청 못 함) vs **`outlived its parent`**(supervisor 는 갔는데 자식이 남음). 뒤엣것만 계약 위반이다.

**반례 — 실제 프로세스, stub 없음** (`update_stop_process_test`, 신규 **13 PASS**)
창 있는 부모(창은 만들되 **보여주지 않는다** — 데스크톱에 UI 를 띄우지 않는다) + **창 없고 콘솔 없는** 자식. 제품의 `enumerate_product_processes` · `request_process_stop` · `PrepareForSwap` · `Quiesce` 를 **그대로** 호출한다.
- `asking the windowless child directly FAILS -- this is the field failure`
- `asking every target directly -- the old routing -- fails  could not ask pid 6696 to stop` ← **현장 로그와 같은 문구**
- `PrepareForSwap succeeds with a windowless child present` · `Quiesce sees both processes actually exit`
- **음성 대조**: 라우팅 한 블록을 빼면 위 두 줄이 FAIL 하고 **`could not ask pid N to stop`** 이 그대로 재현되며 프로세스가 남는다.
- ⚠️ 처음 판에서 `check` 의 detail 이 대기와 **다른 시점**을 다시 읽어 *"PASS … 1 left"* 를 찍었다. 한 번 읽어 그 값을 보고하도록 고쳤다 — **판정과 근거가 다른 순간이면 근거가 아니다.**

**5 의 회귀 (검증용 NEEDS_CHANGES 반영)**
지적: *"`GNLinkUpdater.exe` 가 목록에 있다"* 는 단정만 있고 **실제로 교체되는지 확인한 테스트가 0개**였다. **이번 원인 축과 같은 모양**(실행 대신 형태를 단정)이다.
- `updater-member` 케이스 추가: staging 에 새 바이트 → **Swap → 설치본 `GNLinkUpdater.exe` 의 바이트가 실제로 바뀌고 `.gnlink-old` 백업이 생긴다.**
- 반대 방향도 단정: **설치 디렉터리 안에서 실행되는 업데이터는 거부**(`a payload destination is the running updater`). 안 그러면 첫 단정이 *"검사하는 게 없어서"* 통과할 수 있다.
- `updater_assembly_test` fixture 가 **production 과 다른 목록**(Updater 빠진)으로 조립을 돌리고 있었다 → `product_payload_names()` 로 정렬.

**결과**: 콘솔 세션에서 **64 스위트 `^PASS` 1811 · 실패 0**. `update_effects` 210 · `updater_assembly` 49 · `update_stop_process` 13.
⭐ **#497 의 빨간 4개가 여기서 닫힌다** — 같은 바이너리를 **콘솔에서** 재실행하니 전부 통과했고, 계속 빨갛던 `udp_control_e2e` 도 통과했다. 그때 "강한 후보" 로만 적었던 것이 **재실행으로 확정**됐다.
⚠️ 두 sweep 이 겹쳐 돌아 앞 실행이 어느 바이너리를 썼는지는 확정할 수 없다. **위 수치는 새 단정이 들어 있음을 직접 확인한 빌드**(`update_effects` 210 · `stop_process` 13)의 것이다.

**아직 안 한 것 (3·4)**: 부모의 최종 exit 확정 뒤 relaunch 판정(`update_relaunch.cpp` 의 *"still running -- nothing to bring back"*) · 서비스 stop 의 SCM 분기와 `AccessDenied` false positive.
**하지 않은 것**: 새 자동 버전 게시 없음 · live 앱 설치·종료 없음 · `git push` 없음 · Host 아키텍처 재설계 없음 · `TerminateProcess` 승격 없음 · 광역 kill 없음.

### 504) 2026-09-10 항목 3·4 — **"모른다" 를 "괜찮다" 로 읽던 자리 두 곳**
7항목 배선 완료. 두 결함 모두 *판정 불가를 조용히 통과시키던* 같은 형태다.

**(3) 닫히는 중과 거부는 다르다** (`update_relaunch.cpp`)
현장 로그가 전부 말해 준다:
```
18:56:20.266  the waiting caller acknowledged and is standing down
18:56:20.275  relaunch (required) GNLinkHost.exe: still running -- nothing to bring back   ← 9ms 뒤
```
- 종료를 **수락하고 내려가는 중**인 호스트를 **9밀리초 뒤**에 한 번 읽고 *"살아 있으니 되돌릴 것이 없다"* 로 판정했다. 호스트는 곧 스스로 닫혔고 **남는 것이 없었다.**
- ⚠️ **한 번만 물으면 "닫히는 중" 과 "거부" 는 구분되지 않는다.** 그런데 여기 있는 것들은 **전부 종료를 요청받은** 프로세스다 — 그 상태에서 Running 은 절반이 정상 진행이다.
- → `RelaunchConfig::closingGraceMs`. Running 이면 **정해진 시간 동안 다시 묻는다**(production 15초).
- ⚠️ **소진은 종료가 아니다.** 시간이 다 되어도 여전히 Running 이면 Running 이고, relaunch 는 *"정말로 아직 있다"* 는 이유로 건너뛴다. 반대편(E10c)을 함께 단정해 두지 않으면 이 대기는 *"기다렸으니 갔겠지"* 로 읽힐 수 있다.
- 기본값은 **0** 이다: 고정 답을 주입하는 테스트가 상수 하나 때문에 실제 timeout 을 물게 할 이유가 없다. **production 이 설정한다**(`updater_effects.cpp`).
- 회귀 `update_relaunch_test` **107 → 114**: `E10b` 닫히는 중이면 **실제로 되살아난다**(`4 liveness reads` — 한 번에 알 수 없었다는 증거) · `E10c` 유예가 소진돼도 **시작하지 않고 alreadyRunning 으로 남는다**.

**(4) `OpenProcess` 실패를 "이미 종료됨" 으로 세고 있었다** (`update_process_targets.cpp`)
```cpp
HANDLE held = OpenProcess(...);
if (!held) return true;  // already gone; nothing to ask     ← ACCESS_DENIED 도 여기로 들어왔다
```
- ⚠️ **열 수 없는 것은 죽은 것이 아니라 볼 수 없는 것이다.** 그것을 *"요청 성공"* 으로 세면 **교체할 파일을 아직 쥐고 있을지 모르는 프로세스 위로** swap 이 진행된다.
- → `ERROR_INVALID_PARAMETER`(pid 가 더 이상 프로세스가 아님) 만 성공, `ERROR_ACCESS_DENIED` 는 **실패**. 실패는 디스크를 건드리기 전에 abandon 으로 이어지므로 안전한 방향이다.
- **서비스는 SCM 으로 요청한다.** `GNLinkInputService.exe` 는 창도 콘솔도 없어 아래 두 방식이 **구조적으로 적용 불가**이고, 대신 진짜 supervisor 가 있다 — SCM 이다. `ControlService(SERVICE_CONTROL_STOP)`. 미설치(`ERROR_SERVICE_DOES_NOT_EXIST`)·이미 정지(`ERROR_SERVICE_NOT_ACTIVE`)는 성공, **열지도 제어하지도 못하면 실패**(여기서도 세 답을 둘로 줄이지 않는다).
- 회귀 `update_stop_process_test` **13 → 15**: 실제로 종료된 pid → **성공** · pid 4(System, 열 수 없음) → **실패**. 둘이 같은 답이던 것이 결함이었으므로 **양쪽을 함께** 단정한다.

**전체**: 콘솔 세션에서 **64 스위트 `^PASS` 1820 · 실패 0**. 증분 **1811 → 1820 = +9**(relaunch +7, stop_process +2)로 정확히 맞는다.
**7항목 상태**: 1 supervisor 우선 종료 ✅ · 2 captured handle bounded wait ✅ · 3 부모 최종 exit 뒤 relaunch 판정 ✅ · 4 SCM 분기 + AccessDenied ✅ · 5 Updater 자기 교체(+실제 swap 회귀) ✅ · 6 ready 전 required-set ✅ · 7 게시 전 차집합 거부 ✅
**하지 않은 것**: 새 자동 버전 게시 없음 · live 앱 설치·종료 없음 · `git push` 없음 · `TerminateProcess` 승격 없음 · 광역 kill 없음 · Host 아키텍처 재설계 없음.
**남은 미검증**: 실기 완주(0.2.108 호스트가 실제로 이 경로로 10파일 교체) · 언인스톨 항목 실제 갱신 · **현재 설치된 업데이터와 동일한 실행 조건의 10파일 업데이트**(설치된 0.2.108 업데이터에는 이 수정이 없다 — bootstrap 수동 설치 필요).

### 505) 2026-09-11 1~7 **구현 OK** — 그리고 **총합은 애초에 나쁜 계기였다**
검증용이 `4bb6e64` 를 독립 검사하고 OK. 자기 손으로 HEAD 재빌드 후 실행했고, 구 라우팅 재현·실제 바이트 교체·AccessDenied 양방향을 **코드 줄 번호로** 확인했다.

**🔴 총합이 맞지 않았고, 원인을 찾다가 더 중요한 것을 알게 됐다**
내 측정 **1820**, 검증용 **1823**. 항목으로 쪼개려고 **스위트별 수치를 처음으로 기록**했다(`.claude/sweep-per-suite.txt`). 거기서 드러난 것:
```
remote60_gdi_capture_process_test        0 rc=0      ← PASS 줄을 내지 않는다
remote60_host_bgra_scale_test            0 rc=0
remote60_host_encode_epoch_test          0 rc=0
remote60_viewer_udp_recovery_test        0 rc=0
remote60_capture_cadence_gate_test       0 rc=0
```
- ⚠️ **스위트 5개가 `^PASS` 를 한 줄도 내지 않는다.** 출력 형식이 다르다. 즉 **그 다섯은 총합에 0으로 들어가고, 통과하든 실패하든 총합이 똑같다.**
- 그래서 *"64 스위트 1820, 실패 0"* 에서 **"실패 0" 은 `rc` 로 판정한 것이고 1820 과는 무관**하다. 두 숫자가 한 문장에 있으면 한 근거처럼 읽히는데 **다른 계기**다.
- ⚠️ #497 에서 *"빨간 4개"* 가 총합에 영향을 주지 않았던 이유도 이것이다. 그때는 *"이번 변경 이전부터 red"* 라고만 적고 **왜 총합이 안 변했는지**는 묻지 않았다.
- **1820 vs 1823 의 +3 은 여전히 미해결이다.** 내 `update_manifest_test` 는 85 로 검증용과 같았다. 쪼갤 수 있는 자료(스위트별 수치)는 이제 있고, 검증용의 같은 자료와 맞춰 봐야 한다. **재현되지 않는 총합을 근거로 쓰지 않는다** — 이번 판정의 근거는 총합이 아니라 **실행 수준 단정**이다.

**환경 실패 1건 (검증용 실행에서만)**
`gdi_capture_process_test` rc=1, `GDI_DELIVERED_FPS=40.65`(요구 50). 내 실행에서는 **rc=0** 이었다.
- 🔴 **여기 처음 적었던 "live 호스트 캡처 경합" 은 원인이 아니었다.** 검증용이 정정했다 — 사용자가 GNLink 를 종료한 뒤(프로세스 0개) 재실행해도 **여전히 실패**(42.99 / 46.99 fps). #506 참조.

**남은 미검증 (완료조건 3)**
**현재 설치된 업데이터와 동일 조건의 실제 10파일 업데이트 — 미실행.** 설치된 0.2.108 업데이터에는 이 수정이 **하나도 없고**(5번이 바로 그 이유), 그 조건은 **bootstrap 수동 설치가 선행돼야만** 만들어진다. **구조적 의존이지 누락이 아니다.** 스위트 총합으로 대체하지 않는다.
그 외: 실기 완주 · 언인스톨 항목 실제 갱신 · 위 gdi 환경 실패.

**⚠️ 사용자 안내에 반드시 들어가야 할 순서**
**새 버전을 게시해도 현재 설치된 0.2.108 업데이터로는 그 수정이 실행되지 않는다.** **수동 설치 1회가 선행**돼야 그 다음부터 인앱 업데이트가 이 코드로 돈다. 그전까지 **인앱 재시도 안내 금지.**

### 506) 2026-09-11 🔴 `gdi_capture_process` — 경합도 RDP 도 아니었다. **격리된 캡처 프로세스만 10배 느리다**
#505 에 *"live 호스트가 같은 화면을 캡처 중이라 경합"* 이라고 적었다. **틀렸다.** 검증용이 사용자가 GNLink 를 종료한 상태(프로세스 0개, 콘솔 세션)에서 재실행했고 **두 번 다 실패**했다: **42.9993 fps · 46.9969 fps**(요구 50).

**내가 직접 돌린 것 — 같은 조건에서 통과했다**
```
GNLink processes: 0     >console  shotan  1  Active
GDI_DELIVERED_FPS=56.6656   rc=0   RESULT: ALL PASS
GDI_CAPTURE_COPY_AVG_US=17644     GDI_PARENT_COPY_AVG_US=1798
```
- ⚠️ **같은 조건에서 검증용은 두 번 실패하고 나는 통과했다.** 그러니 이 스위트는 **50fps 문턱 근처에서 흔들린다** — 판정이 그날 그 순간에 달려 있다.

**실제 신호는 문턱이 아니라 복사 시간이다** (검증용 분석, 내 수치로도 성립)
| | capture 프로세스 | 부모 |
|---|---|---|
| 검증용 | 21,170~23,290 µs | 2,412~2,477 µs |
| 나 | **17,644 µs** | **1,798 µs** |
- `1/0.017644 = 56.7fps` — **측정 fps 와 소수점까지 맞는다.** 검증용 쪽도 `1/0.0233 = 42.9`. 즉 **상한은 화면 갱신률이 아니라 복사 시간**이다.
- ⚠️ **같은 복사를 부모는 10분의 1 시간에 한다.** 격리된 캡처 프로세스만 **약 10배 느리다.** 이 격차가 이 테스트가 실제로 관찰하고 있는 값이고, **설명되지 않았다.**
- 이 PC 의 디스플레이 어댑터가 셋이다: `Parsec Virtual Display Adapter`(갱신률 보고 없음) · `Virtual Display Driver`(60Hz) · `AMD Radeon(TM) Graphics`(59Hz). `CLAUDE.md` 의 RDP 예외(Remote Display Adapter 32Hz)와 **같은 계열일 수 있으나 추정이고 단정하지 않는다.**

**판정**
- **이번 3커밋과 무관**하다 — 건드린 파일에 gdi/capture 관련 **0개**.
- ⚠️ **"환경 실패이므로 무시" 가 아니라 "원인 미규명, 별건"** 이다. **RDP 도 아니고 GNLink 경합도 아닌 상태에서 재현**되므로 `CLAUDE.md` 의 기존 RDP 예외로 **설명되지 않는다.**
- 작업목록에 별건으로 추가. 우선순위는 인앱 업데이트 건 **아래**.

**교훈**: 처음 붙인 설명(경합)은 **그럴듯했고 확인 가능한데 확인하지 않았다.** 확인 방법(호스트 정지 후 재실행)이 금지 사항이라 미검증으로 남긴 것까지는 맞았는데, 그 사이 **사용자가 종료해 조건이 생겼을 때** 다시 물은 것은 검증용이었다. **미검증으로 남긴 항목은 조건이 바뀌면 다시 물어야 한다.**
