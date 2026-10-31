# report lockscreen-cred-impl r1 — 잠금·로그인 전 화면 'Windows 로그인' 구현 (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-rs9nhsdv / rev r1(추가 확정 1·2 포함) / kind=implement-report. 정본 `.claude/task_lockscreen_cred_impl_r1.md`, 계약 `codex_lockkeys_dxgi_r1.md` §A · `codex_lockcred_agent_r1.md` · `codex_lockcred_q12_r1.md`.

## 고정 후보
- worktree `C:/work/gnlink-wt/fix-lockscreen-cred`, 브랜치 `fix/lockscreen-cred`, 기준 main `5063cd9`, HEAD **`550a944`** (워킹트리 깨끗). 버전 인상·서명·게시·push 없음.
- 커밋 (제품 / 시험 / 문서):
  - 제품: `c2b4f33` 서비스 purpose·challenge·typing 판단 · `fc2e8b7` one-shot agent·logon_ui_target 공유·서비스 A6 · `9845f26` 호스트 LockCredV1·88·status·A6·K3 · `9956cca` 뷰어 단일 진입·결과창·Enter 경로 · `e10ee55` 번호 확정 표기·리뷰 보수 4건 · `5768b7a` **agent 사설 named pipe(추가 확정 1)·Q1 대기 거부+교체·Q2 per-transaction 점유** · `72b4826` send 시점에 보류한 down 의 up 도 보류
  - 시험: `f985e6a` e2e `--lock-screen` · `fe855a1` 채널·릴레이·pending 시험 · `2e306c6` e2e 보수(아래)
  - 문서: `2e53833` · `550a944` docs/history.md 항목 (APK 0.2.30 호환성 영향 명시)
- 번호(검증용 배정, Codex 이견 없음): `kCaptureFlagLockCredV1=0x40000`, control 88, SecureUnlockKind 5/6/7/8, UnlockStage 13/14/15. 41 의 `reserved`=purpose, 수신 측은 size 고정 검사 뒤 purpose 불일치면 거부(뷰어·릴레이 둘 다).
- 출하 빌드(`C:/work/gnlink-wt/fix-lockscreen-cred-build`, Release, @550a944 소스) sha256: GNLinkInputService.exe `46A5DC03…FADF2`, GNLinkStream.exe `2326793D…B019`, GNLinkViewer.exe `E996B7BA…88851`. 시험 빌드: GNLinkInputServiceLifecycleTest `A4312B29…FCCE`, GNLinkStreamMigrationTest `71C22F14…7FCA5`. (UI e2e 는 같은 소스의 in-repo 빌드 `build-lc` 에서 실행 — 산출물 공유 안 함)

## 설계 해석 (보고 요구)
- **one-shot `--lockcred-agent`**: 서비스가 ① 128-bit 무작위 id 의 일회성 named pipe 를 먼저 생성(FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_REJECT_REMOTE_CLIENTS, DACL `D:P(A;;GA;;;SY)`, 선점·실패 시 거부) ② 설치 exe 를 lpApplicationName 으로, 서비스 자신의 SYSTEM 토큰을 대상 세션으로 옮겨 `winsta0\Winlogon`, **bInheritHandles=FALSE, CREATE_SUSPENDED → kill-on-close Job → resume**(Job 실패 시 재개 안 하고 종료) ③ client 가 CreateProcess 가 준 **보유 핸들의 그 자식**(pid 일치·실행 중·client/자식 세션=대상·토큰 LocalSystem·Hello 의 pid 재확인)일 때만 Job 전송 ④ 자식은 서버가 **자기 부모 pid** 이고 기존 trusted-service 검사(SCM pid·이미지 동일 파일·재확인)를 통과할 때만 Hello. argv 에는 mode+id 뿐(비밀 아님), 비밀은 argv/env/파일/stdout/로그 없음, 폴백 없음. Job→LogonUI 대상 재검사→1회 입력(교체)→Answer→버퍼 zero→종료; 연결 5 s·응답 10 s·종료 2 s 한정, 넘으면 Job 닫아 회수. 구현 `lock_cred_channel.{hpp,cpp}`, `secure_input_service_main.cpp` type_lock_cred / run_lock_cred_agent. 입력 agent 파이프는 InputEvent/InputText 만 허용(Shutdown 등 거부)로 확장 안 함.
- **Q1**: 서비스는 미결 LockCred(입력 후 해제 대기, 최대 120 s)가 있으면 새 challenge 를 `reason=pending` 으로 거부(기존 context 보존), 뷰어는 대기 중 누름에 "앞서 입력한 비밀번호의 잠금 해제를 기다리는 중" 안내. 재시도는 검증된 칸의 **내용 교체**: UIA writable ValuePattern 으로 비우고, 없으면 같은 SendInput 안에서 Ctrl+A 후 Unicode. Backspace/Delete·nav 목록 확장 없음, 실패 시 덧붙임·다른 방식 재시도 없음.
- **Q2**: 릴레이는 서비스의 단일 unlock 파이프를 트랜잭션 동안만: capability 질의는 응답 즉시 닫음(실패 시 60 s backoff), 발급된 challenge 는 seal 까지(≤35 s), 입력된 LockCred 는 해제 대기 마감까지(≤130 s), 최종 답·거부·실패·마감·control 세션 종료(`EndSession`)에 해제. **구분**: idle 잠금 상태의 상시 점유(회귀)는 제거됨 / 단일 인스턴스라 한 트랜잭션이 진행 중이면 다른 호스트는 그 동안 대기하는 직렬 한계는 그대로(서비스 구조, 이번 범위 밖).

