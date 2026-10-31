
### 2026-10-26 lk-ux-retry r1 — 잠금 화면 'Windows 로그인' 같은 연결 재시도 (fix/lk-retry, 미게시 · R1 0.2.163 통합 대기)
- 근거: 0.2.162 실기(10-22) — 틀린 비밀번호를 넣은 뒤 다시 누르면 "앞서 입력한 비밀번호의 잠금 해제를 기다리는 중" 으로 막혀(상한 120 s) 재접속으로만 풀렸다. 계약 `codex_field_0162_0231.md` §2·§3·§5, 정본 `task_lk_ux_retry_r1.md`, 배정 `plan_rel_0163_2026-10-26.md`.
- 행동: 같은 연결에서 'Windows 로그인'(Ctrl+Alt+U)을 다시 누르면 비밀번호 창이 열리고, 입력하면 Host 가 서비스에 앞 대기의 종료를 먼저 확인받은 뒤에만 새 challenge(새 requestId·tx generation·challenge·AEAD)를 받아 칸 내용을 새 비밀번호로 바꿔 입력한다. 재접속 0. 결과 창은 "Windows 메시지가 뜨면 원격 화면에서 닫은 뒤 다시 누르세요" 를 안내한다(GNLink 는 Windows 오류 창의 확인/Enter 를 누르지 않는다).
- 경계: 다른 연결의 재시도·요청은 기존 Busy. 종료를 확인하지 못하면(서비스 거절·무응답·다른 request 를 지목·재시도 미지원 서비스) SupersedeRefused(18)로 아무것도 보내지 않고 앞 대기를 계속 따른다. 앞 대기가 이미 스스로 끝났으면(해제·120 s) 그 실제 결과로 끝난다. 옛 seal 재전송은 입력 0. 구 서비스(RetryV1 없음)·구 Host(0x80000 없음)에는 89 를 보내지 않고 기존 "기다리는 중" 문구 그대로.
- 번호(제안, 검증용 확정 대기): MessageType 89 `ControlLockCredRetryChallengeRequest`, pong `kCaptureFlagLockCredRetryV1` 0x80000, UnlockStage 17 Superseded·18 SupersedeRefused, 서비스 IPC kind 9 `LockCredSupersede`, 기능 비트 `kSecureUnlockFeatureLockCredRetryV1` 0x2. main·spike/host-admission·feat/keepalive-r2-apk·feat/keepalive-multi 와 충돌 없음.
- 결과 창: 고정 64 px 라벨이 새 안내의 넷째 줄을 잘라 먹어(실제 캡처로 발견) 글 높이에 맞춰 창이 늘어나게 했다(기존보다 작아지지 않음 — 로그인 전·재부팅·SAS 결과 창 공통).
- 시험(이 PC 콘솔, SLOT_REQUEST CreateNew→삭제, 설치본·실서비스·실세션 무조작): 서비스 lockcred 72/72 · relay ondemand 57/57 · service 12 · lock_cred_channel 15 · prelogin_cred_host 43 · logon_ui_target 21 · unlock_protocol/sealed_unlock PASS — 모두 rc0. e2e(제품 창 프로시저·툴바·비밀번호 창·ControlClient + Host 시험 빌드 + 서비스 lifecycle 시험 빌드의 실제 SendInput): `--lock-screen` 57/57(재시도 13 항목 추가) · prelogin 79/79 · `--not-prelogin` 28/28 · `--session-ends` 41/41, 모두 rc0, `--lock-screen` 은 최종 코드로 2회, 결과 창 수정 전 단계까지 합쳐 6회 연속 통과(첫 회 1 FAIL 은 시험의 줄 끝 
 가정 — 시험 수정).
- 부정 대조: 뷰어 재시도 게이트를 예전 "pending" 거절로 되돌린 mutant → 10-22 증상 문구 그대로 재현, 재시도 7 항목 FAIL(rc1). 결과 창 크기 고정 mutant → "text 96 px in a 64 px label" FAIL(rc1). 복원 뒤 PASS.
- 보류: §3 인증 실패 자동 감지 — 실제 LogonUI 의 실패 UI 상태 전이 근거가 없다(사용자 콘솔을 잠그는 시험은 금지, 기존 fixture 는 비밀번호 칸만 재현). 그래서 LockCred 의 AuthFailed 문구도 추가하지 않았다(이 빌드에서 도달 불가).
- 남은 실기: 실제 LogonUI·Winlogon·SYSTEM 서비스에서 틀린 비밀번호 → Windows 거절 → 다시 누름 → 해제(R1 게시 뒤 사용자 1회). 알려진 한계: 앞 대기가 해제로 끝난 직후의 재시도는 새 challenge 가 "잠금 화면이 아님" 으로 거절돼 "해제됨" 대신 그 문구가 보인다.
- 제품/테스트/문서: 제품(Host 89·relay·서비스 kind 9·뷰어 재시도·결과 창 크기), 테스트(서비스·relay·e2e 재시도), 문서(이 항목·작업목록 LK·구현계획).
