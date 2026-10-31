# APK decoder liveness r1–r3 통합 SOURCE

2026-10-29. **NEEDS_CHANGES — N1의 실제 경합을 재현했다. ke-r26-shoff-lt는 이번 후보로 실행하지 않는다.** worker 분리·bounded detach·계측 방향을 폐기하는 판정은 아니다. 같은 작업용1 task의 한 보수 묶음으로 아래를 연결한다.

## 고정/검토 근거

- Worktree `C:/work/gnlink-wt/apk-decoder-liveness`, HEAD `7a79b09df02017831515d342733452809b022fd2`; 부모 ec016f0/27d5966, base 9f9a91c. tracked diff 없음, `apk_out/`만 untracked. diff --check 통과.
- 보고 SHA-256 `8076ea7157e4dc3aa614e1a52d9d902081f9173ff8a5aa16182296761977ec11`.
- `liveness_evidence/SHA256SUMS.txt`: `162a223b37537fb9aac342e4668153d689df2b7a2b7396c83405adeb71fbab33`.
- r8 snapshot SUMS: `d6092772658731c6929736595f369498565bf5530e4f4661d98ca7b6997775a2`.
- 제출 원문의 liveness 40/0, session 9/0, IDR 36/0 요약은 확인했다. 이는 작업용 실행이며 Codex의 독립 재실행으로 바꾸지 않는다.

## N1 / P1 — codec 호출 사이의 gate 결정이 원자적이지 않음

위치: `android_video_decoder.cpp:1046–1102`의 TryQueueFrameLocked 및 `decoder_pending_slot.hpp` Offer/Pump, `selection_ack_gate.hpp:148–173`.

PendingSlot은 queue callback에 들어가기 **전에** PermitsQueue를 확인한다. 그런데 TryQueueFrameLocked는 `CodecCall`에서 state lock을 풀고 dequeueInputBuffer/getInputBuffer를 호출한다. 그동안 RX의 Admit 또는 선택/owner 변경이 gate를 바꿀 수 있다. 돌아와서 queue 전에는 gate/세대 재검사가 없고, **queue한 뒤** resetSeq를 비교하는 것으로는 이미 들어간 바이트를 되돌리지 못한다. local candidate 교체는 codecResetSeq를 올리지도 않는다.

Codex가 고정 제품 decoder.cpp/.hpp를 그대로 컴파일하고, fake NDK의 dequeueInputBuffer 진입에만 대역 barrier를 추가해 다음 순서를 강제했다. 이 barrier는 codec API 사이에 다른 thread가 실행되는 스케줄을 만드는 것이며 실제 NDK가 timeout 0에서 반드시 오래 멈춘다는 주장이 아니다.

1. selection 42를 Prepare→BeginLocal.
2. IDR generation 101(tag A1)을 받는다. PermitsQueue(101) 통과 뒤 worker의 input dequeue를 멈춘다.
3. RX가 IDR generation 202(tag B2)를 받는다. 같은 local 선택의 현재 candidate는 202가 된다.
4. worker를 풀면 **A1도 실제 fake codec queue에 들어가고 B2도 들어간다**. 최종 local bound는 202다.

원문:

```text
candidate 202 admitted while candidate 101's dequeue was held
queued tag=0xA1
queued tag=0xB2
local bound=202 count=2 obsolete candidate queued=1
REPRODUCED: gate changed before old candidate was queued
```

재현 자료 `.claude/codex_decoder_liveness_review/reproduce.py`, `r3_7a79/candidate_probe.cpp`, `fake_ndk.cpp`, `build.log`, `candidate_probe.log`. build rc0 / probe rc1(반례 재현). exe SHA-256 `25d8a2433a305f828be0621f720e3b6a85231e141ee62bdbafbbbe5c6c58bb2cd`, 로그 `fae6361651a356d2cf00672669266c2aec55ec5804329af31fcf9c4688abee14`.

