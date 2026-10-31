p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\host_capture_lifecycle_ua_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
start = s.index('    const auto capture = [&](const std::wstring& name, COLORREF expect) {')
end = s.index('    const auto pct = [](double v) {')
new = r'''    // What DWM composes for the viewer window (Windows.Graphics.Capture on that window -- e2e_window_capture.hpp):
    // the presented picture, read back. The share is over the centre half of the window.
    const auto capture = [&](const std::wstring& name, COLORREF expect) {
      Shot sh;
      const remote60::native_poc::e2e::WindowCapture c = remote60::native_poc::e2e::e2e_capture_window(viewer, [&] {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
      });
      if (!c.ok) return sh;
      sh.w = c.w;
      sh.h = c.h;
      size_t hit = 0, all = 0;
      const int er = GetRValue(expect), eg = GetGValue(expect), eb = GetBValue(expect);
      for (int y = c.h / 4; y < c.h * 3 / 4; ++y) {
        for (int x = c.w / 4; x < c.w * 3 / 4; ++x) {
          const uint32_t px = c.px[static_cast<size_t>(y) * c.w + x];
          const int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
          ++all;
          if (std::abs(r - er) <= 48 && std::abs(g - eg) <= 48 && std::abs(b - eb) <= 48) ++hit;
        }
      }
      sh.share = all ? static_cast<double>(hit) / all : 0;
      sh.ok = true;
      if (!shotsDir.empty() && name != L"E1-UA-tmp.bmp") remote60::native_poc::e2e::e2e_save_bmp(shotsDir + L"\\" + name, c);
      return sh;
    };
'''
s = s[:start] + new + s[end:]
o = '#include "viewer_picker.hpp"'
assert s.count(o) == 1
s = s.replace(o, '#include "e2e_window_capture.hpp"  // rel-0163 r2 E1: what DWM composes for the viewer window\n' + o, 1)
s = s.replace('      DeleteFileW((shotsDir + L"\\\\E1-UA-tmp.bmp").c_str());\n', '')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok', 'E1-UA-tmp' in s)
