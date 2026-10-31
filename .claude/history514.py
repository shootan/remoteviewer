# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 514) 2026-09-11 🔴 **Client 는 업데이트를 물어본 적이 없다** — 호출 지점이 로그인 전 한 곳
사용자 보고: 회사 PC 에서 Client 0.2.112 를 켜도 업데이트가 뜨지 않는다. `client.log` 에 매번 같은 줄만 남았다:
```
update check: not configured -- no update URL is configured
```

**무엇이었나**
```
client_shell_main.cpp:652   start_update_check() 정의
client_shell_main.cpp:1131  호출 — grep 결과 이 한 곳뿐
client_shell_main.cpp:1127  ShowWindow(gWindow, SW_SHOW)      ← 1131 이 그 직후, WebView 생성 전 = 로그인 전
client_shell_main.cpp:722   gSessionToken = token (로그인 성공) ← 그 뒤 재호출 0곳
```
- 확인은 **창을 띄운 직후**, 즉 **토큰도 서버 주소도 없을 때** 한 번 돈다. `update_endpoint_for` 는 당연히 아무것도 못 만들고 `NotConfigured` 가 된다.
- **로그인은 그 둘이 생기는 바로 그 순간인데, 아무도 다시 묻지 않는다.**
- ⚠️ **"로그인하면 뜬다" 가 아니다.** 코드상 영영 뜨지 않는다. 그 안내를 하지 않는다.

**함께 나온 두 결함**
- 콜백(`:682`)이 **`result` 만 받아 무조건 게시**했다. 로그아웃하거나 다른 계정으로 로그인한 뒤 늦게 도착한 결과가 **남의 화면에 뜰 수 있는 자리**다.
- `configured_directory_url()`(`:670`)이 **스냅샷 밖에서 따로 읽혔다** — 그 함수는 `gStateMu` 를 **다시 잡는다**. 세션과 주소가 **서로 다른 순간**을 가리킬 수 있었다.

**고친 것**
- 로그인 성공 뒤(`++gOwnerEpoch` 와 목록 게시 후) **비동기로 확인**. 시작 시 확인은 유지(설정된 URL·override 가 이미 있는 경우).
- `client_update_gate.{hpp,cpp}` **신설** — `should_check` / `note_checked` / `may_publish`. ⚠️ **판정이 `client_shell_main.cpp` 안에 인라인이라 어떤 테스트도 링크할 수 없었다** — 이번 라운드 원인 축 3번과 같은 구조라 **실행 가능한 자리로 옮겼다.**
- 콜백이 **요청 시점의 owner+epoch 를 들고 가** 현재와 일치할 때만 게시.
- **URL·토큰·owner·epoch 를 한 lock 안에서** 한 번에. 설정 파일 폴백은 그 뒤, 서명되지 않은 상태에서만.
- **전송 정책 무변경** — http↔https 추측·승격·강등 없음.

**회귀** (`client_update_flow_test`, 신규 **23 PASS**)
제품 경로 그대로: `update_endpoint_for` → `check_for_update_async` → `manifest_fetcher_for` → `load_manifest` → `shell_update_notice`.
- `before sign-in there is nothing to ask` / `after sign-in an endpoint is derived  https://rem.example:443/api/update/manifest?platform=windows` / `...carrying the session credential` / `...bound to the directory's origin`
- `...and an http directory gets none, so no credential can leak`
- gate: 같은 세션 재초대 무시 · 재로그인은 새 질문 · 다른 계정은 새 질문
- 늦은 응답: 로그아웃 뒤·다른 계정 뒤 **게시 안 됨**
- **소스 계약 검사**: `client_shell_main.cpp` 를 읽어 **호출 지점 2곳 이상 + 하나는 세션 생성 이후**임을 단정. 종료 코드로는 볼 수 없고, **없었던 것이 정확히 이 연결**이다.
- ⚠️ **운영 개인키를 쓰지 않았다**(검증용 요구). 그 키는 DPAPI 로 이 기계·이 계정에 묶여 있어 테스트가 다른 곳에서 돌지 않게 된다. 저장소의 **공용 테스트 키**(`apps/shared/update_manifest`)를 `trustedPublicKeyHex` 로 주입했다 — 제품 로직을 스텁으로 바꾼 것이 아니라 **비밀만 주입 지점으로 대체**한 것이다(COM 회귀에서 목적지를 주입한 것과 같은 방식).

**⚠️ 경계 — 넓히지 않는다**
업데이트 경로는 **http 를 두 층에서 거부**한다: `update_endpoint_for` 가 http 디렉터리에서 아무것도 만들지 않고, 전송도 http url 을 직접 받아도 거부한다(`http:// is refused; the update path requires https`). **자격증명이 그 요청에 실려 가므로 둘 다 의도된 것**이고, 그래서 **loopback 으로 실전송을 구동할 수 없다.** 그 **거부 자체를 단정**하고, **https 실전송과 WebView 렌더(`deliver_update_notice` → 페이지)는 미검증으로 분리**한다. **"end-to-end" 라고 쓰지 않는다.**

**Host 경로 불변 확인**: `update_effects` 210 · `update_stop_process` 24 · `updater_assembly` 49 · `update_release` 80 · `client_shell_bridge` 29 — 전부 rc=0.

**전달 경로 (사용자 안내용)**: 회사 PC 의 0.2.112 는 **자동 확인이 고장나 있어 자기 자신을 고칠 수단이 없다.** 새 Client 를 받는 길은 둘뿐이다 — **(a) 그 PC 에서 Host 로 묶음 업데이트**(`payloadNames` 10개에 `GNLinkClient.exe` 가 있고, 이 PC 에서 `c71864fd` → `7e583f6c` 교체를 실측했다) 또는 **(b) 설치본 수동 1회**. Host 가 설치·로그인돼 있으면 (a) 로 끝난다. **회사 PC 무단 조작·로그인은 하지 않는다.**

**별건으로 내린 것**(사용자 우선순위에 따라 조사 중단): 11:48 업데이터 **트리거 정체**(Host·Client·작업용 모두 아님) · **`result:` 줄 부재**(= `effects.run()` 미반환, `updater_main.cpp:321-322`) · 그 단서인 `CREATE_BREAKAWAY_FROM_JOB`(`updater_main.cpp:171`) 경로의 **성공 시 무기록**.
⚠️ 조사 중 **기각한 가설 하나**를 남긴다: *"업데이터의 `log_upload` 정적 소멸자가 `std::terminate`"* — **업데이터는 `log_upload` 를 링크하지 않는다.** 설명력이 커 보였지만 확인해 보니 틀렸다. **보고 전에 확인한 것이 요점이다.**
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
