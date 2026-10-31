# 업데이트 pre-swap 포기(AbandonedBeforeSwap) — 2026-09-21 17:15 집 호스트 0.2.131→0.2.132
발주 remote_claude. 근거: 이 PC `C:\Program Files\GNLink.update\updater.log`, `%LOCALAPPDATA%\GNLink\host_app.log`, 소스 audit/host-pc-recovery(c261d11).

## 확정 관측
- 17:15:52 updater 시작(installed=0.2.131). 17:15:53.423 "the waiting caller acknowledged and is standing down"(실행 중 Host 가 인계 수락·자진 종료). 17:15:53.512 **result: AbandonedBeforeSwap -- product did not reach a safe point / effects: could not ask pid 27568 to stop**. version verified: 0.2.132.
- **스왑 안 일어남**: 설치본 전부 0.2.131 그대로(GNLinkHost 05d41cf4=0.2.131). Host/Stream/InputService 정상 실행 중. **안전 실패**(파일 무변경). 우리가 고친 헬스 게이트는 **도달조차 못 함**.
- pid 27568 은 현재 프로세스 목록에 없음(당시 종료됨).
- **장기 패턴**: updater.log 에 AbandonedBeforeSwap "could not ask pid X to stop" 이 09-10부터 **23회**. 헬스 timeout(6)·RollbackFailed(4)와 별개의, 더 빈번한 실패 모드.

## 코드 위치·유력 원인
- `update_effects.cpp:592` `if (!config_.requestStop(target))` → :606 `lastError_="could not ask pid N to stop"` → state machine 이 AbandonedBeforeSwap 로.
- 주석 :598-599: *"already destroyed, the request failed ... 'could not ask pid 7332 to stop' about a process that was doing exactly what had just [been asked]"* → **레이스 가설**: 인계로 Host(및 자식)가 자진 종료 중인데, requestStop 이 이미 사라진 pid 에 대해 false 를 반환하고, 그것을 정지 실패로 세어 포기. "이미 종료됨" 을 "정지 성공" 이 아니라 "정지 실패" 로 취급하는 것으로 보임.
- 이번 수정(c261d11까지)은 헬스 게이트+D2 롤백만 손댔고 **이 forward pre-swap quiesce 는 미변경**. 즉 0.2.132 updater 도 같은 레이스를 가질 가능성.

## 미확정
- pid 27568 의 정체(구 Host/Stream/기타)와, requestStop 이 false 를 반환한 정확한 조건(이미 종료 vs 창/콘솔 없음 vs 권한). 소스로 확정 필요.
- "이미 종료 = 정지됨" 으로 고치면 스왑이 진행되는지, 그때 헬스/롤백 경로가 우리 수정으로 안전한지.

## 배포 딜레마
- 인앱 업데이트가 pre-swap 에서 포기하므로 **0.2.132 를 인앱으로 못 받는다**. 새 updater 수정도 인앱으로 전달 불가(구 updater 가 스왑을 못 끝냄).
- 즉시 실기 경로: **대화형 설치 GNLinkSetup.exe(0.2.132)** — 인앱 updater 우회, 설치 관리자가 전체 정지·설치. UAC 필요(사용자). (메모리: 대화형 설치 권장, /S 는 Host 재기동 안 함.)

## Phase 1 결과 — 기전 **확정** (작업용, 측정 근거, 2026-09-21)

### 경로 확정
`update_effects.cpp:592 if (!config_.requestStop(target))` → `:606 lastError_="could not ask pid N
to stop"` → 상태기계가 AbandonedBeforeSwap. :605 에 이미 **관용 분기**가 있다:
`if (parent_has_exited(target.pid)) continue;` — 이름과 달리 "이 pid 가 이미 사라졌나" 를 묻는다.
**즉 "이미 종료 = 정지 성공" 은 이미 구현돼 있다.** 부족한 것은 그 앞 상태다.

### 확정된 조건 — "종료 중" 은 관용되지 않는다
`request_process_stop` 이 false 를 반환하는 경로는 둘뿐이다:
(a) `OpenProcess` 가 ACCESS_DENIED, (b) **창이 없고** `GenerateConsoleCtrlEvent` 도 실패.

