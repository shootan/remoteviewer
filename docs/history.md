# remote60 작업 히스토리 (NEW)

업데이트: 2026-03-16

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
