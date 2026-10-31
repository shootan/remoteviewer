# KA PC r3 — SOURCE 재검토

> **환경 근거 정정(2026-10-28): 작업용 PC 실 Host e2e의 입력 주입 ON이 보고돼 무주입 격리 성공 주장은 철회한다.** 123개 등의 기능 관측은 보존하지만 안전한 하네스 재확인 전에는 완료 관문에 사용하지 않는다. 본문의 SOURCE NEEDS_CHANGES와 Codex의 제한 probe 결과는 유지하며 PC 최종 EVIDENCE OK는 발급되지 않았다. [입력 주입 Finding](codex_test_injection_finding.md) 참조.

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 ka-pc-impl task.

**NEEDS_CHANGES — R1·R2·R4의 잔여 순서와 R3의 실제 종료 경계를 같은 묶음으로 보수한다.** owner epoch·기존 슬롯 분리·입력 gate·payload 해제·managed 상태 분리 등 해결된 부분은 유지한다. 전체 SOURCE·독립 EVIDENCE·출하 완료 승인은 아니다.

## 고정 범위·직접 실행

- 후보 **5a1fb457a08340e48d4ca4132e68f80f0aec3af1**, 제품 보수 **705ed23**. 비교 **a864105..705ed23** 및 시험 **705ed23..5a1fb45**를 대조했다. 최초/최종 HEAD 일치·tracked clean·diff --check 정상.
- 공용 **534242b merge c4e0df0**는 기존 Host punch SOURCE/EVIDENCE와 연결한다. 후속 두 commit은 shell e2e 보정뿐이며 제품 변경 0이다.
- 보고의 shell 123/0×2·managed 62/0×2·units와 부정 대조는 **작업용 실행**이다. Codex는 실제 셸/Viewer/Host를 이번에 실행하거나 독립 빌드하지 않았다.
- Codex는 고정 git show에서 추출한 **용량 helper 7개**, **pump 종료/timeout 원문**, **실제 페이지 status handler**를 각각 격리 실행했다. registry/profile·DOM은 fixture이며 전체 UI 재현이라고 주장하지 않는다. 아래 exit0은 반례 재현이다.

## R1 (P1) — 예약 뒤 기존 PC가 해제되면 같은 빈자리를 다른 후보에 또 줌

모든 시작이 reserve_for_new_pc를 거치는 변경과 마지막 standby 활성화 거부는 원 반례를 겨냥한다. 그러나 함수는 **기존 gStagingHost를 검사하기 전에** `logical_pcs(hostId).size() < 3`이면 성공한다. logical_pcs는 이미 예약된 D를 제외한다. 이 사이 기존 PC가 해제되면 D가 점유할 빈자리를 E에게 또 준다.

실제 허용 순서:

1. A standby, B/C 가시 → D 시작, gStagingHost=D.
2. 사용자가 **A 연결 해제**. A의 자기 예약 반납은 D를 바꾸지 않고 A만 registry에서 제거한다.
3. E 탭 → logical_pcs(E)는 D를 제외한 B/C의 **2대**라 예약 검사 앞에서 성공한다.
4. D/E 성공 → B/C/D/E가 가시 **4대**. 남은 standby가 없어 settle_pc_count는 반환한다.

`.claude/codex_ka_pc_review/capacity_r3_probe.cpp`의 고정 원문 helper 실행:

```
reserve_D=1 release_A=1 admit_E=1
final_logical_PCs=4 staging_empty=1
```

exe SHA-256 **a38b5fb8cfcd0682a02020a0acf056c99947ce23a89b2439e3e9de2215532c1f**. A 해제는 실제 release_kept_by_user와 같은 reservation 반납/registry 제거 효과로 fixture에 적용했다. 실제 프로세스/계정은 사용하지 않았다.

필요: 예약은 기존 victim이 사라지거나 슬롯이 비어도 **성공/취소 전까지 자기 자리를 점유**해야 한다. 기존 예약을 포함해 가용 자리를 계산하거나, 조건 변화 때 예약을 정상 점유로 원자적으로 전환한다. 개별 해제를 막아 해결하지 않는다. 성공 commit 시에도 더 이상 합법적인 자리가 없으면 네 번째로 확정하지 않는다.

