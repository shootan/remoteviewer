# rel-0164 / old-apk-lane / r1 — 시험 도구 보고 (기기 실행 0)

## 고정
- 시험 commit **ce90b8e** (branch `test/old-apk-lane`, 부모 f12c9ff ← 53fced4). 53fced4 대비 **제품 파일 변경 0**입니다(`apps/directory/test/*`, `*_test.cpp`, `*_tool.cpp`, `e2e_job_run*`, CMake 시험 target, `automation/old_apk_lane/*`만 바뀌었습니다).
- APK 두 개를 씁니다.
  - published `D:\remote\remote\dist\GNLink-0.2.32.apk`: sha256 `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab`
  - Arm S fixture build `C:\work\gnlink-wt\old-apk-lane\apk_out\apk-0232-fixture.apk`: sha256 `73549dbbf2af79425e0a1af916507c6ca087bfb32217ece3741940140aed61e8`. 같은 소스 b554e76이고 `-Pgnlink.testDirectoryUrl=http://10.0.2.2:18410`로 만들었습니다.

## 전제 충돌과 두 arm (판정 대기)
published 0.2.32는 DIRECTORY_URL이 `https://gnlink.shotan.net`으로 고정돼 있습니다(b554e76 `build.gradle.kts:6/50`). 그래서 로컬 fixture 서버에 로그인할 수 없습니다. TLS 가로채기나 AVD system CA는 금지입니다. 그래서 두 arm으로 나눴습니다.

- **Arm S**: 같은 소스로 만든 fixture build입니다. ①~⑦을 디렉터리 경로로 돌립니다. 바이트는 published와 다릅니다(URL 상수만 다릅니다).
- **Arm P**: published 바이트를 그대로 씁니다.
  - ①은 **NOT RUN**입니다.
  - ②는 0.2.32의 manual connect로 갑니다(intent extras host=10.0.2.2, videoPort=host 로그의 bindPort, controlPort=18460, loginManualButton → connectButton).
  - ⑤는 manual 모드라 0.2.32가 park를 거부합니다. 그래서 `park_refused manual=true`를 판정합니다.
  - ⑥은 manual connect를 다시 한 뒤 desktop을 고르는 것입니다.
- 어느 arm을 쓸지(또는 둘 다)는 검증용·Codex가 판정할 몫입니다. 도구는 둘 다 지원합니다.

## 실행 (검증용)
1. **build**: ce90b8e를 checkout합니다(제품 소스는 53fced4와 같습니다). 다음 target을 빌드합니다.
   - `remote60_mobile_abr_session_e2e_test` (guard 포함, `--host-control-port` 추가)
   - `remote60_native_video_host_pinned_select` (GNLinkStreamPinnedSelectTest)
   - `remote60_viewer_ka_test` (GNLinkViewerKaTest)
   - `remote60_ka_standby_holder_tool`
   - `remote60_e2e_job_run`

   ⚠️ verify-0164의 현재 build-verify(f12c9ff)에 있는 harness는 guard가 없는 53fced4 판입니다. 그래서 위 target들을 ce90b8e에서 다시 빌드해야 합니다.
