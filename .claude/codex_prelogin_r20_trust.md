# prelogin r20 — Q1/Q2 신뢰 경계 결정

`t-r639inm8 / r20 / kind=decision / reply_to=remote#pvbntb3z` · 2026-10-19

**양쪽 계약을 보존하는 다중 인스턴스 설계에 동의한다. 아래 보완을 포함해 통합을 진행한다. 구현·실행·게시 OK 판정은 아니다.**

검토 입력: `q_prelogin_r20_trust.md`, `task_prelogin_integ_r20.md`, 이전 `codex_prelogin_adce5d6.md`, 통합 worktree의 broker·trusted pipe helper·서버 루프·호출부. HEAD는 `37907a09bc3f98e5ffe051a9f64d8a6c4da76944`, merge② 편집 중이므로 고정 통합 후보로 취급하지 않았다. 기존 수용 한계 8건은 유지한다.

## Q1 — 모든 재접속에서 열린 핸들의 서버 신원 확인

- startup/빠른 경로 모두 같은 `OpenVerifiedServicePipe` 검증을 거친 핸들만 저장하고 사용한다. 실패·불명 모두 닫고 fail closed; raw open 폴백·이전 핸들의 신뢰 재사용 금지. SQOS IDENTIFICATION, 진단, cooldown, 불확실한 write 재전송 금지는 보존한다.
- **질문서의 `waitMs=0`은 채택하지 않는다.** 현재 helper는 이를 `NMPWAIT_USE_DEFAULT_WAIT`로 전달한다. 서버의 기본 timeout이 0이면 기본 대기는 50ms다. 확인한 편집본의 `OpenVerifiedLocked(1)`은 이 문제를 피하므로 허용한다. 이는 pipe 가용성 대기를 짧게 제한하는 것이며 SCM·파일 신원 확인을 포함한 전체 함수의 무대기/1ms 완료 보장은 아니다. 빠른 재접속·busy·신원 실패의 실제 지연을 기록하고, 주석도 그 범위대로 적는다. 엄밀한 무대기가 필요하면 같은 검증 helper 안에서 WaitNamedPipe만 생략하는 명시 경로를 사용한다; 검증 자체를 생략하지 않는다.
- 제품 pipe 이름의 단일 출처는 `pipeName_`으로 통합해도 된다. 시험 훅이 별도로 있으면 두 경로 모두 동일한 시험 이름·gatherer·서비스 이름을 사용해야 한다. 제품에서 활성화 가능한 검증 우회 옵션을 추가하지 않는다.

## Q2 — 다중 연결 + 이름 소유권 유지 + 상한에서 정상 동작

- 다중 클라이언트는 유지한다. 첫 인스턴스만 FIRST_PIPE_INSTANCE; 첫 생성 실패 후 플래그를 빼서 재시도하지 않는다. secure-input 제공 실패를 명확히 진단하고 기존 서비스 실패 경로로 연결한다. SCM RUNNING 표시만으로 입력 제공 성공이라고 보고하지 않는다.
- 정상 인계는 **다음 대기 인스턴스 확보 → 연결 처리 스레드 시작** 순서다. connect 실패 때는 같은 소유 핸들을 Disconnect 후 재사용한다. SY/BA DACL, REJECT_REMOTE, 연결별 requester/console binding, overlapped 취소·STOP·join 계약은 보존한다. SY/BA DACL의 방어 범위는 권한 없는 로컬 프로세스이며 관리자/SYSTEM에 대한 배타성으로 설명하지 않는다.
- **작은 상한에 동의하되 상수만 8로 바꾸면 안 된다.** 현재 루프는 `createPipe(false)` 실패 시 종료되고 `registry->Stop()`으로 기존 연결도 끊는다. 정상 포화가 전체 입력 중단으로 바뀌면 안 된다.
- 이번 선택은 **총 인스턴스 8 = 최대 처리 연결 7 + 예약 listener 1**이다. 모든 CreateNamedPipe 호출에 같은 상한을 사용한다. 7개가 처리 중이면 추가 연결은 listener 핸들을 보유한 채 Disconnect/rearm하고 제한 도달을 진단한다. 정상 포화에서는 기존 연결을 끊거나 서버를 종료하지 않는다. 슬롯이 반환되면 새 연결을 다시 수용한다. 완료된 연결의 회수와 실제 핸들 수를 기준으로 판정한다.
- listener 생성의 실제 오류도 소유 핸들부터 닫은 뒤 일반 생성으로 복구하지 않는다. 현재 소유권을 유지한 제한 재시도 또는 명시적인 제공 중단/종료로 처리한다. 종료 전까지는 소유 핸들을 유지하고, 다음 서버 시작은 다시 FIRST로 선점 여부를 확인한다. STOP 경로의 I/O 취소·종료 가능성을 깨는 무한 재시도/바쁜 루프는 금지한다.

## 통합 후보에서 필요한 근거

1. 이름 선점 상태에서 첫 생성 실패, 제공 0, FIRST 없는 재시도 0. 빠른 재접속의 가짜 서버에도 입력 bytes 0, 검증 호출·cooldown 확인. 검증을 제거한 부정 대조는 실패해야 한다.
2. 이전 연결을 유지한 채 새 console/worker 연결의 입력이 도달하는 다중 연결 시험. 실제 OS 빠른 사용자 전환 시험과 격리 fixture의 연결별 binding 시험은 구분해서 기록한다.
3. 상한 도달 → 추가 연결 거부 → 기존 연결 입력 지속 → 슬롯 반환 → 새 연결 성공. 반복 연결/즉시 종료·connect 실패에서도 listener 소유권이 이어지고 STOP이 끝나는지 확인한다. 이 시험을 위해 사용 중인 제품/서비스/desktop을 조작하지 않는다.

검증용이 이 결정을 기존 r20 지시서에 취합해 작업용1에 전달한다. 같은 task에서 구현·자체 실행 → 고정 후보 검증용 독립 검사 → Codex 통합 검토로 연결한다. push·게시·버전 인상·설치·재부팅은 이 결정의 범위가 아니다.

## 근거와 검증 한계

Microsoft 문서: [WaitNamedPipeW](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-waitnamedpipew), [CreateNamedPipeW](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createnamedpipew). 0의 기본 대기 의미, 서버 timeout 0의 50ms 기본값, 인스턴스별 동일 상한 요구를 확인했다. 상한에서 기존 루프가 종료된다는 판단은 해당 소스 흐름에 대한 검토다.

직접 수행은 문서·소스 읽기와 편집 중 파일의 상태/해시 확인이다. 빌드·실 서비스·UI·빠른 사용자 전환은 실행하지 않았다. 제품/공개 문서 변경 없이 검토서만 남기므로 commit/push 없음. 구현 기록은 기존 기능 단위 history/계획/커밋에 작업용이 취합한다.

읽은 시점 파일 SHA256 (이후 편집 가능):

- broker.cpp: `47D70B318B44B0F10EF2C663426C4E2C5E86C1CFA2A68026482F3C42D0DC4083`
- secure_input_pipe_server.hpp: `8A44DB81A59E7F8B40004C43712EDAA73CA4D0B2ECC8901C849DA8F863B35B1F`
- trusted_service_pipe.cpp: `E8EB99AD8FF8D4250F68D69319B184472E7129BB5B36C70579EA3D3619071794`
