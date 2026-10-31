# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 508) 2026-09-11 `gdi_capture` 원인 가설 **세 개가 전부 기각**됐다 — 그리고 문턱이 분산 안에 있다
검증용이 세 번째 설명을 냈다: *"Debug 바이너리를 fps 임계값에 대고 쟀다"* (Debug 42.99/46.99 FAIL, Release 54.33/56.66 PASS). **내 측정으로는 성립하지 않는다.**

**6회 측정 — GNLink 0개, 콘솔 세션, 같은 커밋, config 만 다름**
```
Debug   run1 rc=0 fps=59.3321 copyAvg=16873    Release run1 rc=0 fps=59.3254 copyAvg=16856
Debug   run2 rc=0 fps=59.3114 copyAvg=16914    Release run2 rc=0 fps=58.3117 copyAvg=17196
Debug   run3 rc=0 fps=58.6658 copyAvg=17025    Release run3 rc=0 fps=57.3320 copyAvg=17502
```
- **양쪽 전부 통과**하고, **Debug 가 오히려 미세하게 빠르다.** config 로는 `42.99 → 59.33` 을 설명할 수 없다.
- 가설 세 개가 순서대로 나왔고 셋 다 내 쪽에서 재현되지 않았다: **live 호스트 경합**(#505, 철회) → **원인 미규명**(#506) → **Debug config**(이번, 기각).

**모든 실행에 걸쳐 살아남는 관찰은 하나뿐이다**
`1/copyAvg ≈ 측정 fps` 가 **양쪽 사람, 양쪽 config, 모든 실행**에서 소수점까지 맞는다. **상한은 화면 갱신률이 아니라 복사 시간**이고, 그 값이 실행마다 **16.8ms ~ 23.3ms** 사이에서 움직인다.
⚠️ **50fps 문턱은 정확히 20.0ms 다 — 관측 분산의 한가운데.** 그러면 이 스위트의 판정은 코드가 아니라 **그 순간의 부하**가 정한다. 검증용의 실패 실행이 어떤 부하였는지는 **이제 재구성할 수 없다.**
- 별개 관찰(판정을 뒤집는 요인은 아님): 같은 복사를 **부모는 1.8~2.5ms, 격리 캡처 프로세스는 17~23ms** — 약 10배 격차가 모든 실행에서 일관된다.
- ⚠️ **"원인 미규명" 도 "config 탓" 도 아니고, "문턱이 분산 안에 있다" 가 실제로 확인된 것이다.** 세 번 연속으로 *그럴듯한데 확인되지 않은 설명* 을 붙였다 — 두 사람 모두. **재현되지 않는 설명은 설명이 아니다.**

### 509) 2026-09-11 `udp_control_e2e` — Release 전용이 아니라 **상태 의존**이다
검증용 Release sweep 에서 `udp_control_e2e` rc=1 · `viewer_udp_recovery` rc=127 을 보고했다.

**내 Release 전량 재빌드 후 sweep**: **64 스위트 `^PASS` 1803 · 실패 1건**(`udp_control_e2e` 1P/17F). `viewer_udp_recovery` **rc=127 은 재현되지 않았다**(통과).

**그런데 config 문제가 아니다** — 연속 3회:
```
Release  rc=1 PASS=1 FAIL=17     Release  rc=1 PASS=1 FAIL=17     Debug  rc=1 PASS=1 FAIL=17
```
- **Debug 도 똑같이 실패한다.** 그런데 몇 시간 전 내 Debug sweep 에서는 **18 PASS 로 통과**했다.
- 실패 지점은 `native_video_client_tcp_control.cpp:202` **`udp hello ack failed`** — 세션이 아예 성립하지 않는다. 나머지 17건은 그 결과다.
- 검증용도 이전에 **1P/17F → 16P/2F** 로 오가는 것을 봤다. **같은 두 상태를 오간다.**
- ⚠️ **config 가 아니라 기계 상태에 달려 있다.** 무엇이 바뀌면 성립/불성립이 갈리는지는 **확인하지 않았다** — UDP 포트 점유가 후보지만 **추정이고 단정하지 않는다.** 이번 3커밋과는 무관하다(건드린 파일에 udp control 관련 0개).
- 같은 시점에 좀비 테스트 프로세스 2개(`viewer_udp_recovery`, `winhttp_tls_posture`)를 정지시켰다 — Release 링크를 막고 있었다. **스위트가 프로세스를 남긴다는 것 자체가 별건 결함**이다.

**총합의 재현 조건은 이제 셋이다**: (1) 스위트 출력 형식(5개는 `^PASS` 0), (2) **config**(Debug 1820 / Release 1803), (3) **빌드 최신성**. 셋을 밝히지 않은 총합은 재현되지 않는다. `.claude/sweep-per-suite*.txt` 에 **config·빌드 시각을 헤더로** 넣었다.
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')

p2 = 'docs/작업목록.md'
t = io.open(p2, encoding='utf-8').read()
old = u'''0.0.4 [ ] **`gdi_capture_process` — 격리 캡처 프로세스만 약 10배 느리다** (원인 미규명, 별건, 우선순위 인앱 업데이트 **아래**) — 상한은 화면 갱신률이 아니라 **복사 시간**이고(`1/copyAvg` 가 측정 fps 와 소수점까지 일치), 같은 복사를 부모는 1.8~2.5ms, 캡처 프로세스는 17~23ms 에 한다. **RDP 도 GNLink 경합도 아닌 상태에서 재현**되므로 `CLAUDE.md` 의 RDP 예외로 설명되지 않는다. 50fps 문턱 근처라 실행마다 통과/실패가 갈린다(history #506)'''
new = u'''0.0.4 [ ] **`gdi_capture_process` 는 문턱이 관측 분산 안에 있다** (별건, 우선순위 인앱 업데이트 **아래**) — 상한은 화면 갱신률이 아니라 **복사 시간**이고(`1/copyAvg` 가 측정 fps 와 소수점까지 일치), 그 값이 **16.8~23.3ms** 로 움직이는데 **50fps 문턱은 정확히 20.0ms** 다. 그래서 실행마다 판정이 갈린다. 원인 가설 3개(live 경합·원인 미규명·Debug config)는 **전부 기각**됐다(6회 측정, 양쪽 config 전부 PASS). 별개 관찰: 같은 복사를 부모 1.8~2.5ms / 캡처 프로세스 17~23ms 로 **약 10배 격차**(history #506·#508)
0.0.5 [ ] **`udp_control_e2e` 가 상태에 따라 1P/17F 로 무너진다** (별건) — `udp hello ack failed`(`native_video_client_tcp_control.cpp:202`), 세션이 아예 성립하지 않는다. **Debug/Release 무관**, 같은 기계에서 몇 시간 전엔 18 PASS 였다. UDP 포트 점유가 후보지만 미확인 (history #509)
0.0.6 [ ] **스위트가 프로세스를 남긴다** (별건) — sweep 뒤 `viewer_udp_recovery` · `winhttp_tls_posture` 가 살아남아 Release 링크(`LNK1104`)를 막았다. 테스트가 자기 자식을 정리하지 않는다 (history #509)'''
assert t.count(old) == 1, 'tasklist'
t = t.replace(old, new, 1)
io.open(p2, 'w', encoding='utf-8', newline='').write(t)
print('ok tasklist')
