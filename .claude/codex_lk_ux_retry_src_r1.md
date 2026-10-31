# LK-UX retry r1 — SOURCE 검토

2026-10-26 / 검증용 remote#yj8mrmrx source-review 요청 / 고정 후보 **9991b3687e552a606990fe2a73a70f5a60b48f2a** / base **82b3d14**.

**SOURCE: NEEDS_CHANGES — N1/N2 한 묶음. EVIDENCE는 R1 통합 후보 독립 검사에서 후속 판정한다.** 아래는 소스의 가능한 실행 순서를 대조한 결함이며 Codex가 새 실행 시험으로 재현했다고 주장하지 않는다. 제품 수정·빌드·설치·서비스 조작은 하지 않았다.

보고 `report_lk_ux_retry_r1.md` 전문, 구속 `codex_field_0162_0231.md` §2·§3·§5/`task_lk_ux_retry_r1.md`, base→candidate 제품 diff17파일 및 관련 원문을 읽었다. worktree HEAD=9991b36/ tracked clean을 확인했다. 번호89/pong0x80000/stage17·18/IPC9/feature0x2는 **검증용 확정 배정으로 수용**한다. UA-2의90/0x100000 예약과 통합 때 다시 합집합 검사한다.

## N1 (P1) — Sync timeout은 끝났지만 retry 작업의 실행권은 남는다

위치: `host_unlock_relay.cpp:798~832`, 특히 **828~829**; `Command`와 `WorkerLoop:425~494`의 supersede→새 challenge 단계.

`LockCredRetryChallengeSync`는 새 key의 liveTx/명령을 등록하고 wait_for(timeoutMs) 뒤 **!got이면 false만 반환**한다. 큐 취소·tx invalidation·명령의 절대 만료 시각 전달은 없다. Host는 이를 18(SupersedeRefused)로 보내 '이전 대기를 끝내지 못했고 아무것도 새로 보내지 않았다'고 안내한다. 그러나 Worker는 caller timeout을 모르며, supersede가 나중에 성공하면 계속 ordinary LockCredChallenge로 전환한다.

가능한 순서(소스 추론):

1. A가 typed 대기 중이고 retry B가 큐에 들어간다.
2. 큐 지연 또는 supersede 응답 지연 동안 Sync timeout이 끝나 Host→Viewer18, Viewer는 A를 계속 따른다.
3. Worker가 뒤늦게 B를 처리/재개해 A를 supersede하고 **새 B challenge를 발급·hold**한다. 이 결과는 이미 끝난 Sync waiter의 cache에 남는다.

현재 caller 기본3초와 각 IPC shortDeadline3초는 하나의 deadline이 아니다. supersede+새 challenge가 각각 budget을 쓰고 queue wait도 있어 실제 기본값에서도 가능한 경계다. 테스트의 짧은 IPC 시한이 caller wait3초보다 먼저 끝나는 silent-service 대조는 이 순서를 검증하지 않는다. 비밀번호가 자동 재입력된다는 뜻은 아니지만, **보고한18 뒤에 권한 있는 상태 변경/새 challenge hold가 생기고 다른 Host를 막을 수 있는 수명 결함**이다.

요구:

- retry 전체(대기 큐·supersede·fresh challenge)에 **같은 절대 operation deadline/취소 상태**를 연결한다. timeout이면 아직 시작하지 않은 명령을 취소하고 새 attempt의 liveTx를 회수한다. 각 외부 IPC/상태 전환 직전 같은 operation/owner가 살아 있고 시한 안인지 검증한다. `IsLiveLocked`가 Sealed/Status에만 있는 현재 사전 검사에 기대지 않는다.
- supersede가 이미 실행돼 되돌릴 수 없는 경우는 사실대로 별도 종결한다. **옛 A가 실제로 끝났다는 terminal tombstone은 A의 올바른 generation에 남겨 기존 poll을 해소**할 수 있지만, 만료된 B를 위해 새 challenge/hold를 만들거나 늦은 결과를 새 시도로 승격하지 않는다. timeout/cancel이 Windows 자체 인증을 취소했다는 주장도 하지 않는다.
- EndSession/owner 변경과 supersede 완료→새 challenge 사이도 같은 실행권 fence에 포함한다. IPC/worker 시한을 단순히 늘려 피하지 않는다.

