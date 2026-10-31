# APK decoder inbox / ke-r26 관측에 대한 결정

2026-10-30. 대상 run은 10-29의 ke-r26 두 건이며 제품 고정은 ac587dd다. **APK 0.2.33 출하 보류 유지. inbox overflow/keyframe 반복은 회귀 후보로 추적하고, 아래의 한정 계측·대역 보수·정규화 비교를 진행한다.** PC 0.2.164에는 이 branch가 없으므로 관련 없는 PC 릴리스 작업을 멈추지 않는다.

입력: `report_decoder_liveness_r1.md` 「ke-r26 결과」 SHA-256 `ae0b3e8ac10f9674a4c457075250583e4b37fb423028b89aced593926d692af5`, `ke_r26_summary.txt` `951a65fa606fa06010e9d81bbbe0e2866d0b071b99aea858eaab990fa74d93fe`. 고정 source의 inbox/worker/요청 limiter·control 통계와 보존 slot/fixture 일부 원문을 확인했다.

## 현재 관측의 판정

- 기본 SH run은 요청 weak에서 `no_output_after_ready` 뒤 direct로 fallback했다. **SH-on 렌더 근거가 아니다.** 첫 영상 13.7초·이후60초 종료 미관측이라는 제한된 관측을 보존한다.
- SH-off run은 Settings off/direct·sharpen_start0, 첫 영상5.138초·이후60초 종료 미관측을 보존한다. 이 두 성공만으로 r21 B/r25 control-only peer_lost 원인을 닫지 않는다.
- 두 bundle은 실제 **rc3/fixture1**이다. 검토한 b/c 원문은 capture 대상 보고 없음으로 FAIL하면서 input/power 최종 end/EOF/OFF 검사는 통과했다. 이는 E2-only 계획과 추가 Host의 capture assertion이 맞지 않은 시험 결함이며, raw rc를0으로 바꾸거나 전체 bundle PASS로 선언하지 않는다. A의 유효한 부분 관측은 별도 유지한다.
- inboxDrops/gapDrops 증가와 Host가 받은 reason2 요청의 반복은 유의미하다. 다만 과거5초/14초와 이번117초/119초의 총수를 나눠 **동일 조건 회귀 배율로 확정하지 않는다.** startup/실제60초 watch/목록 정리 구간과 실제 active 시간을 나눠 비교한다. 요약상 watch 중 Host 요청은 각각60/67개이며 전체179/174개와 별개다.

**Source/trace 의미 보정:**

1. `kq`는 일부 `KeyframeRequestState::Request(...).queued` 성공 계수다. 실제 송신/Host 수신 총수가 아니다. pipeline requestKeyframe callback 및 held 요청 재시도 등 모든 경로를 같은 방식으로 세지도 않는다. reason2라는 wire 값만으로 inbox가 모든 요청의 원인이라고 확정할 수 없다.
2. `fragmentRetransmits`는 SendFragments의 `only != nullptr`일 때 증가한다. timeout head 재전송은 Tick에서 SendFragments(head,nullptr)를 부르므로 **retx0이어도 head attempts6/10은 가능**하다. pend는 rxPending 쪽이고 txq와 다르다. “제어 재전송/대기0”이라는 요약으로 확대하지 않는다.
3. worker pass age는 outer loop의 snapshot 시각이다. 내부 while가 계속 처리하는 동안에도 증가할 수 있어 **2.7초 pass age = 2.7초 동안 worker 정지**라고 하지 않는다. codecCall age도500ms 표본에서 본 진행 중 구간이며 전체 호출 지연 분포가 아니다. RX dmax425/634ms 역시 lock wait/실행/스케줄 지연을 아직 분해하지 않은 elapsed 값이다.
4. relay sendErr0은 해당 socket send callback 오류가 없었다는 근거이며 목적지 수신·ACK 채택/무손실 증거는 아니다. SDK client를 썼고 listener가 LDPlayer pid25608로 안정됐다는 사실도 서로 구분한다. 원인으로 단정해 그 프로그램을 종료하지 않는다.

## ① inbox/overflow 정책의 보수 방향

