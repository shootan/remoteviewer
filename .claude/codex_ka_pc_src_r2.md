# KA PC r2 — SOURCE 재검토 및 두 동작 결정

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 ka-pc-impl task.

**NEEDS_CHANGES — 아래 R1~R4를 같은 PC 작업의 한 보수 묶음으로 처리한다.** N3·N5 및 D1/D2의 보수 방향은 수용한다. 테스트 총합이 아니라 남은 동시 사용자 동작·계정 경계 때문에 전체 SOURCE를 보류한다. 제품·독립 EVIDENCE·통합·출하 승인은 별도다.

## 고정 범위와 실행 귀속

- 후보 **a864105c4f96212c5985d1adf40b68625ba56335**, 비교 **7144e1c..a864105** 전체 24파일(+1929/-148). 최초 확인 당시 HEAD 일치·tracked clean·diff --check 정상.
- PC 본체 변경, 공용 merge의 경계, 새 CMake/시험, 보고 r2 및 이전 N1~N6를 대조했다. 84/0×2·managed 61/0×2·IPC 9×7·units/lease는 **작업용 실행**으로 구분한다.
- 검토 중 HEAD가 **c4e0df0**(공용 534242b merge)으로 이동하고 후속 shell e2e 수정도 나타났다. 요청된 a864105를 정본으로 고정했고, 해당 merge가 아래 PC 제품 파일을 바꾸지 않은 것을 확인했다. 후속 미고정 시험을 a864105의 실행 근거로 더하지 않았다.
- Codex 직접 실행은 **실제 용량 판단 함수 5개의 격리 probe**와 **작업용 IPC race 산출물의 독립 실행**뿐이다. 실제 셸/Viewer/Host·사용자 입력·clipboard를 조작하지 않았다.

## 결정 ① — 연결된 PC의 단순 재탭은 정상 창 교체로 사용하지 않음

**일반 카드 재탭으로 정상 연결 창을 닫는 변화는 채택하지 않는다.** 해당 셸이 소유한 같은 Host의 정상 창이 있으면 그 창을 보여주거나 “이미 열려 있습니다”로 처리한다. managed의 기존 동작과 맞춘다. 연결 중인 시도를 새 명시 시도로 교체하는 동작은 기존 계약대로 가능하다. 별도의 명시적인 다시 연결/교체 동작이 있다면 그것은 구분해서 처리할 수 있으며, 이번 판정 때문에 새 버튼을 필수로 추가할 필요는 없다.

현재 ordinary의 begin_session은 이전 슬롯의 **동일 cancel 이벤트**를 신호한다. 예전에는 연결 뒤 no-effect였지만 이번에는 owner fence가 되므로, 카드 재탭도 정상 창을 종료시킨다. 소유 계정 종료를 정상 teardown으로 연결한 N4 방향은 유지하되, 기존 연결 시도 취소와 정상 세션의 owner 종료 의미를 구분한다. 이전 recovery 경로의 “정상 연결을 임의로 종료하지 않음” 조건을 조용히 바꾸지 않는다.

## 결정 ② — 자동 복구에도 3대 한도 적용: 수용

기존 ordinary 43/44/46 복구도 논리 3대 한도를 따라야 하며, 다른 가시 PC 3대를 밀어내거나 네 번째 창을 만들기 위해 예외를 주지 않는다. 사용자에게 한도 때문에 복구되지 않았다는 결과와 가능한 수동 행동을 알린다. owner 변경·개별 해제·최신 사용자 의도가 이미 취소한 복구는 실행하지 않으며, 반복적인 자동 재시도로 한도를 두드리지 않는다.

이는 **용량 규칙의 결정**이다. managed session 종료 뒤 새 gen으로 자동 activation/재탈취하는 권한은 계속 0이고, 기존 ordinary 복구 계약을 무제한 확장한 승인이 아니다. 아래 R1의 예약 경계도 자동/수동 경로에 동일하게 적용한다.

## R1 (P1, N2 잔여) — ordinary 신규 시도들이 staging 하나를 공유하지 않음

