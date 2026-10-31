# KA PC ③ 구현 r1 — SOURCE 및 두 정책 결정

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / task ka-pc-impl.

**NEEDS_CHANGES.** O1·모델 A·자동 준비 OFF / 명시 목록 복귀 유지 ON은 유지한다. 아래 N1~N6을 같은 PC 작업의 보수 묶음으로 취합한다. 공용 FINDING은 이미 조사 중인 작업용1 소유로 유지한다. 이 판정은 SOURCE 검토이며 독립 EVIDENCE·통합·출하 승인이 아니다.

## 고정 범위와 근거 귀속

- 후보 **7144e1c79d1e090c6d4e2b8f446dbfdb3a522d1d**, 비교 **3d86268..7144e1c** 전체 22파일(+2887/-27). 검사 당시 HEAD 일치, tracked clean, diff --check 정상. 공용 `standby_*` 변경 0.
- `report_ka_pc_impl_r1.md`, 정본 `proposal_ka_pc_r2.md`, 기존 PC 설계·kept 모델 및 APK e D1 판정을 대조했다. 셸의 실제 bridge/exit/launch 경로와 Viewer 재진입·IPC·자원/소켓 소유, 새 시험을 읽었다.
- 58/0×2·53/0×3·units 20/20·재진입 및 WGC 캡처/첫 전환 680 ms는 **작업용 실행 근거**다. 보존된 e2e-11 Viewer 로그의 activation→control 연결→picker paint 순서도 확인했다. Codex가 제품 셸/Viewer/Host를 실행하거나 독립 빌드한 것은 아니다.
- Codex 직접 실행은 아래 N5의 **실제 후보 IPC header를 포함한 검토용 pipe 시험**뿐이다. 제품 코드는 수정하지 않았다.

## D1 — returning 12초: 새 wire 신호는 이번에 필수로 추가하지 않음

APK의 `codex_ka_e_src_r1.md` D1과 동일하게, 기존 session timeout에 따른 정리 tail 및 그 도중 **명시 탭의 새 gen rollover**를 조건부 수용한다. 자동 재activation은 0이다.

단, **12초 경과는 Host grant 반환 확인이 아니다.** `kReturningMs`·local SessionOver·entry/Pong 생존으로 이전 Host session/영상이 정리됐다고 판정할 수 없다. 현재 주석의 “Host가 끝낼 수 있는 시간이 지났으니 ready”와 시험의 같은 추정은 확인 근거로 사용하지 않는다.

기존 wire로 확인할 수 없는 사실은 “이전 연결 정리 중일 수 있음/정리 상태 미확인”으로 남긴다. `준비됨`을 local handoff 완료와 살아 있는 entry라는 뜻으로 사용한다면 그 정의와 한계를 UI·정본에 함께 반영한다. 12초 뒤 정리 완료로 의미를 바꾸지 않는다. 정리 미확인을 이유로 명시 재탭이나 개별 해제를 무기한 막지 않는다. held input release·clipboard 차단은 즉시 수행하고, Host의 잔류 영상/relay 비용은 안정 standby 비용과 분리 측정한다.

## D2 — 60초 뒤 1회: 자동 준비의 제한된 재확인 예산으로만 수용

owner 범위의 **자동 준비 재확인 예산**을 60초 간격·추가 1회로 제한하는 것은 수용한다. 무응답 때 일반 Hello/activation으로 자동 우회하지 않는다. 실제 코드에는 60초 타이머가 없고, 그 시간이 지난 **다음 keep_launch 요청**을 허용한다. 따라서 “60초 뒤 자동 재확인했다”라고 보고하지 않는다.

다만 현재 `times >= 2`는 같은 owner 동안 모든 keep_launch를 막는다. 사용자가 PC를 업데이트하거나 네트워크를 복구한 뒤 **명시적으로 다시 유지/연결을 요청한 경우**에도 같은 자동 예산으로 영구 차단하지 않는다. 명시 재시도는 새 유한 시도로 처리하고, 중복 클릭 폭주만 제한한다. 카드의 `상시 유지 미지원 (PC 업데이트 필요)`는 무응답/실패의 확정 진단이 될 수 없다. 확정 미지원과 `유지 확인 안 됨`을 구분한다. 이것은 아래 공용 FINDING을 고치는 대신의 우회가 아니다.

