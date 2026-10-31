# APK decoder/control liveness — r1 결과에 대한 D1–D3 결정

2026-10-29. 앞 설계 r1/r2를 유지하며 이번 결정을 같은 작업용1 지시서에 합친다. 제품 SOURCE 승인이나 출하 승인이 아니다.

읽은 고정 자료:

- `report_decoder_liveness_r1.md`: `dc8c1e048c22a1ae3bd293e26d36ca926502971809801c62b27c89dc15fc68c6`
- `report_e2_relay_investigation.md`: `94f54e24812bdac2f6cec162204e03b4da1e683fecb4afc5569a178e85e0f7ca`
- feat-apk-0233 `9f9a91c852419a2d2848be73aab077d779b0550c`의 Kotlin surface 소유/해제 경로와 기존 decoder 구조.

## 결과의 수용 범위

ke-r24는 환경 단계 INVALID, 기기 조작 0으로 보존한다. ke-r25는 실제 Settings SH OFF·direct 경로의 첫 영상 498ms 성공 근거다. Settings 경유로 선택 전 약 40초가 추가됐고 부하/시각도 달라졌으므로 SH 한 요소만 바꾼 엄밀한 통제 실험이나 decoder 보수의 EVIDENCE로 부르지 않는다. 5/5는 중단 전 assertion 집계이며 E2 목록 복귀/전체 계획 성공이 아니다.

r21 B의 decode 진행 중 예상 밖 종료와 r25의 decode 진행 중 client peer_lost를 별도 증상으로 수용한다. 두 run의 종료를 ke-r23의 codec 미준비 사슬로 설명하지 않는다. 반대로 in/out 증가만으로 모든 순간의 decoder lock 지연이나 RX/control 처리 중단까지 배제하지 않는다. Host clientReportAge, 앱 종료 사유, 영상 누적 계수만으로 c2h wire 유실/ACK 유실/relay 결함 중 하나를 확정하지 않는다. report 증가와 reliable ACK는 다른 관측이다.

## D1 — 무기한 UI 대기 (a) 대신 bounded detach (b)

**(b)를 채택한다.** (a)가 지키려는 “옛 producer를 끊기 전 새 producer를 붙이지 않음”은 필수다. 그러나 worker가 느린 create/stop 안에 있으면 UI가 그 뒤의 detach를 무기한 기다릴 수 있다. RX/control만 풀고 UI에 같은 정지를 옮기는 것을 최종 구현으로 승인하지 않는다. 이미 별도 branch에서 진행한 worker 분리는 그대로 활용하고, 고정 후보 전에 이 계약을 반영한다.

