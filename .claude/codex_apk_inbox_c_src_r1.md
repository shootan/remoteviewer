# APK decoder inbox C r1 — SOURCE NEEDS_CHANGES / §8 결정

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 고정과 판정

대상은 `C:/work/gnlink-wt/apk-inbox-c`, `79b5c25..5fe90a93985e2ac3cac922764d523de7d043920e`다. 제품 commit은 0832e37·53e3fdb이고, 53e3fdb..5fe90a9는 시험 2파일만 바뀐다. tracked clean과 diff --check를 직접 확인했다.

패킷 `c_source_packet_r1.md` SHA-256 `62dd01e9c7bc80f55f9b9270882027d46fb0165213fafdfb58a1238c334fca9c`, diff `3b69c148d6e7e1fcbe9a95ffaab460e3548e6518839e764d4a3fa0e699e94b00`, SUMS `e3811dd356f45946bbf193bf506e8c8566006f889e32695ef44fdc8740184222`와 일치한다.

**SOURCE NEEDS_CHANGES.** 아래 N1~N3와 §8 상태 정합을 같은 C task에서 보수한다. C Android 실행은 보수본 SOURCE 확인 뒤로 둔다. 독립 빌드 준비·순수 회귀 보수는 계속할 수 있고 PC old-apk lane의 슬롯 우선순위는 유지한다. APK 0.2.33 출하 승인이나 PC 0.2.164 판정은 아니다.

## N1 · P1 — 큐 내부와 pending/in-flight의 chain 손실·복구 상태가 일치하지 않음

관련: `decoder_chain_inbox.hpp:165` MarkLoss, decoder.cpp:165 NoteChainLossLocked, :452~511 ProcessFrameLocked, :1433 실제 IDR CommitQueue 뒤 처리.

### N1-a: 이미 큐에 들어간 손실 뒤 delta를 계속 Pop한다

MarkLoss는 inbox 안에 후속 IDR이 하나라도 있으면 즉시 반환하고, 없으면 gapOpen만 올린다. 어느 경우에도 이미 들어간 손실 뒤 delta prefix를 정리하지 않는다. Pop은 앞 frame을 그대로 반환하며, ProcessFrameLocked에는 이전 worker gap 차단을 대신할 dequeue 검사가 없다. pending slot이 frame을 버리거나 codec이 참조를 잃은 시점에도 이 함수가 쓰인다.

고정 제품 헤더의 직접 순수 대조: `delta12, delta13, IDR14`가 큐에 있을 때 `MarkLoss(11)` 후 첫 Pop은 **delta12, key=0, gap=0**이다. 회복 IDR14보다 앞의 끊긴 suffix를 전달한다. 이것은 실제 codec 손상 영상을 관찰한 결과가 아니라, 제품 queue 정책의 반례다. 현재 45/45에 이 경계가 없다.

외부 손실을 반영할 때에는 손실 뒤부터 usable IDR 이전의 종속 delta를 전달하지 않아야 한다. 현재 gate/stream에서 유효한 IDR과 그 suffix는 보존한다. IDR 존재 여부만 보고 앞의 delta를 유효하게 취급하면 안 된다. IDR이 없는 경우와 있는 경우를 모두 제품 sink의 pending/codec 입력 경계로 시험한다.

### N1-b: pending IDR이 실제 복구해도 tail gap이 남아 정상 delta를 버림 — 제품 sink 재현

제품 sink·fake NDK에서 다음 순서를 직접 실행했다.

1. IDR1이 codec input에 들어간다.
2. input buffer를 막고 delta2를 pending으로 둔다.
3. IDR3이 delta2를 대체한다. NoteChainLoss(2)는 tail gap을 열지만 현재 pending/in-flight인 IDR3은 inbox 안에 없다.
4. buffer를 열면 IDR3은 **CommitQueue 성공**, episode2도 closed가 된다.
5. 정상 delta4를 넣으면 **GapDelta로 폐기**, tailGap=1이고 episode3을 새로 연다. 실제 fake codec 입력 tag는 **1, 3**뿐이다.

이것은 회복 직후 불필요한 재요청·영상 공백을 만드는 경로다. IDR commit과 ingress gap 상태를 같은 loss/stream/epoch 기준으로 정합시켜야 한다. 무조건 gap을 내리는 보수도 안 된다. 그 IDR보다 뒤에서 이미 생긴 새로운 tail 손실은 남아야 한다. 다음 대조를 한 묶음으로 둔다: pending IDR이 손실을 덮고 commit한 뒤 다음 delta가 queue됨 / commit보다 뒤의 손실은 유지 / IDR 자체 재폐기·reset·owner 변경 때 잘못 gap을 지우지 않음.

## N2 · P1 — limiter pending/action과 descriptor의 분리로 D8 취소 우회