## N1 (P1) — 나중에 연 PC가 있으면 먼저 연 PC의 첫 유지 전환을 생략함

`client_shell_main.cpp:1318`의 `operation != gViewerOperation` 분기는 **모든 경우 return**한다. 새 `keep_after_close`는 그 뒤 `:1362`에만 있다. 정상적인 A 연결→B 연결→A 창 X 순서에서 A는 전역 최신 operation이 아니므로, 연결 성공·같은 owner·정상 종료여도 standby 전환이 없다. B를 먼저 닫고 A를 닫아도 A의 operation은 전역 최신으로 돌아오지 않는다.

기존 코드가 막던 “옛 종료가 최신 연결 UI/reconnect를 덮는 일”과, 이번에 필요한 **각 PC의 정상 종료→첫 유지 전환**을 분리해야 한다. owner·해당 host의 실제 세션/operation 유효성을 확인한 뒤 유지 정책을 실행하고, 다른 PC의 busy/reconnect는 덮지 않는다. 같은 host의 교체된 옛 세션은 별도로 배제한다. `gConnectedOps` 정리도 중간 return·owner 변경을 포함한 모든 종료 경로에서 끝낸다.

시험: 실제 셸에서 A/B/C를 연결한 뒤 종료 순서를 바꿔 각 PC의 첫 전환을 확인한다. 기존 e2e의 `:594..635`는 ordinary 종료 뒤 시험이 managed Viewer를 **직접 시작**한다. 새 lease admission 가능성/680 ms의 근거는 되지만 제품 `handle_viewer_exit→keep_after_close` 연결의 근거는 아니다.

## N2 (P1) — 용량·축출·최근 순서가 실제 논리 PC 계약과 다름

`keep_launch(:1807..1828)`는 gManaged만 세고, **후보 Launch/admission 전에 기존 standby를 Release**한다. A/B/C 유지 중 D의 프로세스 생성·Init·admission이 실패하거나 D가 미지원이어도 기존 한 대가 이미 사라진다. 실패 D eviction 0 조건과 반대다.

또한 `begin_session`의 ordinary Viewer와 gManaged가 하나의 용량 장부를 쓰지 않는다. 예를 들어 A/B/C standby 상태에서 ordinary D를 열면, D가 활성인 동안 기존 3대와 함께 남는다. ordinary 창 3개도 gManaged 크기에 반영되지 않는다. 앱 전체 active를 1개로 줄이는 것으로 해결하지 않는다.

필요: **ordinary 활성 + managed 활성/대기를 host key별로 합산한 최대 3대**, staging 최대 1개의 유한 후보를 둔다. D 실패는 기존 3대 보존, D 성공 때만 standby LRU를 commit한다. visible 3개면 신규 후보를 명시적으로 거부하고 가시 창을 축출하지 않는다.

최근 순서도 ordinary의 성공 시점이 아니라 `keep_after_close(:1859)`의 **창 닫는 시점**에 touch한다. A 사용→B 사용→A를 나중에 닫으면 A가 최신이 되어 최근 사용/LRU가 뒤집힌다. 성공한 실제 사용·재사용에서 순서를 갱신하고, 유지 준비·Ping·닫는 순서로 갱신하지 않는다. 실패/취소 D, 성공 D, ordinary/managed 혼합, 서로 다른 창 종료 순서로 확인한다.

## N3 (P1) — 사용자에게 개별 “이 PC 연결 해제” 경로가 없음

정본 §2는 **개별 해제→Release→자식 종료→그 key 자동 유지 억제**, 새 명시 접속 때만 억제 해제를 요구한다. 현재 shell.html 카드에는 connect만 있고 native bridge에도 개별 해제 동작이 없다. `ManagedViewers::Release`는 축출·실패·전체 종료에서만 호출된다. managed 창 X는 standby 복귀이므로 개별 해제를 대체하지 못한다.

해당 PC의 준비 중/대기/활성에서 실제로 누를 수 있는 해제 동작을 연결하고, pending/staging·늦은 완료도 무효화한다. 해제한 A를 refresh·history 자동 준비·늦은 IPC가 다시 만들지 않아야 하며, B/C는 보존한다. 명시 A 재접속만 suppression을 푼다. UI 렌더→실제 입력→bridge→Release/자식 종료까지 확인한다.

