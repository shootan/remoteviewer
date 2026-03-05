# AGENTS.md

## Mandatory Workflow (Always)
After each meaningful task is completed, do all of the following in order:
1. Update `docs/history.md` with:
   - goal
   - files changed
   - validation/build/test result
   - next action
2. Update `docs/구현계획.md` with completed work/progress snapshot.
3. Commit only task-related changes with a clear Conventional Commit message, using git MCP tools.
4. In the final report, include:
   - commit hash
   - what was changed
   - what was validated

## Scope Control
- Do not include unrelated files in commits.
- Do not revert user changes unless explicitly requested.
- If no file change is required, state why and skip commit.
