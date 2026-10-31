

---

# r2b · r3 (2026-10-28, 작업용2) — 534242b merge, N6·N2 반례, Codex `codex_ka_pc_src_r2.md` R1~R4 + 결정 2건

## 고정 후보

- `feat/ka-pc` **5a1fb45** (tracked clean, `git diff --check` rc 0).
- a864105 이후 커밋:
  - `c4e0df0` merge ka-r3 **534242b**. 공용 punch 보수(6c3cb5a 큐 수명)와 시험이 들어온다. git CLI 로 merge 했고 충돌 0, 공용 파일은 merge 만 했다.
  - `705ed23` r3 본체 + r2b·r3 시험
  - `d9a75f1`, `5a1fb45` 시험 보정
- 제품 변경 (a864105..705ed23):
  - `client_shell_main.cpp` (+264), 4파일 합계 +303/-89
  - `native_video_client_main.cpp`, `viewer_window_proc.cpp`, `ui/shell.html`
  - 705ed23 이후 제품 변경 0 (시험 파일만 바뀜)
- 빌드 sha256 앞 16자리:
  - GNLinkViewer.exe 62118c9d4b45bca9
  - GNLinkClient.exe c0617443f6165639
  - GNLinkViewerKaTest.exe 123b0fa88611e5d3
  - GNLinkStream.exe 623bd5fca4efa5f7
  - ui/shell.html 12085292b1851ec4
  - remote60_ka_pc_shell_e2e_test.exe cfaa2dc2a90fe3d5 (r3-4/5)
  - remote60_ka_pc_managed_e2e_test.exe 1e4ad8ed1932bbf1 (r3-1/2)
- 출하 exe 의 시험 문자열은 0건이다.

## r2b (검증용 지시: 534242b merge + 반례)

- merge 뒤 공용 `host_agent_punch_test` 9/0.
- 즉시 재유지 (crash 직후 / EndAll 직후 / 축출 직후 A) 는 모두 계수 대상이고 PASS 했다.
- c4e0df0 + r2b 시험 (시험 exe 3cab51eafa3d2ff0): managed 61/0 ×2, shell **98/0 ×2**.
- 반례 4종 (모두 PASS):
  - **N6 옛 op**: B 활성 중 다른 op 의 SessionEnded 2건과 PickerReady 1건을 넣었다. 3건 모두 drop 되고 로그가 남았으며, B 는 '연결됨' 그대로였고 상태줄도 그대로였다.
  - **N6 늦은 이벤트**: B 를 여는 동안 해제된 C 의 옛 incarnation 으로 SessionEnded, State ready, exit 를 넣었다. B 는 정상으로 '연결됨' 이 됐고, C 카드와 상태줄은 바뀌지 않았다.
  - **N6 송신 실패**: A 의 channel 을 닫고 바로 connect 했다. "could not be sent" 로그가 남고, activeOp 0, 상태줄 "여는 요청을 보내지 못했습니다", 일반 뷰어 0.
  - **N2 취소된 4번째**: 연결 중인 C 를 다시 connect 하자 첫 시도가 "connect cancelled" 로 끝났다. 축출 0, C 유지 없음.
- 주입 방식:
  - 늦은/옛 메시지는 셸 forwarder 가 UI 로 넘기는 형태 그대로 (`ManagedEvent` → `on_managed_event`) 넣었다. 처리는 셸 자신의 핸들러가 한다.
  - "다시 connect" 와 "송신 실패" 는 화면을 거치지 않고 bridge 의 native 입구 (`handle_page_message`) 를 직접 불렀다. 페이지는 연결 중인 카드를 누를 수 없게 막기 때문이다.

## r3 보수

**R1 — 예약을 하나로**

