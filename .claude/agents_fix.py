# -*- coding: utf-8 -*-
import io

p = 'AGENTS.md'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- two different completions
old = u'''- **완료 흐름** — 이 순서를 다 지난 것만 "완료" 다:'''
new = u'''- **두 개의 완료를 이름으로 구분한다.** 아래 순서를 다 지나면 **배포가 완료**된 것이고,
  **task 완료는 사용자 행동 검증까지**다. 이 둘을 같은 단어로 부르면 0.2.115 처럼 8단계를 전부
  통과하고도 **누를 버튼이 없는 릴리스**가 "완료" 로 나간다.
- **배포 완료 흐름** — 이 순서를 다 지난 것이 **배포 완료(마일스톤)** 다:'''
assert d.count(old) == 1, 'flow anchor'
d = d.replace(old, new, 1)

old = u'''  → 8. **"배포 완료 · 실기 테스트 대기"**.'''
new = u'''  → 8. **"배포 완료(마일스톤) · 실기 테스트 대기"**.'''
assert d.count(old) == 1, 'step8 anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- commit cadence
old = u'''## Scope Control
- Do not include unrelated files in commits.'''
new = u'''## Scope Control
- **의미 있는 기능·릴리스·조사 단위로 기록하고 커밋한다.** 문서 문구 정정 같은 사소한 변경까지
  history·계획·커밋 3종을 매번 강제하지 않는다.
- Do not include unrelated files in commits.'''
assert d.count(old) == 1, 'scope anchor'
d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('agents ok')
