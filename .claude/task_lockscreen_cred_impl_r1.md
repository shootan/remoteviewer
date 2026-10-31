# task lockscreen-cred-impl r1 — 잠금 화면·로그인 전 화면 'Windows 로그인' 구현 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용1

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로 진행한다.** 조사 task(lockscreen-keys r1, t-nr6r1jja)와 별개의 구현 task 다.
설계 확정: `.claude/codex_lockkeys_dxgi_r1.md` §A(A1~A7 + "A 구현 계약" 1~7) — **그 조건이 구속 계약**이다. 여기 요약과 다르면 Codex 파일이 우선.
근거: `.claude/report_lockscreen_keys_r1.md`(당신의 조사), `.claude/verify_lockscreen_keys_r1.md`, 참고 `docs/OSLink_정적분석_2026-10.md` §2(코드 복제 금지).

## 사용자 행동 acceptance (이게 완료 기준)
1. **잠금 화면(활성 콘솔 + 잠김)** 과 **진짜 로그인 전 화면** 둘 다: 뷰어 툴바에 'Windows 로그인' 버튼이 보이고 누를 수 있다. Ctrl+Alt+U 도 같은 동작.
2. 누를 때마다 비밀번호 창이 뜬다(저장·자동 재사용 없음). 입력 → 확인 → **결과 창**: 입력됨 / 해제(로그인)됨 / 포커스·준비 실패 / 입력 실패 / 입력 후 해제 미확인 / 취소 / 미지원 을 구분해 보여 준다. 자동 제출이 없으면 결과 창에 Enter/제출 필요를 명시.
3. 실제로 Windows 잠금 해제·로그인까지 된다.
4. 잠금 화면에서 글자를 치면 보내지 않고 **버튼 위치·Ctrl+Alt+U 안내**가 보인다. 마우스·내비게이션 키는 기존 prelogin 목록 수준으로 된다.
5. 취소 → 다시 누름, 실패 → 재시도, 연결·세션 변경 뒤 재시도가 된다(latch 없음).
6. **잠기지 않은 UAC 화면의 기존 입력은 그대로** 된다. 일반 데스크톱 입력 회귀 없음.

## 범위 (Codex 계약 요약)
- A1 `LockCredV1` pong 비트: 사용자 Host 자기 세션의 권위 있는 잠김 + 신뢰된 서비스의 새 경로 지원(버전/기능 응답). WTS 조회 실패 ≠ 잠기지 않음. PreloginV1 재사용 금지. 상태 변경 시 stale prompt/credential/result 폐기.
- A2 버튼·메뉴·Ctrl+Alt+U 단일 진입 함수, 누름당 prompt 1, repeat·중첩 modal 방지, modifier 균형. 기존 disconnected unlock 도 결과를 보이게 + RejectedPolicy 시 저장 암호 제거(사용자 실제 저장 파일을 개발 중 지우지 말 것).
- A3 sealed purpose 3분리(Unlock/WTSConnectSession · PreloginCred · **LockCred**) — KDF·AAD 모두, 새 kind/op 또는 명시 버전으로 선택, 서비스가 발급 목적을 저장해 결과 조회까지 고정, 순차 복호 추측 금지, host/account/requestId/cookie/pipe caller identity/session/lock·topology generation/만료 바인딩 + 실행 직전 재검증, replay 는 기존 결과만.
- A4 Host 는 평문을 열지 않고 trusted service 로 relay → 이미 인증된 Winlogon SYSTEM agent 가 실제 system dir LogonUI.exe·같은 session·UIA IsPassword 필드 검증 후 Unicode 1회 입력. ProductCredTarget 판정 규칙 공유(단 prelogin 의 "다른 데스크톱이면 종료" 수명 규칙은 가져오지 않음). 실패 시 입력하지 않고 사유 회신. 데스크톱 강제 전환 금지. 기존 reqActive→RejectedPolicy 유지.
- A5 새 PC 뷰어: 알려진 잠금 역할에서 문자/IME/text/macro 보류 + 안내.
- **A6 호스트·최종 secure 경계 차단**: 잠긴 세션으로 가는 kind 5/6 문자·InputText·raw/IME/macro 평문 경로를 호스트에서 막고 service/agent 도 일관 거부(지연·큐 잔류·위조 역할 포함). SecureDesktopActive 전체 차단 금지 — 잠기지 않은 UAC 유지. release(키 떼기) 정리는 차단과 분리해 stuck key 없음. ACK ≠ 입력 성공.
  - ⚠️ APK 0.2.30 은 변경하지 않는다 → 새 호스트 잠금 화면에서 휴대폰 비밀번호 입력 불가가 된다. 이것을 history·릴리스 문서에 **알려진 호환성 영향**으로 명시하고, 구형 APK 대조 시험(문자 거부 + UAC/일반 데스크톱 유지)을 포함.
