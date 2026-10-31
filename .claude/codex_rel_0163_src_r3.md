# rel-0163 r3 — prelogin picker SOURCE 검토

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 t-bpwzezfg / 제품 **57298805dd782b0e246315c7258f5f986cb23551** / 검토 diff **b8326ec..5729880** / 시험 **9b836e2ce5902774452f7473b57a17932ddfc6c0** 참고.

**SOURCE NEEDS_CHANGES. 로컬 desktop 선택 방향과 실제 renderer에서 막힘을 푼 부분은 수용하나, 아래 R1~R4의 비동기 소유/완료 경계가 남았다.** 기존 task의 한 보수 묶음으로 처리한다. 제품 diff9파일·관련 control/receiver/gate 호출부·새 gate 시험과 E1 보고/저장 결과를 읽었다. rel tracked clean·diff whitespace rc0 확인. 새 Codex 빌드/실행은 없다. PS E1 33/33·275/279ms·갱신 수치는 작업용 인용이며 독립 EVIDENCE는 다음 고정 후보에서 검증용이 확인한다.

## 수용한 부분

- 진입 분기는 roleKnown+kCaptureFlagPreloginV1+desktop0이다. UAC/LockCred만으로 발동하지 않고 windowId≠0은 거부한다. Host allowlist/입력·credential 경계를 확대하지 않았다. 일반 Host의 기존 선택 경로도 남아 있다.
- 로컬 pending/epoch·첫 key generation 결합을 두고 실제 decode→UI reveal→present/restore 흐름을 사용하는 방향은 맞다. 클릭 즉시 picker를 숨기지 않는다. 제품 renderer E1과 수정 전 FAIL은 유용한 행동 근거다.
- APK shared session에 같은 문제를 확인하고 APK 단계로 분리한 것은 PC 우선 범위와 맞는다. APK까지 고쳐졌다고 주장하지 않는다. 아래는 PC 제품 수정의 잔여이며 새 protocol이나 범용 selection 재설계 요청이 아니다.

## R1 (P1) 첫 pong과 deferred 등록 사이의 lost wakeup

begin_pc_target_selection은 hostRoleKnown=false를 읽은 뒤 deferredPick을 쓴다. handle_pong은 known=true exchange 뒤 **그때 deferredPick이 있을 때만**, 그리고 **첫 pong일 때만** 실행 메시지를 post한다.

순서가 UI known=false 읽기 → control 첫 pong known=true/deferred=0 확인(메시지 없음) → UI deferredPick 저장이면, 이후 pong은 roleWasKnown=true라 메시지를 만들지 않는다. 현재 클릭은 waiting_host_role로 남아 추가 클릭 없이는 실행되지 않는다. 보고의 직렬 역할 전 클릭 시험은 이 사이 순서를 고정하지 않는다.

보완: intent 등록과 role 확정의 handshake를 일관되게 만든다. 등록 뒤 알려진 role을 재확인해 실행을 보장하거나 동일 목적의 원자적/잠금 기반 gate를 쓰며, 양쪽에서 메시지가 생겨도 **해당 intent는1회만** 실행돼야 한다. deferred 메시지/intent에는 current connection/run·operation의 소유를 유지한다. 반례는 위 경계에 latch를 두어 첫 pong을 끼우고, 둘째 클릭 없이1회 실행·중복 실행0·옛 run 메시지 무효를 확인한다.

## R2 (P1/P2) 미송신 A의 통지를 보낼 때 현재 B의 epoch를 붙인다

viewer_control_client.cpp의 kMsgUnsendableSelect post는 **action을 만든 선택의 epoch가 아니라 post 시점 ctx.sel.epoch**를 읽는다. windowId는 action A의 값이고 opEpoch는 새 선택 B의 값이 될 수 있다. UI가 A 취소→B 선택한 뒤 옛 A action이 allowlist에서 막히면, 그 통지가 B를 ConvertToLocal하거나 B를 실패로 Clear한다. resolver가 현재 epoch를 비교해도 잘못 붙인 B epoch가 일치해 방어되지 않는다.

보완: enqueue/선택 시작 때의 connection/run+operation tag를 action까지 전달하고 그 원래 tag로 통지한다. 해당 선택 상태와 role을 UI에서 다시 검증한다. post할 때 live epoch를 새로 읽어 옛 action에 붙이지 않는다. 반례 A action hold→A 취소/B 시작→A blocked 통지에서 B mode·pending·stream 상태 변경0, 현재 op 자신의 desktop 통지는1회 전환/비desktop은 거부를 확인한다.

## R3 (P1/P2) generation bind가 op를 모르며 검증 전 key flag에 선행한다

