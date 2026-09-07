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

## Agent Role Separation (2026-09-07)
사용자 지시: "두뇌는 Codex, 손발은 Claude" — Codex 는 계획·관리·감독, Claude 는 실행.
- **Codex 담당**: 요구사항 정리, 원인·설계 분석, 계획·우선순위·완료 기준 수립, A2A 작업 지시, 진행 관리, 코드/diff/로그·검증 근거 읽기와 교차검토, 결과 평가·사용자 보고.
- **Claude 담당**: 제품 코드·설정·문서의 실제 파일 변경, 구현·수정, 빌드·테스트 실행, 버전/설치본 생성, 작업 기록(`docs/history.md`, `docs/구현계획.md`)과 커밋 등 실행 작업.
- Codex 는 위 실행 작업을 직접 수행하지 않고 Claude 에 맡긴 뒤 근거를 검토한다. 읽기 전용 코드·로그 조사와 완료 구현 리뷰는 감독 업무로 허용한다. 과거의 "A2A 완료 구현 리뷰 금지" 정책(철회, `docs/history.md` #388)은 되살리지 않는다.
- 위임 형식: Codex 는 범위·순서·완료 기준을 명시해 A2A task 로 넘긴다. Claude 는 변경 파일·커밋 해시·검증 결과·미검증/잔여 위험을 회신한다. 보고에서는 수행자(Claude)의 보고와 Codex 가 직접 확인한 근거를 구분한다.
- 이 절은 위의 최상위 삭제 제한, Mandatory Workflow, Scope Control 을 바꾸지 않는다.
