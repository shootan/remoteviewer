# report dirretry-rebase r1 — directory-retry 보수를 R2 기준으로 이동 (작업용2, 2026-10-28)

task dirretry-rebase / r1 / kind=maintenance (t-r9vnrvfh).
- 원 보수: `fix/directory-retry` 0d4c27d. 보고는 `report_directory_retry_fix_r1.md`, 계약은 `codex_directory_retry_flake_r1.md`.

## 고정 후보
- **`fix/directory-retry-r2` af9a6af**. worktree `C:/work/gnlink-wt/dirretry-r2` 은 clean 이다. `git diff --check e16ad0d..af9a6af` rc 0.
  - 기준 **feat/np-early-relay e16ad0d** = ka-r3 0a5e71d + NP r1·r2.
  - d11ef62 ← d6bb951 (WIP 커밋 그대로)
  - af9a6af ← 0d4c27d
  - 둘 다 Git MCP 로 커밋했다. 메시지는 원문에 `(cherry picked from …)` 를 덧붙였다. af9a6af 는 이동·충돌 해소를 한 줄 덧붙였다.
  - cherry-pick 은 `-n` 으로 적용만 하고, 커밋은 MCP 로 했다.
- **변경**: 7 파일 +477/−18.
  - **`apps/` 변경은 원 보수(83cbd78..0d4c27d)와 +/- 줄이 완전히 같다**(diff 대조).
  - 차이는 `docs/history.md` 1줄 — 이동 기록이다.

## 충돌과 해소
- **제품 충돌 0.**
  - KA(0a5e71d)가 `directory_client.cpp` 에서 바꾼 곳: heartbeat 의 `hostProof` 파싱(:1392~), `Punch()` 의 peer 제외·hostProof 송신(:1430~1465).
  - directory-retry 의 hunk: `observe_port_for` 부근(:219), `Start`(:463~), health·apply(:1125~1240), `Run()` 루프(:1564~1745). **서로 겹치지 않는다.**
  - `directory_client.hpp`: retry 의 시험 hook 블록(`#ifdef REMOTE60_DIRECTORY_TEST_HOOKS` CycleRecord)이 KA 의 `PunchTarget::hostProof` 바로 위에 들어갔다. 자동 병합 결과 두 블록이 모두 온전하다(눈으로 확인).
  - **KA 영역은 건드리지 않았다.** 작업용1 이 수정 중인 `standby_*` 파일도 이번 변경에 없다.
- **`docs/history.md` 충돌 1건**(0d4c27d): 양쪽을 보존했다. HEAD 쪽 290줄(10-22 이후 항목)을 그대로 두고, 그 뒤에 directory-retry 항목 6줄과 이동 기록 1줄을 붙였다.

## 재시험 (Release, `C:/work/gnlink-wt/dirretry-r2-build`)

| 시험 | 결과 | 원 보수와 비교 |
|---|---|---|
| remote60_directory_retry_test | run1 **154/0**, run3 154/0, run4 154/0. **run2 = 2 FAILED (146 checks)** — 아래 | 원 154/0 |
| mutant_timeout_only (FAIL 기대) | **1 FAIL** (154) — "status says BOTH" | 원 1 FAIL |
| mutant_no_legacy_attempt (FAIL 기대) | **9 FAIL** (154) | 원 9 FAIL |
| directory suite (`node test/run.js`) | **RESULT: ALL PASS, rc 0, PASS 845 / FAIL 0** | NP 묶음 832 + r2 fence 시험 |
| NP np_early_bootstrap e2e | 26/26 rc 0 | — |
| punch_any / directory_session_client / connect_op_gate / connect_candidates | PASS | — |

- **출하 타깃 컴파일 rc 0**(Release), sha256 앞 16:
  - GNLinkStream 94e09fd57885b68b
  - GNLinkViewer b8e6a2cae9379819
  - GNLinkHost 00df332502eb3e85
  - retry_test c504ad06b06a448c
  - 빌드 로그에 `directory_client.cpp` 의 error·warning 0.

