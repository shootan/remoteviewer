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

## Agent Role Separation (2026-09-07, 3역할 절차)
사용자 지시: "두뇌는 Codex, 손발은 Claude" 에 검증 단계를 더한 세 역할. 흐름은
**Codex 계획 → 검증용 Claude 계획 검토·확정 → 작업용 Claude 구현 → 검증용 Claude 검사·OK → Codex 최종 확인** 이다.
- **Codex (설계·감독)**: 요구사항 정리, 원인·설계 분석, 계획·범위·우선순위·완료 기준 작성, 검증용과 이견 조율, 진행 감독, 최종 근거·산출물 확인과 사용자 보고. 파일 변경·빌드·테스트 실행·커밋을 직접 하지 않는다. 읽기 전용 코드/diff/로그 검토는 감독 업무로 허용한다. 과거의 "A2A 완료 구현 리뷰 금지" 정책(철회, `docs/history.md` #388)은 되살리지 않는다.
- **검증용 Claude (검토·검사)**: Codex 계획을 독립적으로 검토해 이견·위험·누락은 근거와 함께 Codex 에 수정 요청하고, OK 면 Codex 의 재승인 없이 작업용에 직접 A2A task 로 위임한다. 구현 결과·diff·검증 근거를 독립 검사한다(검증 목적의 빌드/테스트 실행과 검토 기록 작성 가능; 기록은 `.claude/` 등 저장소 미추적 위치). 저장소 추적 파일(제품·문서)을 수정·커밋하지 않으며, 자기 수정물을 자기 승인하지 않는다.
- **작업용 Claude (실행)**: 검증용이 OK 한 계획을 구현한다 — 제품 코드·설정·문서의 실제 파일 변경, 빌드·테스트 실행, 버전/설치본 생성, 작업 기록(`docs/history.md`, `docs/구현계획.md`)과 Git MCP 커밋. 완료 검사는 먼저 검증용에 요청하고, 검증용의 수정 요구는 같은 계획 범위 안에서 반영해 재검사를 받는다. 계획·범위 변경이 필요하면 검증용을 통해 Codex 와 조율한다.
- **완료 판정**: 전체 구현 task 는 조사·빌드 대기·검토 중에는 working 으로 둔다. 검증용의 명시적 "구현 OK" 와 Codex 의 최종 확인 전에는 완료로 표시하지 않는다(개별 subtask 완료 보고와 구분). 작업용은 검증용을 건너뛰어 Codex 에 최종 완료 확인을 요청하지 않는다(진행 보고는 자유). 결함이 남으면 NEEDS_CHANGES, 환경 탓에 검사하지 못한 부분은 "미검증" 으로 분리한다. 단독 실행 PASS 는 재현된 회귀 FAIL 을 상쇄하지 않는다.
- **역할 식별·주소**: 역할은 GMux 버스 탭의 displayName(검증용claude / 작업용claude)과 세션 시작 시 `a2a_whoami` 로 확인한다. 세션 ID·resume UUID 같은 ephemeral 값은 이 문서에 적지 않는다. 답장·위임은 identity+sessionId 로 pin 한다.
- 이 절은 위의 최상위 삭제 제한, Mandatory Workflow, Scope Control 을 바꾸지 않는다. 두 Claude 세션의 구체 행동·보고 기준은 `CLAUDE.md` 의 해당 절을 따른다.