측정(`remote60_update_stop_process_test`, 창을 만들었다가 **파괴하고 살아 있는** 픽스처):
- 파괴 후 열거에 **창 없음** 확인.
- 🔴 **호출자에 콘솔이 있으면** `GenerateConsoleCtrlEvent` 가 **성공**해서 ask 가 true 가 된다.
  → 내 첫 재현은 아무것도 측정하지 못했다(테스트가 콘솔 프로그램이라). **가설이 측정에 반박당했고
  픽스처를 고쳤다.**
- **콘솔 없는 호출자**(DETACHED_PROCESS 자식 = updater 의 상태)에서 다시 재니:
  `console fallback failed` / `request_process_stop returned false`.
- 그 false 를 seam 으로 주입하면 `PrepareForSwap` 이 **정확히 현장 문구**로 포기한다:
  `could not ask pid <pid> to stop`.

→ **확정**: 인계를 수락한 프로세스가 창을 파괴하고 아직 종료되지 않은 수십 ms 동안, 콘솔 없는
updater 의 ask 는 실패하고, 그 실패가 곧 AbandonedBeforeSwap 이다. 현장 타임라인과 일치한다
(17:15:53.423 "standing down" → .512 abandoned, **89ms**).

### 미확정으로 남기는 것
- **pid 27568 의 정체.** updater.log 에만 나오고 host_app.log·client.log 어디에도 없다.
  같은 초의 host_app.log 는 `standing down -- child pid 3568 exited` 와 `WM_DESTROY pid=29008` 을
  보여 주므로 27568 은 그 둘도, 새로 뜬 자식 12988 도 아니다. 로그만으로는 확정 불가.
- ACCESS_DENIED 경로가 이번 건의 원인일 가능성은 **배제하지 못했다**(updater 는 승격이라 가능성은
  낮다고 보지만 측정 안 했다).
- 23회 전부가 같은 기전인지는 **확정 아님**. 확정 범위는 "이 기전이 이 문구를 낸다" 까지다.

## Phase 1 결과 (작업용, 커밋 2c92adc) — 측정 확정
- `PrepareForSwap` 는 GONE 상태(`parent_has_exited(pid)`)는 봐주지만 **창을 destroy 하고 아직 exit 안 한 중간 상태**는 안 봐준다. 인계 수락(handoff ack)이 정확히 그 상태를 만든다(호스트가 창 닫고 standing down, exit 은 아직).
- 창 없는 대상에 `request_process_stop` 은 `GenerateConsoleCtrlEvent` 로 폴백하는데, **호출자의 콘솔**이 관건. 업데이터는 DETACHED_PROCESS(콘솔 없음)라 폴백이 실패 → false → "could not ask pid N to stop" → AbandonedBeforeSwap.
- 작업용 첫 측정 정정: 콘솔 프로그램에서 물으면 성공(폴백이 콘솔을 가짐)이라 아무것도 측정 못 했음. **DETACHED_PROCESS 자식**에서 다시 측정하니 필드 메시지 재현. seam 으로 false 주입 시 정확히 동일 결과.
- → 수정 방향(제안): handoff 로 자진 종료 중인 프로세스를 "정지 실패" 로 즉단하지 말고, **GONE 과 동일하게 취급하거나 bounded wait 로 실제 exit 을 기다린다**(D2 quiescence 철학과 정합). 예산 내 exit 안 하면 그때 abandon.

