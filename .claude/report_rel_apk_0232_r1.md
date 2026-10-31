# report rel-apk-0232 r1 — APK 0.2.32 통합 후보 (작업용1 → 검증용 remote#yj8mrmrx, 2026-10-26)

**고정 후보: `fix/apk-prelogin-select` ac51e97** (main 5970f04 위, tracked clean). 서명된 manifest·게시·push 는 없다(검증용 몫).
- APK: `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk`
  - 13,885,960 B, sha256 `d5506f679a5f950d056dbb4afe89acaede49c129bb9f268c55331d9f9e029f75`.
  - seam 없는 `clean assembleDebug` 다. 0.2.31 과 같은 debug 키다.
  - 빌드 소스: ac51e97 의 부모 + 버전 변경. ac51e97 과의 차이는 `docs/history.md` 만이다.

## 담은 것 (각 보고 참조)
| | 내용 | 보고 |
|---|---|---|
| ① | 공용 세션: 로그인 전 화면 Host 의 바탕화면 선택 = 로컬 선택(같은 tag, 디코더에 들어간 첫 IDR 로 결합, 결합 뒤에만 selected). 첫 pong 전 선택은 같은 op 를 전환하고, 창 선택은 거부 | `report_apk_prelogin_select_r1.md` |
| ② | 선명도 강하게 0.35 → **0.60**. 표시 크기 1패스(B)는 시험 빌드 전용 실험(출하 = A) | `report_sh_strength_r2.md` |
| ③ | 캡처 방식: 이 PC의 이번 연결에만 적용, Host 실제 적용 표시, 저장·재전송 중단, **GDI 버튼 배선 수정**(08-03 이후 미작동) | `report_ua2_apk_r1.md` |
| 버전 | versionName 0.2.32 / versionCode 31 (ac51e97) | — |

## 후보 검사 (이 APK 파일 그대로)
- `aapt dump badging`:
  - package `com.remote60.androiddirect`, versionCode 31, versionName 0.2.32, sdk 28 / target 34.
  - native-code: arm64-v8a, armeabi-v7a, x86, x86_64 (4 ABI).
- `apksigner verify --print-certs`: V2 인증서 SHA-256 `dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990` — 0.2.31 게시본과 같다.
- `gnlink_check_fixed_server.py --apk`: ALL PASS 7. dex·.so 에 seam 표식 0(`shTestDisplayPass` 추가분 포함). 게이트 self-test 52.
- 단위 `testDebugUnitTest` 257/0.
- **AVD smoke**(같은 APK, `sh_filter_avd.py --scenario ua2`, 시험 Host, 화면 밖 패턴 창): **13/0**. 수동 연결 → 창 선택 → 표시 → 설정 → 캡처 방식 탭 → 해제·재연결.
- Windows 쪽 공유 native 회귀(①③ 의 세션 변경 포함):
  - `client_session_prelogin_select_test` 38/38, `role_park` 13/13, `shared_core` PASS.
  - 실 로그인 전 Host `--android-select` 37/37. 그 밖의 회귀 e2e 7 PASS(① 보고).

## 검증용이 할 일 (제안)
- 독립 AVD:
  - ① 실 APK UI 로 로그인 전 Host 에서 바탕화면 → 화면 표시. 시험 Host 의 `--prelogin` 과 디렉터리 인증이 필요하다 — **이번에 하지 않았다**.
  - ② `compare_ab`/`touch_ab` (seam APK 필요: `-Pgnlink.shTestSeam=true` 빌드).
  - ③ `ua2`.
- 서명된 android.manifest → `gnlink_deploy.sh --platform android`.

## 사용자 실기 안내 (게시 뒤 묶을 것)
- **선명도**: 같은 원격 화면·같은 확대율에서 끄기 / 약하게 0.20 / 강하게 0.60 을 바꿔 보며 글씨 두께·번짐·스크롤 때 반짝임·발열을 본다.
  - AVD 측정상 이번 출하 경로(A)의 강도 간 차이는 작다(글씨 edge 9.95→10.94 @×1.33).
  - "OSLink 처럼 두꺼운 선" 은 B(표시 크기 1패스)에서 크게 나타났다. B 는 이번 APK 에 켜져 있지 않다 — Codex 판정 대기.
