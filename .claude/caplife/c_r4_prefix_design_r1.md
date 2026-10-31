# apk-decoder-inbox C r4 — 유효 prefix·최신성 설계와 원형 (2026-10-30, 정본 codex_apk_inbox_prefix_design_go_r1.md)

## 0. 고정
- branch `exp/apk-inbox-prefix`, **cf3c944**(2b492ef 위, 5파일 +623/−4)입니다.
- 2b492ef(C r3), APK, B/C 원문은 그대로 두었습니다.
- diff·SUMS·시험 원문 (`caplife/`):

| 파일 | 내용 | sha256 앞자리 |
|---|---|---|
| `c_r4_prefix.diff` | 2b492ef..cf3c944 | `9df15340…` |
| `c_r4_prefix_SHA256SUMS.txt` | — | `75829706…` |
| `c_r4_prefix_test_output.txt` | 시험 원문 | `77fbc772…` |

- **제품 default는 그대로입니다.** 새 동작은 런타임 스위치 `AndroidVideoDecoderSink::SetPrefixPolicyForTest(true)`로만 켜고, 제품 경로에서 켜는 곳은 없습니다.
- 하나의 바이너리와 하나의 정의로 두 정책을 비교합니다(inline 정의를 섞지 않음).
- AVD, 실기, 출하, ABR/wire 변경은 0입니다.

## 1. 문제 (원문과 소스)
- C의 887 = 전체 폐기 77회 × 9(693) + tail 2 + gap 187 + trim 5입니다(Codex 계수).
- 소스상 원인은 다음 순서입니다.
  1. worker는 codec 호출 중(`mu_` 해제) inbox를 비우지 못합니다.
  2. 그동안 9번째가 들어오면 inbox에 IDR이 없어 **case 3**이 됩니다.
  3. case 3은 **9개 전부 손실**로 버립니다.
- 그런데 이 큐는 대개 codec이 이미 가진 참조에 바로 이어지는 유효한 연속입니다.
- 또 버퍼가 없을 때 worker가 계속 Pop하면 1칸 pending slot이 다음 delta에 밀려 버립니다. 그러면 보존해도 소용이 없습니다.

## 2. 결정표 (스위치 ON일 때; OFF는 2b492ef 그대로)
| 상태 \ 사건 | 동작 | 손실/회계 |
|---|---|---|
| overflow, inbox에 IDR이 있고 IDR+suffix가 들어감 | case 1 그대로(가장 이른 IDR 이전은 skip) | 손실 아님 |
| overflow, IDR이 있으나 suffix가 안 들어감 | case 2 그대로(마지막 IDR부터, 최신부터 자름) | tail 손실, gap |
| overflow, IDR 없음, **prefix 증명 성립** | **case 3′: prefix 유지, 최신(도착분)부터 잘라 맞춤** | `tailLost`(=손실, inboxDrops에 포함), gap open, episode가 평소처럼 요청. **IDR commit이나 gate 지불이 아님** |
| overflow, IDR 없음, 증명 불성립(연속 끊김·frontier 불일치·chain 무효·head 나이 초과·세대 혼합) | case 3 그대로(전부 손실) | `prefixRefused` 1 |
| Pop 시 head delta의 나이 > 예산 | 그 delta를 버리고 그 seq에서 손실 처리합니다(`NoteChainLoss`, MarkLoss가 다음 IDR까지 trim). 한 장만 건너뛰지 않습니다 | `ageExpPop` |
| slot에 delta가 있고 나이 > 예산 | slot을 비우고 그 seq에서 손실 처리합니다 | `ageExpSlot` |
| slot에 frame이 있음 | **Pop하지 않음**(backpressure). wait predicate도 깨우지 않고 3 ms pump가 재시도합니다 | `backpressure` |
| 새 IDR 도착 | case 1이 우선합니다(prefix 위에 IDR을 유지) | — |
| reset·새 surface·새 pick·binding·stream reset·discontinuity | chain을 무효화합니다. 새 codec은 IDR 전 delta를 받지 않습니다(기존 codecAwaitsKey) | 기존과 같음 |

