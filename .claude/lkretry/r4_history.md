
### 2026-10-26 lk-ux-retry r4 — N1 시한 적용 잔여 2곳 (fix/lk-retry, 미게시 · R1 통합 대기)
- 근거: `codex_lk_ux_retry_src_r3.md`(9ff6c15: N2-a/b·쓰기 개시 닫힘, 시한 적용 2곳 잔여).
- (1) 재시도의 `Exchange` 대기 끝 = min(지금 + 상대 budget, 재시도의 절대 끝) — 쓰기와 읽기 모두. 끝 직전에 시작한 쓰기의 답 대기가 옛 상대 budget 으로 넘어가지 않는다.
- (2) 새 challenge 의 채택(hold·cache) 결정은 그 hold/cache 를 하는 한 임계 구역 안에서 '소유 + 절대 시한' 을 함께 본다(별도 예비 검사 제거, 시험 hook 은 그 구역 바로 앞).
- 시험: relay ondemand 84/84 ×4 rc0(+N1 (8): 끝 직전 쓰기·서비스 900 ms 지연 → 1746 ms 에 17, caller 대기 안). 부정 대조: M10(clamp 제거) → (8) FAIL(2004 ms, 답 없음) / M11(채택 구역에서 시한 미검사) → (7) FAIL(늦은 발급 채택·hold). 복원 뒤 PASS.
- 제품/테스트/문서: 제품(relay Exchange 끝·채택 구역) / 테스트(relay (8), (7) 이 채택 구역 앞 hook 으로) / 문서(이 항목).
