# rel-0164 r1 (+ r2 반영) — PC 0.2.164 통합 후보 dry-run (작업용2, 2026-10-29)

정본: `.claude/task_rel_0164_r1.md`(r1 + 「r2」 절), Codex 분리 결정 `.claude/codex_rel_0164_split_decision.md`.
**게시·서명·push·서버 배포는 하지 않았다.** 아래는 실제 PC EVIDENCE 후보이고 출하 승인이 아니다.

## 1. 통합 commit — `rel/0164-r1` **53fced4c61f6592cb2c10348b43ad359b492bdfd**

- worktree: `C:\work\gnlink-wt\rel-0164`, 기준 main **a0f991a**.
- first-parent 순서:

| commit | 내용 | 충돌 |
|---|---|---|
| c6c9a67 | merge ka-r3 **ccaf745** | `docs/history.md` 1곳(덧붙인 절끼리): 양쪽을 다 보존(main 쪽 다음 ka-r3 쪽) |
| 513d349 | merge ka-pc **320bc36**(ka-r3 b8dcc93 병합은 이미 포함) | 없음 |
| — | np **e16ad0d** | **병합할 것이 없다.** ka-pc 320bc36 안에 이미 들어 있다(`git merge-base --is-ancestor e16ad0d HEAD` 참, `Already up to date`) |
| 3406239 | merge dirretry **34f22de** | `apps/native_poc/src/directory_client.cpp` `HostAgent::Stop()` 1곳 → 아래 표 |
| 6f9458a | merge maint **e58bb29** | 없음 |
| **53fced4** | `release: 0.2.164 (PC)` — `apps/native_poc/src/product_version.hpp:15` 의 `L"0.2.163"` → `L"0.2.164"` 한 줄(Git MCP 로 commit) | — |

- Android versionName/Code 는 바꾸지 않았다.
- 출하 스위치 `kPrepareRecentOnSignIn = false`(`client_shell_main.cpp:1923`, :2362 에서 사용)도 그대로다.

**제품 충돌 해소 — `directory_client.cpp` `HostAgent::Stop()` (현재 :530-546)**

| 쪽 | 내용 | 택한 것 |
|---|---|---|
| KA (HEAD, a7773e6 계열) | `punchMu_` 아래에서 깨움 → `punchCv_.notify_all()` → `punchThread_.join()` | **둘 다 보존** |
| dirretry (34f22de, d11ef62/d6bb951) | `#ifdef REMOTE60_DIRECTORY_TEST_HOOKS` 안에서 `mu_` 잠금 → `cycleCv_.notify_all()` | **둘 다 보존** |

- 순서: punch stop·join → cycle notify(test hooks) → `thread_.join()` → `punchJobs_.clear()`.
- 서로 독립이다. punch thread 는 `cycleCv_` 를 기다리지 않고, cycle 은 `punchCv_` 를 기다리지 않는다.
- 자동 병합된 나머지도 확인했다. `PunchLoop`(:553-585)와 `Punch()`(:1565-1620)의 queue·notify, dirretry 의 `cycleCv_` barrier(:599-635)가 모두 남아 있다.
- **31 s 우회 제거**: `ka_pc_managed_e2e_test.cpp:216-223 wait_after_admission` 은 no-op 이다(a7773e6 수정 뒤). 제품에는 31 s 대기가 없다(grep 0).

## 2. 버전·후보 artifact (release dry-run)

- `automation/gnlink_release.sh --worktree C:/work/gnlink-wt/rel-0164 --version 0.2.164 --dry-run` → rc **0**. 로그 `.claude/np_impl_logs/rel0164_dryrun.log`(66548e027395cbcd).
- 진행: fresh configure → 0 errors → package → manifest → SHA256SUMS → gates. **sign 하지 않았고(not requested), deploy 하지 않았다.**
- 위치: `C:\work\gnlink-wt\rel-0164\.claude\rel\0.2.164`(빌드 `build-0.2.164`).
- 고정값
  - SHA256SUMS.txt `ce84cef0ace01f74a38c19a7f75299929819652451e35a64224262f7bcba9862`
  - windows.manifest(서명 전) `7709970856414bcd385d6ddc97b434f6571b385f289e3e8871538734dcd000ab`
  - sdk set `703a6a41…`, scripts `e40a2cf5…`