- 계측: K3 PhysicalKey 드롭 카운터·RejectedPolicy 사유를 payload·암호·토큰 없이 bounded 한 줄로.

## 시험 (Codex 계약 7)
- 논리/격리: 역할 unknown·구형 서비스·일반 desktop·UAC·활성 콘솔 lock·disconnected lock·prelogin, 취소→재누름·held repeat·실패→재시도·연결/세션 변경, 목적 교차 blob·replay, 비암호 필드·위조 LogonUI·다른 session·부분 입력·해제 미확인. **부정 대조가 실제로 실패하는지** 확인.
- 제품 UI: product-equivalent test build 의 실제 뷰어 창에서 버튼 가시성·활성·가림(elementFromPoint)·클릭 → service/agent 전달까지. DOM 하네스 / 제품 UI / OS 실기를 구분해 적는다.
- **OS 실기(실제 잠금·로그인 전·실제 로그인)**: 사용자 세션 잠금·재부팅·UAC 는 사용자 조작이다. 자동화로 갈 수 있는 데까지 간 뒤 **실기 묶음 절차를 1회** 준비해 검증용에 넘긴다(직접 잠그거나 재부팅하지 않는다). SendInput/UI 입력 시험은 검증용 경유로 사용자 PC 미사용 확인 뒤만.
- 출하 gate: 시험 seam·키·완화 manifest 가 출하물에 없음.

## 규칙
- worktree: main `5063cd9` 에서 새 worktree·브랜치(예: `C:/work/gnlink-wt/fix-lockscreen-cred`, `fix/lockscreen-cred`). 루트 `D:/remote/remote`(refactor/viewer-split) 와 `.claude/worktrees/main-stability-integration`(검증용 문서 작업 중) 은 쓰지 않는다. 새 worktree 는 webview2 junction 필요(메모리 규칙: 제거 시 rmdir 먼저).
- 사용자 설치본·서비스·세션 조작 금지. 버전 인상·서명·게시·push 금지(검증용이 rel 로 통합). 측정 슬롯 `C:\work\gnlink-wt\SLOT_REQUEST.txt` CreateNew.
- 커밋은 Git MCP(`.claude/mcp_git_client_wt.py <worktree>`). history 항목은 이 task 커밋에 함께. 작업목록(LK 행)은 검증용 docs 커밋과 충돌하지 않게 검증용이 통합 때 갱신한다.
- 다른 신뢰 경계·새 기능이 필요해지면 그 부분만 검증용 경유 Codex 재판정. 나머지는 계속.

## 보고 (1회, 기능 완성 시)
`D:/remote/remote/.claude/report_lockscreen_cred_impl_r1.md` + `remote#wwml7h91` pin(identity+sessionId).
내용: commit 목록·worktree·제품/테스트/문서 구분, acceptance 1~6 각각의 증거(무엇이 DOM/제품 UI/OS 인지), 시험 exe·종료 코드·부정 대조, 출하 gate, 남은 OS 실기 묶음 절차, 알려진 한계(APK).

