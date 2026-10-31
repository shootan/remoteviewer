# apk-decoder-inbox r1 — 설계 요지 (구현 전, Codex codex_apk_decoder_inbox_decision_r1.md 기준)

작성: 2026-10-30. 대상: fix/apk-decoder-liveness `ac587dd` 위. **이번 회차에는 inbox 정책을 바꾸지 않습니다.** 이번 commit에 들어가는 것은 계측(시험 build 전용)과 기준선 대조뿐입니다. 아래 정책은 Codex SOURCE 판정과 A/B 측정을 거친 뒤 C(prototype)로 구현합니다. 아래 각 항목은 수용 또는 거부를 고를 수 있게 결정 단위로 적었습니다.

## 0. 현재 동작 (ac587dd, 사실)
- inbox는 FIFO이고 상한은 8 frame / 4 MiB(엄격)입니다. 넘치면 **가장 오래된 1개**를 버리고 다음을 합니다.
  - IDR이었으면 `KeyLost`를 부릅니다.
  - **전역** `inboxGap_ = true`, `decoderKeyframeRequest_ = true`로 둡니다.
- worker는 `inboxGap_`가 서 있는 동안 처리 시점에 delta를 하나씩 버립니다(`:391`). 이때 **delta마다** `decoderKeyframeRequest_ = true`로 둡니다.
  - 처음 만나는 IDR이 `inboxGap_`를 내립니다. 그 IDR이 손실 지점보다 앞이든 뒤든 상관없습니다.
- codec이 key를 기다리는 동안 들어오는 delta(`:415`)에서도 **delta마다** 요청 플래그를 세웁니다.
- 세션은 frame마다 `ConsumeDecoderKeyframeRequest`를 부르고 limiter(min 120 ms, refill 300 ms, cap 3)에 `Request(2)`를 넣습니다. 거부되면 `decoderRekeyPending_`로 두고 control loop에서 다시 시도합니다.
  - 그래서 gap 하나 동안 delta 수만큼 요청이 생깁니다. limiter가 그중 약 1/s를 통과시키고, 나머지는 거부되거나 held 상태가 됩니다(ke-r26의 kr 약 3/s, kq 약 1/s).
- frame header에는 AU 순번 `EncodedFrameHeader::seq`가 있습니다(poc_protocol.hpp:284). 제어 transport의 message seq와는 다른 값입니다.
- **가설(미확정)**: 요청 → IDR 증가 → IDR은 바이트가 커서 inbox 바이트 상한을 더 자주 침 → 다시 overflow로 이어지는 증폭 고리가 있을 수 있습니다. wire reason=2만으로는 원인을 확정할 수 없으며, A/B와 계측으로 확인합니다.

## 1. inbox: chain을 아는 bounded FIFO (결정 D1–D4)
- **D1 — 항목 정보**: 각 항목에 `binding`, `gate epoch`(Prepare/Reset/Ack가 바꾸는 값), `streamGeneration`, `seq`, `key`, `bytes`, `enqueueUs`를 둡니다.
  - 상한 8 frame / 4 MiB는 **그대로** 둡니다. 측정의 출발점이고, 이번 설계에서 상한 확대는 하지 않습니다.
- **D2 — overflow 처리**: 새 frame을 넣은 뒤 상한을 넘으면, 버릴 범위는 `[head .. 첫 usable IDR)`입니다.
  - **usable IDR**: 현재 binding과 gate epoch에 속하고, gate가 지금 허용하며, 단독으로 상한 이내인 IDR입니다.
  - **고르는 IDR**: "IDR + 그 뒤 suffix"가 상한 안에 들어가는 것 중 **가장 이른** IDR입니다. 가장 적게 잃고 계속 bounded이기 때문입니다.
  - 그런 IDR이 없으면 **queue 전체**를 버립니다. 남은 것은 모두 끊긴 chain이기 때문입니다. 그리고 **gap episode**를 엽니다.
  - 이것은 지금의 "남겨 뒀다가 처리 시점에 하나씩 버림"과 잃는 frame이 같습니다. 대신 더 일찍 버리므로 메모리가 줄고, codec에 끊긴 delta를 넘기지 않습니다.
- **D3 — gap 표시는 tail에 둡니다**: episode 표시는 **enqueue 쪽(tail)**에만 있습니다.
  - episode가 열려 있는 동안 들어오는 delta는 **enqueue 시점에 거절**하고 원인별로 셉니다.
  - episode는 **끊긴 지점 뒤에 enqueue된 IDR**만 닫습니다. 그래서 queue 안의 더 이른 IDR이 뒤의 gap을 지우는 일(현재의 전역 bool 문제)이 없습니다.
  - worker 쪽에서는 "다음 처리할 frame은 IDR이어야 함"을 queue 머리의 실제 내용(D2 뒤에는 늘 IDR 또는 연속 chain)으로 판단합니다. 별도 전역 플래그를 두지 않습니다.