## N4 (P1) — 로그아웃의 일반 활성 Viewer는 fence/정상 종료 없이 강제 종료됨

`end_owner_children(:1950..1967)`는 기존 connect 취소 이벤트를 보내고 3초 뒤 TerminateProcess한다. 그러나 `native_video_client_main.cpp:125..139`는 연결 뒤 취소 watcher를 이미 끝내며, 이후 이벤트에는 **“no effect (session established)”만 로그**한다. 활성 ordinary Viewer에는 정상 종료 요청이 도달하지 않는다. 이 경로는 정상 세션도 항상 강제 회수 대상이며, 그 대기 동안 이전 계정의 입력/clipboard/화면 경계도 닫히지 않는다.

설계에서 허용한 강제 회수는 **자기 자식에 정상 fence/종료를 요청한 뒤의 유한 fallback**이다. launch에서 보유한 handle/상속 채널로 소유한 활성 Viewer의 입력·clipboard를 차단하고 정상 WM_CLOSE/teardown까지 도달하게 한다. PID/창명 검색이나 외부 standalone Viewer 종료로 범위를 넓히지 않는다. UI에서 3초 대기를 하지 않는 현재 구조는 유지한다.

managed 쪽도 Release/Shutdown이 init보다 먼저 오는 반례를 검사한다. 현재 watcher의 closeIt은 그 순간 HWND가 없으면 connectCancelled만 세우며, run_session_body는 이를 확인하기 전에 창·decoder를 만든다. 늦은 초기화가 옛 owner 창/입력을 다시 켜지 않는다는 설계 조건을 실제 경로에서 지킨다. 로그아웃 직전·handshake/init 대기 중·이미 활성 상태를 나누어 정상 종료, 강제 fallback, 다른 owner/외부 Viewer 보존을 확인한다.

## N5 (P2) — IPC Close의 유한 종료 보장이 경쟁 상황에서 성립하지 않음

`managed_viewer_ipc.hpp:183..194`는 writerDone을 1초 기다린 뒤 CancelSynchronousIo를 **한 번** 호출하고 무기한 join한다. WriteLoop는 close 뒤에도 큐를 비우며 새 동기 WriteFile을 시작할 수 있다. writer가 아직 WriteFile에 들어가지 않은 순간 취소하면 ERROR_NOT_FOUND이고, **그 뒤 시작한 쓰기는 취소되지 않는다**. Microsoft의 [CancelSynchronousIo 계약](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio)도 현재 pending I/O만 대상으로 하며 취소할 요청이 없으면 ERROR_NOT_FOUND라고 명시한다.

Codex 직접 재현: `.claude/codex_ka_pc_review/channel_close_probe.cpp`가 **후보 원문 header**를 include한다. 검토용 wrapper는 writer를 첫 WriteFile 직전에 세워 취소와 쓰기 사이의 스케줄만 고정한다. 실제 익명 pipe·실제 CancelSynchronousIo/WriteFile을 사용하고 상대는 읽지 않는다. 세 번 모두:

```
close_done=0 write_entered=1 cancel_error=1168
elapsed_ms=1812 / 1812 / 1813
recovered_after_peer_close=1
```

1초 뒤 취소가 실패한 다음 쓰기가 시작했고, 1.8초 관찰 시에도 join에서 막혔다. 시험이 자기 peer handle을 닫자 종료했다. exit 0은 **결함 재현**이다. 제품 파일 수정·사용자 프로세스/창 조작·파일 삭제는 없다. exe SHA-256 **268429c8cac99d3a1f1915db9e01c9985e7f25331bfd189953d3b356d679fd3c**, 포함한 header SHA-256 **c5613ba0b3f6b5748a86d33e3086069acd8384a622442d38c071f21f57c247f4**.

필요: close deadline 이후에는 새 I/O를 시작할 수 없고 이미 진행 중인 I/O도 회수되는 동기화/취소 구조를 만든다. 단순 open 검사 한 번이나 Cancel 한 번을 추가하는 것으로 같은 틈을 남기지 않는다. 정상 마지막 메시지 drain·읽지 않는 peer·취소 직전/직후 새 write·부분 쓰기/EOF를 함께 확인한다. 현재 units의 “이미 막힌 WriteFile 취소” 성공은 이 순서를 다루지 않는다.