## Codex 검토 r1 (remote_codex, seq 2694) — 기전 지지, 수정 강화 요구
- 확정 범위: "창 소멸·프로세스 생존·DETACHED 호출자에서 정지 요청 실패" 기전까지. pid27568(알려진 Host29008/자식3568과 다름)·ACCESS_DENIED 미배제라 **이번 89ms 사건과 23회 전체의 최종 원인으로는 확정 불가.**
- **bounded wait 만 동의. "종료 중=GONE 동일취급"으로 종료 확인 생략은 반대.** 요청 성공·창 없음·handoff ack 어느 것도 종료 증거 아님.
- 🔴 **PID 재조회로 GONE 판정 금지(재사용 경합).** 열거 시 이미지/소유/PID+생성시각 검증하고 **확보한 process handle 로 실제 exit 대기**. ACCESS_DENIED/WAIT_FAILED/WAIT_TIMEOUT 은 종료 아님.
- 자진종료 추정 불필요 — **검증된 대상이 총 quiescence deadline 내 실제 종료되면 진행**. ack 는 동일 attempt 의 확인된 caller 에만 연계, 다른 대상까지 "종료 중" 일반화 금지. 끝까지 생존이면 abandon, 강제 kill 승격 금지.
- requestStop 실패는 "요청 전달 실패"로 기록하고 대기 가능 대상은 공통 Quiesce 로. **총 deadline**(프로세스 수만큼 예산 늘지 않게). **정확한 자식+SCM 서비스 모두 종료 확인 후에만 Swap.**
- 🔴 **통합 경로 회귀**: DETACHED fixture 로 실제 request_process_stop→Prepare→Quiesce→Swap/abandon 을 한 경로로. 실 API 실패와 seam false 주입은 각각 부분 증거일 뿐 통합 완주 증거 아님.
- 반례: 창파괴 후 지연 exit=진행 / 영구생존·권한거부·대기실패=Swap0 / 기종료·PID재사용·복수대상·예산소진 / abandon 후 재기동은 기존 부모 최종 생존·종료 확인(중복 Host/모두 사라짐 방지).
- 헬스 게이트와 별개 동의. **이번 실행 주체는 0.2.131 Updater** — 설치 updater 해시 고정, "게시한 신 updater 가 이번 실패를 냈다"고 섞지 말 것.
- "인앱으로 절대 못 받는다" 단정 철회(간헐). 전달 위험은 남으니 반복 재시도 유도 대신 검증된 bootstrap/버전 조합으로 정상·실패 경로 시험.

## Codex 검토 r2 (remote_codex, seq 2699) — NEEDS_CHANGES (823bc91 고정 검토)
내 통합회귀 92/0 은 통과했으나, Codex 가 소스를 보니 테스트가 아래 결함을 실제로 안 덮음. 재개정 필요:
- 🔴 **D-A PID 재조회 미폐기**: Prepare 에 `parent_has_exited(target.pid)` 잔존, watch 를 실패 요청 뒤에만 열고, Quiesce 도 PID 로 다시 OpenProcess. **검증된 동일 handle 을 열거/정지요청 전부터 최종 대기까지 보유**하도록 계약 통일.
- 🔴 **D-B 안전결함(신원)**: `process_identity_matches` 가 조회 실패(creation_time=0/빈 image_path)도 false 인데 Prepare/Quiesce 는 "타 프로세스=원대상 종료" 로 continue. **Same/Different/Unknown 3분류, Unknown=안전 실패.** 조회 오류 주입 반례 필요.
- 🔴 **D-C Quiesce open 실패 정책**: ACCESS_DENIED 외 전부 gone 처리. **ERROR_INVALID_PARAMETER 외 관용 금지**, Prepare 와 정책 통일.
- **D-D deadline**: 새 deadline 이 undelivered wait 만 공유. requestStop 단계·기존 Quiesce 별도 예산, Quiesce 는 32bit GetTickCount 잔존. **시작/포함 단계 명시 후 일관 64bit deadline 공유**, 복수대상·tick 경계 검증.
- 🔴 **D-E RAII 누수**: undelivered_ 에 handle 추가 후 다음 대상 identity/open 실패로 early return 하면 CloseHandle 루프 못 옴(소멸자는 ReleaseLock 만). **RAII 로 모든 조기반환/재호출 회수**, 첫 대상 watch 확보→둘째 실패 반례.
- 🔴 **D-F 통합 아직 주입**: staying case 가 prober 종료코드 본 뒤 `requestStop=[]{return false}` 로 Prepare(=주입). prober 생성 실패시 check 건너뜀. **DETACHED 실행자 안에서 실제 요청→Prepare→Quiesce→상태기계/Swap 연결**, 실패시 설치 sentinel 바이트 보존 검증.
- **D-G SCM/자식 실제 종료-후-Swap 미확정**: request_service_stop 은 stop 접수만 확인. 실제 부모+자식·격리 SCM service 의 STOP_PENDING/종료/실패·재기동 경계를 별도 회귀로.
- **D-H 주석 정정**: 커밋 주석 "On 2026-09-21 that false ended..." 를 현장 기전 확정처럼 쓰지 말 것. pid27568·ACCESS_DENIED·23회 동일 미확정 유지. abandon 재기동 보존도 이 diff 가 증명 못 함.
- 0.2.133 대화형 bootstrap 은 **검증 완료 후** 가능한 전달안, 유일 경로/출시 승인 아님. 구 132 updater 로 신 수정이 먼저 안 돎을 구분.

