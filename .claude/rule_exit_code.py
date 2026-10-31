# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

anchor = u'''**회귀**: `client_shell_bridge_test` 29 PASS · `client_update_flow_test` 23 PASS.
**게시하지 않았다** — 사용자 설치가 먼저이고, `0.2.116` 게시분은 불변이다.'''

rule = anchor + u'''

**🔴 여기서 나온 규칙 — "PASS 개수는 그 프로세스가 정상 종료했을 때만 결과다"**
이 라운드에서 제가 보고하고 검증용이 OK 를 낸 **"17 PASS"** 는 결과가 아니라 **중단 지점**이었다.
프로세스는 18번째 단정을 찍기 전에 죽었고, 그때까지 찍힌 PASS 만 화면에 남아 있었다. 종료 코드를 보지
않으면 *"여기까지는 통과했다"* 와 *"전부 통과했다"* 가 **똑같이 생겼다.**
- 스위트 결과는 **`^PASS` 개수 + 종료 코드 + 마지막 요약 줄**을 함께 읽는다. 셋 중 하나라도 없으면 결과가 아니다.
- 개수 비교는 **같은 구성끼리만** 한다. 이 파일의 17 과 23 은 항목 구성이 달라 **비교 대상이 아니다.**
- **간헐 결함은 1회 통과로 닫지 않는다.** 4회 연속으로 확인했다.

같은 계열의 사례가 오늘만 셋이다: `updater.log` 에 **`result:` 줄이 없는데 성공으로 읽은 것**(#512·#516),
**`^PASS` 를 한 줄도 찍지 않는 스위트 5개가 총계에 묻힌 것**(#509), 그리고 이것. 전부 *"보고가 끝까지
갔다는 증거를 따로 확인하지 않았다"* 는 한 가지 실수다.
'''

assert d.count(anchor) == 1, 'rule anchor'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(anchor, rule, 1))
print('rule ok')
