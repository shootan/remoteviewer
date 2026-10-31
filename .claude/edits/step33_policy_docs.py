import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from append_history import append_history
ROOT = r'D:\remote\remote'
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_408_policy.md')
print('policy docs history ok')