- artifact sha256

  | 파일 | sha256 |
  |---|---|
  | GNLinkHost | af95a4b87d94768912a06c5eddb1b80e8e6a80474739346ba7268566dc97a666 |
  | GNLinkStream | 43295261f3eb77d6f82bf0aa3784ef2b9ca760fa2e16333bf1d751dfbf8ae250 |
  | GNLinkCapture | b0ab98486c9cfe76e462290ce39fd98ab42a286074e2388b1398c755178058d6 |
  | GNLinkInputService | 61c13a8d4c5ad7f4f3050a38fd85ba9c49ef82133bb9cabef12da75ef6f5f75a |
  | GNLinkClient | 7d2c4e9bbcf0b67025f26ff6c20c34963049dda32324712368052ea52481df89 |
  | GNLinkViewer | df04391d779b87f8bb4adb9de3cdbcce826369107ab92615b28d32b829f51082 |
  | GNLinkSetup | bfe4d1df8251dee823fa00657ccb0bd7877f0fefb44a988e63643cdd966e067e |
  | GNLinkUpdater | ccab8e0c15dffe045c5bf726c733926998a706f3b9cb9a8fb97059640777041a |
  | GNLinkClipHelper | 15218e563e410879d8c473af4633e6fe4c2b8a92be35d0815d9319b681d43a88 |
  | ui/shell.html | 12085292b1851ec42834eaca8121389b5f5079f66100962533ce445a994a0991 |
  | ui/macro.html | 380c232691d05e74506ce7269dd048d97b343735b1d3c86bd088b88227c1a268 |

- **gate**: parity PASS(전달자들이 0.2.164 를 말하고, 옛 버전을 지닌 것이 없다), payload-set PASS, checksums PASS, installer payload 10/10, 빌드 중 commit·SDK 불변.
- **내 확인**
  - payload exe 의 UTF-16 "0.2.164": Client 1 · Host 1 · Stream 1 · Viewer 1 · Setup 5.
  - "0.2.163": 전 exe 0.
  - 시험 표식(KA viewer/shell seam, MT A/B, record-only, ALLOW_REAL_INPUT, E2E_RUN_DIR 등 11종): 0.
- **출하 gate** `gnlink_check_fixed_server.py`
  - `--dir payload`: ALL PASS 9 (faa90ae22b68a8b2).
  - `--self-test`: ALL PASS 56. REMOTE60_TEST_TMP = `.claude/test-tmp/rel0164-gate-selftest` 이고, 자기 run 폴더만 정리했다(3c71abe8aceed68c).

## 3. 시험 결과

**순수 (Host 없음)**
- 통합 시험 빌드(`C:\work\gnlink-wt\rel-0164-testbuild`)로 돌렸다. 각 시험은 `.claude/test-tmp/rel0164-pure-r3-<이름>` 새 root 에서, 상한 600 s 로 실행했다. 요약은 `np_impl_logs/rel0164_pure_summary.txt`(51e55789598fd6b4).
- **17개 모두 rc 0**:

  | 시험 | PASS | 요약 줄 |
  |---|---|---|
  | directory_retry | 177 | "all retry checks passed (177 checks, 0 failed)" |
  | directory_session_client | 147 | PASS |
  | e2e_guard_runtime | 36 | RESULT PASS(%TEMP% 결함은 ccaf745 안에서 이미 보수됨) |
  | e2e_injection_guard | 20 | |
  | host_agent_punch | 9 | |
  | host_power_policy_record | 27 | |
  | host_standby_table | 56/56 | |
  | ka_pc_e2e_child | 34 | ALL PASS(FAIL 9줄은 일부러 일으킨 거부 대조) |
  | ka_pc_fence | 29 | |
  | ka_pc_ipc_close_race | 9 | |
  | ka_pc_shell_logic | 5 | |
  | ka_pc_units | 23 | |
  | os_input_record_match | 9 | |
  | punch_any | — | PASS |
  | standby_entry_client | 72/72 | |
  | standby_lease | 75 | |
  | standby_legacy_host | 13 | |
