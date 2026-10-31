# KA PC r6 — SOURCE 재검토

**NEEDS_CHANGES — 이전 F1 관측 경합·F2의 이미 가져간 clipboard 사본·T1 root는 닫는다. X-button release 누락과 정상적인 fence 거부의 Failed 오분류를 같은 PC task에서 보수한다.** 실 Host/UI·독립 통합 EVIDENCE·출하 승인은 아니다.

## 고정 범위

- 후보 **e1acbb57a571e1900dad7780dd2d1b960082591e**, 비교 **1c321d1..e1acbb5**의 8파일. tracked clean·diff --check 정상. 보고 SHA-256 **2ecc828225ddd756fcf956859c07c54e74f0e80c2d58601e3f9490ed40bf4514** 일치.
- 제품 6파일과 시험 2파일을 읽었다. 고정 source snapshot은 `.claude/codex_ka_pc_review/r6_src/`에 보존했다. 공용 안전 guard의 별도 미결과 실제 Host 시험 보류를 이 변경으로 해제하지 않는다.
- units/IPC/guard/작업용 fence·logic 시험 PASS는 작업용 보고다. Codex는 아래 제한 gate probe와 제공 출하 artifact의 읽기 검사를 수행했으며 실제 Host/Viewer/셸/network/input/power/clipboard를 실행하지 않았다.

## 닫는 지적

- **F1 원 반례:** fence_raise의 baseline, gate_begin_action의 scheduler/dequeue/집계, gate_take_release, fence_verdict가 같은 mutex를 사용한다. “queue에서 꺼냈으나 taken 전” 상태를 verdict가 중간에 관측하던 결함을 닫는다. 실제 sender thread를 dequeue 직후 100ms 정지시킨 고정 helper probe에서도 **Pending 후 Failed**를 확인했다. 모든 thread 순서의 전체 앱 증명으로 확대하지 않는다.
- **F2 원 clipboard 사본:** 실제 ControlClient의 쓰기가 FenceGatedLink를 거치고, fence가 확정된 뒤 새로 요청한 비release Write가 거부된다. 이미 로컬로 가져간 문자열도 결과와 무관하게 zero한다. 고정 gate + 기록 link에서 **쓰기를 요청하기 전 fence→writes 0**을 확인했다.
- **F2 수명/role:** fence를 먼저 올리고 UI의 image/file Stop join을 teardown으로 옮긴 변경, resume 취소·재개 거부·probe의 gated link, release take의 기존 H6/r9/보류된 down 검증 연결을 수용한다. 게이트 허가를 쓰기의 논리적 시작으로 삼고 허가 전/후를 같은 mutex에서 중재하는 정의도 수용한다. **허가가 fence 전에 난 기존 작업의 완료까지 소급 차단했다는 뜻은 아니다.** 후속 기록도 그 시작점에 맞춰 분류한다.
- **T1 root:** substring 검사 대신 정확한 anchor의 바로 아래 root, 실 directory/non-reparse 확인, 신규 run 생성 성공을 요구한다. 새 shelldata만 쓰며 삭제하지 않는다. 원 T1을 SOURCE에서 닫는다. 보고한 기존 r5의 `D:/remote/remote/.claude/x` 빈 폴더 사고는 보존/삭제 후보 기록 그대로 유지한다. Codex가 생성·삭제하거나 복구한 것이 아니다. 이번에 logic exe를 직접 실행하지 않았다.
- 이전 R1-a PC union·R2 pump/exit·R4 host별 상태·두 cancel 신호의 수용은 유지한다. 무관한 재설계를 요구하지 않는다.

## N1 (P1) — X1/X2 mouse up이 release에서 빠져 Host에 도달하지 않음

