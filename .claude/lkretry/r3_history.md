
### 2026-10-26 lk-ux-retry r3 — Codex SOURCE 재확인 잔여 N1·N2-a·N2-b 보수 (fix/lk-retry, 미게시 · R1 통합 대기)
- 근거: `codex_lk_ux_retry_src_r2.md`(598339a SOURCE NEEDS_CHANGES 잔여 3건).
- N1(I/O 개시 선형화): 재시도의 쓰기는 `Exchange` 안에서 mu_ 를 잡은 채 '소유(live)·절대 시한' 을 확인하고 같은 임계 구역에서 시작한다 — 호출자 회수는 그 앞(아무것도 안 씀, Io::Withdrawn)이거나 뒤. 시한이 지나면 1 ms 연장 대신 시작 거절. 새 challenge 의 결과는 hold/cache 채택 직전에 다시 소유·시한을 확인하고, 늦은 '발급' 은 채택하지 않고 17(앞 대기 끝남, 새 challenge 없음)로 답한다. 시험용 hook(`SetRetryIoHookForTest`)으로 fence 뒤·쓰기 직전과 채택 직전에서 worker 를 멈춘다.
- N2-a: 새 비밀번호를 보낸(봉인 송신) 뒤 답을 못 받은 경우 그 요청을 계속 따라간다(대기 상태 유지) — 다음 누름은 그 요청을 지목하는 재시도(앞 대기 종료 먼저)가 되고, 'pending' 거절로 막히지 않는다. 암호 재전송 없음. 그 요청이 입력 전에 끝나면(칸 없음·실패·만료 등) 그 결과를 그대로 말하고 '2분' 으로 바꾸지 않는다.
- N2-b: '앞 대기 끝남' 표식(challengeIssued / supersededAnswer)은 requestId·purpose 검증을 모두 통과한 답에서만 세운다.
- 시험(이 PC 콘솔, SLOT_REQUEST CreateNew→삭제): relay ondemand 81/81 ×3 rc0(+N1 (4)~(7): 쓰기 직전 latch 중 호출자 회수 / supersede 쓰기 직전 회수 / 쓰기 직전 EndSession / 시한 뒤 채택 직전). 새 `viewer_lock_cred_retry_test` 5/5(가짜 control link 의 같은 RID·틀린 purpose 발급/17, 다른 RID). e2e `--lock-screen` 68/68 ×2(+N2-a: 시험 전용 `GNLINK_VIEWER_TEST_STATUS_POLLS=0` 로 '보냈으나 답 없음' 재현 → 계속 추적 → 늦게 입력 → 다음 누름이 그 요청을 지목한 재시도, reconnect 0) · prelogin 79/79 · `--not-prelogin` 28/28.
- 부정 대조: M7(쓰기 gate·채택 fence 제거) → (4)~(7) 5 FAIL(늦은 challenge 1회 쓰기, 시한 뒤 발급 채택·hold). M8(N2-a 되돌림) → 다음 누름이 일반 요청으로 거절('잠금 화면이 아니어서') 3 FAIL. M9(purpose 검증 전 표식) → 2 FAIL. 복원 뒤 PASS.
- 제품/테스트/문서: 제품(relay 쓰기 gate·채택 fence, 뷰어 추적·표식 순서·문구) / 테스트(relay latch 반례, viewer 단위 시험 신규, e2e N2-a + e2e 전용 seam 확장) / 문서(이 항목).
