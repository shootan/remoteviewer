# keepalive-r2 e — APK X 경계 r3 (Codex apk_xfence_src_r2 N1)

task=keepalive-r2(e, APK X 경계) / rev=r3 결과 / 50340ce 위

## 고정
- commit **7c5d027de764044b750d72b0114fe58242258b11** (feat-apk-0233, 부모 50340ce). tracked clean, `git diff --check` 정상.
- 2파일: `client_mouse_xbutton_fence.hpp` (ledger), 시험 1개.
- controller cpp/hpp·shared core·JNI 는 0줄 변경. 실 Host·AVD·NDK 실행 0.

## N1 보수 — owed 소유권을 독립 기록으로
- 버튼마다 기록 넷을 둔다:
  - 입구: `entryHeld_`(허용된 down 의 up 이 아직 없음), `entryRefused_`(거부된 down 의 up 이 아직 없음)
  - 송신단: `dropped_`(송신 직전에 폐기된 down 의 up 이 아직 없음)
  - **`owed_`(Host 가 받은 down: 송신 + ACK, 그 up 은 아직)**
- `owed_` 는 `OnSentAction` 에서만 바뀐다. 실행 뒤 ACK 까지 끝난 down 에서 켜지고, 실행 뒤 ACK 까지 끝난 up 에서만 꺼진다. 이후 같은 버튼 down 의 입구 거부나 송신 폐기는 `owed_` 를 바꾸지 않는다.
- up 처리:
  - 그 버튼이 held 또는 owed 이면: Host 가 X 를 받을 때만 통과한다. **no-X 로 보내는 예외는 없다.**
  - 그렇지 않고 거부/폐기된 press 의 up 이면: 막는다.
  - 둘 다 아니면: capability 로 판정한다.
- held bit mask: 거부/폐기된 press 의 X bit 는 **그 버튼이 held/owed 가 아닐 때만** 숨긴다. Host 가 실제로 누르고 있는 bit 는 사실이므로 남긴다.
- 그대로 둔 것: 연결 Reset, park 의 held 회계(`ClientHeldInput`), unknown→X 차단, steady no-X 차단.

## 시험 (fake wire + ledger 단독)
`remote60_client_session_xbutton_fence_test` **67/67, exit 0, RESULT ALL PASS, 6회 반복**. r2 의 43개에 24개를 더했다.

- wire, 두 경로 × {X 복귀 뒤 up, no-X 인 채 up}:
  - **입구 경로**: X Host 에 X1 down 송신 → no-X Pong → 추가 X1 down 입구 거부 → left click 송신.
    - X 복귀 뒤 up: **X1 up 정확히 1**(X1 down 0).
    - no-X 인 채 up: up 거부, **X1 up 0**.
  - **송신 경로**: X1 down 송신 → Ping 보류 중 추가 X1 down 입구 통과 → no-X Pong 으로 송신 폐기 → left click.
    - X 복귀 뒤 up: **X1 up 정확히 1**.
    - no-X 인 채 up: **X1 up 0**.
  - X 복귀 case 의 이어지는 확인:
    - 빚 소멸: 이어서 park 가 Parked 에 도달한다(parkReleased=0, 추가 X up 0).
    - 독립성: X2 press down/up 은 정상으로 나간다.
- ledger 단독:
  - 두 경로 각각, X 복귀 뒤 up 이 입구와 송신단을 모두 통과하고 ACK 뒤 빚이 0 이 된다. owed 동안의 move 는 X1 bit 를 유지한다.
  - no-X 인 채 up 은 두 경계에서 모두 거부되고 빚은 유지된다.
- **보수 전 FAIL → 보수 후 PASS**: 같은 시험을 50340ce 의 ledger 헤더로 빌드했다. 그 헤더 해시 F7E825E7… 는 r2 측정값과 같다. 결과 **60 PASS / 7 FAIL, exit 1**:
  - ledger r3 3건
  - entry X 복귀: up 0건 / park release 1
  - send X 복귀: up 0건 / park release 1
  - 복원 후 헤더 해시 일치, 재빌드 67/67.
- 회귀: controller 를 컴파일하는 21 target 재빌드 오류 0. role_park 13/13, prelogin_select 60/60.

## X2 문구 정정
cap 이 줄었다고 세션이 **즉시** 끝나는 것은 아니다. 끝나는 것은 **owed X 를 포함한 park/drain 을 완료하지 못했을 때**(Failed "park not confirmed")다. 시험이 확인한 것도 이 경로다. cap 이 돌아오면 owed up 은 정상으로 나간다(N1).

## 미검증
- NDK/JNI/4 ABI 빌드, 실 Host·AVD 는 실행하지 않았다.
- ka-pc cd9937a 병합 상태(큐가 X up 을 보존)에서의 재확인은 R2 통합 때 한다.
- 실제 APK UI 의 오류 안내와 재연결 정책은 이 fake 시험의 범위 밖이다.

## 사고 기록
보수 전 대조를 준비하던 PowerShell 한 줄에 쓸모없는 `Out-File` 이 들어가, 저장소 밖에 `C:\Users\shota\nul_placeholder` (6897 B, 50340ce 헤더 사본)가 생겼다. 저장소 밖이라 직접 지우지 않았고, `.claude/deletion_candidates.md` 에 후보로 올렸다. 비밀 정보는 없다.
