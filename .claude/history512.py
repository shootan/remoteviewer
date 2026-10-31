# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 512) 2026-09-11 등록 실패 — **COM 을 초기화하지 않았고, 그 사실을 볼 방법도 없었다** (A·B·C)
실기 2차 시도가 **Prepare·Quiesce·Swap 10파일을 통과**하고 Register 에서 막혔다. 로그는 `register failed: registration failed` 다섯 글자였다.

**A. 사유가 만들어지고 한 줄 옆에서 버려지고 있었다** (`updater_effects.cpp`)
```cpp
update_registration_wiring.cpp:106  state->lastResult = install::register_install(...);  // completed[] · failedAt · detail
update_registration_wiring.cpp:122  effects.lastResult = [state]{ return state->lastResult; };   // 제공됨
updater_effects.cpp:444             return registration->apply();   // ← 읽는 제품 호출자 0곳
```
`register_install()` 은 **첫 실패에서 멈추고 어디서 멈췄는지 기록한다.** 그 결과를 `bool` 로 좁혀 반환했기 때문에, **현장 실패를 다른 어떤 실패와도 구별할 수 없었다.** 이제 `completed=…  failedAt=…  detail=…` 이 로그에 간다.
⚠️ **상태기계 타입은 건드리지 않았다** — 요건은 *"HRESULT/Win32/자식 exit 가 로그에 도달할 것"* 하나였다.

**B. COM — 등록 경로 한 곳** (`update_registration_wiring.cpp`)
`CoCreateInstance(CLSID_ShellLink, ...)` 를 부르는데 `updater_main.cpp`·`updater_effects.cpp`·`update_registration*.cpp` 에 **`CoInitialize` 가 0개**였다. 설치기는 자기 main(`installer_main.cpp:686`)에 갖고 있다 — **공유하려고 추출할 때 caller 의 전제가 함께 오지 않았다.**
- `ApartmentScope` (RAII): `CoInitializeEx(APARTMENTTHREADED)` → **`SUCCEEDED`(S_FALSE 포함) 면 해제 책임 보유** → 소멸자에서 그때만 `CoUninitialize`. **`RPC_E_CHANGED_MODE` 는 남이 다른 모드로 이미 초기화한 것**이므로 실패 코드지만 **그대로 진행**한다.
- ⚠️ **`update_relaunch.cpp:244-278` 이 이미 이 패턴을 올바르게 쓰고 있었다** — 검증용이 처음 *"거기도 같은 노출"* 이라 했다가 정정했다. **손대지 않았다.** 저장소 안에 이미 있던 선례를 등록 경로에 옮긴 것이 이번 수정이다.
- `createShortcut` 이 **HRESULT 를 detail 로** 반환한다(`0x800401f0` 같은 숫자가 보이도록). 목적지는 **주입 가능**하게 만들었다 — 그래야 테스트가 운영 Start 메뉴를 건드리지 않고 진짜 API 를 돌릴 수 있다.

**반례 — 실제 API** (`update_registration_com_test`, 신규 **9 PASS**)
```
PASS  on a thread with no apartment, CLSID_ShellLink is refused   hr=0x800401f0
PASS  ...and the reason is exactly CO_E_NOTINITIALIZED
PASS  the production shortcut writer succeeds on that same bare thread
PASS  ...and the .lnk is really on disk   com-shortcut-fixture\\GNLinkComFixture.lnk 1349 bytes
PASS  a thread in the MTA reports RPC_E_CHANGED_MODE ... carries on and still writes the link
PASS  ...without taking the caller's apartment away
PASS  an unwritable destination fails ... detail carries an HRESULT  0x80070003
```
⚠️ **"현장 실패가 이것이다" 는 아직 확정이 아니다.** 기구를 실증했을 뿐이고, **A 가 실기에서 `failedAt=Shortcuts` + HRESULT 를 찍는 것을 본 뒤** 확정한다. 검증용과 합의한 선이다.

