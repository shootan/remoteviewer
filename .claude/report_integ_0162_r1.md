# report integ-0162 r1 — PC 0.2.162 통합 후보 (LK e728485 + DW fc4cd57) (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-gsmwtxgn / rev r1 / kind=result. 지시 `task_integ_0162_r1.md`.
**판정: 통합·빌드·영향 시험 완료.** hello(r2)·SH 는 넣지 않았다. 버전 인상·게시·push 는 없다.

## 브랜치 · merge
- worktree `C:/work/gnlink-wt/integ-0162`, 브랜치 **`integ/0.2.162`**, 기준 main 83cbd78.
- **29c9c23** = merge fix/lockscreen-cred. parents 83cbd78 + e728485.
  - 충돌: `docs/history.md` 하나. main 의 10-22 문서 항목 2개와 LK 항목을 모두 보존했다.
- **6083fb5** = merge fix/dxgi-quarantine. parents 29c9c23 + fc4cd57. git CLI `--no-ff` 이고 두 번째 parent 를 보존했다.
- DW merge 의 충돌 3개:
  - `viewer_control_client.cpp` `ControlClient::Run()` 첫 줄 — 검증용 확정(제안 그대로):
    - LK e728485:664 의 `const uint64_t runGen = controlRunGen.fetch_add(1)+1` 은 아래 L5 비밀번호 정리가 쓴다.
    - DW fc4cd57:677-679 는 같은 fetch_add 의 값을 버리고 `windowPanel.SetHostSupportsMonitors(false)` 를 추가했다.
    - 해소: LK 줄 유지 + DW 의 주석·호출을 바로 뒤에 둠. fetch_add 는 1회다(파일 안 `controlRunGen.fetch_add` 1건).
    - 의미 겹침이 없는 기계적 병합이다. 설계 변경은 없다.
  - `automation/gnlink_check_fixed_server.py` TEST_MARKERS: 합집합으로 해소.
    - LK `GNLINK_SIS_TEST_`.
    - DW `mt-ab` · `GNLINK_CHAIN_` · `viewer-present-fail-seam` · `GNLINK_VIEWER_TEST_`.
  - `docs/history.md`: 두 항목 모두 보존.
- 자동 병합된 공유 파일과 확인 결과:
  - `CMakeLists.txt`: 빌드 rc0. 두 쪽의 시험 target 이 모두 생성됐다.
  - `host_control_session.cpp`: DW seam 블록(`#ifdef REMOTE60_STREAM_TEST_SEAM` + 중괄호)이 monitor 플래그 두 줄만 감싼 그대로다.
  - `viewer_window_proc.cpp`: DW 의 restore_on_reveal·kMsgRestoreStep·restore_step 추가는 LK hunk 와 독립이다.
  - 위 셋은 아래 LK·DW 시험으로 함께 실행됐다.

## 산출물 (`C:/work/gnlink-wt/integ-0162-build`, Release, 전체 빌드 rc0 · 오류 0)
| 파일 | SHA256 |
|---|---|
| GNLinkStream.exe | `e66cd4c0beb7126939f776d96955da5afa446e407c3eb04af9b34dc4b056dcd2` |
| GNLinkHost.exe | `a62d2ca7fe8dbf292f61427eca07d7461f3549038b129316d6bb0eeae528fd78` |
| GNLinkViewer.exe | `bb7fab3b491d6db904360a615790cd19f368a3f443cac5cb593fe91f108642c3` |
| GNLinkClient.exe | `5f1cd6228c48a1a2989856f2b84b3402973e37f15849286b39308fc9cb8583e0` |
| GNLinkInputService.exe | `24b85df566cdf5239556ec92b9e2a935777c25fa2e5a8c36a361f00f02dccee6` |
| GNLinkUpdater.exe | `936ae377a9da828609f55f91b85d62c120aed57a4590cb56a48725d597076181` |
| GNLinkCapture.exe | `ab03460d7b3d0ddaf0526c50db1132716f783c1685e9efec72382e3747f24194` |
| GNLinkClipHelper.exe | `123b3dad88ca78b0e8e7d4aab315c822af88775bf35ea0a0a7d5dd54ec8cc290` |
| GNLinkSetup.exe | `2a78a2f69f05a6b515cc3b1fe5dc3610047ec873084241770fdfa902448d01cb` |
| ui/shell.html (= installer_payload) | `8fac49cd810e1445d36305bf5e62e9e539982fb0b671be86cb9008ee5ad62811` |

