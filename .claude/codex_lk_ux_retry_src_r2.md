# LK-UX retry r2 — SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 후보 **598339a927bf3f136c1c6b84545eed3abe75e274**, parent9991b36.

**SOURCE: NEEDS_CHANGES — N1/N2가 일부 보수됐으나 아래 경계가 남았다.** 검토 범위는9991b36..598339a diff와 그 호출 경로다. 새 제품 빌드/실행 시험은 하지 않았고, EVIDENCE는 예정된 R1 통합 고정 후보 검사로 유지한다. worktree HEAD598339a/tracked clean 직접 확인.

## N1 — 전체 시한 도입은 수용, I/O 직전·결과 채택의 만료 경계 잔여

수용: Command.deadlineMs, caller timeout의 queued retry 제거/liveTx 회수, worker 시작 전 검사, supersede 뒤 검사, IPC budget 축소, A terminal17 유지와 늦은 challenge hold 해제 방향은 이전 결함을 실질적으로 줄인다.

잔여 위치: `host_unlock_relay.cpp:514~539, 573~580, 606~619`(supersede의461~470도 같은 budget 형태).

post-supersede 소유/시한 검사는 mu_를 놓고 I/O보다 앞에서 한다. 그 뒤 deadline 계산은 **시한을 이미 넘겼으면 left=1**로 새1ms budget을 준다. `Exchange`는 retry의 절대 시한/liveTx를 모르며 새 상대 시한으로 Write/Read를 시작한다. 결과를 hold/cache하는 부분도 IsLiveLocked만 확인하고 deadlineMs를 다시 확인하지 않는다. caller wait보다250ms 안쪽이라는 여유는 스케줄링 경합을 닫는 동기화가 아니다.

가능한 경계(정적 실행 순서, Codex 실행 재현 아님):

1. supersede 성공 후514~520의 검사 통과.
2. worker가 그 직후 지연돼 op deadline을 넘김. 더 길게 지연되면 caller도 timeout18을 반환하고 liveTx를 회수한다.
3. worker 재개→576행 left=1→Exchange가 **만료/회수된 B의 새 challenge를 뒤늦게 전송**할 수 있다. caller는 아직 wait 중인250ms 구간이면 유효 response를 hold/cache할 수도 있다.

이는 r1의 '18 뒤 새 challenge' 경계를 완전히 닫지 못한 것이다. 이미 시한 전에 송신한 IPC의 결과가 늦게 도착하는 경우와 **취소/만료 뒤 새 IPC를 시작**하는 경우를 구분해야 한다.

요구:

- 만료는1ms 연장이 아니라 새 IPC 시작 거절로 처리한다. **실제 I/O 개시의 선형화 지점**에서 operation 소유/취소·절대 시한을 확인하고 caller의 회수와 직렬화한다. 공유 operation 상태/시한을 아는 Exchange 시작 경계 등으로 처리할 수 있다. 무거운 blocking I/O 전체를 mu_ 아래 잡으라는 뜻은 아니다.
- fresh challenge 결과를 채택해 새 hold/cache를 만들기 직전에도 같은 owner·절대 시한을 검증한다. 만료 뒤의 유효한 늦은 응답은 새로운 발급 성공으로 승격하지 않는다. A가 실제로 끝난17은 올바른 old generation에 보존하되 B의 새 권한/hold는 만들지 않는다.
- 기존 timeout 숫자/여유를 더 크게 만드는 방식은 피한다. **검사 통과 뒤 실제 Write 직전**을 latch로 멈추고 deadline/caller 회수/EndSession을 진행한 후 해제하는 대조가 필요하다. supersede 지연 중 검사에 걸리는 기존 M5 시험만으로 이 지점이 검증되지는 않는다.

## N2 — 로컬 봉인 실패는 닫힘, 전송 후 B 미확정의 소유 상태는 잔여

수용: progress.challengeIssued/sealSent 도입, A 종료 확인과 로컬 봉인 실패 문구 분리, terminal17을15로 바꾸지 않는 수정은 원래 지적의 **로컬 봉인 전 실패**를 해결한다. e2e N2와 M6는 이 경계를 구분하도록 구성됐다(실행 수치는 작업용 인용).

### N2-a: B가 이미 보내졌을 가능성이 있는데 B 추적까지 버린다

위치: `viewer_control_client.cpp:847~860, 872~878`, `viewer_unlock.cpp:488~519, 567~579`.

새 B의 challenge를 받은 뒤 seal을 송신하고 아직 최종 결과를 못 받으면 exchanged=false/progress.sealSent=true이므로 kLockRetryUnknown을 안내한다. 그러나 A의 poll ID를0으로 만든 뒤 `waiting`이 false라서 **B의 requestId도 후속 추적 상태에 남기지 않는다.**

