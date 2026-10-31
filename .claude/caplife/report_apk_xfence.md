# keepalive-r2 e — APK X 버튼 capability 경계 (feat-apk-0233)

task=keepalive-r2(e, APK 0.2.33 공용) / kind=fix 결과 / 근거=codex_ka_pc_src_r7.md 「APK 영향」

## 고정
- commit **3ef6ee6848e6301776e011c6b57cd49ffedb3f61** (feat-apk-0233, 부모 1aecc48). tracked clean, `git diff --check` 정상.
- 제품: `native_video_client_session.cpp` (+13), `native_video_client_session.hpp` (+4), 새 `client_mouse_xbutton_fence.hpp` (Win32 비의존, Android 컴파일 대상과 같은 src 폴더).
- 시험: 새 `client_session_xbutton_fence_test.cpp` + CMake 타깃 `remote60_client_session_xbutton_fence_test`.
- **공유 core(`native_video_client_shared_core.{cpp,hpp}`)·`client_park_state.hpp`·`input_macro.*`·`native_bridge.cpp`는 0줄 변경.**

## 규칙 (PC `fence_mouse_for_host` 와 같은 계약)
- `HostTakesMouseXButtons = role.known && (flags & kCaptureFlagMouseXButtonsV1)`. **첫 Pong 전 미확정 = X 없음**(PC 의 hostMouseXButtons=false 와 같음). `EffectiveHostFlags`(prelogin 용)는 쓰지 않는다.
- X 없는 Host: 모든 kind 의 `buttons &= 0x7`, kind 2/3 + keyCode 5/6 은 거부. X 있는 Host: 그대로.
- 적용 두 곳:
  - A. `QueueInputEvent`(prelogin 검사 직후): JNI·매크로가 `false` 를 받는다. 이때 `MouseXButtonsHeldBack()` 이 증가한다.
  - B. 제어 루프 송신 직전(`ViewerMaySendAction` 직후, 같은 `continue` 패턴): `QueueInputEvent` 를 거치지 않고 큐에 들어온 항목을 막는 backstop이다. 대상은 park/held release, 그리고 ka-pc 병합 후 `DropAllButReleases` 가 보존할 X up이다. 거부된 edge 는 `heldInput_.OnSent` 에 도달하지 않는다.
- 보유 중인 X 는 예외 없이 해제된다. `ClientHeldInput` 은 송신(B 통과)된 down 만 기록하므로, 기록된 X 는 X Host 에서 온 것이고 그 release 도 B 를 통과한다. 이는 소스로 확인했고 park X Host 시험에서 X up 이 실제 wire 에 나간 것으로 확인했다.

## 시험 (fake wire: loopback fake 제어 Host, 실 Host·AVD·입력 0)
제품 `ClientSessionController`/제어 루프/scheduler/role gate/park/held release/TCP client/`InputMacro` 를 링크해 실행했다.
`remote60_client_session_xbutton_fence_test` 결과는 **23/23, exit 0, RESULT ALL PASS** 이고 같은 결과가 6회 반복됐다.

| 경로 | 구 Host(X 없음) wire | X Host wire |
|---|---|---|
| direct(JNI 동등: `buttons&0x7`, keyCode 5/6) | X edge 0 · X bit 0, 호출 false ×4, 양성 대조 left down/up+move 3건 도착 | 7건, X edge 4 |
| macro(`LoadSerialized`→`PollDueStep`→`QueueInputEvent`, nativeMacroPump 루프 동등, buttons 0x1F 포함) | 8 step 중 X 4 거부, wire 4건 X edge 0 · X bit 0, move 0x1F→0x7, 0x19→0x1 | 8건, X edge 4, X bit 4 |
| park(보유→BeginPark→release)/resume | X down 거부, left 만 보유·해제, release 1건 X 0; resume 후 X2 거부·left 도착 | X1+left release 2건(X up 1); resume 후 X2/left 4건 |
| 송신 직전 action fence(순수) | 미확정 role / 플래그 없음: X edge 4/4 거부, bit mask, key kind 5 code 5·Ping 통과 | 통과 |

부정 대조(mutant, 커밋 안 함, 원본 src 해시 3E39B5AB… 로 복원 → 재빌드 23/23):
- **M1: A 제거.** 4 FAIL, 모두 "호출 false" 항목이다. **wire X 는 여전히 0**이므로 B 가 세션 안에서 실제로 막았다는 증거다. exe 해시는 3D229321…
- **M2: A+B 제거.** 6 FAIL. 구 Host wire 에 X edge 4(direct)/4+bit 4(macro)가 나갔고, park release 에 X up 이 나갔다. exe 해시는 9BF2CEB8…
- 회귀: role_park 13/13 exit 0, prelogin_select 60/60 exit 0.

## ka-pc 충돌 위치
- cd9937a 자체는 session.cpp/hpp 를 건드리지 않는다. 바뀐 것은 shared_core.cpp(8)·hpp(4)이고, 0fb4676..cd9937a 범위에서도 session 두 파일의 diff 는 0이다. 이번 변경은 shared_core 를 건드리지 않으므로 **source 충돌은 없다.**
- 이번 hunk 위치:
  - session.cpp: include :24 / `QueueInputEvent` :622–627 / 제어 루프 :978–983
  - hpp: 접근자 :168–170 / 멤버 :406
- CMakeLists 는 role_park 타깃(:709) 뒤에 추가했다. ka-pc 는 `endforeach()` 뒤(0fb4676 기준 :698)에 추가하므로 가까운 위치라 병합 시 문맥 확인이 필요하다(기계적).
- ka-pc 병합 뒤 의미: `DropAllButReleases`/backpressure/2초 만료가 X up 을 보존한다. 구 Host 로 가는 X up 은 A 에서 큐 진입부터 막히고, 다른 경로로 들어온 X up 은 B 에서 송신 전에 버려진다.

## 미검증 / 범위 밖
- JNI 글루(`native_bridge.cpp`) 자체와 NDK/4 ABI 빌드는 이번에 실행하지 않았다. 헤더는 Win32 비의존이고 같은 src 폴더에 있다.
- 실 Host·AVD·실 X 버튼 실행은 0이다.
- 키보드 kind 5/6 + keyCode 5/6 은 계약 밖이며 PC fence 와 같이 막지 않는다(관찰만 함).
- 첫 Pong 전 X 클릭은 버려진다(PC 와 같음).
- ka-pc cd9937a 병합 상태에서의 재실행은 R2 통합 때 한다.
