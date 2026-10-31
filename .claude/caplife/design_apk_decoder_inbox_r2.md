# apk-decoder-inbox — 설계 r2 (r1을 대체; Codex codex_apk_inbox_plan_src_r1.md 결정 ②③ 반영, C prototype의 계약)

작성: 2026-10-30. 상태: **계약 고정, 구현 전**. C는 A/B 측정과 fixed SOURCE 뒤에 구현하며, 그 전에는 게시하지 않습니다. 이번 회차에 제품 정책 변경은 0입니다. 바뀌지 않는 것(상한, limiter, wire, owed 8, codec 생성 21회, 2 s detach, N1 예약·KeyDelivered)은 r1과 같습니다.

## 1. 순서와 usable IDR
- **AU 순서**: header의 `seq`(uint32, Host가 frame마다 1씩 올림)와 `streamGeneration` 기준입니다. 같은 generation 안에서 seq가 앞선 것을 먼저로 봅니다.
  - **wrap**: 차이를 int32로 계산합니다(`(int32_t)(b - a) > 0`이면 b가 뒤). 2^31 frame 안에서만 비교합니다.
  - **중복**(같은 gen, 같은 seq): 뒤에 온 것을 버리고 셉니다.
  - **재정렬**: 손실 지점보다 seq가 **앞선** AU는 늦게 도착한 옛 AU입니다. 회복 후보도 아니고 suffix도 아니며, 버리고 셉니다.
  - 조립기(UDP assembler)가 순서를 보장하는 범위는 C 구현 전에 source(`native_video_client_shared_core.cpp` 조립·`droppedPreviousIncomplete`)로 확인해 이 절에 인용합니다. 전달 시각만으로 판단하지 않습니다.
- **usable IDR**: 현재 binding, gate epoch, stream generation이 맞는 IDR이어야 합니다. 그리고 **AU 순서상 손실 지점 뒤의 참조 시작점**이고, 그 뒤 suffix가 seq 연속이며, IDR+suffix가 상한 안에 들어가야 합니다.
- **선택**: 그 조건을 만족하는 **가장 이른** IDR입니다(수용됨). queue·content age와 복귀 지연을 측정하며, 오래된 chain을 유지해 latency를 숨기지 않습니다. 같은 예산 안에서 더 최신 IDR을 고르는 안은 측정 뒤 비교할 수 있습니다.

## 2. tail gap과 recovery episode는 별개
| 상태 | 열림 | 닫힘 | 비고 |
|---|---|---|---|
| **ingress gap** (tail) | inbox overflow로 chain이 끊길 때 | 끊긴 지점 **뒤** seq의 usable IDR이 enqueue될 때 | 닫히면 뒤따르는 delta를 받기 시작합니다. 회복을 뜻하지는 않습니다 |
| **recovery episode** (id, 원인, 최초 요청 시각, retry 수, 후보 IDR) | 첫 손실(원인별)이 생길 때, 열린 episode가 없을 때만 | 그 episode의 IDR이 **실제 codec queue에 commit**될 때(CommitQueue 성공) | 후보 IDR이 queue나 in-flight에서 다시 버려지면 **같은 episode를 재개**합니다(새 id·새 예산 없음) |
| 무효화 | 새 pick / Bind / Unbind / stream reset / Abort | 즉시 | 그 episode의 미전달 요청도 무효(4절) |

## 3. 요청 예산과 시각
- episode당 **새 논리 key request는 최대 4개**입니다(최초 1 + retry 3). UDP 재전송 packet 수가 아닙니다.
- **이상적 송신 시각**은 t0, t0+1 s, t0+3 s, t0+7 s입니다. 이전 시도가 실제로 전달 교환을 시작한 시각에서 1 / 2 / 4 s 뒤입니다.
- **retry를 세지 않는 것**: limiter 거부와 이미 pending인 같은 요청에 합쳐진 것은 새 송신이 아닙니다. 거부 때 tight loop를 돌지 않습니다. 같은 IDR이 도착하거나 버려져도 retry 수를 리셋하지 않습니다.
- **보류**: usable IDR을 실제로 보존 중이면(inbox, pending, in-flight) 요청하지 않습니다. 그 후보가 사라지면 이전 예산과 기한으로 재개합니다.
- **selection owed(8)와의 관계**:
  - 같은 현재 IDR 의무(같은 binding·selection·generation)는 하나의 논리 교환으로 합칩니다. 4+8을 따로 얹지 않습니다.
  - owed의 기존 보장(8회)은 그대로입니다. 4회는 recovery 자체의 예산이지 모든 원인의 합계가 아닙니다.
  - 서로 다른 원인(codec_await_key / codec_up / selection_abort / pipeline)의 요구는 지우지 않습니다.
- **마지막 대기와 실패**: 4번째 요청 뒤에도 IDR이 commit되지 않으면 `recovery_exhausted` 유한 상태가 됩니다. 기록하고 영상 미회복으로 보고하며, 명시 재시도 경로(새 pick, 새 surface)만 엽니다. 무한 black을 정상 준비로 표시하지 않습니다. 자동 reconnect·takeover로 숨기지 않고, codec을 강제로 중단하지 않습니다.

