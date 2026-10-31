# lockscreen-cred r1 — Codex SOURCE/EVIDENCE 검토

2026-10-22 · task=t-rs9nhsdv / rev=r1 / kind=review-result
reply_to 검증용 `remote#wwml7h91`

**NEEDS_CHANGES.** 목적 분리·전용 named pipe·자식 생성/상대 인증·공통 뷰어 진입은 반영됐다. 그러나 연결별 결과 격리, 실제 입력 시점의 취소/만료, 최종 평문 차단, 동일 암호 필드 교체와 relay I/O 상한이 계약을 충족하지 않는다. 아래 L1~L6을 한 번에 기존 구현 task로 보수한다. 아직 통합/출하 승인이나 OS 실기 성공 판정은 아니다.

## 대상과 근거 층

- 고정 HEAD **550a9441ec5bccd8617029c9db6c784aa0030921**, base5063cd9, `C:/work/gnlink-wt/fix-lockscreen-cred`, tracked clean. 12commit/38파일 변경목록, 관련 제품diff·보안 경계 소스와 시험 일부, 작업용 보고·독립검증 기록을 읽었다. 전체 시험 소스/모든 실행을 재감사했다고 주장하지 않는다.
- 독립 build-vs의 InputService **e3838e0cbf9148640dfaace6409327217ce58cc1f4a2f8c992bdbd42134bbc72**, Stream **3db5ad10622016fe6420c01b14671b320605942edf11eb9e7f3aa4d7c1700468**, Viewer **078b22c355d6e976139cb4e71dd26b2a32e1ffd5594c4c6cac44f9c3c0b82115**를 직접 hash 대조했다. 작업용의 다른 빌드와 hash가 다른 것을 결함으로 보지 않는다.
- 검증용 원본 `secure_unlock_lockcred.out`41/0, `lock_cred_channel.out`14/0, `host_unlock_relay_ondemand.out`11/0 끝줄과 seam scan을 직접 확인했다. 나머지 seal/target/policy/gate의 rc와 수치는 검증용 기록 귀속이다. **Codex가 빌드·UI/OS 입력·테스트 독립 실행을 수행한 것은 아니다.**
- 제품UI 2종은 검증용 SKIP, 작업용 저장 lock-screen.out은35 checks이며 최종소스의36번째 검사는 정적 확인만이다. 현재 후보의36체크 실행 성공으로 바꿔 적지 않는다. 아래 보수 후 실제 고정 후보로 독립 UI를 연결한다.

## L1 — 연결/목적별 결과 격리: requestId 재사용으로 옛 결과가 새 요청에 적용된다 (필수)

`host_control_session.cpp`의 `static HostUnlockRelay gUnlockRelay`는 control 연결들이 공유한다. 그런데 relay의 `sealedResults_`, `challengeResults_`, `lockCredRequests_`, status 응답이 **requestId 하나**를 key로 쓰고 PollResult/RefreshLockCredStatus도 cookie를 받지 않는다. EndSession은 pipe 해제 command만 넣고 이 결과들을 연결별로 지우지 않는다.

`viewer_unlock.cpp:370~371`의 requestId는 뷰어 프로세스 로컬 static1에서 시작해 첫 요청이2다. 따라서:

1. 뷰어 A의 request2가 terminal(TargetNotReady/성공 등)로 cache에 남음.
2. 새 뷰어 B(또는 다른 연결)가 request2로 새 cookie의 challenge를 발급받음.
3. `HostUnlockRelay::SealedAsync:451~452`는 옛 request2의 terminal을 보고 **새 sealed를 보내지 않고 반환**.
4. B의 status 조회는 A의 옛 결과를 반환한다. 새 입력이 생략되거나 이전 해제 성공이 새 시도의 성공처럼 보인다.

이는 새 기능의 ‘연결 변경 뒤 재시도’와 A3 바인딩 위반이다. 기존 unlock 경로에도 공용 캐시가 있었다는 이유로 이번 LockCred 경로를 승인할 수 없다.

**보수:** challenge/sealed/status/terminal/idempotence/timeout은 **control cookie+requestId+발급 목적/서비스 transaction identity**로 격리한다. 같은 소유 요청의 replay만 같은 결과이며 다른 cookie/목적은 새 요청이다. Poll/Refresh API에도 소유자를 넘기고, EndSession/expiry에 해당 연결의 pending·cache를 bounded하게 정리한다. 다른 연결이 진행 중인 pipe를 재사용해 context를 덮지 않게 한다. 단순 새 challenge 때 모든 전역 cache를 지우는 방식은 다른 연결을 깨뜨리므로 금지한다.

