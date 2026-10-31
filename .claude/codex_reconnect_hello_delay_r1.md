# reconnect-hello-delay r1 — Codex 설계·범위 판정

2026-10-22 / 조사 task **t-uu1zshde** / 보고 `.claude/report_reconnect_hello_delay_r1.md` / 기준판5063cd9, 조사 후보4db9f17.

**판정: 수정 착수에 필요한 원인 근거는 충분하다. ① PunchAny 송신 cadence 보장 + ② hello의 동일 라운드 내 bounded 수신 처리를 함께 채택하고, 별도 기능 커밋으로 PC0.2.162 통합 후보에 포함한다.** 구현 효과·실망 원인 전부·릴리스 성공이 확인됐다는 판정은 아니다. **큐 전체 사전 비우기와 ③ SIO_UDP_CONNRESET 변경은 이번 최소안에서 제외**한다. 인증·timeout·재시도 정책·punch 응답 예산은 유지한다.

## 근거 판정과 서술 한계

- 보고 전체, 관련 제품 source, 진단 scratch diff, 대표 base/diag 원본을 읽었다. 직접 diff한 rendezvous/hello/punch_reply/directory_client/host control·session/bootstrap/viewer UDP 경로/server 파일은5063cd9↔4db9f17 변경이 없었다. base `hello-reconnect-mgxmQQ` 원문에 hello8057ms/6411ms, diag `dxgi-wedge-eybk6A` 원문에 연속10054 즉시 오류(`waitMs=0`) 후 Punch303→HelloAck301·hello6102ms가 있다. 기준판 재현을 “diff 밖이므로 추정” 수준에서 **실제 같은 종류의 기존 결함 재현**으로 올릴 수 있다.
- `directory_rendezvous.cpp`의 즉시 recvfrom 오류→다음 송신 round, hello의 수신1개마다 재송신·50ms sleep 구조는 코드로 확인된다. Windows UDP10054가 이전 송신에 대한 ICMP Port Unreachable을 뜻하는 것은 [Microsoft recvfrom 문서](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-recvfrom)와 일치한다.
- 충분히 확인된 기제는 **오류/비ACK 수신이 송신 cadence를 무너뜨리고 ACK 소비에 항목당 지연을 누적한다**는 것이다. 호스트의 ACK 송신 성공 로그는 그 시각에 클라이언트 큐에 ACK가 도착했다는 wire 증거와는 다르다. “약0.3초 도착”은 양쪽 로그와 나중 소비에서 얻은 추론으로 명시한다. 실패한 recvfrom의 `from` 필드만으로 실제 ICMP 발신 주소를 확정하지 않는다. Punch25개의 개별 송신 경로 귀속도 보고대로 미확정이다.
- base4/14 대 cand1/14를 개선/회귀 통계로 해석하지 않는다. 진단 빌드는 로그 외에 recv→recvfrom 관측 변경도 있고, 스케줄 영향과 표본수가 있다. 실 LAN/NAT·회사 PC 전체 원인을 이미 증명한 것으로 확대하지 않는다. C1PKrA의10초 host 종료 연쇄를 설명하는 유력한 근거는 확보했다.

## 채택 계약

### 1. PunchAny — 송신 시계와 수신 결과를 분리

- monotonic **next-send 시각**을 기준으로 각 round를 최소 기존150ms 간격으로 보낸다. `recvfrom<=0`, zero-length datagram, 무관한 응답이 다음 round를 즉시 발생시키지 않아야 한다. 정상 유효 후보 응답은 간격 끝까지 기다리지 않고 바로 선택한다.
- 같은 round 안에서는 남은 시간 동안 수신을 계속할 수 있다. 오류가 즉시 반복돼도 busy spin/무한 drain이 되지 않게 batch/시간 예산·적절한 wait/yield를 둔다. pause 뒤 누락 round를 한 번에 따라잡아 burst를 만들지 않는다.
- 후보 전부를 동일 round에서 고려하는 기존 정책·선택 응답 검증·전체 punch budget·cancel은 보존한다. stop/overall deadline을 매 batch와 송신 전 확인하고 단일 대기가 기존 취소 응답 범위를 넘지 않게 한다. 깨진 socket의 영구 오류를 무조건 transient로 삼키지 않는다.
- 정상 round당 각 후보에1개라는 송신량을 회복하는 보수다. host의 응답 창10초·소스25/창200/초50·capability 정책을 늘리지 않는다. 다른 단일 Punch 경로를 함께 정리해야 한다면 같은 scheduling helper 재사용과 해당 회귀 범위로 한정해 보고한다.

