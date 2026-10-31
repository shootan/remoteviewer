p = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src\ka_pc_managed_e2e_test.cpp'
b = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in b
s = b.replace('\r\n', '\n')

def rep(a, c, cnt=1):
    global s
    assert s.count(a) == cnt, (s.count(a), a[:80])
    s = s.replace(a, c)

# (3) the other device keeps presenting through the standby window
rep('''    const std::string before = hostA.Text();
    const size_t standbyPingsSeen = a.Count();
    Sleep(12000);  // two Ping periods and more, the kept PC standing by
    const std::string during = hostA.Text().substr(before.size());''',
    '''    const std::string before = hostA.Text();
    const size_t standbyPingsSeen = a.Count();
    auto presents = [&other] {
      const std::string t = other.Text();
      size_t n = 0, p = 0;
      while ((p = t.find("[present]", p)) != std::string::npos) ++n, ++p;
      return n;
    };
    const size_t presentsBefore = presents();
    Sleep(12000);  // two Ping periods and more, the kept PC standing by
    const size_t presentsAfter = presents();
    check("...the other device kept presenting through those 12 s", presentsAfter > presentsBefore + 5,
          std::to_string(presentsBefore) + " -> " + std::to_string(presentsAfter) + " present lines");
    const std::string during = hostA.Text().substr(before.size());''')

# (2) the shipped first conversion: an ordinary session ends, then at once a managed viewer for that PC (a key
# never kept before: a fresh device id) -- counted, it is the path the shipped switch takes
anchor = '  // ---------------------------------------------------------------- the message fence\n'
conv = r'''  // ---------------------------------------------------------------- the first conversion, as shipped
  // The shell keeps a PC when the user closes a connected ORDINARY session (kKeepOnListReturn): the managed viewer
  // starts seconds after that session ended, while the Host still counts it (10 s control silence). The key is one
  // never kept before (a fresh device id), as for a PC kept the first time.
  {
    Child ord;
    const std::wstring odir = gRoot + L"viewer-ordinary\\";
    CreateDirectoryW(odir.c_str(), nullptr);
    Managed::Add_offscreen();
    const auto oenv = env_with(odir + L"lad", {});
    ord.logPath = odir + L"viewer.log";
    // the kept PC A must not hold the Host for this: it is parked (ready) here
    const bool started = ord.Start(L"\"" + gBuild + L"GNLinkViewerKaTest.exe\" --transport udp --codec h264 --enable-input-channel"
                                   L" --initial-view stream --directory-url \"" + W(url) + L"\" --directory-session \"" + W(session) +
                                   L"\" --directory-host-id \"" + W(idA) + L"\" --seconds 6",
                                   gBuild, &oenv);
    const bool connected = started && ord.WaitFor([](const std::string& t) { return t.find("connected host=") != std::string::npos; }, 20000);
    check("an ordinary session connects (the user's explicit connect)", connected);
    check("...and ends (the user closes it)", ord.Exited(30000));
    Managed f;
    f.key = url + "\ntester-first-conversion\n" + idA;  // never kept before: a new device id
    f.inc = "1";
    f.epoch = "5";
    const uint64_t t0 = now_ms();
    check("at once, the managed viewer of that PC starts (the first conversion)", f.Start(url, idA, "FIRST"));
    f.ch.Send([&] {
      Message m;
      m.type = Type::Init;
      m.fields = {{"url", url}, {"session", session}, {"tokenVersion", "1"}, {"hostId", idA}, {"hostName", "KA-A"},
                  {"key", f.key}, {"inc", f.inc}, {"epoch", f.epoch}, {"genDir", N(gRoot) + "viewer-FIRST\\standby"}};
      return m;
    }());
    const int r = f.WaitMsg(Type::State, 0, 45000, [](const Message& m) {
      return m.get("state") == "ready" || m.get("state") == "unsupported";
    });
    const std::string got = r >= 0 ? f.At(r).get("state") : std::string("none in 45 s");
    check("...and it is ready (not 'unsupported') -- the shipped list-return keep", got == "ready",
          got + " after " + std::to_string(now_ms() - t0) + " ms");
    f.Send(Type::Release, {{"reason", "test"}});
    f.proc.Exited(8000);
  }

'''
rep(anchor, conv + anchor)
open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))
print('ok')
