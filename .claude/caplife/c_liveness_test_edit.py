p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\android_decoder_liveness_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''    check("[6] the IDR that was dropped is owed again, and a key is asked for", owed != 0 && asked);
''', '''    // apk-inbox C (design r3 §1, replacing r1's drop-the-oldest): the inbox keeps the IDR (0x8A) with the suffix that
    // fits and cuts the newest -- no IDR is lost, so nothing is owed; the tail loss opens ONE episode, which asks
    // nothing while that IDR is preserved. (r1-r5 here: the IDR was the oldest frame, dropped, owed and asked for.)
    check("[6] C: the IDR is KEPT (nothing owed); the tail loss is one episode that asks nothing while it is preserved",
          owed == 0 && !asked && field(st, "episodes") == 1 && field(st, "tailGap") == 1,
          "owed=" + std::to_string(owed) + " asked=" + std::to_string(asked ? 1 : 0) + " episodes=" +
              std::to_string(field(st, "episodes")) + " tailGap=" + std::to_string(field(st, "tailGap")));
''')
rep('''    check("[12] ...and the IDR it was is owed again, a key asked for", owedAfterBig != 0 && askedAfterBig);
''', '''    // apk-inbox C (design r3 §3): the refused IDR is owed again, and the session's owed asks (8 x 1 s) carry it -- that
    // duty IS this IDR, so the sink's recovery episode adds no second ask on top (one logical exchange, not 4 + 8).
    check("[12] ...and the IDR it was is owed again (asked through that duty; the sink adds no second ask)",
          owedAfterBig != 0 && !askedAfterBig,
          "owed=" + std::to_string(owedAfterBig) + " sink ask=" + std::to_string(askedAfterBig ? 1 : 0));
''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
