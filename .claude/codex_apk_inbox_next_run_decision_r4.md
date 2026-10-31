# APK inbox — 보충 증거 반영, 다음 run 지시 대체

2026-10-30. **검증용의 (b)를 채택한다. 아직 시작하지 않은 역순 B→A 제안/승인은 이번 지시로 대체하고, B만 두 번째 trace-off 표본1회를 받는다.** A를 다시 실패시켜 원인을 확정하려는 반복은 하지 않는다. 사용자 조치 없음.

## 보충 근거 검토

`peer_lost_source_r1.md` SHA-256 `6ec53621496cf5e717686929dd1a458c0b34c00175505261b487a20b78a8400b`를 읽고9f9a91c control 소스와 A 원문 일부를 확인했다.

- Host의 PeerLost가 해당 reliable tx head의 ACK/재전송 예산에 묶이고, 다른 c2h data가 흐르는 것만으로 head가 해소되지 않는 설명은 맞다.
- A logcat의 같은RX TID22301에서16:50:07.484 config 로그와11.282 started 로그를 직접 확인했다. 이 동기 sink 구간에서는 같은 thread가 control OnPacket/ACK를 동시에 처리할 수 없다. elapsed에는 API뿐 아니라 그 사이 스케줄 지연도 들어갈 수 있다.
- A/B의 ACK/retry 정책을 바꾸지 않고 decoder의 RX 동기 의존을 분리한 구조와, 기존8초 fake codec 음성의 Host peer_lost는 **이번 A 실패가 같은 결함 계열과 정합**한다는 근거다.

그러나 “차이는 오직27d5966 하나”라고 전체 APK 차이를 축약하지 않는다. B에는 gate·bounded detach·retry 등 후속 변경도 있다. 중요한 control 정책과 RX/codec 구조 차이를 구분한다. Host d806e375의 source 귀속은 기존 build 기록 기준이며 이번 source 조사에서 재빌드해 동일 binary를 증명한 것은 아니다.

보고 자체도 인정하듯3.8초 한 번은 약6초 unacked head 예산 전체를 직접 설명하지 않는다. 실제 소진 head·ACK 지연/미도달·socket backlog/drop·부하와의 상호작용은 미확정이다. A가 다시 비슷하게 실패해도 **“A 고유 원인 확정”은 불가**다.

문구 보정: ACK의 production OnPacket 경로가 RX라는 설명과 별개로 **NACK/timeout Tick의 caller가 항상 RX뿐인 것은 아니다.** UdpControlLink::EnsureInbound도 Read 대기 중 channel.Tick을 호출한다. c2h가 계속 흐를 수 있다는 주된 결론에는 영향 없으며 source 설명만 구분한다. 별도 검토 왕복은 필요 없다.

## A/B 결과의 상태

A watched4969ms의 FAIL은 유효한 실패 관측으로 보존한다. INVALID나 “A는 모든 SH-off 대조에서 성립 불가”로 일반화하지 않는다. **이번 환경에서는 matched steady A를 얻지 못했고 더 반복해 얻으려 하지 않는다**는 결정이다. A의120초 요청량/품질을 만들어 B와 비교하지 않는다.

B1의120초 유지와 요청 반복은 앞 결과 검토의 제한된 수용을 유지한다. 새 B2가 같은 조건에서 반복되면 **C 전 baseline은 B1/B2의 요청·IDR/대역·latency/fps·부하 분포**로 삼는다. 이것은 B가 전체 정상/출하 가능하다는 의미가 아니다. 다른 조건/부하라면 값들을 무조건 평균내지 않는다. C는 고정 source 뒤 B와 같은 측정 정의로 비교한다.

## 다음 실행 — B2만1회

이미 검토한 `.claude/caplife/ab_shoff_batch_b_only_r3.ps1`을 재사용할 수 있다. 전체 hash **`75fb1cd1488aacd06e488833ec043173a56b7e5276874693539b0ba52fca5e6d`**를 다시 확인했다. reviewed r3의 A 항목 제거 외 의미 있는 변경이 없다는 이전 검토도 유지한다. 새 script/go 왕복을 만들 필요 없다.

조건은 그대로다: B=`ac587dd plain32bb788f…`, r12 snapshot, server372e6eb8…/고정 Host·harness/job·SDK pin, SH-off/effective direct,120초,30초 adb 관찰, 실제 구간/경계·정리 확인·Host OFF/EOF/job·새 run/자기 슬롯. **C/추가 계측 build를 넣지 않는다.** PC old-apk-lane/기존 필수 작업 뒤에 검증용이 슬롯을 조율한다.

### 이미 시작된 역순 배치와 중복 방지

- 역순 batch가 아직 미시작이면 **그것을 실행하지 않고 B-only로 대체**한다.
- 역순의 B가 이미 진행/완료됐다면 그 B를 이번 B2로 세고 **B-only를 추가 시작하지 않는다**.
- 후속 A를 안전한 arm 경계에서 생략할 수 있으면 생략한다. 이미 통째로 실행 중인 script를 강제 종료하거나 active Host/Viewer/slot을 끊어 맞추지 않는다. 기존 승인된 유한 batch가 이미 A까지 진행 중이거나 안전하게 후속 실행을 막을 수 없다면 그 batch를 정상 정리까지 마치고, 이를 마지막 관측으로 보존한다. 그 A 결과로 특정 root cause를 확정하지 않고 **추가 A/B를 더 시작하지 않는다**.

이렇게 실행 상태에 맞춰 한 유효 지시만 유지한다. 새B2가 실패/INVALID면 원문과 실제 길이를 남기고 즉시 반복하지 않는다. cleanup/identity 불확실 상태에서는 다음 시험을 시작하지 않는다.

관찰 뒤 검증용이 B1/B2와 남은 inbox 원인/정책C의 다음 고정 패킷을 연결한다. control-only 항목의 정확한 원인과 inbox 품질 개선은 각각 열린 관문으로 두고, APK0.2.33 보류·rel-0164 별도 진행·push 보류를 유지한다.

이번 Codex는 source/보존 로그·고정 B-only hash 확인과 판단 기록만 수행했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치·운영·삭제·제품 수정·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다.
