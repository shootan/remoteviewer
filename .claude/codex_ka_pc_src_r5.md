# KA PC r5 — SOURCE 재검토

**NEEDS_CHANGES — R1-a의 PC 장부 누락은 닫는다. R3의 ACK 판정과 release-only 전환에는 동시 순서가 남는다.** 정상 상태의 카운터·pending 취소 보수는 유지하며, 같은 PC task에서 아래 경계와 새 시험 root를 한 묶음으로 보수한다.

## 고정과 근거

- 제품 후보 **1c321d13206f2611e63e7bf48481539548676bd0**, 비교 **42782c2..1c321d1**. tracked clean·diff --check 정상을 읽기 확인했다. 보고 SHA-256 **2ef61f2acfa373dcf124ca9077286b06123057cb951d7a0e2c902a29d46815d3** 일치.
- 제품 6파일(새 header 포함)과 호출 순서를 검토하고, 새 순수 시험/CMake는 해당 지적 및 출하 hook 경계를 위해 읽었다. **42782c2의 공용 guard merge와 ka-r3 후속 안전 SOURCE 승인은 이번 범위가 아니다.**
- units/IPC/guard/작업용 순수 시험 PASS는 작업용 보고다. Codex는 아래 세 제한 review probe만 실행했다. Host/Viewer/셸/UI·실제 network/input/power/clipboard·금지 prelogin 시험은 실행하지 않았다.

## 닫는 것 — R1-a 및 정적인 R3 보수

ViewerCancelSlot에 hostId를 보존하고 logical_pcs/pc_visible가 replaced를 union으로 세는 변경을 수용한다. 고정 함수와 메모리 registry로 이전 반례를 실행했다.

```
replaced_A_counted=1 visible=1 logical_PCs=3 admit_D=0
```

이전 R1-a를 닫는다. 한 PC의 여러 Viewer가 한 PC로 합산되고, 실제 launch에서 hostId를 채운 뒤 슬롯이 이동하는 것도 확인했다. 기존 R2/R4·두 cancel 신호의 SOURCE 수용은 유지한다. 실제 셸·owner 변경/교체 실패 EVIDENCE까지 확인한 것은 아니다.

R3도 **완료된 실패 교환을 failed로 기록하는 분기**, InputAck를 acked로 기록하는 분기, static pending clipboard/비밀번호 zero·sync OFF·non-release queue 제거를 수용한다. 순차 fixture에서 이전 마지막 교환 실패가 Failed가 되는 것은 원 결함의 일부를 해결한다. 다만 아래 실제 handoff 순서는 그 순차 fixture에 없다.

## F1 (P1) — 카운터/큐/in-flight를 서로 다른 순간에 읽어 조기 Confirmed

viewer_window_proc.cpp의 timer는 다음 인자들을 **각각 읽어** fence_release_verdict에 넘긴다.

```
snapshot(inputExchanges), inputQueue.Empty(), inputInFlight.load()
```

sender는 inFlight를 올리고 **take_release_action으로 dequeue한 뒤에야 taken을 증가**시킨다. 이들 전체에 하나의 상태/lock 경계가 없다. 허용 순서:

1. timer가 inFlight=false를 읽는다.
2. sender가 inFlight=true → 마지막 release dequeue. **taken++ 직전 정지**.
3. timer가 queue empty와 이전 taken=acked를 읽는다. 읽어 둔 false를 함께 넘긴다.
4. verdict는 **Confirmed**. UDP도 아직 요청을 queue하기 전이라 TxPending=false일 수 있다. 실제 마지막 release는 아직 송신/ACK되지 않았다.

MSVC 14.44의 고정 timer 호출식·snapshot/verdict 함수에 위 스케줄 전환만 넣은 메모리 probe:

```
verdict_confirmed=1 sender_in_flight=1 sender_dequeued_last=1 app_acks=0
```

atomic 각각을 seq_cst로 읽어도 여러 값이 같은 순간의 상태가 되는 것은 아니다. 인자 평가 순서에 기대는 것도 해결이 아니다. **fence의 필수 release 집합/완료 상태와 queue→in-flight 소유권 이전을 하나의 일관된 경계로 판정**한다. 예를 들어 sender가 관리하는 per-fence 완료 snapshot을 lock/세대로 게시하거나, release watermark/정확한 ACK 완료를 고정해 UI가 일관되게 읽도록 한다. queue empty·별도 bool·누적 차이를 조합해 빈틈을 다시 만들지 않는다.