추가할 결정적 대조: ① worker가 retry를 받기 전 caller 만료→해제 뒤 supersede/challenge0·A 보존 ② supersede 성공 응답을 caller 만료 뒤 해제→fresh challenge0·B hold0·A의 실제 terminal만 전달 ③ supersede 종료 직후 EndSession→새 challenge0/새 cache0. latch/barrier로 순서를 고정하고 callback을 해제한 **뒤**의 counter/파이프 반환까지 검사한다. 단순 'timeout 안에18 반환'만 보면 부족하다.

## N2 (P2) — old wait 종료 사실이 새 암호 봉인 성공 여부에 묶여 있다

위치: `viewer_unlock.cpp:400~453`의 89 challenge 처리/로컬 봉인 실패, `run_lock_cred_exchange:558~568`, `viewer_control_client.cpp:842~851, 875~886`.

Host가 A를 supersede하고 B의 유효한 새 challenge를 보낸 뒤에도, 클라이언트에서 password 길이/키 생성/랜덤/봉인 등이 실패하거나 B seal/accepted/status 응답을 끝까지 받지 못하면 `sealed_exchange`는 false, stage=0을 반환한다. 뷰어는 'stage0=이전 종료 미확정'으로 A를 계속 poll한다. 하지만 이 경우 **A의 종료는 새 challenge를 받은 시점에 이미 확정**됐다.

A poll의 cached Superseded17을 받으면 현재 루프가 SessionUnlocked가 아닌 모든 terminal을 UnlockUnconfirmed15로 바꾸어, 수 초 전에 사용자가 명시 재시도한 일을 **'2분 안에 해제가 확인되지 않았음'**으로 안내한다. 이는 이전 요청 종료와 새 전송 실패가 섞인 사용자 상태·문구 오류다. N1을 고쳐도 남는 별도 경로다.

요구:

- **이전 요청의 종료 확인**과 **새 요청의 봉인/전달/typed 결과**를 별도 결과로 추적한다. 예: 같은 requestId/purpose의 유효한 retry ChallengeIssued를 받으면 priorEnded를 확정하고, 뒤의 새 전송 실패가 그 사실을 지우지 않게 한다. 근거 없이 stage!=0/18 이외라는 이유만으로 종료를 추정하는 범용 규칙으로 확대하지 않는다.
- 진짜 종료 미확정인18/무응답은 A를 유지한다. A 종료가 확인됐으면 A poll을 정리하고 B의 미전송/결과 불명 상태를 사실대로 안내한다. B가 typed될 가능성이 있는데 결과만 못 받은 경우와, 로컬 봉인 실패로 B seal 자체가0인 경우도 구분한다.
- 명시 supersede17을 실제 timeout15로 바꿔 '2분'을 말하지 않는다. 늦은 A 결과가 B의 새 prompt/최종 안내를 덮지 못하게 한다.

결정적 대조: A typed→89 성공/A17+B ChallengeIssued→**봉인 전 실패** 주입(출하 제외)→A pending0/B seal0/정확한 새 입력 실패 안내·2분timeout 안내0. 추가로 종료 확인 전18/무응답은 A를 보존하는 대조를 유지한다. 이 변경을 위해 새 wire를 무조건 추가할 필요는 없고 helper의 단계별 결과 전달로 해결할 수 있다.

## 소스로 수용한 부분 — 위 결함과 분리

