# report rel-0163 r1 — PC 0.2.163 통합 + 릴리스 후보 dry-run (작업용1, 2026-10-26)

task **t-bpwzezfg** / rev r1 / kind=integrate+release-prep-report. 정본 `.claude/task_rel_0163_r1.md`. R1 은 **PC 0.2.163 먼저**이고 APK 0.2.32 는 나중이다.

## 브랜치·commit
- worktree `C:/work/gnlink-wt/rel-0163`, 브랜치 **`rel/0.2.163`**. 순서:
  - main **a1c551c**
  - \+ merge **`f0a388c`**
    - git CLI `--no-ff`, parents a1c551c + **adc9482**.
    - adc9482 = capture-lifecycle r1~r4 + LK-UX 8d8655e.
  - \+ 버전 **`90192f4`** ← 빌드·산출물의 기준
  - \+ gate·문서 **`66bb5e9`**
    - 바꾼 파일: `automation/gnlink_check_fixed_server.py`, docs/history·작업목록·구현계획.
    - `git diff 90192f4 66bb5e9 -- apps libs` = 0 이라 빌드에는 영향이 없다.
  - tracked clean. `.claude/`(rel 디렉터리·test-tmp)는 미추적이다.
- merge 충돌은 `docs/작업목록.md` 한 곳뿐이었다.
  - HEAD 의 PS 행은 그대로 두었다.
  - LK 행은 capture 쪽 최신판을 썼다(같은 행의 갱신본).
  - 제품 충돌은 0 이다.
- 버전 commit:
  - 0.2.162(b4dfaed)와 같은 파일 집합이다: `product_version.hpp` 한 줄, `L"0.2.162"` → `L"0.2.163"`.
  - **BOM `efbbbf`·CRLF 17·1020 B 를 유지**했다(바이트 대조).
  - 소스에 다른 버전 원천은 없다.
- webview2:
  - `robocopy /E` 로 실제 복사했다(junction 아님, 90 파일).
  - 원본 D: 는 전후 90 파일로 같다.

## dry-run
- 실행: `bash automation/gnlink_release.sh --worktree C:/work/gnlink-wt/rel-0163 --version 0.2.163 --dry-run`(Bash, POSIX PATH).
  - **rc 0**. 로그 `C:/work/gnlink-wt/rel-0163-dryrun.log`.
- gates: parity PASS · payload-set PASS · checksums PASS · installer payload 10/10.
  - tracked files·SDK inputs·commit 은 preflight 이후 불변이다.
  - commit 90192f4, signed no, deployed no.
- rel 디렉터리 `C:/work/gnlink-wt/rel-0163/.claude/rel/0.2.163/`:
  - `windows.manifest`(**미서명**) sha256 `5ea1ac7453465829a369498f795929e11dd7050dcc409b5f96e92e484f60a476`
  - `SHA256SUMS.txt` sha256 `9b1a21dd98b831152f342a8e29c9b7be53e81a0872c5664b16affccbc3c172b5` — 11 항목 `sha256sum -c` 모두 OK.
  - sdk set `703a6a41253e5aa4267b8ce8bdc1173bd017c1a4dd1f5bffc6188e2b4a3f0873`(16 파일, 0.2.162 와 같다).
