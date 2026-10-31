import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_410_release.md')

edit(ROOT + r'\docs\작업목록.md', [
("""1.0 [ ] **0.2.102 실기**""",
 """1.0 [ ] **0.2.103 실기**"""),
("""(4) 정적 타이핑·영상 회귀 없음 (history #406). RDP 종료 상태에서.""",
 """(4) 정적 타이핑·영상 회귀 없음 (history #406, #409, #410). RDP 종료 상태에서. 설치본 `dist/GNLinkSetup-0.2.103.exe` (검증용 OK 후 설치)."""),
("""0.2.102 설치본·UAC 실기 대기; secure 중 kick 차단 보유는 별도 설계(뷰어 보정은 오탐 IDR 비용 감소일 뿐)""",
 """0.2.103 설치본·UAC 실기 대기(S13 held-resume pending 수정 history #409); secure 중 kick 차단 보유는 별도 설계(뷰어 보정은 오탐 IDR 비용 감소일 뿐)"""),
("""**코드 완료 (history #402, #404, #407: dispatch 직전 live rect 재확인·조회 실패 시 거부)**, 0.2.102 설치본·UAC 실기 대기""",
 """**코드 완료 (history #402, #404, #407, #409 선행 기록 1287883: versioned snapshot·단일 dispatch 경로)**, 0.2.103 설치본·UAC 실기 대기"""),
])
print('release docs ok')