## 3. prefix 유효성 증명 (sink가 유지하는 술어)
case 3′는 아래가 **모두** 참일 때만 성립합니다.
- `chainValid_`: IDR이 CommitQueue된 순간 true가 됩니다. 다음 경우 false가 됩니다: decoder가 본 모든 손실(`NoteChainLossLocked`: slot 폐기, IDR 전 delta, gap 등), codec reset 요청(discontinuity 포함), inbox clear, invalidation(pick, binding, abort, 새 surface).
- `head.seq == frontier + 1`: frontier는 worker가 공급하려고 꺼낸 가장 최신 AU의 seq입니다(Pop 시, `mu_` 아래).
- 큐 안의 모든 AU가 바로 앞 AU의 다음 seq입니다. host가 AU마다 `hdr.seq = ++encoder.encodedSeq`(`host_stage_encode_send_h264_au.cpp:320`)를 매기므로, 구멍은 곧 sink에 오지 않은 AU입니다. encoder reset 때 seq는 0부터 다시 시작하는데, 그러면 연속이 끊겨 case 3으로 갑니다.
- 같은 generation입니다.
- head의 나이가 예산 이하입니다.

frontier frame이 나중에 codec에 못 들어가면 그 경로가 `NoteChainLoss`를 부르고, 기존 N1-a의 MarkLoss가 이미 유지한 prefix를 다음 IDR까지 잘라냅니다. 그래서 **늦은 무효화가 안전**합니다.

## 4. 최신성 (나이)
- **예산**: `kInboxMaxFrames(8) / target fps`를 [250 ms, 500 ms]로 자릅니다.
  - 30 fps → **267 ms**, 20 fps → 400 ms, 16 fps 이하 → 500 ms.
  - inbox 전체가 대표하는 시간보다 오래 기다린 frame은 그 inbox로 얻을 가치가 없다는 근거입니다.
  - 하한 250 ms는 고 fps의 짧은 지터로 유효 prefix를 버리지 않게 합니다. 상한 500 ms는 저 fps에서 화면이 그 이상 뒤처지지 않게 합니다.
  - session당 target fps가 정해지면 값 하나로 고정됩니다. 기준선 age가 없으므로 이 값의 성능 효과는 주장하지 않습니다.
- **잰 것**: local residence(이 sink가 받은 순간 `enqueueUs` → codec admission)입니다. `enqueueUs`는 이제 항상 기록합니다(이전에는 trace 빌드만).
- **재지 않는 것**: host capture 기준의 remote span, 실제 표시 latency입니다. local age를 제한했다고 end-to-end 지연을 보장한다고 하지 않습니다. host QPC와 Android clock은 빼지 않습니다.
- **검사 지점 셋**: overflow(case 3′ 판정), Pop, slot 재시도.
  - IDR은 만료되지 않습니다(chain 시작점).
  - codec API에 이미 넘긴 frame도 만료 대상이 아닙니다.
- **유한 시간**: held delta 뒤에 IDR이 있어도 그 IDR은 최대 예산만큼 기다립니다(held delta가 만료되면 비워짐). reset과 surface는 worker 외곽 루프가 먼저 처리합니다.

## 5. episode·원인·회계
- case 3′의 tail 손실은 평소 손실과 같은 경로입니다(`NoteInboxOffer` → episode). **요청을 미루지 않고**, 예산이나 limiter도 바꾸지 않습니다.
- 보존 자체는 commit이 아닙니다. episode는 손실 지점 뒤 IDR의 실제 commit으로만 닫힙니다.
- N1–N3, N2 Merge, §8(a)의 8회 상한/covered-exhausted, terminal/detach는 바꾸지 않았습니다. 스위치 OFF 회귀 14개로 확인했습니다.
- **counter 정의**:
  - `inboxDrops`(inbox lost)에 `tailLost`가 포함됩니다. `prefixKept`/`prefixKeptFrames`는 손실이 아닙니다.
  - `ageExpPop`과 `ageExpSlot`은 sink에서 버린 frame입니다. Pop 뒤라 `inboxDrops`에는 들지 않습니다. 단, 이어서 MarkLoss가 trim한 것은 `inboxDrops`에 들어갑니다.
  - frame ID 추적 없이 in+drop의 보존식은 주장하지 않습니다.

## 6. 같은 schedule 비교 (`android_decoder_inbox_prefix_test`, 제품 sink + stand-in NDK, 21/21, 반복 20/20)
**oracle**은 inbox를 모르고 codec이 받은 입력만 봅니다. 각 시험 AU payload의 NAL type과 seq를 읽습니다.
- IDR은 decodable입니다.
- delta는 같은 codec에서 IDR 이후 직전 decodable AU의 바로 다음일 때만 decodable입니다.
- 그 밖에 codec에 들어간 delta는 corrupt입니다.

