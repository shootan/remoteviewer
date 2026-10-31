import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from append_history import append_history
p = sys.argv[1]
verify = sys.argv[2]
t = open(p, 'rb').read().decode('utf-8')
assert '- 검증: (아래 결과)' in t
t = t.replace('- 검증: (아래 결과)', '- 검증: ' + verify)
open(p, 'wb').write(t.encode('utf-8'))
append_history(r'D:\remote\remote\docs\history.md', p)
