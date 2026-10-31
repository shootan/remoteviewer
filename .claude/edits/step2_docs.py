import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

PLAN = r'D:\remote\remote\docs\구현계획.md'
edit(PLAN, [
("""- [ ] **P5** 키프레임 폭주 감쇠 — **[완성 IDR 중복 요청 진단 완료·구현 전 교차 확인 대기]** **2026-09-02 실기 발견 (history #337)**""",
 """- [x] **P5** 키프레임 폭주 감쇠 — **[2026-09-07 완성 IDR 앞 seq gap 은 추가 요청 없이 재동기 + Congested/keywait/무수신 시간 기준 재요청(500ms→2s backoff) — gate 테스트·실제 경로 통합테스트 PASS (history #393); 실기 미측정]** **2026-09-02 실기 발견 (history #337)**"""),
])
append_history(r'D:\remote\remote\docs\history.md', sys.argv[1])
