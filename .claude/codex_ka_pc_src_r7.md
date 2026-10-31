# KA PC r7 — SOURCE 검토와 APK 공유 영향

**PC SOURCE OK — r6의 N1·N2·완료 문구를 닫는다.** 공유 queue 변경은 APK에도 적용되는 의도된 동작 변경이다. 아래 APK 입력 경로의 capability 공백은 별도 통합 항목으로 남기며, **APK 0.2.33 전체 SOURCE/출하 승인은 이번 판정이 아니다.**

## 고정·직접 확인

- 후보 **cd9937a8712b305498e32acaefda0dc2289c955f**, 비교 **e1acbb5..cd9937a**의 8파일. tracked clean·diff --check 정상, 보고 SHA-256 **23fb27708288c6dd23ccc2989ff3f4c897de4bf90f9df8e745fbe0e2efc47440** 일치.
- 변경 제품 6파일과 시험 2파일을 읽고, APK app/smoke CMake·native JNI/macro·ClientSessionController 경로도 확인했다. 제품 소스/브랜치는 수정하지 않았다.
- Codex는 고정 header 의존성 46개와 **실제 ClientInputQueue 메서드·공유 release 함수·입력 builder 원문**을 별도 review translation unit에 보존해 컴파일했다. 실제 gate header·InputMacro.cpp도 사용했다. inner link는 기록 대역이다. 실제 Host/Viewer/network/input/power/clipboard는 실행하지 않았다.

## 닫는 N1 — 하나의 release 규칙에 X1/X2 포함

release_identity가 mouse up **VK 5/6**을 포함하고, 공개 함수 control_input_release_identity로 gate도 동일 규칙을 호출한다. pending queue의 DropAllButReleases와 fence가 새로 enqueue한 X up을 모두 보존한다. X down/text/move를 release로 승격하지 않는다. 262/263의 X identity는 기존 key-up/다른 mouse/physical identity와 충돌하지 않는다. finite release reserve의 기존 상한에도 두 항목이 추가되는 범위다.

고정 실제 queue/gate 직접 결과:

```
shared_queue_and_fence dropped_down=1 kept=3 X_ups=2 left_up=1 confirmed=1
backpressure X_ups=2 others=0 duplicate_X_up_coalesced=1
expiry_after_2s X_ups=2 expired_downs_sent=0
```

PC의 enqueue_input_event/fence_mouse_for_host는 그대로이며 **미지원 Host의 X edge를 거부하고 X held bits를 mask**한다. r7의 지원/미지원 ViewerState 시험은 작업용 실행이며 Codex가 전체 ViewerState 함수를 독립 실행한 것은 아니다. source와 실제 공유 queue/gate 결과를 연결해 N1을 닫는다. 실제 Host OS에서 X 버튼이 해제됐다는 근거는 아니다.

## 닫는 N2 — 정책상 취소와 실제 실패 구분

FenceGatedLink가 첫 byte 이전 정책 거부·message 중간 거부·시험 hook 거부를 기록하고 gate_take_failure가 소비한다. 다음 caller의 결과 매핑을 대조했다.

- 정상 input/action: **NotStarted만 Cancelled**로 정리하고 release drain을 계속한다. release write/ACK 실패를 Cancelled로 바꾸지 않는다.
- clipboard/IME/thumbnail: NotStarted의 경우만 다음 release 처리로 돌아가며, 실제 transport/protocol failure·Broken을 fence 거부로 추정해 계속 쓰지 않는다. begin_control_resume의 fence 거부도 유지된다.
- fenced release: role상 미송신과 실제 실패를 구분하며, hook/transport/ACK 실패는 Failed다.

messageBytes_는 같은 control thread에서 message의 받아들인 Write 크기를 추적하고 EndMessage에서 초기화한다. 전에 받아들인 message 중간의 정책 거부를 첫 byte 전 취소로 바꾸지 않는다. 허가가 fence 전에 난 작업은 기존에 시작한 작업이라는 r6의 논리적 시작 정의를 유지한다.

직접 결과:

```
cancelled_down pending=1 final_confirmed=1 ack=1 cancelled=1 failed=0
mid_message_refusal broken=1
release_hook write_calls=0 hook_refused=1 failed=1
```

기록 link의 ACK 완료는 대역으로 공급한 값이다. 실제 peer InputAck/동시 UI/전송 장애의 독립 EVIDENCE로 확대하지 않는다. 실제 r6 반례의 정상 취소 후에도 required up이 남아 Pending이고, 그 완료 뒤 Confirmed가 되는 정책 경계를 직접 확인한 것이다.

**완료 문구도 수용한다.** tally가 InputAck·role skip·fence cancellation·failure를 구분하고, generic Confirmed를 모두 Host ACK라고 부르지 않는다. “since the fence”의 ACK/취소 완료 수에는 fence 전에 시작해 이후 끝난 in-flight도 포함될 수 있어, 새로 taken된 수와 단순 1:1로 같다고 해석하지 않는다.

## APK 0.2.33 영향 — queue 의미는 수용, capability 경계는 별도 확인/보수

