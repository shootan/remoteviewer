
## r4 (Codex codex_apk_decoder_liveness_src_r1.md NEEDS_CHANGES) — N1 예약/확정, N2 상한, N3 생성 실패 회복, D1 획득 실패

**보수 commit `0a108b3`** (fix/apk-decoder-liveness, 부모 7a79b09).
- 바뀐 파일: 제품 4개 + 시험 5개입니다.
  - 제품: `android_video_decoder.cpp/.hpp`, `selection_ack_gate.hpp`, `decoder_pending_slot.hpp`
  - 시험: liveness / idr_recovery / pending_slot 시험, fake NDK
- 느린 codec API를 state lock 아래로 되돌린 곳은 없습니다. wire·transport·owed 예산은 바뀌지 않았습니다. SH seam은 손대지 않았습니다.

### N1 — gate 결정과 실제 queue 사이 (Codex 재현 반례)

**원인 (Codex 판정 그대로)**
- worker는 `dequeueInputBuffer`와 `queueInputBuffer`를 mu_를 풀고 부릅니다. 그런데 그 전에 확인한 `PermitsQueue`는 바이트가 실제로 들어가는 순간을 보장하지 못했습니다.
- 게다가 unbound local 선택의 candidate를 **RX 쪽 `Admit`이 바꿨습니다.** 그래서 101을 dequeue하는 사이 202가 candidate를 가져갔고, 둘 다 codec에 들어갔습니다.

**보수 (`selection_ack_gate.hpp`) — queue를 예약으로 바꿨습니다**
- `Admit`은 IDR을 통과만 시키고 아무것도 바꾸지 않습니다. `KeyLost`도 다시 빚(owed)으로 둘 뿐, candidate는 건드리지 않습니다. 즉 RX는 예약을 움직일 수 없습니다.
- worker가 queue할 frame에 대해 `ReserveQueue(gen, isKey)`를 합니다. unbound local 선택의 candidate는 이때 worker가 고릅니다.
- `CommitQueue`는 예약 시점의 epoch가 그대로일 때만 owed IDR을 지불하거나 bind합니다. `AbortQueue`는 그 epoch 안에서만 candidate를 놓습니다.
- `Prepare` · `Reset` · `BeginLocal` · `Refuse` · `OnAck`는 epoch를 올립니다. 디코더에서는 이 변경마다 codec reset도 요청됩니다.

**교환 단위 계약 (`TryQueueFrameLocked` 주석에 적었습니다)**
- dequeue가 끝난 뒤 queue 전에 변경을 보면: frame을 **queue하지 않습니다.** 그 codec은 무효가 되고(reset이 이미 pending이거나 여기서 요청), dequeue한 buffer도 그 codec과 함께 버려집니다.
- queue 호출 도중에 변경이 오면: 바이트는 이미 무효가 된 codec에 들어간 것입니다. owed 지불·bind·bootstrap은 0이고, 그 codec의 그림은 ready로 세지 않습니다.
- 변경 뒤에 시작한 교환은 새 상태로 판정합니다.
- 예약이 stale인데 reset이 없는 경우는 도달하지 않는다고 봅니다. 그래도 그때는 codec을 무효로 하고 로그를 남깁니다.
- unbound local 선택은 **처음 codec에 들어간 IDR이 bind**합니다(클래스 주석 그대로). 다른 세대는 자기 queue에서 거부됩니다.
- 최초 출력이 선택을 ready로 만드는 것은 그 세대가 확정됐을 때(`expected != 0`)뿐입니다.

**Codex probe 기준과의 차이 (한 번만 적습니다)**
- `candidate_probe.cpp`는 "A1이 들어가면 누출"로 판정합니다. 202가 이기는 쪽을 전제한 기준입니다.
- 이번 계약에서는 A1만 들어가고 bound=101, B2는 거부되는 것이 정상입니다.
- 그래서 시험 [9]는 "누출"을 **최종 bound와 다른 세대가 codec에 들어감**으로 정의했고, B2가 들어가지 않았는지도 따로 봅니다.

### N2 — inbox 상한
- `inbox_.size() > 1` 예외를 없앴습니다. 이제 총량 ≤ 4 MB가 항상 지켜집니다.
- 상한보다 큰 단일 frame은 입구에서 거부합니다. 처리는 버린 frame과 같습니다: IDR이면 KeyLost(다시 owed), key 요청, gap 처리. 별도 계수 `inboxRefused`와 로그 1줄을 남깁니다.
- **값의 근거**: 4 MB는 클라이언트가 요구하는 최고 bitrate 1초 분량보다 큽니다(`kRuntimeBitrateMax` 30 Mbit/s = 3.75 MB, viewer_constants.hpp:149). 조립기 자체는 16 MB까지 받습니다(`kMaxUdpAssembledPayloadBytes`, native_video_client_shared_core.cpp:34).
- **inbox 밖 사본** (`android_video_decoder.hpp` 주석): 처리 중 1 + pendingFrame_ 1 + bootstrapFrame_ 1 + replay 임시 사본 1이고, 각각 상한 이내입니다. 따라서 sink 전체 frame 메모리는 최대 4 + 4×4 = **20 MB**입니다.

