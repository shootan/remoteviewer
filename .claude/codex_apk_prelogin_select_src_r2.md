# apk-0232 / ① picker SOURCE r2

2026-10-26 / 검증용 `remote#yj8mrmrx` / 기존 picker 작업 / 입력 `report_apk_prelogin_select_r1.md` r2 전문.

**판정: NEEDS_CHANGES — N1의 최종 poll/취소 경계 한 곳만 남음. N1의 A→B 추적 덮기와 N2의 보류 프레임 queue 보수는 수용한다.**

검토 범위는 제품 **86df4cdcfc9a34092f169d80e310d218833b829b**의 변경과 시험 **1de9dc79996eb0e42c59ab650abb7153578765b0**뿐이다. 두 commit 각각 전체 diff와 Android sink/JNI 연결을 읽고 `git diff --check` rc0를 확인했다. 같은 브랜치의 UA2 r2 cf8c021·41de4cf 및 현재 SH Kotlin WIP는 이 판정에 포함하지 않고 건드리지 않았다.

## N1 잔여 (P2): sink의 마지막 확인 뒤 Abort/Prepare가 panel 완료를 무효화하지 못함

위치: `native_video_client_session.cpp:335-351` (`PollLocalSelection`), `android_video_decoder.cpp:495-546` (Prepare/Abort), `native_bridge.cpp:400-411`.

새 `localSelectMu_`는 controller의 직접 local 시작·queue 전환·poll끼리는 직렬화한다. 그러나 실제 UI 취소/새 Prepare는 JNI에서 **sink만 직접 호출**한다. 이 함수는 sink의 `LockedState`를 잡을 뿐 `localSelectMu_`를 사용하거나 panel의 current tag를 무효화하지 않는다. poll의 `CurrentSelectionTag()`와 `LocalSelectionBoundFor()`도 각각 sink 잠금이 끝나면 반환한다.

가능한 순서:

1. A의 IDR가 정상 queue되어 sink에 A→G가 결합된다. worker poll은 local mutex를 잡고 CurrentSelectionTag()==A, LocalSelectionBoundFor(A)==G까지 읽는다.
2. 그 직후, panel 완료 전 UI가 뒤로 가기 → **nativeAbortVideoSwitch()가 끝난다**. sink gate는 Reset되고 ready=0이지만, controller 추적 tag와 panel currentSelectTag는 A 그대로다. 이 Abort는 local mutex를 거치지 않으므로 지금도 실행 가능하다.
3. worker가 재개해 local tag A를 0으로 CAS하고 `ApplyLocalSelectedFor(A,G)`를 부른다. panel tag가 A라 **취소 후에 window_selected/ACKED가 기록된다**.

새 B의 Prepare가 같은 틈에 끼어도 B의 RequestDesktopMode는 local mutex를 기다릴 수 있다. 즉 B 요청끼리의 직렬화가 **취소/Prepare와 완료의 직렬화**를 대신하지 않는다. 이 경계는 r1 판정의 “poll의 현재성 확인과 panel 완료도 취소·새 Prepare와 최종 소유 경계 확인”에 포함된 잔여다. 이 소스 순서를 실제 APK에서 재현했다고 주장하지 않는다. Kotlin이 pending을 이미 지웠다면 이 ACK 하나만으로 VIEWER에 진입하는 것은 아니지만, 취소한 A를 완료로 표시하지 않는 계약은 여전히 깨진다.

필요 보수는 하나의 최종 소유 경계다. 예를 들어 **취소/Prepare도 같은 controller 트랜잭션을 거쳐 sink·panel·추적을 함께 무효화**하거나, sink의 현재 tag/결합 확인에서 **panel commit까지** 보호하는 짧은 callback/permission 경계를 사용한다. 잠금을 풀고 CurrentSelectionTag를 한 번 더 읽기만 하면 같은 틈이 다시 생긴다. 네트워크·UI message 대기를 그 잠금 안에 넣을 필요는 없다.

기존 순서는 park→local→sink/panel이며, poll은 local을 푼 뒤 session mu를 잡는다. 현재 변경만으로 새 역순을 발견하지는 않았다. 보수하면서 sink→local과 local→sink 양방향 중첩을 만들지 말고, 실제 JNI Prepare/Abort가 같은 경계를 사용하는지 대조한다.

