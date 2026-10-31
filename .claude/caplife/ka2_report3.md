

### ② 진행 3 — e 준비 (2026-10-27): APK 브랜치, runner, 비동기 capability, JNI
**브랜치 결정 ①(검증용)**: `feat/apk-0.2.33`(C:\work\gnlink-wt\feat-apk-0233) = b554e76 + merge 75e37d1 + merge 6d85ce2. main 보다 docs 1 commit(2137f3a) 뒤.
- **feat/apk-0.2.33 1c2116d**: merge 90115a2(history 충돌만) · 8731255(5 파일 해소) · d62df9a · 9f019e5 · 1338930 · c7a7f2e · 94aeee8 · 1c2116d.
  - ⚠️ 8731255 에서 6d85ce2 가 추가한 `android_decoder_binding_test.cpp` 가 빠졌다(내가 test 파일을 unstage). d62df9a 에서 그 내용 + [2b] 로 넣었다.
  - 해소 내용:
    - decoder 는 binding token(*As) 형태를 유지하고, apk-0232 로컬 선택 4함수도 owner 검사(*As)로 만들었다. entry_sink_adapter 가 token 으로 전달한다.
    - ua2 JNI 2개는 binding 인자를 받는다.
    - MainActivity: Apply 는 backend 를 재전송하지 않는다. commit·park 재사용 때 backend 선택을 reset 한다. 설정 poll 은 bound entry 에 질의한다. desktopBackendConnected 는 active entry 기준이다.
  - merge 커밋은 git CLI(두 parent), 나머지는 Git MCP 다.
- **병합 APK 회귀(배선 전)**: noseam 9dbd492d…, shseam 0853297e…
  - `method_ui` 16, `ua2` 13, `ua2_desktop` 9, `prelogin_ui` 15, `layout_b` 47, `touch_ab` 36 — 모두 PASS.
  - 단위 290/0, native(binding 17, prelogin select 60, role park 13, pending slot 10, registry/table/park/gate PASS).
- **ka-r3 (fc1fd46 → 0a5e71d)**:

| 커밋 | 내용 | 시험 |
|---|---|---|
| b851956 | Release: parked 면 StandbyRelease 1회 → Idle. active·activating 면 보내지 않는다(Live) | 51/51, 변이 2 FAIL |
| 31feb28·26b71f7 | `StandbyRunner`: PC 마다 lease + pump 스레드, StatusOf 스냅숏, TakeActivation/SessionOver, Release. GenerationAllocator 잠금 | e2e runner 단계 |
| ddc07a4 | `standby_directory.hpp`: lease 소켓으로 observe + standby connect, relay(reserved)/Host 조준. e2e relay 가 이 제품 helper 를 쓴다 | — |
| fc1fd46 | 비동기 capability: lease 가 needsCapability → 앱이 그 lease 소켓으로 Observe, Kotlin 이 connect → Provide | e2e runner 단계가 APK 방식으로 실행 |
| 0a5e71d | `FileGenerationStore`(20 B, tmp → 교체; Windows 는 MoveFileEx) | 파일 시험 56/56(REMOTE60_TEST_TMP 안에서만, 자기 파일 정리), Windows std::rename 변이 4 FAIL |

  - e2e `--product-client --real-directory --old-host` **63/63**(runner 포함). runner 단계:
    - Release 는 Host 가 세션 끝을 본 뒤에 보낸다.
    - 끝을 보기 전의 Release 는 Live 로 거부되고, entry 는 lease 로 소멸한다(계약 §2.3). 첫 실행에서 실제로 이 동작이 보였다.
  - product-client 실행은 REMOTE60_E2E_HOST_SECONDS=420 이다(90 s Host 가 실행 중 끝나 실패한 적 있음).
- **feat 1c2116d — standby JNI**:
  - nativeStandbyInit(filesDir), Start, Status[exists, state, lost, handedOff, needsCapability], Observe(lease 소켓), Provide, Release, ReleaseAll.
  - 호출처 0(`SEND_TO_REAL_HOSTS=false`).
- **결정 대기**: kept 항목 모델 A/B(seq 14381). considerPreconnect·탭 activation 배선은 그 뒤다.
