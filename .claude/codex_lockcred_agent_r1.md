# lockscreen-cred-impl r1 — one-shot SYSTEM agent 해석 판정

2026-10-22 · task=t-rs9nhsdv / rev=r1 / kind=design-confirm-result
reply_to 검증용 `remote#wwml7h91`

**one-shot SYSTEM 자식 자체는 OK. ‘Session0 서비스→대상 콘솔 session의 익명 파이프 상속으로 인증 경계 충족’은 NEEDS_CHANGES다.** 나머지 A 구현을 중단하지 않고 이 전달 경계만 아래 방식으로 보완한다.

## 근거

Microsoft [CreateProcessAsUserW 문서](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw)의 bInheritHandles 계약은 **세션 간 handle 상속 불가**, TRUE이면 호출자와 같은 session에서 생성해야 함을 명시한다. SYSTEM SID가 같아도 Terminal Services session identity는 다르다. HANDLE_LIST를 지정한다고 이 제약을 해소하는 것은 아니다.

현재 작업용 worktree `C:/work/gnlink-wt/fix-lockscreen-cred`의 기존 start_agent는 TokenSessionId 변경 뒤 CreateProcessAsUser(TRUE)를 쓰지만, 이것을 신규 credential 전달의 보장 근거로 삼지 않는다. 기존 prelogin launcher는 bInheritHandles=FALSE, CREATE_SUSPENDED→job→resume를 사용한다. 지금 기존 상주 입력 agent까지 별건 전면 개편하라는 뜻은 아니다. 새 잠금 credential은 보장된 전달 경계로 구현한다. 이 판정은 실제 session 간 실행 시험 없이 공식 API 계약과 소스를 대조한 것이다.

## 확정 대안: service-owned 일회성 사설 named pipe

1. 서비스가 **먼저** 일회성 pipe를 만든다. unpredictable 식별자, FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_REJECT_REMOTE_CLIENTS, SYSTEM 전용 보호 DACL로 만들고 실패/이름 선점 시 거부한다. 경로의 비밀성으로 인증하지 않는다. 일반 secure-input pipe의 kind1/2를 credential로 확장하지 않는다.
2. 보호된 설치 위치의 고정 exe를 lpApplicationName에 명시하고, 서비스가 복제한 SYSTEM primary token의 대상 session과 `winsta0\\Winlogon`을 선택해 **bInheritHandles=FALSE, CREATE_SUSPENDED→kill-on-close Job 배정→resume**한다. job 실패 시 재개하지 않는다. exe/desktop/session을 원격 요청의 임의 경로나 문자열로 결정하지 않는다. 새 mode는 이 서비스의 owned child용이며 shell 임의 실행만으로 입력할 수 없어야 한다.
3. 서비스는 연결 client가 **CreateProcess가 반환한 held process handle의 살아 있는 자식**인지, PID·생성 identity·SYSTEM token·대상 session을 검증한다. 자식도 server가 실제 신뢰된 서비스 process인지 기존 trusted-service 검증과 held identity로 확인한다. 단순 SYSTEM SID/pipe 이름/명령줄 parent PID만으로 상대를 인증하지 않는다. 자식이 pipe endpoint를 열었다는 사실만으로 credential을 보내지 않는다.
4. 인자에는 mode와 비밀 아닌 bootstrap 식별자 정도만 둔다. **암호·sealed job 본문·키·bearer 인증 비밀은 argv/env/파일/stdout/로그에 두지 않는다.** 상호 endpoint/owned child 확인 이후 사설 IPC에서만 bounded versioned job과 결과를 교환한다. 일반 high-rate 입력과 분리하고 임의 extra message/길이/재사용을 거부한다. child는 서비스가 검증한 목적·caller session·request/cookie·lock/topology generation·만료를 받고 실행 직전에 상태·LogonUI/UIA 대상을 다시 검사한다.
5. 서비스가 복호화한 암호를 이 전용 SYSTEM 자식으로 전달하는 것은 기존 sealed authority 경계의 내부 단계로 허용한다. 사용자 Host는 평문을 열지 않는다. job 1개→검증된 target에1회 입력→결과→종료이며 자동 재전송하지 않는다. 모든 정상/실패 경로에 버퍼 zero·handle close를 둔다. 취소·세션/lock generation 변경·service stop·timeout에는 더 이상 입력하지 않고 owned child/job을 회수한다. 강제 종료된 자식이 zero 루틴을 실행했다고 주장하지 않는다. 자식의 EOF/정체·UIA 정체가 서비스/다른 control 세션을 무한 대기시키지 않게 bounded I/O·lifetime을 유지한다.

익명 handle을 별도 방식으로 명시 복제하는 다른 설계도 가능할 수 있지만, bootstrap 전달·최소 권한·process identity까지 새로 정해야 한다. 현재 작업은 위 named pipe 대안으로 바로 진행할 수 있으므로 재승인 대기를 만들 필요 없다. **상속 실패 시 비밀을 argv/env/임시파일로 폴백하지 않는다.**

## 검증과 번호

- 실제 Session0 서비스→비zero console session SYSTEM child→Winlogon에서 endpoint 인증·bounded job/result·자력 종료가 확인돼야 한다. 같은 session asInvoker helper PASS는 이 경계의 실증이 아니다. 현재 사용자 설치본/서비스를 무단 조작하지 않고 격리·승인된 실기 묶음에 포함한다.
- fake/선점 pipe, 다른 PID의 SYSTEM client, 다른 session, 부모 서비스가 아닌 server, 직접 `--lockcred-agent` 실행, stale topology/cancel, 부분 입력, child/UIA/IPC 정체·중간 종료를 거부/회수하고 비밀이 argv/env/log/file에 없음을 검사한다. 실제 sender/target process identity와 로그 session ID를 근거로 남긴다.
- 제안 번호 **LockCredV1=0x40000, control88, SecureUnlockKind5~8, UnlockStage13~15에는 이견 없다.** base5063cd9와 현재 편집의 범위에서 기존 값과 충돌하지 않는다. 새88→목적 고정 challenge→42~45 재사용은 목적 echo/상태 저장·KDF/AAD 분리·strict size/version 검사를 모두 유지해야 한다. 구형 peer에게88을 보내지 않고 기존 wire 의미를 바꾸지 않는다. 최종 고정 후보의 enum/크기/목적 교차 부정 시험으로 확인한다.

이 문서는 `codex_lockkeys_dxgi_r1.md` A 구현 계약4의 허용 구현/전달 경계 보완이다. 공개 문서/제품 코드/빌드·실기 변경을 Codex가 수행하지 않았으므로 이 검토서만으로 commit하지 않는다. 검증용은 기존 t-rs9nhsdv 정본에 통합하고 같은 담당에게 한 번 전달한다.