- payload sha256:

  | 파일 | sha256 |
  |---|---|
  | GNLinkSetup.exe | `4999bfaeb0765c18bcdffcd48ff29bac2c81c8e6b5bff5eae7902ffd7dfcf265` |
  | GNLinkViewer.exe | `13d9467cd1e5242cf9e9f7acf7d56111c420e360fd83e71cdf8f0807a9e90381` |
  | GNLinkStream.exe | `ddc5b39988eed4052e51d05073d329a075c056adf3767cc3ec1aebaf883838e8` |
  | GNLinkHost.exe | `b57deb9985e317a29b6ff7f94e1610ec4fce218fd9e94ed9e63302c6052e497c` |
  | GNLinkClient.exe | `7773d1e98310acc4cdd0c8184dcfec490d47a594307f5f31ef592a44bef0147d` |
  | GNLinkInputService.exe | `66df85037fda4b9481313427998435cf073a5104ae7a00fced7b0ea941e54ece` |
  | GNLinkUpdater.exe | `26dea9e8906bf01539d122b45a578c53aecf6f524b4e1f4660c59575e6fc830f` |
  | GNLinkCapture.exe | `f9f10f3ac9ea64ae87df68578926f56f42c237e49a6648e5d359eba3b0a9f2ab` |
  | GNLinkClipHelper.exe | `76578540d45b2e6491034d66b8fc4afad7ae4d799aa188d34c9bbe9c8a925f3b` |
  | ui/shell.html | `8fac49cd810e1445d36305bf5e62e9e539982fb0b671be86cb9008ee5ad62811` |
  | ui/macro.html | `380c232691d05e74506ce7269dd048d97b343735b1d3c86bd088b88227c1a268` |

## 산출물 검사 (payload 기준)
- **버전 문자열**(UTF-16):
  - Viewer·Stream·Host·Client 각 `0.2.163`=1, `0.2.162`=0.
  - Setup `0.2.163`=5, `0.2.162`=0. carrier 수는 0.2.162 와 같다.
  - Capture·ClipHelper·InputService·Updater 는 원래 literal 이 없다(0/0). ASCII 형태도 모두 0 이다.
- **출하 gate**(`gnlink_check_fixed_server.py`, 66bb5e9 에서 확장):
  - TEST_MARKERS 에 `][test-seam]`(시험 빌드 host 의 seam 로그 태그), `ForTest`(LK relay hook 이름), `pointerfirst`(capture 시험 모드)를 추가했다.
  - 새 시험 빌드 환경 이름 9개를 self-test 에서 **각각 단독으로** ascii·wide 대조한다: `GNLINK_STREAM_TEST_DESKTOP_ALIAS`·`_STILL_SCREEN`·`_SECURE_DESKTOP_EVENT`·`_WGC_STILL`·`_FAIL_RESTART_FILE`·`_LOGON_DESKTOP`·`_UNLOCK_PIPE`, `GNLINK_VIEWER_TEST_STATUS_POLLS`·`_SEAL_FAIL`. 기존 접두어 `GNLINK_STREAM_TEST_`·`GNLINK_VIEWER_TEST_` 도 덮는다.
  - 결과: `--dir payload` **ALL PASS 9**, `--self-test` **ALL PASS 51**(0.2.162 는 23).
  - **실 부정 대조**: payload 의 GNLinkStream 자리에 시험 빌드 host(`GNLinkStreamMigrationTest.exe`, fix-capture-lifecycle build)를 넣었다.
    - `FAIL … found: stream-test-seam, GNLINK_STREAM_TEST_, REMOTE60_PRELOGIN_CAPTURE_ATTACH, ][test-seam], pointerfirst` 로 실패했다. 새 표식이 실제 시험 바이너리에서 잡힌다.
    - `ForTest` 는 시험 바이너리에도 문자열로 남지 않는다(함수 이름). 이 표식은 hook 이름이 출력·export 로 새는 경우만 막는 보조 장치다.
    - 대조용 사본 `C:/work/gnlink-wt/rel-0163-gatectl` 은 보존했다(삭제하지 않음).
- **compile define**(생성 vcxproj, 출하 10 타깃): client_shell · file_copy_helper · gdi_capture_worker · host_app · installer · client_poc · host_poc · secure_input_service · updater · verify_release.
  - 시험·seam·mutant define 0. `REMOTE60_ENCODER_POLL_DIAG=0`(host_poc, 출하값)만 있다.
  - capture lib vcxproj 에도 시험 define 0.
