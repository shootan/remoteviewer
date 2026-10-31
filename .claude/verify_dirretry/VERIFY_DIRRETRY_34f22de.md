# 검증용 독립 검사 — directory-retry r2 (fix/directory-retry-r2 34f22de) (remote#7fsfy22z, 2026-10-28)

- 고정: worktree `C:/work/gnlink-wt/dirretry-r2` HEAD 34f22de(Codex SOURCE OK `codex_dirretry_src_r2.md`). 독립 빌드 `dirretry-r2/build-verify`(worktree 안, Release, 작업용 build 와 분리): retry_test 61ECF941…, mutant timeout_only A5F56320…, no_legacy_attempt 21500FAF…, no_cycle_gate B0A762D9….
- 환경: REMOTE60_TEST_TMP=`.claude/test-tmp/verify-dirretry`(미리 생성), 측정 슬롯 자기 생성분만.

| 실행 | rc | 결과 |
|---|---|---|
| retry_test ×3 | 0 ×3 | 177/0 ×3 (`all retry checks passed`) |
| mutant TIMEOUT_ONLY | 1 | 2 FAIL(기대 FAIL — 작업용과 같은 수) |
| mutant NO_LEGACY_ATTEMPT | 1 | 12 FAIL(기대 FAIL — 같은 수) |
| mutant NO_CYCLE_GATE | 1 | 4 FAIL(기대 FAIL — inactive-slow 2/1 등, Codex 재현과 같은 경계) |

- 포트 고정 장치 간헐(이전 run2)은 이번 3회에서 재발 없음(1회 통과로 닫지 않음 — 원인 실측·보수는 작업용 maint 보고 근거).
- 미검증: 실 directory 서버·필드 재시도 동작(운영 배포 전), migration 시험의 같은 T2(후속 후보).