### N3 — 생성 실패 + owed 예산 소진 + idle Host
- **7a79b09에서 실제로 회복하지 못합니다**(아래 음성: U5 FAIL, ready 뒤 요청 0, codec 없음, control은 살아 있음).
- **보수**: worker가 생성 실패 뒤 스스로 다시 만듭니다.
  - 1초 간격, surface·선택·binding 하나당 최대 20회입니다.
  - 이렇게 만든 codec이 올라오면 `gate_.Rearm()`을 부릅니다. 새 token이 생기고, 세션은 **같은** 8회 예산으로 다시 요청합니다. 새 surface에 이미 있던 규칙과 같습니다.
  - 20회가 지나면 `codecState=failed_terminal`이 되고 로그 1회를 남깁니다. 그 뒤에는 새 surface·새 선택·Host가 보낸 IDR만 회복시킵니다(IDR이 오면 항상 생성을 시도합니다).
  - 무제한 재요청이나 transport 예산 증가는 0입니다. 재시도는 로컬 생성뿐이고, Host로 가는 요청은 Rearm 1회당 기존 예산 그대로입니다.
- **한계**: `pending_ == 0`(선택 없음)에는 owed token이 없어서 Rearm이 요청을 만들지 않습니다. APK는 항상 선택을 거쳐 보므로 해당하지 않는다고 봅니다. 이것은 코드 경로 판단이고 기기에서 확인하지는 않았습니다.

### D1
- **획득 실패**: non-null Surface에서 `ANativeWindow_fromSurface`가 실패하면 **0(거부)**을 반환합니다. surface·codec·detach 상태는 바뀌지 않습니다. 예전에는 detach로 처리돼 seq를 반환했습니다(음성 [13]).
- **문구**: detach가 마감을 넘긴 Surface는 **교체될 때까지 BLOCKED**입니다. 늦은 완료는 정리만 하고 아무것도 attach하지 않습니다. 같은 blocked Surface는 다음 bind만으로 풀리지 않습니다. 대기 중 새 Surface가 왔다면, 완료 뒤 **그 새 Surface의 다음 bind**에서 연결됩니다. 앞 r3 절의 "다음 bind" 표현은 이것으로 바로잡습니다.
- 사용자에게 열린 명시 재시도 경로가 어떤 surface lifecycle을 만드는지는 실제 배선 EVIDENCE가 없어 **미검증**입니다.

### D2 문구 정정
- off build에서도 비용이 0은 아닙니다.
  - `UdpControlChannel::Stats/Outbound`와 `ClientSessionController`에는 진단 필드가 항상 있습니다(크기·초기화 비용).
  - `GetStats`는 기존 mutex 아래에서 txQueue head와 lastAcked를 O(1)로 조회하고, 더 큰 Stats를 복사합니다.
- 새 network send나 계측 I/O는 없습니다. PC/Host 소비 target은 같은 header로 다시 빌드해야 합니다.
- trace on에서 logger가 `GetStats`의 mutex를 읽습니다. "무-lock snapshot"은 session atomics 부분에만 맞는 말입니다. 따로 부르는 `GetStats`/`IsClosed`/`CloseReason`은 하나의 원자 snapshot이 아닙니다.
- 앞 절의 "출하 코드·비용 변화 0" 표현은 이것으로 바로잡습니다.

### SH seam 수명 (보충 「확인 4」에 대한 정리, 코드 변경 없음)
- 앞 절은 "제품 sink가 전역이므로 static 소멸 경합뿐"이라고 했습니다. 이 설명은 **출하 build에서 seam이 compile되지 않는 범위**에만 해당합니다.
- seam을 켠 build에서는 publisher가 raw this를 잡고 join·완료 추적이 없습니다. 그래서 static 소멸이나 지역 sink에서는 수명 보장이 없습니다. 2000 ms는 sleep 인자의 상한일 뿐, 완료를 보장하지 않습니다.
- 이번 보수 범위 밖이며 미검증으로 남깁니다.

### 시험 (기기 0, stand-in NDK — 모두 pure-logic + threads; idr_recovery는 loopback network)

새로 추가한 대역 기능입니다(시험 전용):
- fake NDK가 `dequeueInputBuffer`(bit 16 = Codex review probe와 같은 위치)와 `queueInputBuffer`(bit 32)를 붙잡을 수 있습니다.
- `ANativeWindow_fromSurface` 실패를 흉내 낼 수 있습니다(`windowAcquireFails`).

