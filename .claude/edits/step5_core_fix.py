# Replace the tail of step5_core.py (the include insertion) with a plain version.
p = r'D:\remote\remote\.claude\edits\step5_core.py'
t = open(p, 'rb').read().decode('utf-8')
marker = "import re\np = SRC + r'\\viewer_startup.cpp'"
idx = t.find(marker)
assert idx > 0, 'marker not found'
tail = '''import re
p = SRC + r'\\viewer_startup.cpp'
t = open(p, 'rb').read().decode('utf-8')
if '#include "viewer_udp_session.hpp"' not in t:
    anchor = '#include "viewer_startup.hpp"\\n'
    i = t.find(anchor)
    assert i >= 0, 'viewer_startup.cpp: no self include'
    i += len(anchor)
    t = t[:i] + '#include "viewer_udp_session.hpp"\\n' + t[i:]
    open(p, 'wb').write(t.encode('utf-8'))
    print('ok include added to viewer_startup.cpp')
'''
t = t[:idx] + tail
open(p, 'wb').write(t.encode('utf-8'))
print('step5_core.py tail rewritten')
