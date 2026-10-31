

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

## stub 대조 (기기 0)
- `oa_orchestration_test.ps1` 17/17 PASS rc 0 (r2 재실행). `oa_adb_redact_test.py` 7/7 PASS. `oa_avd.py` ast OK.
- `oa_avd.py`의 기기 쪽 단계(P 전제 판정, P-5d·P-6r, Host 로그 TCP client 계수)는 **기기 없이 실행되지 않았습니다**.

## 권장 보고 문구 (Codex)
"0.2.32 원 Source의 fixture endpoint build로 새 서버의 directory/relay 경로를 확인하고, 게시 APK 원본은 새 Host와 수동 직접 연결로 확인했다. 게시 APK 원본의 고정 HTTPS 주소를 통한 새 서버 로그인·relay는 배포 전 직접 검증하지 않았다."

두 결과를 합쳐 "게시 binary의 새 서버 end-to-end 직접 검증 완료"라고 쓰지 않습니다.
