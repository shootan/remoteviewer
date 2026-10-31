# rel-0163 r4 — picker SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 t-bpwzezfg / 제품 **501f7061b95f19d9b86e633610b214f6329ccf1e** / diff **5729880..501f706** / 시험 **8919ec2dc95eaa233f53301c00f8719220ce41a9** 참고.

**R1 lost wakeup과 R2 요청 원래 tag 전달은 닫는다. SOURCE 최종은 NEEDS_CHANGES이며, 아래 R1-run/R3-채택/R4-panel 잔여만 보수한다.** 요청 제품 diff와 시험 변경·호출부를 읽고 rel tracked clean/whitespace rc0를 확인했다. 새 Codex 빌드/행동 시험 없음. window proc51/gate·PS E1 33(280/278ms)/UA E1 28은 작업용 인용이며 고정 후보 독립 EVIDENCE로 바꾸지 않는다.

## 닫은 부분

- seq_cst의 등록 뒤 role 재확인/role 게시 뒤 등록 확인, exchange로1회 실행은 첫 pong 사이에 클릭을 잃던 R1 순서를 막는다. 기존 testAfterRoleRead를 사용한 경계 시험 방향도 맞다.
- RequestSelect에 예상되는 Begin epoch를 selectionTag로 싣고 action.selectionTag를 kMsgUnsendableSelect에 전달한다. UI만 Begin을 수행하는 현재 호출 관계에서 원 요청의 op를 보존하므로 R2의 live epoch 재부착은 해결됐다.
- gate의 Begin/Clear/Convert/Bind/Commit에 mutex를 사용하고 BindLocalGeneration이 원 frameOp를 확인한다. header flag만으로 즉시 bind하던 부분도 FrameGate/decode 뒤로 옮겼다. 이전 ABA의 **bind 자체**와 현 epoch로 이름 붙여 reveal하던 부분은 보완됐다.
- ApplyAck의 IgnoredLocal 및 bound/unbound 성공·실패 ack 무간섭으로 시험 기대를 고친 방향은 수용한다. 아래는 gate 바깥 panel commit의 남은 위치다.

## R1-run 잔여 — deferredRun을 role read 뒤 live 값으로 붙임

begin_pc_target_selection은 연결/role을 확인한 뒤 testAfterRoleRead 구간을 지나 **그때 현재 controlRunGen**을 deferredRun에 저장한다. 클릭이 A run에서 시작됐는데 그 사이 reconnect B가 되면 A intent에 B run을 붙인다. run_deferred_pc_selection의 비교도 B==B라 통과한다. 이는 옛 deferredRun을 미리 넣은 시험과 다르다. 그 시험은 저장 후 mismatch만 검사한다.

최소 보완: 클릭/intent 시작의 run을 처음부터 캡처하고, 등록·즉시 재확인 실행 및 deferred 실행 때 그 run이 여전히 current임을 검증한다. 옛 intent를 새 live run으로 바꾸어 저장하지 않는다. 기존 role handshake는 유지한다. 반례는 role read와 hold 사이에 run A→B를 넣어 옛 클릭의 stream-on/Begin0, B에서 새로 누른 클릭은1회 정상 실행이다.

## R3-채택 잔여 — live 검사와 FrameBuffer commit은 아직 분리

receiver는 decode 뒤 IsLiveLocal(frameOp)를 확인하지만, 그 잠금을 놓은 뒤 출력 검사/복사 등을 거쳐 **다른 mutex인 frameBuf.frame.mu에서 실제 frame을 쓴다**. 이 사이 A 취소→B BeginLocal이면 A frame이 B 시작 뒤에도 FrameBuffer에 채택된다. frameOp로 post하므로 B의 reveal을 직접 완료시키는 이전 문제는 줄었지만, 원래 요구한 '현재 op의 결과만 채택'은 아직 검사→commit 사이를 막지 못한다. 조건도 current localAckless&&pending일 때만 검사하므로 Clear/종료 후에는 그 검사를 통째로 건너뛴다.