## Codex 검토 r3 (remote_codex, seq 2703) — NEEDS_CHANGES (eccf3c7 고정)
A/C/D/E 핵심 개선 확인, 그러나 미마감·테스트 공백·G 블로커:
- A 정정: parent_has_exited(target.parentPid) 잔존 + request_process_stop 이 PID 재open. "완전 폐기" 표현 정정, **대상 종료확인(보유 handle) vs 부모 라우팅 vs 정지요청 open** 을 계약으로 구분.
- B: 방향 타당(같은 PID 최초 생성시각+보유 handle 재확인, 조회불가≠Different). 단 생성시각은 전역 고유 아님, 조회실패와 종료 분리. **신규 10검사가 unreadable/종료 handle 을 실제로 안 만들고 readable-equal 만 검사** → 그 반례 추가 필요.
- C/Prepare: creationTime==0 continue 가 "요청 전 Unknown 거부" 계약을 깨고 Quiesce !watch 재open 으로 감. **identityKnown=true/time0 도 요청 전 실패로 고정.** request_process_stop/capture_process_identity 의 bool·open오류 관용 잔존 → Unknown·비INVALID_PARAMETER 를 성공/Gone 으로 보고 안 하게 정합.
- D: 공유 개선. deadline **시작 시점(열거 이후?)·요청 처리시간 포함 여부·settle+quiesce 합산 상한** 명시.
- E: RAII 개선. 단 cLeak 반례의 나쁜 imagePath 는 Unknown 아닌 **Different→gone·requestStop=true 라 Prepare 성공 가능·반환값 버림** → "둘째 대상 실패 조기반환" 을 실제 강제하고 오류지점까지 단정 후 handle 수 측정해야 음성대조 성립.
- F: 실 요청 배선은 확인. 단 capture_process_identity 실패 시 빈 목록으로 성공 가능, runner 가 Prepare/Quiesce 만 호출(Swap·상태기계 미실행). **sentinel 한 파일 보존을 "설치 전체 무변경/Swap 금지 실증" 으로 확대 금지.** 대상수·실요청 실패·지연exit 성공을 같은 DETACHED 경로에서 단정하고 필수파일 swap gate 까지 연결.
- 🔴 **G = 이번 릴리스 blocker 유지**: 격리 SCM fixture 로 STOP_PENDING→프로세스 종료 전 Swap0 / 종료 후 진행 / 거부·timeout 보존. 서비스 설치 권한 없으면 **그 시험만 정확히 "막힘" 으로 보고**, 단위/mock 과 실서비스 증거 구분. 운영 서비스 조작 신규 승인 아님.
- pid27568·ACCESS_DENIED·23회 미확정 유지. 0.2.133 후보 준비는 가능하나 **출시·운영 bootstrap 최종 승인은 위 보완 뒤**. 구132 가 새 코드 미실행하는 전달 한계 기록, 133 수동설치와 이후 인앱검증은 별개 단계.

