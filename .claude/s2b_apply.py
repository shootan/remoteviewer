import io

frag = io.open('.claude/s2b_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 434)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #434 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] step2 상태기계 seam 설계와 결정론 회귀 (교체 실패·트리 종료·재실행·상호배제)'
assert anchor in d and '#434' not in d
d = d.replace(
    anchor,
    '- [x] step2-2 manifest 파서 + 서명 검증 완료 (history #434) — ECDSA P-256/SHA-256 via BCrypt/CNG, '
    'detached 서명, 줄 단위 `key=value` 형식(`apps/shared/update_manifest/README.txt`). '
    '**검증-후-파싱 순서를 타입으로 강제**(`VerifiedManifest` 생성자 private, `load_manifest` 만 friend → '
    '버전 비교가 서명 없이는 호출 경로 자체가 없음). 41 checks PASS. '
    '**릴리스 키는 의도적으로 비어 있어 default_verifier 가 전부 거부** — 운영키는 별도 승인(4.5)\n'
    + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
