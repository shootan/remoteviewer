# -*- coding: utf-8 -*-
"""One product, one accent colour -- and selection told apart by shape, not only by colour.

The picker used green while every screen drawn by shell.html uses blue. Two greens of equal
weight also sat side by side: the selected card and the 전체 화면 button, so "which one is
selected" was not answerable from colour anyway.
"""
import io

# ---------------------------------------------------------------- the button
p = 'apps/native_poc/src/viewer_gdi_util.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  COLORREF fill = RGB(60, 68, 80);
  if (disabled) {
    fill = RGB(42, 46, 54);
  } else if (active) {
    fill = RGB(48, 96, 62);
  }'''
new = '''  // The same blue the rest of the product uses (--accent #3b82f6 in ui/shell.html). This screen
  // was the only one in green, so the two halves of one program disagreed about their own colour.
  COLORREF fill = RGB(60, 68, 80);
  if (disabled) {
    fill = RGB(42, 46, 54);
  } else if (active) {
    fill = RGB(59, 130, 246);
  }'''
assert d.count(old) == 1, 'button fill'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('button ok')

# ---------------------------------------------------------------- the card
p = 'apps/native_poc/src/viewer_overlay_draw.cpp'
d = io.open(p, encoding='utf-8').read()

old = '''  FillRect(hdc, &captionRect, cached_brush(ctx, active ? RGB(38, 70, 52) : RGB(32, 37, 46)));'''
new = '''  // Selected cards are tinted, not filled: a filled card and a filled button would read as the
  // same kind of thing, and the button is an action while the card is a state.
  FillRect(hdc, &captionRect, cached_brush(ctx, active ? RGB(26, 48, 84) : RGB(32, 37, 46)));'''
assert d.count(old) == 1, 'caption'
d = d.replace(old, new, 1)

old = '''  RECT frame = card;
  FrameRect(hdc, &frame, cached_brush(ctx, active ? RGB(88, 178, 122) : RGB(52, 58, 70)));
  if (active) {
    RECT inner{card.left + 1, card.top + 1, card.right - 1, card.bottom - 1};
    FrameRect(hdc, &inner, cached_brush(ctx, RGB(88, 178, 122)));
  }'''
new = '''  RECT frame = card;
  FrameRect(hdc, &frame, cached_brush(ctx, active ? RGB(59, 130, 246) : RGB(52, 58, 70)));
  if (active) {
    RECT inner{card.left + 1, card.top + 1, card.right - 1, card.bottom - 1};
    FrameRect(hdc, &inner, cached_brush(ctx, RGB(59, 130, 246)));

    // A mark, not just a colour. Which card is selected has to survive a screenshot in grey, a
    // monitor with the colours wrong, and eyes that do not separate blue from grey-blue.
    const int side = dpi_scale(ctx, 18);
    const int pad = dpi_scale(ctx, 6);
    const RECT badge{card.right - side - pad, card.top + pad, card.right - pad,
                     card.top + side + pad};
    FillRect(hdc, &badge, cached_brush(ctx, RGB(59, 130, 246)));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT tick = badge;
    draw_text_utf8(ctx, hdc, std::string("\\u2713"), &tick,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }'''
assert d.count(old) == 1, 'frame'
d = d.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('card ok')