snapshot()의 taken/acked/failed도 개별 load라 한 snapshot 안에서 서로 다른 교환 시점을 담을 수 있다. **baseline snapshot의 일관성도 함께** 다룬다. dequeue 직전/직후·taken 게시 전·ACK/failed 게시 전후에 timer를 끼우는 반례를 실제 공유 상태 코드로 검증한다. 네트워크 실패는 계속 NOT confirmed이며 budget도 유지한다.

## F2 (P1) — loop 시작에서만 fence를 검사해 아직 시작하지 않은 일반 송신이 빠져나감

control thread는 loop 상단의 ownerFenceAtMs 검사에서만 release-only 분기로 전환한다. 이미 일반 분기로 들어간 뒤 UI가 fence를 올리면 **해당 반복의 scheduler/credential/clipboard 송신 전에는 fence 재검증이나 동기화가 없다.**

특히 pending clipboard를 control thread가 로컬 outText로 옮긴 뒤, send_clipboard_update 전에 UI가 owner_fence_cancel_pending을 수행하면 공유 pending은 지워져도 로컬 문자열은 남는다. 그 뒤 아직 시작하지 않은 clipboard wire 교환이 시작된다. **고정 r5 pump_clipboard_sync와 cancel helper 원문**을 사용하고, 전송 직전 clock 호출에 UI 스케줄 전환을 넣은 기록 fixture:

```
fence_before_wire=1 sync_enabled=0 pending_shared=0
post_fence_clipboard_writes=1 result=1
```

이는 이미 시작된 wire 교환을 취소하라는 요구가 아니다. **fence가 확정되기 전/후와 최종 송신 시작을 중재**하고, sender가 이미 보유한 로컬 비release 작업도 권한을 잃으면 폐기/zero하도록 한다. password를 로컬로 가져간 경우·이미 뽑힌 pick/일반 action에도 같은 규칙을 연결한다. 단순 loop 상단 또는 최종 직전의 무동기 if만 추가하면 check→send 경합은 남는다. link/owner 경계에서 실제 송신 허용과 fence 확정을 일관되게 처리한다.

다음 호출 순서도 같은 보수에 포함한다.

- **workerState==Resuming 분기는 fence 검사보다 앞**이며 resume hooks.cancelled는 running만 본다. 이미 resuming이던 loop는 fence 뒤에도 proveChannel/ping/rekey를 계속할 수 있다. “fence 뒤에는 release-only·실패 시 resume 안 함” 계약을 이 기존 분기에도 적용한다.
- UI는 **clipImage.Stop()/fileCopy.Stop()의 worker join 뒤에야** ownerFenceAtMs와 input gate를 게시한다. Stop은 각 worker join을 포함하며 여기에는 2초 drain budget이 적용되지 않는다. 효과 차단/송신 권한 취소를 먼저 확정하고, 시간이 걸릴 수 있는 회수는 UI에서 무제한 기다리지 않도록 한다. 기존 세션 teardown·소유 worker 정리는 보존한다.
- **take_release_action은 release 종류를 검사하지 않고 InputEvent/InputText/PhysicalKey를 모두 매핑한다.** release-only라는 전제를 helper 자체/공유 predicate로 보장하고 기존 host-role/prelogin 송신 allowlist도 유지한다. cleanup 시점의 queue가 한 번 필터됐다는 사실만으로 전송 경계의 역할 검증을 생략하지 않는다. 사용자 PC에서 허용되지 않는 입력을 보내 이 경계를 시험하지 않는다.

제공된 순수 시험은 공유 pending을 취소한 **뒤** sender를 호출하는 순서만 확인한다. control이 로컬로 가져간 상태→fence→새 송신 시작, 일반 분기 진입→fence→scheduler, resuming 중 fence를 기록 transport로 강제하고 **비release 새 교환 0, 허용된 release/확인만 수행**을 검사한다.

## T1 — 새 shell logic 시험의 root 검사는 substring만으로는 부족

