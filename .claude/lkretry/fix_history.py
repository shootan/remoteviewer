p = r'C:\work\gnlink-wt\fix-lk-retry\docs\history.md'
b = open(p, 'rb').read().decode('utf-8')
import re
m = re.search('6회 연속 통과\\(첫 회 1 FAIL 은 시험의 줄 끝[ \\r\\n]+ 가정 — 시험 수정\\)\\.', b)
assert m, 'not found'
b = b[:m.start()] + '6회 연속 통과(첫 회 1 FAIL 은 시험이 로그 줄 끝을 LF 로 가정한 것 — 시험 수정).' + b[m.end():]
open(p, 'wb').write(b.encode('utf-8'))
print('fixed')
