import io

frag = io.open('.claude/s8_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 442)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #442 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] step4-5 업데이터 `RegisterInstall` 배선 + `Relaunch`/`HealthCheck` production'
assert anchor in d and '#442' not in d
d = d.replace(
    anchor,
    '- [x] step4-5 **payload 이름 검증**(패키지 계약 ① 조건 2, 심층 방어) 완료 (history #442) — '
    'manifest 에서 온 이름이 관리자 권한 임의 경로 쓰기 수단이 되지 않도록 '
    '**정제가 아니라 거부**. traversal(혼합 구분자 포함)·절대·드라이브 상대·대체 스트림·'
    '**예약 장치명**(NUL 이면 교체가 아무것도 안 쓰고 성공한 것처럼 보임)·후행 점/공백·중복 거부. '
    '54 checks PASS, `validate()` 에 배선돼 나쁜 이름이면 업데이트가 시작되지 않음\n'
    '- [ ] 패키지 계약 ① 본체 — Setup 을 패키지 구성원으로(조건 3) · 버전 일관성 3중 검사(조건 4) · '
    '회귀 3종(Setup 해시 불일치·잠김·후속 실패에서 **구 Setup 까지 롤백**, 조건 5) · '
    '기존 Setup 실행 중 정책(조건 7)\n'
    '- [ ] step4-6 업데이터 `RegisterInstall` 배선 + `Relaunch`/`HealthCheck` production + '
    '클라이언트 셸 시작 시 비동기 확인\n'
    + anchor, 1)
d = d.replace('\n' + anchor, '', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
