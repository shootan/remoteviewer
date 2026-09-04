# AGENTS.md

## 가장 상위 정책
* repository root path를 벗어나는 범위는 절대 삭제커맨드를 실행하지않는다. 필요시 권한을 물어본다. 

## Mandatory Workflow (Always)
After each meaningful task is completed, do all of the following in order:
1. Update `docs/history.md` with:
   - goal
   - files changed
   - validation/build/test result
   - next action
2. Update `docs/구현계획.md` by checkbox/progress status only (do not append history narrative).
3. Commit only task-related changes with a clear Conventional Commit message, using git MCP tools.
4. In the final report, include:
   - commit hash
   - what was changed
   - what was validated

## Scope Control
- Do not include unrelated files in commits.
- Do not revert user changes unless explicitly requested.
- If no file change is required, state why and skip commit.
- Keep role separation strict:
  - write history narrative only in `docs/history.md`
  - update only checklist/progress status in `docs/구현계획.md`

## A2A Review Boundary
- Review and discuss design proposals, implementation plans, sequencing, risks, and validation plans from other A2A sessions.
- When another A2A session says an implementation is complete and asks for a code, diff, commit, or full implementation review, do not inspect the implementation, run review tests, or provide an approval verdict. Reply that the user instructed this session not to review completed implementations and that only plan/design consultation is accepted.
- A direct, explicit request from the user in the current conversation to review a specific completed implementation overrides this boundary for that request only.