- 3대 옆에서 시작하는 모든 경우가 **같은 예약 하나** (`gStagingHost`) 를 시작 전에 잡는다: 손으로 하는 ordinary connect, 자동 재연결, 대기 준비.
- 예약이 이미 있으면 두 번째 후보는 거부한다 ("다른 PC 를 여는 중").
- 예약 중에 그 후보를 위해 비울 마지막 standby 를 여는 것도 거부한다.
- 예약 반납 시점: 실패, 취소, 연결 없이 종료, 사용자 해제, owner 변경.
- 성공 (ordinary connect, managed ready) 할 때만 commit 하고, 그 시점에 다시 센다 (`settle_pc_count`).
- `logical_pcs` 는 예약 후보를 ordinary·managed 양쪽에서 모두 뺀다.

**R2 — 연결 통지와 종료 순서**

- 연결 통지에 owner epoch 를 묶었다. `on_ordinary_connected(op, host, epoch)` 는 다른 owner 의 통지를 버린다.
- 로그아웃하면 옛 owner 의 ordinary 슬롯을 registry 에서 **즉시 분리**한다. 그 뒤 종료 (fence, fallback) 는 worker 가 자기 handle 로 맡는다.
- exit 판정은 stdout pump 의 끝을 worker 에서 **최대 2 s 기다린다**. 그보다 늦은 connected 줄은 세지 않는다 (`gEndedBeforePump`, pump 가 끝나면 비운다).

**R3 — fence 를 gate 로**

- `kMsgOwnerFence` 순서:
  1. 입력이 켜진 상태에서 눌린 입력을 **먼저 해제**한다. control-resume 과 같은 집합이다: modifier 전부, 이 창이 보낸 키, 마우스 버튼, scan 키, capture, picker press, macro 정지.
  2. `inputEnabled=false` 와 `ownerFenced` 를 켠다.
  3. clipboard listener 를 해제하고 WM_CLOSE 로 닫는다.
- fenced 동안 drop 하는 것: IME 메시지 (`WM_IME_*`), deferred·restore·unsendable pick.
- payload 를 가진 post 3종 (`kMsgApplyClipboard`·`kMsgApplyWindowList`·`kMsgDecideRemoteFiles`) 은 **적용하지 않고 해제만** 한다.
- IME 결과 경로 (`enqueue_input_text_units`) 는 `inputEnabled` 로 막힌다 (소스로 확인).
- **cancel 이 미리 signal 된** ordinary 는 창을 만들지 않는다.
- cancel watcher 는 창보다 먼저 시작하고, 창·decoder 를 만든 뒤에 다시 확인해서 connect 전에 끝낸다.

**R4 — 상태줄과 카드 표시 분리**

- PC 에 관한 상태는 `hostId` 를 붙인다 (`post_host_status`).
- 페이지는 다른 PC 의 idle/error 로 지금 연결 중인 카드의 표시를 풀지 않는다.
- managed 이벤트, 해제, 거부, 송신 실패의 상태가 모두 hostId 를 갖는다.

**결정 ①** — 연결된 ordinary PC 를 단순히 다시 탭하면 **그 창을 앞으로** 가져오고 (`SetForegroundWindow`) "이미 열려 있습니다" 를 표시한다. 교체는 아직 연결 중인 시도에만 한다.
- 남은 경계: 연결은 됐지만 "connected" 줄이 아직 셸에 도착하지 않은 짧은 순간의 재탭은 연결 중으로 보고 교체한다. R2 의 join 은 이 창을 좁힐 뿐 없애지는 않는다.

**결정 ②** — 자동 재연결 (43/44/46) 도 같은 예약·한도를 따른다. 거부되면 재시도하지 않는다 (timer 해제, 시도 횟수 0). 상태줄은 "PC 3대가 열려 있어 다시 연결하지 않았습니다. 다른 창을 닫은 뒤 이 PC 를 다시 눌러 주세요" 이고 로그는 `reconnect=refused reason=three-pcs`.

## r3 시험

| 시험 | 결과 |
|---|---|
| shell e2e @5a1fb45 | **123/0 ×2** (r3-4/5). r3-1 (@705ed23) 123/0, r3-3 (@d9a75f1) 123/0 |
| managed e2e @705ed23 | **62/0 ×2** (r3-1/2) |
| units | ALL PASS |
| R4 부정 대조 | 페이지만 a864105 판 (8172a343…) 으로 바꿨다: **R4 FAIL** — A 의 SessionEnded 뒤 C 카드가 '연결 가능' 으로 풀렸다. 대조 뒤 12085292… 로 복원한 것을 해시로 확인했다 |

