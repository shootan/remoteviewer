# report lk-ux-retry r1 — 작업용1 (2026-10-26) — task lk-ux-retry / r1 (원 t-wfv0aneu)

**결론: §2 같은 연결의 명시 재시도 구현·자동 검증 완료. §3 실패 자동 감지는 근거 없음으로 보류. 고정 후보 `fix/lk-retry` 9991b36** (= WIP 5b1d2e0 + 마무리, base main 82b3d14). 버전·서명·게시·push 없음.

## 고정 후보
- commit **9991b36** (worktree `C:/work/gnlink-wt/fix-lk-retry`, 워킹트리 clean). 이 commit 이 건드린 것: 제품(relay·Host 89 응답·뷰어 재시도·결과 창 크기) / 테스트(서비스·relay·e2e) / 문서(history·작업목록 LK·구현계획).
- 빌드 `C:/work/gnlink-wt/fix-lk-retry/build` Release 전체 rc0(기존 C4244 경고만). 시험한 exe sha256 앞 16: GNLinkStreamMigrationTest `2384CEFD92F8FDBD` · GNLinkInputServiceLifecycleTest `3AB63196C696CFBA` · e2e `3C5A38E0B8F4FB87` · GNLinkViewer `8E20D4906B00ADDD` · GNLinkStream `82CB158B9DCCCCCD` · GNLinkInputService `79C70E6C1BF4F086`.

## 사용자 행동 (acceptance 1·2)
같은 연결에서 첫 비밀번호 입력(대기 중) → 'Windows 로그인' 다시 누름 → **비밀번호 창이 열림**(예전: "기다리는 중이라 보내지 않았습니다") → 새 비밀번호 → Host 가 서비스에 앞 대기 종료를 **먼저 확인(Superseded 17)** 한 뒤 새 challenge → 서비스 agent 가 칸 내용을 **새 비밀번호로 바이트 교체** → 결과 창 "입력했습니다 … Windows 메시지가 뜨면 원격 화면에서 닫은 뒤 'Windows 로그인'을 다시 누르세요" → 해제. **재접속 0**. GNLink 는 Windows 오류 창의 확인/Enter 를 누르지 않는다.

## 이번에 추가·수정한 것 (WIP 위)
1. relay: 끝낼 대상은 이 pipe 가 잡고 있는 **이 세션 + 그 requestId + 같은 tx generation** 의 typed 대기뿐. 이 세션의 typed 대기가 다른 request 면 아무것도 묻지 않음(18). 서비스가 답했지만 끝내지 않으면 앞 대기·pipe 그대로 유지(WIP 는 pipe 를 끊었음).
2. "끝나지 않음/아무것도 안 보냄" 의 모든 경로 = **SupersedeRefused(18)**: 재시도 미지원 서비스, capability 낡음, 시간 내 무응답(WIP 는 RejectedPolicy/InternalError 를 내 뷰어가 앞 대기를 버렸음 — 10-22 막힘이 드물게 재발할 경로). 뷰어는 18 과 무응답(0)일 때 앞 대기를 계속 따른다.
3. 결과 창: 고정 64 px 라벨이 새 안내 넷째 줄을 잘랐다(e2e 캡처로 발견) → 글 높이만큼 창이 늘어남(기존보다 작아지지 않음, 로그인 전·재부팅·SAS 결과 창 공통).

## 번호 제안 (검증용 확정 요청)
MessageType **89** `ControlLockCredRetryChallengeRequest`(32 B) · pong **0x80000** `kCaptureFlagLockCredRetryV1` · UnlockStage **17** Superseded · **18** SupersedeRefused · 서비스 IPC kind **9** `LockCredSupersede`(SecureUnlockQuery) · 기능 비트 **0x2** `kSecureUnlockFeatureLockCredRetryV1`. 대조: main(a1c551c)·spike/host-admission·feat/keepalive-r2-apk·feat/keepalive-multi 의 poc_protocol.hpp·*wire*.hpp·secure_unlock_ipc.hpp 에 89/0x80000/17/18/kind 9 없음(86·87 은 file-offer 쌍, 88 이 LK).

