p = 'D:/remote/remote/.claude/deletion_candidates.md'
s = open(p, 'rb').read().decode('utf-8')
bad = 'apk-prelogin-select\x07utomation\\__pycache__'
good = 'apk-prelogin-select\\automation\\__pycache__'
assert s.count(bad) == 1, s.count(bad)
s = s.replace(bad, good)
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
