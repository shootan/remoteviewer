# APK X fence r2 — SOURCE 재검토

**NEEDS_CHANGES — 원 unknown→X press 누출은 닫는다. 이미 송신한 down의 owed release가 이후 같은 버튼 down의 거부로 가려지는 두 경로를 보수한다.** capability 감소 때 park가 bounded 실패/teardown으로 끝나는 방향은 아래 범위로 수용한다.

## 고정·근거

- 후보 **50340ceba9a896bdfe789df181ac2b64522eb818**, 비교 **3ef6ee6..50340ce**의 4파일 +365/-4. tracked clean·diff --check 정상, report SHA-256 **6264ff714c4324822abae4b545c7c32b6e72c45524cc9b0a12058175823e01e8** 일치.
- 고정 ledger와 QueueInputEvent/송신/OnSent/Reset 호출부를 읽었다. Queue와 send 기록을 parkMu_로 직렬화하고 ResetUnlocked에서 연결 이력을 정리하는 위치는 수용한다. shared_core·JNI·park state는 변경하지 않았다.
- 43/43×6·mutant·21 target 재빌드와 소비 시험은 **작업용 보고**다. Codex는 실제 Host/AVD/NDK/JNI/ClientSessionController wire suite를 실행하지 않고 고정 ledger의 제한 직접 probe만 실행했다.

## 닫는 것

거부된 첫 X down의 입구 이력이 cap 지원 이후 move bit/up까지 억제되고, 그 up 뒤 새 press가 허용되는 기본 순서를 수용한다. 송신에서 폐기된 기본 한 press의 후속 up/held bit 억제와 연결 Reset 방향도 맞다.

직접 고정 helper 결과 **original_unknown_transition_closed=1**. 기존 steady no-X/unknown edge·bit 차단과 Queue/send 두 경계의 수용은 유지한다. 모든 반복/중첩 순서의 owed release까지 해결했다는 뜻은 아니다.

## N1 (P1) — 최근 거부 down이 기존 owed release를 가림

이미 X1 down이 송신·ACK돼 **Sent/owed**인 상태에서, 같은 버튼의 추가 down이 거부되는 경우다. 입력/매크로에 연속 down을 금지하는 계약은 없으므로 이 순서를 무효 입력이라고 가정하지 않는다. 기존 ClientHeldInput도 같은 버튼의 down을 set으로 보유한다.

### 입구 경로

1. X1 down 송신 완료 → ledger Sent, ClientHeldInput도 X1을 보유한다.
2. cap이 no-X인 동안 추가 X1 down이 입구에서 거부된다. **AdmitQueued는 entryRefused=true로 덮는다.**
3. X cap 복귀 후 사용자의 X1 up → entryRefused 분기가 **up을 false로 버린다**. SendOwed가 true여도 이 분기가 우선한다.

```
sent_down_repeat_rejected_at_entry
repeat_accepted=0 supported_up_accepted=0 release_still_owed=1
```

### 송신 경로

1. 기존 X1 down이 Sent/owed다.
2. 추가 down이 X 지원 시 입구를 통과했으나 no-X Pong 뒤 송신 단계에서 거부된다.
3. **AdmitSend는 Sent를 Dropped로 덮는다.** cap 복귀 뒤 up은 Dropped 분기에서 버리고 Idle이 된다. 기존 송신 down의 release 빚도 ledger에서 사라진다.

```
sent_down_repeat_dropped_at_send
owed_after_drop=0 dropped=1 supported_up_accepted=0
```

두 경우 모두 Host에는 이미 실제 down을 보냈다는 전제인데, 이제 X를 받는 상태여도 대응 up이 wire로 나가지 않는 source 경로다. 위 수치는 실제 Host가 아닌 **고정 ledger의 송신 완료 모델/직접 함수 실행**이다. 실제 OS 상태가 남았다고 이번에 관측한 것은 아니다.

**이미 송신한 버튼의 release 소유권을 후속 press의 거부/폐기와 독립적으로 보존**한다. 입구 억제와 sender의 Dropped가 기존 Sent/owed를 덮거나 가리지 않게 한다. cap이 지원될 때 owed up이 queue/send를 통과하고 ACK 뒤에만 빚을 끝낸다. no-X 상태에 release 예외를 열지 않는다. 단순히 “모든 up은 허용/무시” 또는 모든 거부 상태 초기화로 해결하지 않는다.

