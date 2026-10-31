import io

frag = io.open('.claude/s4_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 438)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #438 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] step2 Android — `signingConfigs` 명시(동일 debug 키 유지)'
assert anchor in d and '#438' not in d
d = d.replace(
    anchor,
    '- [x] step4-1 업데이트 전용 WinHTTP HTTPS 클라이언트 완료 (history #438) — '
    'TLS 1.2+, 인증서 검증 비활성화 옵션 **미설정**, https→http redirect 스택 차원 차단, '
    'URL 자격증명 거부, 크기 상한은 절단이 아니라 중단, 실패 시 부분 파일 삭제. 36 checks PASS '
    '(**평문 리스너를 https 로 지목해 실패시키는 실제 소켓 케이스 포함**). '
    '실제 HTTPS 서버 왕복은 미검증(검증 약화 없이는 불가)\n'
    '- [ ] step4-2 `RegisterInstall`/`Relaunch`/`HealthCheck` production — **설계 질의 중**: '
    '등록을 자체 구현할지 설치기 `/S` 재사용할지. 후자는 업데이터의 2단계 안전 교체를 '
    '설치기의 비원자 교체가 우회하게 됨\n'
    + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