2. **명령 한 줄**: sh_run.ps1 슬롯 아래에서 `-NativeExe`로 실행합니다. `<B>` = 위 build의 `...\Release`이고, `<WT>` = ce90b8e checkout입니다.
   - Arm S:
     `powershell -NoProfile -ExecutionPolicy Bypass -File <WT>\automation\old_apk_lane\oa_slot.ps1 -Tag oa-s -Arm S -WithStandby -Apk C:\work\gnlink-wt\old-apk-lane\apk_out\apk-0232-fixture.apk -BuildDir <B> -JobRun <B>\e2e_job_run.exe -ServerJs D:\remote\remote\.claude\worktrees\verify-0164\apps\directory\server.js -AdbSha256 7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606`
   - Arm P: 위와 같고 `-Tag oa-p -Arm P -Apk D:\remote\remote\dist\GNLink-0.2.32.apk`만 다릅니다.
   - S 뒤에 P를 돌리면(서명자가 다르므로) 설치가 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`로 **INVALID 중단**됩니다. 이때 `OA_REINSTALL=1`을 주면 시험 패키지를 uninstall한 뒤 설치합니다. 데이터는 어차피 pm clear로 지워집니다. 사용 여부는 검증용이 판단해 주세요.
3. **결과**:
   - `<WT>\.claude\oa_runs\<tag>-<시각>-<pid>\`에 `slot.txt` · `fixture.txt` · `avd\steps.txt` · `avd\result.json` · 스크린샷 · `standby\standby_state.txt`가 남습니다.
   - 판정은 PASS / FAIL / NOT RUN으로 나뉩니다. 도달하지 못한 단계는 `not reached`로 NOT RUN에 남고, 빠지지 않습니다.
   - 설치된 base.apk를 되받아 해시를 APK와 대조합니다. versionName도 0.2.32인지 봅니다. 둘 중 하나라도 다르면 아무것도 판정하지 않고 exit 2로 끝납니다.

## 판정 근거 (0.2.32 자체의 것만)
- **UI id**: loginIdInput · loginPasswordInput · loginButton · hostCardName · listDevicesButton · targetCardTitle · viewerMenuButton / viewerMenuHostsButton / viewerMenuCloseButton · loginManualButton · connectButton
- **log tag**: login_attempt · directory_chosen · targets_ready · select_ready ms= · video_debug out= · park_begin / park_confirmed / park_reuse / park_refused · scene
- **④**: host가 input none이므로 테스트 host 로그의 새 `[input-trace]` 줄 수로 봅니다.
- **⑦**: holder의 `standby_state.txt`에서 ready 이후 state 변화가 0이고, viewer EXITED가 없어야 합니다. holder가 먼저 죽으면 fixture LATE FAILURE가 되어 번들 rc 3입니다.
- **guard·격리**: 공용 guard(input none + power off + 두 줄 gate)를 거칩니다. fixture는 kill-on-close job 안에서 돌고, run folder는 새로 만듭니다(재사용 거부). adb는 절대 경로로 쓰고 sha pin을 겁니다. 5037 조회는 읽기만 하고, clipboard 변경은 0입니다. 계정 비밀번호는 run folder 파일에만 있고 출력에는 없습니다.

## stub 대조 (기기 0)
- `oa_orchestration_test.ps1`: **17 checks, 0 failed, RESULT: PASS, rc 0**. 확인한 경우는 다음과 같습니다.
  - S+standby 정상
  - P는 harness에 `--host-control-port 18460`이 전달되고 holder는 없음
  - standby가 끝내 ready가 안 됨 / 즉시 죽음 / AVD 도중 죽음
  - host exit2 · foreign readiness · node 죽음 · host 늦은 실패 · AVD 실패
  - fixture crash 뒤 job이 잔여를 정리(confirmed)
  - holder 정리 오류
  - APK 없음
  - stale root와 anchor 밖 root 거부
  - 모든 경우에 잔여 프로세스 0
- `oa_adb_redact_test.py`: 7/7 PASS.
- PS parse 0 error, py ast OK.
- build: harness · holder · job_run 모두 OK. `e2e_job_run_start_test`는 8/8입니다(이전 회차 결과입니다).

## 한계 (미검증)
- `oa_avd.py`(기기 쪽 시나리오)는 **기기 없이는 실행되지 않았습니다**. stub 대조는 orchestration만 덮습니다.
- `remote60_ka_standby_holder_tool`은 빌드만 했고 **실 viewer·서버로 실행한 적이 없습니다**.
- Arm P의 manual connect가 새 Host의 TCP control(127.0.0.1:18460, 에뮬레이터는 10.0.2.2)과 실제로 붙는지는 **실행 전에는 미확인**입니다.
- 화면 좌표 1200,540(④ 탭과 메뉴 깨우기)은 ke 하네스에서 그대로 가져온 값입니다.
- ④는 탭 1회 뒤의 새 `[input-trace]`만 셉니다. 그런데 ⑤의 `menu()`가 컨트롤을 깨우려고 같은 좌표를 최대 3회 더 탭합니다. 그래서 host 로그 전체의 input 수는 1보다 많을 수 있습니다. 주입은 하지 않고 기록만 됩니다.
- base.apk 해시 대조는 `adb install`이 바이트를 바꾸지 않는다는 전제에 기대고 있습니다. 바이트가 바뀌면 fail-closed로 exit 2가 되고 아무것도 판정하지 않습니다. 이를 versionName + 서명자 대조로 완화할지는 검증용이 판단해 주세요. 미리 완화하지 않았습니다.
- harness의 `--host-control-port` 인자 처리는 diff로 확인했습니다(이웃 인자와 같은 `argv[++i]` 형태). 실행으로 확인하지는 않았습니다.


---

# r2 (Codex codex_rel_0164_old_apk_lane_decision.md — S+P 대체 수용, 보정)

이 절이 r1의 Arm S APK·명령·⑤⑥ 매핑을 **대체**합니다. 아직 EVIDENCE나 출하 OK가 아니며, 실행은 검증용이 합니다.

## 고정
- 시험 commit **7714c85** (test/old-apk-lane; 부모 ce90b8e ← f12c9ff ← 53fced4).
  - 53fced4 대비 바뀐 것은 시험 파일뿐이고 제품 파일은 0입니다.
  - r2에서 바뀐 것은 `oa_avd.py`와 새 `oa_apk_diff.py` 둘입니다.
- lane snapshot: `D:\remote\remote\.claude\oa_lane_r2_SHA256SUMS.txt` (11파일, 파일 자체 sha256 `5ae2c649…4535`).
- APK:
  - **Arm P** published `D:\remote\remote\dist\GNLink-0.2.32.apk` — `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab` (변경 없이 사용)
  - **Arm S** `C:\work\gnlink-wt\old-apk-lane\apk_out\apk-0232-fixture-r2.apk` — `50080b2bd586ee66ce446ab8f647ce4e5c11fafc22593d15bef7e948d2816962`
    - b554e76(detached worktree, tracked 변경 0)을 `:app:assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410 -Pgnlink.testMigratableOrigins=https://rem.shotan.net`으로 만들었습니다.
    - migratable 값은 출하값과 같게 기존 property로 고정했습니다(Codex 보정). 이 보정이 없는 r1의 `73549dbb…`는 폐기합니다.
  - Arm S build의 BuildConfig는 `DEBUG=true` · `BUILD_TYPE=debug` · `DIRECTORY_URL=http://10.0.2.2:18410` · `DIRECTORY_MIGRATABLE_ORIGINS=https://rem.shotan.net` · `UPDATE_MANIFEST_URL=""`이고, `UPDATE_PUBLIC_KEY_HEX`는 기본값(8709ea70…)입니다.

## Arm S와 Arm P(게시본)의 실제 차이 — 파일에서 직접 비교 (`oa_apk_diff.py`, `oa_so_diff.py`)

