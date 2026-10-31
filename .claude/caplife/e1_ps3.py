p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_prelogin_login_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''    const auto shot_text = [](const Shot& sh) {''', r'''    // The picker before any pick is GDI-drawn (no swap chain exists until a pick produces a frame): PrintWindow.
    const auto capture_gdi = [&](const std::wstring& name) {
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
      const BOOL printed = PrintWindow(viewerWindow, mem, PW_CLIENTONLY);
      SelectObject(mem, old);
      if (printed && bits) {
        remote60::native_poc::e2e::WindowCapture c;
        c.ok = true;
        c.w = sh.w;
        c.h = sh.h;
        c.px.assign(static_cast<const uint32_t*>(bits), static_cast<const uint32_t*>(bits) + sh.w * sh.h);
        size_t picker = 0;
        for (const uint32_t px : c.px) {
          if ((px & 0xFFFFFF) == 0x0D0F14) ++picker;
        }
        sh.pickerShare = static_cast<double>(picker) / (static_cast<double>(sh.w) * sh.h);
        sh.ok = true;
        if (!shotsDir.empty()) remote60::native_poc::e2e::e2e_save_bmp(shotsDir + L"\\" + name, c);
      }
      DeleteObject(bmp);
      DeleteDC(mem);
      ReleaseDC(viewerWindow, wdc);
      return sh;
    };
    const auto shot_text = [](const Shot& sh) {''')
rep('''    const Shot pickerShot = capture(L"E1-1-picker.bmp");''', '''    const Shot pickerShot = capture_gdi(L"E1-1-picker.bmp");''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