- **제품의 시험 hook 호출 0**(소스 정적 확인):
  - LK `SetRetryIoHookForTest`·`KeptEntriesForTest` 는 선언만 있고, 호출은 시험 파일에서만 한다. hook 기본값은 빈 `std::function` 이다.
  - capture `config.testStill` 은 `#ifdef REMOTE60_STREAM_TEST_SEAM` 안에서만 설정된다. 출하 기본값은 `Off` 다.
  - `BackendStatusBoard::testAfterRead` 는 시험에서만 설정한다.
  - 이 셋은 바이너리 문자열로는 판별되지 않아 소스 근거로 남긴다.
- **payload 시험 exe 0**: 제품 9 exe + ui 2 뿐이다.
- **shell.html**: 소스 checkout · build `Release/ui` · `installer_payload` · payload 넷 모두 `8fac49cd…` 로, 0.2.162 와 같다.

## smoke
- 조건: rel-0163 의 **같은 commit 90192f4** 를 시험 빌드로 따로 빌드했다(`build-test`, 출하 build 와 분리, 시험 exe 300개).
- 콘솔 세션 `qwinsta` console Active(RDP Listen 만), SLOT CreateNew→삭제, 잔여 시험 프로세스 0.

| smoke | 태그 | 결과 |
|---|---|---|
| LK `viewer_prelogin_login_e2e_test --lock-screen` | session·display | rc 0, 68/0 |
| LK prelogin (기본) | 〃 | rc 0, 79/0 |
| PS `--still-screen` | display-capture·gpu | rc 0, 32/0 |
| UA `host_capture_lifecycle_ua_e2e_test` (WGC·DXGI) | 〃 | rc 0, 54/0 |
| UA `--fullframe-fails` | 〃 | rc 0, 18/0 |
| `viewer_udp_recovery_test --real-recv-smoke <payload\GNLinkStream.exe>` | network | rc 0 PASS |
| host_punch_reply_e2e | network | rc 0, 102/0 |
| hello_backlog | network | rc 0, 28/0 |
| punch_any | network | rc 0 PASS |
| **`dxgi_wedge_reconnect_runner.js <build-test Release> desktop`** (DW 에서 미뤄 둔 1회) | gpu·display | rc 0, **ALL PASS** — 제품 watchdog 이 wedge(phase=frame_handler)를 명명, supervisor 가 exit 44 관찰, 재기동 child WGC, 셸의 재접속·restore=desktop(picker 클릭 없음)·복원 뒤 프레임 지속 |

- wedge runner 에서 관찰한 것(판정은 PASS):
  - 격리 기록이 `not recorded … reason=folder-unresolved err=3` 였다. 시험의 격리 경로에서 폴더를 해석하지 못했다는 뜻이고, 판정 항목은 "기록을 시도했다" 이다.
  - 실제 설치본에서의 격리 기록은 이 runner 로 확인되지 않는다.
  - fixture 는 `rel-0163/.claude/test-tmp/dxgi-wedge-p17o9J` 에 보존했다.
- 원본 로그: `D:/remote/remote/.claude/caplife/rel_smoke/*.txt`.

## 하지 않은 것·상태
- 서명·NAS 업로드·manifest/sig 교체·push 는 하지 않았다. 사용자 PC 설치·재시작·서비스·주 모니터도 조작하지 않았다.
- repo root 밖 삭제 0. 새로 만든 `rel-0163*`·`rel-0163-gatectl` 은 보존했다.
- **상태: 로컬 실기 후보(게시 전).** 실기는 미검증이다:
  - 실 잠금 화면에서 틀린 비밀번호 → 재시도 → 해제
  - 실 재부팅 뒤 로그인 전 화면
  - 실 UAC 승인/거부
  - 회사 PC DW
- APK 0.2.32 는 별도다(SH r2 + UA-2 APK, 작업용2).
- 구 APK 0.2.31 호환: 매 접속의 wgc 요청은 새 Host 에서 그 세션에만 적용된다(UA-2). 구 APK 는 상태 응답을 요청하지 않는다.

다음 담당: 검증용 — 고정 후보 90192f4/payload 해시 독립 확인 → Codex EVIDENCE → 서명·게시·push(검증용). 작업용1 은 이어서 KA r2 A 보수(S1~S4 + relay ⓐ)로 간다.

