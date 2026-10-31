p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\host_capture_lifecycle_ua_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''    HWND marker = CreateWindowExW(0, mc.lpszClassName, L"GNLink E1 marker", WS_OVERLAPPEDWINDOW, offX, 620, 640, 400,
                                  nullptr, nullptr, mc.hInstance, nullptr);''',
    '''    // A LAYERED window: its pixels are handed to DWM (UpdateLayeredWindow), so they exist off every monitor too
    // -- an ordinary window there gets no WM_PAINT and GDI drawing into it is clipped away (measured 10-26).
    HWND marker = CreateWindowExW(WS_EX_LAYERED, mc.lpszClassName, L"GNLink E1 marker", WS_POPUP, offX, 620, 640, 400,
                                  nullptr, nullptr, mc.hInstance, nullptr);''')
rep('''    const auto paint_marker = [&] {
      HDC dc = GetDC(marker);
      RECT rc{};
      GetClientRect(marker, &rc);
      HBRUSH b = CreateSolidBrush(markerColor);
      FillRect(dc, &rc, b);
      DeleteObject(b);
      ReleaseDC(marker, dc);
    };''',
    '''    const auto paint_marker = [&] {
      const int mw = 640, mh = 400;
      HDC screen = GetDC(nullptr);
      HDC mem = CreateCompatibleDC(screen);
      BITMAPINFO bi{};
      bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
      bi.bmiHeader.biWidth = mw;
      bi.bmiHeader.biHeight = -mh;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      void* bits = nullptr;
      HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
      const uint32_t px = 0xFF000000u | (static_cast<uint32_t>(GetRValue(markerColor)) << 16) |
                          (static_cast<uint32_t>(GetGValue(markerColor)) << 8) | GetBValue(markerColor);
      for (int i = 0; i < mw * mh; ++i) static_cast<uint32_t*>(bits)[i] = px;
      HGDIOBJ old = SelectObject(mem, bmp);
      POINT dst{offX, 620}, src{0, 0};
      SIZE sz{mw, mh};
      BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
      UpdateLayeredWindow(marker, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      ReleaseDC(nullptr, screen);
    };''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
