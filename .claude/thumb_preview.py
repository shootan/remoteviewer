# -*- coding: utf-8 -*-
"""Real thumbnails in the preview, so the aspect handling can be seen rather than assumed."""
import io

p = 'apps/native_poc/tools/picker_preview.cpp'
d = io.open(p, encoding='utf-8').read()

# ---------------------------------------------------------------- a recognisable test image
old = '''}  // namespace

int wmain() {'''
new = '''/**
 * A picture whose shape is obvious at a glance.
 *
 * A flat colour would look correct at any aspect ratio -- stretched, squashed, cropped, all the
 * same rectangle. This draws a border, diagonals and a centred block, so a circle-that-is-an-oval
 * or a border with one edge missing says immediately what the layout did to it.
 */
std::shared_ptr<const remote60::native_poc::viewer::WindowThumb> test_thumb(uint32_t w, uint32_t h) {
  auto thumb = std::make_shared<remote60::native_poc::viewer::WindowThumb>();
  thumb->width = w;
  thumb->height = h;
  thumb->bgra.assign(static_cast<size_t>(w) * h * 4, 0);
  const auto put = [&](uint32_t x, uint32_t y, uint8_t b, uint8_t g, uint8_t r) {
    if (x >= w || y >= h) return;
    const size_t i = (static_cast<size_t>(y) * w + x) * 4;
    thumb->bgra[i + 0] = b;
    thumb->bgra[i + 1] = g;
    thumb->bgra[i + 2] = r;
    thumb->bgra[i + 3] = 255;
  };
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) put(x, y, 40, 46, 58);
  }
  // Border: a missing edge means the image was cropped rather than fitted.
  for (uint32_t x = 0; x < w; ++x) {
    for (uint32_t t = 0; t < 3; ++t) {
      put(x, t, 240, 240, 240);
      put(x, h - 1 - t, 240, 240, 240);
    }
  }
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t t = 0; t < 3; ++t) {
      put(t, y, 240, 240, 240);
      put(w - 1 - t, y, 240, 240, 240);
    }
  }
  // Diagonals: they meet the corners only while the aspect ratio is intact.
  for (uint32_t x = 0; x < w; ++x) {
    const uint32_t y = static_cast<uint32_t>(static_cast<uint64_t>(x) * h / (w ? w : 1));
    put(x, y, 246, 130, 59);
    put(x, h - 1 - (y < h ? y : h - 1), 246, 130, 59);
  }
  // A square block in the middle: it is only square when nothing was stretched.
  const uint32_t side = (w < h ? w : h) / 4;
  for (uint32_t y = 0; y < side; ++y) {
    for (uint32_t x = 0; x < side; ++x) {
      put(w / 2 - side / 2 + x, h / 2 - side / 2 + y, 246, 130, 59);
    }
  }
  return thumb;
}

}  // namespace

int wmain() {'''
assert d.count(old) == 1, 'helper anchor'
d = d.replace(old, new, 1)

# ---------------------------------------------------------------- three shapes, one shot
old = '''      {L"picker-count-30.png", "thirty windows (scrolls)", 30, true, false, 0, ""},'''
new = '''      {L"picker-count-30.png", "thirty windows (scrolls)", 30, true, false, 0, ""},
      {L"picker-thumbs.png", "real thumbnails: wide, tall, square", 3, true, false, 0, ""},'''
assert d.count(old) == 1, 'shot anchor'
d = d.replace(old, new, 1)

old = '''    if (shot.status && *shot.status) ctx.picker.windowPanel.SetStatus(shot.status);'''
new = '''    if (shot.status && *shot.status) ctx.picker.windowPanel.SetStatus(shot.status);

    // Only the thumbnail shot gets images. Everywhere else the cards stay in their "no preview
    // yet" state, which is the one every other picture is about.
    if (std::wstring(shot.file) == L"picker-thumbs.png") {
      std::lock_guard<std::mutex> lk(ctx.picker.thumbMu);
      ctx.picker.thumbs[0] = test_thumb(1920, 1080);   // wide
      ctx.picker.thumbs[1] = test_thumb(1920, 1080);   // wide
      ctx.picker.thumbs[2] = test_thumb(720, 1280);    // tall
      ctx.picker.thumbs[3] = test_thumb(900, 900);     // square
    }'''
assert d.count(old) == 1, 'inject anchor'
d = d.replace(old, new, 1)

if '#include <memory>' not in d:
    d = d.replace('#include <string>', '#include <memory>\n#include <string>', 1)
if '#include <mutex>' not in d:
    d = d.replace('#include <memory>', '#include <memory>\n#include <mutex>', 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