**무한 버퍼·RX/control backpressure로 되돌리지 않는다.** 상한8 frame/4MiB는 이번 측정의 출발점이며 신성한 최적값은 아니다. 하지만 cap만 키워 시험을 통과시키는 안은 먼저 채택하지 않는다. 유한 메모리·최대 queue age/화면 지연과 실제 서비스 처리량을 함께 보고 조정한다. 4MiB를 넘는 유효 IDR 거부의 지원 영향도 별도 유지한다.

H.264 압축 AU에서 임의의 delta를 버린 뒤 다음 delta가 계속 유효하다고 간주하면 안 된다. 기존 code도 gap 뒤 IDR까지 delta를 거른다. 단 **모든 overflow에 전역 bool을 세우고 매번 새 key 요청을 만드는 현재 방식**이 이미 queue에 들어온 usable IDR/후속 연속 구간을 활용하지 못하거나 중복 요구를 만들 수 있다.

같은 Android sink 범위에서 다음 구조의 prototype/회귀를 허용한다.

- owner/selection/stream/codec 세대와 AU 순서를 가진 bounded queue로 유효한 참조 연속 구간을 구분한다. 압축 AU의 sequence와 제어 transport의 message seq는 다르다.
- queue에 **손실 지점 뒤의 유효한 IDR과 그 후 연속 suffix**가 이미 있고 예산에 맞으면, 그 이전의 obsolete prefix를 한 번에 버리고 그 chain으로 회복한다. 요청하지 않아도 될 새 IDR을 무조건 요구하지 않는다. 오래된/oversized/stale IDR은 회복 후보로 세지 않는다.
- usable IDR가 없으면 깨진 chain의 delta를 정상 입력으로 넘기지 않는다. gap/recovery episode를 만들고 유효 IDR을 기다린다. 단순히 newest delta 하나만 보존하는 방식은 채택하지 않는다.
- 보호 중 IDR/chain 뒤에 새 손실이 생겼다면 gap의 **순서/범위**를 유지한다. “IDR가 하나 있다”는 이유로 그 뒤에서 누락된 참조까지 복구됐다고 하지 않는다. tail을 버리는 정책을 택하면 그 이후 delta의 의존성까지 추적해야 하며, 전역 bool 하나를 이른 IDR에서 지워 뒤의 gap을 숨기면 안 된다.
- 미송신 inbox frame의 정리와 이미 시작한 codec queue reservation을 구분한다. N1의 첫 실제 queue IDR binding, dequeue/queue 중 취소·owner fence·KeyDelivered의 실제 queue 성공 조건을 유지한다.

서비스 처리량 자체가 공급보다 낮은 상태에서는 어떤 작은 FIFO도 손실 없이 계속 버틸 수 없다. 가능하면 **참조 AU는 decode하고 오래된 출력의 presentation만 생략**하는 방향을 검토하되(codec 참조는 유지), 이것이 실제 병목을 줄이는지 대역으로 확인한다. 압축 입력을 버리는 것과 출력 render=false는 다르다. 기존 출력/SH handoff·ABR의 계수 의미를 바꾸지 않도록 영향 범위를 기록한다.

필요하면 기존 runtime fps/bitrate·decode telemetry를 활용한 적응을 비교한다. 이를 위해 wire 확장·Host 재시작·새 전역 timeout을 도입하거나 사용자 설정을 몰래 바꾸지는 않는다. 큰 playout/ABR 정책 변경이면 그 실제 확대만 같은 task의 설계 근거로 연결한다.

## ② key 요청 — 원인별 recovery episode로 합침

**기존 전역 limiter를 느리게 만드는 것만으로 해결하지 않는다.** 현재 min interval120ms/token refill300ms/capacity3은 다른 초기 선택·transport 회복에도 쓰인다. 이를 임의로 조여 startup을 늦추거나 request를 영구 유실시키면 안 된다.