- 같은 대조에서 '…exactly one' 도 FAIL 했는데, 이것은 시험 결함이었다. 셸은 07:40:06 에 정확히 한 번 축출했다. log_has 가 R1 단계의 같은 문구를 먼저 잡았다. 5a1fb45 에서 동작 이후 로그만 읽도록 고쳤다.
- r3-2 shell 은 시험 결함으로 9/27 이었다. 페이지의 restore 메시지가 채워 둔 아이디 칸을 비워 로그인이 전송되지 않았다 (client.log 에 로그인 시도 0). 내 시험 프로세스를 경로로 확인해 중단했고 슬롯은 반납됐다. d9a75f1 에서 로그인을 재시도하도록 고쳤다.

**R1 실측 (호스트 5대):** A 대기, B·C 열림 상태에서 페이지에서 D, A, E 를 한 번에 눌렀다.
- D 만 예약됐다 ("reserved beside three PCs").
- A 는 "the last standby" 로 거부됐다.
- E 는 "another PC is being opened" 로 거부됐다.
- D 가 연결된 뒤 "committed" → "releasing host=A" 가 정확히 1건 나왔다.
- 그 뒤 E 는 "three PCs are in use" 로 거부됐다.
- 호스트가 없는 C 는 연결 없이 끝났고 "reservation … released (ended without connecting)" 로 예약을 돌려줬다.

**R4 실측:** A 를 연 상태에서 호스트가 없는 C 를 탭했다 (연결 중). A 의 창을 닫자 A 의 SessionEnded 가 셸에 도착했지만, C 카드는 '연결하는 중…' 을 유지했다.

**R2 실측:**
- 로그아웃 → 다시 로그인 (새 owner) 한 뒤, 옛 epoch 로 연결 통지 2건을 넣었다. recent 파일 바이트, "committed", "making room" 이 모두 그대로였다.
- (exit 와 pump 의 역순은 정상 실행의 '연결 직후 X' 로만 확인했고, 역순을 강제로 만든 시험은 없다.)

**R3 실측:**
- B 뷰어 로그: "owner fence: held input released (modifierUps=11), input and clipboard cut, closing the session" → exit 0, handle 종료 없음.
- cancel 이 미리 signal 된 GNLinkViewerKaTest: 창 0, connect 0, "called off before the session began", exit 0.

## 남은 것 · 미검증

- **R3 의 IME / deferred pick / 클립보드 payload drop 은 소스로만 확인했다.** 실제 IME 조합이나 원격 클립보드 도착 중에 fence 를 거는 시험은 없다.
- **R1 의 경합:** staged managed 가 대기 중일 때의 다른 탭과 복구 timer 경합, D/E 완료 역순은 시험하지 않았다. 실측한 것은 한 턴에 세 번 누른 경우와 순차 성공·실패·취소다.
- **R2 의 역순:** exit 가 pump 보다 먼저 처리되는 순서를 강제한 시험은 없다.
- `client_recovery_ui_test` (기존 셸 회귀) 는 여전히 실행하지 않았다 (화면 위 창, 저장소 밖 쓰기). Codex 가 이 판단을 수용했다. 영향이 있는 단정은 내부 root 의 화면 밖 seam 으로 옮기는 것이 후속이다.
- 실 NAT, 비용, takeover 반례, 동시 잠금, 401, 셸 종료·계정 전환 실측.

## 삭제 후보 (추가만)

- `D:/remote/remote/.claude/test-tmp/ka-shell-r2b-*`·`ka-managed-r2b-*`·`ka-shell-r3*`·`ka-managed-r3-*`·`ka-units-r3`·`ka-punch-r2b`
- `.claude/np_impl_logs/shell_html_*.html` (대조용 사본)
