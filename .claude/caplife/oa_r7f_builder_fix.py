p = r'C:\work\gnlink-wt\old-apk-lane\automation\old_apk_lane\oa_relay_trace_server.py'
s = open(p, 'rb').read().decode('utf-8')
import re
# the two mangled replace() calls -> one line each, normalizing CRLF to LF
s, n1 = re.subn(r"diff = open\(DIFF, 'rb'\)\.read\(\)\.replace\(b'\r?\n', b'\r?\n'\)",
                "diff = open(DIFF, 'rb').read().replace(b'\\\\r\\\\n', b'\\\\n')", s)
s, n2 = re.subn(r"t = open\(TRACE_TEST, 'rb'\)\.read\(\)\.replace\(b'\r?\n', b'\r?\n'\)",
                "t = open(TRACE_TEST, 'rb').read().replace(b'\\\\r\\\\n', b'\\\\n')", s)
assert (n1, n2) == (1, 1), (n1, n2)
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
