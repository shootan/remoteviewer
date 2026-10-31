# apk-decoder-inbox r1 — A/B 측정 계획 (실행은 고정 계측·plan SOURCE 뒤, 검증용 슬롯 조율)

작성: 2026-10-30. 근거: Codex codex_apk_decoder_inbox_decision_r1.md ④⑤.

## 무엇을 비교하나
| arm | APK | sha256 | source | 조건 |
|---|---|---|---|---|
| **A** (baseline) | `C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke-off.apk` | `8c6422fe7104f8272242a9c51a64ac3d7a98989886a855f1d89cbee55f23828d` | 9f9a91c (inbox 없음, RX가 codec에 직접 묶임) | `assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410`, trace 없음 |
| **B** (현재) | `C:\work\gnlink-wt\apk-decoder-liveness\apk_out\plain-debug-r5.apk` | `32bb788f3972cb7b3603d6f0d6907efcd503853dcbfc5b55fadcd427e27579ec` | ac587dd | 같은 property, trace 없음 |

- **두 arm 모두 앱 쪽 trace 없음 (제안)**: 9f9a91c에는 liveness trace가 없습니다(ec016f0에서 session과 control channel에 걸쳐 들어옴). 이것을 A에 넣으면 baseline에 제품 diff를 섞게 됩니다(Codex 경고). 그래서 두 arm을 **같은 최소 조건, 즉 앱 trace 0**으로 맞추고, 아래의 대칭 근거로 측정합니다.
  - A를 다시 빌드하지 않습니다(bitwise `8c6422fe` 유지). B에 계측 build를 대신 넣지 않습니다.
  - 앱 쪽 queue age·lock·API 지연은 A에 없습니다. 이 값들은 C 단계(prototype)의 trace build 진단으로 돌립니다.
- **ApkOn**: e2 계열 계획에서는 쓰지 않지만 ke_slot이 해시를 기록합니다. A는 `ke-on.apk` `64a02118…`(9f9a91c), B는 `ke-on-lt-r5.apk` `8829254…`입니다.
- **같게 두는 것**: Host `GNLinkStreamPinnedSelectTest.exe` `d806e375…`, harness `722bf981…`, server.js `0b656c06…`(KE_SERVER_JS = apk-decoder-liveness), relay trace on(KE_RELAY_TRACE=1, local fixture), 시험 AVD emulator-5554(같은 boot 권장), SDK adb pin `7035cf5e…`, Settings SH-off, 같은 target(`remote60 mobile abr` 창), 기본 해상도·fps·bitrate(아무것도 바꾸지 않음).
- **fixture**: 둘 다 **새 plan-aware fixture r10**을 씁니다. E2-only 계획은 Host a만 시작합니다. 그래서 10-29 ke-r26의 환경(3 Host)과 같다고 주장하지 않습니다. 10-29 raw bundle은 rc 3으로 보존합니다.

## 실행 순서 (검증용; 기존 슬롯 1개, adb 정상 범위, 다른 앱 종료 없음, 자체 build·다른 시험 겹침 없음)
1. 30 s adb 안정 관찰(ke-r26 wrapper와 같음).
2. **A**: `KE_PLAN=e2-shoff`, `KE_WATCH_SEC=120`, `KE_RELAY_TRACE=1`, `KE_SERVER_JS=…apk-decoder-liveness\apps\directory\server.js`
   `ke_slot.ps1 -Tag ab-a-shoff -ApkOff <A ke-off.apk> -ApkOn <A ke-on.apk> -AdbSha256 7035cf5e…`
3. **B**: 같고 `-Tag ab-b-shoff -ApkOff plain-debug-r5.apk -ApkOn ke-on-lt-r5.apk`만 다릅니다.
4. 실행 순서나 큰 외부 부하 변화가 해석을 바꾸면 그때만 **역순 B/A를 1회** 추가합니다. 즉시 반복은 하지 않습니다. 조기 종료가 나면 실제 관찰 길이를 적고, 120 s로 환산하지 않습니다.

## 구간과 측정 (r10 하네스가 남기는 것 + 대칭 원천)
- **구간**: `SEGMENT startup begin`(앱 시작 직전) → `startup end`(E2 첫 영상 직후) → `watch begin/end`(첫 영상 뒤 120 s) → `list begin/end`(PC 목록). 각 경계에 run 시각과 epoch ms를 남겨 Host·relay·app clock을 같은 순간에서 자릅니다.
- **부하 샘플**(두 arm 같게; 시작 전 1회, watch 중 10 s마다, watch 끝 1회): app PSS(dumpsys meminfo), app CPU%(top), 에뮬레이터 loadavg, 이 PC qemu 프로세스 누적 CPU ms. 실행 1회마다 nproc·MemTotal·wm size·GLES 문자열을 한 번 기록합니다.
  - 시작 전 유휴 부하와 제품 실행 중 부하를 나눠 적습니다. **제품이 올린 부하는 환경 탓으로 빼지 않습니다.**
- **Host 로그**(두 arm 같음):
  - 구간별 `keyframe-request seq=`의 unique seq 수
  - `[keyframe] seq bytes`(IDR 수와 바이트)
  - `wire seq key bytes`(전송 frame 크기 표본)
  - `[client-present] fps gap`(client가 보고한 표시 fps와 gap)
  - `encodedFrames / sentFrames`
  - `udp control session ended` / peer-lost
- **relay trace**: 방향별 bytes/s·pps(구간 경계로 잘라), socket send callback 오류.
- **app 로그**(두 arm에 다 있는 것만): `video_debug`의 in/out·stale·(B만) inbox 계수, 첫 영상 시간, `keep_end`·scene, control close.
- **비교 산출**: 구간별 unique 요청 수, IDR 수, IDR 바이트 비중, video bytes/s, 표시 fps와 gap p95, 첫 영상, control 종료 0/1, PSS 최대, CPU 평균.
  - `kq` 하나로 대역 증가량을 계산하지 않습니다.

## 하네스 r10 (feat-apk-0233\.claude\e2e, snapshot avd_guard_r10_snapshot)
- **ke_slot.ps1**: e2-sh / e2-shoff 계획이면 `-Hosts a`를 넘깁니다. readiness는 `hosts=<그 목록>`과 정확히 맞아야 합니다.
- **ke_fixture.ps1**: `-Hosts` 목록의 Host만 시작합니다(guard·final OFF·end/EOF·job은 그대로).
- **ke_avd.py**:
  - 계획에 맞는 PC 목록(E2-only면 ke-pc-a)을 씁니다.
  - `KE_WATCH_SEC`(기본 60, A/B는 120)와 구간 표지, 샘플링을 추가했습니다.
  - E1 판정 문구는 "the plan's PCs listed"로 바꿨습니다.
- **ke_orchestration_test.ps1**: r10 case 3개를 추가했습니다(E2-only는 a만 시작하고 readiness가 a / E2-only에서 a가 실패하면 AVD 0과 나머지 미시작 / full은 3 Host).

## 열려 있는 결정 (Codex)
1. 두 arm을 trace 없이 대칭으로 두는 안을 받을지(위). 아니면 9f9a91c에 trace-only diff를 넣을지.
2. 설계 D2의 "가장 이른 usable IDR" 선택과 D6 retry 숫자(1/2/4 s, 3회).
3. 설계 D8: session의 held 요청이 episode 종료를 알게 하는 sink 조회 hook.