## 시험 (종료 코드 + 마지막 요약 줄, 이 PC 콘솔 세션 `qwinsta` console Active·RDP 없음, SLOT_REQUEST CreateNew→삭제 매회, 설치본 GNLinkInputService/Host/Stream 무조작)
| 시험 | rc | 결과 | 태그 |
|---|---|---|---|
| secure_unlock_lockcred_test | 0 | 72/72 (+12 재시도) | pure-logic |
| host_unlock_relay_ondemand_test | 0 | 57/57 (+18, 가짜 서비스 실제 pipe) | pure-logic |
| secure_unlock_service 12 · lock_cred_channel 15 · prelogin_cred_host 43 · logon_ui_target 21 · unlock_protocol · sealed_unlock | 0 | ALL PASS | pure-logic |
| e2e `--lock-screen` | 0 | **57/57** (+13 재시도) | session-privilege·display |
| e2e prelogin / `--not-prelogin` / `--session-ends` | 0 | 79/79 · 28/28 · 41/41 | 회귀 |
- `--lock-screen` 은 최종 코드로 2회, 결과 창 수정 전 단계 포함 6회 연속 통과. 첫 회 1 FAIL 은 시험이 로그 줄 끝을 LF 로 가정한 것(시험 수정).
- **부정 대조**: ① 뷰어 게이트를 예전 pending 거절로 되돌린 mutant → 10-22 문구 그대로, 재시도 7 항목 FAIL rc1 ② 결과 창 크기 고정 mutant → "text 96 px in a 64 px label" FAIL rc1. 복원 후 PASS.
- 화면 증거(e2e 캡처, 제품 창 프로시저의 실제 창): `.claude/lkretry/shots2/lock_screen_retry_typed.png`(전문 표시), `shots/lock_screen_retry_cancelled.bmp`, `shots/lock_screen_retry_target_received.png`(새 비밀번호 15자 마스킹 칸). 수정 전 잘린 창: `shots/lock_screen_retry_typed.png`.
- Codex §5 항목 대응: 같은 연결 입력→재입력→바이트 교체→성공·reconnect0(e2e) / typed 전 cancel(e2e 재시도 창 취소: 아무것도 안 감·앞 대기 유지) / 새 시도 중 옛 결과·옛 status(서비스·relay: 새 것 불변) / 옛 seal 재전송(입력 0) / 다른 Host Busy 무간섭(relay: 다른 세션 89 → Busy, supersede·challenge 0) / 해제 후 정상(e2e) / 비밀번호 로그 0(e2e 기존 검사가 `Pw-7Qz` 머리 공유로 두 번째 비밀번호도 덮음, 디스크 검색에 추가) / 자동 Enter·재전송 0(typed 2회 = 비밀번호당 1).
  - **미실시**: 입력 중간(agent 타이핑 중) cancel — 서비스는 한 pipe 에서 요청을 하나씩 처리하고 타이핑이 끝나야 다음 요청을 읽으므로 그 사이에 supersede 가 끼어들 수 없다(코드 근거, 시험 아님). 새 topology 중 재시도 — 새 challenge 는 기존 fence(topology·잠김·활성·계정·expiry)를 그대로 다시 거친다(기존 시험이 덮음, 재시도 전용 시험 없음).
- 미실행: `dxgi_wedge_reconnect_runner`(client_reconnect_restore) — GPU wedge 장시간 시험이라 10-22 AMD 크래시 주의로 R1 통합에서 1회로 미룸. 이 변경은 pong 비트 추가뿐이고 그 시험이 세는 `advertised=1` 줄은 불변.

## 호환 조합
- 새 뷰어 + 구 Host(0x80000 없음): 기존 "기다리는 중" 문구, 89 안 보냄 — 코드 근거(e2e mutant 가 같은 화면을 보임).
- 새 Host + 구 뷰어: 89 를 보내지 않으므로 기존 동작 — 코드 근거.
- 새 Host + 구 서비스(RetryV1 없음): 0x80000 광고 안 함, 89 가 와도 18·전송 0 — **relay 시험으로 확인**.

## 보류·한계
- **§3 인증 실패 자동 감지 보류**: 실제 LogonUI 의 실패 UI 상태 전이 근거가 없다. 사용자 콘솔을 잠그는 시험은 task 규칙상 금지, 기존 `logon_ui_identity_ui_test` fixture 는 비밀번호 칸만 재현한다. 그래서 LockCred 용 AuthFailed 문구도 넣지 않았다(이 빌드에서 도달 불가). §2 는 §3 없이 완결.
- 알려진 한계: 앞 대기가 해제로 끝난 직후 누른 재시도는 새 challenge 가 "잠금 화면이 아님" 으로 거절돼 '해제됨' 대신 그 문구가 보인다.
- fixture 와 제품의 차이: 서비스는 lifecycle 시험 빌드(이 사용자·Default 데스크톱, SYSTEM 아님), 입력 대상은 시험 창의 비밀번호 EDIT(LogonUI 아님), 잠김은 이벤트 대역. **실제 LogonUI·Winlogon·SYSTEM 에서 틀린 비밀번호 → Windows 거절 → 다시 누름 → 해제는 사용자 실기(R1 게시 뒤 1회)** — 테스트 후보·실기 대기.

## 다음
작업용1 ② PS+UA 캡처 수명 묶음 — Codex `codex_rel_0163_r1.md` 는 이미 있음. 검증용 착수 통지(task_capture_lifecycle 확정본) 대기, 그동안 판독 문서 선독.

