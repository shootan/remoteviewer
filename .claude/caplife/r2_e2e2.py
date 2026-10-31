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

edit('host_capture_lifecycle_ua_e2e_test.cpp', [
    ('''              pcSeen.Add(Seen{qpc_now_us(), f.captureUs, f.key});''',
     '''              pcSeen.Add(Seen{qpc_now_us(), f.captureUs, f.key, f.synthetic});'''),
    ('''  const auto host_count = [&](const std::string& needle) { return count_of(host_log(), needle); };''',
     '''  const auto host_count = [&](const std::string& needle) { return count_of(host_log(), needle); };
  // capture-lifecycle r2 (Codex N1 evidence): the desktop generation the host's LAST "<edge>" line moved to, and
  // the capture time of the first frame the host adopted from an attachment made under it ("adopted first
  // frame ... desktopGen=G captureUs=X", PublishFrame) -- 0 if none yet.
  const auto last_edge_gen = [&](const std::string& edge) -> uint64_t {
    const std::string log = host_log();
    const size_t at = log.rfind(edge);
    if (at == std::string::npos) return 0;
    const size_t g = log.find("desktopGen=", at);
    return g == std::string::npos ? 0 : std::strtoull(log.c_str() + g + 11, nullptr, 10);
  };
  const auto adopted_capture_us = [&](uint64_t gen) -> uint64_t {
    const std::string log = host_log();
    const std::string needle = "desktopGen=" + std::to_string(gen) + " captureUs=";
    size_t at = 0;
    while ((at = log.find("adopted first frame", at)) != std::string::npos) {
      const size_t eol = log.find('\\n', at);
      const std::string line = log.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
      const size_t n = line.find(needle);
      if (n != std::string::npos) return std::strtoull(line.c_str() + n + needle.size(), nullptr, 10);
      at += 1;
    }
    return 0;
  };'''),
    ('''    Seen k{};
    const bool key1 = wait_until([&] { return seen.KeyCapturedAfter(t1, &k); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE CHANGE IS DECODED (secure side)", key1,
          key1 ? std::to_string((k.us - t1) / 1000) + " ms after the edge" : std::string("none in 5 s"));''',
     '''    Seen k{};
    const bool key1 = wait_until([&] { return seen.KeyCapturedAfter(t1, &k); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE CHANGE IS DECODED (secure side; a real capture, not re-served)",
          key1, key1 ? std::to_string((k.us - t1) / 1000) + " ms after the edge" : std::string("none in 5 s"));
    {
      // r2: ...of the attachment made under the new desktop generation -- the key's capture is no earlier than
      // that attachment's first adopted frame (so it is not a frame of the desktop that was, re-labelled).
      const uint64_t gen = last_edge_gen("input desktop Default -> Winlogon");
      uint64_t adoptedUs = 0;
      wait_until([&] { return (adoptedUs = adopted_capture_us(gen)) != 0; }, 3000);
      Seen kk{};
      const bool joined = adoptedUs != 0 && wait_until([&] { return seen.KeyCapturedAfter(adoptedUs - 1, &kk); }, 3000);
      check(tag + ": ...THAT KEY IS OF THE NEW ATTACHMENT (captured at/after its first adopted frame)", joined,
            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                (joined ? " keyCaptureUs=" + std::to_string(kk.captureUs) : std::string()));
    }'''),
    ('''    Seen k2{};
    const bool key2 = wait_until([&] { return seen.KeyCapturedAfter(t2, &k2); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE RETURN IS DECODED", key2,
          key2 ? std::to_string((k2.us - t2) / 1000) + " ms after the edge" : std::string("none in 5 s"));''',
     '''    Seen k2{};
    const bool key2 = wait_until([&] { return seen.KeyCapturedAfter(t2, &k2); }, 5000);
    check(tag + ": A NEW IDR OF A CAPTURE MADE AFTER THE RETURN IS DECODED (a real capture, not re-served)", key2,
          key2 ? std::to_string((k2.us - t2) / 1000) + " ms after the edge" : std::string("none in 5 s"));
    {
      const uint64_t gen = last_edge_gen("input desktop Winlogon -> Default");
      uint64_t adoptedUs = 0;
      wait_until([&] { return (adoptedUs = adopted_capture_us(gen)) != 0; }, 3000);
      Seen kk{};
      const bool joined = adoptedUs != 0 && wait_until([&] { return seen.KeyCapturedAfter(adoptedUs - 1, &kk); }, 3000);
      check(tag + ": ...THAT KEY IS OF THE NEW ATTACHMENT (captured at/after its first adopted frame)", joined,
            "desktopGen=" + std::to_string(gen) + " adoptedCaptureUs=" + std::to_string(adoptedUs) +
                (joined ? " keyCaptureUs=" + std::to_string(kk.captureUs) : std::string()));
    }'''),
])