## acceptance 별 증거 (층 구분: 논리=단위·격리 / 제품UI=실제 뷰어 창·제품 코드, 시험 빌드 seam / OS=실기)
1. **두 화면 모두 버튼·Ctrl+Alt+U** — 제품UI: e2e `--lock-screen` 0x40000 pong → 툴바 item 7 배치·클릭 → 프롬프트(“원격 PC 잠금 화면에 입력할 Windows 비밀번호”), 해제 후 item 사라짐. prelogin 모드 79/79(버튼·Ctrl+Alt+U 기존 경로 유지). 논리: reboot_menu_policy(LockScreen/Prelogin/UAC 음성), gate 17. OS: 미검증(실기 A·B).
2. **매번 프롬프트·무저장·결과 창 구분** — 제품UI: 취소→“취소했습니다”, 비암호 칸→“비밀번호 칸이 준비되지 않아…”(TargetNotReady, 무입력), 입력→“입력했습니다 … Enter”(해제 아님), 해제 이벤트→“잠금이 해제되었습니다”; 뷰어 자격 증명 파일 생성 0, 비밀 출력·파일 0. 논리: 서비스 41(Injected 비종결·SessionUnlocked 은 같은 세션의 입력 뒤 세션 이벤트+해제일 때만·UnlockUnconfirmed·InjectFailed·pending). 미지원은 Ctrl+Alt+U 경로의 “쓸 수 없습니다” 문구(코드·단위만, e2e 미구동).
3. **실제 해제/로그인** — OS 미검증. 제품UI 는 seam 의 “잠금 해제 이벤트” 대역까지(실제 LogonUI·Enter 제출 아님).
4. **글자 보류·안내 + 호스트/서비스 거부** — 제품UI: 잠금 화면에서 'Q' → 보내지 않음 + “… 'Windows 로그인' 버튼(또는 Ctrl+Alt+U)” 안내, 송신 0. **e2e mutation E1**(뷰어 보류 제거): 2 edge 가 호스트에 도달 → 호스트 `[input][lock] plaintext key refused`, 어떤 창에도 입력 0 (A6 호스트 반쪽 e2e 관측, 시험 빌드 가짜 잠금). 서비스 반쪽: 단위 A6 표 17 + 코드(`refuse_locked_plaintext`) — 호스트가 Default 데스크톱에 직접 주입하는 시험 환경이라 e2e 로 서비스 거부까지는 못 감.
5. **취소→재누름·실패→재시도·연결 변경 뒤 재시도(latch 없음)** — 제품UI: 같은 run 에서 취소→비암호 칸 실패→정상 입력 순서로 매번 프롬프트. 기존 값 든 칸 → 새 비밀번호로 **교체**(바이트 일치; e2e mutation E2 덧붙임 → FAIL). 논리: pending 거부·응답 뒤 재발급, 릴레이 EndSession 해제.
6. **잠기지 않은 UAC·일반 데스크톱 회귀 없음** — 논리: A6 표(“known not locked + secure desktop” 통과), gate(SecureDesktop 만으로 보류 안 함), policy(UAC 단독 → 버튼 없음). 제품UI 회귀: not-prelogin 28·android-session 43·session-ends 41·capture-fallback 34·foreign-peer 37 exit0. OS: UAC 실기 미검증(실기 C).