- **로그인 전 화면**: 재부팅 직후 PC 를 골라 바탕화면을 누르면 화면이 뜨는지 본다. 이전에는 6초 뒤 목록으로 돌아갔다.
- **캡처 방식**:
  - 설정에서 바꾼 방식은 그 연결에만 적용된다. 다시 연결하면 PC 기본(DXGI)으로 돌아간다.
  - 줄에 "실제 적용" 이 따로 보인다.
  - GDI 를 고르면 이제 실제로 요청이 간다.

## 미검증·한계
- 실 폰(Adreno/Mali) 화질·지연·발열.
- ① 실 APK UI 의 로그인 전 화면 흐름(AVD·실기).
- ② B 의 회전·resize·pan·fallback.
- ③ 바탕화면 대상의 실제 backend 전환 화면.
- KA(R2)는 이 APK 에 없다(자동 preconnect OFF 그대로).


## r2 — 최종 통합 후보 (2026-10-27) — 이 절이 위 r1 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` 05fe4b9** (tracked clean). 버전은 그대로 0.2.32 / versionCode 31(미게시). 서명 manifest·게시·push 없음.
- 담은 것(r1 이후): ① picker r2 86df4cd·1de9dc7 · ③ UA-2 r2 cf8c021·41de4cf · ② SH r3 2f6905d·05fe4b9.
- APK: `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk` (덮어씀; r1 의 d5506f67… 는 더 이상 후보 아님)
  - **13,927,836 B, sha256 `b43a94ab22fa9346e4e36d3043a03a1818620b9bb74307b1663de0595a16374f`**
- 빌드: noseam `clean assembleDebug testDebugUnitTest` rc0, 단위 **263/0** (로그 `caplife\apk\gradle_0232r2.txt`).
- aapt: `com.remote60.androiddirect` versionCode 31 / versionName 0.2.32, native-code arm64-v8a·armeabi-v7a·x86·x86_64.
- apksigner V2 인증서 SHA-256 `dcc806ae…2990` (기존 서명자와 같음).
- 게이트 `gnlink_check_fixed_server.py --apk`: 이 APK **ALL PASS (7)**; 대조 shseam APK **FAIL (2)** — 부정 대조가 실제로 실패한다.
- AVD smoke: 이 APK 그대로(해시 b43a94ab…) `method_ui` **16/16** (Host ecb2ff2e…, 콘솔 세션). B: edge 10.97 → 16.31, 기본 복귀 10.97.
- 구성요소별 결과는 각 보고: `report_apk_prelogin_select_r1.md`(r2 절), `report_ua2_apk_r1.md`(r2 절), `report_sh_strength_r2.md`(r3 절).
- 미검증: 실기 화질·지연·발열, 실기 로그인 전 화면 흐름, KA 는 이 APK 에 없음(자동 preconnect OFF).


## r3 — 통합 후보 갱신 (picker N1 마지막 경계, 2026-10-27) — 이 절이 r2 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` 3a17444** (05fe4b9 위 9afcec7·3a17444, tracked clean). 버전 0.2.32 / 31 그대로(미게시).
- APK: `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk` (덮어씀; r2 의 b43a94ab… 는 더 이상 후보 아님)
  - **13,973,100 B, sha256 `ece254f00393400d5d82bd54d2d30ce084552e02c378df766963491933db8e4c`**
- noseam `clean assembleDebug testDebugUnitTest` rc0, 단위 263/0 (`caplife\apk\gradle_0232r3.txt`).
- aapt versionCode 31 / 0.2.32, 4 ABI; apksigner V2 `dcc806ae…2990`; 게이트 ALL PASS(7).
- AVD smoke(이 APK 그대로): `method_ui` 16/16, 슬롯 삭제 확인, 잔여 프로세스 없음.


## r4 — 최종 통합 후보 (picker r3 + UA-2 r3 + SH r4, 2026-10-27) — 이 절이 r3 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` 7795289**
- APK 를 빌드한 커밋은 0c48c6f 이다. 그 뒤의 7795289 는 데스크톱 e2e 시험 파일 하나만 바꿔 APK 에 들어가지 않는다. tracked clean. 버전은 0.2.32 / 31 그대로(미게시).
- 포함 커밋:
  - picker: 9afcec7 · 3a17444 · 7795289
  - UA-2: 482a901 · 3e4d567
  - SH: 9886c22 · 0c48c6f
