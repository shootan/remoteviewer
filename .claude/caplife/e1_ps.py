p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_prelogin_login_e2e_test.cpp'
NL = chr(92) + 'n'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''  bool stillPointerFirst = false;  // capture-lifecycle r3: ...whose remade duplication's first acquire is pointer-only''',
    '''  bool stillPointerFirst = false;  // capture-lifecycle r3: ...whose remade duplication's first acquire is pointer-only
  bool renderE1 = false;  // rel-0163 r2 E1: --still-screen through the PRODUCT renderer (window procedure -> D3D present)
  std::wstring shotsDir;  // ...where its window captures go (--shots <dir>)''')
rep('''    if (a == L"--still-pointerfirst") stillPointerFirst = true;  // r3: a remade duplication's first acquire is pointer-only''',
    '''    if (a == L"--still-pointerfirst") stillPointerFirst = true;  // r3: a remade duplication's first acquire is pointer-only
    if (a == L"--render") renderE1 = true;
    if (a == L"--shots" && i + 1 < argc) shotsDir = argv[++i];''')

# the window: off every monitor in E1 (never over the user's screen)
rep('''    viewerWindow = CreateWindowExW(0, wc.lpszClassName, L"remote60 prelogin login viewer", WS_OVERLAPPEDWINDOW, 500, 40,
                                   800, 500, nullptr, nullptr, wc.hInstance, &ctx);''',
    '''    // rel-0163 r2 E1: the viewer window is placed OFF every monitor -- it is presented and captured
    // (PrintWindow, PW_RENDERFULLCONTENT) without covering any of the user's screen.
    const int e1X = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 200;
    viewerWindow = CreateWindowExW(0, wc.lpszClassName, L"remote60 prelogin login viewer", WS_OVERLAPPEDWINDOW,
                                   renderE1 ? e1X : 500, 40, 800, 500, nullptr, nullptr, wc.hInstance, &ctx);''')

# the receive chain: in E1 the product's picker stays, and the PRODUCT presents (no stand-in present thread)
rep('''      gate.waitForKeyFrame = true;
      ctx.picker.visible.store(false);
      ctx.control.keyframeRequests.Reset();''',
    '''      gate.waitForKeyFrame = true;
      if (!renderE1) ctx.picker.visible.store(false);  // E1: the product's own picker, shown by its control client
      ctx.control.keyframeRequests.Reset();''')
rep('''      // What the renderer would show: each newly published frame, stamped, with its luma spread (max - min
      // of a sparse sample of the visible picture).
      presentThread = std::thread([&] {''',
    '''      // What the renderer would show: each newly published frame, stamped, with its luma spread (max - min
      // of a sparse sample of the visible picture). Not in E1: there the product's window procedure presents.
      if (!renderE1) presentThread = std::thread([&] {''')

