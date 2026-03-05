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
