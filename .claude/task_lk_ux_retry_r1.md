# task lk-ux-retry r1 — 잠금 화면 'Windows 로그인' 재시도 UX (재접속 없는 명시 재시도 + 실패 감지 조건부) (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용1

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로.** 구속 계약: `.claude/codex_field_0162_0231.md` §2·§3·§5(LK 필수 자동 행동). 판독: `.claude/field_0162_0231_2026-10-22.md`.
**우선순위**: keepalive-r2 host spike(t-y1lnbgnd)보다 먼저 — spike 는 현재 단계 WIP commit 으로 보존 후 이것부터. 같은 native/서비스 파일 writer 는 당신 한 명.

## 사용자 실기(0.2.162, 10-22)
원격 잠금 해제 성공 2회. 일부러 틀린 비밀번호를 넣은 뒤 다시 누르면 "앞 요청 처리 중"(typed 후 해제 대기, 상한 120 s)으로 막혀 **재접속으로만** 풀 수 있었다.

## 사용자 행동 acceptance
1. 같은 연결에서 틀린 비밀번호 → Windows 가 거절 → 'Windows 로그인'(또는 Ctrl+Alt+U)을 **다시 누르면** 이전 대기를 끝내고 새 비밀번호 창이 뜬다 → 바른 비밀번호 → 칸 내용이 바이트 교체되어 Enter 후 해제. **재접속 0.**
2. Windows 오류 창(예: 암호가 틀림)을 사용자가 닫아야 하는 상황이면 그 사실과 다시 할 방법을 안내한다(GNLink 가 확인/Enter 를 대신 누르지 않는다).
3. (조건부) 이번 시도에 귀속되는 명확한 인증 실패를 감지하면 빠르게 종료하고 사실대로 안내("저장한 암호를 지웠다" 같은 거짓 문구 금지).

## 범위 (Codex 계약 요약)
- §2 대체 권한: 현재 인증 control 세션 cookie + 기존 requestId + tx generation + LockCred purpose 소유자만. 다른 연결의 새 요청은 기존 Busy. 이전 작업 cancel·비밀 정리·one-shot child/입력 commit 차단 확인 뒤 **새 challenge**(새 requestId/txGen/expiry/AEAD) 직렬 발급 — 서비스·relay·viewer 원자 직렬화. typed 대기 상태와 입력 중 상태 구분. 새 시도 직전 잠김·LogonUI 필드 최종 fence 재검증. 옛 typed/AuthFailed/unlocked/timeout 응답·옛 callback 이 새 transaction 을 못 건드림. cancel 미확인이면 입력 0 + 안내. 같은 sealed 재전송 멱등. 120 s 는 판정 불가 fallback 상한 — terminal(AuthFailed·취소·해제·세션 종료)이면 즉시 파이프 반환, 중복이 마감 연장 금지. 새 capability/취소 IPC 는 구 서비스 조합 유지(구 서비스에 대체된 척 금지). **새 상태 번호는 프로토콜 전수 대조 후 검증용이 확정**(제안해 주면 확정 회신).
- §3 실패 감지(조건부): SYSTEM·대상 세션·Winlogon·LogonUI 신뢰 검증 + **새 관찰 수명 경계**(비밀 없는 bounded 관찰 helper, 같은 보호 IPC/Job/시한/cancel; 일반 입력 agent 에 범용 UIA 명령 추가 금지). 필드/LogonUI 인스턴스·계정/세션·attempt generation 결합, 입력 전 오래된 실패 UI 와 구분. 미지원·타임아웃 = 판정 불가. 계정 잠김·정책·연결 오류와 비밀번호 거절 구분. **먼저 실제 LogonUI 의 비민감 UIA 구조/상태 전이 근거(또는 동등 fixture) 확보** — 근거가 안 나오면 이 항목만 근거와 함께 보류하고 §2 를 완료.
- 일반 평문 Backspace/Delete/문자 허용으로 우회 금지. prelogin/WTSConnectSession/UAC 경계 불변.

## 시험 (Codex §5 LK 필수 자동 행동)
같은 실제 control 연결: 입력 → 제출 실패 → 오류/판정 또는 미지원 → 명시 다시 입력 → 새 challenge·필드 바이트 교체 → 성공, reconnect 0. typed 전 cancel / 중간 cancel / 새 시도 중 옛 결과 / 옛 seal 재전송 / 새 topology / 다른 Host Busy 무간섭 / 해제 후 정상. 비밀번호·sealed 원문 로그 0, 의도치 않은 자동 Enter·재전송 0. 실 OS 실패 UIA 상태와 fixture 증거 구분. 기존 LK e2e(--lock-screen·prelogin·not-prelogin) 회귀.

## 규칙
새 worktree·브랜치(main 82b3d14 기준, 예 `C:/work/gnlink-wt/fix-lk-retry`, `fix/lk-retry`). repo root 밖 삭제 금지(생성만), 설치본·서비스·세션 무조작(실 잠금 시험은 실기 묶음으로), 버전·서명·게시·push 금지, 측정 슬롯 CreateNew, 커밋 Git MCP, history 는 task 커밋에.

## 보고 (1회)
`.claude/report_lk_ux_retry_r1.md` + `remote#wwml7h91` pin.
