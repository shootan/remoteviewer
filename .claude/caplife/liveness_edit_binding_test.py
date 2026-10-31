import importlib.util, sys
spec = importlib.util.spec_from_file_location('e', 'D:/remote/remote/.claude/caplife/liveness_edit_hpp.py')
P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/android_decoder_binding_test.cpp'


def edit(path, reps):
    s = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (a[:90], n)
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(s)
    print('ok', path)


edit(P, [
    ('''ControlWindowSelectedMessage answer(uint64_t gen) {''',
     '''// apk-decoder-liveness r1: the codec is the sink's worker's -- what reached it is read once the worker has taken
// everything handed over before (WaitIdleForTest, test build only).
void settle(AndroidVideoDecoderSink& d) {
  if (!d.WaitIdleForTest(5000)) check("the decoder's worker settled within 5 s", false);
}
ControlWindowSelectedMessage answer(uint64_t gen) {'''),
    ('''    a.OnEncodedH264Frame(frame(7, false, 0x21));
    check("A's IDR and delta are in the codec", last_queued(0x21));
    const size_t q0 = queued();''', '''    a.OnEncodedH264Frame(frame(7, false, 0x21));
    settle(dec);
    check("A's IDR and delta are in the codec", last_queued(0x21));
    const size_t q0 = queued();'''),
    ('''    const bool bUnbound = dec.Unbind(tokB);       // and B "releasing" the decoder
    check("B's late frame is not queued", queued() == q0);''', '''    const bool bUnbound = dec.Unbind(tokB);       // and B "releasing" the decoder
    settle(dec);
    check("B's late frame is not queued", queued() == q0);'''),
    ('''    a.OnEncodedH264Frame(frame(7, false, 0x22));
    check("A's next DELTA still goes in''', '''    a.OnEncodedH264Frame(frame(7, false, 0x22));
    settle(dec);
    check("A's next DELTA still goes in'''),
    ('''    a.OnEncodedH264Frame(frame(77, true, 0x41));  // A's first IDR binds its local selection
''', '''    a.OnEncodedH264Frame(frame(77, true, 0x41));  // A's first IDR binds its local selection
    settle(dec);
'''),
    ('''    b.OnEncodedH264Frame(frame(4, true, 0x42));
    const size_t q0 = queued();''', '''    b.OnEncodedH264Frame(frame(4, true, 0x42));
    settle(dec);
    const size_t q0 = queued();'''),
    ('''    b.OnEncodedH264Frame(frame(4, false, 0x44));
    check(''', '''    b.OnEncodedH264Frame(frame(4, false, 0x44));
    settle(dec);
    check('''),
    ('''    dec.OnEncodedH264Frame(frame(7, true, 0x50));  // A, through the decoder directly
    dec.OnVideoStreamReset();                       // B's late teardown, through the same decoder
    const size_t q0 = queued();
    dec.OnEncodedH264Frame(frame(7, false, 0x51));  // A's next delta
''', '''    dec.OnEncodedH264Frame(frame(7, true, 0x50));  // A, through the decoder directly
    settle(dec);
    dec.OnVideoStreamReset();                       // B's late teardown, through the same decoder
    settle(dec);
    const size_t q0 = queued();
    dec.OnEncodedH264Frame(frame(7, false, 0x51));  // A's next delta
    settle(dec);
'''),
])
