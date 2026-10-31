# APK 0.2.32 독립 EVIDENCE 실행 진입점 (작업용1 → 검증용, 2026-10-27 r3 — r2 를 대체)

## 고정 대상
- 브랜치 `fix/apk-prelogin-select` **b554e76** (worktree `C:\work\gnlink-wt\apk-prelogin-select`). tracked clean.
  - SH r6: d29a034 (제품, Codex codex_sh_strength_r6.md SOURCE OK) · b554e76 (시험)
  - SH r5: 8d67710 (제품) · a6635a2 (시험)
  - evidence A: ed7f4ad (시험 Host seam · 게이트 · 하네스)
- **후보 APK** `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk`
  - sha256 `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab`, 13,990,300 B
  - 0.2.32 / versionCode 31, 4 ABI, 서명자 `dcc806ae…2990`, 단위 277/0, 게이트 ALL PASS(7)
  - 이전 후보 1d4a05f4… 는 `rel-apk-0232\superseded\GNLink-0.2.32-1d4a05f4.apk` 로 보존했다(검증용 부분 배치의 대상).
- **shseam APK** `D:\remote\remote\.claude\caplife\sh_r2\app-shseam.apk`
  - sha256 `b105a845d9acd89a1280aac8d8b45f5ad213ad641cef7f4f24d6aa5379ab2e81`
  - 같은 소스이고 차이는 `src/shseam` 뿐이다. 게이트에서 FAIL(2)이 나는 것이 정상이다.
- **시험 Host** `...\build\apps\native_poc\Release\GNLinkStreamMigrationTest.exe`
  - sha256 `881b3e05e0a789a94e76034b7946ede1ab1b84f0d903210cc203a46c25ece8cd`
  - 새 값이다(evidence A seam 포함, 기본 off). 이전 ecb2ff2e… 를 대체한다.
- 출하 Host 대조 `...\Release\GNLinkStream.exe` sha256 `1c0bc4f05a970729b1a9305b5884eac0fb386b6e5a0b53a91de01a1d17e2d657`
  - `GNLINK_STREAM_TEST_PRELOGIN_ROLE`(ascii·wide), "prelogin-role FORBIDDEN", "][test-seam]" 가 **모두 없다**. 시험 Host 에는 wide 이름과 두 태그가 **있다**.
  - 게이트 `--self-test` 54/54 (이 이름을 목록에 추가).
- 실 로그인 전 Host e2e `...\Release\remote60_viewer_prelogin_login_e2e_test.exe` sha256 `ed69ea1f8e59a7ceb907b2047e588311f1565579587e3cdb4cb6b2415fec82b9`
- picker 단위 `...\Release\remote60_client_session_prelogin_select_test.exe` sha256 `453be9cf7f58989bd09f6ede164dfd5d64eb1d28d0c81425517c7c29e28e1683`
- (ua2_old 전용) 구 Host `C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStreamMigrationTest.exe` 821535a7…

- ⚠️ 2026-10-27 작업용 배치 도중 AVD 의 시스템 서비스가 죽었다("Can't find service: package", 15 시간 가동). `adb -s emulator-5554 reboot` 후 정상. 결과가 모두 timeout/빈 줄이면 먼저 이것을 의심한다.

## 공통 — 실행기 하나: `D:\remote\remote\.claude\caplife\sh_run.ps1`
- 형식: `sh_run.ps1 -Scenario <s> -Tag <t> -Seam false|true -NoBuild [-HostExe <exe>] [-Extra "<args>"]`
- 네이티브 시험은 `sh_run.ps1 -Tag <t> -NativeExe <exe> -Extra "<args>"` 로 돌린다.
  - 이때 `REMOTE60_ALLOW_HOST_E2E=1` 과 `REMOTE60_ALLOW_UI_TEST=1` 을 실행기가 설정한다.
- **슬롯은 실행기만 다룬다** (사용자 결정 2026-10-27).
  - 실행기가 `C:\work\gnlink-wt\SLOT_REQUEST.txt` 를 CreateNew 로 **직접 만든 경우에만**, 끝날 때 그 한 파일을 지운다.
  - 이미 있으면 "slot busy" 로 종료하며 그 파일은 건드리지 않는다. 남이 만든 slot·stale slot 은 지우지 않는다.
  - 이 문서의 어떤 명령도 그 밖의 저장소 밖 파일·폴더를 지우지 않는다.
- `-NoBuild` 이면 아래 파일을 그대로 설치한다.
  - `-Seam false` → `caplife\sh_r2\app-noseam.apk`
  - `-Seam true` → `caplife\sh_r2\app-shseam.apk`
  - 첫 줄의 `apk <hash>` 와 `host <hash>` 로 무엇을 돌리는지 확인한다.
  - **후보로 돌리기 전에 복사한다**: `Copy-Item D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk D:\remote\remote\.claude\caplife\sh_r2\app-noseam.apk -Force`
  - `-NoBuild` 없이 실행하면 gradle 로 다시 빌드해 해시가 바뀐다. 독립 확인에는 쓰지 않는다.
