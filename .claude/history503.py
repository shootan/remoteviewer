# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 503) 2026-09-10 🔴 실기 실패의 원인 — **창 없는 자식에게 직접 요청하고 있었다** (항목 1·2, 5 회귀)
`AbandonedBeforeSwap -- could not ask pid N to stop`. 그 한 줄이 인앱 업데이트를 **구조적으로** 막고 있었다.

**왜 성공할 수 없었나**
```cpp
update_process_targets.cpp  request_process_stop
  EnumWindows → WM_CLOSE ; if (posted) return true;                  // 창 있는 것만
  if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, (DWORD)pid)) ...     // 2번째 인자는 process GROUP id
  return false;
```
- ⚠️ **`GenerateConsoleCtrlEvent` 의 2번째 인자는 pid 가 아니라 process group id 다.** 그 pid 가 **그룹 리더**이면서 **업데이터와 콘솔을 공유**해야만 성공한다. supervisor 는 자식을 그렇게 띄우지 않는다.
- **실패하는 경로가 아니라 성공 사례가 없는 경로**였다. 코드 주석이 이미 *"which the product's own supervisor does not guarantee"* 라고 적어 두었다.
- `PrepareForSwap` 은 하나라도 실패하면 즉시 abort 하므로, **창 없는 자식(Stream·Capture)이 살아 있는 한 업데이트는 불가능**했다.

**🔴 왜 테스트가 이걸 못 잡았나 — 이번 건의 진짜 교훈** (검증용 확인)
`request_process_stop` 의 호출자는 **제품에 1곳, 테스트에 0곳**이다. 테스트의 `requestStop = ` 대입 **11곳이 전부 람다 stub** 이고, "실패 케이스" 라는 것조차 **stub 이 false 를 돌려줄 뿐**이었다. 즉 **실제로 실패하는 분기는 한 번도 실행된 적이 없다.** `update_effects_test.cpp:8` 이 스스로 *"request_process_stop do not exist here"* 라고 적어 두었다.
⚠️ **CMake 링크 추가는 실호출 증거가 아니다.** 스위트 총합이 초록인 것과 그 코드가 돌아본 적 있는 것은 다른 명제다.

**고친 것 (1·2)**
- `ProcessTarget` += `parentPid` · `hasWindow`(열거 시 채운다).
- `PrepareForSwap` — **창 없는 자식은 직접 요청하지 않는다.** 창 있는 supervisor 에게만 요청하고 자식은 supervisor 가 정리하게 둔다.
  ⚠️ **소유는 추정하지 않는다**: 부모가 **대상 목록에 있고 · 창이 있고 · 자식보다 먼저 시작**했을 때만 인정한다. 셋 중 하나라도 없으면 그 프로세스는 **직접 요청 대상**으로 남는다(임의 그룹핑 금지).
- `Quiesce` — **`PrepareForSwap` 이 본 목록**으로 기다린다(재열거 아님). 재열거는 *"그 사이 시작된 것을 기다리고, 이미 요청받고 나가는 중인 것은 확인조차 안 하는"* 결과였다.
- ⚠️ **`OpenProcess` 의 `ACCESS_DENIED` 를 "종료됨" 으로 보지 않는다.** 그건 *"모른다"* 이고, 모르는 것을 죽었다고 읽으면 **파일을 쥔 프로세스 위로 업데이트가 진행된다.** 명시적 실패로 끝낸다.
- 실패 사유를 **분리**: `did not exit`(요청 못 함) vs **`outlived its parent`**(supervisor 는 갔는데 자식이 남음). 뒤엣것만 계약 위반이다.

**반례 — 실제 프로세스, stub 없음** (`update_stop_process_test`, 신규 **13 PASS**)
창 있는 부모(창은 만들되 **보여주지 않는다** — 데스크톱에 UI 를 띄우지 않는다) + **창 없고 콘솔 없는** 자식. 제품의 `enumerate_product_processes` · `request_process_stop` · `PrepareForSwap` · `Quiesce` 를 **그대로** 호출한다.
- `asking the windowless child directly FAILS -- this is the field failure`
- `asking every target directly -- the old routing -- fails  could not ask pid 6696 to stop` ← **현장 로그와 같은 문구**
- `PrepareForSwap succeeds with a windowless child present` · `Quiesce sees both processes actually exit`
- **음성 대조**: 라우팅 한 블록을 빼면 위 두 줄이 FAIL 하고 **`could not ask pid N to stop`** 이 그대로 재현되며 프로세스가 남는다.
- ⚠️ 처음 판에서 `check` 의 detail 이 대기와 **다른 시점**을 다시 읽어 *"PASS … 1 left"* 를 찍었다. 한 번 읽어 그 값을 보고하도록 고쳤다 — **판정과 근거가 다른 순간이면 근거가 아니다.**

**5 의 회귀 (검증용 NEEDS_CHANGES 반영)**
지적: *"`GNLinkUpdater.exe` 가 목록에 있다"* 는 단정만 있고 **실제로 교체되는지 확인한 테스트가 0개**였다. **이번 원인 축과 같은 모양**(실행 대신 형태를 단정)이다.
- `updater-member` 케이스 추가: staging 에 새 바이트 → **Swap → 설치본 `GNLinkUpdater.exe` 의 바이트가 실제로 바뀌고 `.gnlink-old` 백업이 생긴다.**
- 반대 방향도 단정: **설치 디렉터리 안에서 실행되는 업데이터는 거부**(`a payload destination is the running updater`). 안 그러면 첫 단정이 *"검사하는 게 없어서"* 통과할 수 있다.
- `updater_assembly_test` fixture 가 **production 과 다른 목록**(Updater 빠진)으로 조립을 돌리고 있었다 → `product_payload_names()` 로 정렬.

**결과**: 콘솔 세션에서 **64 스위트 `^PASS` 1811 · 실패 0**. `update_effects` 210 · `updater_assembly` 49 · `update_stop_process` 13.
⭐ **#497 의 빨간 4개가 여기서 닫힌다** — 같은 바이너리를 **콘솔에서** 재실행하니 전부 통과했고, 계속 빨갛던 `udp_control_e2e` 도 통과했다. 그때 "강한 후보" 로만 적었던 것이 **재실행으로 확정**됐다.
⚠️ 두 sweep 이 겹쳐 돌아 앞 실행이 어느 바이너리를 썼는지는 확정할 수 없다. **위 수치는 새 단정이 들어 있음을 직접 확인한 빌드**(`update_effects` 210 · `stop_process` 13)의 것이다.

**아직 안 한 것 (3·4)**: 부모의 최종 exit 확정 뒤 relaunch 판정(`update_relaunch.cpp` 의 *"still running -- nothing to bring back"*) · 서비스 stop 의 SCM 분기와 `AccessDenied` false positive.
**하지 않은 것**: 새 자동 버전 게시 없음 · live 앱 설치·종료 없음 · `git push` 없음 · Host 아키텍처 재설계 없음 · `TerminateProcess` 승격 없음 · 광역 kill 없음.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
