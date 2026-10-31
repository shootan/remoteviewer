# -*- coding: utf-8 -*-
"""Let the grid know how many cards there are.

Five cards used a fifth of the window because the layout was computed from a preferred card width
and nothing else. It cannot grow the cards to fit without knowing how many there are.

`itemCount == 0` keeps the old behaviour exactly, so every existing expectation still means what
it meant -- the new behaviour is asserted separately rather than by rewriting the old numbers.
"""
import io

p = 'apps/native_poc/src/viewer_layout_math.hpp'
d = io.open(p, encoding='utf-8').read()

old = '''inline CardGridMetrics compute_card_grid_at(const RECT& gridRect, int dpi) {
  CardGridMetrics m;
  m.gap = scale_dpi(14, dpi);
  const int gridW = std::max<int>(1, gridRect.right - gridRect.left);
  const int gridH = std::max<int>(1, gridRect.bottom - gridRect.top);
  const int preferredCardW = scale_dpi(232, dpi);
  m.cols = std::max<int>(1, (gridW + m.gap) / (preferredCardW + m.gap));
  m.cardW = std::max<int>(scale_dpi(140, dpi), (gridW - (m.cols - 1) * m.gap) / m.cols);
  m.thumbH = (m.cardW * 10) / 16;
  m.cardH = m.thumbH + scale_dpi(30, dpi);
  m.visibleRows = std::max<int>(1, (gridH + m.gap) / (m.cardH + m.gap));
  m.visibleCards = m.visibleRows * m.cols;
  return m;
}'''

new = '''/**
 * The card grid.
 *
 * `itemCount` is how many cards there are, or 0 when the caller does not know. Zero reproduces
 * the original layout exactly: a fixed preferred card width, however much room that leaves over.
 *
 * With a count, the cards grow to use the space. Five of them used to occupy a fifth of the
 * window and leave the rest black, because a layout computed from a preferred width alone cannot
 * tell "five cards" from "the first five of forty".
 */
inline CardGridMetrics compute_card_grid_at(const RECT& gridRect, int dpi, int itemCount = 0) {
  CardGridMetrics m;
  m.gap = scale_dpi(14, dpi);
  const int gridW = std::max<int>(1, gridRect.right - gridRect.left);
  const int gridH = std::max<int>(1, gridRect.bottom - gridRect.top);
  const int preferredCardW = scale_dpi(232, dpi);
  const int minCardW = scale_dpi(140, dpi);
  // An upper bound, because one card in a wide window should not become a poster.
  const int maxCardW = scale_dpi(420, dpi);

  const auto shape = [&](int cols) {
    CardGridMetrics out;
    out.gap = m.gap;
    out.cols = std::max<int>(1, cols);
    out.cardW = std::max<int>(minCardW, (gridW - (out.cols - 1) * out.gap) / out.cols);
    out.thumbH = (out.cardW * 10) / 16;
    out.cardH = out.thumbH + scale_dpi(30, dpi);
    out.visibleRows = std::max<int>(1, (gridH + out.gap) / (out.cardH + out.gap));
    out.visibleCards = out.visibleRows * out.cols;
    return out;
  };

  if (itemCount > 0) {
    // Fewest columns first, which is the largest card. The first arrangement whose rows fit the
    // height without scrolling wins; if none do, fall through to the fixed-width layout and let
    // it scroll, which is what it has always done.
    for (int cols = 1; cols <= itemCount; ++cols) {
      const CardGridMetrics candidate = shape(cols);
      if (candidate.cardW > maxCardW) continue;
      const int rows = (itemCount + cols - 1) / cols;
      const int neededH = rows * (candidate.cardH + candidate.gap) - candidate.gap;
      if (neededH <= gridH) return candidate;
    }
  }

  return shape(std::max<int>(1, (gridW + m.gap) / (preferredCardW + m.gap)));
}'''

assert d.count(old) == 1, 'grid anchor'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('math ok')

# ---------------------------------------------------------------- pass the count through
p = 'apps/native_poc/src/viewer_layout.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''CardGridMetrics compute_card_grid(ViewerState& ctx, const RECT& gridRect) {
  return compute_card_grid_at(gridRect, ctx.ui.dpi);
}'''
new = '''CardGridMetrics compute_card_grid(ViewerState& ctx, const RECT& gridRect) {
  // The desktop card plus one per window: the same total the drawing and the hit test both walk.
  // Taken from the panel here so every caller agrees without having to remember to pass it.
  const int itemCount = 1 + static_cast<int>(ctx.picker.windowPanel.Snapshot().items.size());
  return compute_card_grid_at(gridRect, ctx.ui.dpi, itemCount);
}'''
assert d.count(old) == 1, 'wrapper'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('wrapper ok')
