# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 495) 2026-09-10 🔴 업로더가 종료를 계약하지 않았다 — **정적 소멸자가 joinable 스레드를 파괴**
`log_upload_stop()` **제품 호출자 0건.** 부르는 곳은 `log_upload_test.cpp` **9곳뿐**이었다.

**무엇이었나**
```cpp
UploaderState& state() { static UploaderState s; }   // log_upload.cpp:140
struct UploaderState { ... std::thread worker; };    //             :115
void log_upload_stop() { ... worker.join(); }        //             :551  ← 제품은 안 부른다
```
- 종료 시 static 소멸자가 **joinable `std::thread` 를 파괴** → 표준상 **`std::terminate()`**. 예외도 스택도 없이 **정리하다 죽는다.**
- ⚠️ **현장 BEX64/c0000409 의 원인이라고 단정하지 않는다.** 정확한 빌드의 PDB 가 없어 offset 이 미해석이고, 다른 terminate 경로도 배제되지 않았다. **독립적으로 옳은 수정**으로만 다룬다.
- ⚠️ **테스트가 9번 부르고 제품이 0번 부른 것이 이 결함이 계속 초록으로 보인 이유다.** 커버리지는 계약이 아니다.
- 게다가 Host 의 `WSACleanup()` 은 **worker 가 소켓 안에 있는 채로** 돌고 있었다.

**고친 것 (좁게)**
- `LogUploadShutdown` — **RAII 수명 가드**. Host·Client **두 진입점**에 선언. `log_upload_shutdown()` = 래치 + `log_upload_stop()` + join 후 콜백 해제.
- ⚠️ **`detach` 는 쓰지 않았다.** worker 가 정적 state·콜백을 만지는 중일 수 있어 **소멸 후 UAF / 로그 유실**로 바뀐다. **join 해야 한다.**
- **래치**: `s.shutdown` 은 **worker 를 시작하는 것과 같은 lock 안에서** 검사한다. 밖에서 검사하면 *"configure 가 통과 → shutdown 이 join → configure 가 두 번째 worker 시작"* 창이 남는다. 종료 중 `configure` 는 `"shutting down"` 으로 거부.
- **선언 순서 = 해제 순서**: 함수 끝의 `WSACleanup()` 줄은 **소멸자보다 먼저** 실행된다. 그래서 `WSACleanup`·GDI 정리를 **scope guard 로 바꾸고**, 가드를 **마지막에 선언**했다 → 해제는 **worker join → UI(font) → WSACleanup**.
- **교착 금지**: join 은 **어떤 lock 도 쥐지 않은 채**(worker 가 매 주기 `s.mu` 를 잡는다), 그리고 **메시지 루프의 그 스레드에서**. auth 콜백은 `PostMessageW` 라 동기 대기가 없다.
- **종료 대기 상한**: 최종 drain 이 **새 요청을 시작하는 것**을 `kStopDrainBudgetMs = 3000` 으로 끊는다. 없으면 대기는 **큐 깊이 × http timeout** 이고, 그 대기는 **UI 스레드**에서 일어난다.
- **실측 상한**: `3000ms + 진행 중인 http_post 1건`. accept 후 응답하지 않는 서버 상대 **실측 5,997ms**(directory_client 수신 timeout 6s 가 지배). 가정이 아니라 **테스트가 파일에 적는 숫자**다.

**회귀 — 격리 child 프로세스**(`log_upload_shutdown_test`, 신규 9건)
⚠️ **테스트가 `log_upload_stop()` 을 부르는 것으로는 아무것도 증명되지 않는다** — 지금도 9곳이 그러고 제품은 0곳이었다. 그래서 **판정은 child 의 종료 코드**다.
- **정상 exit** → 0 · 이후 `log_upload_running()` false
- **미시작 exit** → 0 (토큰 없는 Host 가 이 상태다)
- **요청 진행 중 exit** → 0 · **유계 실측 5,997ms**
- **401 pause 상태 exit** → 0 (보낼 방법 없는 held 를 기다리지 않는다)
- **configure 경합** → join 이후 250ms 동안 계속 configure 해도 **재시작 0**, 거부는 실제로 발생
- **음성 대조**: 가드 없이 같은 일을 하면 **exit=3**(abort). ⚠️ 이때 sink 스레드는 **명시적으로 정지**시켰다 — 그러지 않으면 **엉뚱한 스레드 때문에 죽는 것**을 잡고 통과했다고 착각한다.
- **제품 배선 대조**: `host_app_main.cpp` · `client_shell_main.cpp` 를 **소스에서 읽어** 가드 선언을 확인한다. 종료 코드로는 볼 수 없고, **없었던 것이 정확히 이 연결**이다.

**남는 것(기록)**: Client 의 auth 콜백은 메시지 루프 종료 후 발화하면 `PostMessageW` 로 넘긴 `std::string` 이 **디스패치되지 않아 누수**된다 — 프로세스 종료 직전이라 영향은 없고, 범위 밖이라 고치지 않았다.
**하지 않은 것**: 강제 종료 전역 동작 추가 없음 · 광역 재설계 없음 · 버전 인상·새 후보·서명 없음 · 라이브 실행·설치·캐시 삭제·push·NAS 변경 없음.
'''

# appended at the end; entries here run in ascending order
d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok history')
