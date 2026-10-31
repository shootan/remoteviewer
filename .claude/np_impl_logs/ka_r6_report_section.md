

---

# r6 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r5.md` F1 · F2 · T1

## 고정 후보

- `feat/ka-pc` **e1acbb5** (1c321d1 위 1 commit, tracked clean, `git diff --check` rc 0, 8파일 +514/-235)
- 제품 파일
  - `viewer_owner_fence.hpp` (다시 씀)
  - `viewer_control_state.hpp`
  - `viewer_session_state.hpp`
  - `viewer_window_proc.cpp`
  - `viewer_control_client.cpp`
  - `viewer_clip_image_wiring.cpp`
- 시험: `ka_pc_fence_test.cpp`, `ka_pc_shell_logic_test.cpp`
- 셸(`client_shell_main.cpp`) 변경 0, 공용 standby·guard 파일 변경 0
- 빌드 sha256 앞 16자리
  - GNLinkViewer.exe f3349791928c09c7
  - GNLinkClient.exe f14c5942e7f2bf8d (r5 와 같음)
  - GNLinkViewerKaTest.exe ae1b6d8c1c878f90
  - remote60_ka_pc_fence_test.exe 6ceaaaf9a25e513d
  - remote60_ka_pc_shell_logic_test.exe 9aaeb557832087fd
- 출하 GNLinkViewer 에 두 시험 env marker(`…HOLD_SEND_AFTER_FENCE_MS`, `…REFUSE_RELEASE_WRITE_AFTER_FENCE`)가 ASCII·UTF-16 모두 0건이다.
- 실 Host 실행 0건.

## 보수 — fence 를 하나의 lock 아래 하나의 상태로

`OwnerFenceGate` 는 다음을 모두 한 mutex 아래에 둔다: fenced, releaseExchange, inFlight, taken/acked/failed/skipped, 그리고 fence 때의 baseline. 이 필드를 건드리는 코드는 `viewer_owner_fence.hpp` 의 함수뿐이다.

**F1 — 일관된 관측**
- UI 의 fence 는 `fence_raise` 를 **가장 먼저** 부르고, 같은 critical section 안에서 baseline 을 잡는다.
- control thread 는 큐에서 입력을 꺼내는 일과 그것을 세는 일(taken·inFlight·release 여부)을 **한 critical section** 에서 한다.
  - 일반 branch: scheduler 의 `NextAction` 자체를 `gate_begin_action` 안에서 실행한다.
  - fenced branch: `gate_take_release` 를 쓴다.
- 판정 `fence_verdict` 는 queue 비었는지·inFlight·카운터·baseline 을 **같은 lock 안에서** 한 번에 읽는다. 따라서 "꺼냈지만 아직 안 센" 순간이 어떤 판정에도 보이지 않는다.
- Confirmed 조건: baseline 이후 모든 입력 교환이 InputAck 를 받았거나, 로그온 화면 gate 때문에 일부러 보내지 않은 것(skipped)뿐일 때.
- 하나라도 실패하면 즉시 Failed 다. 2 s budget 은 그대로다.

**F2 — fence 와 송신 시작의 중재**
- 루프의 **모든** 제어 쓰기는 `FenceGatedLink` 를 거친다.
  - fence 뒤에는 **집계된 release 교환만** 쓰기를 시작할 수 있다.
  - 쓰기의 "시작"은 gate 가 허락한 순간이다. fence 전에 허락된 쓰기는 fence 전에 시작된 것으로 보고 자르지 않는다. 거부된 쓰기는 아예 시작되지 않는다.
  - 이 시작 판정과 fence 는 같은 lock 아래에서 정해진다. 둘 사이에 무동기 check→send 틈이 없다.
- sender 가 공유 pending 에서 이미 가져간 clipboard 사본은 보냈든 거부됐든 즉시 zero 한다.
- 이미지·파일 bulk datagram 도 gate 가 거부한다.
  - fence 가 worker join(`Stop`)을 기다리지 않게 했다. join 은 세션 teardown(`shutdown_viewer`)이 한다.
- 거부 때문에 실패한 교환은 release-only branch 로 돌아간다. resume 하거나 루프를 끝내지 않는다.
  - 대상: 일반 action, clipboard, 썸네일, IME align.
- Resuming:
  - 이미 진행 중인 resume 은 fence 에서 멈춘다.
  - fence 뒤에는 `begin_control_resume` 이 거부한다.
  - resume probe 도 gate 를 거쳐 쓴다.
