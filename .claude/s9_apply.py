import io

frag = io.open('.claude/s9_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 443)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #443 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = ('- [ ] 패키지 계약 ① 본체 — Setup 을 패키지 구성원으로(조건 3) · 버전 일관성 3중 검사(조건 4) · '
          '회귀 3종(Setup 해시 불일치·잠김·후속 실패에서 **구 Setup 까지 롤백**, 조건 5) · '
          '기존 Setup 실행 중 정책(조건 7)')
assert anchor in d and '#443' not in d
d = d.replace(
    anchor,
    '- [x] 패키지 계약 ① 본체 완료 (history #443) — Setup 이 제품 파일과 동일한 staging·백업·교체·롤백 대상(조건 3) · '
    '`file_contains_utf16_version` 버전 일관성 + manifest 대조(조건 4, **문자열 존재는 정합성 검사이지 신원 증명이 아님** 명시) · '
    '회귀 3종 전부 실제 OS 동작(조건 5) · `updaterImagePath` 로 **자기 교체 금지를 validate 가 검사**(조건 6) · '
    '`product_image_names` 에 Setup 추가(조건 7). 126 checks PASS. '
    '**한계: 첫 업데이트 도달 전까지는 현재 설치된 0.2.104 Setup 이 언인스톨을 지배**\n'
    '- [ ] 원장 후속: manifest 기반 uninstall(③) — 구 Setup 이 매니페스트를 읽지 않아 당면 문제는 못 고치나 장기적으로 더 견고\n'
    '- [ ] **다중 파일 패키지 형식** 미구현 — 현재 staged 아티팩트는 파일 하나이고 Swap 이 각 payload 이름으로 복사(테스트용 단순화)',
    1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
