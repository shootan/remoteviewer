p = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\native_poc\src\android_decoder_inbox_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
anchor = '  std::printf("\\nandroid_decoder_inbox_test: %d checks, %d failed\\nRESULT: %s\\n", gChecks, gFailures,'
assert s.count(anchor) == 1
block = open(r'D:\remote\remote\.claude\caplife\inbox_case6_block.cpp', encoding='utf-8').read()
s = s.replace(anchor, block + anchor)
s = s.replace('//   remote60_android_decoder_inbox_test\n',
              '// r2: [6] the queue-age metric itself (built with GNLINK_LIVENESS_TRACE): a held IDR\'s age is its own.\n'
              '//\n//   remote60_android_decoder_inbox_test\n', 1)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

c = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\native_poc\CMakeLists.txt'
raw = open(c, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')
a = 'target_compile_definitions(remote60_android_decoder_inbox_test PRIVATE NOMINMAX)'
assert s.count(a) == 1
s = s.replace(a, 'target_compile_definitions(remote60_android_decoder_inbox_test PRIVATE NOMINMAX GNLINK_LIVENESS_TRACE=1)')
if crlf:
    s = s.replace('\n', '\r\n')
open(c, 'wb').write(s.encode('utf-8'))
print('ok')