## r2 E1 — 실제 renderer 표시 증거 (2026-10-26)
근거 `codex_rel_0163_evidence_r1.md` §E1, 정본 `task_rel_0163_r1.md` 'r2'.

**결론: UA E1 PASS(28/28 ×2). PS E1 은 제품 결함으로 FAIL** — 로그인 전 호스트에선 뷰어 picker 의 바탕화면 선택이 끝나지 않는다(아래). 0.2.163 후보(90192f4)를 이대로 게시하면, PC 뷰어로 로그인 전 화면을 picker 에서 고를 때 영상이 나오지 않는다. 별도 근거는 `.claude/report_rel_0163_r2_e1_blocker.md`(pin seq 14117).

**고정**
- 시험 commit **`b8326ec`**(rel/0.2.163, 66bb5e9 위).
  - 바꾼 파일은 시험 2개 + `e2e_window_capture.hpp`(신규, 시험 전용) + CMake 의 그 두 시험 타깃 `windowsapp` 링크뿐이다.
  - `git diff 66bb5e9 b8326ec` 에 제품 소스 0 — payload 재빌드 없음. payload 해시는 r1 표 그대로다(Stream `ddc5b399…`, Viewer `13d9467c…`).
- 시험 exe(같은 commit 의 build-test):
  - `remote60_host_capture_lifecycle_ua_e2e_test.exe` `17890dab6517c7c6706d1254ff0da17f39107b4933dc9b70204d60dd215370f4`
  - `remote60_viewer_prelogin_login_e2e_test.exe` `a86d317c7b6c3420df409b03380f457cce1fc397ad11425b212ea6bfeae44582`
  - 시험 host `GNLinkStreamMigrationTest.exe` `2d3751d35a0fafadc40f7b1f55d98484ff7a2dd4d215ef0fa89325d6c0da4012`
- 실행 조건: 콘솔 세션(qwinsta console Active), SLOT CreateNew→삭제, 잔여 프로세스 0.
- **화면 무접촉**:
  - 뷰어 창·시험 대상 창 모두 **모든 모니터 밖**(x = 가상 화면 오른쪽 + 200)에 두었고, 시험이 그 위치를 직접 검사한다.
  - 캡처는 그 창에 대한 Windows.Graphics.Capture(`CreateForWindow`)다 — DWM 이 그 창에 합성한 것.
  - PrintWindow 는 flip-model 창에서 GDI 배경(균일 회색)만 돌려줘서 쓰지 않는다. 예외: GDI 로 그리는 picker 스크린샷.

**UA E1** — `host_capture_lifecycle_ua_e2e_test --render --shots <dir>`, **rc0 28/28 ×2**.
- 환경:
  - 호스트는 시작부터 WGC(`REMOTE60_DESKTOP_CAPTURE_BACKEND=wgc`)다.
  - 대상은 시험의 layered 창(UpdateLayeredWindow — 화면 밖에서도 DWM 이 픽셀을 가진다)이고, 색을 바꿔 가며 쓴다.
  - 뷰어는 **제품 WndProc 창**이다: `request_video_paint` → WM_PAINT → `paint_video_frame` → D3D Present.
- 제품 선택: `queue_window_list_request`(viewer_startup 과 같다) → 목록에 대상 창이 나타남 → `begin_pc_target_selection` → reveal → present.
- 왕복: 같은 연결에서 secure↔Default 2회, 빨강 → 초록 → 파랑 → 노랑 → 자홍. 각 전환에서 확인한 것:
  - host 의 edge·`edge fence desktopGen=G`·`rebuild … attempt=1 ok=1`
  - `adopted first frame … captureUs=X` 와 제품 renderer 의 `lastPresentedCaptureUs` = X — **새 attachment 의 프레임이 실제로 present 됨**
  - **스크린샷 중앙 100% 가 새 색, 이전 색 0%**
  - 그 뒤 1.5 s 동안 6번 표본을 떠도 이전 색이 다시 나오지 않음(옛 raw/AU 가 보이지 않음)