관련: session.cpp:1009~1011, :1022~1030 NextAction, :1078~1092 accepted ledger take, :1465~1479 Note/ClearKeyLedger, :1518~1520 RX publish.

`Request()`가 pending을 공개하는 시점과 NoteKeyRequest가 descriptor를 붙이는 시점이 다르다. scheduler의 pending consume과 keyAccepted_ take도 다르다. 그 사이 RequestWindowSelect/RequestMonitorSelect가 ClearKeyLedger를 부르면, 이미 꺼낸 decoder action이 **descriptor 없는 legacy 요청**으로 전송된다. RX Request → Note 사이의 선점도 같은 문제가 있다.

실제 ClientSessionController + loopback fake Host로 재현했다. 기존 S2와 달리 **NextAction 뒤, keyAccepted_ take 직전**에 scheduling hook만 추가한 사본을 썼다. 제품 취소·검사·송신 로직은 바꾸지 않았다. 그 지점에서 sink owner를 71→72로 바꾸고, public RequestWindowSelect를 호출했다. 결과는 `hook=1 newPick=1 staleDropped=0 hostKeyRequests=322`다. 322는 기준 시각부터 Host 수신까지의 ms이며, **요청 322개라는 뜻이 아니다**. 무효인 이전 entry의 요청이 1회 나갔다. 기존 accepted hook은 ledger를 가져온 뒤여서 이 반례를 가리지 못한다.

보수 조건:

- pending의 공개·coalesce, 원인 descriptor, action으로의 인계와 취소가 일관된 request identity/epoch 아래에서 움직여야 한다. descriptor를 나중에 붙이거나, 취소로 사라진 decoder action을 legacy로 해석하면 안 된다. 진짜 legacy/pipeline 원인은 명시적으로 보존한다.
- held snapshot을 검사하는 동안 새 held가 들어오면, 뒤의 Clear()/pending=false가 그 새 요청을 지우지 않아야 한다(session.cpp:965~996). version/identity를 비교해 자기 snapshot만 반납하거나 이에 준하는 동기화가 필요하다. 이 항목은 소스에서 확인한 경합이며 이번 실험으로 별도 재현했다고 주장하지 않는다.
- sink query나 network exchange를 긴 ledger lock 안에 넣어 RX/control 정지를 되살리지 않는다.
- 새 pick/owner 변경을 action dequeue 전후·descriptor take 전후에 걸고 stale wire 0을 확인한다. RX publish 도중의 선점과 held 검사 도중 새로운 유효 원인 추가도 대조한다. 유효 원인 일부가 남으면 그 원인을 지우면 안 된다.

## N3 · P2 — 30회 중복/역순 뒤 무조건 순서 재설정은 범위 밖

`decoder_chain_inbox.hpp:93~109`의 kOrderResyncAfter 안전판은 명시 reset 없이 같은 generation의 오래된 delta를 허용한다. 직접 대조에서 seq100을 처리한 뒤 seq99 delta를 31번 넣으면 **31번째가 Admitted, orderResyncs=1**이었다. 정상 assembler에서는 도달하지 않을 방어 경로라고 패킷이 설명하지만, 승인한 dedup/reorder 계약을 약화시키는 근거가 되지는 않는다.

이번 C에서는 이 자동 재설정을 제거하고 명시 stream/selection/binding reset에 맡긴다. 재시작 seq를 지원해야 한다면 별도 식별된 reset과 새 IDR에서 시작해야 하며, 오래된 delta를 임의로 참조 chain에 넣지 않는다. 31회 이상 중복·역순, 정상 증가, wrap, 명시 reset 뒤 새 IDR 대조를 남긴다.

## §8 결정 — (a), owed 소진 뒤 episode의 새 4회 예산을 붙이지 않음

owed가 담당하는 동일 IDR 의무에 대해 **기존 owed의 최대 8회 limiter 채택 상한을 유지**한다. 소진 뒤 recovery episode가 새 t0/1/3/7을 자동 시작하는 (b)는 채택하지 않는다. 새 pick/reset이 없는 같은 의무를 loss·candidate 재폐기만으로 재무장하지 않는다. pipeline 등 독립 원인은 자신의 원인으로 보존하며, 이를 episode의 우회 재시도로 이용하지 않는다.

다만 현재처럼 sink가 이를 모르고 `recovery=open, asks=0`에 영구히 머무는 상태는 보수한다. session이 owner/sink epoch/owed token에 연결된 **covered budget 소진**을 sink에 전달하고, 이미 받아 둔 마지막 요청의 송신/취소가 정리된 뒤 최종 응답 window 4초와 후보 보존 상태를 반영해 covered/exhausted를 명시한다. 오래된 token의 소진 통지는 새 선택에 영향을 주면 안 된다. 이 상태 변화가 gate의 owed를 거짓으로 지불하거나 새 recovery 예산을 만들면 안 된다.