**대조:** 서로 다른 cookie에서 같은 requestId=2를 순차/동시에 사용, 첫 실패→새 뷰어 성공, 첫 성공→새 뷰어 재잠금 시 실제 새 입력, 목적 교차·late reply·옛 EndSession. 실제 새 viewer 프로세스 재시작 사례를 포함한다. 옛 id-only cache mutant가 실패해야 한다.

## L2 — one-shot 자식의 실제 commit까지 원래 요청 권한이 이어지지 않는다 (필수)

service `HandleSealedRequest`는 spawn 전 topology/caller/lock을 검사하지만, `type_lock_cred`로 넘기는 인자는 session/password/count뿐이다. `lock_cred_channel::Job`도 **session/count/units만** 있고 원래 account·lock/topology generation·expiry·cancel/요청 identity가 없다. `run_lock_cred_agent:2054`의 `TypeIntoTarget(...3000,&err)`는 지원되는 **stillValid callback을 넘기지 않는다**.

따라서 service 검사 후 최대5초 자식 시작/접속과3초 대상 대기 동안 원래 challenge가 만료되거나, viewer 연결이 끝나거나, 같은 session에서 unlock→relock이 일어나도 새 password field가 있으면 계속 입력할 수 있다. service는 answer를 기다리는 동안 원래 caller/세대 변경을 감시해 취소시키지 않는다. ‘service가 자식을 소유함’은 개별 credential 요청이 여전히 유효함의 증거가 아니다.

**보수:** Job의 immutable request/context/expiry와 서비스 권위의 취소/commit fence를 private protocol에 포함한다. 자식 대기와 **필드 비우기/선택/문자 주입 직전**에 원래 목적·caller/control 수명·account/session/console/lock generation·deadline이 살아 있음을 확인한다. 필요하면 agent-ready→service 재검증→commit 승인으로 구현해도 된다. 상태 변경/상위 연결 종료/service stop/timeout은 자식 회수·zero·무입력으로 연결하고, 취소 완료 후 늦은 입력이 없도록 선형화한다. ‘세션번호가 같고 Winlogon’만으로 세대/만료를 대체하지 않는다. 이 내부 IPC 보수는 기존 신뢰 경계 계약을 구현하는 범위다.

**대조:** 자식 시작 또는 target 대기를 실제로 멈춘 뒤 원래 연결 종료/만료/service stop/unlock→relock/console 변경을 각각 주입→필드 교체/입력0·후속 요청 정상. service 최초검사만 유지한 mutant가 실패해야 한다. 같은 session helper PASS는 Session0→console 실기와 계속 구분한다.

## L3 — A6 마지막 경계: requester를 미리 검사할 뿐 실제 대상/큐 소비 시점에는 검사하지 않는다 (필수)

`refuse_locked_plaintext(message, requester)`는 service 수신 직후, `gAgentMu`와 target 해석 **전**에 requester의 잠금 상태를 읽는다. 실제 `forward_to_agent`는 `resolve_target_session`에 따라 console로 폴백할 수 있다. requester가 known-unlocked/disconnected인데 실제 console이 locked인 경우 그 requester 검사로 console 문자 입력을 허용할 수 있다.

또 service에서 unlocked로 통과한 message가 pipe에 남아 있는 사이 잠기면, 일반 `run_agent`는 매 메시지 desktop을 다시 attach하고 **그대로 inject_message**한다. agent의 최종 소비 위치에는 이 새 lock 정책이 없다. host의250ms cache/비트나 service의 앞단 검사는 ‘큐 잔류도 최종 경계가 거부’ 계약의 대체가 아니다.

**보수:** service가 해석한 **실제 target session**을 검사하고, 마지막 일반 input agent도 자기 실제 session/현재 desktop의 권위 있는 lock 상태를 주입 직전에 재검증한다. unknown+secure는 거부하며 known-unlocked UAC·일반 desktop·허용 nav/mouse/기존 release 정리는 보존한다. 전용 sealed child만 이 평문 경로와 구분된다. prelogin의 기존 logon-only fence를 없애지 않는다.

