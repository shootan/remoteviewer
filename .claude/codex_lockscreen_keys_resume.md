# lockscreen-keys r1 — Codex 재개 시 소스 확인 (2026-10-21)

조사 보고의 최종 판정이나 구현 승인이 아닌 읽기 전용 사전 검토다.

## 기준과 상태

- 인계 정본 `.claude/session_handoff.md`, 지시 정본 `.claude/task_lockscreen_keys_r1.md`를 끝까지 읽었다.
- 기존 실행 task `t-nr6r1jja`, 작업용1 `remote#rqxbj3jb`, 발신 검증용 `remote#wwml7h91`. 조회 시 submitted이며 착수/완료로 해석하지 않았다.
- 나 `remote#am104clp`. 검증용 displayName `remote_claude`, cwd `D:/remote/remote`, onCall=false를 확인했다. 현재 사용자의 A2A 상대 확인 절차에 따라 연결 확인 질문을 올렸으며 아직 송신하지 않았다.
- 루트 checkout은 `refactor/viewer-split` / `659f788`로 게시 기준과 다르다. 검토 대상은 `.claude/worktrees/main-stability-integration`의 `5063cd9`이고 tracked clean, 미추적 `.claude/`만 있다. 루트 제품 소스를 현재 출하 소스로 취급하지 않았다.
- 조사 보고 `.claude/report_lockscreen_keys_r1.md`는 재개 시 아직 없었다. 제품 직접 수정/빌드/설치/사용자 입력을 수행하지 않았다.

## 직접 확인한 코드

1. `reboot_menu_policy.hpp:45`의 DecidePreloginCred는 Pc + PreloginV1만 sealedEntryAvailable=true로 한다. `viewer_picker.cpp:112`가 이를 toolbar state에 전달하고 `client_session_toolbar.cpp:196`이 Windows 로그인 버튼을 추가한다. 따라서 사용자 Host 잠금 화면에는 이 버튼이 없다.
2. `viewer_window_proc.cpp:235`의 Ctrl+Alt+U는 prelogin이면 일회 입력 경로를, 아니면 기존 저장 암호 unlock 경로를 선택한다. 기존 경로는 has_unlock_password(0)가 참이면 prompt를 건너뛴다. local hotkey 호출은 paste/일반 forwarding보다 앞이지만, repeat/진행 중 요청을 막는 공통 latch는 이 분기에 없다.
3. `viewer_control_client.cpp:738`의 기존 unlock 경로는 성공/실패/미지원/저장 암호 없음 결과를 로그에만 남긴다. prelogin 경로는 별도로 kMsgPreloginCredResult를 게시한다. 사용자 확정의 양쪽 결과 표시는 현재 비대칭이다.
4. `viewer_input_forward.cpp`의 새 prelogin gate는 roleKnown=false도 Prelogin으로 취급하고, 안내는 roleKnown=true일 때만 게시한다. 글자/IME/물리키/일반 key/macro 경로와 control 송신 직전 검사를 읽었다.
5. `viewer_control_client.cpp:384` 주변에서 매 pong의 hostCaptureFlags를 저장한 뒤 hostRoleKnown=true를 release로 게시한다. secure-desktop-active 분기는 로그 상태 갱신이다. 이 소스만으로 secure=true가 키 송신을 막았다고 단정할 수 없다.
6. `git diff a622f04 5063cd9`로 0.2.159 대비 위 prelogin gate/버튼/분기 추가를 확인했다. 기존 unlock 저장 암호 분기는 이전에도 있었다. 실제 송신 0건의 원인과 ARSO는 아직 미확정이다.
7. `viewer_unlock.cpp`의 sealed_exchange는 prelogin/lock 목적별 KDF/AAD를 분리한다. 최소 변경에서도 목적을 혼용하거나 평문 키 입력으로 우회하지 않아야 한다. run_unlock_exchange는 SessionUnlocked만 true이며 Injected와 실제 해제를 구분한다.

## 다음 검토

검증용 연결 확인 및 온콜 복구 후 기존 task 하나에 재개를 연결한다. 작업용 조사 보고와 격리 행동 근거를 받아 잠금 상태의 권위 있는 식별, 버튼/단축키 단일 진입점, 반복/취소/연결 변경 latch, 목적별 sealed 전달, 사용자 결과 표시, 차단 시 실제 버튼 위치 안내를 한 묶음으로 검토한다. secure desktop 일반 상태를 잠금 화면으로 단정하지 않는다. APK0.2.30 유지, git push 보류를 유지한다.

공개 문서/제품 변경이 없으므로 별도 history/계획/commit은 만들지 않는다. 이 메모만 미추적 `.claude/`에 보관한다.
