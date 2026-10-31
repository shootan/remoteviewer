import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

edit(r'D:\remote\remote\docs\history.md', [
("2 Congested 재요청 누락=**확인**(11:20 사건 원인은 아님)",
 "2 Congested 재요청 누락=**확인**(11:20 사건을 이 결함으로 설명할 근거는 없음 — 원인 미확정)"),
])
edit(r'D:\remote\remote\.claude\windows_recovery_crosscheck.md', [
("| 2 | Congested 재요청 누락 | **확인** (단, 11:20 사건과는 무관) |",
 "| 2 | Congested 재요청 누락 | **확인** (11:20 사건을 이 결함으로 설명할 근거 없음 — 원인 미확정) |"),
("- 조건: 11:20 장기 정지의 마지막 상태는 normal 이므로 이 결함이 그 사건의 원인은 아니다. 이 결함이 설명하는 것은",
 "- 조건: 11:20 장기 정지의 마지막 관측 상태는 normal 이고 이후 관측 공백이 있어, 이 결함으로 그 사건을 설명할 근거는 없다(원인 미확정). 이 결함이 설명하는 것은"),
])
