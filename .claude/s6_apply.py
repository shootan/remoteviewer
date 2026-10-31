import io

frag = io.open('.claude/s6_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 440)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #440 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] step4-2 `RegisterInstall`/`Relaunch`/`HealthCheck` production — **설계 질의 중**'
assert anchor in d and '#440' not in d
d = d.replace(
    anchor,
    '- [x] step4-3 등록 로직 공유 단위 추출 완료 — **(D)안 Codex 승인** (history #440). '
    '설치기와 업데이터가 `install_registration.{hpp,cpp}` 를 함께 링크하고 **버전을 인자로** 받는다 '
    '((C) 는 디스크의 Setup 이 구버전이라 구 `kProductVersion` 을 DisplayVersion 에 써서 기준점을 오염시킴). '
    '실패 단계 보고 + 롤백용 스냅샷(이전 설치 값으로 복원). 45 checks PASS, 실제 시스템 무영향 실측\n'
    '- [ ] **결정 요청**: 구버전 `GNLinkSetup.exe` 가 유지보수 바이너리로 영구히 남는 문제 — '
    'payload 목록·서비스명·폴더명·바로가기명이 전부 그 바이너리의 컴파일 시점 상수라 '
    '신버전이 바꾸면 구 언인스톨러가 남기거나 엉뚱한 것을 지운다. 선택지 3안 제시(history #440)\n'
    '- [ ] step4-4 업데이터 `RegisterInstall` 배선 + `Relaunch`/`HealthCheck` production\n'
    + anchor, 1)
d = d.replace(anchor, '', 1)  # the old placeholder line is superseded
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