| 시험 | r4 (`0a108b3`) | 7a79b09 sink+gate (음성) |
|---|---|---|
| android_decoder_liveness_test | **63/63** rc0, 반복 ×3 모두 63/63 | **10 FAIL** rc1 |
| android_decoder_idr_recovery_test | **49/49** rc0 (최종 binary 재실행 49/49) | **3 FAIL** rc1 |
| decoder_pending_slot_test | 21/21 (gate 예약 pure-logic 11건 추가) | — |
| client_session_prelogin_select_test | 60/60 | — |
| owed_key / binding / decode_stats / geometry / session_liveness | 32 / 17 / 42 / 10 / 9, 모두 rc0 | — |
| NDK `:app:buildCMakeDebug` (4 ABI) | BUILD SUCCESSFUL (APK 미생성) | — |

**새 대조와 음성 결과** (음성 = 7a79b09 sink + gate, 위 표의 FAIL 열)
- [9] Codex 스케줄
  - r4: bound=101, codec에는 `A1 A1`만 들어갑니다(두 번째는 같은 IDR의 bootstrap replay). B2는 들어가지 않습니다.
  - 음성: `A1 B2 B2`, bound=202로 **Codex 반례가 재현됩니다.**
- [9b] 거울 순서 (101이 input buffer를 기다리며 slot에 보류, 202가 대체)
  - 202만 들어가고 bind됩니다. 음성에서도 PASS입니다(원래 있던 계약).
- [10] dequeue 중 새 pick
  - 옛 frame은 queue되지 않고, 새 선택의 IDR이 **새** codec의 첫 입력이 됩니다.
  - 음성: 옛 frame 1개가 queue되고 새 codec이 만들어지지 않습니다.
- [11] queue 호출 중 새 pick
  - 시작된 교환은 옛 codec에 들어가고 그 codec은 삭제됩니다. ready는 0이고, 새 IDR은 새 codec으로 갑니다.
  - 음성에서도 PASS입니다(reset 계약은 r1부터 있었습니다). 계약 기록용입니다.
- [12] 5 MB frame
  - r4: 거부되고(inboxBytes 0, refused 1) owed와 key 요청이 남습니다. 1.5 MB frame을 연속으로 넣어도 inbox 최대는 3.0 MB입니다. 새 pick은 옛 손실을 owed로 물려받지 않습니다.
  - 음성: owed와 key 요청이 없습니다(행동 FAIL). inboxBytes·refused는 옛 build에 없는 필드라서, 이 두 FAIL은 "보고 없음"일 뿐 행동 증거가 아닙니다.
- [13] 획득 실패
  - r4: 0을 반환하고, 다음 frame은 A의 codec으로 갑니다. 삭제 0, released seq 그대로입니다.
  - 음성: result=2(detach로 처리), codec 1개 삭제.
- U5 생성 실패 + idle
  - r4: ready 전 요청 8회가 모두 소진되고, ready 뒤 **+96 ms**에 현재 IDR이 첫 입력으로 들어가며 +98 ms에 표시됩니다(새 token으로 요청 1회).
  - 음성: 요청 0, 영구 black, control은 alive입니다.
- U5b 26 s 실패
  - `failed_terminal`이 되고, 3초 동안 자체 회복 0·요청 0입니다. 새 surface 뒤 +14 ms에 회복합니다.
  - 음성: terminal 상태 자체가 없습니다(FAIL). 새 surface 뒤 회복은 원래 경로라 PASS입니다.
- 음성 build가 **옛 gate**를 썼다는 근거: 7a79b09 decoder는 인자 1개짜리 `PermitsQueue`를 부릅니다. 새 gate에는 그 서명이 없으므로, 옛 gate가 아니었다면 compile되지 않았습니다. sink dir `.claude/caplife/liveness_neg/r4_7a79b09`(5파일)는 include 순서상 src보다 먼저 옵니다.

### 바뀐 기대와 이유
- **unbound local 선택의 candidate**: "RX가 통과시킨 마지막 IDR"이던 것을 "worker가 처음 codec에 넣은 IDR"로 바꿨습니다.
  - 기존 pending_slot의 "G held → H 도착 → H만 bind" 대조는 그대로 통과합니다. held frame을 IDR이 대체하는 규칙은 바뀌지 않았기 때문입니다.
  - prelogin_select 60건도 그대로 통과합니다.
- **`PermitsQueue`에 `isKey` 인자를 추가**했습니다. unbound local 선택은 IDR만 허용합니다. 호출부는 pending slot 2곳과 bootstrap 1곳입니다.

### 미검증
- 실제 MediaCodec(기기·에뮬레이터)에서 dequeue와 queue 사이 경합 빈도, `failed_terminal`의 사용자 화면, 명시 재시도의 실제 surface lifecycle은 확인하지 않았습니다.
- 새 APK는 만들지 않았습니다(Codex 순서: SOURCE OK 뒤 r8 snapshot, 새 APK, ke-r26). ke-r26은 계속 보류입니다.
- 기존 전체 시험을 ×5로 반복하지 않았습니다(Codex 지시). 새 race 대조만 liveness ×3으로 반복했습니다.
