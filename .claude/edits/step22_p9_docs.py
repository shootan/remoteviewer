import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'
edit(ROOT + r'\docs\구현계획.md', [
("""[x] 캡처 (재)시작마다 실제로 연 모니터의 물리 rect+원점으로 target rect 재도출·변경 시 재푸시+로그 `secure-input target rect=` (`host_input_target_rect.hpp`, `sync_input_target_rect`; 창모드 0x0; WM_DISPLAYCHANGE 별도 훅 없이 재시작 경로가 RDP↔콘솔·백엔드·모니터 전환을 모두 지남)""",
 """[x] 캡처 (재)시작마다 + 1초 tick 마다 실제로 연 HMONITOR 의 live 물리 rect+원점으로 target rect 재도출·변경 시 재푸시+로그 `secure-input target rect=` (`host_input_target_rect.hpp`, `sync_input_target_rect`; 창모드 0x0; 같은 해상도에서 원점만 바뀌는 경우도 periodic 경로가 잡음, history #404)"""),
])
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_404.md')
print('P9 follow-up docs ok')