새 시험: D 예약→A 해제/오프라인/정상 종료→E, 완료 역순·실패/취소·자동 복구·managed 준비를 같은 예약 정책으로 확인한다. 현재 D/A/E 한 턴의 “A 활성화 거부”는 **A 해제** 반례를 다루지 않는다.

## R2 (P2) — pump 종료와 timeout 표식 삽입이 엇갈려 표식이 남음

owner epoch를 연결 통지에 묶고 old ordinary 슬롯을 즉시 분리한 부분은 수용한다. 정상 pump 완료→connected 통지→exit 합류 방향도 맞다. 다만 종료 표식의 생성과 제거는 아직 하나의 완료 상태가 아니다.

현재 pump EndGuard가 gEndedBeforePump를 erase한 **뒤** wrapper가 pumpDone을 signal한다. exit worker는 Wait가 timeout됐으면 이후 lock을 잡아 표식을 insert한다. 허용 순서:

```
exit Wait -> TIMEOUT
pump EndGuard -> erase, pumpDone signal, pump 끝
exit -> insert(gEndedBeforePump)
```

이 표식은 더 이상 지울 pump가 없다. 고정 후보의 EndGuard와 timeout if 원문을 사용하고, 실제 Win32 Wait timeout 직후 pump가 먼저 끝나도록 스케줄만 제어한 **`.claude/codex_ka_pc_review/pump_r3_probe.cpp`** 결과:

```
pump_done=1 marker_retained_after_pump_end=1
```

exe SHA-256 **aa4f38cadc7bc40df22a694f61fb27891fb2dc2421bef82e7a99c22029c548fa**. 새 계정이 변경됐다는 재현은 아니며, 종료 표식의 회수 누락을 직접 확인한 것이다.

필요: pump 완료·exit 결정·늦은 connected 거부를 **동일한 per-op 완료 상태/lock 경계**로 처리한다. timeout 뒤 다시 완료 상태를 확인하고, 이미 끝난 pump에 회수되지 않을 표식을 남기지 않는다. 2초 budget을 없애거나 UI에서 기다리라는 요구는 아니다. timeout 직전/직후 pump 종료 순서를 강제하고 양쪽 종료 뒤 mark/표식 누적 0을 확인한다.

## R3 (P1) — held release를 큐에 넣은 뒤 전송 전에 control loop가 끝날 수 있음

새 inputEnabled gate, IME/deferred/restore 차단, payload 3종의 소유 해제, macro 정지·held release 집합의 **enqueue 순서**는 수용한다. 하지만 modifierUps=11 로그는 실제 Host 수신/적용 증거가 아니다.

enqueue_release_all_modifiers는 enqueue_input_event 호출마다 queued를 증가시켜 반환한다. kMsgOwnerFence는 release들을 넣은 직후 inputEnabled를 내리고 **바로 WM_CLOSE를 post**한다. WM_CLOSE는 running=false로 만들고, shutdown_viewer는 control channel을 닫는다. ControlClient::Run은 **while(running)** 안에서만 inputQueue를 scheduler로 꺼낸다.

따라서 control sender를 잠시 멈춘 상태에서 UI fence→WM_CLOSE가 먼저 처리되면 **release가 큐에만 있고 송신되지 않은 채 종료**할 수 있다. 입력 gate를 닫기 전에 넣었다는 것과 Host에 release가 도착한 것은 다르다. 현재 shell 시험은 Viewer의 modifierUps 로그/exit0만 확인하므로 이 순서를 검사하지 않는다. 이는 소스상 가능한 순서이며 Codex가 실제 held 키/Host OS 입력을 이번에 조작한 것은 아니다.

필요: 신규 입력/선택은 즉시 막되, 이미 held였던 release는 **전송/확인 또는 유한 실패 budget**까지 처리한 뒤 정상 teardown으로 넘어간다. 필요하면 기존 Ping/ack와 release 한정 drain을 활용할 수 있다. 새 wire를 필수로 추가하라는 요구는 아니다. network가 죽은 경우의 bounded 실패 종료와 정상 링크에서 release 선행을 구분한다.

격리 transport/Host 기록기에서 실제 key/button down→sender gate→owner fence→gate 해제 순서를 만들고, release 수신이 session 종료보다 먼저인지 확인한다. 사용자 PC의 실제 키/clipboard를 임의로 바꾸지 않는다. IME/clipboard/deferred payload drop도 source만으로 실 UI 성공이라고 보고하지 않는다.

