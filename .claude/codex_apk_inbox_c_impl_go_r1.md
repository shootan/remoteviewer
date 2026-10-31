# APK inbox — B 기준선 수용 / C 구현 GO

2026-10-30. **B1·B2를 C 이전의 관측 기준선으로 수용한다. 추가 A/B 실행을 구현 시작 조건으로 요구하지 않고, 설계 r3의 C prototype 구현·자체 대조를 허용한다.** 실제 C SOURCE/독립 EVIDENCE·APK0.2.33 출하는 별도 관문이다.

## 결과와 계획 정합

최신 정본 참조는 아래 두 파일로 확정한다. 추가 정정 절을 확인했고 기존 기준선 수용/C 구현 GO 및 추가 A/B0 결정은 유지한다.

- 결과 `result_ab_shoff_r4_reverse.md`: `93b98e470257f8858311aa417245daa5abf2a88e6f8ecd77c9795e00f834e993`.
- source 조사 `peer_lost_source_r1.md`: `c289f4ae927f0b2479e2f3148ce2f7242147363cc28603c3f3fa07a38653d9e7`.

source 조사의 이전 본문은 말미 정정의 범위로 읽는다. Tick은 worker Read 대기에서도 실행되고 ACK의 production OnPacket 경로는 RX다. A/B 전체 차이는 후속 수정 묶음이며27d5966 하나로 원인을 분리한 실험이라고 하지 않는다. 이미 승인돼 시작된 batch의 A2를 보존하는 계획 정합 판단도 그대로다.

현재 읽은 `result_ab_shoff_r4_reverse.md`는 계획 정합 보충 후 SHA-256 **`93b98e470257f8858311aa417245daa5abf2a88e6f8ecd77c9795e00f834e993`**다(인계 f37133d4 이후 갱신). 다음6개 원본 hash를 직접 대조했다.

- B2 `ab-b-shoff-20261030021750-10004`: fixture06bbec02… /result e264179f… /Host00c959dc… 일치.
- A2 `ab-a-shoff-20261030022255-36552`: fixture eb991766… /result d9035dd6… /Host521cb701… 일치.

slot/Host 원문에서 양쪽 OFF2줄·fixture0·job residual0/forced0/ended_confirmed1을 확인했다. B2는 AVD0·8/8/18 NOT RUN, A2는 AVD1·5/6/19 NOT RUN이다. Host peer_lost는 **B2 0/A2 1**로 직접 집계했고 B2 전체 keyframe-request273개도 확인했다.

역순 batch는 대체 지시 전에 시작/완료된 것으로 보고됐고, 기존 지시도 이미 시작된 유한 batch를 강제 종료하지 않고 B를 B2로 세도록 허용했다. 따라서 **B-only를 더 만들지 않은 처리가 맞다.** A2는 앞서 승인된 batch의 보존 관측이며 이를 임의 추가 실행/권한 위반으로 다시 분류할 필요 없다. 앞으로의 유효 지시는 추가 A/B0이다.

## 기준선의 의미

| 표본 | 유효 관측 |
|---|---|
| B1 |watched120398ms, 연결 유지. watch SEGMENT123.007초의 요청229개/1.862회·s |
| B2 |watched120746ms, 연결 유지. watch SEGMENT122.675초의 요청163개/1.329회·s |
| A1/A2 |같은 boot/환경 계열에서 watched4969/3049ms의 peer_lost 기능 FAIL2/2 |

B1/B2는 **연결 지속과 반복 key 요청이 공존하는 C 전 baseline**이다. 완전히 정상인 제품의 기준이나 출시 OK가 아니다. N=2·같은 boot의 관측 범위이며 두 값을 무조건 평균해 보편적 요청률로 만들지 않는다.

A의 두 실패는 동기 RX/codec 처리 구조와의 연관성을 강화하고 기존 fake NDK 음성 메커니즘과 정합한다. 소진된 head/ACK 지연·socket drop과 부하의 기여까지 확정한 것은 아니다. A가 SH-off에서 원천적으로 비교 불가인 것으로 일반화하거나 특정 APK 단독 root cause로 닫지 않는다. 더 실패시키기 위한 A3은 요구하지 않는다.

watch 마지막 in/out값 B1 1750/1747, B2 2089/2087 정정은 수용한다. 이 값도 세션 누적 계수이므로 **watch fps는 같은 구간 시작/끝의 차이와 실제 시각으로 계산**한다. in≈out만으로 inbox 손실0이나 실제 표시 fps·latency가 정상이었다고 하지 않는다.

두 B의 h2c UDP rate가 약44.25/44.07KB·s로 비슷하므로 요청 반복이 곧 큰 대역 증폭이었다고 확정하지 않는다. 작은 IDR 환경에서의 **불필요한 회복 반복·참조 chain 손실과 품질/지연 영향**이 C의 목표다. 요청 감소만으로 bandwidth 절감을 주장하지 않는다.

## C 시작 조건 — 지금 충족, 구현을 잇는다

별도 design-only 왕복이나 정상 A120초 재측정을 요구하지 않는다. 검증용은 기존 작업용1의 한 유효 지시서에 이 결정을 합쳐 같은 task의 C 단계를 배정한다. 정책 변경0 계측 기준79b5c25와 설계 r3를 기준으로 scope를 고정하며, 필요하면 별도 branch/worktree로 B 산출물을 보존한다. 기존 A/B APK·원문·snapshot을 덮지 않는다.