- 서비스 `HandleLockCredSupersede`는 이 pipe의 `lockCredRequestId+clientSessionCookie`가 일치하는 typed 대기만 끝낸다. typed 함수가 끝나기 전에는 같은 pipe의 다음 요청을 처리하지 않는 직렬 구조다. 종료17은 GNLink 대기 종료이고 Windows 인증 취소가 아니다.
- relay는 heldCookie/requestId와 liveTx의 heldTxGen을 비교하고, 같은 세션의 **다른** typed 대기가 있으면18로 거절한다. 다른 control 세션의 held transaction은 Busy 처리하고 supersede를 보내지 않는다.
- supersede 성공 terminal을 oldKey에 캐시하고 old status 재질문을 중단한 뒤 새 LockCred challenge를 요청하는 **정상 경로 순서**는 계약과 일치한다. 명확한 service nonterminal/거절에서는 새 challenge를 보내지 않는 branch도 있다.
- service 새 challenge는 기존 console/active/lock/account/topology/expiry 검증을 다시 거치고, sealed request는 새 requestId/context와 purpose별 KDF/AAD로 검증한다. 옛 seal은 새 context의 requestId와 다르면 no-cache 거절해 새 challenge를 오염시키지 않는다. 비밀번호·sealed 원문 로그/argv 추가와 자동 Enter는 새 diff에서 발견하지 않았다.
- viewer UI는 retry capability가 없으면89를 보내지 않는다. host/service 지원 거절 preflight는18을 반환한다. 새 서비스 capability feature0x2/host pong0x80000은 기존 크기의 flags/features에 추가하고 고정 struct를 확장하지 않았다. prelogin allowlist에89를 열지 않았다.
- 'Windows 오류 메시지는 원격 화면에서 닫은 뒤 다시 입력' 안내와 가변 높이 결과 창은 소스 방향 수용. 실제 화면·DPI/줄바꿈 표시 증거 판정은 통합 EVIDENCE에서 한다.

## 보류·통합 검사 범위

§3의 LogonUI 인증 실패 자동 감지는 근거 없음으로 **별도 보류 수용**한다. 이번 필수 §2 재시도를 그 관찰 helper 구현에 묶어 지연시키지 않는다. '해제 직후 재시도→not locked 안내' 경미 한계도 보고된 대로 남기며 이번 source 보수 범위를 별도 자동 unlock 추측으로 키우지 않는다.

입력 도중 cancel이 service pipe에서 끼어들 수 없다는 설명은 소스 직렬화 근거로 수용하되, 실제 native child/commit fence 실행을 증명한 것으로 쓰지 않는다. 통합 독립 실행에서는 새 topology/owner 변화가 retry의 fresh challenge/최종 입력을 막고, old status/seal/취소가 새 입력을 오염시키지 않는 경계를 포함한다.

검증용이 고정 R1 통합 해시로 LK/PS/UA/SH 행동 검사를 한 번 수행하는 계획은 수용한다. **지금 독립 실행이 없다는 것 자체를 새 SOURCE 결함으로 세지 않는다.** 이번 보고의57/79/28/41·72/57·변이는 작업용 실행 인용이며 Codex/검증용 독립 PASS로 바꾸지 않는다. 출하 define/byte gate 합집합과 새 protocol 번호 충돌은 통합 빌드에서도 확인한다.

다음 담당: 검증용이 **기존 LK-UX task**에 N1/N2를 한 묶음으로 전달→작업용1 보수/좁은 반례→고정 새 commit·변경 보고. 그 diff로 SOURCE 재확인하고 EVIDENCE는 예정한 통합 검사에 귀속한다. PS/UA·SH의 독립 작업/정본 연결은 계속하며 같은 기능의 중복 task나 지금 전량 UI/GPU 재실행은 요구하지 않는다. 번호 주석의 'PROPOSED'는 이미 검증용 확정이므로 관련 commit에서 메타 정정하면 된다. Codex 변경은 미추적 검토서/인계뿐으로 별도 제품 commit/push는 없다.