**필요한 보수:** 현재 선택/generation을 actual codec queue의 시작·성공/실패와 연결하는 reservation/commit 계약을 둔다. 느린 codec API를 state lock 아래로 되돌리지 않는다. 단순히 이미 queue한 뒤 ready/KeyDelivered만 억제하거나, 두 번째 unlock 전에 check 하나만 더 놓고 그 뒤 경합을 그대로 남기는 수정으로 닫지 않는다. local candidate의 교체/채택 순서를 codec worker 소유와 정합되게 만드는 방식도 가능하다.

이 스케줄, dequeue 뒤 reset/owner 변경, queue 성공 뒤 KeyDelivered/최초 출력 게시의 세대 대조를 함께 시험한다. 취소/owner 변경 전에 이미 시작한 교환과 변경 뒤 새로 시작하는 교환을 구분해 계약을 명시한다. 이번 반례는 기존 prelogin 60/SettledSink 시험으로 덮이지 않는 **실제 Android sink 비동기 경계**다. 사용자 prelogin UI 시험은 계속 금지하며 pure fake NDK로 보수한다.

## N2 / P2 — “inbox 4MB 상한”이 실제로는 강제되지 않음

`android_video_decoder.cpp:334`의 while 조건은 `inboxBytes_ > kInboxMaxBytes && inbox_.size() > 1`이다. 따라서 **단일 frame이 4MB보다 커도 마지막 한 개를 그대로 보존**한다. header의 “at most 8 / 4MB”와 보고의 메모리 상한 주장은 이 코드와 다르다. frame 수만 8 이하인 시험으로 byte 상한을 증명하지 않는다.

단일 oversized frame 처리(거부/명확한 실패·필요한 IDR 요구)를 정하고 byte cap을 강제하거나, 허용할 실제 상한을 근거와 함께 명시적으로 바꾼다. 무제한 마지막 frame 예외는 제거한다. inbox 외 worker in-flight/pending/bootstrap 사본도 별도 수량/최대 크기를 합쳐 기록한다. 상한 초과·IDR/delta 교체에서 현재 선택의 빚이 사라지거나 다른 선택을 오염시키지 않는 대조가 필요하다.

## N3 / P2 — (a)의 알려진 잔여 경계를 대조로 닫기

보고가 스스로 남긴 **느린 codec 생성 실패 + owed budget 소진 + idle Host**를 아직 정상 회복 증거로 세지 않는다. “생성을 촉발한 IDR/뒤 IDR이 남아 있다”는 설명은 생성 성공 arm에는 맞지만, 실패 중 그 IDR들을 모두 소비/폐기한 arm의 근거가 아니다. worker가 뒤늦게 세운 요청 플래그가 다음 frame에만 소비된다면 idle에서는 그 다음 frame도 오지 않을 수 있다.

실제 제품 sink/session 대역으로 이 조합을 추가하고, 현재 surface/owner에서 회복 가능한 상태가 되었을 때 기존 예산 안의 새 IDR 수요가 진행하는지 확인한다. 자동 회복하지 않는 terminal codec 실패로 처리한다면 그 실패와 명시적 retry/새 surface 경로를 보여야 하며, 무한 black+control alive를 정상 준비로 세지 않는다. 보수 필요 여부를 이 반례로 판단하고 **무제한 재요청/transport 예산 증가를 먼저 도입하지 않는다.** 기존 (a)/(b) 성공 arm은 보존한다.

## D1 — 수용한 부분과 재연결 표현

SetSurface(null)의 최대 2초 wait_for, 이전 detach pending에서 추가 대기하지 않음, codec stop/delete·window release 뒤의 ReleasedSeq, Kotlin의 나머지 producer 명령 중단과 보유 Surface 보존 방향은 수용한다. 두 seq로 reset/surface를 합친 것 자체는 반려 이유가 아니다. 모든 gate 변화가 실제 queue/publication의 경계와 연결돼야 한다는 N1이 핵심이다.

