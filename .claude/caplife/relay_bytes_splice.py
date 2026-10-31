p = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\directory\test\relay_trace_test.js'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
start = s.index("  // apk-decoder-inbox r2: the bytes that arrived")
tail = "totals[2] === 49 * totals[3], `c2h=${totals[0]}/${totals[1]} h2c=${totals[2]}/${totals[3]}`);"
end = s.index(tail) + len(tail)
new = open(r'D:\remote\remote\.claude\caplife\relay_bytes_test_block.js', encoding='utf-8').read().rstrip('\n')
s = s[:start] + new + s[end:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
