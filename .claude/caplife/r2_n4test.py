src = 'C:\\work\\gnlink-wt\\fix-capture-lifecycle\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = src + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('viewer_prelogin_login_e2e_test.cpp', [
    ('''      check("PS contrast: ...and stay stopped (no new ask in the next 4 s: no loop)", host.Count("full-frame ask=") == asks,
            std::to_string(host.Count("full-frame ask=")) + " asks now");
      check("PS contrast: the worker is still serving", worker_alive());''',
     '''      check("PS contrast: ...and stay stopped (no new ask in the next 4 s: no loop)", host.Count("full-frame ask=") == asks,
            std::to_string(host.Count("full-frame ask=")) + " asks now");
      // r2 (Codex N4): a NEW source -- the idle detach, then the pick's reattach -- gets its own bounded budget;
      // the spent one does not carry over (r1: it did, and the new source was never asked at all).
      ctx.control.streamState.Request(false);
      Sleep(10000);
      const int asksBefore = host.Count("full-frame ask=");
      ctx.control.streamState.Request(true);
      Sleep(8000);
      const int asksNew = host.Count("full-frame ask=") - asksBefore;
      check("PS contrast: A NEW SOURCE (idle detach -> reattach) GETS A NEW BUDGET (asks again, from 1/3)",
            asksNew >= 1 && host.Count("full-frame ask=1/3") >= 2, std::to_string(asksNew) + " new asks");
      check("PS contrast: ...bounded again (3 at most for that source, never 4)",
            asksNew <= 3 && host.Count("full-frame ask=4/") == 0, std::to_string(asksNew) + " new asks");
      check("PS contrast: the worker is still serving", worker_alive());'''),
])
print('ok')
