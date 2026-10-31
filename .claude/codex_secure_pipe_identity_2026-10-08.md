# Secure input/unlock IPC — Codex 우선순위·보수 계약 (2026-10-08)

검증용의 신규 보안 발견을 **main b7c9164486de2772f32fa734492157a407b9fcd1(0.2.153)**과 prelogin984e157에서 소스로 대조했다.

**판정: P1 긴급 출하 보수. 작업용1의 별도 main 기반 task로 진행하고, 검수·통합 후 0.2.154 보안 릴리스를 우선한다. prelogin 게시의 필수 선행 조건으로 둔다.** 단 현재는 소스상 취약 경로를 확인한 단계이며 실제 사용자 비밀번호 탈취/현장 악용을 확인한 것은 아니다. 사용자 설치본/서비스/실 파이프에는 어떤 시험도 실행하지 않았다.

## 사실과 범위

- 0.2.153 `secure_input_service_main.cpp:404–407,509–513`의 두 공개 pipe 생성에 FIRST_PIPE_INSTANCE가 없다.
- `host_unlock_relay.cpp:83–96`은 CreateFile 성공 후 mode만 설정하고 반환한다. 응답의 magic/size/kind/requestId 검사는 있지만 **서버 신원 검사**는 아니다.
- `secure_input_broker.cpp:135–157`도 열린 handle을 곧바로 사용한다.
- `viewer_unlock.cpp:249–278`은 형식/상태를 확인한 challenge의 hostPub/salt/context로 password를 봉인한다. 따라서 로컬 fake unlock server가 선택한 유효 challenge를 relay가 전달하면, 암호화 자체는 fake의 키를 배제하지 못한다. 이 의미에서 검증용의 비밀번호 노출 경로 판정에 동의한다.
- 관리 파이프는 이미 최초 소유권 확보와 ServerIsInstalledService 검증을 수행한다. 이 안전 계약을 공통화할 근거가 있다.

이 문제는 **로컬 사용자 프로세스를 설치된 SYSTEM 서비스로 오인**하는 경계다. 사용자가 생략한 네트워크 TOFU/전체 transport 인증을 다시 필수화하는 작업과는 다르다. 이번 보수 후에도 기존 sealed v1의 네트워크 능동 MITM 한계를 해결했다고 보고하지 않는다.

prelogin의 새 `Windows 로그인` 자격은 worker의 별도 prelogin receiver에서 처리되므로, 그것까지 모두 GNLinkUnlock pipe를 통한다고 설명하지 않는다. 기존 잠금 해제 자격 경로와 secure input IPC가 이미 출하돼 있고 새 prelogin도 같은 서비스/입력 경계를 사용하므로, baseline 보수 없이 prelogin을 게시하지 않는 판단이다.

## 필수 보수 계약

### 1. 클라이언트 — 모든 새 handle을 사용 전에 검증

두 client 모두 **요청/입력/ChallengeRequest를 한 바이트라도 쓰기 전에** 서버를 확인한다. fake challenge를 읽고 나서 검증하거나, 암호문만 안전하다고 보내는 것은 부족하다.

- 그 pipe handle의 GetNamedPipeServerProcessId, SCM의 현재 `GNLinkSecureInput` RUNNING/PID, 등록된 서비스 executable과 실제 process image identity를 결합한다. process handle/생성 시각 등으로 PID 재사용·정지 교차를 다루고, 조회 실패는 Untrusted/Unavailable로 실패시킨다.
- 파일 이름만 맞음, SYSTEM이라는 사실만 맞음, 같은 SID, 설치 폴더 문자열만 맞음은 대체 인증이 아니다. 핵심은 **지금 설치된 서비스의 실제 실행 인스턴스가 이 pipe의 서버**라는 결합이다.
- 검증 성공은 **그 열린 handle의 수명**에만 유효하다. disconnect/broken pipe/재접속/서비스 재시작 때 새 handle을 다시 검증한다. 이름별 전역 trusted 캐시로 생략하지 않는다.
- 실패하면 client가 연 handle만 닫고 명시 오류를 반환한다. 이름이 같은 다른 pipe/미검증 서버로 자동 fallback하거나, 임의 server process를 종료하지 않는다.
- pipe open 시 `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION` 등 서비스가 실제로 필요한 최소 impersonation level을 명시한다. 기존 관리 파이프와 일관되게 caller 식별은 허용하되 불필요한 impersonation 권한을 상대에게 주지 않는다. 특정 권한 상승 exploit이 확인됐다는 주장은 하지 않는다. [CreateFile SQOS 계약](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).
- 신원 검증을 매 키 이벤트의 SCM round-trip으로 만들지 않는다. 이미 검증된 handle을 사용하고, 새 연결이 검증되지 않았으면 아직 입력을 전달하지 않는 구조로 한다. prelogin의 입력 연결 무대기/빠른 실패 보수와 통합 시 느린 재시도를 다시 넣지 않는다.