- selection의 owed IDR, inbox chain gap, pending input slot 폐기, codec 시작/실패, pipeline discontinuity/손실을 최소한 local reason/counter로 구분한다. wire reason은 그대로 둬도 된다.
- 같은 현재 recovery episode에 대해 delta마다 새 요구를 만들지 않고 합친다. 요청 접수/실제 송신/Host 도달/IDR 도착/실제 codec queue/회복을 구분한다. 이미 queue/in-flight에 유효한 회복 IDR이 있는 경우와 아직 없는 경우도 다르다.
- episode의 최초 요구와 필요한 bounded retry/backoff를 보존한다. 한 번 보낸 packet이 유실됐다는 이유로 영원히 요청하지 않는 안은 안 된다. 반대로 뒤따르는 delta마다 retry budget을 새로 시작하지 않는다.
- 실제 IDR queue 후 완료된 episode의 오래된 미송신 요구는 취소/시효 처리하고, 새 selection/다른 원인의 아직 유효한 요구를 함께 지우지 않는다. Consume flag를 false로 만드는 것만으로 session 쪽 held 요청까지 없어진다고 가정하지 않는다.
- 이번 SOURCE에서 닫은 codec 생성21회 예산,2초 detach·gate reservation·control 진행성은 그대로다. IDR 폭증을 감추기 위해 Host 통계/로그를 없애거나 transport/owed 예산을 늘리지 않는다.

## ③ 먼저 추가할 최소 계측/대역 대조

기존 test-build trace에만 낮은 빈도 snapshot으로 추가할 수 있다. RX의 동기 파일 I/O·긴 lock·payload 저장은 만들지 않는다.

- inbox high-water(실제 frame/bytes)·enqueue→worker/codec queue age, 처리 frame 수와 서비스 시간, state lock wait/hold, codec API 종류별 지연 및 output 대기/처리량을 구분한다. outer pass age만으로 처리 정지를 대신하지 않는다.
- drop 원인·AU/stream 세대·gap episode, usable IDR queue/pending/in-flight 유무, request의 local 원인·coalesced/accepted/실제 send/거부·회복 완료를 묶는다. 모든 origin을 같은 의미로 세고 label을 고친다.
- control timeout head 재전송을 별도 test counter로 세어 selective fragment retry와 구분한다. received/sent NACK·tx head/ACK/queue도 기존 필드 의미를 유지한다. PC0.2.164에는 이 진단/제품 branch를 넣지 않는다.

fake NDK/clock·실제 sink로 먼저 확인할 행동:

1. 정상 lossless steady 입력에서 startup 뒤 새 recovery 요청이 지속 발생하지 않음.
2. 단발 burst/worker pause로 gap을 만들었을 때 요청은 episode 단위로 합쳐지고 현재 IDR로 bounded 회복. 같은 gap의 delta 폭주로 요청 수/예산이 새로 늘지 않음.
3. queue 안 usable IDR+연속 suffix가 있는 경우의 회복, 없는 경우, IDR가 새로 손실되는 경우를 분리. codec에 원인불명 손상 delta를 feed하지 않음.
4. 지속 overload에서 frame/byte/age/요청량이 bounded이고 RX/control은 codec를 기다리지 않음. 오래된 frame을 무한 적재해 latency만 키우지 않음.
5. owner/선택/Surface 취소·queue reservation·4MiB 초과·codec 실패/terminal의 기존 대조 보존. 전체 예전 suite를 이유 없이 여러 번 반복하지 않는다.

## ④ 동일 시간 비교/부하 정규화

**먼저 SH-off로 9f9a91c baseline(A)와 ac587dd(B)의 matched 비교를 준비한다.** SH default는 두 run 모두 fallback 여부가 달라질 수 있어 throughput 비교의 최초 축으로 적합하지 않다. 새 overflow prototype(C)는 대역/고정 SOURCE 뒤 B와 비교한다. 먼저 A/B로 확인하기 전부터 회귀 배율을 수치로 고정하지 않는다.

