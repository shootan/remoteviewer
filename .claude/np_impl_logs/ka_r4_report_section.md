

---

# r4 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r3.md` R1~R4 + 결정 ①, 그리고 시험 Host 입력 주입 사건

## 상태

- **제품 보수와 시험 코드는 완료. 실 Host e2e 는 미실행.**
- 검증용 지시(test-injection-finding, 정책 main 14cdde1)에 따라, 시험 Host 의 공용 guard 가 들어오기 전까지 실 Host e2e 를 돌리지 않는다.
  - 공용 guard: 입력 none + 전원·화면 효과 OFF/record-only, 작업용1 ka-r3.
  - 그 commit 을 병합한 뒤 그대로 쓴다. 자체 변형은 만들지 않는다.
- 후보: `feat/ka-pc` **c8f78c3**
  - d81148e: 제품 + 시험
  - c8f78c3: 시험 Host 를 `--input-injection-mode none` 으로
- 5a1fb45..d81148e 의 제품 변경: 11파일 +349/-68. 공용 `standby_*` 변경은 0.

## 시험 Host 입력 주입 사건 (보고·조치 완료)

- **원인**: GNLinkStream 의 `enableInputInjection` 기본값은 **true** 다(`host_args.hpp:47`). 나는 `--enable-input-injection` 플래그가 있다는 이유로 기본이 OFF 라고 잘못 판단했다.
- **영향**:
  - 오늘 ka-pc e2e(r1~r4-1, `test-tmp/ka-*`)의 시험 Host 는 `injectionEnabled=1 mode=background_message` 였다.
  - 뷰어 세션이 시작될 때마다 modifier key-up 11개가 이 PC 의 입력 경로로 갔다. 합계 **2490건**이며 전부 eventKind=6 이다.
  - key-down·mouse·text 는 0건이다(host 로그 집계). 화면에 실제로 반영됐는지는 확인할 수 없다.
- **조치**:
  1. 검증용에 즉시 알렸다.
  2. 모든 시험 Host 를 **`--input-injection-mode none`** 으로 띄우고(Codex 표준), 이번 launch 가 "input injection disabled" 를 말하기 전에는 **뷰어를 연결하지 않는다**(fail-closed).
  3. 기억 메모를 남겼다.
- **중간에 시도했다 버린 방식(`--config`)**: config 파일은 Host 의 env override 를 지운다(`json_profile.hpp clear_runtime_env_overrides`). 그래서 h264 스위치가 사라져 Host 가 "unsupported codec: h264" 로 바로 꺼졌다. r4-2 에서 확인했고, fail-closed 로 뷰어 연결은 0이었다.
- **증거 정정**: Codex 정정(`codex_ka_pc_src_r3.md` 머리)대로, r1~r3 및 r4-1 의 e2e 수치는 기능 관측으로만 보존하고 완료 관문에는 쓰지 않는다.

## r4 보수 (d81148e)

**R1 — 예약이 자리를 지킨다**
- 예약은 성공하거나 반납될 때까지 **PC 하나로 센다**: `occupied = logical_pcs(except) + (예약 있음 ? 1 : 0)`.
- D 를 예약한 뒤 A 를 해제해도 그 빈자리는 E 에게 가지 않는다.
- commit 시점에도 놓아 보낼 standby 가 없어 4번째가 되면 확정하지 않는다. owner-end 신호로 닫고 사용자에게 알린다.

**R2 — pump 종료와 exit 판정을 같은 lock 으로**
- `note_pump_end` / `note_exit_decided` 를 같은 `gConnectedMu` 아래에서 처리한다. 어느 쪽이 먼저 와도 표식이 남지 않는다.
- exit 판정이 먼저면 늦게 온 connected 줄은 세지 않는다.

**R3 — fence 가 release 의 Host 확인을 기다린다**
- fence 순서: held release 를 enqueue → 새 입력을 즉시 gate → 닫기.
- 닫는 조건은 **queue 비었음 ∧ 진행 중인 입력 교환 없음 ∧ control 채널에 미확인 송신 없음** 이다.
  - 입력 교환은 request/response 다. Host 가 각 이벤트에 ack 를 보내며, `inputInFlight` 는 control thread 가 dequeue **전에** 올린다.
  - 위 조건이 2 s 안에 맞지 않으면 그대로 닫는다(죽은 링크에서도 유한하게 끝남).
- 시험 빌드 전용: `REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS` 가 fence 뒤 송신을 N ms 붙잡는다.

**R4 — ordinary 상태도 hostId 로**
- ordinary exit·재연결·실패 상태가 모두 hostId 를 갖는다.
- ordinary 가 연결되면 그 카드의 '연결하는 중…' 표시를 끝낸다("<PC> 연결됨"). r4-1 에서 발견했다: 이게 없으면 연결된 카드가 잠긴 채로 남아 결정 ① 의 재탭이 페이지에서 불가능했다.

**결정 ① — 신호 두 개**
- `--cancel-event` 는 **connect 만** 부른다. 이미 성립한 세션에는 "no effect" 다.
- `--owner-end-event` 는 owner 의 종료(로그아웃·계정 전환·셸 종료)이며 fence 를 건다.
- 교체됐지만 이미 연결돼 있던 뷰어는 계속 이 owner 의 것이다(`gReplacedViewers`). 그 뷰어의 종료나 owner 종료 때 회수한다.
- 셸이 connected 로 본 PC 를 재탭하면 창을 앞으로 가져온다(r3 그대로).

## 시험

| 시험 | 결과 |
|---|---|
| units @d81148e | ALL PASS |
| IPC race @d81148e | ALL PASS |
| managed / shell e2e (주입 OFF) | **미실행** — 공용 guard commit 병합 뒤 실행 |
| r4-1 (주입 ON, 무효) | managed 62/0, shell 133/4 — 기능 관측으로만 기록 |
| r4-2 (`--config`) | Host 시작 실패. fail-closed 로 연결 0 |

r4-1 의 기능 관측(근거 아님):
- R3: 송신을 800 ms 붙잡은 상태에서 fence → "releases acknowledged by the host after **844 ms**" → 닫힘. Host 로그에 key-up 11건.
- 결정 ①: connect cancel 신호에 "no effect (session established)", 뷰어 생존.
- R1·R4 혼합 반례와 R2 양순서는 PASS.
- 실패 4건:
  - 최근 순서 판정 시점 오류(시험)
  - R3 Host 확인을 세션 종료 기록 전에 읽음(시험)
  - 결정 ① 재탭 2건: 카드가 잠긴 제품 결함. d81148e 에서 보수했다.

r4 e2e 에 들어간 반례(실행 대기):
- R1: C 예약 중 A 해제 → E 거부
- R4 혼합: ordinary E 종료 중 managed D 를 여는 카드 유지
- R3: Host 로그에서 key-up 이 세션 종료보다 먼저 기록되고 `injectionEnabled=1` 이 0건
- 결정 ①: 재탭 → 이미 열림, connect cancel → no effect
- R2: 양순서

## 남은 것

- 공용 guard commit(작업용1 ka-r3) 병합 → 주입 OFF·전원 효과 OFF e2e 실행 → 이 절에 결과 추가.
- 이전 미검증(IME·clipboard 실조작, 일부 예약 경합, `client_recovery_ui_test`, 실 NAT·비용·takeover·동시 잠금·401·셸 종료/계정 전환)은 그대로 남는다.