제한 반례: **poll이 G를 얻은 뒤 panel commit 전 latch → 취소 완료 → poll 재개**에서 ACKED/selected가 되면 FAIL. 새 Prepare B가 그 틈에 낀 경우 A는 panel 상태를 새로 완료시키지 않고, B 자신의 IDR/태그로 완료되어야 한다. 직렬 구현에서 이미 진행 중인 commit이 먼저 끝나면 취소는 그 뒤 최종 무효 상태를 보장하는 순서로 검사한다. 지금 P8은 BeginLocal 직후/IDR 전의 다른 latch라 이 반례를 대신하지 않는다. 수정 전 실패·수정 후 통과를 짧게 남기고, 닫힌 부분 전량 재검사는 반복하지 않는다.

## 이번에 닫은 부분

### N1 A의 tag가 B를 덮는 경합: OK

직접 local 시작과 queued local 전환, 추적 게시·poll이 같은 mutex 아래에 있고 Convert 실패 때 추적을 게시하지 않는다. B의 RequestDesktopMode가 같은 경계를 통과하므로 A가 B 게시 이후 그 tracker를 덮는 r1 순서는 막힌다. P8의 취소/새 Prepare 뒤 B 선택·자기 IDR gen910 대조와 무잠금 mutant는 이 부분에 맞는 검사다. 위의 poll/Abort 잔여와 구분한다.

### N2 보류 프레임의 queue 허가·앞지르기: SOURCE OK

- Android sink가 **제품 `DecoderPendingSlot`**의 Offer/Pump를 실제 입력 callback에 연결한다. 새 frame이 이미 보류된 frame을 먼저 queue로 앞지르지 않고, 새 IDR 교체/버린 delta의 rekey를 유지한다.
- `PermitsQueue`는 await 중 거부, 미결합 local의 현재 candidate, 결합 뒤 expected 세대를 실제 queue 직전에 검사한다. Offer/Pump 모두 Android `LockedState` 안에서 callback까지 실행하므로 그 사이 UI Prepare/Abort가 gate를 바꿀 틈이 없다. 검사 직후 callback은 제품 `TryQueueFrameLocked`다.
- bootstrap replay도 같은 generation 허가 검사 후 실제 queue를 호출한다. Prepare/Abort·codec reset에서 held/bootstrap을 정리하는 연결도 유지한다.
- 저장 `slot_base.txt`의10/0과 `slot_N2.txt`의7 FAIL/`200 100` 반례를 직접 읽었다. **실행은 작업용 귀속**이다. 이 시험은 제품 slot+gate를 공유하며 codec만 대역이므로, r1의 Admit 즉시 output으로 보는 Feed보다 정확한 queue 제어 경계 검사다.

따라서 N2 SOURCE를 닫기 위해 **MediaCodec 입력 버퍼 고갈 seam을 새 필수 작업으로 추가하지 않는다**. 실제 입력 고갈/드라이버 오류까지 실행 증명한 것은 아니며 한계는 그대로 기록한다. 최종 통합 APK의 실제 decoder→화면 표시·일반 접속/재선택·SH on/off 독립 AVD 검사는 여전히 필요하다. 드라이버에서 별도 현상이 나오면 그때 가장 작은 native seam을 사용한다.

## 후속

같은 picker 담당에게 **잔여 N1 한 경계만** 보수→해당 diff SOURCE 재확인으로 연결한다. 46/46·10/10·4ABI 빌드·회귀는 작업용 보고이며, Codex 새 빌드/시험 실행은 없다. UA2/SH 미결은 각각의 정본에서 계속 처리하고 새 병렬 writer·PC163 게시 재검토를 만들지 않는다.

최종 고정 APK0.2.32에서 검증용 독립 실제 UI/decoder 행동·gate·산출물 확인 후 EVIDENCE로 잇는다. 사용자 현재 조치 없음. Codex 제품 수정·빌드·실행·commit/push 없음; 미추적 판정서/인계 외 공개 history/계획·Git MCP commit은 같은 구현 작업에 취합한다.
