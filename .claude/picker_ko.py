# -*- coding: utf-8 -*-
"""The three English strings the picker screenshot showed.

The header and buttons were translated earlier from reading the source. These three were not,
because nobody had looked at the screen -- which is the whole reason the preview exists.
"""
import io

p = 'apps/native_poc/src/viewer_overlay_draw.cpp'
d = io.open(p, encoding='utf-8').read()

subs = [
('''    draw_text_utf8(ctx, hdc, windowId == 0 ? std::string("Desktop") : std::string("Loading preview..."),
                   &ph, DT_CENTER | DT_VCENTER | DT_SINGLELINE);''',
 '''    draw_text_utf8(ctx, hdc,
                   windowId == 0 ? std::string("전체 화면") : std::string("미리보기 준비 중…"),
                   &ph, DT_CENTER | DT_VCENTER | DT_SINGLELINE);'''),

('''      draw_target_card(ctx, hdc, card, grid, 0, "Desktop (full screen)", selectedId == 0,''',
 '''      draw_target_card(ctx, hdc, card, grid, 0, "전체 화면", selectedId == 0,'''),

('''  foot << (ctx.control.connected.load(std::memory_order_relaxed) ? "Connected" : "Disconnected")
       << "   Input " << (ctx.session.inputEnabled.load(std::memory_order_relaxed) ? "on" : "off");''',
 '''  foot << (ctx.control.connected.load(std::memory_order_relaxed) ? "연결됨" : "연결 끊김")
       << "   입력 "
       << (ctx.session.inputEnabled.load(std::memory_order_relaxed) ? "켜짐" : "꺼짐");'''),

('''    foot << "   Rows " << (scrollRow + 1) << "-"
         << std::min(totalRows, scrollRow + grid.visibleRows) << " / " << totalRows
         << " (wheel to scroll)";''',
 '''    foot << "   " << (scrollRow + 1) << "-"
         << std::min(totalRows, scrollRow + grid.visibleRows) << " / " << totalRows << " 줄"
         << " (휠로 스크롤)";'''),
]

for old, new in subs:
    assert d.count(old) == 1, 'anchor: ' + old[:50]
    d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('ok')
