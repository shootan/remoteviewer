### 398) 2026-09-07 AGENTS.md 역할 분담 명문화 — Codex 계획·감독, Claude 실행 (A2A task t-7w636lty)
- 목표: 사용자가 Codex 대화에서 지시한 "두뇌는 Codex, 손발은 Claude" 역할 분담을 `AGENTS.md` 의 별도 절(Agent Role Separation)로 좁게 명문화한다. 제품 코드·버전·설치본은 변경하지 않는다.
- 변경 파일: `AGENTS.md`(절 추가; 최상위 삭제 제한·Mandatory Workflow·Scope Control 불변), `docs/history/history_2026-W36.md`(정책 기록·상태), `docs/구현계획.md`(에이전트 운영 정책 상태 1줄), `docs/history.md`(이 항목).
- 내용: Codex = 요구사항 정리·원인/설계 분석·계획/우선순위/완료 기준·A2A 작업 지시·진행 관리·코드/diff/로그 근거 읽기와 교차검토·결과 평가·사용자 보고. Claude = 파일 변경·구현·빌드/테스트·버전/설치본·작업 기록·커밋. Codex 는 실행 작업을 직접 하지 않되 읽기 전용 조사와 완료 구현 리뷰는 감독 업무로 허용(A2A 완료 구현 리뷰 금지 정책은 철회 유지). 위임 시 범위·순서·완료 기준 명시, 회신 시 변경 파일·커밋·검증·미검증/잔여 위험, 수행자 보고와 Codex 직접 확인 근거 구분.
- 검증: diff 로 문구·범위 확인. 빌드·테스트 해당 없음(문서만). Codex(remote#wslm89zu)가 최종 문구를 읽어 확인 예정.
- push 범위(정책 스킬 `agents-policy-updater` 6단계 "push"): 브랜치 `refactor/viewer-split` 은 `origin/refactor/viewer-split`(b3fd92d) 보다 이 커밋 포함 11 커밋 앞서 있어 push 하면 0.2.99~0.2.100 제품 커밋까지 함께 공개된다. 문서 커밋만 따로 push 할 수 없으므로 push 는 사용자/Codex 확인 후로 남김(이 작업에서 미실행). Git MCP 도구는 이 세션에 없어 git CLI 로 커밋.