| schedule | 현재 C | 추천(case 3′+backpressure+age) |
|---|---|---|
| P0 무손실 20 delta | 20 decodable, corrupt 0, episode 0 | 같음 |
| P1 codec이 API 안에서 바쁠 때 burst 13 → 회복 | **decodable 7**, corrupt 0, 12 손실 | **decodable 15**, corrupt 0, prefix 8 유지, tail 1 |
| P2 input buffer 없음 150 ms(< 예산) | 7, corrupt 0 | 11, corrupt 0, backpressure 38, passes 47(현재 48) |
| P3 buffer 없음 700 ms(> 예산) | 7, corrupt 0 | 6, corrupt 0. 예산을 넘긴 AU 13개 중 공급 0 |
| P4 prefix 유지 중 IDR | — | IDR 공급, corrupt 0 |
| P5 prefix 유지 중 reset | — | 새 codec에 IDR 전 delta 0, corrupt 0 |
| P6 / P6b 큐 안 / frontier 직후 구멍 | case 3, corrupt 0 | 증명 불성립 → case 3, corrupt 0 |
| P7 지속 과부하 5회 → 회복 | inbox ≤ 8, asks ≤ 4, 최신 AU로 복귀, 7 | 같은 경계, 15 |

**음성 대조** (조건 하나씩 제거, `neg\pm_*`):

| mutant | 실패하는 대조 |
|---|---|
| 연속 검사 없음 | P6 prefix가 corrupt를 공급 |
| frontier 검사 없음 | P6b corrupt |
| 나이 없음 | P3에서 예산을 넘긴 AU 공급 |
| backpressure 없음 | P2 |
| chain 무효화 없음 | 이번 schedule로는 **보이지 않습니다**(codecAwaitsKey와 MarkLoss가 같은 경우를 막습니다. 따로 증명하지 않음) |

**stand-in 함정**: fake codec이 그림을 하나도 내지 않으면 sink의 bootstrap replay(첫 그림 전 선택 IDR 재공급)가 오래된 IDR을 다시 넣습니다. 그 결과 현재 C run에서 corrupt 1이 두 번 관측됐습니다. 그래서 시험은 첫 IDR 뒤 그림 하나를 내게 했고, 이후 `bootstrapReplay=0`, 20/20 PASS입니다. 실기에서도 첫 그림 전 긴 정체가 있으면 같은 replay가 오래된 IDR을 끼울 수 있습니다. 이번 범위 밖의 **관찰**이며 확정하지 않았습니다.

**스위치 OFF 회귀** (각 1회, exit 0):

| 시험 | 결과 |
|---|---|
| chain | 51 |
| r2 contrast | 2 |
| merge | 8 |
| inbox | 12 |
| inbox_c | 44 |
| session | 26 |
| owed | 32 |
| geometry | 10 |
| binding | 17 |
| decode_stats | 42 |
| liveness | 68 |
| session_liveness | 9 |
| pending_slot | 21 |
| idr_recovery | 49 |

## 7. 한계와 채택 전 필요한 것
- **stand-in 결과입니다.** decodable 7→15는 이 schedule의 입력 수이며 실제 fps나 화면 품질이 아닙니다. pair의 −9.3 %를 다시 재지 않았습니다. B의 높은 out을 정답으로 쓰지 않았습니다.
- **증명은 sink 술어에 의존합니다.** chain 무효화 경로를 빠뜨리면 보존이 잘못 일어날 수 있는데, 그 mutant는 이번 대조로 드러나지 않았습니다. 무효화 지점을 늘리는 쪽으로 보수적으로 두었습니다.
- **나이 예산의 근거는 설계상의 것입니다**(inbox 용량 / fps). 기준선 age 측정이 없습니다. 실기 채택 전에 trace의 ageQueue/agePop으로 잽니다.
- **실제 MediaCodec이 가진 참조**를 sink가 직접 알 수는 없습니다. frontier와 commit 경로로 추론합니다. codec 내부 오류(출력 없음·에러)로 참조를 잃는 경우는 기존 reset 경로에 맡깁니다.
- **실제 채택에는 다음이 필요합니다**: 이 설계의 Source 확인, 고정 NDK/ABI·flag 경계의 제품 반영, 같은 공급 조건의 출력·age·요청·자원 측정.