- 앞선 두 회차는 보존한다.
  - 1회차: runner 가 exit code 를 잃었다.
  - 2회차: root 폴더를 미리 만들지 않아 standby_entry_client 의 파일 경우 10건이 FAIL 했다. 시험은 `root/세대` 를 한 단계 CreateDirectory 하므로 root 가 있어야 한다. setup 결함이고 제품 결함이 아니다.
  - 요약: `_r1_norc.txt`, `_r2_noroot.txt`.
- **directory suite** `node test/run.js`: rc 0, ^PASS 845, `RESULT: ALL PASS`(a179233789c34d7e).
- **NP e2e** `np_early_bootstrap_e2e.js`(새/구 서버 × 새/구 뷰어 × Host 응답/무응답; 구 서버 = `git archive a0f991a apps/directory`): 26/26, rc 0, **×3**.
- **maint** `gnlink_deploy_test.sh`: rc 0, ^PASS 97, FAIL 0(8af4828b23ab8063).
  - 이 시험은 `$REPO/.claude` 에 mktemp 폴더를 만들고 지운다. 그래서 저장소 밖(worktree)에서 지우지 않도록, 필요한 파일의 사본(`.claude/test-tmp/rel0164-deploy-mirror`: automation, apps/directory, apps/shared, apps/native_poc/src)에서 돌렸다.
  - 첫 시도는 사본에 `update_process_targets.cpp` 가 없어 fixture 를 만들지 못했다. 그 로그는 `_fixturefail.log` 로 보존한다.

**Host e2e** (공용 guard: input none + power off + 두 줄 gate + job)
- runner `np_impl_logs/rel0164_host_e2e_r3.ps1`.
- 각 run 은 검토된 `e2e_job_run.exe`(DC4C5049…) 안에서 돌렸다. stop-file 은 미리 만들었고 grace 는 600~900 s 다.
- 슬롯은 runner 가 job 밖에서 CreateNew 로 잡았고(21:01:21), finally 에서 자기 파일만 지웠다(21:07:39).
- 시험 exe 는 저장소 안 worktree `.claude/worktrees/rel-0164-e2e`(같은 53fced4, WebView2 는 junction)의 `build-verify` 에서 빌드했다. 실행 전후 exe hash 는 같았다.
  - GNLinkStream 28D033CC…, GNLinkViewerKaTest F23F7298…, GNLinkClient 56A17F72…
  - admission BE6CE32F…, punch_reply 299B94FE…, managed E7260793…, shell C580BA24…

| run | rc | ^PASS/FAIL | 요약 | job_run | OFF 최종 판정 | 로그 sha |
|---|---|---|---|---|---|---|
| KA admission(기본) | 0 | 61/0 | ALL PASS (60 checks) | exit 0 res 0 forced 0 confirmed 1 | 2 launch 판정, 57167 B | fe48aaa351030804 |
| KA admission `--product-client --real-directory` | 0 | 73/0 | ALL PASS (72 checks) | 같음 | 2 launch, 186099 B | e661fd8521ef589e |
| punch_reply | 0 | 109/0 | ALL PASS (108 checks) | 같음 | 6 launch, 69029 B | e82ba2e3a9bfbe54 |
| ka_pc_managed | 0 | 60/0 | ALL PASS (V1: 두 activation 모두 picker 보임 → fresh picture PASS) | 같음 | 1 launch, 148986 B | 945dc644dbd6eb79 |
| ka_pc_shell | 0 | 148/0 | ALL PASS (WebView UDF getter 일치) | 같음 | 5 launch, 273316 B | 83e8f084a5537847 |