block = '''  // rel-0163 r2 E1 (Codex codex_rel_0163_evidence_r1.md): the same still logon screen, through the PRODUCT
  // renderer -- the viewer's window procedure (WndProc), request_video_paint -> WM_PAINT -> paint_video_frame ->
  // the D3D swap chain's Present (viewer_present.cpp; ctx.present.d3dPresentSuccessCount counts the presents
  // that succeeded). The product picker is shown for 10 s, its desktop card is pressed with the messages a
  // mouse produces, and the window is captured with PrintWindow (PW_RENDERFULLCONTENT: what DWM composes for it).
  // The window is OFF every monitor: nothing of the user's screen is covered or touched.
  if (stillScreen && connected && renderE1) {
    std::cout << "''' + NL + '''--- PS E1: the product renderer, picker 10 s -> desktop ---''' + NL + '''";
    using namespace remote60::native_poc::viewer;
    const auto worker_alive = [&] { return host.pi.hProcess && WaitForSingleObject(host.pi.hProcess, 0) == WAIT_TIMEOUT; };
    const auto presents = [&] { return ctx.present.d3dPresentSuccessCount.load(std::memory_order_relaxed); };
    struct Shot {
      bool ok = false;
      double pickerShare = 1.0;  // share of pixels that are exactly the picker background RGB(13,15,20)
      uint32_t lumaSpread = 0;
      size_t colors = 0;
      int w = 0, h = 0;
    };
    const auto capture = [&](const std::wstring& name) {
      Shot sh;
      RECT rc{};
      GetClientRect(viewerWindow, &rc);
      sh.w = rc.right - rc.left;
      sh.h = rc.bottom - rc.top;
      if (sh.w <= 0 || sh.h <= 0) return sh;
      HDC wdc = GetDC(viewerWindow);
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
      const BOOL printed = PrintWindow(viewerWindow, mem, PW_CLIENTONLY | 0x2 /* PW_RENDERFULLCONTENT */);
      SelectObject(mem, old);
      if (printed && bits) {
        const auto* px = static_cast<const uint32_t*>(bits);
        size_t picker = 0;
        uint32_t lo = 255, hi = 0;
        std::set<uint32_t> colors;
        for (int i = 0; i < sh.w * sh.h; ++i) {
          const uint32_t c = px[i] & 0xFFFFFF;
          if (c == 0x0D0F14 || c == 0x140F0D) ++picker;
          const uint32_t luma = (((c >> 16) & 0xFF) * 3 + ((c >> 8) & 0xFF) * 6 + (c & 0xFF)) / 10;
          lo = std::min(lo, luma);
          hi = std::max(hi, luma);
          if (colors.size() < 5000) colors.insert(c);
        }
        sh.pickerShare = static_cast<double>(picker) / (static_cast<double>(sh.w) * sh.h);
        sh.lumaSpread = hi - lo;
        sh.colors = colors.size();
        sh.ok = true;
        if (!shotsDir.empty()) {
          BITMAPFILEHEADER fh{};
          const DWORD imageBytes = static_cast<DWORD>(sh.w) * sh.h * 4;
          fh.bfType = 0x4D42;
          fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
          fh.bfSize = fh.bfOffBits + imageBytes;
          std::ofstream out(shotsDir + L"\\\\" + name, std::ios::binary);
          out.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
          out.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
          out.write(static_cast<const char*>(bits), imageBytes);
        }
      }
      DeleteObject(bmp);
      DeleteDC(mem);
      ReleaseDC(viewerWindow, wdc);
      return sh;
    };
    const auto shot_text = [](const Shot& sh) {
      char b[128];
      std::snprintf(b, sizeof(b), "pickerBg=%.1f%% luma=%u colors=%zu %dx%d", sh.pickerShare * 100.0, sh.lumaSpread,
                    sh.colors, sh.w, sh.h);
      return std::string(b);
    };
    RECT wr{};
    GetWindowRect(viewerWindow, &wr);
    const RECT vs{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                  GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                  GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    RECT overlap{};
    check("E1: the viewer window is OFF every monitor (the user's screen is not covered)",
          !IntersectRect(&overlap, &wr, &vs), std::to_string(wr.left) + "," + std::to_string(wr.top));
    // The product picker: shown by the control client (startInPicker), the stream off behind it.
    const bool pickerUp = pump_until([&] { return ctx.picker.visible.load(); }, 5000);
    check("E1: the product's picker is shown", pickerUp);
    pump_until([] { return false; }, 10000);  // 10 s in the picker (the worker's capture idles, then detaches)
    const Shot pickerShot = capture(L"E1-1-picker.bmp");
    check("E1: screenshot 1 -- the picker (negative control for the stream screenshots)",
          pickerShot.ok && pickerShot.pickerShare > 0.15, shot_text(pickerShot));
    const ClientLayout layout = compute_client_layout(ctx, viewerWindow);
    const CardGridMetrics grid = compute_card_grid(ctx, layout.listRect);
    const RECT card = card_rect_for_slot(layout.listRect, grid, 0);
    const int cx = (card.left + card.right) / 2, cy = (card.top + card.bottom) / 2;
    const uint64_t presentsBefore = presents();
    const uint64_t t0 = qpc_now_us();
    PostMessageW(viewerWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
    PostMessageW(viewerWindow, WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
    uint64_t firstPresentUs = 0;
    const bool presented = pump_until([&] {
      if (presents() > presentsBefore && !ctx.picker.visible.load() &&
          ctx.frameBuf.lastPresentedVersion.load(std::memory_order_relaxed) != 0) {
        if (firstPresentUs == 0) firstPresentUs = qpc_now_us();
        return true;
      }
      return false;
    }, 4000);
    const uint64_t latencyMs = presented ? (firstPresentUs - t0) / 1000 : 0;
    check("E1 PS: THE DESKTOP CARD WAS PRESSED -> THE PRODUCT RENDERER PRESENTED THE STILL SCREEN WITHIN 1 s",
          presented && latencyMs <= 1000,
          presented ? std::to_string(latencyMs) + " ms (d3dPresentSuccess " + std::to_string(presentsBefore) + " -> " +
                          std::to_string(presents()) + ")"
                    : std::string("no successful present in 4 s"));
    pump_until([] { return false; }, 300);
    const Shot streamShot = capture(L"E1-2-presented.bmp");
    check("E1 PS: screenshot 2 -- the window SHOWS the picture (not the picker, not a blank)",
          streamShot.ok && streamShot.pickerShare < 0.05 && streamShot.lumaSpread >= 8 && streamShot.colors > 64,
          shot_text(streamShot));
    const uint64_t presentsAt = presents();
    pump_until([] { return false; }, 3000);
    const uint64_t refreshed = presents() - presentsAt;
    check("E1 PS: THE STILL SCREEN KEEPS BEING PRESENTED (>= 4 successful presents in the next 3 s, 500 ms refresh)",
          refreshed >= 4, std::to_string(refreshed) + " presents");
    const Shot laterShot = capture(L"E1-3-still-refreshed.bmp");
    check("E1 PS: screenshot 3 -- still the picture 3 s later", laterShot.ok && laterShot.pickerShare < 0.05 &&
                                                                   laterShot.lumaSpread >= 8, shot_text(laterShot));
    check("E1 PS: the worker is still serving (no exit 15 / 14)", worker_alive());
    check("E1 PS: no present failed", ctx.present.d3dPresentFailCount.load() == 0,
          std::to_string(ctx.present.d3dPresentFailCount.load()) + " failed");
  }
  if (stillScreen && connected && !renderE1) {'''
rep('''  if (stillScreen && connected) {
    std::cout << "''' + NL + '''--- PS: a still logon screen, picked after the picker ---''' + NL + '''";''',
    block + '''
    std::cout << "''' + NL + '''--- PS: a still logon screen, picked after the picker ---''' + NL + '''";''')
if '#include <set>' not in s:
    s = s.replace('#include <sstream>\n', '#include <set>\n#include <sstream>\n', 1)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