**C. 첫 시도 실패의 정체 — 고아가 된 창 없는 자식** (`update_effects.cpp`)
로그 순서가 답이었다: `relaunch (required) GNLinkHost.exe: started` 가 찍혔다는 것은 **그 시점 Host 가 이미 없었다는 뜻**이다. 그러면 남은 것은 **부모가 열거되지 않는 창 없는 자식**이고, 내 소유 확인 3조건 중 *"부모가 대상 목록에 있음"* 을 채울 수 없어 직접 요청으로 떨어진다 — 창이 없으니 실패한다.
→ **요청할 상대가 없으면 요청하지 않고 `Quiesce` 로 넘겨 기한 대기.** 사유도 셋으로 갈랐다: `did not exit` · `outlived its parent` · **`orphaned windowless pid N did not exit (its parent … was already gone, so there was nobody to ask)`**.
**양쪽 가지를 모두 단정한다**(검증용 요구): (a) 기한 내 종료 → **swap 진행** · (b) 끝내 미종료 → **abandon 되고 사유가 `could not ask` 와 구분**.
**음성 대조**: 고아 분기를 끄면 `could not ask pid 30540 to stop` — **현장 문구 그대로** 재현된다.

**🔴 전체 sweep 이 내 결함을 잡았다 — 커밋 직전이었다**
첫 판의 고아 판정은 *"부모가 대상 목록에 없다"* 였다. 그런데 **`parentPid == 0` 은 "모른다" 는 뜻**이고, 손으로 대상을 기술하는 호출자는 전부 0 이다. 그 전부가 **아무도 종료 요청을 받지 않은 채 기다림으로** 넘어갔다: `update_effects_test` **210 → 206**, `update_release_test` **80 → 74**.
⚠️ **내가 `ProcessTarget` 주석에 직접 써 둔 규칙을 바로 아래 코드에서 위반했다** — *"0 / false 는 '모른다' 는 뜻이고, 소유가 확인되지 않은 대상은 직접 요청한다."*
→ `parent_has_exited()` 로 교체했다: **미지(0)는 직접 요청** · **살아 있지만 우리 대상이 아닌 부모도 직접 요청**(기다려도 아무도 멈춰주지 않는다) · **실제로 종료된 경우만 고아**. `ACCESS_DENIED` 로 확인 못 하면 **"예" 가 아니다.**

**전체**: 다른 빌드가 겹치지 않은 **격리 sweep** — `config=Debug` **65 스위트 `^PASS` 1838 · 실패 0**. 증분 **1820 → 1838 = +18**(신규 COM 스위트 9 + stop_process 15→24) 로 정확히 맞는다.
⚠️ 그 직전 두 sweep 은 **내 타깃 빌드와 겹쳐 돌아 어느 바이너리를 쟀는지 확정할 수 없었다**(실패 5건 / 실패 0건으로 갈렸다). **오늘 이미 같은 함정을 겪었으므로** 격리 실행으로 다시 냈다.

**미검증 / 막힌 것**
- **`GNLinkStream` 이 부모 급사 시 스스로 나가는가** — 코드에 **부모 감시도 job object 도 없다**(job 은 `gdi_capture_process.cpp` 만). **(b) 라면 기한 대기만으로는 실기가 여전히 막힌다.** 라이브 호스트 종료가 **권한 분류기에 차단**돼 관측하지 못했다. **추정하지 않는다.**
- 한때 부모 없는 `GNLinkCapture` 를 관측했지만 **2분 내 자연 소멸**했고 생성 시각이 내 캡처 테스트와 겹친다 — **제품 거동이라 보고하지 않았다.** 스냅샷 하나로 결론 내지 않은 것이 요점이다.
- 전달 경로: 이번 수정은 **인앱으로 배달되지 않는다**(롤백이 새 업데이터까지 되돌린다). **수동 bootstrap 1회 필요.**
**하지 않은 것**: `update_relaunch.cpp` 무수정 · 등록 비치명화 우회 없음 · 상태기계 개편 없음 · 운영 CommonPrograms·서비스·방화벽 무접촉 · 게시 없음 · `git push` 없음.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
