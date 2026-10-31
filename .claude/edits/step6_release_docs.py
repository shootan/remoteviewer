import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

PLAN = r'D:\remote\remote\docs\구현계획.md'
edit(PLAN, [
("""- [ ] **P2** NACK 재전송 — **[2026-09-07 Windows 실제 수신 경로 연결 + assembler in-order hold + 실제 경로 통합테스트 6/6 PASS (history #392); 실기 10‰ 측정만 미완(RDP)]**""",
 """- [ ] **P2** NACK 재전송 — **[2026-09-07 Windows 실제 수신 경로 연결 + assembler in-order hold(시간·개수·바이트 상한) + 실제 경로 통합테스트 12/12 PASS (history #392~#396), 0.2.100 설치본; 실기 10‰ 측정·현장 효과만 미완(RDP)]**"""),
("""- [x] **P5** 키프레임 폭주 감쇠 — **[2026-09-07 완성 IDR 앞 seq gap 은 추가 요청 없이 재동기 + Congested/keywait/무수신 시간 기준 재요청(500ms→2s backoff) — gate 테스트·실제 경로 통합테스트 PASS (history #393); 실기 미측정]**""",
 """- [x] **P5** 키프레임 폭주 감쇠 — **[2026-09-07 완성 IDR 앞 seq gap 은 추가 요청 없이 재동기 + Congested/keywait/무수신 시간 기준 재요청(500ms→2s backoff, NACK 진행 중 유예 상한 300ms) — gate 테스트·실제 경로 통합테스트 PASS (history #393, #396), 0.2.100; 실기 미측정]**"""),
])
p = sys.argv[1]
verify = sys.argv[2]
t = open(p, 'rb').read().decode('utf-8')
assert '- 검증: (아래 결과)' in t
t = t.replace('- 검증: (아래 결과)', '- 검증: ' + verify)
open(p, 'wb').write(t.encode('utf-8'))
append_history(r'D:\remote\remote\docs\history.md', p)