## 추가 확정 (2026-10-22, 같은 r1 에 통합) — 번호와 one-shot agent 전달 경계
- 번호 확정(Codex 이견 없음): `kCaptureFlagLockCredV1=0x40000`, control 88 `ControlLockCredChallengeRequest`(41 reserved purpose echo, 42~45 재사용 — purpose 상태 저장·KDF/AAD 분리·strict size/version 유지, 구형 peer 에 88 미송신), `SecureUnlockKind` 5/6 Capabilities·7 LockCredChallengeRequest·8 StatusRequest, `UnlockStage` 13 TargetNotReady·14 InjectFailed·15 UnlockUnconfirmed.
- one-shot SYSTEM 자식(`--lockcred-agent`) 자체는 OK. **단 Session0 서비스→콘솔 session 자식의 익명 파이프 상속은 NEEDS_CHANGES**(CreateProcessAsUserW bInheritHandles 는 세션 간 상속 불가 — 기존 start_agent 가 그렇게 쓰는 것은 근거가 되지 않음). 정본 `.claude/codex_lockcred_agent_r1.md` 의 확정 대안으로 구현:
  1. 서비스가 먼저 일회성 named pipe 생성 — 예측 불가 이름, FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_REJECT_REMOTE_CLIENTS, SYSTEM 전용 DACL, 선점·실패 시 거부. 일반 secure-input 파이프(kind 1/2)는 확장하지 않음.
  2. 보호된 설치 위치 고정 exe 를 lpApplicationName 에, 서비스가 복제한 SYSTEM primary token·대상 session·`winsta0\Winlogon`, **bInheritHandles=FALSE, CREATE_SUSPENDED → kill-on-close Job → resume**(job 실패 시 재개 안 함).
  3. 서비스는 연결 client 가 held process handle 의 살아 있는 자식(PID·생성 identity·SYSTEM token·대상 session)인지, 자식은 server 가 trusted service(기존 검증 + held identity)인지 양방향 확인 후에만 job 교환. SID·pipe 이름·명령줄 parent PID 만으로 인증 금지.
  4. argv 에는 mode·비밀 아닌 bootstrap 식별자만. **암호·sealed 본문·키는 argv/env/파일/stdout/로그 금지, 실패 시 그쪽으로 폴백 금지.** bounded versioned job/result 1개, 실행 직전 상태·LogonUI/UIA 재검사, 1회 입력·자동 재전송 없음, 모든 경로 버퍼 zero·handle close, 취소·generation 변경·service stop·timeout 시 입력 중단·child/job 회수, bounded I/O.
- 시험 추가: fake/선점 pipe·다른 PID SYSTEM client·다른 session·비부모 server·직접 `--lockcred-agent` 실행·stale/cancel·부분 입력·child/UIA/IPC 정체 거부·회수, argv/env/log/file 비밀 부재. **같은 session asInvoker helper PASS 는 이 경계의 실증이 아니다** — 실제 Session0→콘솔 session SYSTEM child→Winlogon 확인은 승인된 실기 묶음에 포함.

## 추가 확정 2 (2026-10-22) — Q1 재입력·Q2 unlock 파이프 점유 (정본 `.claude/codex_lockcred_q12_r1.md`)
- Q1 (a)+(b): 서비스는 미결 LockCred job 이 있으면 새 요청을 거부(기존 context 보존). 승인된 재시도는 **같은 검증된 LogonUI 암호 칸의 내용을 교체** — writable ValuePattern, 또는 검증된 칸에 select-all → 일회 Unicode 입력. Backspace/Delete 등 일반 nav/평문 경로는 넓히지 않음. 교체·부분 입력 실패 시 이어 붙이기·다른 방식 자동 재시도 금지, timeout/cancel 뒤 다시 누를 수 있음.
- Q2: 릴레이의 잠금 시 자동 unlock 파이프 점유 제거 → **요청 시점 점유(on-demand, bounded)**. capability query 는 응답 후 즉시 닫고 backoff. challenge→seal→status 는 per-connection 바인딩이라 중간에 끊지 말고 명시 deadline/terminal/cancel/끊김에서 정리 후 반환. idle 잠금·사용자 Enter 대기로 무한 독점 금지.
- 시험 추가: 기존 값 → 재시도 = 새 값만(실제 해제 포함), pending 중복·포커스 변경·부분 실패, idle 잠금 Host A probe 뒤 Host B unlock 성공·정체/종료 뒤 후속 요청. 기존 single-instance 의 실제 transaction 직렬 한계와 idle 점유 회귀 제거를 구분해 보고.

## r2 보수 (2026-10-22) — Codex 550a944 NEEDS_CHANGES (정본 `.claude/codex_lockcred_impl_r1.md`, L1~L6 + EVIDENCE 한 묶음)
- **L1** 공용 relay 의 cache/Poll 이 requestId 만 key → 새 뷰어가 id 2 부터 다시 시작하면 이전 terminal 결과 때문에 새 seal 을 건너뛰거나 옛 결과를 재사용. cookie+id+purpose(transaction) 로 격리하고 종료 시 정리.
- **L2** 서비스 검사 뒤 child Job 에 원래 account·lock generation·expiry·cancel 이 없고 TypeIntoTarget 이 stillValid 를 쓰지 않음 → spawn/target 대기 중 취소·만료·재잠금 뒤에도 입력 가능. 실제 clear/type commit 직전까지 권한 fence.
- **L3** 서비스가 requester 를 target 해석 전에 검사, console fallback/agent 큐 대기 뒤 최종 agent 가 lock 을 재검사하지 않음 → 실제 target 기준 + 마지막 injection 직전 차단.
- **L4** 암호 칸 identity 가 HWND+PID 뿐이라 같은 창의 다른 PasswordField 구분 불가, ValuePattern clear 뒤 같은 요소 재검사 없음, UIA 실패도 select-all fallback, 부분 Ctrl/A 전송 뒤 key-up 정리 없음 → Q1 반례 포함 보수.
- **L5** relay 동기 exchange 가 holdUntil/deadline 밖에서 무한 ReadFile 가능, EndSession 도 큐 대기 → 실제 I/O 취소·회수, service capability 수명, UI 결과 generation 보수.
- **L6** gate TEST_MARKERS 에 `GNLINK_SIS_TEST_` 추가(현재 누출 판정 아님). DW 브랜치 표식(mt-ab·GNLINK_CHAIN_)은 통합 때 합집합으로 — 이 브랜치에서 미리 넣지 않아도 됨.
- EVIDENCE: 보수 후 고정 후보에서 검증용이 `--lock-screen`·prelogin UI e2e 독립 실행. S1 간헐은 미해결로 유지 보고. 실기 A⑧ 은 "idle 점유 해제" 와 "진행 중 transaction 직렬 한계" 를 구분해 절차 수정.
- 보고: `.claude/report_lockscreen_cred_impl_r2.md` + 검증용 pin. 같은 브랜치에 커밋.