논리 key 합산·성공 후 축출·성공 시 최근 순서 갱신은 맞는 방향이다. 그러나 **gStagingHost는 keep_launch의 managed 후보에서만 설정**한다. begin_session의 ordinary 후보는 room_for_new_pc만 확인한 뒤 바로 gViewerByHost에 들어가며, 현재 진행 중인 신규 후보를 예약하지 않는다. 다음 다른 PC도 같은 standby_victim을 보고 허용된다.

실제 허용 순서:

1. A standby, B/C ordinary 가시 — 논리 3대.
2. D를 열기 시작: A가 축출 가능한 것으로 보여 허용. D는 아직 미연결.
3. D 완료 전에 E를 탭: 여전히 A가 축출 가능하므로 E도 허용.
4. D 성공 시 A를 축출해도 **B/C/D/E 가시 4대**가 남는다. settle_pc_count는 더 이상 standby victim이 없어 반환한다. E 성공 후에도 복구되지 않는다.

Codex 직접 probe `.claude/codex_ka_pc_review/capacity_r2_probe.cpp`는 **a864105의 logical_pcs / pc_visible / standby_victim / settle_pc_count / room_for_new_pc 원문 5개**를 그대로 사용한다. 각 원문이 고정 git show와 일치함을 확인했다. registry/profile 저장은 메모리 fixture이며 실제 셸/네트워크는 실행하지 않았다.

```
admit_D=1 admit_E=1 final_logical_PCs=4
standby_A_remaining=0 staging_empty=1
```

exit0은 결함 재현이다. exe SHA-256 **4feb0ee1d55f89ad57a84dc66b99574484b3492c1ebeb05e9c2c76e0031c2039**.

필요: ordinary/managed·수동/자동 모두 **같은 단일 staging 예약**을 사용한다. 시작 전 예약, 실패/취소/owner 변경 때 반납, 실제 성공 시점의 재판정과 commit을 결합한다. 예약 중 사용자가 기존 standby를 가시로 바꿔 더 이상 victim이 없어지는 경우도 후보를 네 번째로 확정하지 않는다. 예약 때문에 가시 3대 동작을 앱 전체 active1로 줄이지 않는다.

다음 검증은 D/E 완료 역순·둘 중 실패/취소·staged managed 대기 중 다른 tap·복구 타이머 경합이다. 성공 D만 정당한 standby 하나를 정리하고 실패/취소 후보 때문에 기존 PC를 추가 축출하지 않아야 한다. 현재의 순차 fourth 성공/실패 시험은 이 반례를 다루지 않는다.

## R2 (P1, N1/N4 수명) — ordinary 연결 통지에 owner epoch와 종료 순서가 없음

pump_viewer_output_to_log는 연결 로그에서 **operation+hostId만** post하고, on_ordinary_connected는 gViewerByHost의 operation만 비교한다. end_owner_children는 old ordinary 슬롯을 즉시 별도 snapshot으로 분리하지 않고 종료 때까지 map에 남긴다. 따라서 owner epoch를 올린 뒤 지연된 옛 연결 통지가 오면 그 슬롯은 여전히 일치할 수 있다. recent_touch_host는 그 순간의 **새 gAccountId**를 읽고, settle_pc_count도 현재 registry를 건드린다.

즉, old 연결 로그→post 지연→로그아웃/새 로그인→old 연결 통지 소비 순서에서 새 계정의 recent/용량 판단에 옛 결과가 들어갈 수 있다. handle_viewer_exit의 ownerEpoch 검사는 이 별도 통지에는 적용되지 않는다. 이는 소스상 허용되는 순서이며 실제 계정/사용자 데이터를 조작해 재현한 것은 아니다.

또한 로그 pump와 process-exit watcher는 독립 worker다. exit가 먼저 소비돼 gConnectedOps를 erase한 뒤, 아직 안 비운 stdout에서 연결 로그를 뒤늦게 insert하는 순서가 남는다. 정상적인 빠른 종료의 첫 유지 전환 누락/표식 잔류를 막는 보장도 필요하다. 셸 시험이 먼저 ordinary_connected를 기다린 뒤 창을 닫는 것은 이 역순을 고정적으로 피한다.

