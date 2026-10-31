import io

frag = io.open('.claude/s12_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 446)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #446 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] **다중 파일 패키지 `artifacts[]`** (Codex 승인)'
i = d.find(anchor)
assert i >= 0 and '#446' not in d
j = d.find('\n', i)
new = ('- [x] 다중 파일 패키지 **스키마 2 세 런타임 반영** 완료 (history #446) — '
       '`artifact=name|size|sha256|url` 반복 + `releaseId`/`arch`, 서명이 목록 전체를 덮음, '
       '목록 정책(개수·크기·총량·https·자격증명·중복·이름) 세 런타임 동일. '
       'C++ 588 / JS 54 + 스위트 exit 0 / Kotlin 11 tests. 공유 벡터를 **서로 다른 아티팩트 3개**로 재생성\n'
       '- [ ] **단일 릴리스 스냅샷 staging** (조건 5) — 모든 파일 staging·검증 후에만 교체 진입, '
       '파일마다 latest 재조회 금지\n'
       '- [ ] **실제 파일 N개 실행 회귀 4종** (조건 7) — 정확 목적지 / 누락·해시오류·중간실패 시 종료 0·설치변경 0 / '
       'latest 변경 중 혼합 0 / 교체 중간 실패 시 전부 복원')
d = d[:i] + new + d[j:]
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