- 공통: 세션 1개(연결 하나, WGC), present 실패 0.
- 스크린샷: `.claude/caplife/e1_final/ua1/`, `ua2/`
  - `E1-UA-0-red` … `E1-UA-4-default-magenta.bmp`
  - 대상 창 자체 `E1-UA-target-red.bmp`
  - host 전체 로그 `ua{1,2}-host.log`

**PS E1** — `viewer_prelogin_login_e2e_test --still-screen --render --shots <dir>`, **rc1, 4 FAIL**(3회 동일 + 커밋 뒤 1회).
- 통과한 것:
  - 화면 밖 창
  - 제품 picker 표시(스크린샷 1: picker 배경 87%)
  - 10 s 뒤 제품 레이아웃 함수로 구한 desktop 카드 누름 → `[picker] select … id=0`
- 그 뒤:
  - **present 0**, 창은 계속 picker(스크린샷 2·3: picker 배경 86%, swap chain 으로 present 된 picker)
  - 호스트는 key frame 을 보내지만 뷰어는 디코드 0(`waitForKey=1` 재시도 반복)
  - 워커 생존, present 실패 0
- 원인(소스):
  - 셸은 항상 `--initial-view targets` 로 띄운다(`client_shell_main.cpp:1450`).
  - 로그인 전 호스트에는 뷰어가 WindowList·WindowSelect 를 보내지 않는다(`viewer_control_client.cpp:1072-1077`, allowlist).
  - 그런데 picker 선택은 그 select 의 ack 를 기다리는 동안 모든 프레임을 `DropAwaitingAck` 한다(`viewer_selection_gate.cpp`, `viewer_picker.cpp:206`).
- **제품 수정 = 새 후보** → Codex 결정 대기. PS 의 "실제 presented ≤1 s" 는 그 수정 뒤 이 시험으로 확인한다.
- 스크린샷: `e1_final/ps1/`. 첫 3회 원본: `e1_ps1~3.txt`.

**정정(r1 기록)**: dxgi_wedge runner 의 `capture-quarantine] not recorded … reason=folder-unresolved err=3` 은 **격리 영속 기록 실패**다. runner 는 "기록을 시도했음" 만 PASS 로 본다. UI 복원·이번 실행의 WGC 재기동 PASS 와 **영속 store 쓰기·다음 프로세스/재부팅 유지(미확인)** 는 다르다. fixture 의 저장 경로 문제로 보고, 후속 ledger(원인·담당 미정)로 둔다. 이번 릴리스에서 DW store 소스는 바뀌지 않았다.

## r8 final — 최종 0.2.163 후보 (2026-10-26)

**빌드 commit**: `fae0e1fc787456e852af37af35de7b19f87f01ac` (rel/0.2.163, worktree C:/work/gnlink-wt/rel-0163, 워킹트리 clean — `?? .claude/` 만)
- 제품 = eefac09 (Codex r7 SOURCE OK), 시험 = 0854ac4, fae0e1f = docs 만 (history·작업목록·구현계획; `git diff 0854ac4 fae0e1f -- apps libs` = 0)
- 90192f4 및 이전 dry-run 산출물(manifest 5ea1ac74…) 사용 안 함.

### dry-run (rc 0)
```
automation/gnlink_release.sh --worktree /c/work/gnlink-wt/rel-0163 --version 0.2.163 \
  --build-dir /c/work/gnlink-wt/rel-0163/build-0.2.163-final \
  --rel-root /c/work/gnlink-wt/rel-0163/.claude/rel-final --dry-run   # log: C:/work/gnlink-wt/rel-0163-dryrun-final.log
```
fresh configure(build-0.2.163-final 신규), gates: parity / payload-set / checksums PASS, installer payload 10/10, sdk `703a6a41253e5aa4267b8ce8bdc1173bd017c1a4dd1f5bffc6188e2b4a3f0873` (16 files), commit·SDK preflight 이후 불변.