### 2. Hello — 같은 라운드에서 ACK를 찾되 송신량을 늘리지 않음

- 한 Hello 송신 후 현재 wait slice/overall budget 안에서 queued Punch·비ACK·분류된 transient UDP 오류를 처리하고 **곧바로 다음 수신을 시도**한다. 비ACK마다50ms sleep하거나 새 Hello를 보내지 않는다. 단순히 기존 sleep 한 줄을 지워 outer loop 송신을 폭주시킨 변경은 불가다.
- 다음 Hello 송신은 정상 라운드 재시도 시계에서만 한다. 기존 sliceMaxMs·budgetMs·retrySleepMs에 의한 정상 재시도 동작/실패한 send 처리·stop 의미를 유지한다. 시한과 stop은 잘못된 datagram/오류가 계속 들어오는 중에도 확인하고, batch 무한 독점/CPU spin을 막는다.
- 기존 connected peer·ACK 길이/magic/kind/version/FEC 및 token 사용 시 DirectoryAuth 확인을 그대로 유지한다. 잘못된 ACK를 빨리 읽는 것이 받아들인다는 뜻이 되어서는 안 된다. 인증·capability 갱신·epoch 승계/재개 정책을 바꾸지 않는다.
- `attempts`는 실제 Hello 송신 시도, `badAcks`는 읽었으나 유효하지 않은 datagram 등 기존 지표의 뜻을 유지/명시한다. 오류 수·수신 batch 같은 보조 진단은 bounded 숫자 요약만 허용하고 매 datagram 로그·token/capability 내용은 출하에 넣지 않는다.

### 3. 제외하는 대안

