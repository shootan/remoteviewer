# APK decoder liveness r4 SOURCE

2026-10-29. 고정 **`0a108b39830664e28ae36d79372912eae894b9bf`**(부모 7a79b09), tracked clean(`apk_out/` untracked), diff --check 통과. 보고 SHA-256 **`f12231479c08223f81ff575a865295a71eba65b3f1dd026fba90cf80e05a3f15`**.

**NEEDS_CHANGES — N1/N2/획득 실패 보수는 수용. 남는 blocker는 N3 재시도 상한을 incoming delta가 우회하는 경로다. ke-r26은 계속 보류한다.** 이전 지적을 새 이름으로 전량 재상신하지 않고 아래 한 건을 같은 작업용1 task에서 보수한다.

## N1 — 기준 변경 수용, 기존 경합 닫음

“A1만 queue되고 bound=101”은 정상으로 수용한다. 원래 사용자 계약은 **처음 실제 codec에 들어간 IDR이 local 선택의 세대를 bind**하는 것이다. 반드시 마지막으로 RX에 도착한 202가 이겨야 한다는 계약이 아니다. 이전 반례는 7a79b09에서 실제 gate candidate가 202로 바뀐 뒤 101도 queue되고 최종 bound=202가 되어 두 세대가 섞인 문제였다.

r4의 Admit은 candidate를 바꾸지 않고, worker ReserveQueue가 queue 교환을 예약한다. Commit/Abort의 epoch와 decoder reset seq가 완료의 유효성을 판정한다. 이 순서로 101이 bind한 뒤 202를 거부하는 것은 허용된다. 반대로 input buffer를 얻지 못해 보류된 G를 H가 대체해 H가 처음 queue되는 기존 계약도 유지한다.

dequeue 뒤의 변경은 queue 전에 차단하고, 이미 시작한 queue API 중 변경은 무효 codec에 한정해 지불/bind/ready를 억제한다는 교환 계약도 수용한다. slow API를 state lock 아래로 되돌리지 않았다. 단순 “최종 bound와 동일한 것만 남았음” 집계만으로 모든 취소 순서를 증명하는 것은 아니며, phase별 대조가 함께 필요하다.

Codex가 고정 decoder·gate·pending slot을 freeze하고 실제 sink/fake NDK로 직접 확인한 결과:

```text
queued tag=0xA1
queued tag=0xA1
local bound=101
CONTRACT first queued generation binds: PASS bound=101
CONTRACT owner change during dequeue: PASS
```

두 번째 A1은 기존 bootstrap replay다. B2 queue는 없었다. 별도 owner 대조는 token 100의 input dequeue를 멈춘 동안 Bind(200)를 완료시킨 뒤, 옛 D4 queue 0·새 owner의 E5 queue를 실제 확인했다. 보고의 “Bind는 같은 경로라 미실행”은 **작업용 보고의 한계로 유지**하되 이 Codex 독립 한정 대조가 추가됐다. 이런 owner 대조를 새 보수의 회귀에 재사용할 수 있다.

문구 보정: gate epoch가 오르는 모든 wrapper에서 실제로 RequestCodecReset을 호출하는 것은 아니다(OnAck/BeginLocal/Refuse wrapper 등). 현재 awaiting 조건상 유효한 예약과 겹치지 않는 경우 및 stale reservation의 방어 경로를 구분해 설명한다. 이를 이유로 불필요한 codec reset을 무조건 추가할 필요는 없다.

## N2 / D1 — 보수 수용, 메모리/bitrate 표현 보정

4MiB보다 큰 단일 payload를 입구에서 거부하고 마지막 한 frame 예외도 없앤 것으로 inbox 논리 byte cap 지적을 닫는다. native window 획득 실패를 sink 상태 변경 전 0 거부로 반환하는 보수도 수용한다. bounded detach/late cleanup·교체 전 BLOCKED 계약은 기존 판정 그대로다.

다음 설명만 고친다(별도 재실행/승인 왕복 불필요).

- 30Mbit/s는 평균/목표 bitrate이며 **그 값만으로 모든 개별 IDR이 4MiB 미만임을 증명하지 못한다.** source의 “within it never meets it” 단정은 제거한다. 4MiB는 현재의 명시적 자원 정책이며 초과 frame의 거부·IDR 요구/실패 처리가 있는 것으로 설명한다.
- “20MB”는 4MiB inbox와 네 payload 사본의 길이를 합친 **최대 20MiB 논리 payload 예산**이다. vector capacity·조립기·CSD·NDK input/output·surface/GPU·allocator 등까지 포함한 앱 실제 memory footprint 상한으로 쓰지 않는다. source/report의 범위와 단위를 맞춘다.
- “mu_ 아래 codec 호출 0”은 활동 중 RX/control/UI 경로의 분리로 한정한다. destructor는 worker join 뒤 LockedState 아래에서 inline ResetCodec/ReleaseSurface를 수행한다. 종료 후 quiescent 경로를 일반 codec worker의 단일 소유와 구분하면 된다.

