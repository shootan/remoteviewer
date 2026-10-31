import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'

edit(ROOT + r'\AGENTS.md', [
("""- Keep role separation strict:
  - write history narrative only in `docs/history.md`
  - update only checklist/progress status in `docs/구현계획.md`
""",
"""- Keep role separation strict:
  - write history narrative only in `docs/history.md`
  - update only checklist/progress status in `docs/구현계획.md`

## Agent Role Separation (2026-09-07)
사용자 지시: "두뇌는 Codex, 손발은 Claude" — Codex 는 계획·관리·감독, Claude 는 실행.
- **Codex 담당**: 요구사항 정리, 원인·설계 분석, 계획·우선순위·완료 기준 수립, A2A 작업 지시, 진행 관리, 코드/diff/로그·검증 근거 읽기와 교차검토, 결과 평가·사용자 보고.
- **Claude 담당**: 제품 코드·설정·문서의 실제 파일 변경, 구현·수정, 빌드·테스트 실행, 버전/설치본 생성, 작업 기록(`docs/history.md`, `docs/구현계획.md`)과 커밋 등 실행 작업.
- Codex 는 위 실행 작업을 직접 수행하지 않고 Claude 에 맡긴 뒤 근거를 검토한다. 읽기 전용 코드·로그 조사와 완료 구현 리뷰는 감독 업무로 허용한다. 과거의 "A2A 완료 구현 리뷰 금지" 정책(철회, `docs/history.md` #388)은 되살리지 않는다.
- 위임 형식: Codex 는 범위·순서·완료 기준을 명시해 A2A task 로 넘긴다. Claude 는 변경 파일·커밋 해시·검증 결과·미검증/잔여 위험을 회신한다. 보고에서는 수행자(Claude)의 보고와 Codex 가 직접 확인한 근거를 구분한다.
- 이 절은 위의 최상위 삭제 제한, Mandatory Workflow, Scope Control 을 바꾸지 않는다.
"""),
])

edit(ROOT + r'\docs\history\history_2026-W36.md', [
("""## 2026-09-05 — 정책 상태

- A2A 완료 구현 리뷰 금지: **철회**. 변경·검증 기록: `docs/history.md` #388.
""",
"""## 2026-09-05 — 정책 상태

- A2A 완료 구현 리뷰 금지: **철회**. 변경·검증 기록: `docs/history.md` #388.

## 2026-09-07 — Agent Role Separation: Codex 계획·감독, Claude 실행

- 사용자 지시("두뇌는 Codex, 손발은 Claude")를 `AGENTS.md` "Agent Role Separation" 절로 명문화. Codex = 요구사항·분석·계획·A2A 지시·진행 관리·근거 검토·보고, Claude = 파일 변경·구현·빌드·테스트·버전/설치본·기록·커밋.
- 읽기 전용 조사와 완료 구현 리뷰는 Codex 의 감독 업무로 허용(위 철회 상태 유지). 최상위 삭제 제한·Mandatory Workflow·Scope Control 불변.
- 상태: **적용**. 기록: `docs/history.md` #398 (A2A task t-7w636lty).
"""),
])

edit(ROOT + r'\docs\구현계획.md', [
("""## 에이전트 운영 정책

- [취소] A2A 설계·구현계획 상담만 허용하고, 다른 세션의 완료 구현·diff·커밋 리뷰 요청은 사용자 지시에 따라 거절
""",
"""## 에이전트 운영 정책

- [취소] A2A 설계·구현계획 상담만 허용하고, 다른 세션의 완료 구현·diff·커밋 리뷰 요청은 사용자 지시에 따라 거절
- [x] Agent Role Separation — Codex 계획·관리·감독 / Claude 실행(파일 변경·빌드·테스트·설치본·커밋). `AGENTS.md` 명문화 2026-09-07 (history #398)
"""),
])

entry = ROOT + r'\.claude\edits\history_398.md'
open(entry, 'wb').write("""### 398) 2026-09-07 AGENTS.md 역할 분담 명문화 — Codex 계획·감독, Claude 실행 (A2A task t-7w636lty)
- 목표: 사용자가 Codex 대화에서 지시한 "두뇌는 Codex, 손발은 Claude" 역할 분담을 `AGENTS.md` 의 별도 절(Agent Role Separation)로 좁게 명문화한다. 제품 코드·버전·설치본은 변경하지 않는다.
- 변경 파일: `AGENTS.md`(절 추가; 최상위 삭제 제한·Mandatory Workflow·Scope Control 불변), `docs/history/history_2026-W36.md`(정책 기록·상태), `docs/구현계획.md`(에이전트 운영 정책 상태 1줄), `docs/history.md`(이 항목).
- 내용: Codex = 요구사항 정리·원인/설계 분석·계획/우선순위/완료 기준·A2A 작업 지시·진행 관리·코드/diff/로그 근거 읽기와 교차검토·결과 평가·사용자 보고. Claude = 파일 변경·구현·빌드/테스트·버전/설치본·작업 기록·커밋. Codex 는 실행 작업을 직접 하지 않되 읽기 전용 조사와 완료 구현 리뷰는 감독 업무로 허용(A2A 완료 구현 리뷰 금지 정책은 철회 유지). 위임 시 범위·순서·완료 기준 명시, 회신 시 변경 파일·커밋·검증·미검증/잔여 위험, 수행자 보고와 Codex 직접 확인 근거 구분.
- 검증: diff 로 문구·범위 확인. 빌드·테스트 해당 없음(문서만). Codex(remote#wslm89zu)가 최종 문구를 읽어 확인 예정.
- push 범위(정책 스킬 `agents-policy-updater` 6단계 "push"): 브랜치 `refactor/viewer-split` 은 `origin/refactor/viewer-split`(b3fd92d) 보다 이 커밋 포함 11 커밋 앞서 있어 push 하면 0.2.99~0.2.100 제품 커밋까지 함께 공개된다. 문서 커밋만 따로 push 할 수 없으므로 push 는 사용자/Codex 확인 후로 남김(이 작업에서 미실행). Git MCP 도구는 이 세션에 없어 git CLI 로 커밋.
""".encode('utf-8'))
append_history(ROOT + r'\docs\history.md', entry)