- 버전 문자열은 0.2.161 그대로다(인상 없음). 해시 목록: `C:/work/gnlink-wt/integ-0162-build.hashes.txt`.

## gate · seam
- `--dir Release`: ALL PASS 9. `--self-test`: ALL PASS 23.
  - 합집합 부정 대조가 둘 다 들어 있다:
    - LK: `sis-test-seam`, `REMOTE60_SIS_TEST_`, `GNLINK_SIS_TEST_`(+ 서비스 ascii/utf-16 단독).
    - DW: `mt-ab`, `GNLINK_CHAIN_`, `viewer-present-fail-seam`, `GNLINK_VIEWER_TEST_`.
    - 각각 "streaming host containing … fails" 로 PASS 했다.
- 출하 9개 exe 직접 스캔: 표식 19개(gate 합집합 + `GNLINK_SH_`·`REMOTE60_RESTORE_MUTANT`·`R3Test`·`R3Mutant`)를 ascii·utf-16 으로 찾았다. **검출 0**.
  - 양성 대조: GNLinkViewerR3Test(viewer-present-fail-seam), GNLinkStreamMigrationTest(stream-test-seam), GNLinkStreamMtAb(mt-ab) 는 검출 True.

## 시험 (종료 코드 + 마지막 요약 줄; 이 PC 콘솔 세션, SLOT_REQUEST CreateNew → 삭제 완료, 설치본·실서비스·실세션 무조작)
**LK 논리** (pure-logic):

| 시험 | 결과 |
|---|---|
| secure_unlock_lockcred | 54/0 |
| lock_cred_channel | 15/0 |
| host_unlock_relay_ondemand | 41/0 |
| logon_ui_target | 21/0 |
| lock_input_policy | 17/0 |
| prelogin_input_gate | 17/0 |
| secure_unlock_service | 12/0 |
| prelogin_cred_host | 43/0 |
| prelogin_cred_seal | PASS |

- 모두 rc0.
- logon_ui_identity_ui 는 SKIPPED 다. 창을 앞으로 가져와 입력을 보내는 시험이라 `REMOTE60_ALLOW_UI_TEST` 없이 실행하지 않았다.

**LK UI e2e** (`remote60_viewer_prelogin_login_e2e_test`, 통합 GNLinkViewer/Stream):
- `--lock-screen` **44/44**, prelogin **79/79**, `--not-prelogin` **28/28**. 모두 rc0.
- 캡처: `integ-0162-build.tests/cape2e_*`.

**DW 논리**:

| 시험 | 결과 |
|---|---|
| capture_recovery_store | 64/0 |
| host_capture_quarantine | 23/0 |
| viewer_restore_target | 30/0 |
| host_os_snapshot | 7/0 |

- 모두 rc0.
- store·quarantine 은 첫 실행에서 "scratch 가 저장소 밖 build 디렉터리" 로 거부됐다. `REMOTE60_TEST_SCRATCH_STANDALONE=1`(exe 폴더 아래 test-scratch)로 다시 실행했다.