- 결과: `caplife\sh_r2\<Tag>.log` 와 `caplife\sh_r2\<Tag>\`(스크린샷·host.log·app_log.txt).
- 화면: 패턴 창은 화면 밖 layered 1440x810 이다. 바탕화면 시나리오(ua2_desktop)는 주 모니터를 **캡처만** 한다.
- AVD 는 하네스가 띄우거나 재사용한다.
- 시간은 이번 배치의 실측이다(AVD 기동 상태 기준).

## 1) evidence A — 실제 APK UI × 로그인 전 역할 시험 Host (Codex codex_apk0232_evidence_design.md)
| 시나리오 | 명령 | APK | 시간 | 작업용 결과(후보 1a3eee63) |
|---|---|---|---|---|
| A 본시험 | `sh_run.ps1 -Scenario prelogin_ui -Tag v_A -Seam false -NoBuild` | 후보 | ~5분 | 15/15 |
| tripwire 대조 | `sh_run.ps1 -Scenario prelogin_tripwire -Tag v_A_trip -Seam false -NoBuild` | 후보 | ~1분 | 3/3 |
| 부정 대조(일반 Host) | `sh_run.ps1 -Scenario prelogin_ui_control -Tag v_A_ctl -Seam false -NoBuild` | 후보 | ~2분 | **FAIL 이 정상**: "no window list / WindowSelect" 가 lines=13 로 실패, local 선택 없음 |

- 구성:
  - 하네스가 시험 Host 를 `GNLINK_STREAM_TEST_PRELOGIN_ROLE=1` 과 `--capture-window-title GNLINK-SH-PATTERN` 으로 시작한다. source 는 시작부터 패턴 창(WGC)이다. 실 worker 의 DXGI 로그인 화면이 아니다.
  - 취소 단계는 `GNLINK_STREAM_TEST_PAUSE_PUBLISH_FILE`(실행 출력 폴더 안의 하네스 자기 파일)로 프레임을 잠시 잡는다.
  - 입력 주입은 none 이다(OS 입력·자격증명·SAS·재부팅 효과 0).
- 단계와 판정:
  - 목록 → PC → 바탕화면 카드 → **local 선택**(`selection local (logon screen) begun=1`) → 화면에 패턴 디코드 표시.
  - 프레임을 잡은 동안 취소: 목록이 유지되고, 풀린 늦은 프레임도 뷰어를 다시 열지 않는다.
  - 같은 연결에서 다시 선택: 새 local 선택과 표시.
  - 같은 연결에서 SH 끄기 / 약하게 / 강하게 / 화면 크기(실험) 를 거쳐 기본으로 돌아간다.
  - 전 구간에서 Host 에 window list / WindowSelect / thumbnail 이 0 이고 FORBIDDEN 도 0 이다.
- tripwire 대조: `=2`(역할은 광고하지만 사용자 데이터 비트를 남김) 에서 실제 APK 의 clipboard 요청이 FORBIDDEN 으로 기록되고 링크가 닫힌다.
- 스크린샷: `prelogin_0_list`, `prelogin_1_shown`, `prelogin_2_waiting`, `prelogin_2_cancelled`, `prelogin_3_again`, `prelogin_4_off|weak|strong`, `prelogin_5_display`.

## 2) UA-2 바탕화면 대상
`sh_run.ps1 -Scenario ua2_desktop -Tag v_ua2d -Seam false -NoBuild` — 후보, ~4분, 9/9.
- 흐름: CHECKING → IDLE "지금 바탕화면 캡처: DXGI" → GDI 요청(Host 답 3,3,1) → "요청: GDI 접수 · 아직 이 방식으로 캡처하지 않음" → DXGI 로 되돌리면 "적용됨".
- 스크린샷: `ua2d_1_idle`, `ua2d_2_gdi`, `ua2d_3_dxgi`.

## 3) 그 밖의 시나리오
| 항목 | 명령 | APK | 시간 | 작업용 결과 |
|---|---|---|---|---|
| 제품 설정 UI A↔B↔끄기·재시작 | `-Scenario method_ui -Tag v_method -Seam false -NoBuild` | 후보 | ~6분 | 16/16 |
| 창 대상 backend | `-Scenario ua2 -Tag v_ua2 -Seam false -NoBuild` | 후보 | ~2분 | 13/13 |
| 구 Host | `-Scenario ua2_old -Tag v_ua2old -Seam false -NoBuild -HostExe C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStreamMigrationTest.exe -Extra "--new-host-exe C:\work\gnlink-wt\apk-prelogin-select\build\apps\native_poc\Release\GNLinkStreamMigrationTest.exe"` | 후보 | ~5분 | 이번 후보로는 미실행(r2 때 7/7) |
| B 실제 Activity: letterbox·모서리·회전 버튼·크게보기 pan·wm size·입력 fence(holdplace)·fallback | `-Scenario layout_b -Tag v_layout -Seam true -NoBuild` | shseam | ~6분 | 47/47 |
| renderer fixture: 강도·A/B·crop/rot·크기 불일치·정지 redraw·출력 식별 | `-Scenario strength_ab -Tag v_ab -Seam true -NoBuild` | shseam | ~1분 | 35/35 |
| 화질 A/B · 터치 A/B · 비용 | `compare_ab` / `touch_ab` / `latency_ab` (같은 형식, `-Seam true -NoBuild`) | shseam | ~4 / 3 / 4분 | — / 36 / 6 (compare_ab 는 r6 무관이라 이번 후보로 미실행; 1d4a05f4 에서 26) |
| picker 실 로그인 전 Host | `sh_run.ps1 -Tag v_sel -NativeExe C:\work\gnlink-wt\apk-prelogin-select\build\apps\native_poc\Release\remote60_viewer_prelogin_login_e2e_test.exe -Extra "--android-select"` | — | ~2분 | 37/37 |
| picker 단위(슬롯 불필요) | `& ...\Release\remote60_client_session_prelogin_select_test.exe` | — | 수 초 | 60/60 |
| 게이트 | `python C:\work\gnlink-wt\apk-prelogin-select\automation\gnlink_check_fixed_server.py --apk <apk>` · `--self-test` | — | 수 초 | 후보 7 PASS · shseam FAIL(2) · self-test 54 |

- `--android-select` 시험은 자기 창 하나를 잠깐 앞으로 가져와 이 PC 데스크톱에서 SendInput 한다. 사용자가 작업 중일 때는 피한다.

## 판정하지 않는 것
- 스크롤 shimmer: 수치 oracle 이 없다. 연속 화면을 정성적으로 관찰하고, 수치 판정과 구분해 기록한다.
- 실기 화질·지연·발열.
- 진짜 worker 의 디렉터리 인증·SYSTEM/Winlogon·DXGI 로그인 화면 캡처: A 는 시험 Host 의 역할 광고다. `--android-select` 37/37 은 실 worker + 공유 세션 층이다. 둘을 단일 E2E 라고 부르지 않는다.


## 4) E1 — 통제 스크롤의 최종 출력 (Codex codex_apk0232_evidence_final.md NEEDS_EVIDENCE) — 2026-10-27 추가
하네스만 바꿨다: `fix/apk-prelogin-select` **5f53879**(`automation/sh_filter_avd.py`, `automation/sh_pattern_window.ps1`). 제품 0이며, 후보 APK 1a3eee63 를 **그대로** 쓴다(seam 불필요).
```
Copy-Item D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk D:\remote\remote\.claude\caplife\sh_r2\app-noseam.apk -Force
& D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario scroll_shimmer -Tag v_shimmer -Seam false -NoBuild
```
- 약 3분.
- 구성:
  - 화면 밖 layered 패턴(1440x810, 주 모니터 무조작). 플래그 파일이 있는 동안 내용 전체(얇은 글씨·1px 선·체커·색 블록)가 **33 ms 마다 4 px** 위로 스크롤한다(순환).
  - 매 원본 그림 오른쪽 아래에 **프레임 번호**(6자리 + 16칸 줄무늬, 흰=1/회색=0, 왼쪽이 최하위 비트)를 넣는다.
  - `pattern.log` 에 `scroll frame=<n> offset=<px> ms=<t>` 를 기록한다(원본 약 28 fps).
- 한 연결에서 구간 4개: `off`(끄기) / `A_weak_s20`(약하게 0.20) / `A_strong_s60`(강하게 0.60, 기본 A) / `B_display_s60`(설정 "화면 크기 (실험)" + 0.60).
- 구간마다 남는 것:
  - `shimmer_<구간>.mp4`: 폰 화면 `adb screenrecord` 4 s, 스크롤 중. 녹화 간격은 screenrecord 가변 프레임이며, 누락·중복은 영상 안의 프레임 번호로 대조한다.
  - `shimmer_<구간>_still1.png`·`_still2.png`: 녹화 직후 아직 스크롤 중일 때 0.3 s 간격으로 찍은 연속 정지 2장.
  - `shimmer_<구간>_after.png`: 정지 후.
- `shimmer_summary.json`: 구간별 원본 프레임 범위, 녹화 초, 앱의 그리기 경로 로그, 두 정지의 평균 차.
  - 강하게는 약하게에서 같은 renderer 의 강도만 바뀌므로 경로 로그가 없을 수 있다.
- 판정은 "스크롤 중에 녹화됨"(mp4, 원본 프레임 범위, 두 정지가 다름)뿐이다. **반짝임·halo·세부 소실은 사람이 연다**(점수 없음).
- 작업용 실행(후보 1a3eee63, Host 881b3e05, 콘솔 세션): 6/6.
  - 원본 프레임 1–184 / 185–374 / 375–545 / 546–742.
  - 결과 폴더 `caplife\sh_r2\shimmer_2\`.
