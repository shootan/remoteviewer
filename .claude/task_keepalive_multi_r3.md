# task keepalive-multi / r3 — 클립보드 결합 시험 1건 (검증용 remote#pvbntb3z, 2026-10-16)

**r2 를 대체하는 통합본.** 담당 작업용2, task `t-f4nz270q`. 보고는 검증용 remote#pvbntb3z pin.
계약 최신: **`codex_keepalive_multi_92956b1.md`(SOURCE OK, M1/M2/L1 종결)** + 이전 판정. 기준 HEAD `92956b1`.

## r2 → r3 변경 요약
- 종결: M1/M2/L1, 검증용 독립 6-host 35/35·TTL 15/15·r1 부정대조 21/35. **제품 보수 라운드 아님.**
- 남은 것: (1) 정책값(3대·5분) 사용자 답 — 검증용이 받는다. (2) **클립보드 결합 근거 1건**(아래). 6-host fixture 는 clipboard 를 꺼 둬서 기능 누출 0 을 입증하지 못했다.

## 할 일 — 클립보드 결합 한 묶음 (계약 §종결된 지적 2)
- 실제 사용자 PC/호스트 클립보드를 건드리지 않는 **기존 격리 station / no-capture parkclip + 모의 로컬 clipboard sink**, 또는 동등한 **제품 controller / entry / binding 결합 시험**으로. 제품 클립보드 handler 를 통과하고 **적용 대상 / 실제 sink 호출까지 관측**할 것(단위 state reset 을 다시 세는 것으로 대체 금지). 사용자 clipboard·물리 폰 불필요.
- 단언(한 실행 안에서):
  1. A 의 수신 텍스트를 보류 → A park / B active → **늦은 A 수신·정리 해제 → B 또는 로컬 적용 0**.
  2. B 의 새 복사는 **B 로만**.
  3. A 복귀 뒤 **park 중 텍스트 / 옛 pending push 재생 0**, 그리고 **새 복사는 정상 전달**.
- 실패하면 그 범위만 수정(제품 delta 가 생기면 보고에 diff 범위 명시 — 검증용이 영향 범위만 재확인).
- 이전 parkclip 시험 host 는 no-capture(사용자 화면 캡처 0)였다 — 같은 안전 경계 유지, 잘못된 제목 대조를 사용자 데스크톱에서 실행 금지.

## 작업 공간·규칙 (동일)
`C:/work/gnlink-wt/feat-keepalive-multi`. junction 재귀 삭제 금지. 측정 슬롯은 CreateNew 로 먼저 요청 → 작업용1 의 가장 최근 `fix-catchup-only/.claude/*runs.log` 마지막 줄 end 확인(매번) → 실행 → 삭제. main 머지·push·게시·버전 변경 금지. Git MCP 커밋, "제품/테스트/문서" 한 줄.

## 보고 (1회)
`.claude/report_keepalive_multi_r3.md`: HEAD(변경 없으면 92956b1)·시험 구성(어떤 station/sink, 제품 경로 어디까지), 단언 1~3 결과와 sink 호출 기록, 제품 delta 유무. 검증용에 1~3줄 pin.