산출 `.claude/rel-final/0.2.163/`:
| 파일 | sha256 |
|---|---|
| windows.manifest (unsigned) | `6d89ae487a125ebc620e46e6be5396cb3131dc6380878b596a85b47edf64dfd3` |
| SHA256SUMS.txt (11 OK) | `2a3f5163279d3e07d67b1ad6051cf59dc966f30548970c69961bdda1c49d6c0d` |
| GNLinkCapture.exe | `6b270dd6f3af214bd17d0f0d2c87f0fbb0a09ba842565c007bbc2e8d6d2cb644` |
| GNLinkClient.exe | `d84d5c846cdc0bf28e339aa78f12a8979064936a338b42c7b0c889603f857d5a` |
| GNLinkClipHelper.exe | `5dbd89741783b915b2898b78138e562b8f0e5b946117cc7e0b6bb3d8927b3346` |
| GNLinkHost.exe | `0bb227680c6344afce54698fec5f89ed36429a057c2ec7da10791b05796c4ce3` |
| GNLinkInputService.exe | `64a4a0673aac8786b46127480c3c823719182483b211b311964e9437789ef08b` |
| GNLinkSetup.exe | `907e5702ac744c558e023f11d9751a8f7e2a3d6d5795123cbb2729ab8979cc1d` |
| GNLinkStream.exe | `825feb97a067b7b7841a5317fca220c7b6fa59bb917412dc9cc96ed8a5791b35` |
| GNLinkUpdater.exe | `8fe677cc8e83a33563ca2fecb117314d1f9a9d87a814f228f453be3e1dcc0937` |
| GNLinkViewer.exe | `0d47c4ba9daf038034c5fcf396a8dd5215e4aca41972e5c118ba6a8d0bd1d81f` |
| ui/macro.html | `380c232691d05e74506ce7269dd048d97b343735b1d3c86bd088b88227c1a268` |
| ui/shell.html | `8fac49cd810e1445d36305bf5e62e9e539982fb0b671be86cb9008ee5ad62811` (source = build = payload) |

### 산출물 검사 (payload 기준, 직접 실행)
- UTF-16 `0.2.163`: Viewer·Stream·Host·Client 각 1, Setup 5 / `0.2.162` = 0 (전 payload exe).
- `python -I automation/gnlink_check_fixed_server.py --dir .claude/rel-final/0.2.163/payload` → 9/9 PASS; `--self-test` → 51 PASS.
- 출하 define: build-0.2.163-final vcxproj 의 REMOTE60_* 는 `REMOTE60_ENCODER_POLL_DIAG=0` 하나, capture.vcxproj 없음 → 시험 define 0.
- 시험 hook 감사: SelectionGateState 의 testAfterRoleRead / testBeforeAckBoundary / testAfterFrameTicket / testBeforeFrameCommit / testAfterFramePermit 는 제품 코드에서 **빈 검사 뒤 호출만** 있고 대입 0 (대입은 *_test.cpp 에만).

### 시험 exe (build-test, 같은 commit fae0e1f; smoke 후 재해시 동일 — 재빌드하지 않음)
| exe | sha256 |
|---|---|
| remote60_viewer_prelogin_login_e2e_test.exe | `704bdd9bf57161d4255cd89acff09e6e39ff2c06034d9a74d52782cd08a8bc16` |
| remote60_host_capture_lifecycle_ua_e2e_test.exe | `d2bbde518ea3ce089c1091c13397ad677326f29145d6c6e75996f7d4ec4f93a9` |
| GNLinkStreamMigrationTest.exe | `2d3751d35a0fafadc40f7b1f55d98484ff7a2dd4d215ef0fa89325d6c0da4012` |
| remote60_viewer_udp_recovery_test.exe | `a32a5d8dc4c8d899fe1786d847581117fa433dac2303f375dd0a41329a0ce7c0` |
| remote60_viewer_selection_gate_test.exe | `4dd5395ae0edbc5ed6f8f29f14d016048cfa5c0fbe3fe2bb1a74e93c61ab2ca4` |
| remote60_viewer_window_proc_isolated_test.exe | `84651cebeb565f2cc31687284166ada44324d87d2fdc563734703a48108d9fc7` |