**대조:** unlocked requester→locked console 폴백, service 검사 뒤 큐에서 멈춤→lock→소비, 잘못된 역할/구형 APK 문자, unknown lock 상태, known-unlocked UAC/일반 desktop 대조. 단위 정책 함수17PASS만으로 최종 agent 경로를 실증했다고 하지 않는다.

## L4 — Q1의 ‘동일 암호 필드 교체’와 부분 입력 정리 미충족 (필수)

- `FocusedPasswordFieldIdentity`는 password field를 찾고도 **foreground HWND+PID**만 반환한다. 같은 LogonUI 창의 다른 IsPassword element로 focus가 바뀌어도 identity는 같다. `empty_by_value_pattern`은 다른 focused element를 다시 취득해 PID/IsPassword만 검사하고, 비운 뒤 `send_units` 앞에 **동일 UIA element의 현재 focus 재확인**이 없다. 같은 창 안 A필드를 비우고 B필드에 암호가 들어가도 전후identity가 같아 Injected가 될 수 있다.
- UIA 생성/포커스 취득 실패나 읽기전용 여부 조회 실패도 result0으로 남아 **정상 ‘ValuePattern 없음’과 섞여 Ctrl+A fallback**한다. 실패는 준비/입력 실패로 분리해야 한다.
- select-all 경로의 SendInput이1~3개만 성공하면 자체 Ctrl/A down이 남는다. 현재 `send_units`는 INPUT을zero하고 false만 반환하며 own key-up 정리가 없다. 기존 Ctrl/Alt/Shift/Win이 눌렸는지도 확인하지 않아 선택 동작이 다른 chord가 될 수 있다.

**보수:** 검증한 UIA element의 RuntimeId/CompareElements 또는 동등한 안정 element 참조로 같은 필드를 유지하고 교체/문자 주입 직전 확인한다. password 내용은 읽지 않는다. 정상 pattern 미지원과 UIA 실패/readonly를 구분하고 latter는 입력하지 않는다. 지원이 검증된 Ctrl+A 경로만 허용하며 기존 modifier가 간섭하면 준비 실패/재시도 안내로 처리한다. 부분 전송 시 **agent가 실제로 만든 down에 대한 up만** 정리하고 사용자의 물리 키를 전역 reset하지 않는다. 실패 뒤 append나 자동 다른 방식 재시도는 계속 금지다.

**대조:** 동일 HWND/PID 아래 IsPassword A→B focus 변경, 비우기 직후 focus 변경, UIA 실패/readonly, SendInput 결과0/1/2/3/중간Unicode만 성공, 기존 modifier down. 테스트 암호/필드만 확인하며 real password 추출/로그 금지. 교체→append mutant뿐 아니라 coarse element identity/cleanup 제거 mutant도 실패해야 한다. prelogin이 공유하는 helper 변경의 영향도 해당 기존 UI로 확인한다.

## L5 — Q2 실제 I/O 상한·서비스 cache 수명·뷰어 결과 세대 (필수)

`WorkerLoop`의 holdUntilMs는 command를 기다리는 cv.wait_for에서만 적용한다. `exchange`는 **동기 WriteFile→ReadFile**이고 별도 deadline/cancel이 없다. service가 연결을 받았지만 응답을 안 주면 caller wait_for만 끝나고 relay worker는 pipe를 계속 잡는다. EndSession의Release도 같은queue 뒤에서 기다려 처리되지 않는다. **표면 timeout으로 마지막 pipe 점유가 끝나지 않는다**는 Q2 금지 형태다.

실제 exchange를 transaction 남은deadline 및 Stop/해당 EndSession으로 취소하고, owner thread가 I/O 종료/handle 회수·pending 결과를 마무리하도록 한다. 무응답 capability/partial reply/미완료 sealed·status에서도 다음 Host가 접근할 수 있어야 한다. 버려진 늦은 completion은 L1의 소유자와 세대 검사로 거부한다. 단순 현재cv.wait_for timeout을 늘리거나 다른 thread에서 소유권 없이CloseHandle하는 보수는 금지한다.

또 `ProbeLockCredAsync`는 Supported이면 영원히 돌아가고 현재cache가 서비스identity에 연결돼 있지 않다. 종료/재시작/구형service로변경/실패 시 지원값을 무효화하고 bounded 재조회하며, 현재trusted service의지원이 없는상태에 stale LockCredV1을 계속광고하지 않는다.