## 시험 결과 (exit code 기준)
- 신규 논리: prelogin_cred_seal(3×3 purpose) PASS, secure_unlock_lockcred 41/41, logon_ui_target 11/11, lock_input_policy 17/17, prelogin_input_gate 17/17, reboot_menu_policy PASS, **lock_cred_channel 14/14**(실제 파이프·프로세스: 1회성 이름·held child 수락·낯선 client 선점 거부·무전송·비부모 서버 거부·**빌드된 GNLinkInputService.exe 를 직접 `--lockcred-agent` 실행 → exit 3, 잘못된 id exit 2**·사망/무응답 한정), **host_unlock_relay_ondemand 11/11**(호스트 2개 vs 가짜 단일 인스턴스: A probe 뒤 B 트랜잭션 성공·진행 중엔 상대 거부·최종 답/EndSession 해제).
- 기존 비-UI 회귀 51종 exit0(+in-repo 빌드 필요 3종 store_chain 29·user_host_launch 11·worker_log 38). 제외: `ka_relay_client_test` 는 기준 5063cd9 에서도 같은 fixture 실패. **간헐 미해결**: client_session_role_park S1 2/39 (기준 0/35; 그 경로는 이번 diff 밖 — 1회 통과로 닫지 않음).
- 제품UI e2e (`remote60_viewer_prelogin_login_e2e_test`, 사용자 확인 10-22 “이 PC 사용해라”, SLOT_REQUEST CreateNew→삭제 완료, Unity 0, 설치본·실서비스·실세션 무조작): `--lock-screen` 36/36(앞 판 35/35 ×3), prelogin 79, not-prelogin 28, android-session 43, session-ends 41, capture-fallback 34, foreign-peer 37 — 모두 exit 0. 산출 로그 `C:\work\gnlink-wt\lockcred-scratch\e2e\*.out`, 캡처 `cap_*`.
- mutation(각각 적용→해당 검사 FAIL→복원 PASS): 라벨 동일 / reqActive 제거 / 상태 미상=비잠김 / agent 재확인 생략 / capability 뒤 점유 유지(7 FAIL) / pending 거부 제거 / client pid 검사 제거(Hello pid 재확인이 여전히 거부 — 이중 방어, 실패 사유만 변경) / e2e E1 뷰어 보류 제거 / e2e E2 교체→덧붙임.
- 출하 gate: 출하 exe 3종에 seam 문자열 8종 0(ASCII/UTF-16), 시험 빌드 2종에는 각 4(검출기 양성 대조).

## 기존 시험 기대값 변경 (계약 반영, 숨기지 않음)
- A6 “release 는 막지 않음”: 뷰어 gate 가 key up 통과 → prelogin_input_gate H6 “key up 도 보류” 기대를 “up 은 통과” 로 변경. 대신 뷰어는 보류한 down 을 잊어(키보드·send 시점 모두) **보낸 적 없는 down 의 up 은 보내지 않음** → e2e G8 6→3 edge, H6(PC) 4→2, 4b 는 원래대로 0 송신. Android 공유 세션은 raw up 이 가고 worker 필터가 버림(dropped 2) — e2e 기대 갱신.
- not-prelogin: 구 unlock 프롬프트 취소가 결과 창으로 표시(계약 2) — 그 창을 닫는 operator 로 시험 수정(이전 수정판은 하네스 교착 — 제품 결함 아님, 수정 후 28/28).

