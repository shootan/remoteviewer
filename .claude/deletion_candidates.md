# 삭제 후보 목록 (사용자가 "삭제해" 할 때 한 번에 보여주고 지운다)

사용자 지시(2026-10-27): "삭제해야 될 거는 모아놨다가 나중에 내가 말할 때 한 번에 말해서 삭제할 수 있도록 해. 작업 중간에 권한 묻지 말고."
- 작업 중에는 **묻지 않고 여기에 추가만** 한다(검증용·작업용·Codex 모두). 사용자가 말하면 이 목록을 보여주고 승인된 것만 지운다.
- ⚠️ 지울 때: junction 이 든 폴더는 junction 을 `rmdir` 로 먼저 끊는다(webview2 SDK 날린 전례). worktree 는 `git worktree remove` 후 `git branch -d`(병합 확인).
- 상태: **보존 필요** = 진행 중 작업이라 아직 지우면 안 됨(목록에만 표시).

## A. 저장소 밖 (C:\work\gnlink-wt\)
| 경로 | 무엇 | 지워도 되는 조건 | 상태 |
|---|---|---|---|
| `ka-r3\scratchX` | KA N4 가드 시험 중 생긴 빈 폴더 | 언제든 | 후보 |
| `apk-prelogin-select\automation\__pycache__` | 작업용1 이 하네스 문법 검사(py_compile)로 만든 바이트코드 캐시(미추적) | 언제든 | 후보 |
| `rel-0163` (worktree, rel/0.2.163) | PC 0.2.163 릴리스 worktree·build·rel-final(서명본) | 0.2.163 게시 완료(main 5970f04 push) — rel-final 은 dist·NAS 에 있음 | 후보 |
| `rel-0163-gatectl` | gate 부정 대조용 payload 사본 | 0.2.163 게시 완료 | 후보 |
| `fix-lk-retry` (worktree, fix/lk-retry 8d8655e) | LK-UX 브랜치 | main 에 병합됨(5970f04) | 후보 |
| `fix-capture-lifecycle` (worktree, adc9482) | capture-lifecycle 브랜치 | main 에 병합됨 | 후보 |
| `fix-capture-lifecycle-baseline` | 구 Host 대조용 빌드(ua2_old 시나리오가 씀) | APK 0.2.32 독립 검사 끝난 뒤 | 보존 필요 |
| `spike-host-admission` (worktree, spike/host-admission-r2 9f1fac1) | KA spike 이전판(r3 로 대체) | KA r2 출하 뒤 | 보존 필요 |
| `feat-keepalive-multi`, `ka-multi-ref` | KA 원본(a2e9686) 보존본 | KA r2 출하 뒤 | 보존 필요 |
| `feat-keepalive-r2-apk`, `feat-keepalive-r2-apk-build`, `.runs` | KA APK 준비(6d85ce2) | KA r2 출하 뒤 | 보존 필요 |
| `ka-r2-out`, `lockcred-scratch` | 10-22 시험 산출물 | 확인 후 | 후보 |
| `fix-directory-retry`, `-build`, `.runs`, `verify-dirretry`, `verify-dirretry-base` | directory-retry 수정(0d4c27d, 미검증) | 그 작업 끝난 뒤 | 보존 필요 |
| `sh-avd` | SH AVD 시험 산출물 | APK 0.2.32 게시 뒤 | 보존 필요 |
| `apk-prelogin-select` (worktree) | APK 0.2.32 작업 중 | — | 보존 필요 |
| `ka-r3` (worktree, spike/host-admission-r3) | KA 작업 중 | — | 보존 필요 |

## B. 저장소 안 오래된 worktree (D:\remote\remote\.claude\worktrees\) — D: 여유 5.8 GB
| 경로 | 지워도 되는 조건 | 상태 |
|---|---|---|
| `release-0.2.135` ~ `release-0.2.139`, `release-apk-0.2.23`, `release-apk-0.2.24` | 오래전 게시 완료 | 후보 |
| `base-6e5cd70`, `base-a95215e`, `verify-c2-ef533f2` (detached) | 과거 대조용 | 후보 |
| `performance-20260922`, `feat-prelogin-service`, `feat-prelogin-ui` | 병합 여부 확인 후 | 후보(확인 필요) |