- old-host: NOT RUN.
- real-input 4: 실행하지 않았다.
- viewer_prelogin_login: 실행하지 않았다.
- dirretry 에는 Host e2e 가 없다(SOURCE 근거가 순수 시험이다).
- **보존한 실패·중단 회차**
  - **1회차**(`rel0164_host_e2e/`): 시험 빌드가 저장소 밖에 있었다. admission harness 가 "build directory is not inside the repository" 로 Host 를 띄우지 않았는데, 그 뒤 계속 FAIL 하며 **900 s 상한까지 끝나지 않았다**(job_run forced, 잔여 0). setup 실패에서 바로 멈추지 않고 상한까지 가는 harness 동작이다. 관찰로 기록만 했다.
  - **2회차**(`_r2/`): admission(기본) ALL PASS 뒤, runner 의 `$ErrorActionPreference='Stop'` 이 native stderr 를 종료 오류로 처리해 product-client run 이 중단됐다. 중단 뒤 잔여 프로세스 0을 확인했다. 그 run 의 부분 로그는 결과로 치지 않는다.
- 실행 중 PC 의 설치 제품(GNLinkHost pid 28932 / GNLinkStream pid 27044)은 건드리지 않았다. `remote60_android_decoder_liveness_test`(pid 45816)는 작업용1 의 것이라 건드리지 않았다.

## 4. directory server (`apps/directory/`) — 변경과 구 client 호환

- **런타임 모듈 중 바뀐 것은 `server.js` 하나다**(+454/-13 줄). `accounts.js` · `device_credentials.js` · `relay_diag.js` · `wake_target.js` · `update_manifest.js` · `version_compare.js` · `package.json` 은 a0f991a 대비 변경 0이다. 나머지 변경은 test/ 와 README 다.
- **server.js 변경 요약**
  - KA relay standby(`/api/connect {standby:true}` → 예약 host-facing 소켓, StandbyHello bind, Host 전용 hostProof pin, host 당 8개 상한(legacy lease 포함), `/api/hosts` standby metadata)
  - env 상한 검사(`relayEnvBound`)
  - NP relayEarly(요청에 relayEarly 가 있을 때만 응답 필드)
  - relay reply fence
  - UDP kind 311..316
- **구 client 호환 근거**
  - 새 동작은 모두 **요청 쪽 플래그로만** 켜진다. `body.standby === true`, `body.relayEarly === true`(`server.js:2489-2497`, 주석 "a viewer that does not know the field gets the same response as before").
  - PC 0.2.163 / APK 0.2.32 는 이 필드를 보내지 않으므로 legacy 경로(newest-wins lease)를 탄다.
  - 실행 근거
    - directory suite 845 PASS 중 relay_standby_test 의 "a legacy connect is offered the relay as before (no standbyRelay field)"·"legacy Hello reaches the host… HelloAck"·"a second legacy connect still takes over the legacy lease"
    - relay_early_test 의 "an older viewer: no flag"
    - NP e2e 의 구 뷰어 × 새 서버
  - **실제 구 APK·구 PC client binary 로 새 서버에 붙인 실행은 아직 없다** → 검증용 lane(6장).
- **배포 때 영향(사용자 안내에 반영)**: server.js 를 바꾸면 **재시작이 필요하다**(CLAUDE.md 릴리스 배포 절). 재시작하면 다음이 메모리와 함께 사라진다.
  - client 로그인 세션(`server.js:795 sessions = new Map()`)
  - relay 세션·standby lease(`:562 relaySessions`)
  - 그 결과: 진행 중인 relay 연결이 끊기고, 앱이 다시 로그인해야 할 수 있다.
  - host token·기기 기록은 디스크에 저장되므로 유지된다.
- **rollback 절차**(검증용이 실행): 교체 **전에** 운영 `apps/directory/` 의 `server.js`(와 함께 올리는 모듈 전량)를 버전 이름을 붙인 백업으로 남긴다. 실패하면 그 백업을 원래 자리로 되돌리고 서비스를 재시작한다. 그 뒤 legacy connect·relay 를 외부에서 확인한다. **PC pair 게시는 server 확인이 통과한 뒤에만** 하고, server 가 실패하면 PC 게시 0 이다. Android 0.2.32 pair/payload 는 전후 hash 가 변하지 않았는지 확인한다.