- detach 요청 시 짧은 state lock 아래 현재 surface/selection 세대를 무효화하고 worker에 보낸다. RX/control 조회와 hook은 완료를 기다리지 않는다. UI query/non-null surface 요청에도 동기 codec 호출을 되살리지 않는다.
- UI에 동기 호환 대기가 필요하면 **한 handoff 전체에 최대 2초**의 명시 deadline을 둔다(기존 SH release 예산에 맞춘 상한이며 정상 지연 목표가 아니다). 대기 중 stateMu_·session/book lock을 잡지 않는다. 여러 단계에서 각각 2초씩 누적하지 않는다. UI handler의 토큰별 완료 continuation으로 같은 계약을 지키는 구현도 허용한다.
- timeout/worker 실패는 성공이 아니다. JNI가 상태 또는 요청 토큰/완료를 전달하고 Kotlin이 남은 StartRenderer/Attach/BindDirect 명령을 중단해 해당 surface를 **BLOCKED/ProducerStuck에 준하는 상태**로 둔다. void API에서 실패를 숨기거나 timeout 뒤 그냥 다음 producer를 연결하지 않는다.
- 완료 ACK는 정확한 binding/surface/request 세대의 codec stop/delete와 해당 old native window의 해제 완료를 뜻해야 한다. 단순 enqueue·gate 무효화·worker 명령 수신은 detach 완료가 아니다. 늦은 ACK가 새 owner/surface의 blocked 상태를 풀거나 자동 attach하지 않게 한다.
- 특히 MainActivity.kt:6196 다음의 Java Surface release와 :2356의 `onSurfaceTextureDestroyed` 소유 반환까지 확인한다. detach가 pending인데 기존 성공 경로처럼 무조건 반환/release하지 않는다. 필요하면 SurfaceTexture를 앱 소유로 보존해 맞는 producer 해제 뒤 한 번만 release한다. TextureView 콜백에서 false를 반환하면 앱이 후속 release를 책임지는 API 계약이다. [Android SurfaceTextureListener 문서](https://developer.android.com/reference/android/view/TextureView.SurfaceTextureListener#onSurfaceTextureDestroyed(android.graphics.SurfaceTexture))
- 느린 codec를 강제로 동시에 stop/delete하거나, 살아 있는 worker가 쓰는 자원을 timeout thread에서 해제하는 우회는 금지한다. worker 단일 소유를 유지하고 pending frame뿐 아니라 명령·NativeWindow 참조도 bounded/coalesced 관리한다. 미완료 자원을 새 surface마다 무제한 쌓지 않는다.

8초 barrier 대조에서 RX/control 진행뿐 아니라 UI detach의 bounded 실패·새 producer 연결 0·늦은 완료의 단일 해제와 세대 차단을 확인한다. 보고의 50ms 예시 상한은 RX/control의 빠른 호출에 적용하고, 명시적으로 완료를 기다리는 detach는 별도의 최대 2초 계약으로 시험한다. native/Kotlin 계약 변경은 같은 Android liveness 보수 범위로 허용한다. 별도 사용자 승인이나 이번 설계를 다시 상신할 필요 없이 고정 SOURCE 패킷으로 연결한다.

## D2 — active control 종료 조사 분리, APK 0.2.33 gate 유지

**별도 조사 항목으로 분리하되 0.2.33 출하 gate에서 빼지 않는다.** decoder worker 보수와 병행하고, 같은 원인이라고 미리 묶거나 별도 확정 결함 두 개라고 세지 않는다. ordinary와 standby activation 양쪽의 예상 밖 연결 종료가 남아 있어 현재 APK는 계속 출하 보류다. 독립 PC/서버의 관련 없는 작업 전체를 멈추지는 않는다.

**다음 실제 run 전에 local fixture relay 방향별·시각별 계측을 넣는 순서에 동의한다.** r2에서 허용한 계측 범위의 구체화다.

1. run/relay flow/활성화 세대와 양 끝 transport의 마지막 진행 시각을 연결한다. 각각의 monotonic 시간과 wall-clock offset을 기록하고 단순 시각 문자열을 동일 clock으로 취급하지 않는다.
2. c2h/h2c 각각 수신·정상 분류·forward 시도·send 결과/오류·수신 tuple 변경/lease/fence drop·종료 이유를 분리한다. 1초 bucket과 마지막 시각을 정상 종료/비정상 정리 때 모두 남긴다. video와 reliable control/ACK 등 안전하게 식별 가능한 header 종류를 나눈다. 식별 못 하면 unknown으로 둔다.
3. pending reliable head의 실제 message/seq·처음 전송/재전송/ACK 수신·ACK 채택 또는 거부·close 이유, ClientSessionController의 control Tick/RX phase와 함께 본다. relay의 send 성공은 목적지 수신 증거가 아니다. ACK datagram 수만으로 올바른 pending head가 해소됐다고 하지 않는다. 기존 r2 correlation 계측을 먼저 재사용한다.
4. payload·자격 증명은 기록하지 않고 낮은 빈도 snapshot/계수로 남긴다. RX에 동기 파일 I/O나 긴 lock을 추가하지 않는다. wire·전달 정책·timeout/재시도 예산을 바꿔 증상을 감추지 않는다. **운영 server.js 게시/재시작은 범위 밖**이며 로컬 시험 fixture에만 적용한다.

고정 계측 SOURCE 확인과 adb 안정성 확인 뒤 **같은 SH-off E2를 1회, 첫 영상 뒤 최소 60초 active 관찰**로 한정해 방향별 진행/종료를 확인할 수 있다. 하네스 메뉴 탐색 지연 때문에 peer_lost 뒤 처음 실패한 것처럼 보이지 않게 최초 원인을 기록한다. 환경이 불안정하면 INVALID로 남기고 blind 반복하지 않는다. 이 1회 무발생만으로 두 과거 실패가 해결됐다고 닫지 않는다.

닫힘에는 단절 경계/원인에 대한 근거, 필요한 제품 또는 fixture 대응, 실패를 구분하는 대조와 해당 실제 사용자 흐름의 재확인이 필요하다. 시험 환경 원인으로 입증되면 제품을 불필요하게 고치지 않는다. 직접/relay나 ordinary/standby 비교는 그때의 증거가 필요한 arm만 추가한다. 고정 전송 예산을 늘리거나 자동 재연결로 종료를 덮는 것은 이번 계약이 아니다.

## D3 — 시험 adb 경로 고정과 관측 허용

**허용한다.** SDK adb의 절대 경로·version/hash를 고정하고 slot readiness·모든 Python/PS helper·logcat·설치/조회 호출이 같은 client를 사용하게 한다. PATH fallback과 재시도 때 다른 adb를 찾는 동작은 없앤다. 각 기기 호출은 승인된 시험 serial을 명시한다. 기존 서버의 listener PID/실행 경로/port·시작/종료 시각은 읽기 전용으로 확인해 단순히 다른 adb가 설치돼 있다는 이유로 원인이라고 하지 않는다.

절대 경로만 고정해도 shared 5037 서버가 격리되는 것은 아니다. 계속 불안정하면 시험 전용 loopback server port와 동일 SDK server/client를 쓰는 한정 구성을 허용한다. `-P`는 server 접속 port, `-L`은 server listen socket 지정이므로 모든 호출을 일관되게 맞춘다. [AOSP adb 매뉴얼](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/docs/user/adb.1.md)

전용 서버를 쓰면 자기 process/job/endpoint를 식별하고 관리하며 시험 serial 연결을 검증한다. 별도 port만으로 emulator transport 독점까지 보장됐다고 가정하지 않는다. 사용자 USB/다른 emulator를 조회·재연결·인증하는 범위로 넓히지 않고, 그 격리가 성립하지 않으면 전용 서버 실행을 중단해 구체 blocker를 남긴다. 전역 환경/PATH 변경·adb 설치 교체·5037 kill-server·전체 adb/taskkill·LDPlayer/Desktop 프로그램 종료·물리 기기 재설정은 승인하지 않았다. 자기 전용 서버의 정상 종료/필요한 자기 job 회수만 허용한다.

daemon 안내 stderr를 PowerShell terminating error로 오인하지 않도록 rc·stdout·정제 stderr를 보존하되, 실제 readiness/serial/boot 확인 실패는 fail-closed로 남긴다. logcat 종료는 즉시 기록하고 계측 공백을 0건으로 해석하지 않는다. 민감한 input text는 r6 정제를 유지한다. adb 불안정은 관측/오케스트레이션을 훼손하지만, adb를 지나지 않는 제품 UDP 종료의 원인으로 현재 확정하지 않는다.

시험 AVD의 기존 유휴·슬롯 독점·동일 userdata 정상 재시작 허용은 유지한다. **우선 계측·adb 관측/고정 후 필요성을 판단**하며, 재시작 자체를 제품 수정이나 문제 해결의 증거로 삼지 않는다. 사용자 프로그램을 건드리지 않는다.

## 설계/회귀 확인 사항

worker 하나와 짧은 stateMu_, 실제 sink를 사용하는 fake NDK barrier, 실제 ClientSessionController/loopback 대조 방향은 수용한다. session.cpp 무변경은 합리적인 우선 범위이며 그 자체가 완료 조건은 아니다. 모든 control 인접 sink 호출이 빨리 반환해야 한다.

`KeyDelivered`의 “queue 성공”은 **inbox enqueue가 아니라 실제 AMediaCodec_queueInputBuffer 성공 + 현재 binding/surface/selection/codec 세대 일치**라는 보고 본문 정의로 고정한다. 8 frame/4MB는 초기 제안값이며 in-flight/bootstrap/명령 참조를 제외한 착시 상한이 되지 않게 한다. 포화·oversized IDR·준비 전 IDR 소진 뒤 재요구·취소·실패·해제도 검증한다. 이전 sink의 음성 대조가 어느 assertion에서 왜 실패했는지 남기고 실제 hang 없이 유한 시험 deadline으로 회수한다.

다음 제출은 위 결정 반영 고정 제품 SOURCE/대역 대조, local 계측·adb script snapshot 및 한정 실행 근거를 같은 정본에 연결한다. 이미 성공한 독립 안전 하네스 전량 반복이나 매 미시 단계 재승인은 필요 없다.

이번 Codex는 보고·고정 source·공식 API 문서 읽기와 판정만 수행했다. 제품 편집/build·Host/Viewer/AVD/adb 실행·재시작·삭제·설치·운영·commit/push는 없다. 검토 artifact만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
