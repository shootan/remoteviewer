p = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\native_poc\src\android_decoder_liveness_test.cpp'
N = '\r\n'
s = open(p, 'rb').read().decode('utf-8')
a = '''    const size_t q0 = snap().queued;
    sink.OnEncodedH264Frame(frame(0, true, 0x8D));
    check("[7] the next IDR makes a codec and is queued",
          wait_until([&] { return !queued_from(q0).empty() && queued_from(q0).front().first == 0x8D; }, 2000));'''.replace('\n', N)
b = '''    // r5: after a failure nothing tries before the shared deadline (1 s) -- then the worker's own try makes the
    // codec, and the next IDR goes in
    const bool made = wait_until([&] { return sink.DebugStatus().find(" codec=on") != std::string::npos; }, 2500);
    const size_t q0 = snap().queued;
    sink.OnEncodedH264Frame(frame(0, true, 0x8D));
    check("[7] the codec is made again by the retry (<= 2.5 s), and the next IDR is queued",
          made && wait_until([&] { return !queued_from(q0).empty() && queued_from(q0).front().first == 0x8D; }, 2000));'''.replace('\n', N)
assert s.count(a) == 1
s = s.replace(a, b)
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