필요: 생성 시 owner epoch/account/operation을 연결 완료 통지에 묶고 소비 전에 검증한다. old-owner registry와 teardown 소유를 분리해 새 owner의 용량/최근 상태를 갱신하지 못하게 한다. 연결 로그 수집과 최종 exit 결정의 합류도 유한하게 처리해 늦은 insert가 종료된 op를 되살리지 않게 한다. UI thread에서 stdout EOF를 무기한 기다리라는 요구는 아니다.

검증: owner 전환 뒤 지연 연결 통지, 새 계정의 같은 host, exit 뒤 연결 로그 도착, 연결 직후 즉시 X. 새 계정 오염·stale mark·정상 first conversion 누락을 각각 확인한다.

## R3 (P1, N4 잔여) — owner fence가 실제 입력·선택 경로 전체를 닫지 않음

정상 ordinary에 소유 cancel을 전달하고 joined watcher→UI fence→WM_CLOSE teardown을 쓰며 fallback을 8초로 둔 방향은 수용한다. 로그아웃 exit0/외부 Viewer 보존의 작업용 근거도 기록한다. 다만 현재 fence는 메시지 일부를 거르는 bool일 뿐이다.

- kMsgOwnerFence는 **inputEnabled를 내리지 않고**, 키/마우스의 일반 범위·WM_INPUT·clipboard post만 filter한다. **WM_IME_COMPOSITION은 그 범위 밖**이며 handler→send_ime_result_text→enqueue_input_text_units가 계속 실행될 수 있다. enqueue는 inputEnabled를 보는데 여전히 true다. fence 뒤 WM_CLOSE가 처리되기 전에 큐에 있던 IME 결과가 전송될 수 있다.
- **kMsgRunDeferredPick/restore/toolbar·macro 등**은 그 filter에 없다. deferred selection은 current run/연결/visible만 검사하며 owner fence를 보지 않으므로 남은 선택을 다시 진행할 수 있다. 선택/입력의 실제 진입점에서도 fence가 성립해야 한다.
- release_all_physical은 gPhysicalDown의 scan-key 목록만 처리한다. 현재 fence에서 일반 VK/마우스 버튼까지 해제했다고 단정하지 않는다. 기존 release helper와 올바른 순서로 held input을 회수하고, 그 뒤 새 입력/선택을 차단해야 한다. gate를 먼저 내려 필요한 release까지 버리는 보수도 피한다.
- fenced kMsgApplyClipboard를 DefWindowProc로 반환하면 원래 handler의 **RemoteTextApply 소유 해제**도 건너뛴다. 적용은 하지 않더라도 소유한 payload는 해제한다.

초기화 취소도 아직 부분적이다. ordinary cancel watcher는 **창·decoder 생성 뒤**에 시작하므로, 상속 cancel 이벤트가 시작 전에 이미 signal돼 있어도 run_session_body 첫 connectCancelled는 false일 수 있다. managed도 첫 flag 확인과 HWND 생성/Show 사이 취소가 들어오면 watcher가 아직 없는 HWND에 post하지 못한 뒤 늦은 init이 진행할 수 있다. 첫 줄의 atomic load 하나로 “창 전 취소는 창0” 전체가 보장되지는 않는다.

필요: owner 종료와 새 init/표시·입력 enable의 수명을 결합하고, pre-signaled ordinary cancel 및 init 중 managed Release를 실제 경로에서 처리한다. fence 뒤 IME 결과·deferred pick·매크로/클립보드 내부 post가 새 작업을 보내지 않으며 held key/button은 해제되는지, init 지연/완료 역순에서 옛 owner 창/입력이 되살아나지 않는지 격리 fixture로 확인한다. 이 항목은 **소스 경로 지적**이며 Codex가 실제 IME/clipboard를 조작했다는 뜻은 아니다.

## R4 (P2, N6 잔여) — 유효한 다른 PC 완료가 현재 카드 busy를 지움

현재 op 검사와 Activate Send 성공 뒤 activeOp 설정은 수용한다. taken/connected 분리와 handshake 실패 오류 표시도 원 결함을 겨냥한다. 그러나 **각 PC에 유효한 완료**와 **지금 연결 중인 카드**는 별개다.