C 범위:

- bounded chain-aware inbox(8 frame/4MiB), 현재 owner/gate/stream·AU 순서/중복/재정렬/wrap에 맞는 usable IDR+suffix.
- ingress gap과 실제 queue commit까지의 recovery episode 분리, 후보 IDR 재폐기 때 같은 episode·예산 유지.
- 최초1+추가3개 논리 요청, backoff1/2/4초와 마지막4초 응답/commit window의 명시 상태. 이미 후보 IDR을 보존 중인 상태/소진/늦은 결과·명시 retry를 구분한다.
- 동일 IDR 의무의 owed와 recovery coalesce·원인 집합 보존, descriptor의 held/accepted 송신 시작 유효성·owner/park/session 취소·in-flight completion 경계.
- N1 예약/실제 queue에서만 KeyDelivered, 생성21회/2초 detach·RX/control의 codec 대기0 계약 유지.

wire/limiter/transport·보안·자동 재탈취·사용자 설정·상한을 임의로 바꾸지 않는다. presentation 생략/ABR까지 확대해야 한다면 그 실제 변경 범위를 source packet에 명시한다. 그런 tradeoff를 inbox 숫자 조정 속에 숨기지 않는다. PC0.2.164에는 이 branch를 합치지 않는다.

## C 제출과 검증 조건

### 1. 작업용의 고정 SOURCE + 의미 있는 자체 대조

기존 expected-red inbox 기준선을 C에서 통과시키되 구현 모양에 맞춰 false positive 기준만 낮추지 않는다. 실제 sink/session을 쓰는 다음 경계를 한 묶음으로 제출한다.

- lossless steady의 불필요한 recovery 요구0, 단발 gap의 episode 단위 bounded 회복, 기존 queue의 usable IDR/suffix 활용.
- IDR 재손실·뒤 gap/seq 불연속·재정렬/중복/wrap, bytes/frame/age bound·4MiB 초과.
- held뿐 아니라 **accepted/dequeued action의 취소 경합**, 여러 원인 coalesce 후 일부만 무효화, 실제 Android binding adapter의 owner/park/session 변경.
- t0/1/3/7·마지막 대기·exhausted/명시 retry, codec 실패/느린 lifecycle·queue 중 변경. 다른 유효 요청을 지우거나 stale token을 새 episode에 붙이지 않음.
- 지속 overload에서도 control/ACK 진행·메모리/요청량이 bounded. **요청을 막고 영상을 멈춰 PASS하는 대조는 금지**하며 서비스 가능한 조건에서는 현재 프레임으로 회복해야 한다.

새 핵심 경계는 이전 source/관련 mutant로 실패하는지 확인한다. 이미 영향 없는 전체 suite를 여러 번 반복하지 않는다. shared API 변경이면 소비 target compile과 필요한 회귀를 한 번 확인한다. NDK4ABI 및 plain/trace·release 시험 경계도 고정한다.

### 2. Source 뒤 검증용 독립 Android 관측

C 구현 전 추가 A/B는 종료했다. **고정 C SOURCE를 통과한 뒤** 검증용이 C의 독립 build·동일 SH-off/동일 fixture/대상·관찰120초·동일 metric 정의로 실제 동작을 확인한다. 구체 run/hash 계획은 그 SOURCE 패킷에 묶는다. 새 기능 후보의 실기 실행 승인을 지금 미리 부여한 것은 아니다.

성능 판단은 plain/trace를 분리한다. C의 기본 처리와 함께 항상 켜지는 진단 비용이 B1/B2와 다르면 diff에 기록하며 CPU 변화 전체를 정책 하나의 인과로 단정하지 않는다. 그 차이가 판정에 중요할 때만 같은 정책의 진단 대칭 control을 한정 추가한다. 구현 전부터 새로운 대량 baseline 반복을 요구하지 않는다.

C 채택에는 **중복 회복 요구가 원인별로 줄었다는 근거 + 영상 갱신/first frame·fps/gap·실제 지연/control·메모리를 유지/개선한 근거**가 함께 필요하다. 대역만/요청 수만 내려간 것을 완료로 세지 않는다. B1/B2의 변동 범위·측정 부하와 경계 오차를 함께 보고, 차이가 불명확하면 그 불확실성만 한정 확인한다. cap 확대로 latency를 쌓거나 codec failure/exhausted를 숨겨 수치를 좋게 만들지 않는다.

실제 SH-on/terminal UX·명시 retry surface lifecycle·4MiB 지원 영향·control-only 원인의 미검증도 따로 유지한다. 기존 실패/불일치 기록을 삭제하거나 C 성과에 합쳐 덮지 않는다.

## 운영 범위

제품 구현/자체 대조는 작업용, 독립 실행은 검증용, 최종 확인은 Codex의 기존 역할을 유지한다. rel-0164 old-apk-lane 등 필수 작업의 슬롯을 중복 점유하지 않고 PC 작업을 C 때문에 멈추지 않는다. Host OFF/end/EOF/job·자기 repo run/슬롯·단일 시험 package/SDK pin·clipboard/사용자 프로그램 미조작·삭제 제한과 push 보류를 유지한다.

이번 Codex는 고정 결과 hash·로그·설계를 읽고 기준선/범위만 판정했다. 실제 Host/Viewer/AVD/adb·GPU/OS 입력·설치·운영·삭제·제품 수정·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