## 5. 사용자 안내 문구 초안

> **GNLink PC 0.2.164 업데이트 안내**
> - 최근에 쓴 PC(최대 3대)는 목록으로 돌아가도 연결이 대기 상태로 남아, 다시 누르면 바로 화면이 나옵니다.
> - 직접 연결이 안 되는 곳에서는 중계 연결로 더 빨리 붙습니다. 디렉터리 서버에 다시 연결하는 시도가 멈추지 않습니다.
> - 로그인 직후 최근 PC 를 미리 준비해 두는 기능은 이번 버전에서 켜져 있지 않습니다.
> - **새 PC 프로그램(0.2.164)으로 이전 버전(0.2.163 이하) Host 에 연결한 경우**, 목록으로 돌아가면 그 연결은 닫힙니다(이전처럼 5분 동안 유지되지 않습니다). 상대 PC 도 0.2.164 로 업데이트해 주세요.
> - 서버 업데이트 때 잠깐 연결이 끊기고, 앱에서 다시 로그인해야 할 수 있습니다.

(APK 0.2.32 의 동작이 바뀐다는 문구는 넣지 않았다. 0.2.32 는 그대로이고 5분 park 소실은 "새 PC client → 구 Host" 에 한한다.)

## 6. 검증용 독립 lane — 고정 artifact·hash·절차 (실행은 검증용)

- **고정**
  - commit 53fced4
  - 후보 `C:\work\gnlink-wt\rel-0164\.claude\rel\0.2.164`(SUMS ce84cef0…, manifest 77099708…, artifact 위 표)
  - server = 그 commit 의 `apps/directory/server.js`
  - 구 APK `D:\remote\remote\dist\GNLink-0.2.32.apk`(13,990,300 B, sha256 **1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab**)
  - e2e_job_run `C:\work\gnlink-wt\feat-apk-0233\build\apps\native_poc\Release\e2e_job_run.exe`(DC4C5049…)
- **lane 목록**(Codex r2)
  1. final Release/flag/marker: 2장의 gate 와 UTF-16·표식 grep 을 같은 payload 로 재현.
  2. directory_client punch+cycle Stop/retry: `remote60_directory_retry_test`, `remote60_host_agent_punch_test`.
  3. KA admission(기본·`--product-client --real-directory`)/punch: 3장 표의 명령.
  4. ka_pc shell+managed(V1 포함).
  5. directory/NP legacy/fence: `node test/run.js`, `np_early_bootstrap_e2e.js`(NP_OLD_SERVER = a0f991a 의 apps/directory).
  6. maint/gate sandbox: `gnlink_deploy_test.sh`(저장소 안 사본), `gnlink_check_fixed_server.py --self-test`.
  7. **구 APK 0.2.32 → 새 Host·새 server**: 아래 절차.
- **구 APK 0.2.32 AVD 절차**(작업용1 의 D3 고정 규칙)
  1. **adb**: `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe` 만 쓴다(sha256 7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c349660, 이 PC 2026-04-03). PATH fallback 은 쓰지 않는다. 모든 호출에 `-s <승인된 시험 serial>` 을 붙인다. 5037 kill-server, 전역 PATH 변경, 다른 adb 설치는 하지 않는다. AVD 는 `gnlink_kbd_test`(작업용1 automation 의 이름), 패키지 `com.remote60.androiddirect`.
  2. **슬롯**: `C:\work\gnlink-wt\SLOT_REQUEST.txt` 를 CreateNew 로 잡는다. 다른 시험의 GNLinkStream 이 없음을 확인한다(사용자 설치본 pid 는 제외).
  3. **새 server**: 53fced4 의 `apps/directory/server.js` 를 저장소 안 시험 root 에서 띄운다(REMOTE60_DIR_DATA·LOG_DIR = 그 root, relay ENABLED, 계정 test, 포트는 빈 것). AVD 가 닿는 주소에 bind 한다(emulator 의 host loopback = 10.0.2.2).
  4. **새 Host**: 후보 payload 의 GNLinkStream(또는 같은 commit 시험 빌드)을 `--input-injection-mode none --host-power-management off` 로 띄운다. 두 OFF 줄을 확인하기 전에는 연결하지 않는다. job 과 guard 를 쓴다.
  5. **구 APK**: `adb -s <serial> install -r dist\GNLink-0.2.32.apk` → 시험 계정으로 로그인 → Host 선택(ordinary 연결) → picker 에서 바탕화면 선택 → 영상 표시 확인(스크린샷 hash) → control(입력 1회 — Host 는 injection none 이므로 Host 로그의 input-timing 으로 확인) → 목록 복귀(park) → 재선택(resume) → 영상 복귀.
  6. **PC standby 무간섭**: 같은 Host 에 PC managed standby(시험 viewer)를 하나 세워 둔 상태에서 5를 하고, standby 가 유지되거나 규칙대로 양보되는지 Host 로그와 viewer State 로 본다.
  7. **정리**: 자기 job/server 만 끝내고, 슬롯 파일은 자기 것만 지운다. 시험 root 는 보존한다.
