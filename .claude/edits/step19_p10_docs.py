import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'

edit(ROOT + r'\docs\구현계획.md', [
("""[x] 서버 세션 persist(해시+만료, 코드·테스트; **NAS 배포는 별도 지시**) [x] 실패 배치 재시도 상한(3회·60s·보관 4개, 4xx 즉시 폐기)""",
 """[-] 서버 세션 persist — **범위 제외**(Codex seq 1052: 서버 인증 수명/저장 계약은 별도 범위; f911845 의 변경은 history #403 에서 되돌림, 세션은 in-memory 계약 유지) [x] 실패 배치 재시도 상한(3회·60s·보관 4개, 4xx 즉시 폐기) [x] owner(계정+서버+장치)/로그아웃 epoch 펜싱: 늦은 401/5xx 가 새 세션을 pause 하거나 구 배치를 새 owner 로 보내지 않음 (history #403)"""),
])

edit(ROOT + r'\docs\작업목록.md', [
("""업로더 재포인팅/401 일시정지/상한 재시도/세션 persist **코드 완료 (history #401)**""",
 """업로더 재포인팅/401 일시정지/상한 재시도/owner epoch 펜싱 **코드 완료 (history #401, #403; 서버 세션 persist 는 범위 제외)**"""),
])

append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_403.md')
print('P10 follow-up docs ok')
