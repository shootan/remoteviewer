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