- **구 Host 게시본 실행은 계속 NOT RUN** 이다(safe fixture `standby_legacy_host_test` 13 PASS 로 한계를 표시).

## 7. 미검증 목록

- 실제 구 APK 0.2.32·구 PC 0.2.163 client binary 로 새 server/Host 에 붙는 실기(6장 lane).
- 서명, 게시, server 재시작 실기(세션 단절·재로그인의 실제 체감), 외부 https/UDP 확인.
- 구 Host(0.2.163) 상대의 5분 park 소실 실기: 구 Host 는 실행하지 않으므로 순수 대역으로만 확인했다.
- 실제 사용자 화면에서의 KA 체감: 시험은 화면 밖 창, PrintWindow, PostMessage 로 했다.
- `kPrepareRecentOnSignIn` 은 false 다. 로그인 직후 최근 3대 자동 준비는 출하에서 꺼져 있고, 여기서 검증하지도 켜지도 않았다.
- push 는 사용자 확인 대기(r2)다. 이 작업에서 push 는 0이다.

## 산출 위치

- 로그: `D:\remote\remote\.claude\np_impl_logs\rel0164_*`
- 시험 root: `.claude/test-tmp/rel0164-*`
- 후보: `C:\work\gnlink-wt\rel-0164\.claude\rel\0.2.164`
- 빌드
  - `C:\work\gnlink-wt\rel-0164\build-0.2.164`(release)
  - `C:\work\gnlink-wt\rel-0164-testbuild`(순수 시험)
  - `D:\remote\remote\.claude\worktrees\rel-0164-e2e\build-verify`(Host e2e; third_party\webview2 는 junction — 지울 때 rmdir junction 을 먼저 한다)
- 모두 보존한다. 삭제 후보 목록에 올렸다.

## 8. 시험 빌드·시험 대기 보수 — **f12c9ffb2b8b1e3cd471d4ca03921a71c995c39c** (53fced4 위, 시험만)

**① `remote60_viewer_window_proc_isolated_test` 링크 실패** (검증용 ALL_BUILD 에서 발견)
- 원인: ka-pc r3 **705ed23** 부터 제품 WndProc 가 실행 중인 macro 를 멈춘다(`viewer_window_proc.cpp:1279-1280` `ctx.input.macro.StopPlayback()/StopRecording()`). 그런데 이 시험 target(CMakeLists.txt:3739)의 소스 목록에는 InputMacro 구현이 없다 → LNK2019 ×2, LNK1120.
- **ka-pc `feat/ka-pc` 320bc36 에서도 같다.** `ka-pc-build` 로 그 target 을 링크해 보니 같은 LNK2019 ×2, LNK1120 이었다. a0f991a(main)에는 이 호출이 없었다.
- 보수: 그 target 에 `src/input_macro.cpp` 를 더했다. 이 파일은 자기 header 와 표준 라이브러리만 쓴다. double 은 만들지 않았다.
- **전체 ALL_BUILD**(`rel-0164-testbuild`, 보수 뒤 reconfigure): exit 0, **0 errors**, 출력 337개, 그 시험 exe 가 링크됐다(`np_impl_logs/rel0164_allbuild.log`).
- **판정(검증용 10-29): "링크만 확인, 실행 안 함(사용자 화면을 덮음, 격리 세션·VM 필요)".** 제품 결함의 근거로도, 통과의 근거로도 쓰지 않는다.
- **그 시험은 실행하지 않았다.** hypothesis 5 절이 창을 `HWND_TOPMOST` 로 올리고 `SetForegroundWindow` 를 부른 뒤 **화면** 픽셀을 읽는다(`viewer_window_proc_isolated_test.cpp:404-406`). 사용자 화면을 덮는 시험은 이 PC 에서 돌리지 않는다. 격리 세션이나 VM 에서, 또는 사용자 허락을 받고 돌려야 한다 → 검증용이 정한다.

