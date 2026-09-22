# Docs Work Rules

## Read Order (Mandatory)
1. Read `docs/README.md` first (this file).
2. Read `docs/작업목록.md` — what is left, priority-ordered. Start here.
3. Read `docs/구현계획.md` only for the detail of an item you are about to work on.
4. Read `docs/history.md` for recent context only (newest entries at the bottom).

## Layout
- Active docs live directly under `docs/`:
  - `작업목록.md` — the remaining work, priority-ordered (verification waits / code / deploy decisions / record debt / backlog).
  - `구현계획.md` — per-item detail and checkboxes. Large; read the section you need.
  - `history.md` — running work log (append only).
  - `ui_state_table.md` — the PC client/viewer UI state table and its evidence grades.
  - `업데이트_기능_설계.md` — in-app update design (manifest, signing, state machine).
  - `update_field_retest_runbook.md` — in-app update field retest and recovery procedure.
  - `수동확인_체크리스트.md`, `external_wan_test_guide.md`, `성능기준선_60fps_20260804.md` — test aids.
  - `connection_version_logging.md` — the `[connection-version]` log contract (0.2.126+).
  - `OSLink_구조분석.md` — measured architecture of a competing remote tool; background only.
- `docs/history/` — weekly history splits.
- `docs/legacy/` — finished or superseded documents. `legacy/2026-09/` holds the September 2026
  stabilization round: field tests, incidents, audits, refactor plans and ledgers, release reviews.
  Index: `legacy/README.md`. Read only when an item in `작업목록.md` points there.

## History Rule
- Do **not** read `docs/legacy/history_old.md` or `docs/legacy/history_old2.md` by default.

## Execution Rule
- Follow `docs/작업목록.md` order. Do not start out-of-plan tasks unless the user changes priority.
- A finished task is removed from `작업목록.md` and recorded in `history.md`; a new task is added the moment it appears.

## After Work (Mandatory)
- Append a concise summary to `docs/history.md`.
- Update `docs/작업목록.md` (remove finished, add new) and the matching checkbox in `docs/구현계획.md`.
- Keep docs synchronized with actual code state before final report.
- End every reply with a "수행된 작업" summary (see repo-root `CLAUDE.md`).
