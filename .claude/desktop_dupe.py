# -*- coding: utf-8 -*-
"""Two ways to pick the desktop, one outcome.

Recorded before changing anything:
  header button -> begin_pc_target_selection(ctx, 0, "desktop_select_requested")
  desktop card  -> try_hit_window_list_item -> id 0 -> begin_pc_target_selection(ctx, 0, "window_select_requested")
Same selection, different status token. Both drawn in the accent colour, so the screen showed two
blue things meaning the same thing.

Separable from the open-picker investigation: that narrowed to releasing off the button, the input
gate, and the swapchain reveal race (history: picker_open_chain_test). None of them touch this
rect. The only coupling here is that the refresh button is positioned relative to the desktop
button, which is one line.
"""
import io

# ---------------------------------------------------------------- layout: refresh anchors right
p = 'apps/native_poc/src/viewer_layout_math.hpp'
d = io.open(p, encoding='utf-8').read()
old = '''  layout.desktopButtonRect =
      make_rect(clientW - margin - buttonW, margin / 2 + (headerH - buttonH) / 2,
                buttonW, buttonH);
  layout.refreshButtonRect =
      make_rect(layout.desktopButtonRect.left - buttonGap - scale_dpi(96, dpi),
                layout.desktopButtonRect.top, scale_dpi(96, dpi), buttonH);'''
new = '''  // Refresh sits at the right edge now. It used to be placed relative to a 전체 화면 button that
  // selected exactly what the desktop card already selects -- two hit areas, one outcome, both in
  // the accent colour. The card stays; the button is gone.
  //
  // `desktopButtonRect` is kept as an empty rect rather than removed from the struct, so the
  // `point_in_rect` guards that still reference it can never match and nothing downstream has to
  // learn a new shape.
  layout.refreshButtonRect =
      make_rect(clientW - margin - scale_dpi(96, dpi), margin / 2 + (headerH - buttonH) / 2,
                scale_dpi(96, dpi), buttonH);
  layout.desktopButtonRect = make_rect(0, 0, 0, 0);
  (void)buttonW;
  (void)buttonGap;'''
assert d.count(old) == 1, 'layout anchor'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('layout ok')

# ---------------------------------------------------------------- stop drawing it
p = 'apps/native_poc/src/viewer_overlay_draw.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  draw_panel_button(ctx, hdc, layout.refreshButtonRect, "새로 고침", false,
                    !ctx.control.connected.load(std::memory_order_relaxed) || selectionPending);
  draw_panel_button(ctx, hdc, layout.desktopButtonRect, "전체 화면", selectedId == 0,
                    actionsDisabled);'''
new = '''  draw_panel_button(ctx, hdc, layout.refreshButtonRect, "새로 고침", false,
                    !ctx.control.connected.load(std::memory_order_relaxed) || selectionPending);
  // No 전체 화면 button: the first card is the desktop and selecting it does the same thing. Two
  // controls for one outcome, both accent-coloured, made "which one is selected" ambiguous.
  (void)actionsDisabled;'''
assert d.count(old) == 1, 'draw anchor'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('draw ok')
