
### 2026-10-26 lk-ux-retry r2 — Codex SOURCE N1·N2 보수 (fix/lk-retry, 미게시 · R1 통합 대기)
- 근거: `codex_lk_ux_retry_src_r1.md`(9991b36 SOURCE NEEDS_CHANGES N1 P1 / N2 P2). 번호는 검증용 확정(89·0x80000·17/18·IPC 9·0x2) — 주석의 'PROPOSED' 를 '배정됨' 으로.
- N1(재시도의 실행권): 재시도 전체(큐 대기·supersede·새 challenge)에 하나의 절대 시한(호출자 대기보다 조금 안쪽, 최대 250 ms)을 붙였다. 호출자가 18 로 답하면 시작 안 한 재시도는 큐에서 빠지고 그 transaction 은 회수된다. worker 는 시작 직전·supersede 뒤 새 challenge 직전에 '살아 있고 시한 안' 을 다시 확인하고, 각 IPC 는 시한까지만 기다린다. supersede 가 이미 끝났으면 A 는 자기 실제 종료(17)를 남기고, 재시도에는 새 challenge·hold 를 만들지 않고 17 로 답한다('앞 대기는 끝났고 이번 비밀번호는 보내지 않음').
- N2(앞 대기 종료와 새 전송 결과의 분리): 뷰어는 재시도의 challenge 가 발급된 순간(또는 재시도 답이 17)에만 A 를 끝난 것으로 본다. 그 뒤 새 비밀번호가 봉인 전에 실패하면 '대기는 끝냈지만 새 비밀번호는 보내지 못함', 보냈지만 답이 없으면 '보냈지만 답을 받지 못함' 으로 따로 안내한다. A 의 늦은 17 은 '2분 미확인(15)' 으로 바꾸지 않는다.
- 시험(이 PC 콘솔, SLOT_REQUEST CreateNew→삭제): relay ondemand 70/70 ×5회 rc0(N1 반례 ①시작 전 회수 ②supersede 가 시한 뒤 ②b supersede 성공 후 challenge 지연 ③supersede 중 EndSession). e2e `--lock-screen` 62/62(N2: 재시도 challenge 발급 뒤 봉인 전 실패 주입 — e2e 대상에만 정의된 REMOTE60_VIEWER_UNLOCK_TEST_SEAM, 출하 뷰어에는 없음). 
- 부정 대조: M5(N1 되돌림) → 18 뒤 supersede 1·새 challenge 1 실행 등 5 FAIL. M6(N2 되돌림) → '원격 PC가 응답하지 않았습니다' + 다음 누름에서 '2분 안에 … 확인되지 않았습니다' 3 FAIL. 복원 뒤 PASS.
- 제품/테스트/문서: 제품(relay 시한·fence, 뷰어 결과 분리·문구), 테스트(relay N1 반례, e2e N2 반례 + e2e 전용 seam), 문서(이 항목·번호 주석).
