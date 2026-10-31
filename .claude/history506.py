# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

# --- correct the explanation I recorded in #505
old = u'''- 검증용 근거: 세 커밋이 건드린 파일에 gdi/capture 관련 **0개**, 그리고 그 시점에 **live `GNLinkHost` 14408 / `GNLinkStream` 27204 가 실제로 화면을 캡처 중**이라 같은 화면을 두고 경합했다.
- ⚠️ **확정하려면 호스트를 멈추고 재실행해야 하는데 live 앱 종료가 금지돼 있어 하지 않았다.** **환경 실패로 분리하고 미검증으로 남긴다** — "무관" 으로 단정하지 않는다.
- 같은 스위트가 한 기계에서 통과하고 다른 실행에서 실패한 것 자체가, **이 스위트의 판정은 그 순간의 부하에 달려 있다**는 사실이다.'''
new = u'''- 🔴 **여기 처음 적었던 "live 호스트 캡처 경합" 은 원인이 아니었다.** 검증용이 정정했다 — 사용자가 GNLink 를 종료한 뒤(프로세스 0개) 재실행해도 **여전히 실패**(42.99 / 46.99 fps). #506 참조.'''
assert d.count(old) == 1, 'correct 505'
d = d.replace(old, new, 1)

entry = u'''
### 506) 2026-09-11 🔴 `gdi_capture_process` — 경합도 RDP 도 아니었다. **격리된 캡처 프로세스만 10배 느리다**
#505 에 *"live 호스트가 같은 화면을 캡처 중이라 경합"* 이라고 적었다. **틀렸다.** 검증용이 사용자가 GNLink 를 종료한 상태(프로세스 0개, 콘솔 세션)에서 재실행했고 **두 번 다 실패**했다: **42.9993 fps · 46.9969 fps**(요구 50).

**내가 직접 돌린 것 — 같은 조건에서 통과했다**
```
GNLink processes: 0     >console  shotan  1  Active
GDI_DELIVERED_FPS=56.6656   rc=0   RESULT: ALL PASS
GDI_CAPTURE_COPY_AVG_US=17644     GDI_PARENT_COPY_AVG_US=1798
```
- ⚠️ **같은 조건에서 검증용은 두 번 실패하고 나는 통과했다.** 그러니 이 스위트는 **50fps 문턱 근처에서 흔들린다** — 판정이 그날 그 순간에 달려 있다.

**실제 신호는 문턱이 아니라 복사 시간이다** (검증용 분석, 내 수치로도 성립)
| | capture 프로세스 | 부모 |
|---|---|---|
| 검증용 | 21,170~23,290 µs | 2,412~2,477 µs |
| 나 | **17,644 µs** | **1,798 µs** |
- `1/0.017644 = 56.7fps` — **측정 fps 와 소수점까지 맞는다.** 검증용 쪽도 `1/0.0233 = 42.9`. 즉 **상한은 화면 갱신률이 아니라 복사 시간**이다.
- ⚠️ **같은 복사를 부모는 10분의 1 시간에 한다.** 격리된 캡처 프로세스만 **약 10배 느리다.** 이 격차가 이 테스트가 실제로 관찰하고 있는 값이고, **설명되지 않았다.**
- 이 PC 의 디스플레이 어댑터가 셋이다: `Parsec Virtual Display Adapter`(갱신률 보고 없음) · `Virtual Display Driver`(60Hz) · `AMD Radeon(TM) Graphics`(59Hz). `CLAUDE.md` 의 RDP 예외(Remote Display Adapter 32Hz)와 **같은 계열일 수 있으나 추정이고 단정하지 않는다.**

**판정**
- **이번 3커밋과 무관**하다 — 건드린 파일에 gdi/capture 관련 **0개**.
- ⚠️ **"환경 실패이므로 무시" 가 아니라 "원인 미규명, 별건"** 이다. **RDP 도 아니고 GNLink 경합도 아닌 상태에서 재현**되므로 `CLAUDE.md` 의 기존 RDP 예외로 **설명되지 않는다.**
- 작업목록에 별건으로 추가. 우선순위는 인앱 업데이트 건 **아래**.

**교훈**: 처음 붙인 설명(경합)은 **그럴듯했고 확인 가능한데 확인하지 않았다.** 확인 방법(호스트 정지 후 재실행)이 금지 사항이라 미검증으로 남긴 것까지는 맞았는데, 그 사이 **사용자가 종료해 조건이 생겼을 때** 다시 물은 것은 검증용이었다. **미검증으로 남긴 항목은 조건이 바뀌면 다시 물어야 한다.**
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')

# --- task list
p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old2 = u'''0.0.2 [ ] **ready 전 필수목록 검사 누락**'''
new2 = u'''0.0.4 [ ] **`gdi_capture_process` — 격리 캡처 프로세스만 약 10배 느리다** (원인 미규명, 별건, 우선순위 인앱 업데이트 **아래**) — 상한은 화면 갱신률이 아니라 **복사 시간**이고(`1/copyAvg` 가 측정 fps 와 소수점까지 일치), 같은 복사를 부모는 1.8~2.5ms, 캡처 프로세스는 17~23ms 에 한다. **RDP 도 GNLink 경합도 아닌 상태에서 재현**되므로 `CLAUDE.md` 의 RDP 예외로 설명되지 않는다. 50fps 문턱 근처라 실행마다 통과/실패가 갈린다(history #506)
0.0.2 [ ] **ready 전 필수목록 검사 누락**'''
assert t.count(old2) == 1, 'tasklist'
t = t.replace(old2, new2, 1)
io.open(p2, 'w', encoding='utf-8', newline='').write(t)
print('ok tasklist')
