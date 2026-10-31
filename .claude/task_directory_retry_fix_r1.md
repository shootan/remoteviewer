# task directory-retry-fix r1 — 상태 문구 진단 계약 + 시험 관찰 단계 보수 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용2

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 조사 task(t-azc166pv)와 별개의 구현 task. **차기 유지보수 후보 — 0.2.162 에 backport 하지 않고 새 버전도 정하지 않는다.**
설계 확정: `.claude/codex_directory_retry_flake_r1.md` — 구속 계약. 조사 근거: `.claude/report_directory_retry_flake_r1.md`.

## 범위 (A·B 함께, Codex 수정 조건)
- **A (제품 진단 문구)**: HTTP legacy 기본 포트 observe 시도는 **유지**. "health 광고 없음" 과 "그 기본 주소 관찰 timeout" **두 사실을 함께** 진단에 남긴다 — 서버 오설정으로 확정하거나 실제 timeout 사실을 지우지 않는다. HTTPS 주소 없음 / 명시 pin / 광고 timeout / legacy 실제 성공·온라인을 구분. 진단 출처·이유는 명시 값으로(bool/string 추측 금지). network·auth·ready·cooldown 동작 불변.
- **B (시험)**: 고정 15 s 뒤 단일 샘플 제거. 서버 Count 2 뒤 짧은 시간 창 읽기도 경합이라 불가 → **클라이언트의 해당 cycle·상태 전이 완료**를 관측(필요 시 test-only barrier). `[inactive]` 목표 3 / 기대 ≤2 모순은 관찰 단계 계약으로 해소 — 숫자 완화로 PASS 만들기 금지.
- 별도 후보(이번 범위 아님, 확대 금지): silent-host-loses-relay 는 실제 PunchAny(:638) 경유라 "hello 미도달" 로 확대하지 않는다.
- 시험: 핵심 대조·변이·bounded 반복만(성공까지 반복 금지). 원래 FAIL 근거는 보존.

## 규칙
새 worktree·브랜치(main 83cbd78 또는 5063cd9 기준, 예 `C:/work/gnlink-wt/fix-directory-retry`, `fix/directory-retry`). **repo root 밖 삭제 금지**(생성만), 설치본·서비스·세션 무조작, 버전·서명·게시·push 금지, 측정 슬롯 CreateNew. 커밋 Git MCP, history 항목은 task 커밋에.

## 보고 (1회)
`.claude/report_directory_retry_fix_r1.md` + `remote#wwml7h91` pin.
