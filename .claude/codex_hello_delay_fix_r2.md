# hello-delay-fix r2 — Codex SOURCE/EVIDENCE 판정

2026-10-22 / task **t-q4eq9aiz** / 후보 **6e89962d7c21adc9f74756e829584bff821f41d9** (제품bcb6a46, 시험3e625d8, 문서6e89962).

**SOURCE / EVIDENCE OK — N1/N2 보수 검토를 닫는다.** 현재 고정 빌드의 시험 코드 분리도 아래 생성 프로젝트 대조로 수용한다. 추가 제품 보수나 동일 시험 반복 요구는 없다. 다만 **회귀 시험 실패의 qemu 원인 귀속은 철회·기록 정정**해야 한다. 전체 회귀 PASS/실 LAN·Android 실기 성공/통합·일반 릴리스 승인은 아니다.

## N1/N2 확인

- **N1 OK:** 512 reads는 batch 경계다. full batch 뒤 최대10ms 대기하고 **같은 roundEnd**까지 계속 읽는다. 다음 Hello로 넘어가지 않는다. 매 batch/수신 전 stop·시한 확인, 기존 ACK/Auth 식과 전체 budget 및 라운드 뒤 pause가 유지된다. 주석과 fullBatches 관측도 실제 동작과 일치한다.
- **N2 OK:** PunchAny 로컬 분류에 WSAENETRESET만 기존 계속 집합에 추가했다. TTL advisory 뒤 다른 응답 후보는 선택하고 NOTSOCK은 실패한다. 전역 classify_udp_recv/native_socket.hpp는 기준판 이후 변경이 없으며 connected 세션 정책을 바꾸지 않았다. 실제 TTL 오류가 아니라 test seam 공급을 통한 검증이라는 한계도 명확하다.

## 직접 확인한 근거

- 작업용 `.claude/report_hello_delay_fix_r2.md`, 독립 `C:/work/gnlink-wt/verify-hello-r2/.verify/verify_hello_r2.md`, 제품/시험/CMake diff·원문을 읽었다. 구현 HEAD/tracked clean·CR-at-EOL 허용 diff-check 확인.
- 독립 build-vs hash 직접 일치:
  - Viewer `9088e8fc27d302162d0cd64b44e409615669c24d86d98aae82a404662fda714f`
  - Stream `d69988fab2a4fe375b67c6e62b97e7efe1df7ac23d78e19f07251474cb1d1d08`
  - Client `66f80c4abbc0227f5ecacfdab9d86660edb6d8e4d1470c9af5aea4301a1bafbd`.
- 원본 hello28×2·punch_any×2·hello_stats14·udp_recv_class17·gate9/self16 확인. torrent의 fullBatches52/65·Hello 각4는 cap 실제 도달 전제를 충족한다.2000개 뒤 ACK50ms/attempt1, stop약310ms, TTL13회 뒤 후보203~205ms·NOTSOCK10038 및1.5초10round도 확인했다.
- **독립 변이 유효:** batch 변이는 cap 경로를 실제 타고 PC17/Android305 Hello·ACK4round로3FAIL, TTL 변이는10052로 조기 포기해6FAIL. rig 충돌이나 전제 미충족을 결함 검출로 세지 않았다.
- 재현 후보 첫 연결+재연결8회265~535ms·3초 초과0은 검증용 기록 귀속. 사슬 원문에서 첫533ms/첫 재연결269ms, attempts2·badAcks1·recvErrors0→`restored target=desktop gen=4`를 직접 대조했다. r1에서 이미 확보한 긴 유지·짧은 유지 대조와 함께 효과를 수용한다. 중앙값의 두 봉우리 비율만으로 r1 대비 회귀/향상을 일반화하지 않는다.
- Android4ABI native 빌드와 일부 영향 e2e는 작업용 인용이다. Android 옵션250/0의 생산 루프 경계 반례는 검증용 Windows 실행이다. 실제 Android/POSIX/실 LAN/NAT·회사 PC 검증과 구분한다. Codex가 제품 빌드·시험을 재실행한 것은 아니다.

## 판단 요청 세 건

### ① viewer_udp_recovery 실패 — ledger 유지, qemu 귀속 철회

후보/기준판이 같은 세 처리량 assertion에서 비슷하게 실패했다는 A/B를 수용한다. 이것은 **이번 hello 변경이 원인이라는 근거가 없다**는 판단에 도움이 되지만, 시험 전체가 통과했거나 실패 원인이 환경이라고 확정한 것은 아니다.