- **D4 — 경계**:
  - inbox 정리는 이미 시작된 codec queue 예약(N1 `ReserveQueue`/`CommitQueue`)에 손대지 않습니다.
  - `KeyDelivered`는 계속 실제 codec queue 성공에서만 일어납니다.
  - Prepare/Bind/Abort/stream reset은 지금처럼 inbox를 비웁니다. 거기에 더해 열린 episode와 그 요청 상태도 지웁니다(D7).

## 2. key 요청: 원인별 recovery episode로 합침 (결정 D5–D8)
- **D5 — 원인**: 흩어진 `decoderKeyframeRequest_ = true` 7곳을 `RequestKeyLocked(cause)` 하나로 바꿉니다. 원인은 다음과 같습니다.
  - `inbox_gap`(D2 overflow), `oversize`(4 MiB 초과 거부), `pending_discard`(input slot의 held delta 폐기), `codec_await_key`(새 codec이 IDR 전에 받은 delta), `codec_up`(retry로 codec 생성), `selection_abort`
  - wire reason은 2 그대로 둡니다.
- **D6 — episode 하나당 요청 하나**:
  - episode가 열릴 때 요청을 1회 냅니다. 같은 episode 안에서 뒤따르는 delta는 요청을 새로 만들지 않고, retry 예산도 새로 시작하지 않습니다.
  - IDR이 **도착**(enqueue)하지 않은 채 시간이 지나면 bounded retry를 합니다: 1 s, 2 s, 4 s 뒤, 최대 3회. 숫자는 Codex 판정 대상입니다.
  - IDR이 도착했지만 아직 codec에 들어가지 않은 동안에는 요청하지 않습니다. 그 IDR이 나중에 버려지면 episode를 다시 엽니다.
  - episode의 **회복**은 그 IDR이 실제 codec에 queue되는 시점입니다. 도착·회복·요청 접수·거부를 따로 셉니다.
  - queue나 in-flight에 이미 usable IDR이 있으면 episode를 열지 않습니다(D2와 같은 판단).
- **D7 — 취소와 시효**: 새 selection·Bind·stream reset은 열린 episode와 미전달 요청을 지웁니다. selection의 owed IDR(gate `OwedToken`)은 **별도 경로로 그대로** 둡니다.
- **D8 — session에 남은 held 요청 (Codex에 묻는 점)**:
  - `ConsumeDecoderKeyframeRequest`가 false를 낸다고 해서, session이 이미 들고 있는 `decoderRekeyPending_`이 사라지지는 않습니다(Codex 지적).
  - **제안**: sink에 `KeyRecoveryEpisodeFor(token)` 조회를 하나 둡니다. 열린 episode의 id를 내고, 없으면 0을 냅니다.
  - session은 held 요청을 다시 보내기 직전에 이 값을 보고, episode가 닫혔거나 바뀌었으면 held를 버립니다. 최소 hook 하나만 추가하고, wire 변경은 없습니다.
  - 다른 방안(이 hook 없이 짧은 시효)도 같이 적어 두니 골라 주세요.

## 3. 바꾸지 않는 것 (명시)
- 상한 8 frame / 4 MiB, limiter 상수(120 ms / 300 ms / 3), wire와 reason 값, owed 8회 예산, codec 생성 21회 예산, 2 s detach, gate 예약 계약은 그대로입니다.
- 무한 버퍼나 RX/control backpressure(RX가 codec을 기다리던 구조)로 돌아가지 않습니다.
- 끊긴 chain의 delta를 decode에 넘기지 않습니다.

## 4. C 단계에서 비교할 선택지 (지금 구현하지 않음)
- **오래된 출력의 presentation 생략**: 참조 AU는 decode하고, 오래된 output만 `render=false`로 release합니다. 압축 입력 폐기와는 다른 방식이며, 출력·SH handoff·ABR 계수의 의미가 바뀌는 범위를 먼저 적습니다.
- **기존 runtime fps/bitrate 적응 활용**: wire 확장, Host 재시작, 새 전역 timeout, 사용자 설정 변경은 하지 않습니다.

## 5. 이번 회차 commit의 범위
- **계측**: GNLINK_LIVENESS_TRACE 시험 build 전용이고 출하물에는 0입니다.
  - 원인별 drop·요청 계수, inbox high-water(frame/bytes), enqueue→worker와 enqueue→codec queue의 age(최대 + 거친 histogram), pass당 처리 frame 수와 서비스 시간, RX 경로의 `mu_` wait/hold, codec API 종류별 지연, control의 timeout head 재전송 계수(fragment 재전송과 분리), session의 `Request(2)` 경로별 accepted/refused.
- **기준선 대조** `android_decoder_inbox_test`(새, fake NDK): 정상 / 단발 gap / queue 안 IDR+suffix / 지속 overload / 취소. 기준은 위 설계의 수용 조건이라 **현재 코드에서는 일부 FAIL이 예상**되며, 그 수치를 기준선으로 보고합니다.
EOF
sha256sum /d/remote/remote/.claude/caplife/design_apk_decoder_inbox_r1.md