**retry run2 의 FAIL 2건 — 판정 불가(환경), 제품 판정 아님**
- 두 줄 모두 `could not lay out the fixture — the adjacent udp port was not free after 32 tries` 다(legacy-adjacent 2행). 고정 장치가 httpPort+1 이 비어 있는 배치를 32번 시도해 못 잡았다. 판정 대상 동작은 실행되지 않았다(checks 146 = 8건 미실행).
- 이 장치 코드는 이번 보수 이전의 것이다(b4d2a7a). 못 잡으면 **건너뛰지 않고 FAIL 로 드러내게** 설계돼 있다.
- **원인 가능성**: 그 시각 이 PC 에 다른 작업자의 node 프로세스 여러 개와 사용자 설치본 GNLinkStream·GNLinkHost 가 실행 중이었다(tasklist). 포트 점유자를 특정하지는 않았다.
- 4회 중 3회는 같은 행이 PASS 했다. 간헐 결함 규칙에 따라 "통과" 로 닫지 않고 **환경 의존(network·port) 판정 불가**로 남긴다. 검증용 독립 실행에서 다시 본다.

## 직접 확인 / 미검증
- **직접 확인**: 위 표(이 PC, 콘솔 세션, loopback·시험 장치. 사용자 설치본·서비스 무조작).
- **미검증**:
  - 원 보고서에 있던 "전 코어 부하 3회" 반복은 다시 하지 않았다.
  - 검증용 독립 검증(원 보수에서 중단된 것)은 아직이다.
  - KA r3 보수(N1~N3)가 이 기준(0a5e71d)을 바꾸면 다시 옮겨야 할 수 있다 — 같은 파일이므로 KA 고정 후보가 바뀌면 재확인이 필요하다.
- 삭제 후보: `deletion_candidates.md` §G 에 worktree·빌드 디렉터리를 추가만 했다.

## 다음
- 검증용: af9a6af 독립 실행 → Codex SOURCE.
- 작업용2: 지적 대기. KA PC 구현은 공용 API 고정·NP 병합 통지 뒤 시작한다.


## r2 — Codex `codex_dirretry_src_r1.md` T1·T2 보수 (2026-10-28)

**고정 후보**: `fix/directory-retry-r2` **34f22de** = d2f1243(T1·T2) + 34f22de(포트 장치, maint r1 2번 — `report_maint_r1.md` §2). 제품 진단은 수용됐고 변경 0 이다. hook 코드는 `REMOTE60_DIRECTORY_TEST_HOOKS` 안에만 있다.

**T1 — 한 시점 읽기**
- HostAgent 시험 hook 에 **bounded barrier** 를 추가했다(`EnableCycleGateForTest`·`ReleaseCycleForTest`·`CycleGateTimedOutForTest`). 같은 loop 경계에서 cycle n 기록 직후 시험이 풀 때까지(최대 60 s) 멈춘다.
- `StagedHost::Wait(n)` 은 cycle 을 하나씩 진행시키고 n 에서 loop 를 붙잡아 둔다. 그래서 그때 읽는 fake 계수·LegacySends 는 cycle n 의 값이고, 그때 설치한 Script 는 다음 cycle 전에 들어간다.
- barrier 가 timeout 으로 풀리면 loud FAIL 한다.
- **제한 반례**: inactive·cached-silent 단계를 **17 s 느린 관찰자** 로 한 번 더 돈다(cycle 4 이후까지). 계수는 같고, 광고 설치가 다음 ask 전에 들어간다.
- 새 mutant **NO_CYCLE_GATE**(barrier 없음) → **4 FAIL**, 정확히 느린 pass 들이다:
  - `inactive-slow 2/1` — Codex 의 "cycle1 에 heartbeat 2" 재현.
  - cached-silent-slow 의 cycle 4 가 광고를 못 받음.
