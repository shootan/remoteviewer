# -*- coding: utf-8 -*-
import io

p = 'docs/history.md'
d = io.open(p, encoding='utf-8').read()

old = u'''**남는 것(기록)**: Client 의 auth 콜백은 메시지 루프 종료 후 발화하면'''
new = u'''**추가 회귀 — 새 질문이 옛 답을 망가뜨리지 않는가**(`directory_retry_test` **57 → 67**)
health 조회는 **새로 생긴 질문**이고, 새 질문은 옛 답을 깨뜨린다. http 에서 관측 포트는 늘 **유도 가능**했고(`httpPort+1`) **현재 배포된 디렉터리가 전부 그 배치**다.
- 이 케이스만 UDP 를 **`httpPort+1` 에 바인딩**한다(loopback, 고정 포트 아님, 최대 32회 재시도). 나머지 캐시 케이스는 **임시 포트**라 정반대를 증명한다 — 하나는 *"광고를 실제로 썼다"*, 이것은 *"광고가 필수는 아니다"*.
- **health 500** · **health 200 인데 아무 말 없음** 두 행 모두: 하트비트 1 · **관측이 실제로 `+1` 로 감**(`ObserveProbes>=1`) · 재등록 0 · `public=` 게시.
- ⚠️ 인접 포트를 못 잡으면 **조용히 건너뛰지 않고 FAIL** 시킨다. 안 돌아서 통과하는 케이스가 제일 위험하다.

**남는 것(기록)**: Client 의 auth 콜백은 메시지 루프 종료 후 발화하면'''
assert d.count(old) == 1, 'history anchor'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