**DW UI**:
- `client_recovery_ui_runner.js` 84/0, rc0.
- 사슬(출하 GNLinkViewer, 격리 Host·loopback): desktop **38/0** · monitor **44/0** · nomonitors **29/0**. 모두 rc0. DW r4 의 개수와 같다.
- **첫 사슬 실행은 INVALID 로 기록한다** (display-capture). FAIL 은 desktop 2 / monitor 2 / nomonitors 1 이었고, 모두 스크린샷 항목이다.
  - picker 와 stream 캡처가 같은 그림이었다. 화면 BitBlt 가 뷰어 창이 아니라 그 위치의 다른 창(터미널·Host 창)을 찍었다.
  - 나머지 기능 항목은 PASS 였다.
  - 바로 이어서 DW 단독 빌드(fc4cd57) desktop 을 대조로 돌렸더니 38/0 이었다. 통합 빌드 재실행도 세 시나리오 모두 PASS 했다.
- 첫 실행의 client_recovery_ui rc2 는 exe 를 인자 없이 직접 실행한 내 실수다(usage). 위 runner 결과가 정본이다.

## 상호 영향
- **잠금 화면 + 통합 뷰어**: `--lock-screen` 44/44 를 DW 변경(연결당 1회 화면 목록 요청, capability 초기화)이 들어간 GNLinkViewer 로 실행했다.
  - pong 0x40000 광고와 철회 PASS.
  - 툴바 캡처는 `매크로 · 다시 시작 · Windows 로그인`(item 7)이다.
  - 다만 이 e2e 의 Host 가 monitor 플래그를 알리는지, 그리고 이때 `monitor-list requested` 가 났는지는 로그에 남지 않았다(뷰어 로그는 정리된 scratch 안). **잠금 상태에서 화면 목록 요청이 일어나는지는 직접 관찰하지 못했다.**
- **DW 재연결 + 통합 Host/뷰어**: 사슬 3 시나리오 PASS. 화면 목록 요청 1회, nomonitors 에서 0회.
- **DW 재연결 뒤 LockCredV1 pong**: **미검증**.
  - 사슬 fixture 의 Host 는 잠기지 않은 세션이다.
  - LK e2e 에는 wedge 재연결이 없다.
  - 코드상 Run() 시작에서 runGen 과 hostRoleKnown 이 연결마다 초기화되고 pong 은 연결마다 다시 받는다. 다만 이것은 정적 확인이지 동작 확인이 아니다.

## 남은 위험
- 위 두 상호 영향 항목(잠금 상태의 화면 목록 요청, 재연결 뒤 0x40000)은 제품 UI 로 관찰하지 못했다.
- LK·DW 각자의 OS 실기 미검증은 그대로다:
  - LK: SYSTEM 서비스 → 콘솔 agent, LogonUI, 재부팅, UAC, APK 0.2.30 영향.
  - DW: 회사 PC NVIDIA, 실 두 번째 출력.
- logon_ui_identity_ui 는 SKIP 했다.
- 사슬 스크린샷은 화면 가림에 의존한다(display-capture). 사용자 창이 위에 있으면 INVALID 가 된다.
- 사용자 행동: 없음(rel 단계에서 검증용이 버전·게시).

## r1b — 미관찰 2건 보강 (시험만, 제품 변경 없음; 2026-10-22)
- 커밋: **71d6834** (`integ/0.2.162`, 6083fb5 위 1커밋). 시험 코드만 바꿨다.
  - `dxgi_wedge_reconnect_runner.js`: 시나리오 `lockscreen` 추가.
  - `client_reconnect_restore_e2e_test.cpp`: `lockscreen_checks` 추가.
- 출하 exe 는 바뀌지 않았다. GNLinkViewer `bb7fab3b…` 등 r1 해시 그대로다(대상 하나만 재빌드).
- 시험 exe 해시:
  - remote60_client_reconnect_restore_e2e_test `bcc386da…a99ea84f`
  - GNLinkInputServiceLifecycleTest `af9fa4ce…2e39377`
  - GNLinkStreamMigrationTest `b4089dbe…603806f5`
- **조합**: DW 사슬(출하 GNLinkViewer + 셸 + 격리 Host)에 LK e2e `--lock-screen` 의 대역을 붙였다.
  - Host 세션 가짜 잠금: `GNLINK_STREAM_TEST_FAKE_LOCK_UNTIL_EVENT`.
  - 서비스 대역: `GNLinkInputServiceLifecycleTest --lifecycle-test`, 제품 서비스 코드. unlock pipe 로 lock-cred probe 에 답한다.
  - 두 번째 화면: `MONITORS_FILE=extra`.
