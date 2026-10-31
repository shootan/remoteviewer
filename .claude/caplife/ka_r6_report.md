

## r6 — Codex N4 a/b (codex_keepalive_r2_spike_r5.md, 2026-10-27)
**고정: `spike/host-admission-r3` cf8fb70** (357ee5e 위 1 commit, `apps/directory/test/` 5파일만, 제품 변경 0).
- a) `test_tmp.root()`: `REMOTE60_TEST_TMP` 는 **신뢰 anchor `D:\remote\remote\.claude\test-tmp` 바로 아래 한 폴더**만 허용한다.
  - anchor 는 존재하고, 링크가 아니며, 실경로가 자기 자신이어야 한다(조상에 junction 없음).
  - 생성 전에 이름을 검사하고(한 단계 mkdir, anchor 안에서만), 생성 후에 실경로를 다시 검사한다.
  - 모양만 맞는 바깥 경로나 링크는 거부한다.
- b) child 4개(recovery_fault·relay_wire·account_admin·device_credential)는 `testTmp.scratchDir()` 로만 폴더를 받는다. raw 변수와 worktree fallback 을 없앴고, 정리는 `testTmp.remove()` 로 한다.
- 제한 대조(첫 쓰기 전 거부, 바깥 생성·삭제 없음):
  - ① 모양 맞는 바깥 루트 `C:\work\gnlink-wt\ka-r3\.claude\test-tmp\ka` → 거부, 생성 안 됨.
  - ② anchor 안 junction `ka-r6-link` → `D:\...\.claude\caplife` → "the test root is a link" 로 거부. junction 은 내가 저장소 안에 만들고 `rmdir` 로 링크만 지웠으며, 대상 caplife 는 온전하다.
  - ③ child 단독 실행(변수 없음) → rc1, worktree test-tmp 항목 12 → 12.
- 양성: anchor 루트 `ka-r6-n4` 로 recovery_fault ALL PASS · account_admin 113/0 · device_credential 83 · relay_wire(fixed) PASS. 815 전량은 지시대로 다시 돌리지 않았다.
- scratchX 는 그대로 둔다(사용자 판단 대기).
