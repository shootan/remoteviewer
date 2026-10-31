너는 GNLink 프로젝트(D:\remote\remote)의 **작업용1 (remote_작업용1)** 이다. 이전 세션을 clear 하고 이어받는다. 이전 대화 맥락은 없다고 보고 아래 파일로 복원한다.

먼저 할 일
1. `CLAUDE.md`·`AGENTS.md` 를 읽고 `a2a_whoami` 로 내 주소, `a2a_agents` 로 검증용 `remote_claude` 주소를 확인한다(보고·질문은 그 identity+sessionId 로 pin).
2. 내 정지 메모 `C:/work/gnlink-wt/fix-catchup-only/.claude/PAUSE_NOTE.md` 를 읽는다.
3. 현재 지시서 정본을 전부 읽는다: `C:/work/gnlink-wt/fix-motion-settle/.claude/task_motion_settle_r8.md`, 계약 `C:/work/gnlink-wt/fix-motion-settle/.claude/codex_motion_settle_r8.md`(필요하면 이전 codex_motion_settle_r5_idr~r7, motion_settle_proposal_2026-10-12.md 의 Codex 절).

현재 작업 — task `t-0wsagafw` motion-settle r8 (catch-up 단독 좁힌 후보)
- 목표: 입력 gate 가 건너뛴 **마지막 실제 화면을 1 Hz periodic refresh 전에 전달(T_final)**. 글자 재정제·motion-settle 전체 해결로 보고하지 않는다. 반응·화질 무회귀 유지.
- 후보: branch `fix/catchup-only` @`f933b6f`, worktree `C:/work/gnlink-wt/fix-catchup-only`(측정 `.claude/m8runs/`). 연구 경로 브랜치는 `fix/motion-settle`(@`86dd057`, worktree `fix-motion-settle`) — 연구 경로 전량을 main 에 넣지 않는다.
- 남은 일(지시서 §할 일): 좁힌 후보에서 SETTLE 기본값·live30·3 s 제거·강제 env 로도 안 켜짐 + build gate(+부정 대조), 반례(`NoteInputAdmitted`(:353)·최신 재검사(:347) 경계: 실제 수락·최신 identity·중복 0, gate skip→queue→수락→receiver final marker 사슬, OFF 대조, 미수락/hold/새 내용/epoch·pause), ON/OFF **3회 짝 비교**(GOP 위상 결합).
- 보고: `C:/work/gnlink-wt/fix-motion-settle/.claude/report_motion_settle_r1.md` 에 §11 로 1회 → 검증용에 pin 으로 1~3줄 알림. 중간 보고·ACK 불필요.

규칙
- **측정 슬롯**: 각 run 시작 직전에 `C:\work\gnlink-wt\SLOT_REQUEST.txt` 가 있으면 run 을 시작하지 말고 파일이 없어질 때까지(상한 60분) 기다린다. 다른 담당과 겹친 회차는 보고서에 표시하고 필요하면 재측정.
- 다른 worktree 수정·main 머지·push·게시 금지. 사용자 화면·실행 중인 제품 호스트 조작 금지. 커밋은 Git MCP(D: 고정 클라이언트를 worktree 경로로 바꾼 사본), 메시지 끝 "제품/테스트/문서" 한 줄.
- worktree 의 `third_party\webview2` 는 junction: `Remove-Item -Recurse`·`git worktree remove --force` 전 반드시 재귀 없이 끊는다.
- 사용자에게 직접 보고하지 않는다(검증용이 창구).
