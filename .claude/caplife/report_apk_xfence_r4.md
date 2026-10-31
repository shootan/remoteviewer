# keepalive-r2 e — APK X 경계 r4 (Codex apk_xfence_src_r3 N1)

task=keepalive-r2(e, APK X 경계) / rev=r4 결과 / 7c5d027 위

## 고정
- commit **7f07f1aecfd33f9eb3e588c0ae40fe5008d11b42** (feat-apk-0233, 부모 7c5d027). tracked clean, `git diff --check` 정상.
- 3파일:
  - `client_mouse_xbutton_fence.hpp` (ledger)
  - `native_video_client_session.cpp` :630–633. `QueueInputEvent` 의 enqueue 를 둘로 나눴다(메시지를 만든 뒤 enqueue 하고 `NoteQueued`). 다른 hunk 는 없다.
  - 시험 1개
- shared core·JNI·hpp 는 0줄 변경. 실 Host·AVD·NDK 실행 0.

## N1 보수 — 미송신 down 과 거부된 up 을 함께 취소
- 기록하는 시점:
  - controller 는 입구를 통과해 enqueue 된 X down 의 **queue 순번(`inputEvent.seq`)**을 `NoteQueued` 로 ledger 의 `pending_` 에 남긴다.
  - 입구를 통과한 up 이 enqueue 되면 그 버튼의 `pending_` 을 비운다. 짝(down·up)이 모두 큐에 있으므로 정상 rapid pair 다.
- **up 이 입구에서 거부될 때**(그 시점에 no-X): 그 버튼의 아직 송신되지 않은 down 들을 순번째로 `cancelled_` 로 옮긴다.
- 송신단(`AdmitSend`, 순번 사용):
  - `cancelled_` 에 있는 down 은 cap 이 X 로 돌아와 있어도 **보내지 않는다**. `dropped_` 로 그 press 의 held bit 를 다음 press 까지 숨긴다.
  - sender 가 꺼낸 down 은 `pending_` 에서 빠진다. 그 뒤에 up 이 거부되면 취소하지 않고, ACK 뒤 **owed** 가 된다(r3 계약 그대로: X 가 돌아오면 up 이 나가고, no-X 에는 예외 없음).
  - FIFO 이므로, 보지 못한 더 오래된 순번(park 나 만료로 버려진 것)은 정리해 기록 크기를 제한한다.
- 구분 기준은 버튼 bool 이 아니라 **연결 내 queue 순번**이다. 연결 Reset 이 `pending_` 과 `cancelled_` 를 함께 비운다. "entryHeld 가 false 면 모든 down 폐기" 방식은 쓰지 않았다.

## 시험 (실제 controller + fake Host phase, ledger 단독)
`remote60_client_session_xbutton_fence_test` **75/75, exit 0, RESULT ALL PASS, 6회 반복**. r3 의 67개에 8개를 더했다.

- **phase 대조**(실제 `ClientSessionController`):
  - X Host 에 연결한다. fake Host 가 InputAck 를 20 ms 늦게 보내, key 8개가 sender 를 붙잡게 한다.
  - Ping A 를 보류한 상태에서 다음을 큐에 넣는다: key 8개 → X1 down → move(X1|L).
  - Pong A 는 no-X 로 답한다. 그 50 ms 뒤 Ping 이 queue 입력보다 먼저 나가 **Ping B 가 보류**된다. 이때까지 key 2개만 나갔고 X1 down 은 아직 큐에 있다(`downsSoFar=0`).
  - X1 up 은 입구에서 거부된다. 그 뒤 Pong B 를 X 로 답한다.
  - 결과: **X1 down 0, X1 up 0, X bit 0**. move 는 `buttons=0x1`(X1 숨김)로 나간다.
  - 이어서 park → Parked, release 0 (빚 없음). 다음 X1 quick pair 는 down/up 이 모두 나간다.
- ledger 단독:
  - 큐의 down + 입구 거부 up → 송신에서 취소, bit 숨김, owed 아님
  - quick pair(둘 다 큐) → 온전히 송신, 취소 0
  - sender 가 이미 꺼낸 down(in flight) + 거부 up → owed 유지, 취소 0
- 기존 범위는 모두 그대로 PASS 다: r3 owed 두 경로(입구·송신 × X 복귀/no-X), unknown→X, X→noX, Reset, X2·left 독립, steady 의 23개.
- **보수 전 FAIL → 보수 후 PASS**:
  - 같은 시험을 `NoteQueued` 무동작 mutant(= 7c5d027 의 동작, 큐 down 기록 없음)로 빌드했다. 결과 **72 PASS / 3 FAIL, exit 1**:
    - ledger r4 취소
    - phase 대조: **orphan X1 down 이 wire 에 1개**, X1 up 0, X bit 2
    - park: X1 release 1 (빚)
  - 복원 후 헤더 해시 일치, 재빌드 75/75.
  - 7c5d027 헤더 그대로는 새 API(`NoteQueued`) 때문에 시험이 컴파일되지 않는다. 그래서 동작이 같은 mutant 로 대조했다.
- 회귀: controller 를 컴파일하는 21 target 재빌드 오류 0, ledger 헤더 경고 0. role_park 13/13, prelogin_select 60/60.

## X2 문구
"owed 포함 park/drain 실패 시 종료"는 수용된 그대로다.

## 미검증
- NDK/JNI/4 ABI 빌드, 실 Host·AVD 는 실행하지 않았다.
- ka-pc cd9937a 병합 상태(큐가 X up 보존·만료 예외)에서의 재확인은 R2 통합 때 한다. 병합 뒤에는 큐가 X up 을 더 오래 보존하므로, NoteQueued 의 짝 완성 규칙과 함께 통합 후보에서 다시 본다.
