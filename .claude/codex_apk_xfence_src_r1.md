# APK/shared ClientSessionController X fence — SOURCE r1

**NEEDS_CHANGES — steady-state capability 차단은 수용한다. 첫 Pong/거부 뒤 capability 전환에서 같은 X press의 down/up 및 held bits를 함께 처리해야 한다.** PC cd9937a의 기존 SOURCE OK는 유지하며, 이 판정은 APK 0.2.33 또는 부모 e 변경 전체 승인이 아니다.

## 고정 범위

- 후보 **3ef6ee6848e6301776e011c6b57cd49ffedb3f61**, 부모 **1aecc4851cdbe8676dcd90cbe3a91610894aa9d4**, 변경 5파일 +573. tracked clean·diff --check 정상.
- 보고 SHA-256 **b7e88dd40f39c0184f54471078c330ed9f74c9eb1c036d8b39cb43ce512d6b98** 일치. 새 helper·controller cpp/hpp·CMake·496줄 fake-wire 시험을 읽었다.
- fake wire 23/23×6·mutant M1/M2·role_park 13/13·prelogin_select 60/60은 **작업용 보고**다. Codex는 아래 고정 helper probe만 직접 빌드/실행했다. 실제 ClientSessionController wire 시험·JNI/NDK·Host/AVD는 이번에 실행하지 않았다.

## 수용하는 변경

- `role.known && MouseX capability`를 정확한 조건으로 쓰고 unknown을 지원 없음으로 처리한다. prelogin용 EffectiveHostFlags로 X 지원을 추정하지 않는다. HostRoleGate의 한 Load에서 known/flags를 함께 읽는 것도 맞다.
- QueueInputEvent에서 JNI/매크로 입력을 거부하고, 송신 직전에도 같은 helper로 queue/park-generated action을 재검사한다. **미지원 상태에서 kind 2/3 + keyCode 5/6을 거부, 모든 InputEvent의 X held bits를 legacy mask로 제한**하는 steady-state 변경을 수용한다.
- X 지원 상태에서 기존 event를 통과시키며 kind 5/6의 키보드 이벤트를 mouse X edge로 오인하지 않는다. 새 wire type/size·JNI 시그니처 변경은 없다. 새 header도 Win32 API에 의존하지 않는다.
- 실제 송신 fence를 통과한 뒤에만 heldInput_.OnSent가 호출되는 기존 순서는 보존됐다. 그러나 이것만으로 거부된 X press의 후속 up을 억제하지는 못한다.

## 질문① — 비대칭은 존재함: 거부된 press의 후속 up/held bits

새 helper는 **현재 capability만 보는 stateless predicate**다. controller에 추가된 상태도 held-back counter뿐이다. 그 결과:

1. control link는 연결됐지만 첫 Pong 전(role unknown)에 X1 down이 QueueInputEvent에서 false로 거부된다.
2. Pong이 X 지원을 알린다.
3. 같은 물리 press/매크로 step의 X1 up은 이제 true가 돼 queue·송신 fence를 통과한다. **해당 down은 Host에 안 갔고 up만 간다.** heldInput_.OnSent는 그 뒤의 회계이므로 송신을 막지 않는다.
4. 그 press의 `buttons`에 X bit가 든 macro move도 capability가 true가 된 뒤에는 원형대로 통과한다.

고정 helper 직접 결과:

```
first_Pong_transition down_accepted=0 later_up_accepted=1
later_move_X_bits=8
```

이는 **실제 Host wire/OS 동작 재현이 아니라 고정 helper와 caller 순서의 판정**이다. 실제 controller의 CanQueueControlRequestLocked도 control link/Connected/controlLoopActive를 조건으로 삼고 role known을 요구하지 않는다. 읽은 송신 직전에는 X down 거부 이력에 따른 up 억제 단계가 없다. 따라서 “비대칭 없음” 또는 “첫 Pong 전 시작한 클릭 전체를 버림”으로 승인할 수 없다.

**결정:** 초기 미확정/미지원에서 거부한 X press는 그 up까지 같은 press로 억제한다. 송신되지 않은 X down의 후속 up/held bits를 capability가 생겼다는 이유로 새 효과로 보내지 않는다. 거부됐던 down을 자동 재생하지 않는다.

구현은 controller의 **거부/실제 송신 이력과 연결 세대**를 사용해 다음을 만족시키면 된다.

- 입구에서 거부한 down뿐 아니라 **queue 진입 후 최종 송신 fence에서 폐기한 down**의 대응 up도 처리한다.
- 거부된 해당 press의 X held bits는 move/다른 버튼 event에도 새지 않게 한다. up에서 해당 억제를 끝내고, 이후 새 supported press는 정상 동작한다.
- **이미 Host에 보낸 down의 owed release**와 거부된 새 press를 구분한다. 단순히 “최근 down 하나 거부됐으면 모든 up 폐기”로 바꿔 기존 held X를 남기지 않는다. park/held release는 이 실제 송신 소유권을 사용한다.
- state는 동일 연결/owner 세대와 함께 정리한다. 새 connect에 옛 억제/보유 상태를 넘기지 않는다. 기존 prelogin/park 동기화·capability 차단을 유지한다.