## Codex 검토 r4 (remote_codex, seq 2707) — A~F 코드 확인, 출시는 오류분기+G 통과 후
- A~F 이번 보수(time0 요청 전 거부, 3상태 반영, move/RAII, 실 DETACHED run_update 실패·성공 payload 단정) 코드 확인. 전면 재작업 불요. 단 아래 전엔 출시 OK 아님.
- 🔴 오류분기: 5/87 외 OS 억지 재현 불요. **제품이 실제 호출하는 공통 open-error 분류 함수로 모아** 87만 Gone, 5·8·6·임의=Unknown/실패를 표 검증 또는 얇은 OpenProcess 경계 오류 주입. 실 API 시험 유지. **그 비교 넓힌 변이가 반드시 FAIL** 해야(현재 미검출 변이 해소).
- 🔴 G: mock 만으론 출하 불가. 현 바이너리 단순 승격 1회로도 부족 — Part2 가 attempt_against(...,true/false) 로 requestStop 답 주입, 'real refusal' 이 Linger+false. **실 SCM stop 을 attempt 의 요청 callback 으로 연결**하고 증거등급 정정. 실서비스 RUNNING identity/handle 잡은 **한 attempt 안에서 STOP_PENDING→실제 exit→Swap 순서** 확인(현 stopped 케이스는 종료 후 새 attempt=빈 target 성공일 수 있음). pending timeout·거부=Swap0+바이트 보존, 정상종료=교체를 같은 배선으로.
- 🔴 gate: BLOCKED 인데 RESULT ALL PASS/rc0 는 릴리스 게이트 오독. **--require-real 모드: blocked>0 이면 nonzero**, 실행수·정리 결과 산출, mock 모드와 분리.
- 🔴 승격 전 격리 안전: 신규 시험이 %TEMP% 아래 remove_dir_tree 재귀 삭제·reparse 추적 가능 → **저장소 내부 고유 test root·정규화 경계·reparse 거부**, 생성한 고유 이름/handle 만 정지·정리. 운영서비스·사용자 세션 무접촉.
- D 문구 한정: deadline 은 요청/API 실행시간을 남은 wait 에서 차감하는 뜻. 동기 API 자체 미중단이라 **절대 wall-clock 상한 보장으로 쓰지 말 것**(watchdog 재설계 요구 아님).
- 0.2.133: 오류분기 회귀+실 SCM 필수모드 통과 후 health/rollback 검증과 묶어 패키징/외부확인. 설치/UAC 는 별도 범위, 이 리뷰로 새 승격 권한 없음. bootstrap 을 인앱 성공으로 세지 말 것. 현장 PID/23회 미확정 유지.

## Codex 검토 r5 (remote_codex, seq 2711) — A~F 코드 확인, 승격 전 fixture 안전 2건 필수
- classify_open_error caller 연결·87 한정, 실 SCM callback·동일 attempt RUNNING/STOP_PENDING·targets1·파일교체, deadline 문구 = 코드 확인, 재설계 불요. 아래 닫은 뒤 G 실측.
- 🔴 **삭제 경계 구멍**: canonical_path 는 문자열 정규화+최종경로 reparse 만 검사. root\junction\child 직접 삭제 시 child 는 일반 디렉터리라 **root 밖 재귀삭제 가능**. root 조상~대상 **중간 reparse 검증/거부** + 반례 추가. GetFinalPathName 회피가 안전 근거 아님.
- 🔴 **root 보장 허위**: CMAKE_BINARY_DIR/test-scratch 는 저장소 밖 빌드도 허용 → "컴파일타임=저장소내부" 아님. **실제 빌드 root 를 저장소 경계와 런타임 대조**, 생성·삭제 모두 검증된 root 의 **실행별 고유 디렉터리**만(현재 svc-mock/real 이름은 동시실행 공유).
- 🔴 **FixtureService::destroy**: 최대 15s 만 대기하나 B 는 STOP_PENDING 20s. DeleteService 성공=삭제 예약일 수 있어 deleted=true 만으로 잔존0 판정 불가. **소유 process handle 실제 종료+서비스 최종 부재 대기**, query 실패를 STOPPED 로 보지 말고 cleanup 실패로 기록. DACL거부 fixture 도 기존 handle 권한으로 회수 유지.
- 안전 닫은 뒤 **시험 바이너리 hash/commit·경계경로·고유 서비스명·복구/정리 고정** 후 UAC 1회. 운영 GNLinkSecureInput/사용자 프로세스 무접촉. 이 리뷰가 승격/설치 승인 아님.
- 🔴 **G 종료조건**(단순 3 PASS 아님): --require-real rc0, blocked0/fail0, real3 attempt 실실행, RUNNING→요청→STOP_PENDING→실제exit→Swap 및 timeout/거부 Swap0·파일보존, 서비스/프로세스/scratch 잔존0. mock 의 signature/registration/relaunch seam 범위 명시.
- 통과 시 abandon-race/SCM gate 닫힘. 닫힌 A~F 를 총합만으로 재검사 말고 무관 범위 확장 말 것. G 통과가 전체 업데이터 성공 대체 아님.
- 최종 출시: D1/D2 검증 유지, 검증 소스/빌드↔후보 해시 연결, 서명·payload·외부게시 확인까지. 133 후보 준비 가능, bootstrap 과 이후 인앱 완주 증거 분리.

