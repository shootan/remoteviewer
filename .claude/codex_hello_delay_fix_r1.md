# hello-delay-fix r1 — Codex SOURCE/EVIDENCE 검토

2026-10-22 / task **t-q4eq9aiz** / 후보 **83c3ac5b8b3fd2e42e2da9d46cbe985de79b506b** (제품7646eef, 시험4d8e4a8, 문서83c3ac5).

**NEEDS_CHANGES — N1/N2 한 묶음 보수.** 문제를 만든 PunchAny 즉시 재송신과 항목별 hello 지연은 정상 재현 조건에서 개선됐고, 인증/ACK 식도 보존됐다. 다만 **hello 수신512개 상한이 송신 시계를 앞당기는 경로**, **멀티 후보 PunchAny에서 개별 datagram 오류를 전체 소켓 실패로 취급하는 분류**가 남는다. 신규 범위 확대가 아니라 채택한 cadence/후보 공정성 계약의 보수다. directory_retry 간헐은 아래처럼 별도 기록하고 전체 PASS로 합성하지 않는다.

## 직접 확인한 근거

- 작업용 보고, `C:/work/gnlink-wt/verify-hello/.verify/verify_hello_r1.md`, 5063cd9→83c3ac5 제품/시험 diff·관련 원문을 읽었다. 구현 HEAD/tracked clean·CR-at-EOL 허용 diff-check 확인.
- 검증용 출하 구성 hash 직접 일치: Viewer `82540434c33be03f2850d08e2690b17981e41d52b34f020315441ce719408bc4`, Stream `b7b4d242ad2710a6918a15dc90398c4a5b4ab93fe36a338357ea4a6cab596871`, Client `f043508f16e4bd01448404a0f4e7c7f11b59806bfaa279af7f57e2370512afea`.
- 독립 원본 hello_backlog18×2·punch_any×2·hello_stats14 및 영향 시험/gate9/self16을 대조했다. directory_retry는 **첫132/133 FAIL, 이후133/133×3**이며 합쳐서 전량 통과라고 하지 않는다. 변이H1~H3·Android4ABI 빌드는 작업용 인용이고 독립 재실행으로 쓰지 않는다.
- 기준판8.661초/139attempt·badAcks25와 후보11초 유지 재연결10회262~539ms·3초 초과0은 검증용 기록에 근거한 개선 관찰이다. 사슬 원문에서 첫 연결534ms, 첫 재연결262ms·attempts2·recvErrors0, `result=restored target=desktop gen=4`를 직접 확인했다. 작업용/검증용 측정 수치를 혼합하거나 실 LAN/NAT 성공률로 확대하지 않는다.
- 검증용은 출하 구성 고정 후 시험 Host seam을 추가했다. 이후 측정 Viewer hash가 동일하다는 귀속을 유지하며, 현재 verify worktree가 전체 clean이라고 주장하지 않는다. Codex가 제품 빌드/사슬/시험을 직접 재실행한 것은 아니다.

## N1 — read cap에 먼저 닿으면 Hello 재송신이 라운드를 기다리지 않음 (필수)

`native_video_client_tcp_control.cpp:305~340`은 `for(reads<512)`를 빠져나온 직후 retrySleepMs만 쉬고 다음 Hello를 보낸다. **512개를 slice200/250ms보다 빨리 읽으면 roundEndMs까지 기다리지 않는다.** PC는50ms pause만, Android 옵션은 pause0이라 즉시 새 round 송신이 가능하다. “다음 Hello는 slice 뒤 원래 시계”라는 계약/주석과 다르다.

현재 Flood 시험은 datagram마다 `Sleep(1)`을 넣는다. 200/250ms에512개보다 적게 공급하므로 이 상한 경로를 검증하지 못한다. 백로그25+100 성공과 1초4회 관찰은 유효하지만, 그것만으로 강한 연속 유입에서도 송신 clock이 보존됐다고 할 수 없다.

**보수:** 수신 batch 상한과 다음 Hello 송신 시각을 분리한다. cap에 닿아도 기존 roundEnd까지 새 Hello를 보내지 않으며, 남은 시간은 stop/전체deadline을 지키는 bounded wait/yield 또는 추가 bounded 수신 batch로 처리한다. ACK를 기다리는 정상 slice와 기존 retrySleepMs의 의미를 보존한다. 단순 cap 확대·Android pause 강제 추가·전체 budget 증가로 우회하지 않는다.

**반례:** 같은 생산 hello 루프에512개를 넘는 즉시 준비 항목/계속 채워지는 큐를 주고 cap 도달 자체를 관측한다. PC200/50·Android250/0 모두 다음 송신 시각/횟수가 정상 round clock 상한을 지켜야 한다. 유효 ACK 처리·무응답 deadline/cancel·CPU bounded 대조도 유지한다. cap에서 곧바로 다음 송신을 허용하는 변이가 실패해야 한다. 공급량/수신큐 손실 때문에 cap 미도달이면 그 회차는 이 반례의 증거가 아니다.

## N2 — PunchAny 개별 UDP 오류와 소켓 자체 실패를 구분 (필수)

새 `punch_receive_is_transient`는 기존 범용 `UdpRecvClass::Terminal`이면 POSIX ECONNREFUSED 외 전부 false로 끝낸다. 그러나 그 기존 분류는 기존 연결/세션의 정책이지 **멀티 후보 중 어느 한 경로의 실패가 소켓 자체 손상임을 증명하는 분류가 아니다.**

