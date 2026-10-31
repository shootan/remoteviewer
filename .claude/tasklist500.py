# -*- coding: utf-8 -*-
import io

p = 'docs/작업목록.md'
d = io.open(p, encoding='utf-8').read()

old = u'''0.0 [~] **0.2.109 실기 대기**'''
new = u'''0.0.1 [ ] **업데이터 자신이 영영 갱신되지 않는다** — `GNLinkUpdater.exe` 는 manifest 에 있어 **staging 까지 받아 놓지만** `payloadNames`(`updater_effects.cpp:293-295`)에 없어 **설치 디렉터리로 옮겨지지 않는다**(`update_effects.cpp:468` swap 대상이 아님). 업데이터 버그를 고쳐도 배포되지 않는다는 뜻. 검증용 지적, **별건 유지 — 고치지 말 것**(history #500)
0.0.2 [ ] **ready 전 필수목록 검사 누락** — 검증용 지적, 별건 (history #500)
0.0.3 [ ] **한글 UI 깨짐** — 검증용 지적, 별건 (history #500)

0.0 [~] **0.2.109 실기 대기**'''
assert d.count(old) == 1, 'anchor'
d = d.replace(old, new, 1)

old2 = u'''**남은 것은 실기 하나** — 업데이트 후 Host 가 `directory=online` 이 되는지.'''
new2 = u'''**복구 게시 완료**: `GNLinkSetup.exe` 가 manifest 에 빠져 swap 이 실패하던 것을 `releaseId=r-0.2.109-2`(version 0.2.109 유지)로 복구, 공개 URL **10/10 200**, 검증용 **구현 OK**(history #499·#500). **남은 것은 실기 하나** — **0.2.108 호스트에서 업데이트 재시도** → Host 가 `directory=online` 이 되는지.'''
assert d.count(old2) == 1, 'anchor2'
d = d.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok tasklist')