qemu0.15core/s·전체CPU3~4%라는 독립 관측은 작업용의 “qemu CPU 부하 탓” 설명을 뒷받침하지 않는다. 보고와 **history.md의 동일 귀속 문구를 정정**한다: 같은 조건의 기준/후보3FAIL, 원인 미확정, qemu는 실행 중이었으나 부하 원인은 증명 안 됨. 단일 코어 정체/타이머·페이싱·MF 입력 속도 가능성도 확인 사실로 쓰지 않는다.

**viewer recovery 처리량/fixture 신뢰성 후보로 ledger에 올리고 다음 담당을 연결**한다. 앞서 directory_retry cached-silent 문구 간헐과도 원인을 합치지 않는다. 관련 상태/시간/전송 프레임을 제한해서 조사하되 전체 suite를 성공할 때까지 반복하거나 assertion 숫자를 낮춰 PASS를 만들지 않는다. 이 hello 코드의 개선 판정과 실패한 전체 회귀 결과는 분리해 보고한다. 통합 판정에서도 그 미해결 항목을 숨기지 않는다.

### ② gate9가 새 매크로를 보지 않음 — 현재 빌드의 별도 분리 근거 수용

**gate9/self16을 이 세 매크로의 누출 방지 증거로 쓰지 않는다.** 별도 근거는 다음이다.

- CMake 정의는 시험 target3개에만 있다. 생성된 모든 vcxproj를 직접 검색해 매크로가 있는 프로젝트가 `remote60_punch_any_test`, `remote60_punch_any_mutant_netreset_test`, `remote60_hello_backlog_mutant_batch_test` 세 개뿐임을 확인했다.
- 출하 Viewer 정의에는 없고, Stream/Client 및 참조 프로젝트는 변경 cpp를 컴파일하지 않는다는 검증용 대조를 함께 수용한다. 시험 양성은 실제 변이 target 실행·기대 FAIL로 확인됐다.

이는 **현재 source/build 설정과 hash로 고정한 후보의 부재 근거**다. gate 스크립트가 앞으로 잘못된 compile define까지 자동으로 잡는다고 확대하지 않는다. 이번 후보를 이 이유로 다시 제품 보수에 돌려보내지는 않는다. **통합 고정 빌드에서도** 각 출하 target의 이 금지 매크로 부재·시험 target 양성·관련 산출물 선택을 분리 확인하고 기록한다(생성 프로젝트/실제 compile command 기반으로 자동화할 수 있다). 전처리에서 사라지는 매크로 이름을 exe에서 문자열 검색하는 것만으로 증명했다고 하지 않는다. LK/DW/SH의 기존 artifact marker gate 합집합은 별도로 유지한다.

### ③ 제품 소스의 변이 #if — 유지 허용

**유지해도 된다.** 같은 생산 루프의 기존 결함을 되살려 검증용이 소스 수정 없이 실행하는 목적이고, 정의가 시험 target에만 있어 출하 코드에 포함되지 않는다. 제품 cpp에 시험용 텍스트가 있다는 이유만으로 별도 파일 복제/루프 복제/삭제 리팩터링을 요구하지 않는다. runtime/env로 켤 수 있는 출하 토글로 바꾸지 않고, 위 compile 정의의 분리를 유지한다.

## 다음 담당·잔여 상태

검증용은 **hello N1/N2 자동 보수 검토를 닫고6e89962를 통합 검토 입력으로 고정**한다. 작업용의 qemu 귀속 정정과 ledger 연결은 필수지만 문구 때문에 제품 재검토 왕복을 만들 필요는 없다. 같은 기능/통합 기록에 취합하고, 문서만 바뀐 최종 commit/tree와 제품 소스 불변 여부를 메타로 연결하면 된다. 사고 기록/남은 inv·verify worktree/junction·증거 보존도 유지한다.

LK e728485·DW fc4cd57·SH c494dc6와 각각 고정 commit을 통합하고, 새 build commit/hash·공통 gate/compile 정의·영향 있는 실제 UI/재접속 검증을 독립 확인해 Codex 최종 통합 검토로 잇는다. 현재 회귀 ledger·SH control 실패·S4 AVD 미달·현장 미검증은 여전히 별도 잔여다. 버전·서명·게시·사용자 설치·push를 이 판정만으로 허가하지 않는다.

사용자가 지금 할 일: 없음. Codex 변경은 미추적 판정서/인계뿐으로 제품 수정·빌드·테스트 실행·설치/서비스 조작·commit·push 없음.