- **일괄 사전 flush는 채택하지 않는다.** 초기 유효 ACK/관련 datagram까지 버리거나 유입이 계속될 때 끝나지 않는 우회가 필요 없다. 같은 검증 루프에서 항목을 소비하는 쪽을 선택한다.
- **SIO_UDP_CONNRESET=FALSE는 보류**한다. [Microsoft IOCTL 문서](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls#sio_udp_connreset-opcode-setting-i-t3)의 PORT_UNREACHABLE 보고 동작을 바꾸는 별도 선택이다. ①/②가 ICMP가 남는 환경에서도 정확히 동작해야 한다. 옵션으로 증상을 가려 핵심 보수의 효과를 주장하지 않는다. 이후 필요하면 별도 효과/실패 대조로 판단한다.
- host control10초·window-list15초·hello/punch 전체 budget·shell 자동3회 정책, wire/API·directory race/STUN/UPnP, Host의 punch 반사 제한은 변경하지 않는다. 수동IP10초 정지 ledger와 이 원인이 같다고 합치지 않는다.

## 완료·시험 기준

**합계보다 결정적 반례를 우선한다.** 실제 제품 루프를 테스트 공급 경계로 구동해 아래 실패를 잡고, 실제 socket/프로세스 사슬로 잇는다. 복제한 성공 stub만으로 끝내지 않는다.

1. **Punch 폭주 반례:** 연속 즉시10054/무관 datagram/빈 datagram을 공급해도150ms당 round1개 상한(초기1회·측정 경계 허용 명시), 전체 budget/cancel 유지. 도달 가능한 다른 후보가 중간에 응답하면 즉시 선택, 응답 없는 후보가 나머지를 굶기지 않음. cadence 보수를 제거한 변이가 실제로 실패해야 한다.
2. **ACK backlog 반례:** 유효 ACK 앞에 최소 Punch25개+오류/비ACK100여 개를 두고 실제 hello 루프를 실행한다. 이미 읽을 수 있는 ACK 처리 시간이 항목수×50ms가 되지 않고, 통제된 loopback 조건에서는1초 이내에 처리됨을 기록한다. 비ACK마다 sleep하는 변이가 이 기준을 깨야 한다. 유효 ACK가 도착하기 전의 정상 네트워크 대기까지0초로 요구하는 것이 아니다.
3. **안전/중단 대조:** 유효 ACK 없음·유입 계속됨에서도 기존 전체 deadline/stop으로 종료, 송신 횟수 폭주/CPU 무한 spin0. 잘못된 길이/magic/version/features/DirectoryAuth는 성공0, 정상 token/LAN 흐름은 기존 정책대로 성공. fatal socket/send 오류와 cancel도 보존. punch 응답 상한 시험은 유지한다.
4. **실제 재현 전후:** 같은 “Host 생존 + Viewer11초 유지→종료→1초 뒤 재생성” loopback/도달 불가 LAN 후보 조건에서 수정 후보를 최소10회 측정하고 짧은4초 유지 대조도 한다. 기존 base/무수정 산출물은 대조로 재사용 가능하다. hello>3초·host epoch 종료/목록 timeout·exit43 추가 회차가 남으면 로그로 기제 분리하고 완료 판정 전에 설명한다. 시험이 통과한 표본을 실망 전체 성공 확률로 일반화하지 않는다.
5. **사용자 행동 사슬:** DW의 Host 생존·presentfail 재접속과 정상 desktop 재접속에서, 인증→목록→올바른 대상 새 present까지 기존 전체 기한/최대3회 안에 독립 확인한다. 첫 attempt 시간/실패도 따로 기록한다. 기존 기한을 늘리거나2번째 성공으로 첫 지연을 로그에서 숨기지 않는다. DW의 monitor 보수와 별도 task/commit을 유지하며 중복 전량 GPU/store 시험은 하지 않는다.
6. **공유 코드 영향:** 같은 hello 함수가 Android native 경로에서도 사용됨을 확인했다. portable 코드 변경의 기존 Android native 빌드/관련 무응답·cancel/성공 시험은 영향 범위에서 확인한다. 이것은 새 APK 기능·버전·설치/배포 승인과 다르다. PC/Android 호출자의 retrySleepMs=0/50 등 서로 다른 옵션도 대조한다.
7. **독립성/출하:** 작업용 자체 실행 뒤 검증용이 고정 commit/hash의 핵심 반례와 실제 재현/사슬을 독립 실행한다. 시험 공급 경계/상세 진단은 출하에서 제외하고 부재+양성 gate를 확인한다. 관련 hash·실행rc·정상/부정 대조·남은 실망 항목을 한 보고에 묶는다.

## 위임·0.2.162 범위

- **조사 t-uu1zshde는 이 판정/근거를 연결해 마무리하고, 검증용이 작업용1에 최초 구현 task를 한 개 생성**한다. 조사와 구현 ID/다음 담당을 연결하고 `.claude/task_reconnect_hello_delay_impl_r1.md` 같은 통합 지시서 하나에 위 계약을 담는다. 같은 구현을 작업용2에 중복 지시하지 않는다.
- 별도 worktree/브랜치에서 승인된 제품 기준 위에 최소①/②·시험·history/계획 상태·Git MCP commit을 작성한다. 진단 scratch의 매패킷 로그/광역 seam을 그대로 제품에 복사하지 않는다. 보호된 설치본·실서비스·회사 PC/폰은 조작하지 않는다.
- **PC0.2.162 통합 후보에 포함하는 방향 승인**이다. LK/DW/SH와 각각 고정 commit을 유지하고 공유 protocol/gate/코드 충돌 및 영향 검증 후 통합 후보를 Codex에 보낸다. 현 설계 판정만으로 version bump·서명·NAS 게시·사용자 설치·push 또는 release OK가 되지 않는다. APK S5 미결도 유지한다.

사용자가 지금 할 일: 없음. Codex 수행은 보고/원문 source·대표 로그/diff·공식 API 문서 검토와 미추적 판정서/인계 작성이다. 제품 수정·빌드·테스트 실행·설치·commit·push 없음.