## 남는 N3 / P2 — delta 입력이 retry cadence/budget를 우회

위치: `android_video_decoder.cpp` ProcessFrameLocked의 unconditional EnsureCodecLocked 호출(약 :396), 실패 때의 `codecRetryAtUs_=now+1s`(약 :995), RetryCodecLocked(약 :1056).

현재 codec이 없으면 **매 incoming frame이 timer/budget 검사 전에 EnsureCodecLocked를 직접 호출**한다. delta도 같다. 생성 실패마다 다음 timer를 다시 now+1초로 미루고, codecAutoRetries는 timer 경로에서만 증가한다. 따라서 delta가 1초보다 자주 오면 자동 생성 실패가 반복되어도 timerRetries=0으로 남고 failed_terminal에 도달하지 않는다. 보고에서 허용한 “Host의 새 IDR로 재시도” 예외와도 다르다.

Codex 순수 대조는 현재 선택의 ACK와 CSD/IDR을 한 번 제공하고 fake create를 계속 실패시킨 뒤, **새 IDR 없이 50ms 간격 delta 40개**를 넣었다. 약 2초 후:

```text
codecState=failed codecFail=41 codecRetries=0
CONTRACT two seconds, one IDR plus 40 deltas:
failures=41 timerRetries=0 FAIL (deltas bypass retry cadence and budget)
```

idle U5/U5b의 회복/terminal 성공을 부정하지 않지만, incoming delta가 있는 일반 스트림의 유한 재시도까지 증명하지 못한다. 실제 codec 생성 실패에서 API/로그가 frame rate로 반복될 수 있으므로 “1초 간격 최대20회”라는 최종 정책을 이 상태로 승인하지 않는다.

**필요 보수:** 정상 frame 경로와 timer 경로가 같은 생성 재시도 next-deadline/예산을 소비하게 하거나, 실패 상태에서 일반 delta가 직접 생성하지 못하게 한다. 최초 생성·명시적 새 surface/선택/owner·허용할 새 IDR 예외를 구분하고 예외의 재시도/속도 계약을 명시한다. 단순히 timer counter 이름을 바꿔 기존 frame-rate 재시도를 정상으로 재분류하지 않는다. codec API는 계속 state lock 밖이며 RX/control의 진행성 및 기존 owed 8회/transport 예산은 유지한다.

대조에는 (1) 지속 delta 중 실패의 실제 호출 간격/총량·terminal 진입, (2) terminal 뒤 delta만으로 생성 재개 0, (3) 명시적 새 surface/selection/owner와 허용한 IDR의 회복, (4) 기존 idle U5/U5b 회복을 포함한다. virtual clock 또는 실제 sink의 유한 대역으로 하고, 실제 MediaCodec/AVD를 실패시키지 않는다. 정상 create/start/IDR 선택 결과가 바뀌지 않는 영향 시험을 이어간다.

## Codex 독립 대조 기록

`.claude/codex_decoder_liveness_review/review_r4.py` 및 `r4_0a108/{candidate_probe.cpp,android_video_decoder.cpp,android_video_decoder.hpp,selection_ack_gate.hpp,decoder_pending_slot.hpp,fake_ndk.cpp,build.log,candidate_probe.log}`.

- build rc0, probe rc1. rc1은 위 N3 반례이며 N1과 owner check는 PASS다.
- exe SHA-256 `1b5647c204ab6efb7643e81374ed564528b2673c8a755cb564e91fc0fa7181b0`.
- log SHA-256 `627e23c4517991a4538288d264e17a42e8b0bcfc444a54aba92f3d34f78f0763`.
- 첫 실행의 inherited 옛 diagnostic label을 r4 계약에 맞춰 보정 후 한 번 재실행했다. 첫 raw는 `initial_probe.log`에 보존했다. 두 실행 모두 candidate/owner PASS, N3 41회/0 counter로 같다. 옛 “A1이면 누출” 출력 문구를 새 계약의 실패로 인용하지 않는다.

## 다음

N1/N2/D1은 다시 전량 반려하지 않는다. 같은 지시서에 N3 보수와 위 문구 정정을 합쳐 고정 후보/새 대조를 제출한다. 기존 idle/queue/dequeue·selection 회귀를 필요한 범위에서 이어가며 모든 과거 시험을 ×3/×5로 반복하지 않는다. 새 APK/ke-r26은 아직 실행 보류다. Source 통과 후 새 hash/r8 snapshot·30초 adb 안정 관찰로 한정 E2 진단을 잇는다.

terminal UI·명시 재시도의 실제 surface lifecycle·실기 경합 빈도는 후속 EVIDENCE 항목으로 유지한다. 다음 건강한 E2 60초 한 번의 성공으로 terminal/전체 APK 출하까지 완료라고 하지 않는다. PC 0.2.164 분리 릴리스는 이 branch 미포함이라는 경계를 유지하며 그 독립 작업은 계속한다.

Codex는 source 검토와 실제 제품 sink/fake NDK의 자체 순수 probe만 실행했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·사용자 설치/운영·삭제·제품 수정·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
