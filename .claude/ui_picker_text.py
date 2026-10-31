# -*- coding: utf-8 -*-
import io

p = 'apps/native_poc/src/viewer_overlay_draw.cpp'
d = io.open(p, encoding='utf-8').read()

subs = [
# header
('''    DrawTextW(hdc, L"Remote60", -1, &t, DT_LEFT | DT_SINGLELINE);''',
 '''    // The product name was already on the window title; what this screen needs to say is what
    // it is for.
    DrawTextW(hdc, L"공유 화면 선택", -1, &t, DT_LEFT | DT_SINGLELINE);'''),

# subheader source + the three progress lines
('''  std::string statusLine =
      selectionLocked ? std::string("Target locked by host config") : panelStatus;
  if (selectionPending) {
    statusLine = awaitingAck ? std::string("Selecting target...")
                             : std::string("Waiting for first frame...");
  }
  if (!ctx.control.connected.load(std::memory_order_relaxed)) statusLine = "Connecting to host...";''',
 '''  // panelDisplayStatus, not panelStatus: the latter is the token the code matches on
  // (`window_list_received count=3`), and it used to be drawn here verbatim.
  std::string statusLine = selectionLocked
                               ? std::string("호스트 설정으로 대상이 고정돼 있습니다")
                               : (panelDisplayStatus.empty() ? panelStatus : panelDisplayStatus);
  if (selectionPending) {
    statusLine = awaitingAck ? std::string("선택하는 중…")
                             : std::string("첫 화면을 기다리는 중…");
  }
  if (!ctx.control.connected.load(std::memory_order_relaxed)) {
    statusLine = "호스트에 연결하는 중…";
  }'''),

# the snapshot read
('''  const std::string& panelStatus = windowPanel.status;''',
 '''  const std::string& panelStatus = windowPanel.status;
  const std::string& panelDisplayStatus = windowPanel.displayStatus;'''),

# buttons
('''  draw_panel_button(ctx, hdc, layout.refreshButtonRect, "Refresh", false,''',
 '''  draw_panel_button(ctx, hdc, layout.refreshButtonRect, "새로 고침", false,'''),
('''  draw_panel_button(ctx, hdc, layout.desktopButtonRect, "Desktop", selectedId == 0, actionsDisabled);''',
 '''  draw_panel_button(ctx, hdc, layout.desktopButtonRect, "전체 화면", selectedId == 0,
                    actionsDisabled);'''),
]

for old, new in subs:
    assert d.count(old) == 1, 'anchor: ' + old[:60]
    d = d.replace(old, new, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(d)
print('overlay ok')

# ---------------------------------------------------------------- window title
p = 'apps/native_poc/src/viewer_window_proc.cpp'
d = io.open(p, encoding='utf-8').read()
old = '''  ctx.session.hwnd = CreateWindowExW(0, cls, L"remote60 native video client",'''
new = '''  // The user sees this in the taskbar and in Alt-Tab. "remote60 native video client" is the
  // name of a prototype; set_viewer_window_title() puts the target beside it once one is chosen.
  ctx.session.hwnd = CreateWindowExW(0, cls, L"GNLink",'''
assert d.count(old) == 1, 'title anchor'
io.open(p, 'w', encoding='utf-8', newline='').write(d.replace(old, new, 1))
print('title ok')
