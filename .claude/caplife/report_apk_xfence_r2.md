# keepalive-r2 e — APK X 경계 r2 (Codex apk_xfence_src_r1 X1·X2·X3)

task=keepalive-r2(e, APK X 경계) / rev=r2 결과 / 3ef6ee6 위

## 고정
- commit **50340ceba9a896bdfe789df181ac2b64522eb818** (feat-apk-0233, 부모 3ef6ee6). tracked clean, `git diff --check` 정상.
- 4파일 +365/−4:
  - `client_mouse_xbutton_fence.hpp`: 새 `ClientMouseXPressLedger`
  - `native_video_client_session.cpp`: :624–625 (입구), :981–988 (송신 직전), :1015 (`OnSent`), :1791 (`ResetUnlocked`)
  - `native_video_client_session.hpp`: :17 (include), :480–482 (멤버)
  - 시험 1개
- 공유 core·`client_park_state.hpp`·JNI 는 0줄. 실 Host·AVD 실행 0.

## X1 — 같은 press 단위의 처리 (입구·송신 양쪽, 연결 세대)
`ClientMouseXPressLedger` 는 X 버튼마다 press 의 처리 결과를 기억한다. 입구 기록과 송신 기록은 따로 둔다. 입구에서 거부된 press 는 큐에 들어가지 않고, 송신 직전에 폐기된 press 는 송신단만 알기 때문이다.

- 입구(`AdmitQueued`, `QueueInputEvent`):
  - X down 은 새 press 로 보고 지금의 capability 로 판정한다. 거부되면 `entryRefused` 로 기록한다.
  - 거부된 press 의 up 은 capability 가 생겼어도 거부하고, 그 press 를 끝낸다.
  - 거부된 press 가 진행 중이면 모든 event 의 해당 X bit 를 mask 한다.
- 송신 직전(`AdmitSendAction`, 제어 루프, parkMu_ 아래):
  - X down 이 X 없는 Host 로 가게 되면 폐기하고 `Dropped` 로 기록한다. 그 press 의 up 은 폐기하고 `Idle` 로 되돌린다. 진행 중에는 X bit 를 mask 한다.
  - 송신된 down 은 `OnSentAction` 에서 `Sent`(release 빚짐)로 기록하고, 송신된 up 은 `Idle` 로 기록한다.
- **이미 송신한 down 의 owed release 는 구분**한다. `Sent` 인 up 은 Host 가 X 를 받는 동안에만 나간다. 최근 거부 이력만 보고 모든 up 을 버리지 않는다. 다른 버튼(X2·left)에는 영향이 없다.
- 거부된 down 을 자동으로 재생하지 않는다.
- 세대: `ResetUnlocked` 에서 `heldInput_.Reset()` 옆에 `mouseXLedger_.Reset()` 을 둔다(parkMu_ 아래).

## X2 — capability 감소
- 미지원 Host 로 X release 를 예외로 보내는 경로는 없다. 이 경우 `Sent` 기록은 남고 `ClientHeldInput` 도 그 버튼을 계속 보유한다.
- 그래서 park 는 reRelease 1회 뒤 `Failed` 가 되어 기존 bounded 계약대로 끝난다("park not confirmed", 세션 종료).
- 새 연결은 Reset 으로 기록 0 에서 시작한다.
- r1 보고의 "held 는 반드시 B 를 통과" 문장은 **capability 가 유지될 때만** 맞다. 이렇게 정정한다.

## 시험 (fake wire: 제품 controller + loopback fake Host, Pong 보류·flag 전환)
`remote60_client_session_xbutton_fence_test` **43/43, exit 0, RESULT ALL PASS, 6회 반복**. 기존 23개는 그대로이고 20개를 새로 추가했다.