- APK: `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk` (덮어씀; r3 의 ece254f0… 는 더 이상 후보가 아니다)
  - **13,973,916 B, sha256 `8782f38701e5b6ba154a068a7d15949574a8c9a56166e248871caa4807f1769f`**
- 빌드·패키지:
  - noseam `clean assembleDebug testDebugUnitTest` rc0, 단위 **266/0**.
  - aapt versionCode 31 / 0.2.32, 4 ABI. apksigner V2 `dcc806ae…2990`.
  - 게이트: 후보 ALL PASS(7), shseam 대조 FAIL(2).
- 후보 그대로: `method_ui` 16/16, `ua2` 13/13.
- 같은 HEAD 의 shseam(1f63839e…, product-equivalent test build; 차이는 shseam 소스셋만): `strength_ab` 34, `compare_ab` 26, `touch_ab` 36, `latency_ab` 6, `layout_b` 47 — 모두 ALL PASS.
- 실 Host: `viewer_prelogin_login_e2e_test --android-select` 37/37.
- 미검증:
  - 실기 화질·지연·발열.
  - 스크롤 shimmer 판정(oracle 없음).
  - 바탕화면 대상 backend IDLE 줄의 AVD 실행.
  - shseam 빌드는 noseam 과 해시가 다르다(허용된 차이). 시험 빌드는 권한 항목을 바꾸지 않는다.


## r5 — 최종 통합 후보 (SH r5 + evidence A, 2026-10-27) — 이 절이 r4 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` ed7f4ad** (tracked clean). 0.2.32 / 31 그대로(미게시).
- APK `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk` — **13,990,300 B, sha256 `1d4a05f4f318496a895ac748be5705365598c3de677c6f4652988034bd3deebd`**. 8782f387… 는 대체됐다.
- 빌드: noseam clean, 단위 275/0. 4 ABI, `dcc806ae…`, 게이트 7 PASS. shseam 9f1a0529… 게이트 FAIL(2).
- 후보 그대로:

| 시나리오 | 결과 |
|---|---|
| `method_ui` | 16 |
| `ua2` | 13 |
| `ua2_desktop` | 9 |
| `prelogin_ui` (A) | 15 |
| `prelogin_tripwire` | 3 |
| `prelogin_ui_control` | 일반 Host 부정 대조 — no-window-list 검사 FAIL(lines=12), **기대대로** |

- shseam: `strength_ab` 35 · `compare_ab` 26 · `touch_ab` 36 · `latency_ab` 6 · `layout_b` 47.
- 실 Host `--android-select` 37/37(`sh_run.ps1 -NativeExe` 로 실행), picker 단위 60/60.
- 시험 Host `GNLinkStreamMigrationTest.exe` 881b3e05… (새 값). 출하 `GNLinkStream.exe` 1c0bc4f0… 에는 seam 표식이 0 이고, 시험 Host 에는 양성이다.
- 독립 실행 진입점: `.claude\caplife\apk0232_verify.md` r2.


## r6 — 최종 통합 후보 (SH r6, 2026-10-27) — 이 절이 r5 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` b554e76** (d29a034 제품 SOURCE OK). tracked clean. 0.2.32 / 31(미게시).
- APK `D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk` — **13,990,300 B, sha256 `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab`**.
  - 이전 1d4a05f4 는 `superseded\GNLink-0.2.32-1d4a05f4.apk` 로 보존했다.
- 빌드: noseam clean, 단위 277/0. 4 ABI, `dcc806ae…`, 게이트 7 PASS. shseam b105a845… 는 게이트 FAIL(2).
- 후보 그대로:

| 시나리오 | 결과 |
|---|---|
| `method_ui` | 16 |
| `ua2` | 13 |
| `ua2_desktop` | 9 |
| `prelogin_ui` | 15 |
| `prelogin_tripwire` | 3 |
| `prelogin_ui_control` | 기대대로 FAIL(window-list 13) |

- shseam: `strength_ab` 35 · `layout_b` 47 · `touch_ab` 36 · `latency_ab` 6. `compare_ab` 는 r6 와 무관해 이번에 미실행(1d4a05f4 에서 26).
- 실 Host `--android-select` 37/37. 시험 Host 881b3e05… 는 변경 없음.
- 진입 문서 `caplife\apk0232_verify.md` r3.