적어도 Windows **WSAENETRESET**은 UDP에서 TTL 만료를 뜻한다([Microsoft recvfrom 오류 문서](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recvfrom)). 현재 이 값은 Terminal→PunchAny false로 모든 후보 대기를 중단한다. 한 후보가 TTL 오류를 내고 다른 후보가150ms 안에 유효 응답해도 후자를 기다리지 않고 caller의 첫 후보 fallback으로 간다. fallback이 있다는 것은 인증 취약점이나 전체 프로세스 치명 실패는 아니라는 뜻이지, 나머지 도달 가능한 후보를 놓치지 않는 계약을 만족한다는 뜻은 아니다.

**보수:** PunchAny의 로컬 문맥에서 알려진 개별 datagram/path advisory(최소 WSAENETRESET)를 bounded하게 계속 처리하고 cadence를 유지한다. WSAENOTSOCK/유효하지 않은 socket·shutdown 같은 실제 영구 실패는 종료한다. 범용 classify_udp_recv나 모든 connected 수신 경로의 기존 정책을 광역 변경하지 않는다. 문서/근거가 없는 오류를 모두 transient로 만들지도 않는다. 다른 WSAEHOSTUNREACH류는 분류 근거/처리를 명시하되 실제 재현 성공이라고 쓰지 않는다. POSIX 실망 미검증은 유지한다.

**반례/대조:** 개별 TTL 오류 후 다른 후보 유효 응답은 채택, 영구 socket 오류는 실패, advisory 연속이면 송신 상한/deadline/stop 유지. 실제 제품 루프의 오류 공급 경계를 쓰는 test-only 주입은 허용한다. 실제 router/회사 PC 설정 변경은 필요 없다. 기존10054 loopback 대조도 유지한다.

## directory_retry 간헐 판정

**ledger에 올리고 미해결로 유지한다.** 후보1/4 FAIL, 기준3/3 PASS와 diff 밖이라는 사실만으로 기존 결함/무관을 확정하지 않는다.

직접 확인한 `RunHost`는 목적 요청 횟수가99인 cached-silent 경우 약15초 기다린 뒤300ms 후 비동기 `StatusLine()` 한 번을 샘플링한다. 실패 회차는 여러 health/observe cycle을 거친 뒤 `address observation timed out`이었고, 다른 회차는 observations 설명이다. `HostAgent::Loop`가 observeAddrReady 상태에서 실패 시 timeout 문구를 쓰는 코드도 있다. **상태 문구의 관측 시점/health cooldown/HTTP 기본 observe-port 의미를 분리할 필요가 있다**는 근거이며, 스케줄 경합이나 제품 결함의 최종 원인 판정은 아니다.

필요한 것은 해당 case의 상태 전이·샘플 시점·실제 publish/heartbeat0 유지 여부를 제한해서 조사하는 것이다. 전체 suite를 성공할 때까지 반복하거나 기존 실패를 환경 INVALID로 지우지 않는다. 시험 의도가 특정 단계의 진단이면 그 단계의 명시적 조건에서 관찰하도록 고칠 수 있지만, 기대 문자열을 느슨하게 만들어 PASS만 얻거나 기존 제품 문구/관찰 정책을 이번 hello 보수에 섞지 않는다. 실제 상태 계약 변경이 필요하면 별도 근거로 올린다. 이 ledger만으로 이미 확인된 ACK 개선/인증 보존을 부정하지도 않는다.

## 비차단 기록·다음 검증

- `tcp_control.hpp`의 “pause after … bad ack” 주석은 N1의 확정된 round 동작에 맞게 같은 편집에서 고친다. 별도 문구 task/커밋은 불필요하다.
- N1/N2 한 유효 보수 지시를 **기존 t-q4eq9aiz/작업용1**에 연결한다. 고정 후보로 cap 경계·오류 분류 반례를 자체/검증용 독립 확인한다. 가능하면 변이는 test target으로 만들어 검증용이 제품 소스를 수정하지 않고 실행하게 한다.
- 이미 확보한10회 재현/전체 영향 suite/실제 사슬을 무조건 전량 반복하지 않는다. 변경된 clock/advisory 경계와 정상·인증/취소, 소수의 실제 재현/사슬, 새 artifact/gate에 집중한다. Android 옵션0/250은 필수이고 공유 native build 영향 근거도 새 후보에 연결한다. 실기기/POSIX 실제 오류/실 LAN·NAT는 별도 미검증이다.
- 0.2.162 통합 후보 포함 방향은 유지하되 통합/릴리스 OK는 아니다. LK e728485·DW fc4cd57·SH c494dc6와 별도 commit을 유지하고, 최종 통합 기준/hash와 gate 합집합·연결 영향 검증을 고정한다. SH control/clipboard 실패와 이 원인이 같다고 합치지 않는다.
- 조사 scratch 삭제 사고는 이미 기록된 정책 위반으로 유지한다. 남은 inv/verify worktree·junction·증거를 삭제하지 않는다. root 밖 삭제나 광역 정리는 허가되지 않았다.

사용자 현재 조치 없음. Codex 변경은 미추적 판정서/인계뿐이며 제품 편집·빌드·테스트 실행·설치/서비스 조작·commit·push 없음. 버전·서명·게시·사용자 설치·push 보류 유지.
