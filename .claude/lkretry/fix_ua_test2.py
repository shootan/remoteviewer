p = r'C:\work\gnlink-wt\fix-capture-lifecycle\apps\native_poc\src\host_capture_lifecycle_ua_e2e_test.cpp'
s = open(p, encoding='utf-8').read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:90]
    s = s.replace(old, new)

rep('''    Seen k{};
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE CHANGE IS DECODED (secure side)",
          wait_until([&] { return seen.KeyCapturedAfter(t1, &k); }, 5000),
          k.us ? std::to_string((k.us - t1) / 1000) + " ms after the edge" : std::string("none in 5 s"));''',
    '''    Seen k{};
    const bool key1 = wait_until([&] { return seen.KeyCapturedAfter(t1, &k); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE CHANGE IS DECODED (secure side)", key1,
          key1 ? std::to_string((k.us - t1) / 1000) + " ms after the edge" : std::string("none in 5 s"));''')
rep('''    Seen k2{};
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE RETURN IS DECODED",
          wait_until([&] { return seen.KeyCapturedAfter(t2, &k2); }, 5000),
          k2.us ? std::to_string((k2.us - t2) / 1000) + " ms after the edge" : std::string("none in 5 s"));''',
    '''    Seen k2{};
    const bool key2 = wait_until([&] { return seen.KeyCapturedAfter(t2, &k2); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE RETURN IS DECODED", key2,
          key2 ? std::to_string((k2.us - t2) / 1000) + " ms after the edge" : std::string("none in 5 s"));''')
rep('''    check("...and decodes the desktop", wait_until([&] { return sink.decoded > 0; }, 15000),
          std::to_string(sink.decoded) + " decoded, " + std::to_string(sink.failed) + " failed");''',
    '''    const bool decodes = wait_until([&] { return sink.decoded > 0; }, 15000);
    check("...and decodes the desktop (the product decoder)", decodes,
          std::to_string(sink.decoded) + " decoded, " + std::to_string(sink.failed) + " failed");''')
rep('''    check("UA-1 WGC: SAME CONNECTION throughout -- the session never left Connected, one session at the host",
          phone.Snapshot().state == ClientSessionState::Connected && host_count("stream restored for new session") <= 1,
          std::to_string(host_count("stream restored for new session")) + " session start(s)");''',
    '''    check("UA-1 WGC: SAME CONNECTION throughout -- the session never left Connected; one session (epoch) at the host",
          phone.Snapshot().state == ClientSessionState::Connected && host_count("[control] session epoch=") == 1,
          std::to_string(host_count("[control] session epoch=")) + " session epoch(s)");''')
rep('''    check("UA-2: ...and the PC viewer shows the desktop (decoded frames)", wait_until([&] { return pcSeen.CountSince(0) > 0; }, 15000),
          std::to_string(pcSeen.CountSince(0)) + " frames");''',
    '''    const bool pcFrames = wait_until([&] { return pcSeen.CountSince(0) > 0; }, 15000);
    check("UA-2: ...and the PC viewer shows the desktop (decoded frames)", pcFrames, std::to_string(pcSeen.CountSince(0)) + " frames");''')
rep('''      check("UA-1 DXGI: SAME CONNECTION throughout -- two sessions at the host in all (the phone's, this one)",
            host_count("stream restored for new session") <= 2,
            std::to_string(host_count("stream restored for new session")) + " session start(s)");''',
    '''      check("UA-1 DXGI: SAME CONNECTION throughout -- two sessions (epochs) at the host in all: the phone's, this one",
            host_count("[control] session epoch=") == 2,
            std::to_string(host_count("[control] session epoch=")) + " session epoch(s)");''')
rep('''      for (const char* key : {"[desktop]", "desktop-backend", "fallback_reason", "full-frame", "test-seam", "stream restored",''',
    '''      for (const char* key : {"[desktop]", "desktop-backend", "fallback_reason", "full-frame", "test-seam", "session epoch=",''')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