늦은 완료는 **정리만 하고 자동 attach하지 않는다**는 결정을 유지한다. 단 현재 policy는 동일한 `decoderBlocked` Surface에 대해 `bind()`를 다시 불러도 풀지 않는다. 정확한 표현은 “그 Surface는 교체될 때까지 BLOCKED. 대기 중 새 Surface가 왔다면 완료 뒤 그 새 Surface의 다음 bind에서 연결”이다. 같은 blocked surface의 다음 bind만으로 회복한다고 안내하지 않는다. 사용자에게 가능한 명시 재시도 경로가 어떤 surface lifecycle를 만드는지는 후속 실제 배선 EVIDENCE로 확인한다.

추가 실패 처리: `ANativeWindow_fromSurface`가 non-null 인자에서 실패하면 현재 구현은 nullptr detach 경로로 진행한다. non-null 호출자는 0만 거부로 보므로 음수도 attach 접수로 처리할 수 있다. **획득 실패는 detach/성공 seq로 바꾸지 말고 0 거부로 반환**하고, 기존 Surface/producer 소유 상태를 보존하는 대조를 추가한다. 이 예외 처리는 위 보수와 함께 처리한다.

## D2 공유 trace — 동작/성능 판정

GNLINK_LIVENESS_TRACE 매크로의 기록은 off build에서 비워지고 Gradle은 release에 trace 속성이 주어지면 거부한다. session의 bool 변수 추출·diagnostic hook만으로 기존 전송 성공/실패·retry/ACK 판단을 바꾸는 변경은 찾지 못했다. **wire format 변경도 없다.** 다만 “출하 코드/비용 변화 0”이라고 하면 부정확하다.

- UdpControlChannel::Stats/Outbound 및 ClientSessionController에 진단 필드가 항상 존재한다. 클래스/struct 크기와 초기화 비용이 변한다.
- GetStats는 off build에서도 txQueue head/lastAcked 등의 O(1) 조회와 더 큰 Stats copy를 추가한다. 기존 mutex 아래이며 새 network send/계측 I/O는 만들지 않지만 zero cost는 아니다. PC/Host 소비 target은 같은 header로 재빌드해야 한다.
- trace on에서는 별도 logger가 GetStats의 기존 mutex를 읽는다. 완전한 무-lock snapshot이라는 표현은 session atomics 부분에만 적용한다. 별도 GetStats/IsClosed/CloseReason 호출은 하나의 원자 snapshot도 아니다.

PC 0.2.164에 이 branch를 넣지 않는 범위는 유지한다. 이후 통합할 때 shared target compile/해당 UDP control 회귀를 확인한다. trace 관측값만으로 프로덕션 타이밍 불변을 증명하지 않는다.

직접 확인한 APK hash:

| APK | SHA-256 | native liveness-trace |
|---|---|---|
| r3-release-unsigned | `25fb6d981994637c1ade12d4e08f74ce22ca0517b008e9aafb17e3fe74379503` | 0/4 |
| ke-off-lt | `a7816b3cbbc04134fca1671a12e18866ff5e4b3fc9b5cbda3f76541bc551b7e7` | 4/4 |
| ke-on-lt | `aa15aad48e9b75f0377cdc31bc13e65247357760250972b7b630c02be80b46c7` | 4/4 |

fixture relay trace는 flag off에서 기존 send 경로를 유지하고, on일 때 방향/종류별 rx와 send callback 결과를 나눈다. 600 bucket/50 event 상한도 있다. 이 local 계측의 방향을 수용하며 운영 배포는 승인하지 않는다. send callback 성공을 목적지 도착/ACK 채택으로 쓰지 않는다.

## D3/r8와 다음 단계

r8의 SDK adb 절대 경로/hash·version 기록, PS의 KA_SERIAL=emulator-5554 지정, 준비 확인/reader 오류 보존·listener 읽기 방향을 수용한다. 경로 고정이 shared 5037 서버 격리를 증명하지는 않는다. 예정된 30초 안정 관찰에서 같은 listener PID/path·serial/boot와 실제 probe rc를 기록한다. kill-server/다른 프로그램 종료 승인은 없다.