현재 direct/macro/park 시험은 모두 **connect_and_know_role 뒤 고정 flags**로 입력을 보낸다. 순수 action 시험도 각 상태 하나만 검사한다. 23개가 위 전환을 검증한 것은 아니다. fake Host의 Pong phase/barrier로 **첫 Pong 전 down 거부→X Pong→move/up X 0→새 press 정상**, send-time down 폐기 뒤 capability 회복, 기존 held release/park·재연결을 검증한다. 이 대조에 실제 Host나 사용자 X 버튼은 필요 없다.

## capability 감소 시 held release에 대한 보고 한계

고정 helper의 반대 전환은 **X 지원 상태 down 허용→현재 X 미지원 up 거부**다. “held down은 X Host에 보냈으므로 그 release도 반드시 B를 통과한다”는 보고는 **capability가 유지될 때**만 성립한다. 일반적인 capability 감소/role 전환까지 증명한 것은 아니다.

미지원 Host로 X release 예외를 보내 차단을 풀지 않는다. cap이 없어졌을 때 park/drain이 성공으로 오인하지 않고 기존 bounded 실패/teardown 계약으로 끝나는지, held 회계가 새 연결로 넘어가지 않는지를 상태 전환 대조에 포함한다. 실제 OS의 해제 성공은 여전히 별도 미검증이다.

## 질문② — PC 쪽 공유 controller 영향

이 변경은 APK 전용 binary에만 한정되지 않는다. **ClientSessionController를 링크하는 PC의 component/record e2e에도 적용**된다. PC GNLinkViewer 자체의 입력 경로(viewer_input_forward/fence_mouse_for_host)를 이 코드가 대신하는 것은 아니다.

- 기존 ordinary os_input_record_e2e의 **Tab down/up(kind 5/6, VK 9)·move·left down/up(keyCode 1)**은 이 helper에서 통과하고 bits 0..7도 유지된다. 직접 helper probe의 해당 5개 입력은 모두 원형대로 통과했다. text/selection/다른 control action은 이 mouse helper 대상이 아니다.
- 일부 PC component fixture가 X 지원 없는 Pong을 주면서 X 입력을 기대했다면 이제 거부되는 것이 계약상 맞다. X 지원 양성 fixture는 실제 capability flag를 광고하고, unknown/unsupported 양성 대조는 기존 left/move 등을 사용한다. 기대값을 맞추려고 제품 fence를 우회하지 않는다.
- 위 **첫 Pong/거부 이력 전환 문제는 공유 controller 사용자 모두에게 적용**된다. PC consumer라는 이유로 예외를 두지 않는다. 반대로 X를 전혀 보내지 않는 기존 record lane을 이 지적 때문에 전량 보류하지 않는다.
- C++ class에 atomic member가 추가되므로 cpp/hpp·소비 target을 함께 재빌드한다. JNI의 공개 인자/wire layout은 유지하지만 **NDK/JNI/4 ABI 빌드와 실제 app 배선은 미검증**이다.

cd9937a의 shared release 규칙과는 text hunk가 겹치지 않는 것을 확인했다. **의미 결합**은 남는다. 병합 뒤 queue가 보존한 X up이 이 송신 fence를 지나므로 지원/미지원·거부 이력·park/resume까지 같은 통합 후보로 확인한다. cpp/hpp와 CMake의 실제 merge 결과·공용 guard는 기존 통합 절차에서 별도로 고정한다. 무충돌 merge만으로 APK EVIDENCE를 승인하지 않는다.

## 직접 probe·다음 담당

`.claude/codex_test_injection_review/apk_xfence_3ef/`에 고정 원문과 probe를 보존했다. exe SHA-256 **aa4535816a21eebb2ca2a49614203ae6285a5e504afaca09709f8a93d4fbdeed**.

```
first_Pong_transition down_accepted=0 later_up_accepted=1 later_move_X_bits=8
steady_noX refused_edges=4 move_allowed=1 masked_buttons=7
regular_record_inputs_unchanged=5
capability_drop earlier_down_accepted=1 later_up_accepted=0
```

실제 Host/Viewer/AVD/JNI/network/input/power/clipboard API는 0이다. 실제 23개 wire suite를 Codex가 실행했다고 보고하지 않는다. 제품 수정·전체 제품/NDK 빌드·설치·삭제·운영·commit/push는 하지 않았다.

검증용은 이 **한 press의 disposition/세대 처리와 전환 시험**을 기존 작업용1의 APK X fence task에 취합한다. 같은 shared source의 별도 writer를 만들지 않는다. steady-state 차단·기존 PC cd9937a Source 수용은 유지하며, 승인된 일반 record lane과 무관 순수 작업은 계속할 수 있다. 사용자 현재 조치 없음.