- 판정은 출하 뷰어의 툴바를 비활성 sweep(누르고 버튼 밖에서 뗌)해 버튼별 x 구간으로 얻은 것으로 한다.
- in-process e2e 를 쓰지 않은 이유: 그 하네스는 `viewer_startup.cpp:878` 의 연결 직후 window-list 요청을 거치지 않는다. 그래서 r1 의 `--lock-screen` 실행에서 화면 목록이 오지 않았다.

### ① 잠금 상태에서 화면 목록 · 두 버튼 공존 · 글자 보류
- 첫 뷰어(LockCredV1 광고 중) 툴바 구간:
  - 1@6-94 · 2@102-170 · 4@178-258 · 5@266-350 · **3(화면 전환)@358-458** · 6@466-554 · **7(Windows 로그인)@562-702**.
  - 폭 781. 겹치지 않고 둘 다 막대 안이다.
- 화면 목록 요청은 연결당 **1회**였다.
- 잠금 화면에서 친 글자 'Q' 는 보류됐다. 안내 창(GNLinkPreloginResult)에 "Windows"·"Ctrl+Alt+U" 가 나왔다.
- 결과 (rc 0, `dxgi_wedge_reconnect_runner: ALL PASS`):
  - 실행 4·5 각각 **53/0**.
  - Host 의 `[lock-cred] advertised=1` 은 두 streaming child 에서 각 1회였다(실행 4: 11:13:27.462 · 11:14:02.529). runner 의 4회는 같은 2줄이 `host_app.log` 와 디렉터리 업로드 `host.log` 양쪽에 있어서다(확인함).
- 앞선 실행 1·2: 클라이언트 항목은 전부 PASS 였다(`client_reconnect_restore_e2e: ALL PASS`, 52 PASS).
  - runner 의 로그 스캔이 쓰는 중인 Host 로그를 열다 EBUSY 로 FAIL 했다. 하네스 결함이고 try/catch 로 고쳤다.
- 실행 3: **제품 전 단계 INVALID · 원인 미확정 · 하네스 후속.**
  - runner 의 서비스 대역 pipe 준비 대기(`dxgi_wedge_reconnect_runner.js:116`, 10 s)가 만료됐다. 제품 단계는 하나도 실행되지 않았다.
  - 대역 diag 에는 시작 줄 하나만 있었다(정상 회차와 같음).
  - 원인은 확정하지 않았다(existsSync 탓으로 단정하지 않음).
  - **정정**: 처음에 대기를 30 s 로 늘려 71d6834 에 넣었다. 검증용 지시에 따라 8934ee7 에서 10 s 로 되돌리고 원인 단정 주석을 뺐다.
  - 실행 4·5 는 30 s 대기판으로 돌았다. 두 회차 모두 pipe 를 10 s 안에 봤는지는 기록하지 않았다.
- **부정 대조**(`GNLINK_R1B_NO_SERVICE=1`, 서비스 대역 없음 → Host 가 0x40000 을 광고하지 않음): 2회 모두 FAIL 이다.
  - 첫 뷰어 id 7 없음(폭 634).
  - 글자 안내 없음.
  - 재연결 뷰어 id 7 없음.
  - advertised 0회.
  - 화면 목록 요청 1회와 id 3 은 그대로 PASS 했다. 부정 대조가 LK 쪽 항목만 정확히 떨어뜨린다.

### ② DW 재연결 뒤 새 뷰어의 0x40000 · 'Windows 로그인' 재표시
- 같은 실행 4·5 의 흐름:
  1. wedge → Host exit 44 → WGC 격리 재기동.
  2. 뷰어 종료 → 셸이 새 뷰어를 시작한다.
  3. 새 뷰어는 restore=desktop 으로 돌아가 스트림이 다시 흐른다.
