# task directory-retry-flake r1 — directory_retry 간헐 FAIL 제한 조사 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용2

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 조사만 — 제품·시험 수정 금지(원인 근거 뒤 검증용→Codex).
근거: Codex `.claude/codex_hello_delay_fix_r1.md`(directory_retry 절), 검증용 기록 `C:/work/gnlink-wt/verify-hello/.verify/verify_hello_r1.md`: hello 후보 83c3ac5 에서 directory_retry 4회 중 1회 FAIL(132/133) — cached-silent Host agent 의 상태 문구가 "reason is about the server" 대신 "address observation timed out". 기준판 5063cd9 3/3 PASS 는 기존 결함 여부를 증명하지도 부정하지도 않음.

## 할 일 (제한 조사)
1. 해당 시험 시나리오 코드와 기대 판정을 읽고, RunHost 15 s + 300 ms **단일 상태 샘플**·health cooldown·HTTP 기본 observe 의미가 어떻게 겹쳐 문구가 갈리는지 근거(file:line)로 정리.
2. 기준판·후보에서 N회 반복해 분포를 얻되 **성공할 때까지 전량 반복·문구 완화로 PASS 만들기 금지**. 실패 회차의 시각선(observe 시작/timeout/샘플 시점)을 로그로 맞춤.
3. 결론: 시험 설계 결함인지, 제품 경합인지, hello 변경과의 관계(있다/없다/미확정) — 근거 수준 명시. 최소 수정안은 제안만.

## 규칙
측정 슬롯 CreateNew, 설치본·서비스·세션 무조작, **repo root 밖 삭제 금지**(새 worktree 생성만 허용). 보고 1회: `.claude/report_directory_retry_flake_r1.md` + 검증용 pin.