새 명시적 down의 처리와 기존 owed release의 우선순위를 한 per-button 계약으로 정리한다. up이 입구에서 거부돼 sender에 도달하지 않는 순서도 고려해, 두 기록이 서로 다른 press의 억제/소유권을 잘못 소비하지 않게 한다. 연결 세대 Reset과 park의 실제 held 회계는 유지한다.

기록 wire/ledger 대조에 **이미 송신된 X down→추가 down의 입구 거부/송신 폐기 각각→X 지원 복귀→up 도착·owed 0**, X2·left 독립성, park/연결 Reset을 추가한다. 최근 down 거부만 보고 기존 owed up을 버리지 말라는 r1의 완료 기준을 검증하는 같은 범위 보수다. 실제 사용자 버튼/구 Host를 실행할 필요는 없다.

## X2 결정 — cap 감소 뒤 park 실패/세션 종료는 수용

**미지원 Host로 X release를 예외 송신하지 않고, park의 제한된 재해제 뒤 확인 실패로 세션을 종료하는 방향을 수용한다.** 안전한 capability 계약을 유지하는 fail-closed 결과이며 새 권한/자동 재탈취를 추가하지 않는다.

정확히는 **cap bit 감소만으로 모든 active session이 즉시 종료된다고 확인한 것은 아니다.** 보고/시험의 종료는 owed X를 포함한 **park/drain을 완료하지 못했을 때**의 Failed("park not confirmed") 경로다. cap이 다시 지원될 때 N1처럼 보낼 수 있는 owed up을 버리는 것은 이 fail-closed 수용과 별개다.

드문 기능 철회/role 전환에서 목록 복귀 유지가 실패하고 연결이 닫힐 수 있다는 사용자 영향은 통합 기록에 남긴다. 실제 APK UI의 오류 안내·목록 상태·기존 재연결/재activation 정책은 fake controller 시험만으로 완료됐다고 보고하지 않는다. 기존 KA의 자동 재탈취 금지와 새 연결 이력 0을 통합 EVIDENCE에서 확인한다.

## X3 — 소비 target 영향

21개 target 재빌드 오류 0과 role_park/prelogin_select/wire_cap_idr/recv_loop/FEC PASS 보고를 수용한다. **ka_relay_client_test는 fixture 미설정으로 sign-in 전 실패한 미판정**이며 이번 입력 변경 통과/회귀로 합산하지 않는다. 실제 Host 시험 미실행·NDK/JNI/4 ABI·AVD 미검증도 유지한다.

기존 PC record의 Tab/move/left 등 non-X 경로를 우회하지 않고 기대값 변경 없이 확인한 방향은 맞다. PC cd9937a의 SOURCE 수용, 일반 record lane, 승인된 guard 하네스 범위는 이 ledger 잔여 때문에 전량 다시 보류하지 않는다. 다만 R2 통합 때 **shared queue의 X up 보존 + 이 ledger + 실제 sender**를 같은 고정 후보로 확인해야 한다. 새 header/member를 소비하는 cpp/hpp/target의 동시 재빌드도 유지한다.

## 직접 실행·다음 담당

`.claude/codex_test_injection_review/apk_xfence_503/`에 고정 source/의존성과 **owed_repeat_probe.cpp/exe**를 보존했다. exe SHA-256 **691a9c97f3072d5e1befb13600f5ede09b7888b40f4d0517730a463acbec8e9c**.

probe는 실제 ledger header를 사용하고, OnSentAction에 송신 완료를 대역으로 제공했다. 반환 0은 원 반례의 수정 확인과 위 두 owed 반례의 재현이며 제품 PASS가 아니다. 참고로 연속 down을 새 press로 재판정하는 현재 동작도 관측했지만, 이번 반려의 핵심은 **그 결정이 기존 release 빚을 훼손한다는 점**이다.

실제 Host/Viewer/AVD/JNI/network/input/power/clipboard API는 0이다. 제품 편집·전체 제품/NDK 빌드·설치·운영/삭제·commit/push는 하지 않았다. 검증용은 기존 작업용1의 같은 APK X task에 **owed 보존 두 경로와 대조**를 한 묶음으로 연결한다. 기본 전환/steady-state/Reset·소비 회귀의 닫은 범위를 다시 설계하거나 별도 writer를 만들지 않는다. 사용자 현재 조치 없음.