- 이 **새 뷰어 툴바에 id 7 이 다시** 배치됐다(같은 구간 562-702). id 3 과 겹치지 않는다. 그 연결의 화면 목록 요청도 1회다.
- Host 는 재기동된 streaming child 에서도 광고했다.
- **L5 runGen 경로에 대해**: DW 재연결은 뷰어 **프로세스를 바꾼다**.
  - 뷰어는 exit 43/44 로 끝나고, 셸이 새 프로세스를 시작한다(`client_shell_main.cpp:1306` reconnect=scheduled).
  - 출하 뷰어에서 `ControlClient::Run()` 은 프로세스당 한 번 시작된다(`viewer_startup.cpp:869`).
  - 그래서 이전 연결의 비밀번호 대기(`ctx.session.preloginCred*`)는 이전 프로세스와 함께 사라진다. 새 뷰어로 넘어갈 길이 없다.
  - L5 의 정리 코드는 같은 프로세스에서 Run() 이 다시 시작될 때를 막는 장치다:
    - `viewer_control_client.cpp:683-693` 은 runGen 이 다르면 0 으로 지우고 버린다.
    - 전송 시점 검사는 `:816` sameConnection 이다.
  - 이 장치는 DW 재연결 경로에서는 실행되지 않는다. 그 동작 근거는 LK 의 기존 시험(prelogin e2e `--session-ends` G7, relay·lockcred 시험)이다. 이번 실행에서 다시 관찰하지 않았다.
  - 통합 해소(fetch_add 1회, `:678`)는 runGen 값을 그대로 보존한다.

### r1b 이후 남은 것
- Host 가 실제 WTS 잠금인 경우는 보지 않았다(가짜 잠금 seam). OS 실기는 사용자 단계에 남는다.
- 실 두 번째 물리 출력도 보지 않았다(가상 항목).
- 툴바 판정은 hit 구간 기준이다. 그림 가림은 화면 캡처에 의존하므로 이번 판정에 쓰지 않았다.
- 첫 r1 사슬 INVALID(화면 가림) 기록은 그대로 둔다.

## r2 — hello-delay merge (2026-10-22)
- **9f54912** = merge fix/hello-delay @ d0fe74e into `integ/0.2.162`. parents 71d6834 + d0fe74e. git CLI `--no-ff`.
  - 충돌: `docs/history.md` 하나, 두 항목 모두 보존.
  - 제품 충돌은 없다. hello 는 gate 표식 목록을 건드리지 않았다(hello 의 시험 매크로는 시험 타깃 전용).
  - hello 의 변경 파일: CMakeLists(시험·mutant 타깃), `directory_rendezvous.{cpp,hpp}`, `native_video_client_tcp_control.{cpp,hpp}`, `viewer_startup.cpp`, 시험 2개, history.
- 재configure rc0, 전체 Release 빌드 rc0(오류 0). 산출물 SHA256:

| 파일 | 해시 | r1 대비 |
|---|---|---|
| GNLinkViewer | `003eadde9f3acc06ac23d9777dbc93d30c12efa87a980f2630d5c7883837089e` | 바뀜(hello 클라이언트 쪽) |
| GNLinkSetup | `8d16dadd218a88b76da683f5438a6462c2f45c3ef6ea1ee32753ff992ec50867` | 바뀜(Viewer 포함) |
| Stream | `e66cd4c0…` | 같음 |
| Host | `a62d2ca7…` | 같음 |
| Client | `5f1cd622…` | 같음 |
| InputService | `24b85df5…` | 같음 |
| Updater | `936ae377…` | 같음 |
| Capture | `ab03460d…` | 같음 |
| ClipHelper | `123b3dad…` | 같음 |
| shell.html | `8fac49cd…` | 같음 |

  - 목록: `C:/work/gnlink-wt/integ-0162-build.hashes_r2.txt`.
