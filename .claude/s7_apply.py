import io

frag = io.open('.claude/s7_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 441)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #441 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
anchor = '- [ ] step4-4 업데이터 `RegisterInstall` 배선 + `Relaunch`/`HealthCheck` production'
assert anchor in d and '#441' not in d
d = d.replace(
    anchor,
    '- [~] step4-4 UI 진입점 — **트레이 "Check for updates" 완료** (history #441). '
    '`update_check` 가 NotConfigured/Unreachable/Rejected/UpToDate/UpdateAvailable 다섯으로 답해 '
    '**"확인 실패" 가 "최신" 으로 뭉개지지 않는다**. 비동기 detach + UI 스레드로 post, 28 checks PASS. '
    '**UI 실동작은 미검증**(빌드한 호스트를 실행하면 라이브와 충돌). '
    '클라이언트 셸 시작 시 비동기 확인은 미착수\n'
    '- [ ] step4-5 업데이터 `RegisterInstall` 배선 + `Relaunch`/`HealthCheck` production\n'
    + anchor, 1)
d = d.replace('\n' + anchor, '', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('구현계획.md updated')