## Codex 검토 r6 (remote_codex, seq 2715) — 개선 확인, 승격 전 좁은 결함 4건 (현 바이너리 UAC 보류)
- 중간/조상 reparse·저장소 경계·run 단위 정리·G 필수모드 계수 개선 확인. A~F 재작업 없음. 단 아래 전엔 UAC 보류.
- 🔴 destroy 가 before.pid 로 process 재open. 정상성공 A 는 이미 STOPPED→pid0 가능→processExited=false→gProcessesExited 미증가→**G 실패**. start_and_wait 의 RUNNING 시점에 PID/identity 확인 handle 을 **멤버로 확보·전 구간 보유**, destroy 에서 재open 금지.
- 🔴 destroy 남은시간: GetTickCount64 두 번 호출→경계 unsigned underflow. **now 한 번 캡처, now>=deadline?0:deadline-now**. state query 실패와 handle 확보 실패 구분 기록.
- 🔴 scratch_root: create_directory_chain 먼저→그 뒤 조상 reparse 검사. 기존 조상이 junction 이면 **거부 전에 경계 밖 디렉터리 생성 가능**. 기존 컴포넌트 검사→생성→재검사. "외부대상 생성0" 반례.
- 🔴 scratch_run_dir: tick 24bit 절단+ERROR_ALREADY_EXISTS 를 성공 취급→고유/자기소유 미증명. **새 디렉터리 생성 성공만 수락**, 충돌은 새 이름 재시도/실패, 기존 파일/디렉터리/junction 재사용 금지. 삭제 시 root/조상 캐시 말고 확인. 시험 중 경로 변경 금지 명시.
- 좁은 수정+비승격 반례 후 **바이너리 hash/절대경로·서비스3명/시험root·최대대기/정리실패 복구명령** 묶어 --require-real 승격 1회 안내 가능. 미실행 코드 존재 자체는 영구 blocker 아님. 운영서비스/사용자프로세스 무접촉.
- G 닫힘 수용: rc0+G:MET+**realAttempts3+created/absent/processExited 모두3+blocked/fail/cleanup0+실제 로그**, **binary hash 가 검토버전과 일치**. 숫자/자가라벨만으로 판정 안 함.
- G=정지/교체 gate 증거, manifest서명·registration·relaunch·health 는 seam 유지. D1/D2·최종 패키징 조건 그대로. 133 후보 준비 계속 가능. 이 답으로 실제 승격·서비스 생성 금지.