| 항목 | published (P) | fixture (S) | 같음 |
|---|---|---|---|
| package / version | com.remote60.androiddirect 0.2.32 (31) | 같음 | 예 |
| build type / debuggable | **debuggable** | debuggable | 예 |
| signer (v2, cert #1) | CN=Android Debug, sha256 `dcc806aeb30b2e3c…` | 같은 인증서 | 예 |
| min/target SDK, ABI | 28/34, arm64-v8a armeabi-v7a x86 x86_64 | 같음 | 예 |
| dex 전체 sha256 | c504991c74a1462e | 7d506b3d74275179 | 아니오 |
| dex 문자열 차이 | `https://gnlink.shotan.net` + BuildConfig class checksum `c742ccda` | `http://10.0.2.2:18410` + `6fd564ca` | **이 둘뿐** |
| dex `https://rem.shotan.net` | 1 | 1 | 예 |
| dex SharpenTestSeam 참조 | 3 | 3 | 예 (둘 다 noseam stub; native `sh-test-seam` 0/4) |
| native `.so` (4 ABI) | 예: x86_64 `9506a0c9…` | x86_64 `8b29b64a…` | 아니오 — 아래 설명 |

- **native 차이의 실체**: 다른 문자열은 assert가 박아 넣는 `__FILE__` 한 줄뿐입니다(`C:/work/gnlink-wt/apk-prelogin-select/...` 대 `C:/work/gnlink-wt/apk-0232-src/...android_video_decoder.cpp`). 그 결과 build-id와 이후 offset이 달라졌고(크기 차 16 B), `liveness-trace`·`sh-test-seam` 표지는 양쪽 모두 0입니다.
- **확인된 사실**: 게시된 0.2.32 자체가 debug build type이고 같은 Android Debug 키로 서명돼 있습니다. 그래서 signer·debuggable·build type 차이는 **없습니다**.
  - r1 보고에서 "서명자 달라 S→P 설치 거부, OA_REINSTALL 필요"라고 한 것은 **틀렸습니다**. 같은 versionCode·같은 키이므로 `install -r -d`로 서로 교체됩니다.
  - OA_REINSTALL 경로는 다른 키로 서명된 예전 설치본이 있을 때만 쓰입니다.
- **유효 설정 차이는 `DIRECTORY_URL` 하나**이고, 근거는 위 dex 문자열 비교입니다. 다만 이 값에서 파생되는 동작은 다릅니다.
  - **update 확인**: P는 시작할 때 `https://gnlink.shotan.net/api/update/manifest?platform=android`를 확인합니다(`DirectoryClient.updateManifestUrlFor`는 https일 때만 URL을 만듭니다). S는 http라서 NotConfigured이고 아무 요청도 하지 않습니다.
  - **로그 업로드**: 세션이 있을 때만 하며, S는 fixture로 보냅니다.
  - **TLS/DNS**: S는 HTTP fixture라 실제 HTTPS·TLS·DNS 경로를 검증하지 않습니다.
  - 자동 update와 업로드는 이 lane의 성공으로 세지 않습니다.
- **SDK·도구**: 게시본의 빌드 기록이 남아 있지 않아 AGP·NDK 버전을 직접 대조하지는 못했습니다. dex와 native가 위 두 차이를 빼면 문자열 수준에서 같다는 것이 간접 근거입니다.

## ①~⑦ arm별 매핑 (실제 UI·transport·판정; 다른 흐름에 같은 번호를 쓰지 않음)

| # | Arm S (fixture build, 새 server + 새 Host) | Arm P (게시본, 새 Host 정상 모드 수동 직접 연결) |
|---|---|---|
| 전제 | 설치 → `pm clear` → base.apk 되받아 해시·versionName 대조 | 같음, 그리고 **앱 시작 1회 직후 `auto_login NO_CREDENTIAL` · `sign_in_store` 0줄**. 아니면 INVALID(즉시 force-stop) |
| ① 로그인 | loginIdInput/Password → loginButton → `card:oa-pc-a` 목록 (`login_attempt`) | **NOT RUN** — 고정 https 서버 전용이라 이 lane의 fixture가 아님 |
| ② 연결 | 카드 탭 → targets (`directory_chosen` · `targets_ready`). 0.2.32가 실제로 탄 경로는 Host 로그 줄로 기록(Host가 127.0.0.1에 묶여 있어 relay가 예상되지만 단정하지 않음) | 수동 직접 연결: extras host=10.0.2.2 / videoPort=Host bindPort / controlPort=18460 → loginManualButton → connectButton → targets. Host 로그에 `control waiting port=18460`이 있고 `[control] client connected`가 **+1**이어야 함 (TCP control + UDP video; requireTcpControl=true, controlOverUdp=false) |
| ③ 첫 영상 | listDevicesButton → targetCardTitle → `select_ready ms=` · 스크린샷 sha256 · `video_debug out=` | 같음 |
| ④ 입력 1회 | 탭 1200,540 → Host `[input-trace]` 새 줄 > 0 (input none: 기록만 하고 주입 안 함) | 같음 |
| ⑤ PC 목록 | viewerMenuHostsButton → `park_begin` + `park_confirmed` + 목록 | **NOT RUN** — 수동 UI에 PC 목록이 없음(`SessionPark.canPark` false, SessionPark.kt:78) |
| P-5d | — | (⑤ 아님) viewerMenuDisconnectButton → 확인 → `viewer_disconnect`, connect 폼 복귀, Host `tcp client disconnected` +1, PC 목록 버튼 미노출 확인 |
| ⑥ 재선택(resume) | 같은 카드 → `park_reuse` → 다시 desktop → 첫 영상 | **NOT RUN** — park된 것이 없음 |
| P-6r | — | (⑥ 아님) 같은 앱에서 connectButton(앱 재시작 없음) → 새 세션, Host TCP client +1 → desktop → 첫 영상 |
| ⑦ standby 무간섭 | 같은 Host의 PC 쪽 standby 1개 유지 (holder `standby_state.txt`에 ready 뒤 변화 0, EXITED 0; holder가 먼저 죽으면 번들 rc 3) | 같음 (`-WithStandby`) |

- Codex 지시대로 새 Host의 인증 directory · standby activation · NP relay 조합은 이 lane이 아니라 S와 별도 suite가 맡습니다. P를 directory UDP나 relay로 넓히지 않습니다.
- peer_lost나 첫 영상 실패를 "알려진 APK 결함"으로 자동 면제하지 않습니다. 단계별 판정 그대로 FAIL로 둡니다.

## Arm P 운영 로그인 방지 (Codex 조건)
- **경로**: onCreate의 `initDirectoryUi`가 `beginAutoLogin`을 시작합니다. 이 함수는 `settleOwedSignOuts`(owed 목록) → `comeBack`(vault.load) 순서로 돕니다.
  - b554e76 `LoginFlow.kt:225-226`: vault가 비어 있으면 `NO_CREDENTIAL`로 끝나고 아무것도 보내지 않습니다.
  - `LoginFlow.kt:603-618`: owed 목록이 비어 있으면 revoke 요청도 0입니다.
  - vault는 `KeystoreLoginVault` = 앱 자신의 SharedPreferences + keystore이고, `pm clear`가 둘 다 비웁니다.
- **도구가 하는 일**: 설치 → `pm clear` → (그 사이 앱을 시작하지 않음) → 시작 **1회**. 그다음 manual 버튼을 누르기 **전에** 앱 로그의 `[auto_login] NO_CREDENTIAL`과 `[sign_in_store]` 0줄을 확인합니다. 다르면 즉시 force-stop하고 INVALID(exit 2)로 끝내며 아무것도 판정하지 않습니다.
  - P-6r도 앱을 다시 시작하지 않으므로 자동 로그인 시도와 update 확인은 각 1회뿐입니다.
- **범위**: 사용자 실제 앱 데이터와 물리 기기는 다루지 않습니다. 대상은 공용 **시험 AVD**의 시험 패키지뿐입니다.
- ⚠️ **검증용·Codex 판단이 필요한 1건**: 게시본은 시작할 때 **운영 origin으로 무인증 update manifest GET 1회**를 보냅니다(세션이 없어 credential 헤더 없음, 로그인 아님). 시험 AVD에 인터넷 경로가 있으면 실제로 나갑니다. 도구는 그 결과(`update_check` 줄)를 기록할 뿐 막지 않습니다. DNS·hosts·CA 우회는 금지라 막을 수단도 없습니다.
  - 이 1회 GET을 허용할지, 아니면 그 때문에 P를 보류할지 정해 주세요.

## 실행 (검증용; sh_run.ps1 슬롯 안에서 `-NativeExe`; 기존 guard·job·slot·adb pin 유지)
- **build**: 7714c85 checkout에서 `remote60_mobile_abr_session_e2e_test` · `remote60_native_video_host_pinned_select` · `remote60_viewer_ka_test` · `remote60_ka_standby_holder_tool` · `remote60_e2e_job_run`을 빌드합니다. 제품 소스는 53fced4와 같습니다.
- **Arm S**:
  `powershell -NoProfile -ExecutionPolicy Bypass -File <WT>\automation\old_apk_lane\oa_slot.ps1 -Tag oa-s -Arm S -WithStandby -Apk C:\work\gnlink-wt\old-apk-lane\apk_out\apk-0232-fixture-r2.apk -BuildDir <B> -JobRun <B>\e2e_job_run.exe -ServerJs D:\remote\remote\.claude\worktrees\verify-0164\apps\directory\server.js -AdbSha256 7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606`
- **Arm P** (위 update GET 판단 뒤): 위와 같고 `-Tag oa-p -Arm P -Apk D:\remote\remote\dist\GNLink-0.2.32.apk`만 다릅니다.
- **APK 차이 재현**: `python -I <WT>\automation\old_apk_lane\oa_apk_diff.py <published.apk> <fixture.apk> <out.md>`

## S 뒤 P 실행 순서와 데이터 초기화 범위
- 순서: Arm S 한 번(슬롯 하나) → Arm P 한 번(슬롯 하나). 각 arm은 `oa_avd.py`가 시작할 때 `install -r -d` → `pm clear com.remote60.androiddirect` → 되받은 base.apk 해시와 versionName 대조를 합니다. 그래서 S가 남긴 앱 상태가 P로 이어지지 않습니다.
- 초기화 범위: **공용 시험 AVD(emulator-5554)의 시험 패키지 `com.remote60.androiddirect` 하나**의 앱 데이터만 비웁니다(SharedPreferences·keystore 항목·앱 전용 저장소). 다른 패키지, AVD 전체, 사용자 물리 폰과 그 앱은 건드리지 않습니다.
- OA_REINSTALL=1: 위 차이표대로 두 APK의 서명 인증서가 같아서(dcc806ae…) S→P 교체에는 **필요 없을 것으로 봅니다**. 다만 시험 AVD에 다른 키로 서명된 예전 설치본이 있으면 설치가 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`로 INVALID 중단됩니다. 그때 `OA_REINSTALL=1`로 그 패키지만 uninstall한 뒤 다시 설치할지는 검증용이 실행 때 판단합니다. 초기화 범위는 위와 같습니다.

## stub 대조 (기기 0)
- `oa_orchestration_test.ps1` 17/17 PASS rc 0 (r2 재실행). `oa_adb_redact_test.py` 7/7 PASS. `oa_avd.py` ast OK.
- `oa_avd.py`의 기기 쪽 단계(P 전제 판정, P-5d·P-6r, Host 로그 TCP client 계수)는 **기기 없이 실행되지 않았습니다**.

## 권장 보고 문구 (Codex)
"0.2.32 원 Source의 fixture endpoint build로 새 서버의 directory/relay 경로를 확인하고, 게시 APK 원본은 새 Host와 수동 직접 연결로 확인했다. 게시 APK 원본의 고정 HTTPS 주소를 통한 새 서버 로그인·relay는 배포 전 직접 검증하지 않았다."

두 결과를 합쳐 "게시 binary의 새 서버 end-to-end 직접 검증 완료"라고 쓰지 않습니다.

---

# r3 (Codex codex_old_apk_lane_src_r2.md — update GET 허용, N1·N2 보수)

이 절은 r2를 보수합니다. 아직 SOURCE OK가 아니며, 실행은 SOURCE OK 뒤 검증용이 합니다.

## 고정
- 시험 commit **494d05b** (test/old-apk-lane; 부모 7714c85). 제품 파일 변경은 0입니다.
- snapshot은 `D:\remote\remote\.claude\oa_lane_r3_SHA256SUMS.txt`(파일 sha256 `4ca00450…7c86`)입니다.
  - 17개 파일 각각에 **canonical**(commit blob, LF) hash와 **raw**(이 checkout의 바이트) hash를 함께 적었습니다.
  - 두 값이 다른 4개(`oa_adb.py`, `oa_adb_redact_test.py`, `oa_avd.py`, `stub_harness.py`)는 checkout의 CRLF 차이일 뿐 내용 차이가 아닙니다.
- APK는 r2와 같습니다: P `1a3eee63…815ab`, S `50080b2b…6962`.
- **update GET**: Codex가 허용했습니다. P의 초기화된 앱이 1회 시작할 때 생기는 무인증 운영 update manifest 읽기 1회만 해당합니다. download·install·update 버튼·credential·로그 업로드로 넓히지 않습니다. 도구는 시작 횟수(1)와 `update_check` 줄을 기록합니다.

## N1 — 초기화 성공 전에는 앱을 시작하지 않음
- `oa_avd.py`는 다음 두 가지가 모두 확인된 뒤에만 앱을 시작합니다(`am start`).
  - `install`이 rc 0이고 출력에 `Success` 줄이 있음
  - `pm clear com.remote60.androiddirect`가 rc 0이고 stdout이 정확히 `Success`임(timeout 60 s, `clear_app_data()`)
- 그 밖의 경우(rc≠0, `Failed`, 빈 출력, timeout, 실행 실패)는 **INVALID exit 2**입니다. 이때 `am start`와 `input tap`은 0이고, 실패한 초기화를 재시도하지 않습니다.
- P의 시작 뒤 `auto_login NO_CREDENTIAL`·`sign_in_store` 0줄 확인은 추가 방어로 그대로 둡니다.
- 초기화 범위는 시험 AVD emulator-5554의 이 패키지 하나입니다. signer가 다를 때도 자동 uninstall은 하지 않습니다(OA_REINSTALL은 명시했을 때만).
- **대조 `oa_pmclear_test.py`** (기기 0): `oa_avd.py` 자체를 fake adb(`stubs/fake_adb.cmd`)로 돌립니다.
  - **494d05b: 12/12 PASS**
    - P·S 각각 pm clear rc1·Failed·빈 출력·timeout → INVALID, 앱 시작 0, tap 0, clear 1회
    - install 실패 → 앱 시작 0, clear 0
    - 대조(둘 다 Success) → 앱 시작 1 (막다른 문이 아님)
    - adb pin이 걸린 상태에서 fake adb → adb 호출 0으로 거부
  - **7714c85의 `oa_avd.py`(음성): 9 FAIL** — 초기화나 설치가 실패한 뒤에도 `am start`가 실행됐습니다. 기록은 `.claude/oa_pmclear_neg_7714c85.txt`(worktree)입니다.
  - fake adb는 `OA_TEST_FAKE_ADB`일 때만 쓰입니다. adb pin(`KE_ADB_SHA256`)과 같이 주면 import 단계에서 거부합니다. 실제 실행 경로인 `oa_slot.ps1`은 pin을 걸고 이 변수를 지웁니다.

## N2 — standby holder 종료 판정
- `remote60_ka_standby_holder_tool`의 exit는 이제 `ka_standby_holder_verdict.hpp`에서 정합니다.

  | exit | 뜻 |
  |---|---|
  | 0 | ready이고, Release를 보냈고, 15 s 안에 스스로 끝났고, 공용 child 판정(`children_end_all`: 종료 확인·EOF·log 보존)이 깨끗함 |
  | 4 | viewer가 스스로 죽음 |
  | 3 | ready가 없음 |
  | 5 | Release 송신 실패이거나 15 s 안에 끝나지 않음. job이 끝낸 것은 **cleanup으로만** 기록하며 pass가 아님 |
  | 6 | child 판정 실패 |

  여러 조건이 겹치면 4 → 3 → 5 → 6 순서로 앞의 것이 exit가 됩니다.
- **대조**
  - `ka_standby_holder_verdict_test` 7/7 (ready 뒤 released=false → 5, child 오류 → 6, 정상 → 0 등)
  - `oa_orchestration_test` **18/18**: 새 case로 holder가 ready 뒤 stop에서 exit 5를 내면 fixture가 `standby exit 5`로 세고 번들 rc 3이 되는지 봅니다.
  - `oa_adb_redact_test` 7/7
- 실제 viewer·서버로 holder를 돌린 적은 여전히 없습니다(기기·Host 실행 0).

## 문구 정정 (r2 차이표에 대한 것)
- `oa_apk_diff.py`와 `oa_so_diff.py`는 metadata, 특정 문자열의 수, printable 문자열 집합, hash와 길이를 비교합니다. **이것만으로 DEX bytecode 전체나 ELF 명령의 차이가 그 두 항목뿐이라고 증명하지는 못합니다.**
- 따라서 r2의 "dex 문자열 차이는 이 둘뿐", "유효 설정 차이는 DIRECTORY_URL 하나"는 다음과 같이 고칩니다: **같은 source(b554e76)와 property 통제(testDirectoryUrl·testMigratableOrigins)라는 build 근거, 그리고 이 도구들로 관측된 차이**(dex 문자열은 DIRECTORY_URL과 BuildConfig checksum, native 문자열은 `__FILE__` 경로 한 줄, 나머지는 hash와 offset 차이).
- 같은 debug 키, debuggable, ABI, SDK가 같다는 것은 apksigner·aapt2의 직접 출력입니다.

## 실행 (r2와 같음, commit만 494d05b)
- build: 494d05b에서 5 target(r2 목록)을 빌드합니다. holder 판정이 바뀌었으므로 `remote60_ka_standby_holder_tool`은 반드시 다시 빌드해야 합니다.
- 명령은 r2 절 그대로이고 `<WT>`만 494d05b checkout입니다. 순서는 S 한 번 → P 한 번(각각 별도 슬롯)입니다.

---

# r4 (검증용 실행 oa-s / oa-p 2026-10-29 23:56 INVALID — 기본 holder 경로)

- **시험 commit 949fe07** (부모 494d05b). 제품 파일 변경은 0입니다.
- **snapshot**: `D:\remote\remote\.claude\oa_lane_r4_SHA256SUMS.txt`. 18개 파일 각각에 canonical(LF blob)과 raw(이 checkout) hash를 적었습니다.
- **원인**: `oa_fixture.ps1:214`의 `$hc = if (…) { $HolderCmd } else { @("<exe>") }`.
  - PowerShell의 if **식**은 출력이 파이프라인을 지나갑니다. 그래서 원소가 하나인 배열이 풀려 문자열이 됐고, `$hc[0]`이 첫 글자 'D'가 되어 상대 경로로 해석됐습니다.
  - stub 대조는 늘 `-HolderCmd`를 여러 원소로 줬기 때문에 이 기본 경로를 지나간 적이 없습니다.
- **보수**:
  - `$hc = @(if …)`로 배열을 고정했습니다.
  - oa_*.ps1 · ke_*.ps1 전체를 grep했습니다. 다른 if/switch 식 대입은 모두 스칼라(문자열·숫자)이고, 배열로 받아 인덱싱하는 곳은 이 한 곳뿐이었습니다. ke_*.ps1에서는 해당 패턴이 0건이라 변경하지 않았습니다.
  - `oa_slot.ps1`이 `-Apk / -BuildDir / -ServerJs / -JobRun`이 절대 경로가 아니면 시작 전에 INVALID(exit 2)로 거부합니다. `oa_fixture.ps1`도 상대 build 폴더를 거부합니다.
- **대조** (기기 0):
  - `oa_orchestration_test.ps1` **20/20**. 새로 2 case를 넣었습니다.
    - **기본 holder 경로**: `-HolderCmd` 없이 돌리면, 이 run의 stub build 폴더에 실제 이름으로 놓은 `remote60_ka_standby_holder_tool.exe`를 fixture가 직접 hash하고 실행합니다. 그 exe는 새 시험 전용 stand-in `remote60_oa_stub_holder.exe`이고 viewer·network를 쓰지 않습니다.
    - **상대 `-BuildDir` 거부**.
  - **음성**: 494d05b의 `oa_fixture.ps1`로 같은 시험을 돌리면 기본 경로 case만 FAIL합니다(rc 2). 오류는 `Cannot find path 'D:\remote\remote\C'`로, 검증용 실행과 같은 오류이고 drive 글자만 stub 폴더에 맞게 C입니다.
- **build**: 검증용 실행에 필요한 target은 r2/r3과 같습니다. `remote60_oa_stub_holder`는 orchestration 대조에만 필요합니다.

---

# r5 (검증용 실행 oa-s / oa-p 2026-10-30 00:14 INVALID — holder의 viewer env 누락)

- **시험 commit 953aef2** (부모 949fe07). 제품 파일 변경은 0입니다.
- **snapshot**: `D:\remote\remote\.claude\oa_lane_r5_SHA256SUMS.txt`(canonical + raw).
- **holder 재빌드**: `remote60_ka_standby_holder_tool.exe` = `e9ae941514db0c3f2c967136a94573103aafa354c7e982bb792841aa231e4084`(lane build, 953aef2). 검증용은 이 commit에서 다시 빌드해야 합니다.
- **원인**:
  - holder가 띄운 `GNLinkViewerKaTest --managed-standby`가 h264를 쓰려면 `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`이 필요합니다(viewer_startup.cpp:134).
  - holder는 viewer의 env를 호출자 env 상속에 맡겼습니다. 호출자인 fixture에는 그 변수가 없었습니다.
  - ka-pc 시험은 자기 프로세스 env에 넣어 두었고(ka_pc_managed_e2e_test.cpp:292), 제품 shell은 viewer를 띄우기 직전에 넣습니다(client_managed_viewers.hpp:149).
- **보수**:
  - viewer의 env 블록에 `FORCE=1`과 창 seam의 X/Y를 **명시적으로** 넣습니다(`ka_standby_holder_env.hpp: env_block_with`, 같은 이름이 있으면 대체).
  - holder는 viewer가 받은 FORCE 값을 standby_state에 기록합니다.
  - 이 holder를 쓰는 곳은 oa_fixture 하나입니다. ka-pc e2e는 holder를 쓰지 않고, 자기 viewer에는 프로세스 env로 FORCE를 줍니다(정상 동작 중).
- **기기 없는 대조 `oa_holder_viewer_check.ps1`**:
  - **실제** holder로 **실제** GNLinkViewerKaTest(`6806dd41…`)를 띄웁니다. loopback directory(18521/18522, relay off, 시험 계정 1개)를 쓰고 Host는 없습니다. Init에는 존재하지 않는 host id를 넣습니다. 시작 전에 호출자 env에서 FORCE를 지웁니다.
  - **음성** `-Negative`(`--omit-viewer-encoded-env 1`, r4와 같은 방식): `unsupported codec: h264` → viewer가 스스로 종료 → holder rc 4. **검증용 실행과 같은 실패가 재현됐습니다.**
  - **양성**: codec 거부 0. viewer가 `[viewer][standby] standing by for oa-check-no-such-host` 상태로 Init 뒤 6 s 이상 살아 있었고, Release 뒤 종료했습니다. holder rc 3은 Host가 없어 ready가 오지 않은 것으로, 예상한 값입니다. **PASS**.
  - 잔여 프로세스와 18521 listener는 0입니다.
- `ka_standby_holder_verdict_test` 9/9: env 블록 2 case를 추가했습니다(대체 1건, 나머지 보존, 아무것도 넣지 않으면 호출자 값 그대로).

## lane이 띄우는 프로세스별 필수 env 점검표 (세 번째 INVALID 방지)

| 프로세스 (누가 띄우나) | 시작에 필요한 env | 누가 넣나 | 상태 |
|---|---|---|---|
| `node server.js` (oa_fixture) | `REMOTE60_DIR_DATA`·`DIR_PORT`·`DIR_UDP_PORT`·`LOG_DIR`·`RELAY_*` | oa_fixture `$dirEnv`(Start-Owned가 child 시작 때 설정) | 있음 |
| harness `remote60_mobile_abr_session_e2e_test` (oa_fixture) | `REMOTE60_ALLOW_HOST_E2E=1`(없으면 SKIPPED), `REMOTE60_PINNED_TEST_FAULT_FILE` | oa_fixture Start-Owned | 있음 |
| 시험 Host `GNLinkStreamPinnedSelectTest` (harness) | `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`(h264), `REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC`, `REMOTE60_CLIPBOARD_SYNC`, 시험 seam(REMOTE60_PINNED_SELECT_TEST_SEAM)은 compile define | harness가 Host launch 직전에 설정(mobile_abr_session_e2e_test.cpp:882–886) | 있음 (r1–r4 실행에서 Host ready 확인) |
| standby holder (oa_fixture) | 없음(인자만) | — | — |
| managed viewer `GNLinkViewerKaTest` (holder) | **`REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1`**(h264), `REMOTE60_VIEWER_TEST_WINDOW_X/Y`(화면 밖), 격리 `LOCALAPPDATA` | **holder가 child env 블록에 명시(r5)** | **r4까지 누락 → r5 수정, 실제 대조 PASS** |
| `e2e_job_run` (oa_slot) | 없음(인자만) | — | — |
| `oa_avd.py` (oa_slot) | `KA_SERIAL`, `KE_ADB_SHA256`(pin) | oa_slot | 있음. `OA_TEST_FAKE_ADB`는 slot이 지움 |

- viewer와 Host 시작 경로의 다른 `REMOTE60_NATIVE_*`는 기본값이 있는 조정값이라 필수가 아닙니다(viewer_startup.cpp, host_startup_config.cpp의 `env_u32_clamped` / `env_truthy`).
- fixture가 넣는 `REMOTE60_PINNED_TEST_MACHINE_ID`는 non-test source에서 읽는 곳을 찾지 못했습니다. 무해하지만 기록해 둡니다.

---

# r6 (검증용 실행 oa-s / oa-p 2026-10-30 00:56 INVALID — standby "unsupported")

- **시험 commit 7db9c27** (부모 953aef2). 바뀐 파일은 oa_fixture.ps1과 oa_orchestration_test.ps1이고, 제품 파일 변경은 0입니다.
- **snapshot**: `D:\remote\remote\.claude\oa_lane_r6_SHA256SUMS.txt`(canonical + raw).

## 원인 (source + run 로그로 확정)
- viewer의 `unsupported`는 "StandbyHello 6회에 답 없음"입니다(standby_entry_client.hpp:281).
- standby lease는 capability가 알려 준 **relay 주소**로 보냅니다(standby_lease.hpp:9). 서버는 그 주소로 `REMOTE60_RELAY_IP`를 줍니다(server.js:1310).
- relay는 lease를 client가 **관측한 wire 주소**에 묶습니다.
- fixture는 relay 주소로 `10.0.2.2`를 줬습니다. 이것은 에뮬레이터 안에서만 이 PC를 가리키는 주소입니다.
  - **00:56 run**: PC의 holder viewer가 보낸 hello가 relay에 **하나도 도착하지 않았습니다**(server.stdout에 hello 줄 0). Host log에도 standby 줄이 0입니다.
  - **내 확인 01:15** (relay를 이 PC LAN 주소 192.168.0.76으로 바꾼 fixture-only): hello는 도착했지만 `standby hello rejected src=192.168.0.76:60986 reason=wire-mismatch expectedWire=127.0.0.1`. viewer는 127.0.0.1에서 관측했는데, LAN 주소로 보내면 출발 주소가 LAN이 되기 때문입니다.
- **ke fixture에서는 된 이유**: 그때 standby를 잡은 쪽은 에뮬레이터 안의 폰이었습니다. 폰은 10.0.2.2로 관측하고 보내며, 이 PC에는 둘 다 127.0.0.1로 들어와 wire가 맞습니다.
- **결론**: relay 주소 하나로 폰(10.0.2.2)과 PC viewer(127.0.0.1만 가능)를 동시에 만족시킬 수 없습니다. Host build 차이나 standby admission 설정 문제는 아닙니다. 53fced4 Host는 아래처럼 PC standby를 정상 admit했습니다.

## 보수
- **arm별 relay 주소**:
  - S(폰이 directory와 relay를 씀) → `10.0.2.2`
  - P(폰은 수동 직접 연결로 relay를 쓰지 않음) → `127.0.0.1`
  - readiness 줄에 `relay=`가 붙습니다.
- **arm S + `-WithStandby`는 시작 전에 거부**됩니다(node·Host·holder 미시작, 이유 출력). 따라서 **⑦(PC standby 무간섭)은 arm P로 실행**합니다. 같은 Host에 PC lease를 유지한 채 게시본 0.2.32가 수동 직접 연결로 ②–④, P-5d, P-6r을 진행합니다.
- **Host log 보존**: harness는 정상 끝에서 Host 종료 뒤 `directoryhost_host.log`로 복사합니다(mobile_abr_session_e2e_test.cpp:997–1011, 이번 실패 run에도 있음). 그 사본이 없을 때(harness 강제 종료 등)는 fixture가 readiness의 `host_log=` 경로에서 `host.log.kept`로 복사하고, 결과에 기록합니다.

## 대조
- **stub orchestration 22/22**: P+standby(relay 127.0.0.1) / S(relay 10.0.2.2, holder 없음) / S+standby 거부 / 그 밖의 standby case는 모두 arm P.
- **실제 Host fixture-only 확인** (에뮬레이터 없음, 기기 조작 0, 슬롯 CreateNew `C:\work\gnlink-wt\SLOT_REQUEST.txt` **2026-10-30 01:22:41 → 01:23:08**, sh_run이 자기 파일을 지움, 잔여 0)
  - 명령: `sh_run.ps1 -NativeExe powershell … caplife\oa_fixture_only_check.ps1` → `oa_slot.ps1 -Arm P -WithStandby -AvdCmd <stub_avd 20 s>`
  - lane build: test host `2f81f6bd…`, harness `cbecae52…`, holder `e9ae9415…`, viewer `6806dd41…`, e2e_job_run `248d9829…`, server.js `5a22f372…`(53fced4)
  - **ready 줄**: `ready run=oa-fxonly-20261030012241-21712 hosts=a standby=ready controlPort=18460 relay=127.0.0.1`
  - **Host standby 줄**: `01:22:44.998 [native-video-host][standby] hello refused (capability)`(capability가 Host에 닿기 전 첫 hello) → `01:22:45.505 [native-video-host][standby] hello admitted entries=1 active=0`
  - **holder**: preparing → **ready**(01:22:45.599) → 20 s 내내 ready → Release → `RESULT rc=0 (ok: ready, released, ended by itself, its output kept)`
  - **번들 rc 3 / fixture exit 1**: Host a harness의 "host captured the window" FAIL 때문입니다. 에뮬레이터가 없어 Host에 client가 한 번도 붙지 않았습니다(ke b/c와 같은 종류). 실제 P 실행에서는 폰이 붙습니다. guard의 input/power 판정은 PASS입니다.
  - 같은 방식으로 돌린 **arm S + standby (01:15)**는 위 wire-mismatch로 실패했고, 그 결과가 r6의 S 거부 근거입니다.
- **build**: 실행에 필요한 target은 r5와 같습니다. holder는 r5와 같은 소스입니다(이번 변경 없음).

## r7 (2026-10-30) -- 3 / 6b / 6r pick the pinned window (commit 69ff7e3, on 7db9c27)
- Cause, from the verifier's S/P runs at 7db9c27:
  - pick_desktop tapped the Desktop tab's card, id 0.
  - The PinnedSelectTest host refuses it by design (host_control_session.cpp:1699, `pinned_test_seam_refused`).
  - The app logged `select_failed pinned_test_only`. This is a harness defect.
- What changed:
  - pick_pinned reads the pinned id from the Host log. The capture hwnd must equal the last window-list selectedId, with count 1.
  - It then opens the Windows tab and taps the one card whose title matches.
  - judge_pick passes only when all of these hold: the app's first select_request is that id with tab=WINDOWS, the host logged applied=1 for it and refused nothing, and select_ready names that id.
  - The targets screen is kept as `<OUT>/<n>_targets_ui.txt`, so the next real run leaves the actual 0.2.32 UI dump.
- Selector evidence:
  - Source: b554e76 activity_main.xml (listWindowsButton / listDevicesButton), target_card.xml (targetCardTitle), MainActivity.kt:4513-4545.
  - b554e76→9f9a91c changes no target-screen id.
  - On a real device, 0.2.33 (ke runs, e.g. ab-a-shoff steps.txt:26-27) tapped `listWindowsButton` 'Windows' and the card 'remote60 mobile abr…'.
  - I have not yet seen a real UI dump of 0.2.32 itself, because no device run was allowed in r7.
- Tests:
  - oa_pick_test 7/7: control PASS. NEGATIVE r6 Desktop card id 0, Windows card sent as id 0, id 0 applied with a frame, no pinned card (no card tap), capture/list disagreement (no tap).
  - Regressions: pmclear 12/12, redact 7/7, orchestration 22/22.
- Artifacts: diff `.claude/oa_lane_r7.diff` (2390a421…), snapshot `.claude/oa_lane_r7_SHA256SUMS.txt` (9b6a3d5e…).
## r7b (2026-10-30) -- 선택을 끝까지 묶어 판정, ④는 tap 자신의 event로 (commit 665a47d, 69ff7e3 위; Codex codex_old_apk_lane_pinned_scope_r7.md)
- ③/6b/6r: 이 run의 Host가 list한 단일 nonzero id(capture hwnd == window-list selectedId, count 1)를 쓰고, HWND는 하드코딩하지 않습니다. 다음이 모두 맞아야 PASS입니다.
  - app select_request가 그 id, tab=WINDOWS, gen G
  - Host window-select가 applied=1이고 streamGen S, 거부 0
  - app select_ack가 gen G / streamGen S
  - select_ready가 그 id, gen G
  - 그 뒤 video_debug out > 0(최대 8 s 기다림)
- listed id 0이나 Host 로그 불일치이면 tap 0으로 거부합니다.
- ④: input_check / judge_input. Host [input-trace]에 button down(kind 2) 다음 up(kind 3, 더 큰 seq)이 와야 하고, 둘 다 창 그림 안(Host [keyframe] size)이어야 합니다.
- 문구와 파일명은 "window"로 맞췄습니다(`<n>_window_first_frame.png`). seam 완화와 id 0 redirect는 하지 않았습니다.
- 범위는 S ③④⑤⑥, P ③④·P5d·P6r(⑤⑥ NOT RUN), ⑦은 P입니다(코드 경로는 그대로).
- oa_pick_test 15/15. 회귀: pmclear 12/12, redact 7/7, orchestration 22/22.
- 미검증(기기 필요):
  - 0.2.32 실제 targets 화면 dump(다음 run에서 `<n>_targets_ui.txt`로 남음)
  - 0.2.32가 tap 하나에 실제로 down과 up을 창 그림 안에서 보내는지
- 결과물: diff `.claude/oa_lane_r7b.diff`(7db9c27..665a47d, 8c3d94ec…), snapshot `.claude/oa_lane_r7b_SHA256SUMS.txt`(9fb1ed62…).
## r7c (2026-10-30) -- ④는 tap 자신의 down→up, 같은 위치, 창 안 (commit 548d199, 665a47d 위)
- 지적 원인(검증용, 69ff7e3 기준): ④가 tap 뒤 3 s 안에 새 [input-trace] 줄이 하나라도 있으면 PASS였습니다. tap과의 연관이 없었습니다.
- 판정:
  - tap 직전에 Host 로그 offset을 잡고, 그 뒤 줄만 봅니다.
  - 첫 down(kind 2) 다음에 up(kind 3, 더 큰 seq)이 와야 합니다. 위치는 같아야 하고(±2 px), 둘 다 창 그림 안(Host [keyframe] size)이어야 합니다.
  - kind 값은 양쪽 source로 확인했습니다: Host `host_control_session.cpp:1296`, 앱 b554e76 `MainActivity.kt:72-75`.
- 한계:
  - 0.2.32 DIRECT tap은 화면 좌표를 그림 좌표로 바꿉니다(`MainActivity.kt:3057-3125`, mapTouchToVideoCoords). 이 변환은 기기의 view 배치에 달려 있습니다.
  - 그래서 화면 중앙 tap의 기대 위치는 aim/off로 **기록만 하고 판정하지 않습니다**.
- Windows 탭: listWindowsButton이 visible·enabled가 아니면 FAIL이고 tap하지 않습니다. ③ 판정 줄에 `windowsTab shown=… enabled=1`로 남깁니다.
- 테스트: oa_pick_test 19/19.
  - ④ 부정 대조: none, uponly, outside, pretap(tap 전 pair가 실제로 있는지도 확인), moveonly, downonly, moved.
  - 회귀: pmclear 12/12, redact 7/7, orchestration 22/22.
- 결과물:
  - 전체 diff `.claude/oa_lane_r7c.diff` (7db9c27..548d199, 39ebd6a2…)
  - 증분 diff `.claude/oa_lane_r7c_delta.diff` (665a47d..548d199, 888b1be5…)
  - SUMS `.claude/oa_lane_r7c_SHA256SUMS.txt` (b42777a7…)