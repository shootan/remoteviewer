import io

frag = io.open('.claude/s5_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 439)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #439 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] step4-2 `RegisterInstall`/`Relaunch`/`HealthCheck` production — **설계 질의 중**'
assert anchor in d and '#439' not in d
d = d.replace(
    anchor,
    '- [x] step4-2 서버 manifest 발행 라우트 + JS·Kotlin 소비자 완료 (history #439) — '
    '`GET /api/update/manifest?platform=` (기존 인증 재사용, platform 화이트리스트, '
    '`REMOTE60_UPDATE_DIR` 미설정이면 꺼짐, 내보내기 전 자체 검증). '
    '**세 런타임이 같은 고정 벡터의 실제 서명을 함께 검증**: JS 34 checks · Kotlin 9 tests · C++ 41 checks. '
    '서버 스위트 전체 exit 0. 아티팩트는 디렉터리 서버가 스트리밍하지 않음\n'
    + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
