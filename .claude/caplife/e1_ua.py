p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\host_capture_lifecycle_ua_e2e_test.cpp'
NL = chr(92) + 'n'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''    if (std::wstring(argv[i]) == L"--fullframe-fails") fullFrameFails = true;''',
    '''    if (std::wstring(argv[i]) == L"--fullframe-fails") fullFrameFails = true;
    if (std::wstring(argv[i]) == L"--render") renderE1 = true;
    if (std::wstring(argv[i]) == L"--shots" && i + 1 < argc) shotsDir = argv[++i];''')
rep('''  bool fullFrameFails = false;  // r4''', '''  bool fullFrameFails = false;  // r4
  bool renderE1 = false;        // rel-0163 r2 E1: the PC viewer's PRODUCT renderer across the edges
  std::wstring shotsDir;''')
rep('''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_WGC_STILL", fullFrameFails ? L"1" : nullptr);''',
    '''    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_WGC_STILL", fullFrameFails ? L"1" : nullptr);
    // rel-0163 r2 E1: the host is on WGC from its start (the connection is "already WGC").
    SetEnvironmentVariableW(L"REMOTE60_DESKTOP_CAPTURE_BACKEND", renderE1 ? L"wgc" : nullptr);''')

block = r'''  // ------------------------------------------------------------------ rel-0163 r2 E1: the product renderer
  // The PC viewer as the product draws it: its window procedure (WndProc) -> request_video_paint -> WM_PAINT ->
  // paint_video_frame -> the D3D swap chain's Present (ctx.present.d3dPresentSuccessCount;
  // ctx.frameBuf.lastPresentedCaptureUs is set THERE, for a presented non-synthetic frame). The capture target is
  // a window of this test whose colour changes at every edge, so each transition's picture is told apart by
  // what is SHOWN. Both windows are OFF every monitor (PrintWindow, PW_RENDERFULLCONTENT): the user's screen is
  // neither covered nor touched. The host runs WGC from its start; the connection is one throughout.
  if (launched && renderE1) {
    std::cout << "\n--- E1: the PC viewer's product renderer across secure <-> Default, twice ---\n";
    using namespace remote60::native_poc::viewer;
    static COLORREF markerColor = RGB(220, 30, 30);
    const int offX = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 200;
    WNDCLASSEXW mc{};
    mc.cbSize = sizeof(mc);
    mc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
      if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        RECT rc{};
        GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush(markerColor);
        FillRect(dc, &rc, b);
        DeleteObject(b);
        EndPaint(h, &ps);
        return 0;
      }
      return DefWindowProcW(h, m, w, l);
    };
    mc.hInstance = GetModuleHandleW(nullptr);
    mc.lpszClassName = L"GNLinkE1Marker";
    RegisterClassExW(&mc);
    HWND marker = CreateWindowExW(0, mc.lpszClassName, L"GNLink E1 marker", WS_OVERLAPPEDWINDOW, offX, 620, 640, 400,
                                  nullptr, nullptr, mc.hInstance, nullptr);
    ShowWindow(marker, SW_SHOWNOACTIVATE);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = remote60::native_poc::viewer::WndProc;  // the product's
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GNLinkE1Viewer";
    RegisterClassExW(&wc);
    HWND viewer = CreateWindowExW(0, wc.lpszClassName, L"GNLink E1 viewer", WS_OVERLAPPEDWINDOW, offX, 40, 800, 500,
                                  nullptr, nullptr, wc.hInstance, &ctx);
    ShowWindow(viewer, SW_SHOWNOACTIVATE);
    ctx.session.hwnd = viewer;
    const RECT vs{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                  GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                  GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    RECT vr{}, mr{}, ov{};
    GetWindowRect(viewer, &vr);
    GetWindowRect(marker, &mr);
    check("E1: the viewer window and the test's target window are OFF every monitor (nothing covers the user's screen)",
          viewer && marker && !IntersectRect(&ov, &vr, &vs) && !IntersectRect(&ov, &mr, &vs));
    const auto pump_until = [&](const std::function<bool()>& done, int budgetMs) {
      const DWORD deadline = GetTickCount() + static_cast<DWORD>(budgetMs);
      for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
        if (done()) return true;
        if (GetTickCount() >= deadline) return done();
        Sleep(10);
      }
    };
    struct Shot {
      bool ok = false;
      double share = 0;  // of the centre half: pixels within 48 of the expected colour per channel
      int w = 0, h = 0;
    };
    const auto capture = [&](const std::wstring& name, COLORREF expect) {
      Shot sh;
      RECT rc{};
      GetClientRect(viewer, &rc);
      sh.w = rc.right - rc.left;
      sh.h = rc.bottom - rc.top;
      if (sh.w <= 0 || sh.h <= 0) return sh;
      HDC wdc = GetDC(viewer);
      HDC mem = CreateCompatibleDC(wdc);
      BITMAPINFO bi{};
      bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
      bi.bmiHeader.biWidth = sh.w;
      bi.bmiHeader.biHeight = -sh.h;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      void* bits = nullptr;
      HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
      HGDIOBJ old = SelectObject(mem, bmp);
      const BOOL printed = PrintWindow(viewer, mem, PW_CLIENTONLY | 0x2 /* PW_RENDERFULLCONTENT */);
      SelectObject(mem, old);
      if (printed && bits) {
        const auto* px = static_cast<const uint32_t*>(bits);
        size_t hit = 0, all = 0;
        const int er = GetRValue(expect), eg = GetGValue(expect), eb = GetBValue(expect);
        for (int y = sh.h / 4; y < sh.h * 3 / 4; ++y) {
          for (int x = sh.w / 4; x < sh.w * 3 / 4; ++x) {
            const uint32_t c = px[y * sh.w + x];
            const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            ++all;
            if (std::abs(r - er) <= 48 && std::abs(g - eg) <= 48 && std::abs(b - eb) <= 48) ++hit;
          }
        }
        sh.share = all ? static_cast<double>(hit) / all : 0;
        sh.ok = true;
        if (!shotsDir.empty()) {
          BITMAPFILEHEADER fh{};
          const DWORD imageBytes = static_cast<DWORD>(sh.w) * sh.h * 4;
          fh.bfType = 0x4D42;
          fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
          fh.bfSize = fh.bfOffBits + imageBytes;
          std::ofstream out(shotsDir + L"\\" + name, std::ios::binary);
          out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
          out.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
          out.write(static_cast<const char*>(bits), imageBytes);
        }
      }
      DeleteObject(bmp);
      DeleteDC(mem);
      ReleaseDC(viewer, wdc);
      return sh;
    };
    const auto pct = [](double v) {
      char b[32];
      std::snprintf(b, sizeof(b), "%.0f%%", v * 100.0);
      return std::string(b);
    };
    const auto presents = [&] { return ctx.present.d3dPresentSuccessCount.load(std::memory_order_relaxed); };
    const auto presentedCaptureUs = [&] { return ctx.frameBuf.lastPresentedCaptureUs.load(std::memory_order_relaxed); };

    // The PC viewer, as UA-2 sets it up -- but with the product window and no stand-in present thread.
    args.codec = "h264";
    args.transport = "udp";
    args.fpsHint = 30;
    args.udpMtu = 1200;
    args.inputLogEvery = 0;
    dec.useH264 = true;
    dec.transport = VideoTransport::Udp;
    gate.catchupReenterMinIntervalUs = kCatchupReenterMinIntervalUsDefault;
    gate.staleCaptureDropUs = kStaleCaptureDropUs;
    gate.staleReferenceRecoveryMinIntervalUs = kStaleRecoveryMinIntervalUsDefault;
    gate.congestionRecoverMinUs = kCongestionRecoverMinUsDefault;
    gate.congestionRecoveryTimeoutUs = kCongestionRecoveryTimeoutUsDefault;
    gate.decodeQueueLagDropUs = kDecodeQueueLagDropUs;
    gate.catchupLagDropUs = kCatchupLagDropUs;
    gate.denseArrivalMaxGapUs = kDenseArrivalMaxGapUsDefault;
    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;
    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;
    gate.recoveryRetryDeferMaxUs = kKeyRecoveryDeferMaxUsDefault;
    gate.waitForKeyFrame = true;
    ctx.picker.visible.store(false);
    ctx.control.keyframeRequests.Reset();
    ctx.session.running.store(true);
    ctx.session.controlRequired = true;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(hostPort);
    connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    const int recvBuf = 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    UdpHelloOptions hello = viewer_udp_hello_options(std::string(), /*requestNack=*/true);
    hello.budgetMs = 20000;
    uint32_t ackFeatures = 0;
    std::string helloError;
    const bool helloOk = udp_hello_handshake(s, hello, nullptr, &helloError, &ackFeatures);
    check("E1: the PC viewer's Hello is answered", helloOk, helloError);
    (void)viewer_arm_udp_recv_timeout(s, 25);
    ctx.session.sock = s;
    viewer_apply_udp_hello_ack(ackFeatures, true, ctx.session);
    ctx.control.udpControl.Configure(
        [s](const void* data, size_t len) { return send(s, static_cast<const char*>(data), static_cast<int>(len), 0) > 0; },
        kUdpControlStreamClientToHost, kUdpControlStreamHostToClient, args.udpMtu);
    ctx.control.overUdp.store(true, std::memory_order_release);
    ctx.control.clipboard.enabled.store(false);
    if (helloOk) {
      VideoReceiver::NackOptions nack;
      nack.enabled = ctx.session.hostSupportsNack;
      nack.holdUs = 120000;
      receiver.emplace(ctx, args, dec, gate, qpc_now_us(), 0, 0, nack);
      recvThread = std::thread([&] { receiver->Run(); });
      control.emplace(ctx, args, /*startInPicker=*/false);
      ctx.control.scheduler.Reset(kClientControlIntervalMsDefault, qpc_now_us());
      ctx.control.connected.store(true, std::memory_order_relaxed);
      controlThread = std::thread([&] { control->Run(); });
      queue_window_list_request(ctx, "window_list_request pending");  // what viewer_startup.cpp does
      const uint64_t markerId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(marker));
      const bool listed = pump_until([&] {
        const auto snap = ctx.picker.windowPanel.Snapshot();
        for (const auto& it : snap.items) {
          if (it.id == markerId) return true;
        }
        return false;
      }, 10000);
      check("E1: the host lists the test's target window", listed);
      // The product's own pick (what the picker's press calls): the window-select transaction, then the reveal.
      const bool begun = listed && begin_pc_target_selection(ctx, markerId, "window_select_requested");
      const uint64_t presentsAtPick = presents();
      const bool shown = begun && pump_until([&] {
        return !ctx.picker.visible.load() && ctx.sel.activeStreamGeneration.load() != 0 && presents() > presentsAtPick &&
               presentedCaptureUs() != 0;
      }, 8000);
      check("E1: the window is picked and the PRODUCT RENDERER presents it (d3dPresentSuccess moves)", shown,
            "presents " + std::to_string(presentsAtPick) + " -> " + std::to_string(presents()));
      pump_until([] { return false; }, 600);
      const Shot first = capture(L"E1-UA-0-red.bmp", RGB(220, 30, 30));
      check("E1: screenshot 0 -- the window shows the target's colour (red) before any edge", first.ok && first.share > 0.8,
            pct(first.share) + " red");

      struct Phase {
        bool secure;
        COLORREF color;
        const wchar_t* shot;
        const char* name;
        const char* edge;
      };
      const Phase phases[] = {
          {true, RGB(30, 200, 60), L"E1-UA-1-secure-green.bmp", "#1 secure (green)", "input desktop Default -> Winlogon"},
          {false, RGB(30, 80, 230), L"E1-UA-2-default-blue.bmp", "#1 Default (blue)", "input desktop Winlogon -> Default"},
          {true, RGB(230, 200, 30), L"E1-UA-3-secure-yellow.bmp", "#2 secure (yellow)", "input desktop Default -> Winlogon"},
          {false, RGB(200, 40, 200), L"E1-UA-4-default-magenta.bmp", "#2 Default (magenta)", "input desktop Winlogon -> Default"},
      };
      COLORREF previous = RGB(220, 30, 30);
      for (const Phase& ph : phases) {
        const size_t edgesBefore = host_count(ph.edge);
        markerColor = ph.color;  // the target changes at the edge: what is shown after it can only be new
        InvalidateRect(marker, nullptr, TRUE);
        UpdateWindow(marker);
        if (ph.secure) SetEvent(secure); else ResetEvent(secure);
        const bool edge = pump_until([&] { return host_count(ph.edge) > edgesBefore; }, 4000);
        const uint64_t gen = edge ? last_edge_gen(ph.edge) : 0;
        const bool fence = edge && pump_until([&] {
          return host_log().find("edge fence desktopGen=" + std::to_string(gen) + " ") != std::string::npos;
        }, 2000);
        const bool rebuilt = edge && pump_until([&] {
          return host_log().find("rebuild desktopGen=" + std::to_string(gen) + " attempt=1 ok=1") != std::string::npos;
        }, 4000);
        uint64_t adoptedUs = 0;
        pump_until([&] { return (adoptedUs = adopted_capture_us(gen)) != 0; }, 4000);
        const bool newShown = adoptedUs != 0 && pump_until([&] { return presentedCaptureUs() >= adoptedUs; }, 5000);
        check((std::string("E1 ") + ph.name + ": the host saw the edge, fenced it and rebuilt the capture (same connection)").c_str(),
              edge && fence && rebuilt, "desktopGen=" + std::to_string(gen));
        check((std::string("E1 ") + ph.name + ": THE PRODUCT RENDERER PRESENTED A FRAME OF THE NEW ATTACHMENT").c_str(), newShown,
              "adoptedCaptureUs=" + std::to_string(adoptedUs) + " presentedCaptureUs=" + std::to_string(presentedCaptureUs()));
        pump_until([] { return false; }, 400);
        const Shot sh = capture(ph.shot, ph.color);
        const Shot oldSh = capture(L"E1-UA-tmp.bmp", previous);
        check((std::string("E1 ") + ph.name + ": SCREENSHOT -- the window shows the NEW colour, not the previous one").c_str(),
              sh.ok && sh.share > 0.8 && oldSh.share < 0.05, pct(sh.share) + " new, " + pct(oldSh.share) + " previous");
        // And it stays the new picture: sampled for 1.5 s, the previous colour never comes back.
        bool stale = false;
        for (int k = 0; k < 6; ++k) {
          pump_until([] { return false; }, 250);
          const Shot again = capture(L"E1-UA-tmp.bmp", previous);
          if (again.ok && again.share > 0.05) stale = true;
        }
        check((std::string("E1 ") + ph.name + ": ...and the previous picture never comes back (6 samples over 1.5 s)").c_str(),
              !stale);
        previous = ph.color;
      }
      DeleteFileW((shotsDir + L"\\E1-UA-tmp.bmp").c_str());
      check("E1: ONE connection throughout (one session at the host), on WGC",
            host_count("backend-owner epoch=") == 1 && host_log().find("active=wgc") != std::string::npos,
            std::to_string(host_count("backend-owner epoch=")) + " session(s)");
      check("E1: no present failed", ctx.present.d3dPresentFailCount.load() == 0);
      pcUp = true;
    }
    if (viewer) DestroyWindow(viewer);
    if (marker) DestroyWindow(marker);
  }

'''
anchor = '  // ------------------------------------------------------------------ UA-2 + UA-1 (DXGI): the PC viewer'
# the E1 block needs ctx/args/dec/gate/receiver/control/threads declared: insert it right after those declarations
decl_end = '''  SeenLog pcSeen;
  bool pcUp = false;
'''
rep(decl_end, decl_end + block)
rep('''  if (launched && !rebuildFails && !fullFrameFails) {''', '''  if (launched && !rebuildFails && !fullFrameFails && !renderE1) {''')
rep('''  if (launched && !rebuildFails && !fullFrameFails && host_alive()) {''', '''  if (launched && !rebuildFails && !fullFrameFails && !renderE1 && host_alive()) {''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
