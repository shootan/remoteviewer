import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_409_s13.md')

edit(ROOT + r'\docs\구현계획.md', [
("""[x] 뷰어: 유효 stamp(send≥capture)에서 hold>300ms 인 재개 프레임은 anchor 아님(pending 8프레임/500ms 상한·같은 episode 재개 프레임으로 연장 없음·키프레임 wait/decode/상태 전환 시 종료, 진짜 backlog 유지) — 오탐 IDR 비용 감소이며 오래된 픽셀의 실제 지연은 남음""",
 """[x] 뷰어: 유효 stamp(send≥capture)에서 hold>300ms 인 재개 프레임은 anchor 아님(pending 8프레임/500ms 상한·같은 episode 재개 프레임으로 연장 없음; 종료는 admit 에서만 — fresh anchor·예산·상태 전환·키프레임 wait, decode 자체는 종료 안 함·held/미상 IDR 은 유지(history #409); 진짜 backlog 유지) — 오탐 IDR 비용 감소이며 오래된 픽셀의 실제 지연은 남음"""),
])
print('s13 docs ok')