실제 연결이 끊기지 않아도 가능한 소스 순서:

- Host는 결과가 아직 없으면 ControlStatus에서 **ConnectStarted/terminal0**을 정상 응답한다(`host_control_session.cpp:1052~1055`). Viewer의40회×250ms status loop는 이를 정상 수신하다 약10초 뒤 false가 될 수 있다.
- relay sealedDeadline은25초다. 즉 B가10초 뒤에는 아직 처리 중이고, 그 후 typed가 되는 범위가 존재한다. 이때 link는 정상이다.
- A는 끝났고 B도 추적되지 않아 다음 클릭은 supersedes=0의 일반88이다. 서비스에서는 B가 typed 대기 중이므로 pending 거절된다. **같은 연결에서 다시 누르면 앞 요청에 막히는 원래 UX가 실패 경로에 재발**할 수 있다.

요구: 'A 종료', 'B 미전송', **'B 송신 가능·결과 미확정'**은 UI 문구뿐 아니라 소유 request ID/상태도 분리한다. 미전송이면 B를 대기하지 않는다. 송신 가능/미확정이면 B를 tentative 상태로 유지하고 status로 terminal/typed를 확정하거나, 사용자의 다음 명시 retry가 **B를 대상으로** 기존 안전한 직렬 supersede를 거치도록 한다. 확인 전에는 '입력 완료/성공'이라고 표시하지 않는다. 암호/암호문을 보관·자동 재전송하지 않는다. 실제 연결 종료 시에는 해당 generation을 기존 규칙대로 정리한다.

대조: A17+B ChallengeIssued/Accepted→같은 실제 control link가10초 이상 정상 pending 응답→B 늦은 typed→사용자 다시 누름→B를 대상으로 명시 retry→새 입력/해제, reconnect0. 로컬 봉인 전 실패는 B pending0 유지 대조를 함께 둔다. 입력 시한/Viewer poll 횟수를 늘리는 것만으로 피하지 않는다.

### N2-b: challengeIssued 신뢰 표식은 purpose 검증 뒤에 세워야 한다

`viewer_unlock.cpp:429`에서 progress.challengeIssued=true가 먼저 설정되고, **432~434에서 LockCred purpose 불일치로 거절**한다. 이런 응답도 caller는 priorEnded로 믿어 A 대기를 지우고 '암호화 실패'로 안내한다. 또한 retry17의 비-ChallengeIssued 응답은 purpose 검사에 들어가기 전에 return한다.

요구: LockCred retry에 대해 **해당 requestId/purpose 등 필요한 응답 검증을 모두 통과한 뒤에만** A 종료를 신뢰한다. 잘못된 purpose/foreign 응답은 종료 미확정이며 A를 보존한다. 올바른17을 검증한 경우만 별도의 prior-ended 응답으로 채택한다. fake Host의 같은RID·잘못된purpose ChallengeIssued/17을 주어 A가 보존되고 seal0인지 확인하는 좁은 반례면 된다.

## 기타 수용·다음 단계

- 새 TEST seam은 CMake에서 `remote60_viewer_prelogin_login_e2e_test`만 정의하는 방향 수용. 통합 출하 compile define/byte gate에서 **REMOTE60_VIEWER_UNLOCK_TEST_SEAM / GNLINK_VIEWER_TEST_SEAL_FAIL** 부재·시험 양성을 기존 합집합에 귀속한다.
- 번호 PROPOSED→검증용 배정 주석 갱신은 닫음. 서비스/암호화 wire 변경이 없는 나머지 r1 소스 수용, UIA 실패 자동 감지 보류, R1 통합 독립 EVIDENCE 계획은 유지한다.
- 보고 relay70×5·e2e62/79/28·M5/M6는 작업용 실행 귀속으로만 기록한다. 현재 요청은 SOURCE 재확인이므로 Codex/검증용 독립 PASS로 합산하지 않는다.

검증용은 기존 LK-UX task에 **위 잔여 경계를 한 묶음**으로 취합해 보수하고 새 고정 diff를 요청한다. 같은 시험을 전량 반복하기보다 I/O 직전 회수·B 늦은 typed·잘못된 purpose의 결정적 반례와 영향 회귀에 집중한다. PS/UA·SH 독립 진행은 유지하며 별도 새 중복 task·제품 설치·UAC 자동 조작을 요구하지 않는다. Codex 변경은 미추적 검토서/인계뿐이고 제품 commit/push는 없다.