## 알려진 한계·사용자 영향
- **APK 0.2.30(무변경): 새 호스트 잠금 화면에서 휴대폰으로 비밀번호 입력 불가**(호스트·서비스가 글자 거부). 화면·탭·내비게이션 키와 해제 후 일반 입력은 그대로. history 에 명시. 휴대폰 비밀번호 수단이 필요하면 sealed APK 또는 사용자 평문 예외 결정 필요(Codex A6).
- 자동 제출 없음: 입력 뒤 뷰어에서 Enter(결과 창이 안내). 로그인 전 화면은 “로그인 화면 이탈”만 관측(로그인 확인 아님으로 표기).
- 같은 세션·같은 사용자 시험은 **Session0→콘솔 SYSTEM 자식→Winlogon 경계의 실증이 아님**(Codex 지시) — 실기 A-6.
- 단일 unlock 파이프의 트랜잭션 직렬 한계(위 Q2 구분).
- 이번 버스 메시지 중 늦게 도착한 “상속 파이프 교체 요청”(seq13182 재안내)은 `5768b7a` 에서 이미 반영됨.

## OS 실기 묶음 절차 (사용자 조작 1회, 검증용 경유)
사전: 검증용이 이 브랜치를 통합한 시험 설치본을 회사 PC(뷰어)·이 PC(Host·서비스)에 설치(이 PC 서비스 재설치 UAC 1회).
확인 로그: 이 PC `C:\ProgramData\GNLink\secure_input.log`(`unlock challenge issued purpose=lock-cred`, `lock-cred requestId=… typed session=N`, `… -> unlocked`, `plaintext … refused`, `lock-cred: the pipe's client was refused` 없음), `%LOCALAPPDATA%\GNLink\host_app.log`(`[lock-cred] service capability supported`, `advertised=1`, `[input][lock] plaintext … refused`), 회사 PC viewer.log(NAS, `[lock-cred] … final stage=9`).
- A 잠금 화면: ① 콘솔 Win+L, 새 뷰어 접속 ② 툴바 'Windows 로그인' → 비밀번호(칸이 안 보이면 TargetNotReady 안내 → 원격 화면 클릭 후 재시도) ③ “입력했습니다 … Enter” → 뷰어 Enter → “잠금이 해제되었습니다” ④ 다시 잠금 → 글자 입력 → 안내·무입력, Ctrl+Alt+U → 매번 프롬프트 → 취소 안내 ⑤ 틀린 비밀번호 → Enter → 2분 뒤 “확인되지 않았습니다” → 재시도 성공 ⑥ **Codex 경계**: ③ 직후 secure_input.log 에 `typed session=<콘솔 세션>` 있고 `client was refused` 없음; 가능하면 작업 관리자(관리자)로 `GNLinkInputService.exe --lockcred-agent <32hex>` 가 세션 N·SYSTEM 으로 잠깐 떴다 스스로 사라짐(명령줄에 id 만) ⑦ Q1: 틀린 비밀번호 입력 직후 Enter 없이 다시 누름 → “기다리는 중” 안내 → 2분 뒤 올바른 비밀번호 → 칸이 **교체**되어 Enter 로 해제 ⑧ (선택) 빠른 사용자 전환 2세션: 한쪽이 잠금 대기여도 다른 쪽 unlock 이 막히지 않음.
- B 로그인 전 화면: 뷰어 '다시 시작'(GNLink 재시작 — Windows Update 재시작은 ARSO 로 잠금 화면) → 'Windows 로그인' → “입력했습니다 … Enter” → Enter → 로그인 → “로그인 화면을 벗어났습니다(로그인 확인 안 됨)”.
- C UAC: 잠기지 않은 데스크톱에서 UAC 창 → 뷰어 클릭·글자 입력 기존대로, 'Windows 로그인' 없음.
- D APK 0.2.30: 잠금 상태에서 폰 접속 → 화면·탭 됨, 글자는 잠금 화면에 안 들어감(host_app.log 거부 줄), 해제 뒤 일반 입력 정상.

## 미검증
OS 실기 A~D 전부(실제 SYSTEM agent·Winlogon·LogonUI ValuePattern/Ctrl+A 동작·Enter 해제·ARSO 오버레이·다른 세션), 실 SCM 설치 경로, NVIDIA/Intel 무관. 간헐 S1. Ctrl+Alt+U “미지원” 문구 e2e 미구동.