Android **app/src/main/cpp/CMakeLists.txt:8**과 **smoke/CMakeLists.txt:16**이 이 shared_core.cpp를 직접 컴파일한다. 따라서 “PC만 바뀜/시험뿐”으로 기록하면 안 된다. APK에도 다음이 적용된다.

1. **DropAllButReleases**가 X1/X2 up을 보존한다. ClientSessionController의 park/resume 큐 정리에도 영향이 있다.
2. **backpressure**에서 X up은 폐기되지 않고, 같은 X up identity의 중복은 기존 release 규칙대로 합쳐진다.
3. **2초 queue age 처리**에서 X up은 stale 일반 입력으로 버리지 않는다.
4. 기존 ClientHeldInput은 이미 mouse 1/2/4/5/6을 추적·release 생성한다. 이번 공유 queue 수정은 그 release 보존과 일치한다. 기존 left/right/middle/key/physical 의미는 유지한다.

이는 release 보존 결함의 수정이며 **새 wire type/size/capability bit·JNI 시그니처를 추가하지 않는다.** 추가 C++ 함수를 선언/정의한 header/cpp는 함께 통합해야 한다. NDK/4 ABI 링크·APK 배선/실행은 이번에 검증하지 않았다. 통합 고정 source/artifact의 기존 빌드·영향 회귀에서 확인한다.

**다만 APK 경로에 PC의 capability fence가 있다고 판단할 수 없다.** 확인한 코드에서:

- NativeBridge 직접 QueueInputEvent는 **buttons & 0x7**을 하지만 **keyCode 5/6 자체를 거부하지 않는다**.
- InputMacro::LoadSerialized는 mouse kind 1..4의 **keyCode 5/6을 받아들인다**. nativeMacroTick은 해당 step을 ClientSessionController::QueueInputEvent로 바로 넘긴다.
- ClientSessionController::QueueInputEvent와 송신 직전은 prelogin/role gate를 적용하지만, 읽은 경로에는 **kCaptureFlagMouseXButtonsV1에 따른 X edge 거부/mask가 없다**. PC의 fence_mouse_for_host와는 다르다.

고정 InputMacro/role 함수의 메모리 직접 확인:

```
APK_ingress_macro_Xup loaded=1 key=5 normal_role_allows=1
```

이는 **APK 또는 구 Host에 실제 송신한 재현이 아니다.** 그러나 X 매크로 입력이 native ingress에 도달할 수 있는 source 경로이므로 “APK는 X를 전혀 만들지 않음/미지원 Host X 송신 0도 PC처럼 확인됨”으로 닫지 않는다. 이 공백은 r7이 새로 만든 parser/capability 경로는 아니지만, r7은 해당 X up을 queue 정리/만료에서 더 오래 보존하므로 **이번 APK 공유 영향에 명시**한다.

검증용이 기존 APK/공용 client 담당과 **JNI 직접 입력·매크로·queued input·park/resume의 미지원 Host X edge 0**을 fake wire/queue로 확인하고, source의 capability 경계를 필요 범위에서 보수하도록 통합한다. 판단 기준은 기존 X capability 계약이다. 실제 사용자 X 버튼이나 구 게시 Host를 실행할 필요는 없다. 제품 파일 소유는 기존 창구에서 조율하며 PC 작업용에 중복 병렬 지시를 보내지 않는다. **이번 PC SOURCE OK로 이 APK 항목까지 승인된 것은 아니다.**

## 산출물·다음 단계

제공 Release/GNLinkViewer.exe SHA-256 **ddda8e8c9423cc6bff4e276a98f89b831301ea54919630fcb8637e3b7cc29f27**을 읽기 확인했다. 생성 출하 vcxproj의 KA seam 정의 없음, HOLD_SEND/REFUSE_RELEASE 두 marker **ASCII·UTF-16 0건**을 확인했다. 최종 통합 출하 artifact 검사는 별개다.

review probe는 `.claude/codex_ka_pc_review/r7_src/queue_gate_r7_probe.exe`, SHA-256 **0fd1c31fd8827c7819fba96172306ab3b536c963a8c59472601fcd0c44cd6641**이다. 기본 mode와 **기록 link만 쓰는 hook mode**를 별도 process 실행했다. 실제 제품 Host/Viewer·네트워크·입력/전원/clipboard 0이며, test env 설정은 review process 내부뿐이다. 제품 전체 빌드·APK 빌드/설치·운영/삭제·commit/push는 하지 않았다.

**PC의 r6 N1/N2 Source 관문은 통과**했으므로 기존 검증용은 검수된 공용 안전 guard와 합친 최종 고정 후보에서 PC 독립 실행을 준비할 수 있다. **아직 공용 guard 85f9383의 종료 구간/INVALID 미결과 슬롯 조건이 있으므로 지금 실 Host e2e를 시작하라는 지시는 아니다.** 조건이 해결되면 기존 task에서 실제 카드 혼합/예약·owner 종료·release-before-close·hook 실패·동일 계정/전환 등 필요한 사용자 동작을 독립 확인한다. source 성공을 EVIDENCE/게시 완료로 대신하지 않는다. 사용자 현재 조치 없음.