관련해서 `ManagedViewers::End(:328)`는 해제마다 joinable reaper를 vector에 추가하고 **앱 종료의 JoinReapers에서만** 회수한다. 완료된 thread handle도 장시간 축출/재준비 횟수에 따라 쌓인다. 정상 실행 중 완료 reaper를 유한하게 회수하고, 반복 Release/재생성의 thread/handle 증가가 안정되는지 확인한다. child Job 강제 회수가 모든 channel 사용자와 이 경쟁을 없앤다고 가정하지 않는다.

## N6 (P2) — 소켓 인수와 실제 연결 결과를 혼동하고 activation 상태를 누락함

`viewer_managed_standby.cpp:309..320`는 **standbyTaken을 connected로 보낸다**. 이것은 open_media_socket이 dup 소켓을 인수한 사실이며 인증 handshake 성공이 아니다. 인수 뒤 handshake 실패(rc6)도 connected=1이다. 셸 SessionEnded는 reason이 not-ready가 아니고 connected=1이면 오류 대신 idle로 간다. 실제 연결 실패가 정상 종료처럼 처리된다.

소켓 소유/SessionOver 의무는 그대로 분리하고, handshake/control의 실제 연결 성공과 rc/종료 사유를 정확히 보낸다. dup 인수 뒤 실패·취소에서 연결 성공/정상 종료를 표시하지 않는다.

또한 activate()가 세션 실행 동안 blocking이므로 main loop의 publish는 **활성 동안 실행되지 않는다**. 셸도 Activate 성공 때 hostKeep을 active로 바꾸지 않아, 카드가 이전 ready/returning을 계속 표시한다. 활성/연결 중 상태를 정확한 op와 연결한다. `Current`는 key/inc/epoch까지만 검사하고 PickerReady/Presented/SessionEnded handler는 op를 검사하지 않으므로 activation 메시지의 op 검증도 소비 지점에 넣는다. `ManagedViewers::Activate`의 Send 실패 때 미전송 activeOp를 남기는 것도 함께 정리한다.

실패 handshake, 실제 active 배지, 다른 PC 연결 중 늦은 이벤트, 옛 op 메시지, IPC 송신 실패를 셸 bridge까지 검사한다. 기존 보존 로그에서는 성공 handshake/control 뒤 picker paint가 나왔고, 그 성공 경로의 PickerReady/Presented·WGC 근거 자체를 부정하는 지적은 아니다.

## 공용 의존 및 다음 검증 경계

- **같은 key 30초 내 재lease 무응답 FINDING**은 원인 미확정으로 유지한다. kStandbyHelloReplayMs와 시각 일치는 원인 증명이 아니다. 기존 작업용1 조사에 결과를 연결하며 중복 지시하지 않는다. 즉시 crash/EndAll/축출 뒤 재유지는 실사용 복구 경로다. 31초 기다린 시험 성공은 그 조건의 admission 근거이며 즉시 복구 성공 근거가 아니다.
- 공용 동기 capability의 connected socket 재관찰 문제는 기존 APK e N2 판정과 같은 의존이다. PC의 cfg는 그 동기 factory를 사용한다. 공용 담당의 검수된 보수를 통합한 뒤 실제 PC Lost/fresh admission을 확인한다. PC 담당이 standby_*를 따로 고치지 않는다.
- O1의 suspended→자식별 비상속 Job→resume, private pipe로 token 전달, epoch 먼저 증가, 자동 새 activation 0 방향은 유지한다. lease를 셸로 옮기거나 옛 장기 park 모델을 되살릴 필요는 없다.
- 다음 고정 후보에서 위 반례를 **실제 셸 WebView 카드/bridge + 실제 Viewer**로 검증한다. 현재 58개 시험은 이를 대체하지 않는다. 401/권한 철회와 network 분리, 같은 key 동시 잠금, takeover를 network처럼 보이게 한 반례, init 역순 취소, 다른 두 PC 보존, standby 자원 0/1/3 및 반복 비용은 남은 EVIDENCE다. 실 NAT·직접 admission·실기 비용은 별도 미검증으로 유지한다.

이 미추적 판정서/검토용 probe 외 제품·history·계획·commit/push·배포·설치는 변경하지 않았다. history/계획/Git MCP 기록은 검증용이 같은 구현 작업에 취합한다. 사용자 현재 조치 없음.
