

---

# r5 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r4.md` P1 3건 (R1-a · R3-a · R3-b)

## 고정 후보

- `feat/ka-pc` **1c321d1** (42782c2 위 1 commit, tracked clean, `git diff --check` rc 0).
- r5 제품 변경 (42782c2..1c321d1, 9파일 +442/-23 중 제품):
  - `client_shell_main.cpp`
  - `viewer_control_client.cpp`
  - `viewer_control_state.hpp`
  - `viewer_session_state.hpp`
  - `viewer_window_proc.cpp`
  - `viewer_owner_fence.hpp` (새 header)
- 나머지는 시험 2개와 CMake 다. 공용 `standby_*`·guard 파일 변경은 0이다.
- 빌드 sha256 앞 16자리:
  - GNLinkViewer.exe cc5e426245d6c5cb
  - GNLinkClient.exe f14c5942e7f2bf8d
  - GNLinkViewerKaTest.exe a99bf4f751245ff0
  - remote60_ka_pc_fence_test.exe 45ff6844b29d368f
  - remote60_ka_pc_shell_logic_test.exe 166b6e91320d8a60
- 출하 GNLinkViewer 에서 `REMOTE60_VIEWER_TEST` 는 0건이다.
- 실 Host 실행은 0이다(검증용 지시).

## 보수

**R1-a — 교체로 남은 viewer 를 그 PC 로 센다**
- `ViewerCancelSlot` 에 `hostId` 를 넣었다. struct 끝에 두어 기존 brace 초기화의 의미가 바뀌지 않는다.
- `logical_pcs` 는 `gReplacedViewers` 를 PC 별 합집합에 넣는다.
- `pc_visible` 도 이 viewer 를 화면에 있는 것으로 본다. 그래서 standby 를 축출할 때도 이 PC 를 보이지 않는 것으로 취급하지 않는다.
- 한 PC 의 viewer 두 개(남은 옛 것과 새 교체)는 한 PC 로 센다.

**R3-a — fence 확인은 InputAck 로만**
- control thread 가 실행한 입력 교환을 센다: `taken` / `acked`(Host 의 InputAck) / `failed`.
- fence 는 release 를 넣기 **전에** 이 카운터를 기록한다.
- `fence_release_verdict` 판정:
  - Confirmed: 그 뒤 시작된 교환이 모두 acked 됐을 때만.
  - Failed: 하나라도 실패하면 즉시 "NOT confirmed (an exchange failed)" 로 닫는다.
  - 2 s budget 초과는 "NOT confirmed within the budget".
- queue 가 비었거나 channel 이 비었다는 것은 보조 조건일 뿐이고, ack 로 치지 않는다.

**R3-b — fence 뒤에는 release 만 간다**
- fence 때 UI thread 가 즉시 하는 일 (`owner_fence_cancel_pending`):
  - 큐에 남은 release 가 아닌 입력(down·text·move·wheel)을 버린다.
  - pending clipboard text 를 zero 하고, clipboard sync 를 끈다(`enabled=false` 이므로 `pump_clipboard_sync` 가 보내지도 poll 하지도 않는다).
  - 봉인 대기 중인 비밀번호를 zero 한다.
  - 이미지·파일 offer 를 `Stop` 한다.
- 그 뒤 control thread 는 **release 만** 큐에서 바로 꺼내 보낸다(`take_release_action`).
  - ping 예약 작업, pick, stream state, clipboard, credential, IME align 은 보내지 않는다.
  - release 교환이 실패하면 resume 하지 않는다.
- fence 상태는 atomic `ownerFenceAtMs` 로 읽는다. UI bool 을 무동기로 읽지 않는다.

## 회귀 시험 — 보수 전 FAIL → 보수 후 PASS (실 Host·창·네트워크·clipboard API·실 비밀번호 없음)

| 건 | 시험 | 보수 전 | 보수 후 |
|---|---|---|---|
| R1-a | `ka_pc_shell_logic_test`: 셸의 **실제** `logical_pcs`/`pc_visible`/`reserve_for_new_pc` 를 메모리 registry 로 실행한다. replaced A 는 살아 있고 map 에는 B·C 만 있다 | r5 두 줄을 뺀 mutant: **FAIL 3** — logical=2, A 가 보이지 않음, "D admitted (four PCs open)". 로그 `np_impl_logs/ka_r5_logic_neg.log` | **ALL PASS** — 3대로 세고 D 거부, 같은 PC 의 두 viewer 는 1대 |
| R3-a | `ka_pc_fence_test`: 마지막 release 교환이 실패(ack 1, fail 1)한 뒤 queue 가 비고 inFlight 도 없는 상황 | d81148e 의 판정식(원문 복사) → **delivered=1** (결함 재현) | `fence_release_verdict` → **Failed**. 정상 경로(모두 ack)는 Confirmed, 교환 중이면 Pending, fence 이전에 진행 중이던 교환은 그대로 Confirmed |
| R3-b | 같은 시험: pending clipboard 와 비밀번호, 큐에 down·text·up | r4 fence 는 이 상태를 건드리지 않음 → pump gate(`enabled && hostSupports`)가 열려 있고 pending 이 남음 → **보낸다** | clipboard 꺼짐 + pending 없음, 비밀번호 zero, down·text 는 버리고 **release 1건만** 나간다 |

- mutant 는 소스를 임시로 바꿔 만들었다. 대조 뒤 원본을 복원했고 sha256 db57b771… 로 일치를 확인했다. 재빌드 뒤 다시 PASS 했다.
- 그 밖에 실 Host 가 필요 없는 시험: units, IPC race, 공용 injection guard 시험 — 모두 PASS.

## 실행하지 않은 것

- 실 Host e2e (managed·shell). 공용 guard 의 Codex SOURCE OK 와 슬롯 해제를 기다리는 중이다.
- 시험 빌드 전용 `REMOTE60_VIEWER_TEST_FAIL_RELEASE_AFTER_FENCE=1` (fence 뒤 첫 release 교환을 실패시킴)은 넣어 두었다. 이것을 쓰는 실 viewer 경로 시험(실패 → "NOT confirmed")은 그 실행 때 함께 붙인다.
- 실제 IME 조합·클립보드 조작·자격증명을 쓴 확인은 하지 않는다(사용자 PC). 기록 fixture 로만 확인했다.
