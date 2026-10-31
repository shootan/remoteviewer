# rel-0163 r2 E1 — PS 실제 renderer 에서 재현된 제품 결함 (작업용1, 2026-10-26)

task t-bpwzezfg r2 E1. 후보 소스 90192f4(제품 무변경), 시험만 rel worktree 에서 수정(미커밋).

## 무엇을 돌렸나
`viewer_prelogin_login_e2e_test --still-screen --render --shots <dir>` (신규 E1 모드, 시험 파일만 변경):
- 로그인 전 이름 대역 워커(기존 --still-screen 경로) + **제품 WndProc 창**(모든 모니터 밖 2120,40 — 사용자 화면 무접촉), 제품 picker(startInPicker), `queue_window_list_request`(viewer_startup.cpp:879 와 같은 호출).
- picker 10 s → 제품 레이아웃 함수(compute_client_layout/compute_card_grid/card_rect_for_slot)로 구한 desktop 카드에 WM_LBUTTONDOWN/UP post → 제품 present(d3dPresentSuccessCount) 대기, PrintWindow(PW_RENDERFULLCONTENT) 캡처.

## 결과 (3회 동일, e1_ps1~3.txt)
- picker 표시·캡처 OK(pickerBg 87%), 선택 로그 `[picker] select source=mouse … id=0` 정상.
- **그 뒤 4 s 동안 present 0, 화면은 계속 picker**. host 는 keyframe 을 계속 보냄(assembly key=1 seq 1,5,10,14…), viewer 는 디코드 0·`keyframe recovery retry … waitForKey=1` 반복. 워커 생존, present 실패 0.
- host 로그에 window list / window select 수신 **0건**.

## 원인 (소스)
1. 제품 셸은 뷰어를 항상 `--initial-view targets`(picker 먼저)로 띄운다 — `client_shell_main.cpp:1450`.
2. picker 의 desktop 선택은 `begin_pc_target_selection` → `ctx.sel.Begin()`(awaitingAck=true) + WindowSelect 요청 — `viewer_picker.cpp:206-218`.
3. 호스트가 로그인 전(kCaptureFlagPreloginV1)으로 알려진 뒤에는 뷰어가 allowlist 밖 요청을 **보내지 않는다** — `viewer_control_client.cpp:1072-1077`. WindowListRequest·WindowSelect 는 allowlist 에 없다(`prelogin_control_allowlist.hpp:26-…`: MonitorList/MonitorSelect·StreamState 만).
4. 그래서 ack 가 오지 않고, `SelectionGateState::AdmitGeneration` 은 `pending && awaitingAck` 동안 모든 프레임을 **DropAwaitingAck** — `viewer_selection_gate.cpp`. reveal 이 없어 picker 가 닫히지 않는다.

→ **PC 뷰어가 로그인 전 화면 호스트에 붙으면 picker 에서 바탕화면을 골라도 영상이 영원히 나오지 않는다**(호스트가 프레임을 보내도). 10-22 실기("로그인 전 화면 PC 뷰어 영상 0")의 뷰어 쪽 원인일 가능성이 있다(호스트 쪽 PS-A/B 와 별개, 단정은 아님 — 그날 viewer.log 로 확인 필요).

## 지금 상태
- PS E1 은 이 결함 때문에 **제품 무변경으로는 PASS 할 수 없다.** 0.2.163 후보(90192f4)를 그대로 게시하면 PC 뷰어의 로그인 전 화면 표시가 여전히 안 될 수 있다.
- 결정 필요(Codex): 뷰어 수정 방향 — 예) 로그인 전 호스트에선 desktop 선택을 ack 없는 legacy 선택으로(selection gate 를 Begin 하지 않음 / StreamState 만), 또는 MonitorSelect 트랜잭션 사용. 제품 변경이므로 새 후보(재빌드·재 dry-run)가 된다.
- UA E1(로그인 뒤 일반 호스트, 실제 renderer)은 이 결함과 무관하므로 계속 진행한다.
