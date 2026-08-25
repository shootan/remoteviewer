# Docs Work Rules

## Read Order (Mandatory)
1. Read `docs/README.md` first (this file).
2. Read `docs/HANDOFF.md` — current state, next actions, test gates, env switches.
3. Read `docs/구현계획.md` and execute work by the plan order.
4. Read `docs/history.md` for recent context only (newest entries at the bottom).

## Layout
- Active docs live directly under `docs/`:
  - `HANDOFF.md` — session handoff / current plan (start here).
  - `구현계획.md` — the plan + checklist.
  - `history.md` — running work log.
  - `external_wan_test_guide.md`, `성능기준선_60fps_20260804.md`, `수동확인_체크리스트.md` — active test aids.
- `docs/legacy/` — dated/superseded designs, reviews, and old history splits. Read only when
  explicitly needed for deep historical context; do not read by default.

## History Rule
- Do **not** read `docs/legacy/history_old.md` or `docs/legacy/history_old2.md` by default.

## Execution Rule
- Follow `docs/구현계획.md` milestones and priority exactly.
- Do not start out-of-plan tasks unless the user explicitly changes priority.

## After Work (Mandatory)
- Append a concise summary to `docs/history.md`.
- Update checkbox/progress status only in `docs/구현계획.md` for finished items (no history narrative append).
- Keep docs synchronized with actual code state before final report.
- End every reply with a "수행된 작업" summary (see repo-root `CLAUDE.md`).