viewer_input_forward.cpp::enqueue_release_for_pressed_mouse_buttons는 **VK_XBUTTON1=5, VK_XBUTTON2=6**의 kind=3 up을 생성한다. 지원 Host에서 해당 버튼을 held로 기록하는 기존 경로다. 그러나 새 **action_is_release**는 mouse up을 keyCode **1·2·4**만 인정한다. gate_take_release는 X up을 버리고, gate_begin_action도 fence 뒤 X up을 Refused로 취급한다.

원 r5는 owner_fence_cancel_pending의 queue 정리 **뒤에 새로 enqueue된** 이 X up들을 take_release_action에서 보낼 수 있었다. r6의 새 필터는 그 fresh release도 버리는 회귀다. 결과적으로 Host가 X1/X2 down을 받은 상태에서 owner가 끝나면 해당 up은 안 갔는데 queue가 비어 **Confirmed**가 될 수 있다.

고정 gate 함수/상태와 protocol-field형 메모리 queue fixture:

```
X1_X2_ups_taken=0 queue_empty=1 verdict_confirmed=1 host_acks=0
```

**공유 release_identity와 gate의 분류를 함께 맞춘다.** shared core의 기존 predicate도 1/2/4뿐이므로 gate 한 곳만 고치면 fence 이전에 queued돼 있던 X up이 DropAllButReleases에서 여전히 사라진다. pending X up과 fence 때 새로 만든 X up을 모두 보존해야 한다. 실제 지원 Host에만 X edge를 보내는 기존 capability fence와 prelogin role 검증은 유지한다.

부정 대조는 기록 transport/queue로 **X1·X2 down을 보낸 상태→owner fence→각 up이 세션 종료 전에 수신/확인**, 이미 queued된 up/새 enqueue된 up, 미지원 Host 송신 0을 확인한다. 사용자 OS에 실제 X 버튼을 주입하지 않는다. 다른 key-up/mouse 3개/physical release 분류도 같은 공통 계약에서 확인한다.

## N2 (P1) — 정상적으로 거부된 비release를 Failed로 세어 남은 해제 전에 닫음

다음 순서는 gate로 동기화돼도 가능하다.

1. 정상 branch에서 새 **A key-down**이 gate_begin_action에 Admitted돼 taken/inFlight가 된다. **아직 Write 전**이다. Host에는 이전에 보낸 Ctrl 같은 다른 held 입력이 있다.
2. UI가 fence_raise를 수행하고 그 held 입력의 up들을 queue에 넣는다.
3. A down의 FenceGatedLink::Write는 정상적으로 **fenced && !releaseExchange**여서 거부한다. 실제 byte는 0이다.
4. ControlClient :1182–1185는 !actionOk를 **InputOutcome::Failed**로 기록한다. :1245에서 “fence refused”라 continue하더라도 AdmitEnd가 failed를 증가시킨다.
5. fence_verdict는 **queue/inFlight를 보기 전에 failed > failed0이면 Failed**다. timer는 Failed를 즉시 WM_CLOSE로 연결한다. 남은 정상 링크의 Ctrl up 등이 아직 queued/in-flight여도 닫을 수 있다.

고정 gate·link/outcome 매핑의 기록 fixture:

```
benign_down_refusal bytes_writes=0 held_up_pending=1 verdict_failed=1
```

이것은 실패한 release를 성공이라고 처리하라는 요구가 아니다. **fence가 쓰기 시작 전에 정책상 거부한 비release**, **이미 시작한 전송/응답의 실제 실패**, **시험 hook의 release 쓰기 거부**를 구분한다. 첫 경우는 정상적인 취소/미송신으로 정리하고 남은 required release를 계속 drain한다. 실제 release write/ACK 실패는 NOT confirmed·bounded 종료로 유지한다. 현재의 `gate_is_fenced()`만으로 거부 원인을 추정하거나 모든 실패를 Skipped로 바꾸지 않는다.