### 2. 서버 — 최초 이름 소유권과 서비스 수명 유지

- 서비스의 최초 pipe 생성은 FIRST_PIPE_INSTANCE로 선점을 탐지하고, 기존 SY/BA DACL·로컬 전용 제한을 유지한다. 다른 주체가 먼저 이름을 잡았으면 그 옆의 인스턴스로 조용히 합류하지 않고 오류를 알린다.
- 서비스가 정상 실행 중인 동안 이름 소유권에 빈틈이 없게 한다. 현재 관리 파이프가 같은 인스턴스를 유지하는 방식이 참조가 된다.
- **prelogin의 다중 인스턴스에 매번 FIRST_PIPE_INSTANCE를 붙이면 두 번째 정상 인스턴스도 실패한다.** 최초 소유권과 이후 자기 인스턴스 생성을 구분한다. 이름 유지를 위해 client가 연결할 수는 있지만 아무도 서비스하지 않는 유령 instance를 남기지도 않는다.
- Stop/재시작/생성 실패에서도 owner handle·연결 worker·진행 I/O를 정리한다. 이미 검토한 stop event/cancel/completion 수명을 보존한다. 이름 충돌을 서비스가 영원히 RUNNING으로 보이는 hang으로 만들지 않는다.

FIRST_PIPE_INSTANCE는 서버의 이름 선점 탐지이며, **서비스가 꺼져 있을 때 fake server에 연결하는 client의 인증을 대신하지 않는다**. 반대로 client 검증만 붙이고 서버가 선점된 이름에 합류하는 상태도 남기지 않는다. [CreateNamedPipe의 최초 인스턴스 계약](https://learn.microsoft.com/ko-kr/windows/win32/api/winbase/nf-winbase-createnamedpipea), [GetNamedPipeServerProcessId](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeserverprocessid).

### 3. 공통화와 실패 정책

main에는 prelogin_admin_pipe 구현 전체가 없으므로, 관리 파이프의 검증 아이디어를 작은 공통 trusted-service-pipe 모듈로 옮겨 두 client/서버에서 재사용하는 방향이 적합하다. prelogin 기능 전체를 보안 hotfix에 끌어오지 않는다. 테스트의 expected-service/pipe 대역과 출하 동작을 분리하고, 출하에서 환경변수/옵션으로 검증을 생략하는 경로는 만들지 않는다.

이름 선점·서버 불일치·SCM 조회 실패·실제 서비스 없음·busy/timeout을 기록상 구분한다. 이 발견을 서비스 상시 실행, pipe ACL의 광역 완화, 임의 프로세스 kill로 덮지 않는다. 외부 wire/crypto 형식 변경 없이 PC local IPC 범위의 보수로 진행할 수 있다.

## 격리 증거

실 `GNLinkUnlock`/`GNLinkSecureInput` 이름을 사용자 PC에서 선점하거나 저장된 사용자 자격을 사용하지 않는다. 시험 전용 pipe 이름·시험 소유 프로세스·합성 password marker만 사용한다.

1. **fake server 반례:** 서비스가 없거나 다른 PID일 때 별도 비신뢰 프로세스가 시험 이름을 먼저 소유한다. 제품과 같은 client 경로에서 candidate는 protocol/input bytes0으로 거절해야 한다. 인증을 제거한 대조는 fake로 합성 입력/요청이 넘어가야 한다.
2. **sealed 반례:** 가능하면 실제 relay/crypto 경로를 연결해 pre-fix fake challenge가 합성 marker를 열 수 있음을 확인한다. candidate는 fake challenge를 뷰어의 유효 서비스 challenge로 전달하지 않고 sealed payload도 보내지 않는다. 사용자 비밀번호·credential store는 전혀 읽지 않는다.
3. **정상 대조:** 올바른 서비스 identity fixture는 input 및 sealed 왕복을 수행한다. 모든 서버를 막아 통과한 시험이어서는 안 된다.
4. **재접속:** 진짜 연결 종료 후 같은 이름을 fake가 잡은 경우 거절, 실제 새 서비스 인스턴스 재시작 후 재검증 성공. 처음 검증한 trust가 다음 handle로 새지 않는다.
5. **서버 이름 수명:** fake가 먼저 있으면 서비스 생성 거절, 정상 서비스가 소유 중이면 비신뢰 선점 불가, 연결 교체 중 이름 소유권 유지, Stop 전후 유계 종료. prelogin 통합에서는 정상 두 client 동시 처리도 유지해야 한다.
6. **출하 gate:** server-check를 제거한 변이가 동일 행동 시험에서 실패하고, 정상 코드로 복원 후 고정 commit/바이너리 hash 및 검증용 독립 결과를 연결한다. SQOS와 신원 체크는 소스뿐 아니라 해당 connect 경로의 관찰값에 결합한다.

## 배정·릴리스 순서

검증용이 **작업용1, 현재 검수된 main 기준의 독립 worktree/task**로 위 계약을 한 번에 배정하는 데 동의한다. 0.2.154 예약/버전 변경·게시 담당은 검증용의 기존 역할대로 한다. 고정 보수 후보 → 검증용 독립 + Codex 검토 → main 통합 회귀 → 보안 릴리스 순서이며, 이 문서는 구현되지 않은 후보의 게시 go가 아니다.

작업용2의 r16은 기존 prelogin 보수를 계속할 수 있다. 다만 같은 `secure_input_service_main`/pipe factory를 양쪽에서 서로 다른 계약으로 고치지 않도록 검증용이 공통 interface/소유권을 연결한다. 보안 보수 main을 prelogin에 합친 뒤 **다중 instance·unlock caller binding·stop/재시작·fake server 반례**를 최종 통합 후보에서 다시 확인한다. main에서 통과한 단일-instance 증거만으로 prelogin의 다중-instance 통합을 승인하지 않는다.

## 984e157 추가 추적에 대한 회신

검증용의 **prelogin 자격 environment가 Challenge 발급 때가 아니라 Sealed 도착 때 잡힌다**는 지적은 직접 대조해 확인했다. `prelogin_cred_host.cpp::Challenge(:24–67)`에는 environment snapshot이 없고, Sealed(:127)에서 현재 environment를 잡아 Deliver에 넘긴다.

이것은 이전 Codex R15-2의 감시/왕복 세대 지적보다 더 직접적인 별도 창이다. **감시가 모든 전환을 잡아 generation이 달라져도, 이전 challenge에 대해 Sealed가 늦게 오면 그때의 새 environment를 기준으로 전달할 수 있다.** 이전 검토에서 이 발급 시점 결합 누락까지 짚지 못했다. R16에 다음을 R15-2와 함께 넣는다.

- 발급 시점의 desktop/session/허용 context를 challenge에 보관·결합한다.
- Sealed 수신/복호화 전 및 최종 전달에서 그 발급 context와 현재 context를 대조한다. Sealed 시점에 새 기준으로 재설정하지 않는다.
- **Challenge 발급→전환(감시가 확실히 관찰)→같은 이름 복귀→옛 Sealed 도착** 반례에서 입력0을 확인한다. 기존 'Deliver 대기 중 환경 변경'만의 시험과 구분한다.

UDP reader의 서비스 시작 대기, unlock의 단일 인스턴스 상주, createPipe 실패/agent write 종료 문제 등 검증용 나머지 추적은 동일 r16 묶음에서 역할·실제 반례와 함께 처리하면 된다. Codex가 작업용2에 별도 조각 지시를 보내지 않는다. 이 신규 IPC 신원 보수의 우선순위를 그 잔여들과 혼동하지 않는다.

직접 수행은 고정 source/계약 읽기와 이 미추적 검토서 작성이다. 빌드·실 IPC 선점·자격 획득·서비스/설치/프로세스 조작을 하지 않았으며 commit/push 없음.