## r3 보수 (2026-10-22) — Codex 8ecaa8f NEEDS_CHANGES (정본 `.claude/codex_lockcred_impl_r2.md`, 네 항목 한 묶음)
- **L2-R** Ready fence 가 now+20 s 만 검사해 원래 `ctx.expiresMs` 만료 뒤에도 Go 허용(29 s 접수 → 31 s Ready 반례). 마감 = min(원래 expiry, 처리 상한).
- **L5-R** 큐가 비어 있지 않으면 wait_for predicate 가 true 라 절대 hold 마감(35/130 s)을 우회. 큐·exchange 예산과 무관하게 35/130 s 상한을 지키고, 다음 Host 가 통과하는 반례 시험.
- **UX-R** 진행 중 transaction 때문에 못 쓰는 경우를 "오류 0/170" 으로 보이지 말 것. 실제 busy 를 식별해 "다른 연결이 잠금 해제를 처리 중입니다 — 잠시 뒤 다시 눌러 주세요" 류의 수동 재시도 안내. **번호 확정(검증용): `UnlockStage::Busy = 16`**(main·로컬 전 브랜치에 16 사용 없음 확인). 일반 오류를 busy 로 오표시 금지.
- **L1-R** EndSession 이 map 삭제·취소해도 늦게 도착한 `challengeResults_[key]`/`statusAnswered_[key]` 가 무조건 publish 되어 항목을 되살림 → 살아 있는 transaction 세대 확인 후에만 publish, challenge/status 취소 반례.
- L3/L4/L6 은 Codex 확인 완료. 무관 회귀 전량 반복 불필요. OS 실기 A~D·S1 미검증 유지.
- 보고: `.claude/report_lockscreen_cred_impl_r3.md` + 검증용 pin.

## r4 보수 (2026-10-22) — Codex 53506ab NEEDS_CHANGES (정본 `.claude/codex_lockcred_impl_r3.md`, 잔여 R1/R2 만)
- L2(원래 expiry)·L1(late cache) 은 닫힘. Busy 실파이프→16·무입력·A 무간섭·종료 뒤 수동 재시도 근거 수용.
- **R1** SealedAsync 가 terminal 만 중복 생략 → 같은 typed 요청 재전송 시 서비스는 Injected 멱등 응답, relay(약 540~546)가 hold_for(130 s)를 무조건 다시 시작 → t0 typed, t119 duplicate 면 마감 249 s; 그 뒤 poll 이 없으면 서비스 ReadFile 대기라 120 s 에 pipe 를 자동 반환하지 못함. **최초 typed 마감 보존**(재전송이 마감을 늘리지 않음) + short hold(예 2 s)로 같은 sealed 재전송 반례·변이·다음 Host 통과 시험(실 249 s 실행 주장 아님).
- **R2** Busy 문구의 "길어야 2분" 은 relay 130 s·challenge→typed 전환상 보장되지 않음 → 숫자 삭제: "그 요청이 끝나거나 취소된 뒤 다시 누르세요." 보고서·현장 묶음 문구도 같이.
- 검증 범위: R1 영향 relay 시험 + 같은 후보 Busy UI 1회·캡처·gate·해시면 충분(L1/L2·UI/GPU 전량 반복 불필요). OS A~D·S1 미확인 유지.
- 보고: `.claude/report_lockscreen_cred_impl_r4.md` + 검증용 pin.