같은 결과 구분을 일반 action·clipboard·IME·thumbnail 등 gated failure 경로에도 적용해 실제 transport/protocol 오류를 단순 fence 취소라고 오인하고 손상된 교환을 계속 쓰지 않게 한다. wire 변경은 필요 없다. per-exchange gate 결정/쓰기 시작·실제 오류를 caller가 구분할 수 있는 로컬 결과로 연결하면 된다.

새 반례: **다른 held key가 있는 정상 링크→down dequeue/admit에서 sender 정지→fence→down Write 거부(0 byte)→required up 전송/ACK→그 후 종료**. release hook/진짜 ACK 부재도 별도 대조하여 각각의 결과가 취소/Failed/Confirmed로 정확히 구분되는지 확인한다. 이 순서가 현재 “fence 뒤에 새 down을 gate_begin_action으로 넣으면 Refused”라는 시험에는 없다.

## 완료 문구·hook 경계

- fence_verdict의 Confirmed에는 **skipped**도 포함된다. 현재 timer의 “acknowledged by the host”는 그 경우 사실보다 넓다. 계약상 미송신/역할상 불필요 항목과 실제 InputAck 수를 구분해 로그/판정 의미를 맞춘다. N2를 고치면서 정책상 취소까지 성공 ACK로 표시하지 않는다.
- REFUSE_RELEASE_WRITE_AFTER_FENCE는 gate에서 최초 release Write를 거부한다. r5의 after-the-fact 결과 변경과 달리 그 Write가 inner link에 도달하지 않는 구조를 수용한다. 해당 #if는 **REMOTE60_VIEWER_KA_TEST_SEAM**이다. 실제 Viewer/Host 경로의 hook EVIDENCE는 아직 없다.
- 제공 Release/GNLinkViewer.exe SHA-256 **f3349791928c09c7daf0c5a34e7c01fac271aaacc1c482dc3235be4718181932**가 보고값과 일치했다. 생성된 출하 vcxproj의 seam 정의 없음, HOLD_SEND/REFUSE_RELEASE 두 env marker **ASCII·UTF-16 0건**을 읽기 확인했다. 이 artifact의 hook 배제는 수용하며 최종 통합 artifact gate는 별개다.

## Codex 직접 근거·다음 담당

`.claude/codex_ka_pc_review/gate_r6_probe.cpp`는 **고정 OwnerFenceGate와 gate 함수/link/verdict 원문**을 사용했다. protocol/queue/inner link는 해당 필드를 제공하는 메모리·기록 fixture로, 전체 제품의 실제 wire/class 구현을 실행한 것은 아니다. 원 F1에는 실제 두 번째 std::thread를 사용했다. 반환 0은 **원 결함 수정 확인과 N1/N2 반례 확인**이며 제품 PASS가 아니다.

```
prior_F1_handoff_closed=1
X1_X2_ups_taken=0 queue_empty=1 verdict_confirmed=1 host_acks=0
benign_down_refusal bytes_writes=0 held_up_pending=1 verdict_failed=1
prior_F2_local_copy_blocked=1 writes=0
actual_Host_Viewer_network_input_power_clipboard_APIs=0
```

exe SHA-256 **11ffccf8f9f26429a1d098b6c9f04832cd53dd110413547379f7d93e20a0bdfd**. review source/산출물은 저장소 안에 보존했다. 제품 수정·전체 제품 빌드·실제 Host/UI·사용자 입력/clipboard·운영/설치·삭제·commit/push는 하지 않았다.

검증용이 **N1·N2와 완료 문구**를 기존 작업용2의 PC task 정본에 한 묶음으로 연결한다. 공유 predicate의 파일 소유는 기존 창구 안에서 조율하며 Codex가 별도 writer를 만들지 않는다. 닫은 F1 원 반례/F2 사본/T1의 재작업이나 무관 전량 시험을 요구하지 않는다. 실제 Host EVIDENCE는 공용 안전 guard와 최종 PC 고정 후보가 준비된 뒤 기존 순서에서 확인한다. 사용자 현재 조치 없음.
