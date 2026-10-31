p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_prelogin_login_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
start = s.index('    const auto capture = [&](const std::wstring& name) {')
end = s.index('    const auto shot_text = [](const Shot& sh) {')
new = r'''    // What DWM composes for the viewer window (Windows.Graphics.Capture on it, e2e_window_capture.hpp). PrintWindow
    // cannot be used: for the D3D flip-model swap chain it returns the window's GDI background (measured 10-26).
    const auto capture = [&](const std::wstring& name) {
      Shot sh;
      const remote60::native_poc::e2e::WindowCapture c = remote60::native_poc::e2e::e2e_capture_window(viewerWindow, [&] {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
      });
      if (!c.ok) return sh;
      sh.w = c.w;
      sh.h = c.h;
      size_t picker = 0;
      uint32_t lo = 255, hi = 0;
      std::set<uint32_t> colors;
      // The client area only (the capture includes the frame): the centre 80 %.
      for (int y = c.h / 10; y < c.h * 9 / 10; ++y) {
        for (int x = c.w / 10; x < c.w * 9 / 10; ++x) {
          const uint32_t px = c.px[static_cast<size_t>(y) * c.w + x] & 0xFFFFFF;
          if (px == 0x0D0F14) ++picker;
          const uint32_t luma = (((px >> 16) & 0xFF) * 3 + ((px >> 8) & 0xFF) * 6 + (px & 0xFF)) / 10;
          lo = std::min(lo, luma);
          hi = std::max(hi, luma);
          if (colors.size() < 5000) colors.insert(px);
        }
      }
      const double area = static_cast<double>(c.w * 8 / 10) * (c.h * 8 / 10);
      sh.pickerShare = area > 0 ? picker / area : 0;
      sh.lumaSpread = hi - lo;
      sh.colors = colors.size();
      sh.ok = true;
      if (!shotsDir.empty()) remote60::native_poc::e2e::e2e_save_bmp(shotsDir + L"\\" + name, c);
      return sh;
    };
'''
s = s[:start] + new + s[end:]
o = '#include "viewer_layout.hpp"'
assert s.count(o) == 1
s = s.replace(o, '#include "e2e_window_capture.hpp"  // rel-0163 r2 E1: what DWM composes for the viewer window\n' + o, 1)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
