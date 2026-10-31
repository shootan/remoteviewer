# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

entry = u'''
### 504) 2026-09-10 항목 3·4 — **"모른다" 를 "괜찮다" 로 읽던 자리 두 곳**
7항목 배선 완료. 두 결함 모두 *판정 불가를 조용히 통과시키던* 같은 형태다.

**(3) 닫히는 중과 거부는 다르다** (`update_relaunch.cpp`)
현장 로그가 전부 말해 준다:
```
18:56:20.266  the waiting caller acknowledged and is standing down
18:56:20.275  relaunch (required) GNLinkHost.exe: still running -- nothing to bring back   ← 9ms 뒤
```
- 종료를 **수락하고 내려가는 중**인 호스트를 **9밀리초 뒤**에 한 번 읽고 *"살아 있으니 되돌릴 것이 없다"* 로 판정했다. 호스트는 곧 스스로 닫혔고 **남는 것이 없었다.**
- ⚠️ **한 번만 물으면 "닫히는 중" 과 "거부" 는 구분되지 않는다.** 그런데 여기 있는 것들은 **전부 종료를 요청받은** 프로세스다 — 그 상태에서 Running 은 절반이 정상 진행이다.
- → `RelaunchConfig::closingGraceMs`. Running 이면 **정해진 시간 동안 다시 묻는다**(production 15초).
- ⚠️ **소진은 종료가 아니다.** 시간이 다 되어도 여전히 Running 이면 Running 이고, relaunch 는 *"정말로 아직 있다"* 는 이유로 건너뛴다. 반대편(E10c)을 함께 단정해 두지 않으면 이 대기는 *"기다렸으니 갔겠지"* 로 읽힐 수 있다.
- 기본값은 **0** 이다: 고정 답을 주입하는 테스트가 상수 하나 때문에 실제 timeout 을 물게 할 이유가 없다. **production 이 설정한다**(`updater_effects.cpp`).
- 회귀 `update_relaunch_test` **107 → 114**: `E10b` 닫히는 중이면 **실제로 되살아난다**(`4 liveness reads` — 한 번에 알 수 없었다는 증거) · `E10c` 유예가 소진돼도 **시작하지 않고 alreadyRunning 으로 남는다**.

**(4) `OpenProcess` 실패를 "이미 종료됨" 으로 세고 있었다** (`update_process_targets.cpp`)
```cpp
HANDLE held = OpenProcess(...);
if (!held) return true;  // already gone; nothing to ask     ← ACCESS_DENIED 도 여기로 들어왔다
```
- ⚠️ **열 수 없는 것은 죽은 것이 아니라 볼 수 없는 것이다.** 그것을 *"요청 성공"* 으로 세면 **교체할 파일을 아직 쥐고 있을지 모르는 프로세스 위로** swap 이 진행된다.
- → `ERROR_INVALID_PARAMETER`(pid 가 더 이상 프로세스가 아님) 만 성공, `ERROR_ACCESS_DENIED` 는 **실패**. 실패는 디스크를 건드리기 전에 abandon 으로 이어지므로 안전한 방향이다.
- **서비스는 SCM 으로 요청한다.** `GNLinkInputService.exe` 는 창도 콘솔도 없어 아래 두 방식이 **구조적으로 적용 불가**이고, 대신 진짜 supervisor 가 있다 — SCM 이다. `ControlService(SERVICE_CONTROL_STOP)`. 미설치(`ERROR_SERVICE_DOES_NOT_EXIST`)·이미 정지(`ERROR_SERVICE_NOT_ACTIVE`)는 성공, **열지도 제어하지도 못하면 실패**(여기서도 세 답을 둘로 줄이지 않는다).
- 회귀 `update_stop_process_test` **13 → 15**: 실제로 종료된 pid → **성공** · pid 4(System, 열 수 없음) → **실패**. 둘이 같은 답이던 것이 결함이었으므로 **양쪽을 함께** 단정한다.

**전체**: 콘솔 세션에서 **64 스위트 `^PASS` 1820 · 실패 0**. 증분 **1811 → 1820 = +9**(relaunch +7, stop_process +2)로 정확히 맞는다.
**7항목 상태**: 1 supervisor 우선 종료 ✅ · 2 captured handle bounded wait ✅ · 3 부모 최종 exit 뒤 relaunch 판정 ✅ · 4 SCM 분기 + AccessDenied ✅ · 5 Updater 자기 교체(+실제 swap 회귀) ✅ · 6 ready 전 required-set ✅ · 7 게시 전 차집합 거부 ✅
**하지 않은 것**: 새 자동 버전 게시 없음 · live 앱 설치·종료 없음 · `git push` 없음 · `TerminateProcess` 승격 없음 · 광역 kill 없음 · Host 아키텍처 재설계 없음.
**남은 미검증**: 실기 완주(0.2.108 호스트가 실제로 이 경로로 10파일 교체) · 언인스톨 항목 실제 갱신 · **현재 설치된 업데이터와 동일한 실행 조건의 10파일 업데이트**(설치된 0.2.108 업데이터에는 이 수정이 없다 — bootstrap 수동 설치 필요).
'''

d = d.rstrip('\n') + '\n' + entry
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