## r7 완료(6a30c55) + remote_claude 독립 확인 + 🔴 새 크래시
- r7: destroy 가 RUNNING 시점 확보 handle 로만 대기(pid 재open 폐기), remaining_ms 단일 시계 함수(underflow 제거), validate_and_create_root(기존검사→생성→재검사), run dir=이 프로세스가 만든 것만(QPC+시도번호, ERROR_ALREADY_EXISTS 성공취급 폐기). G 잔존 3/3/3 고정. 승격 안내 출력(sha256·서비스3명·최대대기·복구명령).
- 독립 확인: **scratch_dir 57/0**(run dir QPC 이름·중간 reparse 안전 유지).
- 🔴 **새 크래시(제품 경로 의심, 출시 차단 후보)**: remote60_update_stop_process_test 가 간헐 **0xC0000409(스택 오버런)** 로 abort. 항상 같은 지점(**unopenable fixture 블록**, 111검사 중 61번째), fault offset 0x455a1. 내 재현: 4회 중 일부만 exit0, 나머지 비정상. 파일 무변경·해당 블록은 이번 작업보다 오래됨(작업용). ⚠️ 이 블록은 **열 수 없는 프로세스의 identity/stop 처리** = abandon 수정이 손댄 경로 → 테스트 버그인지 제품 버그인지 먼저 규명 필요. 제품이면 blocker. service_stop(G) 스위트와는 별개 바이너리.
- 판정: G 승격 실측·0.2.133 패키징 **전에 이 크래시 root-cause(제품 vs 테스트) 확정**.

## Codex 검토 r7 (remote_codex, seq 2719) — 크래시 우선 규명 동의 + 방법 지정
- 크래시 분류·규명을 G 승격·133 후보보다 먼저. 같은 수정 경로 재현 crash 를 성공 몇 회로 상쇄 금지.
- 🔴 **0xC0000409=스택 오버런 단정 금지**: __fastfail 도 이 코드 사용, **첫 exception parameter 가 사유**(stack-cookie/CRT invalid-parameter/terminate 등을 dump·subcode·stack 으로 구분).
- 실패 exe SHA256·빌드/commit·동일 PDB·fault module+offset 고정 → 해당 fixture 격리 → **처음 실패 1건의 dump/예외 parameter/전체 stack 수집**. "61번째" 는 경계일 뿐 훼손 지점 아님. 전역 WER 변경 말고 시험 프로세스 범위 수집.
- 제품 경로 메모리/수명 오류면 blocker. **fixture-only 여도** 필수 stop 검증을 망가뜨리므로 성공 회차 선별로 닫지 말고 fixture 최소 수정해 검증 복구. 원인 미확정은 계속 blocker.
- 🔴 **고정 gate**: 수정 전 재현·수정 후 원인별 반례 먼저, 같은 Release 후보/환경으로 **문제 블록 새 프로세스 20회 + 전체 stop 스위트 10회 사전지정**, 전부 exit0·필수단언 도달·fixture잔존0. 한 번 실패=배치 실패, 원인/변경 없이 카운트 재시작 금지.
- 반복 무실패는 보조 근거. 주근거=상태/이벤트로 interleaving 강제한 회귀 + dump 로 좁힌 기전.
- r7 diff 에 새 차단 결함 없음(적대적 동시 경로치환 보장 주장 금지). "G 3/3/3" 은 기대 종료조건이지 실측 성공값 아님. 순서: 크래시 해소→고정 안전범위+명령으로 UAC1회→require-real rc0·G체크리스트·실로그.

## Codex 검토 r8 (remote_codex, seq 2724) — 크래시 수정 수용 / 비크래시 3FAIL 미해결 / G 대기
- 픽스처 크래시 기전·수정 **수용**(값반환 임시 begin/end→length_error→단일 바인딩). 제품 프레임 부재만으로 제품 손상 전체 배제는 아니나 구체 기전·대조가 보강. 20 sweep/10 전체/잔존0 을 **그 크래시 회귀** 근거로 수용(독립결과·전체SHA/PDB·원로그 고정).
- 🔴 **별도 108/3 exit1 은 미해결 회귀 실패. 후속 30회 성공으로 상쇄 불가.** 133 최종 출시 OK 보류.
- 원로그 없으면 서비스 3이름 추정 금지. 매 실행 stdout/stderr **즉시 flush·개별 파일**, 종료코드/hash/fixture PID/단계/예산/실제시간/잔존 기록 → 최초 실패와 연쇄실패 분리.
- 유계 진단에서 **첫 실패 포착 후 그 interleaving 만 강제 재현**. 미재현을 해결로 쓰거나 count 재시작·assert 완화·skip 금지.
- 🔴 **먼저 분류**: 3FAIL 이 공유 scratch/handle/cleanup 안전성에 영향 없는지. 이름 없는 지금은 **사용자 UAC 요청보다 실패정보 회수·분류 우선**. G 안전 독립성 확보되면 기존 승인 범위서 require-real 가능. G 통과와 3FAIL 해결은 별도 gate, 상호 대체 없음.
- 판정: 크래시 수정 수용 / 3FAIL 미해결 / G 실측 대기. 다음 회신 = 독립 재확인+최초 실패단언·영향분류 한 묶음.

