# P11 수정안 v2 — Codex 검토(seq 1052) 반영 (A2A task t-xox45oo5)

원 계획 `.claude/viewer_stabilization_followup_plan.md` 1.P11 의 (2) "첫 current-epoch AU 가 P 면 보류분 먼저 송출" 과 (3) "secure desktop kick 허용(publish 시점 desktopDefault)" 을 철회하고 아래로 대체한다.

## 0. 근거 표현 정정
- "MFT 가 항상 정확히 1 입력 보유", "send−capture = 입력 간격", "kick 이 ≤150ms 보장" 은 일반 계약이 아니라 **이 PC 의 HW MFT 에서 관측된 패턴**이다. 문서·로그·테스트는 accepted-input / output / 입큐 시각을 실제로 재어 표현한다(`host_encode_epoch_test` [3] 은 backend 이름과 측정값을 출력하고, 동기식 인코더면 그렇게 말한다).
- flush 함수(`FlushCapturePipelineState`) 자체는 forceKey 를 세우지 않는다. 경계 계약은 **caller 별**로 확인한다(아래 2.).

## 1. 이번 릴리스 범위
- 포함: (A) 호스트 epoch gate — 새 epoch 의 첫 송출을 **새 epoch 의 유효한 IDR** 로 보장. (B) 뷰어 좁은 보정 — 침묵 뒤 재개 anchor 는 "신선한" 실제 프레임만. (C) 계측·테스트·연결 테스트.
- 제외: secure desktop kick 허용(캐시 desktop provenance 설계는 별도). 기존 `KickTryFill` 의 secure 차단 그대로. 따라서 15:15:57 형상(secure 중 kick 차단으로 실제 AU 가 0.5s 보유)은 **호스트에서는 미해결**이고 뷰어 보정(B)이 그 비용을 없앤다(뷰어 측 회귀로 고정).

## 2. (A) 호스트 epoch gate
- 태그: `CaptureState::inputEpoch`(atomic) 을 `EncoderState::ResetTimelineAnchors` 에서 +1. 호출처 21곳 전부 flush/restart/geometry-confirm/encoder-reset 경계이며 각 caller 가 `forceKeyNext=true` 를 세우는지 확인: backend 116/167/194/256, geometry 132/161, selection 135/177/248/308, stats 165/306, watchdogs 114/214, stream_active 160, loop_helpers 396(window-select), encoder_manager 64/75/136, au.cpp 193(stale reset), startup 401 — 각각 forceKeyNext 인접 설정 여부를 스크립트로 검사해 결과를 history 에 기록. forceKey 를 세우지 않는 caller 가 있으면 gate 가 스스로 `forceKeyNext=true` 를 세운다(아래).
- 인코더 FIFO: 입력마다 `set_next_input_epoch` → AU `inputEpoch`(synthetic·timestamp 와 lockstep, initialize/shutdown 에서 reset, 64 overflow 시 함께 pop).
- emit 단계 gate(`host_epoch_gate.hpp`, 순수):
  - 상태: `awaitingKey`(epoch 이 바뀌면 true), `sinceUs`, `droppedOld`, `droppedNonKey`, `droppedBytes`.
  - `au.inputEpoch < current` → **폐기**(old-epoch AU 는 key 여도 송출하지 않고, forceKey latch/barrier 를 만족시킨 것으로 계산하지 않는다). 보류 후 재송출 없음 → old-stamp→fresh burst 재발 불가, 이전 화면이 새 대상으로 표기될 수 없음.
  - `awaitingKey && au.inputEpoch == current && !key` → 폐기 + `forceKeyNext=true` 재요청(latch `forceKeySubmittedAtUs` 300ms 창은 그대로; 창 밖이면 다음 입력이 IDR).
  - `au.inputEpoch == current && key` → 송출, `awaitingKey=false`.
  - 상한: `awaitingKey` 가 `kEpochAwaitMaxUs`(700ms) 또는 `kEpochAwaitMaxDropped`(12 AU) 를 넘기면 verdict `ResetEncoder` → 기존 stale-reset 경로와 같은 codec shutdown/initialize + forceKey (IDR 지연·무시·FIFO 손상 대비 최후 수단, 로그 `epoch-gate encoder reset`). 연속 flush 는 epoch 만 계속 올리고 상태는 마지막 flush 기준.
  - 로그: `epoch-gate dropped-old auEpoch= curEpoch= auCaptureUs= holdUs=`, `epoch-gate dropped-nonkey … reforce=1`, `epoch-gate key-accepted curEpoch= waitedUs= dropped=`. `capture-switch first-frame` 에 `auEpoch curEpoch holdUs`, user-feedback/trace 에 `auEpoch curEpoch`.
  - seq 는 emit 시 부여하므로 폐기된 AU 는 seq 를 소비하지 않는다(뷰어 gap 없음). sender 는 old-epoch AU 를 받지 않으므로 `waitingForKey` barrier 는 새 epoch IDR 로만 열린다.