viewer credential의송신시에는runGen검사가있지만 `kMsgPreloginCredResult`는 role/stage만실어 `WndProc:1262`에서무조건표시한다. **queued result에도request/control generation을붙여** 연결변경·취소후늦은성공/실패창을버린다. pending/password 상태도해당소유연결의종료에서정리한다. 새연결의결과나새prompt를옛cleanup이덮지않게한다.

**대조:** 신뢰된 fake service가Read/Write/부분응답에서계속정체→명시deadline/EndSession→worker·pipe반환→HostB성공, service교체시cap무효화, UIqueue에옛SessionUnlocked를남긴채control새세대로전환→옛창0. 현11개 on-demand시험은정상fake응답/점유반환을보며이정체·세대반례까지입증한것은아니다.

## L6 — 출하 gate 발견2건의 판정

1. **GNLINK_SIS_TEST_를 TEST_MARKERS에추가하고ASCII/UTF-16단독표식부정대조를넣는다.** 현재출하5exe0hit와testbuild6/4hit를직접확인했으므로현후보에실제누출이있다고판정하지는않는다. 다만다른기존표식이우연히동반된다는것에새env접두검사를의존하지않게한다. 같은보수범위에서처리한다.
2. DW의mt-ab/GNLINK_CHAIN_은 **통합gate에서합집합유지**한다. DW기능이없는이단독base에DW소스를끌어올필요는없다. 통합후제품과시험산출물mapping/hash를다시고정하고새SIS접두+DW표식각각의단독negative및실시험exe대체검사를한다. 다른브랜치gate를덮어써표식을잃지않는다.

## 통과한 설계와 남은 EVIDENCE

| 항목 | 판정 |
|---|---|
| A1 역할/지원 | 별도LockCredV1·자기세션상태·prelogin재사용안함확인. 현재cap수명은L5보수 |
| A2 단일버튼/단축키 | 매번prompt·무저장·repeat/modal guard·결과창·Enter별도설계확인. 연결/결과수명과재시도는L1/L5, UI독립미완료 |
| A3 sealed purpose | KDF/AAD3분리·발급purpose선택·호출size/echo검사확인. crypto음성시험근거있음. 전체request/result바인딩은L1/L2보수 |
| A4 SYSTEM job | named pipe선생성/SY전용/remote거부/first-instance·FALSE inheritance·고정exe·suspended/job/resume·held child와trusted부모검사확인. 최종권한/필드교체는L2/L4보수 |
| A5 뷰어안내 | 양role gate·안내·down/up정리의추가방향확인. 실제최종UI독립검사필요 |
| A6 호스트/최종차단 | 호스트·service앞단추가확인, **최종target/agent경계는L3보수**. 구APK영향은고지유지 |
| A7 시험 | 비UI독립근거는확인. 최종후보UI36/prelogin독립실행·신규반례·OS실기구분필수 |

- 보수후고정후보로영향있는단위/process/실제뷰어UI `--lock-screen`·prelogin을검증용이슬롯확보뒤실행한다. 작업용저장35개와최종소스36개를합성해PASS로쓰지않는다. 제품동등시험과Session0→console/실제LogonUI·Enter·UAC·구형APK OS실기는별도층이다.
- 보고된 `client_session_role_park` S1간헐2/39는**미해결**로유지한다. ‘diff밖’또는기준1회PASS만으로기존문제라고확정하지않는다. 같은구성의기준/후보와연관경로로영향을분리하고회귀면보수,시험환경/fixture면그근거를남긴다. 이번판정으로모든회귀51종PASS를독립승인한것은아니다.
- OS A~D미검증은그대로다. 현재실기안A⑧의‘한쪽대기여도다른unlock안막힘’은실제transaction직렬한계와충돌하므로 **idle capability연결해제후다른Host요청가능 / 진행중transaction은명시상한내직렬**로정정한다. 실제설치/서비스재설치/Win+L/재부팅/UAC/폰설치는정확범위사용자실기묶음으로준비하고지금임의실행하지않는다.
- APK0.2.30잠금비밀번호수단차단영향은유효하다. SH재개를A6용sealed APK구현승인으로읽지않는다(S5미결). 버전·서명·게시·push는현재후보검토로허용되지않는다.

검증용이기존t-rs9nhsdv에L1~L6/영향검증을통합해작업용1에한번전달하고같은task에서보수한다. 독립인DW/SH는계속진행한다. Codex는공개문서/제품소스편집·빌드·사용자UI입력·서비스설치·게시를수행하지않았고이미추적판정서만작성했다. 별도commit/push없음.
