# integ-0162 r1 — 제품 코드 충돌 1건 (작업용2 → 검증용, 2026-10-22)

- worktree `C:/work/gnlink-wt/integ-0162`, 브랜치 `integ/0.2.162`.
  - LK merge 는 커밋됨: **29c9c23** (parents 83cbd78 + e728485). 충돌은 docs/history.md 하나였고 두 항목을 모두 보존했다.
- DW merge(fc4cd57)는 진행 중이고 아직 커밋하지 않았다. 충돌 3개:
  - `automation/gnlink_check_fixed_server.py`: TEST_MARKERS 합집합으로 해소. self-test 23/23 이고 LK·DW 표식 모두 부정 대조된다.
  - `docs/history.md`: 두 항목 모두 보존해 해소.
  - **`apps/native_poc/src/viewer_control_client.cpp`: 미해소, 판단 대기.**
- 겹치는 파일 중 자동 병합된 것: `CMakeLists.txt`, `host_control_session.cpp`, `viewer_window_proc.cpp`. 통합 시험에서 함께 본다.

## 충돌 — `ControlClient::Run()` 첫 줄
```
<<<<<<< HEAD (LK e728485)
  const uint64_t runGen = ctx.session.controlRunGen.fetch_add(1, std::memory_order_acq_rel) + 1;  // r5 G11
=======
  ctx.session.controlRunGen.fetch_add(1, std::memory_order_acq_rel);  // r5 G11: a new connection's run
  // dxgi-wedge-fix r4: the screen capability (and so the one screen-list request) belongs to THIS connection.
  ctx.picker.windowPanel.SetHostSupportsMonitors(false);
>>>>>>> fix/dxgi-quarantine
```
- **LK 의 의도**: 새 연결의 runGen 값을 받아 둔다. 바로 아래(충돌 밖, 자동 병합됨)의 L5 블록이 이 값을 쓴다. 이전 연결이 남긴 비밀번호 대기가 있으면 0 으로 지우고 버린다.
- **DW 의 의도**: monitor capability 를 연결마다 초기화한다. 그래서 화면 목록 요청이 연결당 1회가 된다.
- 같은 줄의 fetch_add 를 한쪽은 값을 받아 쓰고, 다른 쪽은 버리고 한 줄을 덧붙였을 뿐이다. 의미가 겹치지는 않는다.

## 제안 (확정 전에는 적용하지 않음)
```
  const uint64_t runGen = ctx.session.controlRunGen.fetch_add(1, std::memory_order_acq_rel) + 1;  // r5 G11: a new connection's run
  // dxgi-wedge-fix r4: the screen capability (and so the one screen-list request) belongs to THIS connection.
  ctx.picker.windowPanel.SetHostSupportsMonitors(false);
```
- fetch_add 는 한 번만 한다. 두 번 하면 runGen 이 어긋난다.
- 순서상 영향은 없다. monitor 초기화는 L5 의 비밀번호 정리와 서로 독립이다.
- 확정되면 바로 적용하고 merge 를 커밋한다. 그다음 빌드와 통합 시험(지시서 4)을 이어서 하고 보고는 1회로 낸다.