- release-only take 의 조건
  - **release 규칙**(공용 core 의 `release_identity` 와 같은 규칙)을 helper 안에서 직접 검사한다. down·move·wheel·text 는 버리고 보내지 않는다.
  - 기존 **로그온 화면 gate** 도 그대로 거친다: r7 H6 `ViewerMaySendAction`, r9 prelogin allowlist, 보내지 않은 down 의 up.
  - 그 gate 에 걸린 것은 skipped 로 센다.

**T1 — 시험 root**
- `REMOTE60_TEST_TMP` 는 다음을 모두 만족해야 한다.
  - `GetFullPathNameW` 로 정규화한 경로의 부모가 정확히 `D:\remote\remote\.claude\test-tmp` 다.
  - 그 anchor 와 root 둘 다 reparse 가 아닌 실제 폴더다.
  - 이번 실행이 그 안에 **새** `run-<pid>-<tick>` 폴더를 만들 수 있어야 한다. 이미 있으면 거부한다.
- 앞의 shelldata 를 다시 쓰지 않는다.
- 실측
  - 저장소 밖 `C:\work\elsewhere-test-tmp`: 거부, 폴더 만들지 않음.
  - `…\test-tmp\..\x2`: 거부, 만들지 않음.
  - anchor 자체: 거부.
  - 정상 경로: 새 run 폴더 1개.

## 시험 hook 의 의미 (Codex 지적 그대로 기록)

- r5 의 `REMOTE60_VIEWER_TEST_FAIL_RELEASE_AFTER_FENCE` 는 `execute_control_action` 을 **실제로 실행한 뒤** 결과만 false 로 바꿨다.
  - 그래서 그것은 write 거부나 Host ACK 부재를 만든 시험이 **아니었다**. 실패 결과 처리의 대조로만 쓸 수 있었다.
- r6 는 이 hook 을 지우고 `REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE` 로 바꿨다.
  - gate 가 fence 뒤 첫 release **쓰기 자체를 거부**한다. 바이트가 하나도 나가지 않으므로 Host 는 ACK 할 수 없다.
  - 이 hook 은 시험 빌드(`GNLinkViewerKaTest`)에만 있다.
  - 이 hook 을 쓰는 실 viewer 시험은 아직 실행하지 않았다(실 Host 대기).

## 회귀 시험 — 보수 전 FAIL → 보수 후 PASS (실 Host·창·네트워크·clipboard API·실 비밀번호 없음)

| 건 | 시험 | 보수 전 (r5, 1c321d1 원문 복사) | 보수 후 (r6 실제 함수) |
|---|---|---|---|
| F1 | Codex 의 스케줄을 실제 두 번째 thread 로 만든다. timer 가 inFlight=false 를 읽은 뒤, sender 가 마지막 release 를 dequeue 하고 taken++ 전에 멈춘다(300 ms). | **Confirmed** — ACK 0, release 는 sender 손에 있음 | timer 의 판정이 handoff 가 끝날 때까지 기다린 뒤(297 ms) **Pending**. 이어서 ACK 없이 실패 → **Failed** |
| F1 | 정상 경로: fence 전 진행 중 교환 + fence 뒤 release | — | Pending → (release 가져감) Pending → ack → **Confirmed** |
| F2 | sender 가 pending clipboard 를 로컬로 옮긴 뒤 UI fence(공유 zero·sync off), 그다음 sender 송신. "link" 는 기록기다. | **writes=1** — fence 뒤 clipboard 송신 | gate 가 거부해 **writes=0** |
| F2 | fence 뒤 일반 branch: pick(WindowSelect)·key down / key up | — | pick·down 은 **Refused**(쓰지 않음), up 은 Admitted 되어 쓰기가 나감. 교환이 끝나면 다시 거부. bulk 거부 |
| F2 | fenced take: 큐에 down + up | — | down 은 버리고 **up 만** 가져감 |
| T1 | root 경계 | r5 의 substring 검사는 `…\test-tmp\..\x` 를 받아들여 **`D:/remote/remote/.claude/x`(빈 폴더)를 만들었다**. 내 실수로 빌드가 실패한 상태에서 옛 exe 가 돌아 우연히 재현됐다. 지우지 않고 삭제 후보에만 올렸다. | 위 4가지 경로 모두 기대대로 |

- units, IPC race, 공용 injection guard 시험: 모두 PASS
- `ka_pc_shell_logic_test` (R1-a): ALL PASS

## 남은 것

- 실 Host e2e 와 그 안의 실 viewer 경로 시험(REFUSE_RELEASE_WRITE hook 사용)은 공용 guard SOURCE OK 와 슬롯 해제를 기다린다.
- 실 IME·clipboard·자격증명 조작은 하지 않는다(사용자 PC). 기록 대역으로만 확인했다.