- **unknown→X**:
  - 첫 Pong 을 보류한 상태에서 X1 down(macro 처럼 bits 8) → 거부. 이후 X Pong.
  - move(X1|L) 는 `buttons=0x1` 로, X1 up 은 거부(false). wire 는 move 1건.
  - 다음 press 는 down·move(X1)·up 4건이 모두 나간다.
- **X→noX(송신단)**:
  - Ping 을 보류한 상태에서 X2 down, move(X2|L), left click 을 큐에 넣는다(입구 통과). 이후 no-X Pong.
  - down 은 송신 직전에 폐기된다(heldBack +1). move 는 0x1, left down/up 은 나간다. wire X 는 0.
  - 다시 X Pong. 같은 press 의 move 는 `buttons 0`, up 은 송신 직전에 폐기된다.
  - 다음 X2 press 의 down/up 은 나간다.
- **X→noX(owed)**:
  - X1 down 이 X Host 로 송신된 뒤 no-X Pong.
  - 사용자 up 은 거부되고, park release 는 송신 직전에 폐기된다(parkReleased=2, wire X 0).
  - park 는 Parked 가 아니라 phase=5(Failed), lastError="park not confirmed", 세션 종료.
- **reconnect**: 연결 1(no-X)에서 X1 down 이 거부된다. 연결 2(X)에서는 단독 X1 up 이 새로 판정되어 나가고, 새 press 가 온전히 나간다.
- **ledger 단독**: 입구 전환, 송신 전환, Sent release 의 no-X 거부(빚은 유지)와 X 복귀 후 송신, Reset, 다른 버튼 무영향.

부정 대조 mutant(각각 다른 파일은 good 사본에서 복원한 상태로 단독 적용, 커밋 안 함). 복원 후 src 해시 일치, 재빌드 43/43.
- **r1** 입구가 press 를 잊음 → 3 FAIL. unknown→X 에서 up 이 통과해 wire 5건이 됐다.
- **r2** 송신단이 폐기된 down 을 잊음 → 2 FAIL. X 복귀 뒤 up 이 새어 wire 4건이 됐다.
- **r3** 연결 Reset 없음 → 1 FAIL. 연결 2 의 단독 up 이 거부됐다.

## X3 — 공유 controller 소비 target (feat-apk-0233)
- 이 controller 를 컴파일하는 21개 target 을 함께 재빌드했고 오류는 0이다.
- 실 Host·clipboard 를 쓰지 않는 소비 시험 실행 결과:
  - role_park 13/13, prelogin_select 60/60, host_wire_cap_idr 14/14, client_udp_recv_loop 10/10: 모두 exit 0
  - udp_fec_single_chunk 501, single_chunk_loss 405: PASS, exit 0
  - shared_core_test, udp_fec_interleave: exit 0 (PASS 줄 형식이 없음)
  - `ka_relay_client_test`: exit 1 이지만 **fixture 디렉터리/Host 인자가 없어 첫 단계("signs in … missing host")에서 멈췄다**. 입력 단계에 도달하지 않았으므로 이번 변경의 판정이 아니다.
- 실 Host 를 띄우는 소비 시험은 실행하지 않았다: udp_control_e2e, mobile_abr_session_e2e, pinned_seam_boundary, km_multiclip, host_* e2e.
- 소비 시험 소스에서 X 입력(kind 2/3 + keyCode 5/6, X bits)을 쓰는 곳은 없다. 사용하는 것은 키, left, move 뿐이다. 그래서 기대값 수정이 필요 없었고, 제품 fence 우회도 없다. `viewer_prelogin_login_e2e_test` 는 금지 상태라 손대지 않았다.
- os_input_record_e2e 는 이 브랜치에 없다(ka-r3 쪽). 통합 때 재빌드한다.

## 미검증
- NDK/JNI/4 ABI 빌드, 실 Host·AVD·실 X 버튼은 실행하지 않았다.
- ka-pc cd9937a 병합 상태(큐가 X up 을 보존)에서의 재실행은 R2 통합 때 한다.