**② directory 시험의 고정 대기** (검증용 suite ×3 에서 매번 다른 시험이 ECONNREFUSED)
- 새 `apps/directory/test/server_ready.js`
  - `waitForServer(child, port, label)`: GET /healthz 를 요청마다 1 s 상한으로 보내며 200 을 기다린다. 상한은 **15 s** 이고, 서버가 먼저 끝나면 그것도 알아챈다.
  - 실패하면 `SERVER DID NOT START <label>: <why>` 를 남기고 그 서버를 끝낸 뒤 **exit 3** 으로 나간다. setup 실패이고 시험 결과가 아니다.
  - `waitForOutput`: 출력 줄을 같은 상한으로 기다린다.
- 적용한 곳
  - run.js 의 서버 시작 9곳(고정 1500 ms)
  - recovery_fault · device_credential · account_admin(100×50 ms = 5 s 폴링)
  - observe_contract · proxy_contamination(8 s)
  - relay_wire(1500 ms) · connect_diag · wake_resend(900 ms)
  - np_early_bootstrap_e2e(1500 ms)
  - relay_env_bound: 2500 ms 타이머 대신 "listening" 줄을 기다린다.
- server.js 는 바꾸지 않았다. 판정(check)도 바꾸지 않았다.
- **부정 대조**: 바로 끝나는 서버 → "the server exited first (code=5)" exit 3. 아무도 듣지 않는 포트(상한 1.5 s) → "no /healthz 200 … (last: ECONNREFUSED)" exit 3.
- **결과**
  - **suite ×3**: 매번 rc 0, ^PASS 845, ^FAIL 0, "SERVER DID NOT START" 0, `RESULT: ALL PASS`(`np_impl_logs/rel0164_dir_suite_ready_{1,2,3}.log`).
  - **wake_resend 단독 ×3**: rc 0, 33 PASS(검증용 환경에서는 단독 3회 모두 실패했던 시험이다).
  - NP e2e: rc 0, 26 PASS.

**후보 SUMS 를 그대로 쓸 수 있는가**
- 53fced4 → f12c9ff 의 diff 는 `apps/directory/test/*`(12) 와 `apps/native_poc/CMakeLists.txt` 의 그 시험 target 3줄뿐이다. 제품 소스, server.js, payload target 의 입력은 0 줄 바뀌었다.
- 따라서 **53fced4 로 빌드한 후보**(`.claude/rel/0.2.164`, SUMS ce84cef0…, manifest 77099708…)는 f12c9ff 의 제품 소스와 같은 입력에서 나온 것이다. **그대로 쓸 수 있다.**
- 단 두 가지를 기록으로 함께 남긴다.
  - ⑴ 후보의 빌드 commit 은 53fced4 다(release 스크립트 summary).
  - ⑵ f12c9ff 에서 새로 빌드하면 MSVC 의 시각 기록 때문에 바이트가 다를 수 있다. 그러니 다시 빌드한 것이 아니라 **이 후보 파일 자체**를 게시한다.