### smoke (각 1회, 스크립트 `D:/remote/remote/.claude/caplife/r8_smoke.ps1`, 출력 `caplife/r8final/*.txt`)
세션: console(shotan, ID 1) Active, RDP 미접속. SLOT_REQUEST CreateNew → 종료 후 삭제(removed=True), 잔여 시험 프로세스 0.
env `REMOTE60_ALLOW_HOST_E2E=1 REMOTE60_ALLOW_UI_TEST=1`; 시험 창은 모든 모니터 밖.
| 실행 | 인자 | 결과 |
|---|---|---|
| gate 단위 | — | rc 0 PASS |
| window proc isolated | — | rc 0, 55/0 |
| PS E1 (실제 renderer) | prelogin e2e `--still-screen --render --shots r8final/ps_e1` | rc 0, **39/39** — 바탕화면 카드 → 표시 277 ms, 10 s 후 재선택 294 ms, (5) permit~write 사이 cancel 대기, (6) ticket 직후 A취소·B시작 → 미기록(version 84→84, refused 1→2), B 완료 |
| UA E1 | UA e2e `--render --shots r8final/ua_e1` | rc 0, **28/28** |
| LK lock-screen | prelogin e2e `--lock-screen` | rc 0, 68/68 |
| LK prelogin | prelogin e2e (인자 없음) | rc 0, 79/79 |
| UA e2e | UA e2e (인자 없음) | rc 0, 54/54 |
| fullframe-fails | UA e2e `--fullframe-fails` | rc 0, 18/18 |
| real-recv-smoke | udp_recovery `--real-recv-smoke <payload>/GNLinkStream.exe` (`825feb97…`) | rc 0 PASS |

### 하지 않은 것·상태
- 서명·게시·push 안 함 (검증용 담당). build-test 재빌드 안 함 — 검증용 독립 실행 대상 그대로.
- 미검증: 실 UAC 설치·업데이트 실기, 실 로그인 화면 실기(사용자 대기).
- 별건: APK 공용 세션의 같은 ack 대기(native_video_client_session.cpp:866-869/974) — APK 0.2.32 단계.

### r8 final 보충 — 검사 명령 (worktree C:/work/gnlink-wt/rel-0163)
- 제품 diff: `git diff 0854ac4 fae0e1f -- apps libs | wc -l` → 0.
- 출하 define(payload 9개 exe 의 vcxproj, Release|x64 ItemDefinitionGroup, 경로형 SRC/DIR/PATH/ROOT 제외):
  `remote60_installer_payload.vcxproj` 가 복사하는 exe → TargetName 으로 vcxproj 대응 → `sed -n '/Release|x64/,/<\/ItemDefinitionGroup>/p' <vcxproj> | grep -o 'REMOTE60_[A-Z0-9_]*=[^;<]*'`
  결과: Stream(native_video_host_poc) `REMOTE60_ENCODER_POLL_DIAG=0 REMOTE60_NATIVE_ENCODED_EXPERIMENT=0`, Viewer(native_video_client_poc) `REMOTE60_NATIVE_ENCODED_EXPERIMENT=0`, Capture·Client·ClipHelper·Host·InputService·Updater·Setup 없음. *_TEST_SEAM/*_MUTANT_* 는 시험 vcxproj 에만. (정정: 위 본문의 "POLL_DIAG=0 하나" → 실제는 위 두 개, 둘 다 =0 비활성.)
- hook 감사: `grep -rn 'testAfterRoleRead|testBeforeAckBoundary|testAfterFrameTicket|testBeforeFrameCommit|testAfterFramePermit' apps/native_poc/src | grep -v _test.cpp` → 선언(viewer_selection_gate.hpp:92-101) + `if (hook) hook()` 호출 5곳(viewer_picker.cpp:255,323 · viewer_video_receiver_frame.cpp:185,333,348) 뿐, 제품 대입 0.