초기화 취소의 pre-signaled ordinary 경로와 설정 후 connect 차단은 수용한다. 설정 중의 취소는 창을 잠시 만들었다 닫는 경계가 남으므로 “모든 취소에서 창0”으로 확대하지 않는다. 독립 검증에서는 늦은 초기화가 입력/연결을 다시 켜지 않는 것을 확인한다.

## R4 (P2) — ordinary exit의 전역 status가 다른 managed 연결 카드를 풀음

managed 이벤트·해제·거부에 hostId를 붙인 변경은 맞다. 그러나 **handle_viewer_exit의 ordinary 종료/reconnect/error/idle에는 여전히 post_status**가 남아 있다. 예를 들어 ordinary A를 연 뒤 managed B 연결 중 A가 종료되면, A는 최신 ordinary operation이므로 전역 idle/error를 보낸다. 페이지는 hostId 없는 상태를 전역으로 해석해 B의 connectingId를 지운다.

고정 페이지 handler를 그대로 실행한 **`.claude/codex_ka_pc_review/status_r3_probe.js`**:

```
current_card="B"
scoped_A_idle="B"
ordinary_A_idle=""
```

이는 실제 handler + fake DOM의 제한 실행이며 전체 UI 재현은 아니다. ordinary caller가 hostId 없는 JSON을 보내는 점은 고정 소스의 post_status 호출과 대조했다.

필요: ordinary exit/복구를 포함한 **알려진 PC에 대한 상태**도 post_host_status로 전달한다. 계정/앱 전체 상태만 전역으로 둔다. managed A→ordinary C뿐 아니라 **ordinary A→managed B 연결 중 종료/오류**의 혼합 경로를 실제 셸에서 확인한다. 현재의 페이지만 a864105로 바꾼 대조는 managed 경로의 보수 근거로 유지하며, 모든 status caller가 수정됐다는 증거로 확대하지 않는다.

## 두 결정과 후속 경계

- **결정①**: 셸이 connected로 확인한 ordinary 재탭을 forward/이미 열림으로 바꾼 분기는 수용한다. 다만 보고한 “connected 줄 미도착이면 교체”는 기존 정상 창 보존 계약의 완전한 해결이 아니다. 살아 있는 Viewer의 통지 지연은 **exit 뒤의 2초 pump join으로 줄어들지 않는다**. 연결 시도 교체 신호와 owner 종료 fence를 구분해, Viewer가 이미 연결된 경우 정상 창을 닫는 신호로 해석하지 않게 한다. parent의 늦은 로그만으로 실제 정상 연결을 연결 중이라고 단정하지 않는다. 연결 확정/취소가 교차하는 순서를 같은 보수에서 검사한다.
- **결정②**: 자동 복구에도 한도/예약을 적용하고 거부 뒤 반복 재시도하지 않는 방향을 수용한다. R1 수정은 자동·수동 양쪽에 적용한다. managed 자동 새 activation/재탈취 0은 유지한다.
- d9a75f1의 bounded 로그인 재입력과 5a1fb45의 **행동 이후 로그만 검사**하는 보정을 수용한다. setup 실패와 R4의 실제 구 페이지 실패를 구분해 보존한 점도 맞다. 로그 재사용으로 “축출 1회”를 잘못 세던 행을 제품 결함 재현으로 세지 않는다.
- N3 managed 해제/suppression·N5 IPC/reaper·공용 534242b의 기존 판정은 유지한다. 위 scope를 이유로 무관 suite를 다시 전량 반복하지 않는다.
- 실제 IME/clipboard/deferred drop·강제 exit/pump 역순·일부 예약 경합·client_recovery_ui_test의 안전한 영향 동작·실 NAT/비용/takeover/동시 잠금/401/셸 종료·계정 전환은 각각 남은 검증으로 유지한다. 제품 SOURCE 보수 뒤 고정 후보에서 독립 실행한다.

검증용은 위 잔여 경계와 결정①을 같은 PC task의 한 정본으로 취합해 기존 작업용2에 연결한다. Codex 제품 편집·제품 빌드·실제 앱/UI/Host/입력 조작·삭제·commit/push 없음. 직접 probe/판정서는 저장소 안에 보존했다. history/계획/Git MCP 기록은 같은 구현 작업에서 취합한다. 사용자 현재 조치 없음.