## r2 — Codex SOURCE N1·N2 보수 (2026-10-26) → 고정 후보 **598339a** (9991b36 위 1 commit, clean)
- 근거 `codex_lk_ux_retry_src_r1.md`. 이 commit: 제품(relay 시한·fence, 뷰어 결과 분리·문구) / 테스트(relay N1 반례, e2e N2 반례 + e2e 대상 전용 `REMOTE60_VIEWER_UNLOCK_TEST_SEAM`) / 문서(history, 번호 주석 PROPOSED→배정).
- N1: 재시도 전체에 절대 시한 하나(호출자 대기보다 최대 250 ms 안쪽). 호출자 timeout(18) 시 시작 안 한 재시도는 큐에서 제거·transaction 회수, worker 는 시작 직전·supersede 뒤 새 challenge 직전에 소유·시한 재확인, 각 IPC 는 시한까지만. supersede 가 이미 끝났으면 A 는 자기 17 을 남기고 재시도는 17 로 답(새 challenge·hold 0).
- N2: A 종료 = 재시도 challenge 발급(또는 재시도 답 17)일 때만. 이후 새 비밀번호의 봉인 전 실패 / 전송 후 무응답을 따로 안내(kLockRetryNotSent 0xFFFA / kLockRetryUnknown 0xFFF9 — 뷰어 내부 코드, wire 아님). A 의 늦은 17 은 15('2분')로 바꾸지 않음.
- 시험 rc0: relay ondemand 70/70 ×5(①시작 전 회수 ②supersede 가 시한 뒤 ②b supersede 성공 후 challenge 지연 ③supersede 중 EndSession) · lockcred 72 · service 12 · e2e `--lock-screen` 62/62(N2 반례 포함) · prelogin 79 · `--not-prelogin` 28. SLOT_REQUEST CreateNew→삭제.
- 부정 대조: M5(N1 되돌림) → 18 뒤 supersede 1·challenge 1 등 5 FAIL / M6(N2 되돌림) → '응답하지 않았습니다' + 다음 누름 '2분 안에 … 확인되지 않았습니다' 3 FAIL. 복원 후 위 PASS.

## r3 — Codex SOURCE 재확인 잔여 N1·N2-a·N2-b (2026-10-26) → 고정 후보 **9ff6c15** (598339a 위 1 commit, clean)
- 근거 `codex_lk_ux_retry_src_r2.md`. 이 commit: 제품(relay 쓰기 gate·채택 fence, 뷰어 추적·표식 순서·문구) / 테스트(relay latch 반례, 신규 `viewer_lock_cred_retry_test`, e2e N2-a + e2e 전용 seam `GNLINK_VIEWER_TEST_STATUS_POLLS`) / 문서(history).
- N1: 쓰기는 `Exchange` 안에서 mu_ 아래 소유·절대 시한 확인 + 같은 임계 구역에서 WriteFile 개시(호출자 회수와 직렬, 시한 뒤엔 시작 거절 — 1 ms 연장 없음). 새 challenge 결과는 채택 직전 재확인, 늦은 발급은 채택 없이 17.
- N2-a: 봉인 송신 뒤 답 없음 → 그 요청을 계속 추적(다음 누름 = 그 요청을 지목한 재시도). 입력 전 종료는 그 결과 그대로(‘2분’ 아님). 재전송 없음.
- N2-b: 앞 대기 종료 표식은 requestId·purpose 검증 통과 뒤에만.
- 시험 rc0: relay 81/81 ×3((4) challenge 쓰기 직전 latch 중 호출자 회수 (5) supersede 쓰기 직전 회수 (6) 쓰기 직전 EndSession (7) 시한 뒤·호출자 대기 중 채택 직전) · viewer_lock_cred_retry_test 5/5(같은 RID 틀린 purpose 발급/17, 다른 RID) · e2e `--lock-screen` 68/68 ×2(N2-a 포함) · prelogin 79 · `--not-prelogin` 28.
- 부정 대조: M7(gate·fence 제거) 5 FAIL(늦은 challenge 쓰기, 시한 뒤 발급 채택+hold) / M8(N2-a 되돌림) 3 FAIL(다음 누름이 일반 요청으로 거절 — 원래 UX) / M9(purpose 전 표식) 2 FAIL. 복원 후 PASS.

## r4 — N1 시한 적용 잔여 2곳 (2026-10-26) → 고정 후보 **8d8655e** (9ff6c15 위 1 commit, clean)
- 근거 `codex_lk_ux_retry_src_r3.md`. 이 commit: 제품(relay Exchange 끝·채택 구역) / 테스트(relay (8)) / 문서(history).
- (1) 재시도 Exchange 의 쓰기·읽기 대기 끝 = min(지금+상대 budget, 재시도 절대 끝). (2) 새 challenge 채택은 hold/cache 하는 한 mu_ 구역 안에서 소유+절대 끝을 함께 검사(예비 검사 제거, hook 은 그 구역 바로 앞).
- 시험: relay 84/84 ×4 rc0 — (8) 끝 직전 쓰기(≈1650/1750 ms)+서비스 900 ms 지연 → 1746 ms 에 17(호출자 2000 ms 대기 안), hold 0 / (7) 끝 뒤·호출자 대기 중 채택 직전 latch → 17, hold 0.
- 부정 대조: M10(clamp 제거) → (8) FAIL(2004 ms 무응답) / M11(채택 구역 시한 미검사) → (7) FAIL(status 3 채택·hold). 복원 후 PASS.