## Codex 검토 r9 (remote_codex, seq 2728) — 3FAIL gate 닫힘 수용, G 승격 실측 경로 열림
- Q1: 108/3 차단 이 범위서 닫음 수용(최초단언 포착·강제신호 재현·블록분리·되돌림 반례 주근거, 조용10+부하24 보강). 34회 재요구 없음.
- 야생 신호 발신자는 미확정 유지: "재현된 간섭 기전 + 시험한 stray 조건 무재현" 으로 한정(제품 전반 배제 아님).
- Q2: **G 승격 실측 1회 안내 경로 열림.** 두 스위트 무영향+r7 안전 근거로 고정 격리 범위서 사용자 승인 후 --require-real 가능. 제품 서비스/운영 세션 조작 확대 금지.
- Q3(실행 전 1회 안내): 검증 트리/commit·exe 절대경로·전체 SHA·root/run 경계·생성 fixture 서비스명·예상시간·정리/복구. 운영 바이너리 교체·새 전역설정 없이. 실행 후 rc0/G:MET/real3attempt/created·absent·processExited3/blocked·fail·cleanup0 **원로그 대조**.
- 별도(비게이트) #5: stop_test 끝의 childexit-*.txt 전량 삭제가 다른 동시/실패 회차 증거까지 지움 → 자기 run/owner PID 생성분만 회수, 타 owner 생존 반례. #6: INCOMPLETE gSkipped 는 일부 guard 만 증가 — 릴리스 runner 도 기대 검사수/필수케이스 도달 확인.
- G 통과 시 서비스 gate 닫고 D1/D2·서명/payload·후보해시·배포 검증으로. bootstrap·인앱완주 별도.

## 승격 실측 안내값 (바이너리 preflight, commit 0fdefe8)
- exe: build-local/apps/native_poc/Release/remote60_update_service_stop_test.exe (self-print SHA·root·서비스명·복구)
- 서비스명 동적: GNLinkUpdFixtureSvc<pid>A(stops)/B(lingers20s)/C(stop denied). test root=build-local/test-scratch. max wait 60s/teardown·8s quiesce, 전체 5분 미만. 복구 대개 불요.

## 🟢 G 승격 실측 통과 (사용자 관리자 실행, 2026-09-22 12:2x, remote_claude 파일 대조)
- 결과 파일 %USERPROFILE%\g_result.txt (UTF-16): **RESULT: ALL PASS (60 checks, 0 failed, 0 blocked, 6 attempts), G: MET by this run.**
- 체크리스트 전부 [x]: real 3 attempt 실실행 / RUNNING→실 stop 요청→STOP_PENDING→실제 exit→Swap (한 attempt) / 정지 미완=Swap0·기존바이트 / 거부=Swap0·기존바이트 / 3 created·3 absent·3 processExited / scratch 잔존0 / blocked0.
- exe sha256 **44674c21ffd14fc02754aea766f96f7a32e51225cd1630d21c84768be3f0f0dd** (검토·안내 값 일치). 실서비스 케이스: created err=0, RUNNING, request_service_stop accepted(calls=1), STOP_PENDING, Updated/0.2.105, 3015ms over 3000ms linger, payload 교체, STOPPED, SCM 부재. lingering=AbandonedBeforeSwap. denied=RUNNING 유지·payload 보존.
- 시스템 이벤트 로그 7045 3건(12:23:06/09/30, image=test.exe --fixture-service 3000/20000/1000) + 현재 잔존 서비스 0 로 교차 확인.
- ⇒ abandon-race/SCM 정지 안전 gate **닫힘**. Codex r9 대로 D1/D2·서명·payload·후보해시·외부게시로 진행. 다음: 0.2.133 재패키징.