- **시험 define 부재(생성 vcxproj 기준)**: `C:/work/gnlink-wt/integ-0162/.claude/test-tmp/check_defines.py`, ALL PASS.
  - 출하 9 타깃의 Release `PreprocessorDefinitions`(ItemDefinitionGroup + 파일별)에 TEST_SEAM·MUTANT·`*_TEST_*` 류는 0 이다.
  - 대응은 vcxproj 의 Release `<TargetName>` 으로 확인했다:
    - GNLinkStream=remote60_native_video_host_poc
    - GNLinkHost=remote60_host_app
    - GNLinkViewer=remote60_native_video_client_poc
    - GNLinkClient=remote60_client_shell
    - GNLinkInputService=remote60_secure_input_service
    - GNLinkUpdater=remote60_updater
    - GNLinkCapture=remote60_gdi_capture_worker
    - GNLinkClipHelper=remote60_file_copy_helper
    - GNLinkSetup=remote60_installer
  - 양성 대조 6개는 자기 define 을 갖고 있다:
    - punch_any_test → `REMOTE60_RENDEZVOUS_TEST_SEAM`
    - punch_any_mutant_netreset → `REMOTE60_PUNCH_TEST_MUTANT_NETRESET_ENDS`
    - hello_backlog_mutant_batch → `REMOTE60_HELLO_TEST_MUTANT_BATCH_ENDS_ROUND`
    - migration_seam → `REMOTE60_STREAM_TEST_SEAM`
    - viewer_present_fail_test → `REMOTE60_VIEWER_PRESENT_FAIL_TEST_SEAM`
    - secure_input_service_lifecycle_seam → `REMOTE60_SIS_TEST_SEAM`
- gate `--dir` ALL PASS 9, `--self-test` ALL PASS 23.
- 시험 (종료 코드 + 마지막 요약 줄):

| 시험 | rc | 결과 |
|---|---|---|
| hello_backlog | 0 | ALL PASS 28 |
| punch_any | 0 | `punch_any_test: PASS` |
| **mutant** hello_backlog_mutant_batch | 1 | FAILED 3/28 (FAIL 기대 충족) |
| **mutant** punch_any_mutant_netreset | 1 | `punch_any_test: FAIL (6)` (FAIL 기대 충족) |
| LK e2e `--lock-screen` | 0 | 44/44 |
| DW 사슬 presentfail | 0 | 37/0 |
| DW 사슬 desktop | 0 | 38/0 |

  - hello_backlog_mutant_batch 의 FAIL 3건:
    - viewer 옵션: full batch 가 다음 Hello 를 앞당김. 17 attempts.
    - android 옵션: 같은 결함. 279 attempts.
    - 2000 punch replies 뒤 ack: 첫 라운드에 받지 못함.
  - LK e2e·DW 사슬은 이 PC 콘솔에서 SLOT_REQUEST CreateNew → 삭제했다.
- **hello 시간**(뷰어 로그 `[attempt] hello ok=1`). **정정**: 측정한 뷰어는 사슬마다 다르다.
  - desktop 은 출하 GNLinkViewer `003eadde…` 다.
  - presentfail 은 runner 가 **GNLinkViewerR3Test**(present-fail seam 시험 빌드, `dxgi_wedge_reconnect_runner.js:163-165`)로 띄운 것이다. 출하 뷰어 측정이 아니다.

| 사슬 | 첫 연결 | 첫 재연결 |
|---|---|---|
| presentfail (GNLinkViewerR3Test) | 527 ms (attempts 3, badAcks 1) | **530 ms** (attempts 3, badAcks 2) |
| desktop (출하 GNLinkViewer) | 263 ms (attempts 2) | **539 ms** (attempts 3, badAcks 1) |

  - 모두 budgetSpent=0, recvErrors=0, loopback 이다.
- 남은 것:
  - hello 의 실 LAN·NAT, Android 실기기는 미검증이다(hello 보고 그대로).
  - directory_retry 간헐은 t-azc166pv 에서 따로 조사 중이다.