ka_pc_shell_logic_test.cpp는 `REMOTE60_TEST_TMP` 문자열에 **\\.claude\\test-tmp\\**가 들어 있는지만 검사하고 CreateDirectory/gShellTestDataDir에 사용한다. 다른 드라이브/저장소 밖의 같은 이름 경로, `..`, junction을 통과시키며 기존 shelldata를 재사용할 수 있다. 오류/반환 길이도 충분히 검증하지 않는다.

이 시험이 실제로 저장소 밖에 썼다고 확인한 것은 아니다. 그러나 shared test root 규칙을 만족하는 guard가 아니므로 **정확한 D:/remote/remote/.claude/test-tmp 경계·실 폴더/비reparse·신규 소유 run·생성 결과 확인**을 기존 공용 방식으로 연결한다. 바깥 경로는 보존하고 임의 cleanup하지 않는다. 순수 장부 검사를 하는 데 새 전역 파일/기존 데이터가 필요하지 않도록 한다. Codex는 이 shell logic exe를 실행하지 않았다.

## 출하 hook 경계 — 수용한 범위와 시험 의미

- 고정 CMake에서 **REMOTE60_VIEWER_KA_TEST_SEAM=1은 remote60_viewer_ka_test에 PRIVATE**로만 추가된다. FAIL_RELEASE_AFTER_FENCE와 HOLD_SEND_AFTER_FENCE 코드는 해당 #if 안이다.
- 생성된 **remote60_native_video_client_poc.vcxproj에는 seam 정의가 없고**, viewer_ka_test 프로젝트에는 있다. 제공된 Release/GNLinkViewer.exe SHA-256 **cc5e426245d6c5cb0f97acdfc54d82f293cf9ade49937c9639f32268ec92ba78**은 보고 앞자리와 일치하며, 두 env marker는 **ASCII/UTF-16 모두 0건**이었다. 실행하지 않고 읽기 확인했다. 이 source/제공 artifact의 hook 배제는 수용한다. 최종 통합 출하 artifact gate는 별개다.
- 현재 FAIL_RELEASE_AFTER_FENCE hook은 **execute_control_action을 실제 수행한 뒤 releaseOk=false로 덮는다.** 실제 write 거부/Host ACK 부재를 만든 시험이 아니다. 실패 결과 처리의 부정 대조로 쓸 수 있지만 “release가 Host에 안 갔다/ACK가 없었다”는 근거로 보고하지 않는다. F1처럼 exchange 시작 전/dequeue handoff·진짜 reply 부재를 검사하려면 무효과 transport 대역으로 해당 조건을 따로 만든다.

## Codex 제한 직접 확인

probe 원문·고정 source snapshot은 `.claude/codex_ka_pc_review/` 안에 보존했다. 실제 Host/Viewer/network/input/power/clipboard API는 **0**이며, 파일/registry/recent는 메모리 fixture다. F1/F2의 스케줄 seam은 원문 로직을 바꾸지 않고 해당 read/clock 뒤에 일어날 수 있는 상대 thread 진행을 모델링한다. 전체 앱이나 실제 멀티스레드 UI 실행으로 확대하지 않는다.

| probe | 결과 | exe SHA-256 |
|---|---|---|
| capacity_r5_probe | 실제 변경 용량 helper + fixture: replaced A 포함·D 거부, 이전 반례 닫힘 | d0802e5b07378f3fbf61ad39e299585281ef3687e6feb119af696dd7023d71d1 |
| fence_r5_race_probe | 고정 snapshot/verdict/timer 호출식: ACK 0·sender in-flight인데 Confirmed 반례 | c23331dafe848f83d02ade49ca8132f6509d532c40dc18da9dbe3b8a88fc14df |
| clipboard_r5_handoff_probe | 고정 pump/cancel 함수: 공유 pending 제거·sync OFF 뒤 새 clipboard 전송 요청 반례 | 3c0d3c0d1df7f36d4919d9c9c7e4e26063bb9d53c25838b481ba5b0df5f9f617 |

검증용은 **F1·F2와 T1**을 기존 작업용2의 같은 PC task 정본으로 묶어 연결한다. 닫은 R1-a/R2/R4/결정①의 재설계나 무관 suite 전량 재실행을 요구하지 않는다. 공용 안전 guard는 기존 작업용1 고정 SOURCE와 별도로 이어 받고 최종 통합 후보의 시험에서 함께 확인한다. 제품 편집·전체 제품 빌드·운영/설치·삭제·commit/push는 하지 않았다. 사용자 현재 조치 없음.