- baseline은 9f9a91c 제품과 transitive gate/pending/session 등을 함께 고정한다. 후속 worker/정책을 부분 backport해 baseline이라 부르지 않는다. 같은 최소 trace를 넣을 필요가 있으면 **trace-only diff**와 처리/전송 정책 무변경을 고정한다. 둘 다 같은 debug/fixture endpoint·기본 flags/SDK/ABI, 같은 Host/server/relay/path·resolution/fps/bitrate/target 동작을 쓴다.
- arm마다 같은 초기화/설정 절차 후 **첫 영상부터120초**를 목표로 관찰한다. startup/120초 steady/목록 종료를 별도 구간으로 남긴다. 조기 종료하면 그 실패와 실제 관찰 길이를 적고120초를 채웠다고 환산하지 않는다. 실패를 채우려고 즉시 반복하지 않는다.
- 요청은 각 구간의 Host 실제 seq unique 수, app 실제 send, IDR frame 수·bytes 비중, relay/video payload bytes/s·pps와 함께 본다. kq 하나로 대역 증가량을 계산하지 않는다. output fps·frame gap/stall·queue age/first frame·control retry/close와 메모리도 함께 측정한다.
- 같은 boot/config/GPU backend/AVD vCPU·online CPU 수·RAM·해상도·network를 고정하고, 시작 전 유휴 부하와 측정 중 qemu/app CPU·loadavg/CPU 수·memory·긴 dispatch/GC를 같은 간격/길이로 기록한다. SwiftShader를 임의로 host GPU로 바꾸지 않는다.
- 측정 동안 자체 build/다른 시험을 겹치지 않도록 검증용이 조율한다. 사용자 앱/LDPlayer/다른 adb를 종료하거나 인위적인 CPU hog를 만들지 않는다. **새 제품 때문에 측정 중 부하가 올라간 경우는 결과일 수 있으므로 환경 탓으로 제외하지 않는다.** 시작 전 외부 부하와 처리 중 변화가 다르다.
- A/B 결과의 해석이 실행 순서나 큰 외부 부하 변화에 달리면 역순 B/A를 한정 추가한다. 처음부터 전체 suite/모든 SH arm을 반복하지 않는다. 시험 자원의 정상 재시작은 기존 유휴/독점·동일 userdata 허용 범위며 새 boot는 별도 환경 arm이다. 재시작 후 성공을 제품 보수로 쓰지 않는다.

새 baseline/trace·fixture plan의 고정 source/script/빌드 매핑을 먼저 묶어 검토하고 위 한정 A/B를 실행한다. 기존 슬롯은 검증용이 조율하며 **rel-0164 old-apk S/P 등 이미 진행 중인 독립 필수 lane을 중복 점유하지 않는다.** 같은 허용 범위에서 사용자 자원 승인을 다시 묻지 않는다.

## ⑤ E2-only fixture를 계획에 맞춤

다음 비교의 b/c capture 실패를 없애기 위해 **E2-only에서는 필요한 a Host만 시작**하고 E1의 예상 목록/ready와 검사 대상을 그 계획에 맞추는 보수를 우선한다. 또는 명시 no-client 관찰 target의 capture assertion만 해당 없음으로 분리할 수 있으나 final OFF/end/EOF/job·예상 밖 child 실패는 절대 건너뛰지 않는다.

fresh readiness·a 실패→AVD0·정리 residual0·다른 Host 미실행의 기기 없는 orchestration 대조를 남긴다. E2/full 계획을 분리해 full의 capture 실패를 전역 무시하지 않는다. 10-29 두 raw bundle은 FAIL로 보존하고 새로운 plan-aware 정상 실행과 구분한다. fixture를 바꾸면 A와 B에 **같은 새 fixture**를 사용하며 r26과 환경이 같다고 주장하지 않는다.

## 완료 기준/다음 담당

검증용은 같은 작업용1 지시서에 **원인별 계측 + plan-aware fixture + bounded recovery prototype/대역 + matched A/B**를 한 묶음으로 연결한다. cap/전역 limiter부터 임의 조정하는 것은 승인하지 않았다. 제품 코드의 실제 수정은 작업용, 고정 Source 뒤 독립 실행은 검증용이다.

출하 판정에는 실 Android에서 불필요한 recovery 반복/IDR·대역/latency 회귀가 해소되거나 근거 있는 수용 조건이 있어야 한다. 순수 시험 통과·60초 종료0만으로 닫지 않는다. 실제 SH-on 렌더, terminal UI/명시 재시도,4MiB 입력 지원 영향과 기존 control-only 종료 항목도 각 범위의 미검증으로 유지한다.

이번 Codex는 보고·고정 source·보존 raw 일부를 읽고 판정만 했다. 제품 수정/build·실제 Host/AVD/adb·자원 재시작·OS 입력·설치·운영·삭제·commit/push는 없다. 미추적 검토 기록만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