# ---- PS e2e: =nofirst contrast, and the recreate branch shown in the still run
edit('viewer_prelogin_login_e2e_test.cpp', [
    ('''    if (a == L"--still-screen") stillScreen = true;''',
     '''    if (a == L"--still-screen") stillScreen = true;
    if (a == L"--still-nofirst") stillNoFirst = true;  // r2: not even a new duplication's first frame'''),
    ('''  bool stillScreen = false;  // capture-lifecycle r1 (PS): a still logon screen picked after the picker''',
     '''  bool stillScreen = false;  // capture-lifecycle r1 (PS): a still logon screen picked after the picker
  bool stillNoFirst = false;  // capture-lifecycle r2: ...that gives no frame at all (the bounded reacquire gives up)'''),
    ('''                                  stillScreen ? std::wstring(L"GNLINK_STREAM_TEST_STILL_SCREEN=1") : std::wstring());''',
     '''                                  stillScreen ? std::wstring(stillNoFirst ? L"GNLINK_STREAM_TEST_STILL_SCREEN=nofirst"
                                                                          : L"GNLINK_STREAM_TEST_STILL_SCREEN=1")
                                              : std::wstring());'''),
    ('''    // The viewer started in its picker; the worker's capture comes up behind it.
    Sleep(1500);
    pick_after("A (still attached)", 3000);
    pick_after("B (after the idle detach)", 10000);
    pick_after("B2 (after the idle detach, again)", 10000);''',
     '''    // The viewer started in its picker; the worker's capture comes up behind it.
    Sleep(1500);
    if (!stillNoFirst) {
      pick_after("A (still attached)", 3000);
      pick_after("B (after the idle detach)", 10000);
      pick_after("B2 (after the idle detach, again)", 10000);
      // r2 (Codex): the full-frame reacquire ran the capture library's REAL duplication remake -- not a seam in
      // the host -- and that remake's first frame is what was delivered.
      check("PS: THE REACQUIRE RAN THE LIBRARY'S DUPLICATION REMAKE (\\"duplication made anew\\")",
            host.Count("full-frame reacquire: duplication made anew") >= 1,
            std::to_string(host.Count("full-frame reacquire: duplication made anew")) + " remakes");
      check("PS: ...and its first frame was delivered (\\"reacquire: delivered after\\")",
            host.Count("full-frame reacquire: delivered after") >= 1,
            std::to_string(host.Count("full-frame reacquire: delivered after")) + " deliveries");
    } else {
      // r2 contrast (=nofirst): the same pick with a capture that gives NOTHING, not even a remade
      // duplication's first frame. Nothing is shown (so the still run's picture came from the remake), the
      // library gives up after its bounded remakes, the host stops asking at its bound, the worker lives on.
      std::cout << "\\n--- PS contrast: a still screen that gives no frame at all ---\\n";
      ctx.control.streamState.Request(false);
      Sleep(3000);
      const uint64_t t0 = qpc_now_us();
      ctx.control.streamState.Request(true);
      Sleep(8000);
      check("PS contrast: NOTHING IS SHOWN (no frame from a capture that gives none)", frames_since(t0) == 0,
            std::to_string(frames_since(t0)) + " frames");
      const size_t remakes = host.Count("full-frame reacquire: duplication made anew");
      check("PS contrast: the library's duplication remake ran (\\"duplication made anew\\")", remakes >= 1,
            std::to_string(remakes) + " remakes");
      check("PS contrast: ...and gave up, bounded (\\"reacquire gave up after 3\\")",
            host.Count("full-frame reacquire gave up after 3") >= 1,
            std::to_string(host.Count("full-frame reacquire gave up after 3")) + " give-ups");
      const size_t asks = host.Count("full-frame ask=");
      check("PS contrast: the host's asks stop at their bound (3/3 reached, never 4)",
            host.Count("full-frame ask=3/3") >= 1 && host.Count("full-frame ask=4/") == 0, std::to_string(asks) + " asks");
      Sleep(4000);
      check("PS contrast: ...and stay stopped (no new ask in the next 4 s: no loop)", host.Count("full-frame ask=") == asks,
            std::to_string(host.Count("full-frame ask=")) + " asks now");
      check("PS contrast: the worker is still serving", worker_alive());
    }'''),
])
print('ok')
