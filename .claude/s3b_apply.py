import io

frag = io.open('.claude/s3b_history.md', encoding='utf-8').read()
p = 'docs/history.md'
h = io.open(p, encoding='utf-8-sig').read()
assert '### 437)' not in h
io.open(p, 'w', encoding='utf-8-sig', newline='').write(h + frag)
print('history #437 appended')

p = 'docs/구현계획.md'
d = io.open(p, encoding='utf-8').read()
old = '**실제 제품 종료·실제 ProgramFiles 교체·실제 다운로드는 미수행 — 사용자 후속 실기 몫**'
new = (old + '\n'
       '- [x] step3 보강 완료 (history #437) — **PID 재사용 방어**(경로+생성시각+핸들로 신원 대조, 불일치는 "이미 사라짐") · '
       'registryRoot/serviceName 주입 강제(실제 Uninstall 키·GNLinkSecureInput 하드코딩 불가) · '
       '**모의를 실제 OS 동작으로 교체**(실제 CreateProcess 실패 · 실제 파일 잠금으로 롤백 실패 · '
       '**실제 두 번째 프로세스**로 락 경합). 98 checks PASS. 관리자 권한 실증은 미검증으로 기록')
assert old in d and '#437' not in d
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('구현계획.md updated')
