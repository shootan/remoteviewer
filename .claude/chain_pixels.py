# -*- coding: utf-8 -*-
"""The picker's own logs go to stdout, so the test cannot capture them.

Better anyway: assert that pixels changed. "It logged that it painted" and "something is on the
screen" are different claims, and only the second is what the user means.
"""
import io

p = 'apps/native_poc/src/picker_open_chain_test.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  ctx.control.connected.store(true, std::memory_order_relaxed);
  ctx.session.logSink = [](const std::string& line) { gLines.push_back(line); };
'''
new = '''  ctx.control.connected.store(true, std::memory_order_relaxed);
'''
assert d.count(old) == 1, 'sink anchor'
d = d.replace(old, new, 1)

old = '''  ok(said("targets clicked, callback present"), "hypothesis 1: the click reaches the callback");
  ok(ctx.picker.visible.load(std::memory_order_relaxed),
     "hypothesis 2: the picker's visible flag actually flips");
  ok(said("[picker] visible 0->1"), "and the transition is reported", "");'''
new = '''  ok(said("targets clicked, callback present"), "hypothesis 1: the click reaches the callback");
  ok(ctx.picker.visible.load(std::memory_order_relaxed),
     "hypothesis 2: the picker's visible flag actually flips");'''
assert d.count(old) == 1, 'hyp anchor'
d = d.replace(old, new, 1)

old = '''  // ------------------------------------------------------------------ does anything get drawn
  //
  // Painted into a memory DC rather than waiting for WM_PAINT: this owner window is off screen
  // and DWM will not necessarily paint it, and what is being decided here is whether the picker
  // HAS something to draw -- not whether this test's window got a paint message.
  {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 1280, 800);
    HGDIOBJ old = SelectObject(mem, bitmap);
    gLines.clear();
    remote60::native_poc::viewer::draw_overlay(ctx, mem);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
  }
  ok(said("[picker] first paint after show"), "hypothesis 3: the picker draws once it is shown");'''

new = '''  // ------------------------------------------------------------------ does anything get drawn
  //
  // Pixels, not a log line. The picker writes "first paint after show" to stdout, which this
  // process cannot capture, and in any case "it said it painted" and "something is on screen"
  // are different claims. Both states are rendered into identical bitmaps and compared: shown
  // has to differ from hidden, and it has to differ a lot.
  //
  // Painted into a memory DC rather than waiting for WM_PAINT -- the owner is off screen and DWM
  // need not paint it. What is being decided is whether the picker HAS something to draw.
  const int kW = 1280;
  const int kH = 800;
  const auto render = [&](bool visible, std::vector<uint32_t>* out) {
    ctx.picker.visible.store(visible, std::memory_order_relaxed);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kW;
    bmi.bmiHeader.biHeight = -kH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ prev = SelectObject(mem, bitmap);
    remote60::native_poc::viewer::draw_overlay(ctx, mem);
    GdiFlush();
    out->assign(static_cast<uint32_t*>(bits), static_cast<uint32_t*>(bits) + kW * kH);
    SelectObject(mem, prev);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
  };

  std::vector<uint32_t> hidden;
  std::vector<uint32_t> shown;
  render(false, &hidden);
  render(true, &shown);
  size_t differing = 0;
  for (size_t i = 0; i < shown.size(); ++i) {
    if (shown[i] != hidden[i]) ++differing;
  }
  const double share = static_cast<double>(differing) / static_cast<double>(shown.size());
  ok(share > 0.5, "hypothesis 3: showing the picker changes most of the window",
     std::to_string(static_cast<int>(share * 100)) + "% of pixels differ");'''
assert d.count(old) == 1, 'draw anchor'
d = d.replace(old, new, 1)

d = d.replace('#include <vector>', '#include <cstdint>\n#include <vector>', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
