# rel-0163 r6 — 최종 채택 잠금·revealed op SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 t-bpwzezfg / 제품 **5ce3ce744d103df4ff6f15fae54aa7f91335534b** / diff **7d82c86..5ce3ce7** / 시험384b377 참고.

**이번 두 보수는 SOURCE OK. 전체는 R3의 frame token 취득 한 곳이 남아 NEEDS_CHANGES다.** 이 취득 조건은 r5 판정 말미의 원 op/frameLocal 일관성 조건이며 새 기능 요구가 아니다. 제품·시험 diff와 직접 lock 호출부를 읽고 tracked clean/whitespace rc0를 확인했다. Codex의 새 빌드·실행 없음. PS37/UA28/gate·M4/M5 결과는 작업용 인용이며 독립 EVIDENCE는 아니다.

## 닫은 두 곳

- **검사→write 잠금 수명 OK**: 제품 CommitFrame 호출은 FrameBuffer mutex를 먼저 잡고 non-null unique_lock 출력에 gate mutex를 넘겨받는다. selHeld는 실제 bytes/surface/version 기록까지 살아 있고 같은 블록 끝에서 FrameBuffer mutex보다 먼저 해제된다. Begin/Clear가 permission과 write 사이에 완료되는 틈은 닫혔다. 제품 호출 한 곳이 이 보관 경로를 사용하는 것도 확인했다.
- **취소/성공 구분 OK**: CommitReveal이 실제 성공한 epoch를 revealedOp에 기록한다. pending 종료 후 예외는 revealedOp==frameOp와 current epoch를 요구하므로 이전 R(gen7)의 active 값만으로 취소된 새 A를 허용하지 않는다. 기존 generation admission과 함께 실제 성공 op의 후속 frame만 이어가는 방향을 수용한다.

## 잠금 순서 검토

검토한 제품 경로에서 **FrameBuffer→selection gate**의 역순을 발견하지 않았다.

- gate의 Begin/Clear/Convert/Bind/CommitReveal 등은 FrameBuffer를 잡지 않고 짧은 상태 처리만 한다.
- ApplyHostAck의 gate→panel callback은 WindowPanelStateModel의 model mutex/값 갱신만 사용하며 FrameBuffer·decode·UI 호출로 들어가지 않는다. picker는 panel Snapshot/RequestSelect를 반환받은 뒤 gate를 잡는다(동시에 panel을 보유한 채 gate 대기하지 않음).
- renderer의 FrameBuffer snapshot 구역은 gate를 요청하지 않는다. receiver의 AckedSelectionPending/RecordReveal/paint 요청은 위 채택 블록이 끝난 뒤다.
- testAfterFramePermit는 제품 기본 empty이며, 시험에서는 gate/FrameBuffer를 소유한 receive thread가 그 안에서 locking gate 함수를 다시 호출하지 않고 다른 thread의 대기를 관찰한다. 최종 출하에서 runtime/env 연결0은 기존 hook 감사에 포함한다.

이는 소스 lock-order 감사이며 모든 GPU/OS 상황에서 deadlock이 없다는 실기 보증은 아니다. 이번 수정 자체의 FrameBuffer↔gate 역전 지적은 없다.

## 유일한 잔여 — epoch와 frameLocal을 따로 취득하면 검사 우회 가능

receiver 시작부는 아직 다음 두 값을 서로 다른 시점에 정한다.

- frameOp = dec.recvSelectionEpoch (앞선 EpochChanged/AdmitGeneration 때의 op)
- frameLocal = ctx.sel.IsLiveLocal(frameOp) (그 뒤 live 상태를 다시 조회)

그 사이 Clear/새 Begin이 들어오면 **원래 local A로 admission한 frame의 frameLocal이 false**가 된다. 최종 CommitFrame은 frameLocal=false이면 op 검사 없이 ordinary permit을 돌려주므로, 새로 완성한 write 잠금/취소 예외를 통째로 우회한다.

결정적인 순서: A BeginLocal→receiver가 A epoch를 기록하고 A의 generation admission 통과→A Clear/B BeginLocal→IsLiveLocal(A)==false→CommitFrame(A,false,...)==true. A는 B가 시작된 뒤에도 기록된다. 현재 CommitFrame 전/후 hook은 frameLocal을 이미 취득한 뒤라 이 순서를 고정하지 않는다.

**최소 보완:** frame의 selection epoch와 local mode를 **같은 gate 잠금 아래 한 snapshot/ticket으로 먼저 취득**하고, 그 ticket의 epoch로 decoder reset/처리를 진행한다. 같은 ticket의 mode/op를 최종 CommitFrame과 reveal까지 보존한다. 이미 취득한 옛 local frame을 나중의 live 조회 실패 때문에 ordinary로 재분류하지 않는다. 새 protocol·capture 변경·긴 decode 중 잠금은 필요 없다. 하나의 selector snapshot API와 해당 호출부/제한 반례로 해결할 범위다.

반례는 token 취득 경계의 취소/B 시작을 고정한다. snapshot이 A보다 먼저/나중 어느 쪽에 선형화되든 **A token이면 A의 취소 뒤 write0**, B token이면 B의 epoch/reset·own key 경로여야 한다. op=A/mode=ordinary 같은 혼합 ticket은 생성되지 않아야 한다. cancellation만 있고 새 B가 없는 경우도 옛 A를 ordinary로 허용하지 않는다. 현재 정상 ordinary 스트림과 실제 reveal된 같은 op 후속 frame은 유지한다.

## 다음 담당·검증 범위

기존 t-bpwzezfg에서 **R3 token snapshot 한 곳만** 보수한다. 이번에 닫은 write lock/revealedOp, R1/R2/R4와 LK/capture SOURCE 판정은 유지한다. 새 제한 대조만 보강하고 PS/UA/LK 전량을 다시 반복할 필요 없다. 제품 commit 고정 후 SOURCE 재확인→재빌드·재 dry-run1회/fresh hash→검증용 독립 E1→Codex EVIDENCE→기존 승인 서명·게시·외부 검증·일반push 흐름은 그대로다.

현재 SOURCE/게시 최종 GO는 아니며 사용자 조치는 없다. Codex 산출물은 미추적 판정서·인계만이고 제품/공개 문서 편집·빌드·설치·commit/push 없음. 같은 task에서 history/계획/Git MCP 커밋을 취합한다.
