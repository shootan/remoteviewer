# rel-0163 r5 — picker SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 t-bpwzezfg / 제품 **7d82c86cc6e98019a5e667d6d181c4f11e56f2a4** / diff **501f706..7d82c86** / 시험 **f92e4b6fd044fba6fa2eee23471932b5e6d09755** 참고.

**R1-run과 R4-panel은 SOURCE OK. 남은 것은 R3의 실제 frame commit 경계이며 최종 SOURCE는 NEEDS_CHANGES다.** 요청 diff·시험과 직접 호출부를 읽고 rel tracked clean/whitespace rc0를 확인했다. Codex의 새 빌드·행동 실행은 없다. PS35/UA28 및 window proc55·변이 결과는 작업용 인용이다.

## 닫은 부분

- 클릭이 시작될 때 runAtPick을 먼저 잡고 deferredRun에 그 값을 기록한다. 즉시 실행도 같은 run일 때만 하며 deferred 실행의 기존 run 비교도 유지한다. R1-run의 role read→재접속→옛 intent에 새 run 부착 지적은 닫힌다.
- ApplyHostAck가 gate mutex 아래 local 여부를 확인하고 **panel callback과 gate 적용을 그 구역 안에서** 수행한다. local이면 callback 자체를 부르지 않는다. R4-panel의 precheck→새 local 시작→옛 panel 적용 틈은 닫힌다. 일반 ack 대조도 유지한다.
- frame의 op와 local 여부를 저장하고 유효 payload 검사 뒤 CommitFrame에서 bind하려는 방향, reveal에 그 반환 gen과 원 frameOp를 쓰는 부분은 수용한다. 다만 아래 두 곳 때문에 실제 commit 보장은 아직 완성되지 않았다.

## R3 잔여 1 — CommitFrame은 허가만 반환하고 잠금을 먼저 푼다

viewer_video_receiver_frame.cpp는 frameBuf.frame.mu를 잡고 sel.CommitFrame을 호출한 뒤 FrameBuffer를 쓴다. **CommitFrame의 gate.mu는 함수 return 때 풀린다.** BeginLocal/Clear는 gate.mu만 잡으며 frameBuf.frame.mu는 잡지 않는다.

따라서 A의 CommitFrame=true 반환(검사 통과·gate 잠금 해제) → UI A취소/B BeginLocal 완료 → A가 FrameBuffer bytes/version을 씀 순서가 여전히 가능하다. FrameBuffer mutex 안으로 검사 호출을 옮긴 것만으로 UI selection 전이와 실제 write가 직렬화되지 않는다. 직전 요청의 검사→commit 지점이 그대로 남은 것이다.

**최소 보완:** 유효 frame 준비는 바깥에서 하되, 원 op/mode 검증·필요한 bind와 **실제 FrameBuffer write까지 gate permission 수명을 유지**한다. 이미 이번에 성공적으로 적용한 ApplyHostAck의 callback 형태처럼, 짧은 write callback을 gate 잠금 안에서 수행하거나 같은 효과의 scoped permission을 사용하면 된다. 긴 decode/GPU 작업을 잠그거나 전체 selection 구조를 다시 쓰라는 요청은 아니다. lock 순서는 일관되게 유지한다.

시험은 CommitFrame 호출 **전** 취소뿐 아니라 검사 통과와 실제 write 사이를 겨냥한다. B 전이가 먼저면 A write0, A permission이 먼저면 B 전이는 A write가 끝난 뒤에만 완료되는 두 순서를 확인한다. B가 이미 시작한 뒤 A write가 발생하면 실패다. 현재 PS hook은 CommitFrame 앞에 있어 이 틈을 고정하지 못한다.

## R3 잔여 2 — 취소와 이미 reveal된 op를 active generation만으로 구별

CommitFrame의 예외는 pending=false && activeStreamGeneration!=0 && epoch==opEpoch이면 이미 reveal됐다고 보고 허용한다. 그러나 Clear는 epoch와 activeStreamGeneration을 바꾸지 않는다. **이전 성공 화면이 있는 상태에서 새 선택을 취소한 경우**도 같은 조건이다.

결정적인 단위 순서: 이전 op를 gen7로 reveal→Clear→새 local A Begin(epoch A)→A frame 처리 중 A Clear(취소, active gen7은 그대로)→CommitFrame(A,true,9,9). 현재 코드는 true를 돌려주고 revealGen7을 붙인다. A는 reveal된 적이 없는데 취소된 A의 gen9 frame이 허용된다. inputGeneration이 active generation과 같은지도 검사하지 않는다. 현재 cancel 단위는 active generation이0인 초기 상태만 검사한다.

**최소 보완:** 실제로 reveal/active가 된 op의 identity와 generation을 함께 보관해, pending 종료 후 허용은 **바로 그 성공한 op와 그 generation의 후속 frame**에만 한다. 새 op 취소/종료가 이전 active generation을 근거로 허용되지 않게 한다. 위 순서에서 A 거부, 진짜 reveal된 op의 같은 gen 후속 frame 허용, 다른 gen 거부의 대조를 둔다.

frameLocal을 원 op와 함께 일관되게 취득·보존한다는 기존 조건도 유지한다. 취득을 나누어 Clear가 사이에 끼면 옛 local frame이 ordinary로 재해석되지 않도록 최종 permission에서 원 token을 확인한다. 원 op/실제 active identity와 write permission의 한 경계 안에서 해결할 부분이다.

## 시험 근거 정정·다음 담당

PS E1 저장 원문 RESULT35/0 및 UA28/0을 확인했다. 다만 새 PS (4)의 첫 check는 localFramesRefused 증가를 검사하며, **versionAfter==versionAtFire를 assert하지 않는다**(변수는 출력용이다). B 자신의 후속 frame이 version을 정상 증가시킬 수 있으므로 전체 version을 일정 시간 뒤 비교하는 대신 **옛 A의 write callback이 실행됐는지/선택 전이와 write의 순서**를 제한 대조로 검증한다. 작업용의 version 불변이라는 요약을 이 검증 없이 확정 근거로 쓰지 않는다.

R1-run/R4-panel/R2 원 tag와 이전 LK/capture SOURCE OK는 유지한다. 검증용은 기존 **t-bpwzezfg**에 **R3 commit 수명·취소/성공 구분만** 한 묶음으로 연결한다. 위 limited 대조에 집중하며 window proc/PS/UA/LK 전량 반복은 필요 없다. 새 고정 SOURCE 뒤 재빌드/dry-run1회·fresh hash·검증용 독립 E1→Codex EVIDENCE→기존 승인된 서명·게시·외부 확인·일반push 흐름을 유지한다. 현재 서명·게시 GO는 아니다.

Codex 산출물은 미추적 판정서·인계뿐이며 제품/공개 문서 편집·빌드·설치·commit/push 없음. 기록·계획·Git MCP 커밋은 기존 task에 취합한다. 지금 사용자 조치는 없다.