BindLocalGeneration(streamGeneration)은 LocalUnbound 확인과 expectedGeneration의0→gen CAS만 한다. **원래 selection epoch/run을 인자로 받지 않는다.** A의 key 처리를 hold한 뒤 Clear→새 B BeginLocal(다시 expected0)하면, 옛 A가 B의0을 bind할 수 있다. receiver의 post_pc_selection_reveal도 frame 작업을 시작한 op가 아니라 **post 시점 현재 epoch**를 읽어 옛 frame을 B 결과로 만들 수 있다. BeginLocal 자체도 여러 atomic 필드를 쓰고 epoch를 마지막에 올려 중간 상태를 reader가 볼 수 있다.

또 bind는 FrameGate의 stale/congestion/key 대기 판정과 decode 성공보다 앞에 있다(viewer_video_receiver_frame.cpp:197 부근). 이후 drop/decode 실패할 key-header가 먼저 다른 gen을 고정하면 정상 IDR이 DropWrongGeneration이 될 수 있다. 계약의 정상 IDR/reference sync·출력에 결합한 완료가 아니다.

보완: frame 처리 시작 때 받은 **일관된 selection/run token**을 성공한 IDR/reference sync·유효 출력의 generation 결합부터 raw FrameBuffer 채택/reveal까지 전달하고, 그 **같은 op가 여전히 live인 경우에만** 채택한다. 새 op로 바뀐 결과는 폐기하고 현 epoch로 이름 붙이지 않는다. Begin/Clear/Convert/bind/commit의 관련 상태를 일관되게 관찰하게 한다. 무조건 decoder를 멈추거나 UI thread에서 decode할 필요는 없다. 제한 반례는 A 처리 hold→cancel/B→A 해제 시 B bind/reveal0, 거부/실패 key-header 뒤 정상 IDR은 정상 완료, 새 op의 P-frame 단독 완료0이다. 일반 ack 경로를 일괄 완화하지 않는다.

## R4 (P1/P2) 늦은 Host ack가 local 선택의 generation/실패를 바꾼다

SelectionGate.ApplyAck는 pending만 확인하고 localAckless를 구분하지 않는다. 로컬 op가 pending일 때 옛 WindowSelected가 오면 expectedGeneration을 덮고 Acked/Failed를 반환한다. apply_window_selected_result도 그 전에 windowPanel의 선택 상태를 바꾼다. 잘못된 generation에 묶이거나 옛 실패로 새 로컬 op가 중단될 수 있다.

9b836e2의 새 시험은 'late WindowSelected never completes a local op'라고 쓰면서 **ApplyAck(true,5)==Acked를 기대**한다. 주석과 실제 기대값이 반대이며 이전 결정의 옛 ack 무효 조건을 검증하지 못한다.

보완: current op가 host ack를 기다리는 mode인지와 원래 request/op tag를 확인한 뒤 결과를 적용한다. local mode에는 옛/무관 host ack로 panel/generation/pending/stream을 변경하지 않는다. expectedGen이 아직0이든 이미 실제gen에 bind됐든 성공·실패 옛 ack 모두 무간섭이어야 한다. 시험 기대값도 이를 기준으로 바꾼다. old ack 후 정상 local frame이 완료되고, 정상 일반 선택의 자기 ack는 여전히 작동하는 대조를 둔다.

## 다음 담당·근거/릴리스 경계

검증용이 기존 **t-bpwzezfg**에 R1~R4를 취합해 같은 작업용1에게 연결한다. 현재 올바른 Prelogin0 분기·render 배선·allowlist 정책은 유지하고 위 경계만 보수한다. SOURCE 재확인은 새 고정 diff에 한정한다. 각 경계의 제한 반례를 추가하고, 매 보수마다 PS/UA/LK 전량을 반복하지 않는다.

UA E1 및 빌드 준비 같은 독립 작업은 계속 가능하다. 제품이 다시 바뀌므로 지금 생성 중인 dry-run 산출물을 이후 새 후보의 해시/검증으로 재사용하지 않는다. 새 고정 product 후보→SOURCE→재빌드/dry-run·검증용 독립 E1/관련 회귀→Codex EVIDENCE 뒤 기존 승인 서명·gnlink 게시·외부 확인·main/docs 일반push로 잇는다. 아직 서명·실게시/main push GO가 아니다.

Codex는 미추적 판정서·인계만 작성했고 제품/공개 문서 편집·빌드·설치·commit/push 없음. 구현 기록·계획·Git MCP 커밋은 기존 task에 취합한다. 지금 사용자 조치는 없다.