## C. 브랜치 (병합 확인 뒤 `git branch -d`)
- 10-22 정리분: `rel/0.2.162 rel/apk-0.2.31 fix/lockscreen-cred fix/dxgi-quarantine fix/hello-delay feat/sh-filter`
- 10-26: `rel/0.2.163 fix/lk-retry fix/capture-lifecycle`(main 병합됨)

## D. 기타 (관리자 필요)
- 시험 exe 방화벽 Allow Inbound 규칙 10개 — `dist/remove_gnlink_test_firewall_rules.ps1 -Apply`(관리자 PowerShell)

## E. 로컬 증거 중 민감할 수 있는 것
- `.claude/verify_0163/final/ps_e1/E1-2-presented.bmp`·`ps_presented.png`(이 PC 화면 캡처 포함) — 0.2.163 기록 보존 기간 뒤
| `D:
emote

## F. 저장소 안 도구
- `D:/remote/remote/.claude/.venv_ffmpeg` — E1 shimmer 프레임 디코드용 격리 venv(imageio-ffmpeg·pillow). 지워도 되는 조건: APK 0.2.32 게시 뒤. 상태: 후보

## G. np-impl r1 (작업용2, 2026-10-27)
- `C:/work/gnlink-wt/np-impl/.np_dir_test.log` — 시험 첫 실행의 실패 로그(시험 루트 미설정, 954 B). 내가 만든 파일, 추적 안 됨. 지워도 되는 조건: 지금. 상태: 후보
- `C:/work/gnlink-wt/np-impl`(worktree)·`C:/work/gnlink-wt/np-impl-build`·`np-impl-build.*.log` — feat/np-early-relay 빌드. 지워도 되는 조건: 병합·게시 뒤(그 전엔 검증용이 쓰는 고정 산출물). 상태: 후보(보존 중)
- `D:/remote/remote/.claude/test-tmp/np-impl` — 시험 루트(구 서버 사본 old-server/ 포함). 지워도 되는 조건: 검증용 독립 실행 뒤. 상태: 후보(보존 중)
- `D:/remote/remote/.claude/test-tmp/np-impl/fence-mutant` — np-impl r2 부정 대조용 c803768 server.js 사본·모듈 복사. 지워도 되는 조건: Codex SOURCE 재확인 뒤. 상태: 후보
- `C:/work/gnlink-wt/np-verify-build` — 검증용 NP 독립 빌드(e16ad0d 시험 타깃). 지워도 되는 조건: NP 게시 뒤. 상태: 후보(저장소 밖)
- `C:/work/gnlink-wt/dirretry-r2`(worktree)·`C:/work/gnlink-wt/dirretry-r2-build`·`dirretry-r2-build.*.log` — dirretry-rebase r1. 지워도 되는 조건: 병합·게시 뒤. 상태: 후보(보존 중)
- `C:/work/gnlink-wt/ka-verify-build` — 검증용 KA 독립 빌드 1차(저장소 밖이라 e2e scratch 가드가 거부 — 결과 무효). 지워도 되는 조건: 언제든. 상태: 후보(저장소 밖)
- `C:/work/gnlink-wt/ka-r3/build-verify` — 검증용 KA 독립 빌드 2차(worktree 안). 지워도 되는 조건: KA r2 게시 뒤. 상태: 후보
- `D:/remote/remote/.claude/test-tmp/verify-ka`·`verify-np`·`verify-np-e2e`·`verify-np-old` — 검증용 시험 임시 루트. 지워도 되는 조건: R2 게시 뒤. 상태: 후보
- 2026-10-28 (작업용1, KA e): `C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke_r1_hung\` — 첫 AVD bundle 이 fixture 파이프 대기로 멈춘 실행의 잔여(로그만). 내가 만듦, 결과로 쓰지 않음.
- 2026-10-28 (작업용1, KA e): `C:\work\gnlink-wt\feat-apk-0233\build\test-scratch\` 아래 ke_r1 의 강제 종료된 harness 들이 남긴 staging 디렉터리(mobile_abr*) — 정상 종료 시 harness 가 스스로 지우는 것.
- 2026-10-28 (작업용1, KA e): `C:\work\gnlink-wt\feat-apk-0233\build\apps\native_poc\Release\GNLinkStreamPinnedOld.exe` — fixture 가 baseline 의 PinnedSelectTest 를 복사해 둔 것(빌드 출력 폴더).
- `C:/work/gnlink-wt/maint-r1`(worktree)·`C:/work/gnlink-wt/maint-r1-build`·`maint-r1-build.*.log` — maint r1. 조건: 병합 뒤. 상태: 후보(보존 중)
- `D:/remote/remote/.claude/test-tmp/maint`·`maint-base`·`dirretry-r2`(시험 root, 비어 있음) — 조건: 검증용 독립 실행 뒤. 상태: 후보
- `C:/work/gnlink-wt/dirretry-r2/build-verify`·`D:/remote/remote/.claude/test-tmp/verify-dirretry` — 검증용 dirretry 독립 빌드·임시 루트. 지워도 되는 조건: R2 게시 뒤. 상태: 후보
- `C:/work/gnlink-wt/maint-r1/automation/__pycache__` — maint r2 중 python import 로 생긴 캐시(미추적). 조건: 지금. 상태: 후보
- `C:/work/gnlink-wt/ka-pc`(worktree)·`C:/work/gnlink-wt/ka-pc-build`·`ka-pc-build.*.log` — ka-pc-impl r1(feat/ka-pc). 조건: 병합 뒤(그 전엔 검증용 고정 산출물). 상태: 후보(보존 중)
- `D:/remote/remote/.claude/test-tmp/ka-*` — ka-pc e2e 증거(로그·캡처 BMP). 조건: 검증용 독립 실행 뒤. 상태: 후보(보존 중)
- `D:/remote/remote/.claude/test-tmp/verify-maint` — 검증용 maint 독립 시험 sandbox. 지워도 되는 조건: maint 병합 뒤. 상태: 후보
- `D:/remote/remote/.claude/test-tmp/ka-shell-r2-*`·`ka-managed-r2-*`·`ka-units-r2`, `.claude/np_impl_logs/ipc_new.hpp` — ka-pc-impl r2 시험 증거·대조 사본. 조건: 검증용 독립 실행 뒤. 상태: 후보(보존 중)
- `D:/remote/remote/.claude/test-tmp/ka-shell-r2b-*`·`ka-managed-r2b-*`·`ka-shell-r3*`·`ka-managed-r3-*`·`ka-units-r3`·`ka-punch-r2b`, `.claude/np_impl_logs/shell_html_*.html` — ka-pc-impl r2b/r3 시험 증거·대조 사본. 조건: 검증용 독립 실행 뒤. 상태: 후보(보존 중)

- `D:/remote/remote/.claude/test-tmp/injprobe-39848/` — 2026-10-28 입력 주입 OFF 확인용 loopback 프로브(GNLinkStream 사본·로그). 결과는 report 「안전 injection」 절에 기록됨. 불필요.
- `D:/remote/remote/.claude/test-tmp/ka-shell-logic-r5*`·`ka-units-r4`·`ka-units-r5`·`ka-cfgcheck`, `.claude/np_impl_logs/client_shell_main.r5.cpp`·`ipc_new.hpp` — ka-pc r4/r5 시험 루트·대조 사본. 조건: 검증용 독립 실행 뒤. 상태: 후보
- `D:/remote/remote/.claude/x` (빈 폴더) — 2026-10-28 작업용2 실수: r6 경계 시험을 빌드 실패 상태에서 돌려 **r5 의 substring root 검사**(Codex T1 지적 그대로)가 `...\test-tmp\..\x` 를 받아들이고 만든 폴더. 저장소 안이지만 test-tmp 밖. 조건: 지금. 상태: 후보(지우지 않음)
- `D:/remote/remote/.claude/test-tmp/ka-shell-logic-r6`·`ka-units-r6` — ka-pc r6 시험 루트. 조건: 검증용 독립 실행 뒤. 상태: 후보
- `D:/remote/remote/.claude/worktrees/verify-inj` (git worktree, detached ba2eaf2) — 2026-10-28 검증용: os_input_record_e2e 독립 실행용 빌드. ⚠️ 안에 `third_party/webview2` junction 이 있다 — 삭제 전 `rmdir` 로 junction 먼저 떼고 `git worktree remove`(--force 금지). 조건: record EVIDENCE 판정 뒤. 상태: 후보
- `D:/remote/remote/.claude/test-tmp/ka-units-r7`·`ka-shell-logic-r7`, `.claude/np_impl_logs/shared_core.r7.cpp`·`client_shell_main.r5.cpp` — ka-pc r7 시험 루트·mutant 복원용 사본. 조건: 검증용 독립 실행 뒤. 상태: 후보
- `D:/remote/remote/.claude/worktrees/verify-inj-b8` (git worktree, detached b8dcc93) — 2026-10-28 검증용: 일반 8 + fatal 독립 실행용 빌드. ⚠️ `third_party/webview2` junction 포함 — `rmdir` 로 먼저 떼고 `git worktree remove`(--force 금지). 조건: 해당 EVIDENCE 판정 뒤. 상태: 후보

- `C:/Users/shota/nul_placeholder` (6897 B, 2026-10-28 12:08) — 작업용이 실수로 만든 파일(50340ce 의 client_mouse_xbutton_fence.hpp 사본, 불필요한 Out-File 한 줄). 저장소 밖이라 직접 지우지 않음. 공개 소스 사본뿐, 비밀 없음. 조건: 지금. 상태: 후보

- (2026-10-28 작업용2, ka-pc 시험 r2) `.claude/test-tmp/ka-e2e-child/` (ka_pc_e2e_child_test run 폴더 2개 + mutant run 1개), `.claude/np_impl_logs/ka_pc_e2e_child.hpp.orig`, `ka_test_r2_report_section.md`, `ka_child_hdr_apply.py`, `ka_add.json`, `ka_commit.json` — 보존, 삭제 승인 시 일괄
- (작업용2 ka-pc 시험 r2 추가분) `.claude/np_impl_logs/ka_webview_move.py`, `ka_add2.json`, `ka_commit2.json` — 보존

## 2026-10-28 사용자 지시 정리 ("용량 없음, 네가 관리하는 것 중 필요 없는 것 지워")
- ✅ 삭제함: `C:\work\gnlink-archive\2026-10-22`·`verify-rcbudget` 의 녹화 프레임(pgm)·시험 hang 덤프(dmp, 10-12 시험 Host — 현장 DXGI wedge 아님)·exe/pdb/apk/bmp/png/out·1MB 넘는 로그 14,816개(11.88 GB). 보고서(md/txt/json/csv/py)와 1MB 이하 로그는 남김. 목록: `C:\work\gnlink-archive\_pruned_2026-10-28.txt`. 지우기 전 두 폴더 안 reparse point 0 확인. C: 여유 4.1 → 30.2 GB.
- ✅ junction 만 끊음(SDK 파일 90개 전후 동일): `C:\work\gnlink-wt\fix-lk-retry\third_party\webview2`, `C:\work\gnlink-wt\fix-capture-lifecycle\third_party\webview2`.
- ⏳ 자동 분류기가 막음 → 사용자 실행 대기: `git worktree remove --force` 로 `C:/work/gnlink-wt/rel-0163`(1.8 GB, junction 없음)·`fix-lk-retry`(1.3 GB)·`fix-capture-lifecycle`(1.3 GB) 제거 + `git branch -d rel/0.2.163 fix/lk-retry fix/capture-lifecycle`(셋 다 origin/main 에 병합 확인, tracked clean).
- (작업용2 ka-pc 실 Host e2e 10-29) `.claude/test-tmp/ka-managed-fin-1,shell fin-1/`, `.claude/test-tmp/ka-shell-fin-1/` (UDF 포함, 보존), 로그 `np_impl_logs/ka_managed_fin-1,shell fin-1.log`, `ka_shell_fin-1.log` — 보존
- (작업용2 10-29) `.claude/test-tmp/ka-managed-r3-1/run-33672-611534937/`, `np_impl_logs/ka_managed_r3-1.log`, `ka_add3.json`, `ka_commit3.json` — 보존

## 2026-10-29 사용자 지시 "워크트리 다 쓴 것 정리"
- 대상 17개(브랜치 유지, 작업 폴더만): C: apk-prelogin-select·verify-dirretry-base / D: base-6e5cd70·base-a95215e·verify-c2-ef533f2·feat-prelogin-service·feat-prelogin-ui·release-0.2.135~139·release-apk-0.2.23·24·performance-20260922·verify-inj·verify-inj-b8.
- ✅ webview2 junction 9개 먼저 끊음(SDK 파일 90 → 90). base-6e5cd70 의 미커밋 변경은 `.claude/base-6e5cd70_uncommitted_2026-10-29.diff`(2061 B)로 보존.
- ⏳ `git worktree remove` 는 분류기 거부 → 사용자 실행 스크립트 `.claude/cleanup_worktrees_2026-10-29.ps1`(실행 직전 링크 재검사·SDK 개수 대조).
- 남김(R2 게시 전 필요): ka-r3, ka-pc, feat-apk-0233, np-impl, dirretry-r2, verify-dirretry, maint-r1, fix-directory-retry, spike-host-admission, feat-keepalive-multi, ka-multi-ref, feat-keepalive-r2-apk, main-stability-integration.
- 2026-10-29 정리 결과: 사용자 스크립트 2회로 worktree 15개 + 폴더 정리 완료(registered 14 남음 = R2 진행분). SDK 90 유지. C: 38.2 GB / D: 9.2 GB. **남은 폴더 2개**(다른 프로그램이 잡고 있음, git 등록 해제됨): `C:\work\gnlink-wt\apk-prelogin-select`, `C:\work\gnlink-wt\fix-capture-lifecycle` — 재부팅 뒤 `Remove-Item -Recurse -Force`.
- (작업용2 V1) `np_impl_logs/ka_add4.json`, `ka_commit4.json` — 보존
- (작업용2 V1 실행 10-29) `.claude/test-tmp/ka-managed-v1-1/` (presented 이미지에 이 PC 화면 내용 포함, png 사본 포함), `np_impl_logs/ka_managed_v1-1.log`, `ka_v1_job_run.ps1` — 보존
- `D:/remote/remote/.claude/verify_inj/v1_view/A-round1-picker-client.png` — 2026-10-29 검증용이 V1 picker 직접 열람용으로 변환한 사본(이 PC 창 제목 포함). 조건: KA PC EVIDENCE 판정 뒤. 상태: 후보
- (작업용 apk-decoder-liveness r1, 2026-10-29) worktree `C:\work\gnlink-wt\apk-decoder-liveness`(branch fix/apk-decoder-liveness, 27d5966) 안의 `build-neg-old`, `build-neg-m1`, `build-neg-m2`(음성 대조 build), worktree 루트의 `*.log`(build·gradle 기록). worktree 자체는 SOURCE·병합 뒤 후보. 조건: Codex SOURCE/EVIDENCE 판정 뒤. 상태: 후보. ⚠️ `third_party\webview2` 는 junction — 지울 때 rmdir 먼저.
- (같은 작업) `.claude/caplife/liveness_neg/`, `.claude/caplife/liveness_evidence/` — 증거 입력, 보존. `.claude/caplife/liveness_edit_*.py`, `avd_r7_edit.py` — 편집 스크립트, 보존(작업 종료 후 후보).
- (같은 작업 r2) worktree 의 apk_out(시험 APK 3개), build-neg-m3, *.log — 조건: 판정 뒤. 상태: 후보
- (작업용2 rel-0164 r1, 10-29) worktree `C:\work\gnlink-wtel-0164`(+ build-0.2.164, .claude/rel/0.2.164, third_party/webview2 실복사본), `C:\work\gnlink-wtel-0164-testbuild`, `.claude/worktrees/rel-0164-e2e`(webview2 junction — rmdir 먼저), `.claude/test-tmp/rel0164-*`, `np_impl_logs/rel0164_*` — 게시 전까지 보존
- `D:/remote/remote/.claude/worktrees/verify-0164` (detached 53fced4) — 2026-10-29 검증용: PC 0.2.164 통합 후보 독립 확인 빌드. ⚠️ webview2 junction 포함 — rmdir 먼저. 조건: 0.2.164 게시·판정 뒤. 상태: 후보
- (같은 작업 r3) worktree 의 build-neg-m4, relay_trace_test*.log, gradle_r3*.log; .claude/test-tmp/relay-trace, dir-r3, dir-base(directory 시험 tmp) — 조건: 판정 뒤. 상태: 후보

## 2026-10-29 rel-0164 old-apk-lane (보존 · 사용자 판단)
- C:\work\gnlink-wt\old-apk-lane (worktree, branch test/old-apk-lane @ ce90b8e; build\, apk_out\apk-0232-fixture.apk 73549dbb, .claude\oa_runs\ stub run folders 포함) — 검증용 실행 끝난 뒤
- C:\work\gnlink-wt\apk-0232-src (detached b554e76 worktree, Arm S APK 빌드용; build 산출물 포함)
  - ⚠️ old-apk-lane 에는 third_party/webview2 junction 이 있음 — 지우기 전에 rmdir 먼저
- (apk-decoder-liveness r4, 10-29) worktree 의 build-neg-r4(+ .configure/.build/.live/.idr 로그), r4_*.log, build_r4.log, gradle_r4_ndk.log; `.claude/caplife/liveness_neg/r4_7a79b09`(음성 입력, 판정까지 보존), `.claude/caplife/r4_*_edit.py`, `.claude/r4_add.json`, `.claude/r4_commit.json` — 조건: Codex 판정 뒤. 상태: 후보

- 2026-10-30 검증용: D:/remote/remote/.claude/worktrees/verify-0164/build/ (old-apk orchestration 시험용 exe 사본 2개 폴더; verify-0164 worktree 정리 때 함께)
- 2026-10-30 검증용: D:/remote/remote/.claude/worktrees/verify-0164/.claude/oa_relay_server/v-traced, v-plain (relay trace 사본 확인용; v-traced 는 lane 7 실기 -ServerJs 로 쓸 예정)
- 2026-10-30 검증용: C:/work/gnlink-wt/verify-c2-apk (C r2 독립 APK 빌드 worktree, d50415e; C EVIDENCE 끝나면 삭제 후보 — 저장소 밖이라 사용자 확인 필요)
- 2026-10-30 검증용: .claude/verify_0164/mcp_git_client_v0164.py, v_add.json, v_commit.json (verify-0164 commit 용 임시)
- 2026-10-30 검증용: .claude/verify_c2/plain-debug-STALE-d50415e-copy.apk (재빌드 실패 때 잘못 복사된 d50415e APK, 사용 금지)
- C:\work\gnlink-wt\old-ref-163 (git worktree, branch test/old-ref-163 at fae0e1f + old-ref test-only commits) -- made 2026-10-30 for the old/new reference comparison (task_old_apk_ref_r1); remove with `git worktree remove` only after rmdir of any webview2 junction inside it
  - note: C:\work\gnlink-wt\old-ref-163\third_party\webview2 is a JUNCTION to D:\remote\remote\third_party\webview2 -- rmdir it (never Remove-Item -Recurse) before removing the worktree
- C:\work\gnlink-wt\old-ref-163-configure.txt (cmake configure log, written by mistake next to the worktree instead of inside it, 2026-10-30)
