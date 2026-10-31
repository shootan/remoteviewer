

---

# r7 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r6.md` N1 · N2 · 완료 문구

## 고정 후보

- `feat/ka-pc` **cd9937a**
  - e1acbb5 위 1 commit, 8파일 +257/-37
  - tracked clean, `git diff --check` rc 0
- 제품 변경
  - `native_video_client_shared_core.cpp/.hpp`: 공유 release 규칙. 셸·뷰어 클라이언트 공용 core 이며, 공용 `standby_*` 는 아니다.
  - `viewer_owner_fence.hpp`
  - `viewer_control_state.hpp`
  - `viewer_control_client.cpp`
  - `viewer_window_proc.cpp`
- 시험 변경: `ka_pc_fence_test.cpp`, `ka_pc_shell_e2e_test.cpp`(로그 문구 1곳)
- 닫힌 항목은 다시 손대지 않았다: F1 lock/handoff, F2 사본, T1, join 제거, resume/role, R1/R2/R4, 두 신호.
- 빌드 sha256 앞 16자리
  - GNLinkViewer.exe ddda8e8c9423cc6b
  - GNLinkClient.exe f14c5942e7f2bf8d (변경 없음)
  - GNLinkViewerKaTest.exe bdff93631077fe96
  - remote60_ka_pc_fence_test.exe 473149823dbc3256
- 출하 Viewer 안의 두 시험 env marker: ASCII·UTF-16 모두 0건
- 실 Host 실행: 0건
- `.claude/x` 는 보존했다(삭제하지 않음).

## 보수

**N1 — X1/X2 up 도 release 다**
- 공유 core 의 release 규칙(`release_identity`)이 mouse up 을 VK 1·2·4 뿐 아니라 **5·6(X1·X2)** 까지 release 로 센다.
- 같은 규칙을 `control_input_release_identity` 로 공개했다. gate 의 `action_is_release` 는 사본 없이 이 하나의 규칙을 호출한다.
- 결과
  - fence 전에 큐에 있던 X up 이 `DropAllButReleases` 에서 살아남는다.
  - fence 가 새로 만든 X up(`enqueue_release_for_pressed_mouse_buttons`)도 drain 이 가져간다.
- X edge 를 Host 에 보내도 되는지는 기존 viewer capability fence(`enqueue_input_event`/`fence_mouse_for_host`)가 그대로 정한다. 이번 변경은 규칙만 바꿨다.

**N2 — fence 가 첫 byte 전에 거부한 쓰기는 '취소'다**
- gated link 가 거부 이유를 gate 에 기록한다.
  - fence 가 메시지의 **첫 byte 전에** 거부
  - 메시지 **중간에** 거부
  - 시험 hook 거부
- 실패한 교환을 다루는 caller 는 모두 공통 결과 타입 `gate_take_failure` 로 그 이유를 한 번 받는다. 대상: 일반 action, input, clipboard, IME, thumbnail.
- 분류
  - **NotStarted(정책상 미송신)** → `InputOutcome::Cancelled`. 남은 release drain 을 계속한다.
  - **Broken(메시지가 잘림)**, **HookRefused(release 거부)**, **Transport(거부 없는 실제 실패)** → Failed. fence 뒤에는 resume 하지 않으므로 loop 가 끝난다.
- 손상된 교환을 fence 취소로 착각해 계속 쓰는 일은 없다. 반대로 모든 실패를 Skipped 로 돌리지도 않았다.
- release 교환은 gate 가 허가하므로 취소될 수 없다. release 의 실패는 언제나 Failed 다.

**완료 문구**
- verdict 의 tally 가 다음을 따로 센다: ack, 로그온 gate skip, fence 취소, failed, taken.
- 로그 예: `owner fence: releases confirmed after N ms -- since the fence: A acknowledged by the host (InputAck), S not sent by the logon-screen gate, C non-release(s) cancelled by the fence before a byte, F failed, of T taken`
- 보내지 않은 것을 InputAck 로 부르지 않는다.

## 회귀 시험 — 보수 전 FAIL → 보수 후 PASS (실 Host·창·네트워크·실제 입력 없음)

| 건 | 시험 | 보수 전 | 보수 후 |
|---|---|---|---|
| N1 | 공유 규칙에 X1·X2 up / left up / X down 을 넣는다. fence 전에 queued 된 X1 up 을 cancel 한 뒤 drain 한다. 제품 함수 `enqueue_release_for_pressed_mouse_buttons` 로 X1·X2·left 를 held 로 두고 fence 한다. | 공유 규칙의 r7 줄을 뺀 **mutant: FAIL 3**. X up 이 버려지고 drain 이 X 를 하나도 가져가지 않는다(x=0). 그런데도 verdict 는 **Confirmed** 다. Codex 가 지적한 결함 그대로다. 로그 `np_impl_logs/ka_r7_fence_neg.log` | X 를 받는 Host: X up 2 + left 1 을 가져간 뒤 Confirmed. **X 미지원 Host: X edge 0**, left 1 |
| N2 | down 이 fence 전에 Admitted → fence → held Ctrl up 이 queued → down 의 Write 가 0 byte 로 거부된다(실제 gate/link). | r6 의 결과 매핑(!ok → Failed): 즉시 **Failed**, Ctrl up 은 아직 큐에 있음, wire 0 | **Pending** → Ctrl up 송신·ack → **Confirmed** (ack 1, cancelled 1, failed 0, wire 1) |
| N2 | 메시지 중간 거부 / hook 거부 / ack 없는 release | — | 각각 Broken / HookRefused / Transport 로 분류되고 모두 **Failed**. 이전에 쓰기를 시작한 메시지가 fence 뒤 중간에서 거부되면 Broken 으로 기록된다. |

- mutant 는 소스를 임시로 바꿨다가 복원했다. 복원 뒤 sha256 703a407d… 로 일치를 확인했고, 재빌드 후 ALL PASS 다.
- units / IPC race / 공용 injection guard / shell logic: 모두 PASS.

## 남은 것

- 실 Host e2e 와 그 안의 실 viewer 경로 시험(hook 을 이용한 release 쓰기 거부 → "NOT confirmed")은 공용 guard SOURCE OK 와 슬롯 해제를 기다린다.
- 실 X 버튼 주입은 하지 않는다(사용자 PC). 기록 queue·gate 로만 확인했다.