**현재는 ke-r26 실행 보류.** 같은 task에서 N1/N2·획득 실패를 보수하고 N3 대조 및 새 고정 SOURCE를 제출한다. 이미 통과한 전체 시험을 이유 없이 5회씩 반복하지 않고 새 경합/실패 arm과 영향받은 기존 selection/IDR/surface 대조를 우선한다. 보수 SOURCE 뒤 r8 snapshot/새 APK·server hash·relay trace flag·30초 adb 관찰을 연결해 예정된 SH-off E2 1회/60초 관찰로 이어간다. APK 출하 보류와 PC 분리 릴리스의 별도 범위는 유지한다.

Codex는 source/기존 자료 읽기·APK zip/marker 확인과 **제품 sink + fake NDK의 순수 review probe**만 실행했다. 새 barrier는 자기 review 복사본의 fake NDK에만 추가했으며 제품 파일은 바꾸지 않았다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치·운영·삭제·commit/push는 없다. 미추적 검토 artifact만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.

## 보충 「확인 4」 — SH seam detached handoff 수명

보고 최신 SHA-256 `495b1580f17665091a20d0a29532a29060ab80a87b744a4733aee45c98fdff5b`의 보충을 읽었다. 코드는 7a79b09 그대로이며 **기존 NEEDS_CHANGES와 ke-r26 보류는 변하지 않는다.** 이 항목을 새 반려 사유로 추가하지 않는다.

9f9a91c에도 같은 `[this, lateSlot, meta, size, handoffKey]` detached publisher가 있음을 git show로 확인했다. 현재 cpp:1333–1353의 `#ifdef GNLINK_SH_TEST_SEAM`/publishAfter 가지다. delay helper가 받아들이는 sleep 설정값은 1–2000ms이며, 그 뒤 handoffNs/Meta/Size/Key atomic 네 곳을 쓴다. codec/window/decoder lock을 쓰는 thread는 아니다. 이 APK JNI의 sink는 native_bridge.cpp:33의 전역 객체이므로 **일반 session teardown이 그 sink를 해제하지 않는다는 설명은 수용**한다. 앞 worker 자체의 session 소유와 이 테스트 publisher를 혼동하지 않는다.

다만 이를 “수명 안전이 증명됐다” 또는 모든 build에서 남는 위험이 오직 하나뿐이라고 확대하지 않는다. publisher는 raw this를 잡고 join/완료 추적을 하지 않으며, sink destructor가 기다리는 것은 worker_뿐이다. 따라서 static destruction/해당 객체·library 수명 종료까지 publisher가 남는 상황이나 seam을 켠 지역 sink에서는 안전 보장이 없다. atomic도 객체 수명이 끝난 뒤의 접근을 합법으로 만들지 않는다. **2000ms는 sleep 인자의 상한이지 thread가 반드시 그 시간 안에 완료된다는 보장은 아니다.**

현재 판정의 제외 근거는 출하에서 seam이 compile되지 않고 Windows 회귀도 이 seam을 쓰지 않는 범위다. “시험 seam이므로 UAF를 허용한다”는 일반 예외를 승인한 것은 아니다. 향후 publish-after-release seam을 켠 시험에서 sink 파괴/회수까지 검증하려면 publisher 완료·join/명시적 수명 보장 또는 그 종료 범위의 미검증을 별도로 기록한다. 단순 2초 sleep으로 완료를 주장하지 않는다. 이번 ordinary liveness 반례 보수에 관련 없는 SH seam 재설계를 끼워 넣거나 해당 시험을 추가 재실행할 필요는 없다.

보고/주석의 session teardown 위험 설명은 위 실제 전역 수명과 구분해 정리하면 된다. 이것을 고친다는 이유로 N1의 queue gate 경합을 닫거나 SOURCE/실행 승인을 다시 부여하지 않는다.