- ⚠️ 처음엔 지연을 7 s 로 잡았다. 그때는 mutant 가 **통과**했다(401 뒤 cycle 2·3 은 요청이 없어 15 s 전엔 차이가 안 남). 이것을 보고 17 s 로 고쳤다 — 통과하는 부정 대조는 장식이므로.

**T2 — cache 와 삭제는 시험 root 안에서만**
- `REMOTE60_TEST_TMP` 는 `D:/remote/remote/.claude/test-tmp` 바로 아래의 ASCII 폴더 하나여야 한다(test_tmp.js 규칙).
  - anchor 는 존재하고 link 가 아니어야 한다.
  - 폴더는 한 단계 안에 만들고, link 가 아니어야 한다.
- 모든 cache(RunHost·StagedHost·PunchHarness)는 그 안에 둔다. 유일한 삭제 `delete_fixture_file` 은 root 밖·하위 경로·`..` 를 거부한다(FAIL 로 셈).
- root 가 없으면 실행하지 않는다(exit 2).
  - 확인: 미설정 → rc 2 "REMOTE60_TEST_TMP is not set". `C:/work/gnlink-wt/x` → rc 2, 그 폴더는 만들어지지 않음.
  - 실행 뒤 C: 빌드 폴더에 fixture 파일 0, root 도 비어 있음(자기 파일만 정리).

**⚠️ 이전 실행의 저장소 밖 삭제 — 사실 보고**
보수 전 시험은 cache 를 exe 옆에 만들고 `DeleteFileA` 로 지웠다. 내가 그 시험을 **저장소 밖 빌드 폴더에서 실행**했으므로 그 삭제 호출이 실제로 일어났다(자기가 만든 fixture 파일을 지운 것이다. 같은 이름의 기존 파일이 있었는지는 확인하지 못했다).

| 시험 | 빌드 폴더(저장소 밖) | 실행 |
|---|---|---|
| directory_retry_test | `np-impl-build`(Debug) | 10-27 1회 |
| directory_retry_test·mutant 2 | `dirretry-r2-build`(Release) | 10-28 r1 재시험 retry 4 + mutant 2 |
| directory_retry_test(main 판) | `maint-r1-build` | 10-28 3회 |
| directory_migration_test(같은 방식 — exe 옆 `migration-fixture-*.json` 생성·삭제) | `maint-r1-build` | 10-28 1회 |

- 지금 그 폴더들에 남은 fixture 파일은 0 이다(`find` 로 확인). 새로 지우지 않았다.
- **후속 후보**: `directory_migration_test` 도 같은 T2 보수가 필요하다(이번 범위 밖, 기록만).
- r1 보고의 "삭제한 것 없음" 은 이 점에서 틀렸다. 이 절로 정정한다.

**시험 (d2f1243, Release, REMOTE60_TEST_TMP=`.claude/test-tmp/dirretry-r2`)**

| 시험 | 결과 |
|---|---|
| directory_retry_test | **177/0 ×3** |
| mutant NO_CYCLE_GATE | **4 FAIL**(느린 pass) |
| mutant TIMEOUT_ONLY | 2 FAIL(일반·느린 cached-silent) |
| mutant NO_LEGACY_ATTEMPT | 12 FAIL |
| 빌드 | rc 0, 경고 0 |

- unrelated suite 는 다시 돌리지 않았다(Codex: 불필요).

**34f22de(포트 장치) 뒤**: retry 177/0 ×2. legacy-adjacent 두 행은 실제로 실행돼 PASS 했다("a health route that fails/says nothing leaves the http fallback working"). 이 헤더를 쓰는 다른 시험(migration·punch_reply e2e·viewer_cancel e2e)은 컴파일만 했고 실행하지 않았다 — migration 은 위 저장소 밖 삭제 문제 때문이다.