- 계약(수명): (i) 일반 데스크톱: 실제 stamp=callback qpc, 이 backend 의 보유 ≈ 다음 입력까지(kick 150ms 요청 시점), 뷰어 anchor 는 실제 stamp 만. (ii) flush(→epoch+1, caller forceKey): epoch 의 첫 송출 AU = IDR, 그 전 AU 는 폐기(카운트·로그), generation 표기는 첫 IDR 부터 새 값. (iii) 복귀 flush 도 동일 → 복귀 첫 실제 AU 의 stamp 는 복귀 후 캡처 시각.

## 3. (B) 뷰어 좁은 보정 — 재개 anchor 는 신선한 프레임만
- 관찰: 두 형상 모두 "침묵(>250ms) 뒤 첫 실제 프레임의 hold(`sendQpcUs−captureQpcUs`, 둘 다 호스트 stamp) 가 0.5~0.8s" 였고 그 프레임이 `presentAnchorFloorUs` 를 오래된 stamp 로 재고정했다. 정상 재개 프레임의 hold 는 수십 ms.
- 규칙(`FrameGate::admit`, Normal 상태·`recvGapUs>250ms`·실제 프레임에서만): hold > `kResumeAnchorMaxHoldUs`(300ms) 이면 그 프레임은 present 하되 floor 를 재고정하지 않고 `resumeAnchorPending=true`; 이어지는 실제 프레임 중 hold ≤ 300ms 인 첫 프레임이 floor 를 재고정(`recvGap` 무관)하고 pending 해제. pending 중 decode_queue 판정은 이전 floor 기준이 아니라 **보류**(pending 프레임 수 상한 8 또는 500ms 지나면 기존 규칙으로 복귀 → 진짜 backlog 감지 유지).
- 진짜 backlog 와의 구분: 실제 적체는 밀집 도착한 프레임 전부 hold 가 크다(호스트 큐 지연) → 신선한 프레임이 오지 않아 pending 이 상한에서 풀리고 기존 lag 판정이 발동. 정상 burst(S10)는 hold 작음 → 기존과 동일.
- 회귀: `viewer_frame_gate_test` — (a) 침묵 뒤 hold 800ms 프레임 → floor 미갱신, 다음 신선 프레임 → floor 갱신·Congested 0, (b) 밀집·모두 hold 큰 프레임(실제 backlog) → 상한 뒤 Congested 발동 유지, (c) S10 형상 불변. `viewer_udp_recovery_test` S13(두 형상, 뷰어 보정 후) → Congested 0·요청 0.

## 4. (C) 테스트·연결
- `host_encode_epoch_test`(실 MFT): [1] epoch·synthetic·timestamp 가 accepted-input→AU 로 일치(+overflow 64 초과 시 함께 pop 확인은 fabricated FIFO 로), [2] A→flush→B 재현: 비동기면 old-epoch AU 가 flush 뒤에 나옴을 **먼저 재현**(backend 이름·hold 출력) → gate 적용 시 첫 송출 = 새 epoch IDR, old 폐기 카운트 일치, [2b] IDR 지연(현재 epoch P 선행)→폐기+재요청, 연속 flush(epoch 2회) → 두 epoch 의 old 전부 폐기, 상한 초과 → ResetEncoder verdict, [3] 보유 패턴 측정(kick 유무, backend 명시), [4] 기존 secure 차단 유지(kick 정책 변경 없음 — 테스트 제거).
- 연결 테스트: `viewer_udp_recovery_test` 의 FakeHost 가 실 H264Encoder + **제품 `host_epoch_gate.hpp` 정책**을 그대로 통과시켜 송출(`FakeHost::Flush()` = epoch+1, forceKey) → S14 "flush 뒤 첫 와이어 AU 는 새 epoch IDR, old-epoch AU 0, 뷰어 Congested 0·요청 0·gap 없음". S13 은 pre-fix 형상(가짜 호스트 임의 생성)에 대한 **뷰어 보정** 회귀.
- 실기(사용자): UAC 2회에서 호스트 `epoch-gate dropped-old` 1~2건·`capture-switch first-frame … auEpoch==curEpoch holdUs<200ms`, 뷰어 `[congestion]` 0. RDP Active 면 제외.

## 5. 위험
- gate 폐기로 flush 직후 1~2 프레임(≤ 수십 ms)이 비는 것은 기존 IDR 전환과 동일 수준. ResetEncoder 는 700ms/12AU 상한에서만.
- 뷰어 보정은 Normal 상태·침묵 뒤·hold 기준으로 좁게 한정; 상한(8 프레임/500ms)으로 진짜 backlog 판정이 늦어도 0.5s 내 복귀.
- 15:15:57 형상의 호스트 측(secure 중 보유) 은 미해결로 남김 — 별도 설계 항목(ledger).