- 검증용 시험 build 는 f12c9ff 기준으로 다시 하면 된다.
- **작업 중 실수(바로잡음)**: 처음 commit `6c58185` 은 파일 목록에 추적하지 않는 `.claude/` 가 섞여 dry-run 후보 파일(.claude/rel/0.2.164 의 exe·SUMS·manifest)까지 담겼다. 보고하기 전에 `git reset --soft 53fced4` → `.claude` unstage → 시험 파일만 다시 commit 해서 f12c9ff 로 만들었다. 작업 트리의 후보 파일은 그대로이고(SUMS ce84cef0… 동일), 6c58185 은 참조 없는 로컬 객체로만 남았다(push 0).

## 9. directory_retry_test 의 single-use 간헐 실패 보수 — **1b10da90be477244ccf66d13fc3c7a6240bb4845** (f12c9ff 위, 시험만)

- **기전 검토(검증용 판단 확인)**: 검증용 판단에 동의한다.
  - fake directory 의 `Script` 는 마지막 reply 를 반복한다(`directory_fake_server.hpp:105-110`, 시험 :648 주석). 그래서 heartbeat 마다 **같은 token** 의 pendingPunch 가 다시 온다.
  - agent 는 heartbeat 의 capability 를 **덧붙여 설치**한다(`directory_client.cpp:1586`, 30 s 만료).
  - AuthorizePeer 는 맞는 것 하나를 지운다(:1704).
  - 바로 앞의 punch 200개가 refresh 를 요청한다(:760). 그 결과 두 AuthorizePeer 사이에 heartbeat 가 끼면, 소비된 token 이 다시 설치돼 두 번째 호출도 통과한다.
  - 실제 server 는 connect 마다 새 token 을 주므로 같은 token 을 다시 주는 일이 없다. → **제품 결함이 아니라 fake 와 시험 타이밍의 결함이다.**
  - 이 기전은 아래 contrast 로 결정적으로 재현했다.
- **보수**
  - single-use 판정 전에 heartbeat 를 capability 없는 reply(`{"ok":true}`)로 바꾼다.
  - capability 없는 heartbeat 하나가 실제로 나갈 때까지 기다린다(상한 8 s, +200 ms). agent 의 heartbeat 는 순차라서, 그 전에 token 을 실은 reply 는 모두 처리된 뒤다.
  - 이것 자체를 check 로 남긴다: "the directory stops handing the token out…" "1 heartbeat(s) since".
  - capability 없는 heartbeat 는 기존 capability 를 지우지 않는다. 덧붙이기만 한다.
  - 두 호출 사이의 heartbeat 수를 detail 로 기록한다(실행에서는 매번 0).
  - **contrast**(판정 뒤): token 을 실은 reply 를 되돌리고 heartbeat 하나를 기다린다 → 소비된 token 이 **다시 받아들여진다**. 이것이 기전의 직접 증거다.
  - 제품 코드 변경은 0이다.
- **반복 결과**
  - **보수 전**(53fced4 와 같은 시험 소스, exe 825D8F7B…): **20회 중 재현 0회**(전부 rc 0, 177/177 — run 19 의 176 은 host 로그 한 줄이 같은 줄에 섞여 "PASS" 가 줄 첫머리에 오지 않은 계수 착시다. 시험 요약은 "177 checks, 0 failed"). 검증용 환경에서는 4회 중 1회였다. 이 PC 에서는 우연으로 재현되지 않았고, 대신 contrast 로 기전을 보였다.
  - **보수 후**(contrast 전, exe C50D3D18…): 10/10 rc 0, 178 checks.
  - **최종**(1b10da9, contrast 포함): 3/3, 이어서 **10/10** rc 0, 179 checks. single-use FAIL 0, contrast PASS 10.
  - 로그: `np_impl_logs/rel0164_retry_{before,after,final}/`, `rel0164_retry_contrast_{1,2,3}.log`.
- **후보**: 제품과 payload 입력은 여전히 변경 0이다. 후보는 53fced4 빌드 그대로다(SUMS ce84cef0…, 다시 빌드하지 않음).
- 검증용 독립 실행은 **f12c9ff + 1b10da9** 기준이다(directory suite·NP e2e·retry 반복).