A activation→B를 새로 tap해 B 연결 중→현재 A의 정상 PickerReady가 도착하면, on_managed_event는 op 일치로 수락한 뒤 **전역 post_status("idle")**를 보낸다. shell.html의 status idle/error는 setBusy(false)로 **connectingId를 무조건 지운다**. B가 아직 준비 중인데 카드 잠금이 풀린다. A의 정상 SessionEnded/오류/해제 결과도 같은 문제가 있다.

늦은 **옛 incarnation C**를 Current로 버리는 검사만으로는 해결되지 않는다. A와 B 모두 현재 child/owner/op인 순서를 검사해야 한다. 카드 busy/완료 통지를 host+op에 결합하고, 다른 PC의 결과가 B의 연결 상태를 지우지 않게 한다. 전체 상태줄과 카드별 진행 상태를 구분한다. N6의 미시험 3종은 새 고정 후보에서 채우되, 시험 추가만으로 위 실제 소스 경계를 닫았다고 하지 않는다.

## 수용한 부분과 다음 검증

- **N1의 PC별 정상 close 판단·최근 순서의 connect 시점 이동**은 수용하되 R2의 비동기 완료 경계를 보수한다.
- **N3**의 managed 준비/대기/활성 카드 해제·suppression·명시 connect 해제·Current에 의한 옛 child 배제 방향을 수용한다. source상 존재뿐 아니라 새 독립 UI 검증에서 같은 행동을 확인한다.
- **N5는 닫음**: abort 뒤 새 I/O 제한 및 완료할 때까지 반복 cancel이 기존 cancel-before-write/read 틈을 겨냥하며, handle은 해당 I/O thread 종료 후 닫는다. 완료 reaper도 다음 ending에서 회수한다.
- Codex가 작업용 **ka_pc_ipc_close_race_test**를 직접 실행해 **9/9, rc0**을 확인했다. 늦게 시작된 write/read의 cancel miss 도달, 이미 막힌 write, partial frame, 정상 drain이 포함된다. exe SHA-256 **02ecb319895e51671b3adb767cecb27b397e908f810a3022547198ddd6854448**. 파일/네트워크/사용자 창을 쓰지 않는 pipe 시험이다. 이는 독립 실행이며 독립 빌드는 아니다.
- **D1/D2**의 정리 미확인 문구·12초는 grant proof 아님·명시 재탭 허용·자동 준비에만 재확인 예산을 적용하는 방향을 수용한다. 새 종료 wire는 필수로 추가하지 않는다.
- 고정 a864105의 공용 Host는 **a7773e6**이므로 최종 후보는 검수한 **534242b 이상**으로 맞춰야 한다. 이미 나타난 c4e0df0 merge를 이 PC 전체의 승인으로 취급하지 않는다. e r2의 별도 SOURCE·AVD, dirretry cycleCv_와 worker 동시 보존도 별도 경계다.
- 일반 셸 회귀를 화면 위/바깥 쓰기 때문에 실행하지 않은 판단을 수용한다. 필요한 영향 행동만 내부 root·기존 화면 밖 seam으로 옮기거나 동등한 실제 UI 경로로 확인한다. 무관 4대 이상 가시 허용 assertion을 새 정책 때문에 무작정 유지하지 않는다.
- 실 NAT·비용·takeover를 network로 보이게 한 반례·동시 잠금·401·셸 종료/계정 전환은 미검증으로 유지한다. 실제 WebView DOM click→bridge→Viewer의 새 근거는 진전이지만, 순차 성공 84개만으로 위 경쟁/계정 동작을 대신하지 않는다.

검증용은 R1~R4와 두 결정을 같은 task 정본에 취합해 기존 작업용2로 연결한다. 제품을 Codex가 직접 수정하거나 별도 작업용에게 중복 지시하지 않았다. 검토 probe/판정서 외 제품·운영/설치·삭제·commit/push 변경 없음. history/계획/Git MCP 기록은 같은 구현 단위에서 취합한다. 사용자 현재 조치 없음.
