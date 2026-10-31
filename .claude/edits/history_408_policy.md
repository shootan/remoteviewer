### 408) 2026-09-07 정책 문서 — 3역할 절차(Codex 계획 → 검증용 Claude 검토·확정 → 작업용 Claude 구현 → 검증용 검사·OK → Codex 최종 확인) 명문화 (A2A task t-5ahw0t5x 계획 A, 검증용 remote#1528xbin 위임)
- 목표: 사용자가 확정한 새 절차를 `AGENTS.md`(공통 역할·흐름)와 `CLAUDE.md`(두 Claude 세션의 구체 행동·보고 기준)에 반영한다. 배경: 2026-09-07 P9/P11 후속 작업에서 작업 세션이 검증 없이 완료 보고·릴리스를 반복한 문제와 GMux 재시작 체크포인트(`.claude/gmux_restart_checkpoint.md`).
- 변경 파일: `AGENTS.md` — "Agent Role Separation (2026-09-07, 3역할 절차)" 절을 다시 씀(Codex/검증용/작업용 담당, 완료 판정 규칙, 역할 식별은 버스 displayName + `a2a_whoami`, ephemeral ID 기재 금지, "Claude 담당: … 커밋" 을 작업용으로 한정, 과거 "A2A 완료 구현 리뷰 금지" 미부활, 최상위 삭제 제한·Mandatory Workflow·Scope Control 불변). `CLAUDE.md` — "3역할 절차에서 두 Claude 세션의 행동·보고 기준" 절 추가(검증용의 "구현 OK" 고정 형식: commit 해시·검토 범위·직접 실행/보고 구분·산출물 파일명·크기·sha256·빌드 commit·미검증 한계; OK 뒤 변경 시 재검사; 작업용의 완료 검사 요청 형식·working 유지·단독 PASS 로 회귀 FAIL 상쇄 금지). 기존 응답·RDP·NAS 로그 규칙 유지.
- 검증: 문서 변경만(빌드/테스트 없음). `docs/구현계획.md` 변경 없음(정책 항목 없음).
- 다음: 계획 B(S13 held-resume pending 회귀 수정, 0.2.103 후보) — 같은 task.