후속 요청 주체는 **자동 episode가 아니라 Host의 늦은/주기 IDR 또는 사용자가 명시한 새 선택·재연결 등 기존 경로**다. exhausted는 늦은 유효 IDR을 거부하는 상태가 아니다. usable IDR을 보존 중이면 그것의 실제 commit 또는 재폐기를 보고 상태를 정한다. 늦은 commit은 정상 영상으로 회복하고 상태를 닫아야 한다.

주의: 현재 owedAttempts_는 `Request().queued` 때 증가하므로 **실제 wire 송신 8회**라고 기록하지 않는다. accepted/merged/sent/dropped와 covered/exhausted의 근거를 구분한다. N2 보수 후에도 limiter/transport 정책을 임의로 바꾸지 않는다. owed 소진→추가 episode 요청0 / 마지막 응답 window / 늦은 IDR 회복 / 이전 token 소진 통지 / 명시 새 선택 / 유효 pipeline 원인 유지의 대조를 포함한다. terminal UX와 명시 retry의 실제 사용자 행동은 아직 미검증이며 이 결정으로 출하 관문을 닫지 않는다.

## 독립 debug APK와 실행 계획

**검증용의 깨끗한 제품 tree 독립 빌드 hash로 offSha를 고정하는 방식은 허용**한다. 작업용 APK의 byte hash 재현을 독립 검증의 필수 조건으로 강제하지 않는다. 이번 53e3fdb 제품 source와 5fe90a9의 시험 전용 차이는 직접 확인했다. 보수 후에는 그 승인 제품 commit에 맞춰 다시 고정한다.

hash만 바꾸는 것으로 source 동등성을 주장하지는 않는다. commit/clean 상태, build 명령·toolchain·BuildConfig/endpoint, debug/release, signer, ABI, trace/seam/auto-prepare 속성, APK hash와 실제 설치 대상을 함께 기록한다. 배치의 변경은 경로·해당 hash·그 고정 기록으로 제한하고 실행 전에 새 snapshot hash를 남긴다. 전체 SOURCE 재심을 위한 별도 설계 왕복은 필요 없다.

직접 ZIP 열람: B plain-r5 `32bb788f…`와 C plain `94966459…`는 각각 20,509,196 / 20,710,732 bytes, entry 157개로 추가/삭제 entry는 없다. 주요 크기 차이는 4 ABI의 native library와 classes3.dex다. 이것은 구조 비교이고 ELF/DEX의 의미 동일성이나 debug 재현 불가 원인을 증명한 것은 아니다.

패킷의 “검토를 받지 않은 B-only 배치”는 기록을 정정한다. `75fb1cd1…` B-only는 앞서 직접 diff 검토·한정 허용한 배치였다. C batch가 그 범위에서 파생한 점은 인정한다. SOURCE 보수 뒤의 독립 120초 SH-off 계획, B1/B2 한정 baseline, 원인별 request 및 같은 구간 fps·연결 지속·age 측정은 유지한다. 서로 정의가 다른 inboxDrops/gapDrops를 직접 비율 비교하지 않는다. peakPre=0은 미계측으로 표시한다.

## 직접 확인의 범위와 다음 담당

Codex 실행은 고정 blob의 순수 queue와 제품 sink + fake NDK, 제품 session + loopback fake Host뿐이다. 실제 GNLinkStream/Viewer/AVD/adb/GPU·사용자 입력·설치·게시·push는 0이다. review probe의 설정 단계 컴파일 실패는 검사 성공 근거에서 제외했고, 최종 build/실행 rc0 원문을 보존했다. session probe는 기존 common.lib를 링크했으므로 전 의존성 독립 재빌드라고 주장하지 않는다. 수정한 제품 사본은 scheduling hook 1개만 추가했으며 worker worktree에는 손대지 않았다.

근거는 `.claude/codex_decoder_liveness_review/`의 `c_review_probe.py`, `c_session_probe.py`, `c_5fe90_review/`에 있다. 최종 `probe.log` SHA-256 `a84d67aae54a7216116d047c191ac44008e9c813a6be5bd6ff77f04d23600795`, `session_probe.log` `2dea0ec2c32cff9f3c64bc1dcf8a7d9110622d557809f86b25dfe38250ec34e4`; 소스·exe hash는 같은 폴더 SHA256SUMS.json이다. 종료코드0은 **반례 관찰 프로그램이 정상 실행됐다는 뜻**이고 제품 계약 PASS가 아니다.

검증용이 기존 작업용1의 한 C 지시서에 N1~N3·§8을 합쳐 보수와 관련 음성 대조를 잇는다. 고정 보수본 SOURCE → 검증용 독립 실행 → 한정 Android 120초 순서다. 제품·tracked 문서 변경이 없는 전문 검토이므로 제품 commit은 만들지 않았다.
