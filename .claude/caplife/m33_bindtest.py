p = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/android_decoder_binding_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old = '''  std::puts("--- [3] B -> A -> B: the same PC with a new binding; its OLD binding's callbacks are stale too");'''
assert s.count(old) == 1
new = '''  std::puts("--- [2b] keepalive-r2 merge: the logon-screen LOCAL selection (apk-0232) is the bound PC's alone");
  {
    AndroidVideoDecoderSink dec;
    dec.SetSurface(env, surface());
    EntrySinkAdapter a(&dec), b(&dec);
    dec.Bind(301);
    a.SetToken(301);
    b.SetToken(300);  // stale
    dec.PrepareForWindowSelectionAs(301, 41);
    check("B (stale) cannot turn A's armed selection local", !b.BeginLocalSelectionFor(41));
    check("A turns its own armed selection local", a.BeginLocalSelectionFor(41));
    a.OnEncodedH264Frame(frame(77, true, 0x41));  // A's first IDR binds its local selection
    check("B reads nothing of A's local binding", b.LocalSelectionBoundFor(41) == 0 && a.LocalSelectionBoundFor(41) != 0);
    bool bCommitted = false;
    const auto bc = b.CommitLocalSelectionFor(41, [&](uint64_t) { bCommitted = true; });
    check("B's commit of A's selection: Abandoned, nothing recorded",
          bc == remote60::native_poc::ClientEncodedFrameSink::LocalCommit::Abandoned && !bCommitted);
    b.RefuseSelectionFor(41);
    check("B's refusal does not refuse A's selection", a.CurrentSelectionTag() == 41);
    uint64_t gen = 0;
    const auto ac = a.CommitLocalSelectionFor(41, [&](uint64_t g) { gen = g; });
    check("A commits its own local selection (with its generation)",
          ac == remote60::native_poc::ClientEncodedFrameSink::LocalCommit::Committed && gen == 77);
    EntrySinkAdapter unbound(&dec);  // a PC never put on screen (token 0)
    check("a PC with no binding selects nothing", !unbound.BeginLocalSelectionFor(41) && unbound.LocalSelectionBoundFor(41) == 0);
    dec.SetSurface(env, nullptr);
  }

''' + old
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
