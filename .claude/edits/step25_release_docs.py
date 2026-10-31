import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'

# the verification line is filled from the sweep before this runs (history_406.md edited in place)
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_406.md')

edit(ROOT + r'\docs\작업목록.md', [
("""1.3 [~] **P11 UAC 복귀 혼잡**""",
 """1.0 [ ] **0.2.101 실기** — 설치 후 (1) 호스트 재시작→NAS host.log 재개·재로그인 뒤 401 없음 (2) 회사 클라 재로그인→viewer.log 연속 (3) UAC 2회: `inject landed target=1920x1080 mapped==in` 두 번째도 기록, 호스트 `epoch-gate dropped-old`, 뷰어 Congested 0 (4) 정적 타이핑·영상 회귀 없음 (history #406). RDP 종료 상태에서.
1.3 [~] **P11 UAC 복귀 혼잡**"""),
])
print('release docs ok')
