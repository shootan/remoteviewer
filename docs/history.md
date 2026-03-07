# remote60 작업 히스토리 (NEW)

업데이트: 2026-03-05

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
