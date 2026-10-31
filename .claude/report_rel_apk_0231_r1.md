# report rel-apk-0231 r1 — APK 0.2.31 (선명도 필터) 릴리스 후보 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-en2shyuh / rev r1 / kind=result. 지시 `task_rel_apk_0231_r1.md`.
**판정: 후보 준비 완료.**
- 빌드·gate·버전·서명 인증서·ABI·AVD smoke 를 확인했다.
- 서명된 manifest·게시·push·폰 설치는 하지 않았다.
- one-screen smoke 의 FAIL 1건은 하네스 기대값 오류다(아래, 소급 PASS 로 바꾸지 않음).

## 브랜치 · commit
- worktree `C:/work/gnlink-wt/rel-apk-0231`, 브랜치 **`rel/apk-0.2.31`**.
- 기준 **rel/0.2.162 = 6a127eb**.
  - 6a127eb 는 게시된 0.2.162 릴리스 commit b4dfaed 위의 docs 전용 1커밋이다(history·작업목록). main 에도 포함돼 있다.
  - 그래서 제품 소스는 게시된 PC 0.2.162 와 같다.
- **4394d2c** = merge feat/sh-filter c494dc6. git CLI `--no-ff`, parents 6a127eb + c494dc6.
  - 충돌 `automation/gnlink_check_fixed_server.py`(self_test): 양쪽 보존.
    - LK 의 서비스 표식 검사(ascii/utf-16).
    - SH 의 APK dex/native seam 검사.
    - self-test 30/30.
  - 충돌 `docs/history.md`: 양쪽 보존.
  - 제품 충돌은 없었다.
- **2b57bb8** = APK 버전 commit(Git MCP). `apps/android_direct_client/app/build.gradle.kts` 를 versionCode 29→**30**, versionName 0.2.30→**0.2.31** 로 바꿨고(b4fd204 와 같은 방식), history 에 후보 항목을 넣었다. PC 버전 파일은 건드리지 않았다.
- `third_party/webview2` 는 robocopy 로 실제 복사했다(junction 아님).

## APK
- 빌드: `gradlew clean testDebugUnitTest assembleDebug`(seam 속성 없음, 지난 게시와 같은 debug 파이프라인). rc 0, 단위 시험 249/0.
- 산출물 `C:/work/gnlink-wt/rel-apk-0231-out/GNLink-0.2.31.apk`:
  - 크기 **13,799,096 B**
  - SHA-256 **`230a65e99a2a7ee30ce0af978e51eb0fa189f457e9a07dfddb4dc69484a9d5d4`**
- apksigner 37.0.0 결과:
  - v2 서명, `CN=Android Debug`.
  - 인증서 SHA-256 **`dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990`** — 지난 게시(0.2.23~0.2.30) 값과 같다.
- aapt badging:
  - `versionCode='30' versionName='0.2.31'`, package `com.remote60.androiddirect`.
  - native-code `arm64-v8a armeabi-v7a x86 x86_64`.
  - `lib/<4 ABI>/libremote60_android_direct.so` 가 모두 들어 있다.
- gate `gnlink_check_fixed_server.py --apk`: rc 0, **ALL PASS 7**.
  - dex 와 native 라이브러리 모두에서 선명도 seam 표식 0.
  - 저장된 sign-in 과 두 경로 포함, 백업 제외.

## AVD smoke (공유 native 영향 + SH 핵심)
- 공통 조건:
  - 이 PC 콘솔, AVD(SwiftShader). SLOT_REQUEST CreateNew → 삭제 완료.
  - 시험 Host 는 `GNLinkStreamMigrationTest` `b4089dbe…`(integ-0162 빌드, 9f54912 계열 = 0.2.162 와 같은 제품 소스의 시험 빌드). loopback, 수동 IP.