## 4. D8 — held 요청의 descriptor와 유효성 hook
- 요청마다 descriptor를 만듭니다: `{binding token, selection tag, gate epoch, 원인, episode id, sink 세대 token}`.
- session은 descriptor를 **held(limiter 거부)와 accepted pending/action에 함께 보관**합니다.
- **검사 시점은 두 곳**입니다.
  - held를 limiter에 다시 넣기 직전
  - accepted pending/action을 **실제 control 송신 교환으로 시작하기 직전**
- 두 시점 모두 sink의 `KeyRequestStillValid(descriptor)`로 묻습니다.
  - 유효 조건: binding·selection·epoch가 같고, 그 원인의 episode가 아직 열려 있고 commit 전입니다. owed는 gate 토큰이 같아야 합니다.
  - 무효면 그 descriptor만 버립니다. 다른 원인이나 다른 episode의 요구는 지우지 않습니다.
  - "지금 열린 episode id 하나"만 조회해 옛 요청을 새 episode에 붙이는 방식이나, TTL만 쓰는 방식은 쓰지 않습니다.
- **이미 시작한 교환**은 completion 규칙(응답·시간초과)으로 끝냅니다. 무효가 된 old token은 새 시도나 재시도를 만들지 않습니다.
- sink의 state lock을 network I/O 동안 잡지 않습니다(조회만 짧게).
- 다른 sink 소비 target은 default 구현(항상 유효)으로 호환시킵니다. park·owner·session 무효화 대조를 포함합니다. wire 변경은 없습니다.

## 5. C 검증 (구현 뒤)
- 새 계약에 맞춘 행동 대조와 이전 source 음성으로 검증합니다. 기준선 `android_decoder_inbox_test` [1]–[5]의 예상 FAIL은 진단 기준선일 뿐 회귀 PASS가 아닙니다.
- 추가할 대조:
  - 재정렬된 옛 AU
  - 중복 seq
  - wrap
  - 후보 IDR이 다시 버려질 때 같은 episode로 재개되는지
  - 4회 시각(t0/1/3/7)과 exhausted
  - owed와 합쳐짐 / 다른 원인 보존
  - held·accepted 두 시점의 descriptor 무효화

## r3 보충 (Codex codex_apk_inbox_plan_src_r2.md; C Source에 남길 것, A/B의 blocker 아님)
- **마지막 응답 대기 window**: 4번째 요청을 보낸 직후 바로 exhausted로 가지 않습니다. 4번째 요청의 전달 교환이 시작된 시각부터 **응답 대기 window**(제안: 이전 간격과 같은 4 s, 즉 t0+11 s까지)에 그 episode의 IDR이 도착해 commit되면 회복입니다. window가 끝나야 `recovery_exhausted`가 됩니다. window 길이는 C Source에서 상수로 고정합니다.
- **합친 descriptor는 원인을 잃지 않습니다**: owed와 inbox recovery처럼 같은 의무를 하나의 논리 요청으로 합치더라도 descriptor에 **모든 유효 원인 집합**을 담습니다. 한 원인이 무효가 되어도 남은 유효 원인이 있으면 요청은 유효합니다.
- **송신 직전 validation**: held를 다시 넣을 때와 accepted pending/action을 송신 교환으로 시작할 때 모두 검사합니다(r2와 같음).
- **in-flight 정합**: 이미 송신 교환이 시작된 요청은 그 completion 규칙으로만 끝납니다. 그 사이 episode가 commit되거나 무효가 되면, 완료 뒤 새 시도를 만들지 않습니다(옛 token은 재시도 근거가 되지 않음).
- **Android owner 취소**: binding이 바뀌면 그 binding의 descriptor는 모두 무효이고, sink는 새 owner의 요청만 유효로 답합니다. park, 다른 PC로 전환, session 종료가 여기에 해당합니다(keepalive-multi binding token과 같은 기준).

## C r1 구현 메모 (2026-10-30, feat/apk-inbox-c 0832e37 / 53e3fdb / 5fe90a9; 정본 패킷 `caplife/c_source_packet_r1.md`)
- **§1 조립기 순서 인용**:
  - `native_video_client_shared_core.cpp` PopDelivery(:906-915)는 seq상 **가장 오래된** assembly부터 내보냅니다.
  - :1058은 마지막으로 전달한 AU보다 새롭지 않은 packet을 무시합니다.
  - 그래서 sink에는 seq가 증가하는 순서로만 도착합니다. sink의 중복·재정렬 검사는 **방어용**입니다.
- **r3를 넘어 정한 것 세 가지** (Codex 판정 대상):
  1. overflow 때 IDR이 하나도 없으면 inbox 전체를 버립니다(손실 하나로 셈). IDR이 있으면 결정표의 ① 또는 ②를 따릅니다.
  2. 순서 drop이 30번 연속이면 순서를 다시 잡는 안전판을 둡니다(셉니다).
  3. selection이 IDR을 빚지는 동안(gate KeyLost / Rearm, 세션 8 × 1 s) episode는 자기 요청을 더하지 않습니다.
     - 그 결과, owed 8회가 IDR 없이 끝나면 아무도 요청하지 않습니다(주기 IDR이나 명시 경로를 기다림).
     - **"owed 소진 뒤의 요청 주체"는 결정 항목**으로 남깁니다.
- **상수**: 최종 응답 window `kFinalWindowUs` 4 s, 나가지 않은 want의 재발행 `kReissueUs` 4 s(attempt 아님), 안전판 `kOrderResyncAfter` 30.