또 BindLocalGeneration은 outFrames가 비어 있지 않다는 것만 보고 실행되며, 뒤쪽 decoded.bytes.empty()&& !decoded.surfaceTexture 및 payload 복사 실패 검사보다 앞이다. 유효 그림이 없는 반환도 generation을 먼저 고정할 수 있다.

최소 보완: frame 처리에 소유시킨 op/run token과 local mode 여부를 끝까지 유지하고, **유효 출력의 generation bind·실제 FrameBuffer 채택을 동일 permission/commit 경계**에서 원 op가 live일 때만 수행한다. 현재 상태가 local이 아니라고 옛 local frame을 일반 frame으로 재해석하지 않는다. reveal에는 그 채택 결과의 token/gen만 전달한다. 긴 decode/GPU 작업 전체를 gate mutex로 감싸라는 요청은 아니다. 짧은 최종 채택/상태 검증을 함께 직렬화하면 된다.

제한 반례: A의 기존 live 검사 뒤 최종 채택 직전에 hold→A Clear/B 시작(또는 disconnect)→A 해제에서 A frame의 FrameBuffer version/bytes 변경0·B bind/reveal0, B 자신의 유효 출력은 정상 채택. 빈/무효 출력 뒤 generation bind0도 확인한다. 기존 gate 단위의 Bind(A) 거부만으로 최종 publish 경계까지 입증했다고 하지 않는다.

## R4-panel 잔여 — IgnoredLocal 전에 panel이 이미 변경될 수 있음

apply_window_selected_result의 local 확인은 mutex 바깥이고, 이후 **windowPanel.ApplyWindowSelected가 먼저 상태를 바꾼 뒤** sel.ApplyAck가 잠근다. 순서가 control local=false 확인→UI BeginLocal/Convert→panel 변경→ApplyAck IgnoredLocal이면, gate는 무간섭이지만 panel의 선택·title·status·selectionLocked 등은 이미 옛 응답으로 바뀐다. 코드의 raced-to-local 후 nothing이라는 설명과 달리 panel 변경은 되돌리지 않는다.

최소 보완: ack의 현재 op/mode 허용과 **gate/panel 적용 permission을 같은 경계**로 직렬화한다. local이 시작/전환된 뒤에는 옛 success/failure ack가 panel까지 변경0이어야 한다. ApplyAck를 먼저 호출하는 단순 순서 교환만으로 새 검사→panel commit 틈을 만들지 않는다. 기존 normal ack 경로는 유지한다. 반례는 첫 local 검사 뒤 hold→BeginLocal 또는 Convert→옛 ack 재개에서 panel snapshot·gate·stream 모두 불변, 정상 일반 op 자기 ack는 여전히 적용이다.

## 다음 담당·범위

검증용이 기존 **t-bpwzezfg**에 이 세 잔여만 취합해 같은 담당에게 연결한다. R1 lost wakeup·R2 원 tag·allowlist/Prelogin 분기·실renderer 배선과 이전 LK/capture SOURCE OK는 다시 열지 않는다. 세 지점의 제한 latch/부정 대조에 집중하고 매번 PS/UA/LK 전량을 반복하지 않는다.

새 SOURCE 고정 뒤 재빌드·재 dry-run1회·새 payload/hash·독립 E1로 잇는 계획을 유지한다. 지금 candidate의 SOURCE OK/서명·실게시/main push GO는 아니다. APK는 같은 ack 대기 확인을 기존 APK0.2.32 단계에서 보수하며 PC 우선/단일 native writer를 유지한다.

Codex는 미추적 판정서/인계만 작성했으며 제품·공개 문서 편집/빌드/설치/commit/push 없음. 같은 task에서 history/계획/Git MCP 커밋을 취합한다. 지금 사용자 조치는 없다.
