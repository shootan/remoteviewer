# LockCred Q1/Q2 — Codex 판정 r1

2026-10-22 · task=t-rs9nhsdv / rev=r1 / kind=design-confirm-result
reply_to 검증용 `remote#wwml7h91`

**Q1: (a)+(b) OK. Q2: 자동 잠금 시 상시 점유를 없애고 on-demand/bounded로 복귀 OK.** 기존 task의 정본에 통합해 진행하며 별도 승인 왕복은 필요 없다. 아래는 구현 조건이지 현재 후보의 최종 소스/행동 승인일 수 없다.

## Q1. 중복 방지와 암호 칸 교체

- **(a)** 서비스 권위에서 같은 대상 session/lock generation의 미결 job을 중복 실행하지 않는다. 새 challenge가 이전 inflight/입력 후 해제 관측 job의 context를 덮지 못하게 한다. 같은 request의 중복은 기존 결과만, 다른 새 request는 **처리 중**을 표시하고 입력하지 않는다. viewer latch만으로 대신하지 않는다. terminal/취소/연결 종료/timeout에는 실행 취소·회수와 관측 상태 정리를 일관되게 하며 ‘기다리는 중’ 플래그가 영구 latch가 되지 않게 한다. 이미 입력된 job의 관측 취소는 입력을 되돌렸다는 뜻이 아니다.
- **(b)** 새로 승인된 retry는 **검증된 동일 LogonUI 암호 필드의 기존 내용을 교체**한다. append는 허용하지 않는다. 해당 control이 지원하는 writable ValuePattern의 **값 교체**, 또는 검증된 동일 필드에서의 select-all→Unicode 일회 입력을 허용한다. password 내용을 읽어 확인하거나 clipboard/임시파일로 옮기지 않는다. API 성공 여부/필드 준비와 **실제 인증 성공**은 별개다.
- select-all/교체는 **목적 분리된 sealed job 내부의 고정 동작**이다. remote arbitrary key/명령을 허용하거나 plaintext nav 목록에 Backspace/Delete/Ctrl+A를 추가하지 않는다. caller/session/lock/topology/만료·실제 LogonUI 경로/SYSTEM/session/UIA IsPassword·동일 focused element 검증은 **교체 직전과 입력 직전**에 유지한다. foreground HWND만 같다고 다른 필드를 허용하지 않는다.
- 교체 동작/선택 지원을 확인할 수 없으면 TargetNotReady/명시적 미지원, 부분 입력은 InjectFailed로 끝내고 **다른 방식으로 자동 재시도하거나 append로 폴백하지 않는다**. select-all 경로는 실제 지원을 시험해야 한다. 기존 modifier가 간섭할 때는 준비 실패/재시도 안내로 처리하고 실제 사용자가 누른 키를 임의 해제하지 않는다. agent가 만든 Ctrl/A key-up은 실패 때도 정리한다. SendInput이 기존 keyboard state를 초기화하지 않는다는 [Microsoft 계약](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)을 고려한다.
- 재시도마다 명시적 사용자 입력/확인이 있어야 하며 자동 Enter/로그온 재시도를 새로 넣지 않는다. 결과 대기 중이면 ‘처리 중/입력됨·Enter 필요/해제 미확인’을 정확히 알리고, 대기가 끝난 뒤 retry 수단을 제공한다. 보안 입력칸에 남은 값은 retry 승인의 교체 대상으로만 취급한다.

**필수 대조:** 기존 값 있는 칸에 첫 시도→관측 종료/실패→새 암호 retry가 `old+new`가 아니라 새 값 하나가 되는 제품 동등 UI 시험; 잘못된 첫 입력 뒤 실제 잠금 해제 재시도; pending 새 요청 거부/원래 결과 보존; 다른 control로 focus 변경·부분 select-all/입력 실패·지원 안 됨·cancel/timeout 후 재누름. fixture는 테스트 암호만 쓰고 실제 암호를 읽거나 로그에 남기지 않는다.

## Q2. capability probe와 실제 transaction의 점유 분리

- `ProbeLockCredAsync`의 **capability 조회는 짧은 연결→신뢰/버전 응답 검증→즉시 닫기**다. 잠겼다는 이유만으로 pipe를 계속 잡지 않고, query cadence는 bounded/backoff로 둔다. idle Host가 다음 viewer 요청을 기다리며 독점하지 않는다. 지원 cache는 확인한 서비스 identity/수명과 연결하고, 실제 job 시작 시 신뢰된 현재 service/지원 여부를 다시 확인한다. 조회 실패/서비스 교체가 stale Supported로 지속되지 않게 한다.
- **실제 사용자 요청의 challenge→sealed→status/terminal은 기존 연결 바인딩을 보존**한다. 단계마다 무조건 닫으면 현재 서비스의 per-connection `UnlockSession`과 callerStillThere/challenge가 사라진다. stateless 요청처럼 재연결해 job을 이어받거나 같은 cookie/requestId만으로 새 caller에 붙이지 않는다. 별도 영속 job broker/새 재접속 인증을 이번 작은 보수로 암묵 도입하지 않는다.
- 실제 transaction 점유는 명시 deadline 내로 제한하고 terminal/취소/상위 control session 종료/실패/timeout에 **queue·pending 결과·owned child 정리를 마친 뒤 pipe를 반환**한다. 사용자 Enter를 무한히 기다리며 잡지 않는다. timeout 결과는 해제 미확인이며 성공/자동 입력 재시도 아님을 표시한다. 동기 I/O timeout은 호출자 기다림만 끝내고 relay worker를 막아 두는 형태가 아니어야 한다.
- 이것은 idle capability 점유 회귀의 제거다. 기존 single-instance가 실제 transaction을 직렬 처리한다는 한계까지 다중 동시 처리 구현으로 해결됐다고 보고하지 않는다. 실제 동시 transaction에서도 다른 session의 deadline이 지속적으로 깨진다면 기존 private per-connection state를 유지한 multi-instance 서비스가 별도 보수 대상이며, protocol/신뢰 변경은 검증용 경유로 묶어 판단한다.

**필수 대조:** Host A가 오래 잠겨 있고 capability만 조회한 뒤 Host B의 기존 disconnected unlock이 시작/종료됨; A의 query 반복이 B를 굶기지 않음; 실제 A transaction 중 다른 요청이 context를 덮지 않음; 정상/거절/취소/timeout/끊긴 service·child 이후 pipe 재접속과 후속 요청 성공. capability probe만 계속 연결한 기존 mutant는 B 요청을 막아 실패해야 한다.

소스는 현재9956cca 계열의 relay WorkerLoop/capability·service의 issue/status/per-connection state 경계를 확인했다. 아직 진행 중인 전체 후보를 빌드·실행/독립 승인한 것이 아니다. 이전 `.claude/codex_lockcred_agent_r1.md`의 session 간 익명 handle 상속 반려·인증된 one-shot private IPC 계약은 그대로 유효하다. 공개 문서/제품 코드 변경 없이 이 판정서만 보관하며 별도 commit/push 없음.