- 하네스: `automation/sh_filter_avd_relsmoke.py`(rel 워크트리, **미추적·릴리스 준비 전용**). `sh_filter_avd.py` 의 fixture/UI 도우미를 그대로 쓴다.
- 한 실행의 흐름:
  1. 접속하고 첫 대상을 고른다.
  2. 그림이 화면에 있고 테두리가 맞는지 본다.
  3. 2 s 간격 두 캡처로 새 프레임이 오는지 본다.
  4. 0.20 필터가 그림 위에 있는지 본다(`path=sharpen`).
  5. 뷰어 메뉴 → 연결 해제 → 같은 앱 프로세스에서 다시 접속하고 고른다(재연결).
  6. 2~4 를 다시 본다.
  7. 충돌·fallback 이 없는지 본다.

| 실행 | 결과 | 내용 |
|---|---|---|
| **smoke_monitors** (Host `MONITORS_FILE=extra`) | rc 0, **13/0** | 대상 목록 `['Desktop'(탭), '모니터 1  1920x1080 • 주 화면', '모니터 2  1920x1080']` — 화면 목록이 앱에 도달했다. 모니터 1 선택 뒤 영상 live, `path=sharpen` gen=1. 연결 해제 → 재접속 뒤 live, `path=sharpen` gen=2. 충돌·fallback 0. |
| smoke_one (화면 1개 Host) | rc 1, 12/1 | 접속·영상 live·0.20·연결 해제·재접속·live·0.20·충돌 0 은 모두 PASS. |
| **compare** (`sh_filter_avd.py --scenario compare`) | rc 0, **25/0** | SH 핵심. |

- smoke_one 의 **FAIL 1건은 하네스 기대값 오류**다.
  - 기대: `labels == ['Desktop']`.
  - 실제: `['Desktop', 'Desktop']`. dump 가 대상 **탭** 이름 'Desktop' 과 카드 'Desktop' 을 함께 집었다.
  - smoke_monitors 의 목록도 첫 항목이 같은 탭이다. 그러므로 화면 1개 Host 의 실제 목록은 Desktop 카드 1개다. 이것은 제품 규칙(`MainActivity.kt:4447-4459`, 화면이 1개면 Desktop 하나)과 맞다.
  - 기록은 rc 1 그대로 둔다. 다시 돌려 PASS 로 바꾸지 않았다.
- 참고: live 판정의 meanDiff 가 네 번 모두 12.96 이었다. 패턴이 시간 결정적으로 움직여서 2 s 간격의 차이가 같게 나온 것으로 본다(그림이 멈췄다면 0 근처). 확인은 하지 않았다.
- hello 시간: Android native 는 hello 소요 시간을 로그에 남기지 않는다(`native_video_client_session.cpp:1524-1531`). 그래서 기록하지 않았다.

## 남은 위험 · 미검증
- **폰 실기 전부 미검증**: 실 GPU 화질·지연·발열, 회전 뒤 터치, Codec2 crop, 실 LAN/relay 재연결.
- AVD 에서 확인한 것은 공유 native(접속·hello·화면 목록·재연결)의 동작 여부까지다.
- 휴대폰 잠금 화면 비밀번호(sealed APK)는 미구현이다(알려진 한계 유지).
- APK 0.2.30 이하는 PC 0.2.162 잠금 화면에서 휴대폰으로 비밀번호를 칠 수 없다. LK 기록 그대로이고 0.2.31 도 같다.
- keepalive-multi 는 넣지 않았다(범위 결정).
- 수동 IP 접속 뒤 약 10 s control 정지는 하네스에서 11.5 s 대기로 우회한 그대로다(ledger 항목, 이 APK 에서 해결하지 않음).
- 게시 단계(검증용/사용자 지시 시):
  - android manifest 직접 작성(schema 2, versionCode 30, url `…/updates/0.2.31/GNLink-0.2.31.apk`).
  - `gnlink_release_sign.ps1 -Platform android`, `gnlink_deploy.sh --platform android`.
- 사용자 행동: 없음(게시 뒤 폰 실기 1회는 검증용이 안내).
