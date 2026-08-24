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
