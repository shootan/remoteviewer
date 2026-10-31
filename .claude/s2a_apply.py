import io

frag = io.open('.claude/s2a_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 433)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #433 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
old = ('- [ ] step2 버전 비교 **계약 + 3개 런타임(C++/Kotlin/JS) 공통 테스트 벡터** — 헤더 공유는 불가하므로 '
       '"공용 헤더 추출" 이 아니라 계약과 공유 벡터로 일치를 보장한다. 추출은 **C++ 안에서만** (현재 C++ 테스트 0건)')
new = ('- [x] step2-1 버전 비교 계약 + 3런타임 공통 테스트 벡터 완료 (history #433) — '
       '`apps/shared/version_compare_vectors.txt` 규칙 6개 + 벡터 46개를 C++/JS/Kotlin 세 테스트가 각각 읽는다. '
       'C++ 139 checks PASS · JS 95 checks PASS(디렉터리 스위트 전체 ALL PASS) · Kotlin tests=2 failures=0. '
       '`compare_versions` 는 `version_compare.hpp` 로 추출(C++ 내부만), 설치기 재빌드 exit 0. '
       '성분 포화(2147483647)는 wrap 대비 **의도된 동작 변경**(실 버전 문자열로는 도달 불가)')
assert old in d, 'plan anchor not found'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
