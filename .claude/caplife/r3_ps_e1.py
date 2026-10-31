p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_prelogin_login_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

# a fourth fixture env: the first pong late (the role unknown when the first pick is pressed)
rep('''                const std::wstring& extraEnv2 = std::wstring(), const std::wstring& extraEnv3 = std::wstring()) {''',
    '''                const std::wstring& extraEnv2 = std::wstring(), const std::wstring& extraEnv3 = std::wstring(),
                const std::wstring& extraEnv4 = std::wstring()) {''')
rep('''  if (!extraEnv3.empty()) fixtures.push_back(extraEnv3);''',
    '''  if (!extraEnv3.empty()) fixtures.push_back(extraEnv3);
  if (!extraEnv4.empty()) fixtures.push_back(extraEnv4);''')
rep('''                                                                                 : L"GNLINK_STREAM_TEST_STILL_SCREEN=1")
                                              : std::wstring());''',
    '''                                                                                 : L"GNLINK_STREAM_TEST_STILL_SCREEN=1")
                                              : std::wstring(),
                                  // rel-0163 r3 E1: the first pong late, so the first pick is pressed before the host's
                                  // role is known (the viewer must hold it, not send a WindowSelect blind).
                                  renderE1 ? std::wstring(L"GNLINK_STREAM_TEST_FIRST_PONG_DELAY_MS=4000") : std::wstring());''')

start = s.index('    const bool pickerUp = pump_until([&] { return ctx.picker.visible.load(); }, 5000);')
end = s.index('    check("E1 PS: the worker is still serving (no exit 15 / 14)", worker_alive());')
body = r'''    const bool pickerUp = pump_until([&] { return ctx.picker.visible.load(); }, 5000);
    check("E1: the product's picker is shown", pickerUp);
    // The desktop card where the product lays it out, pressed with the messages a mouse produces.
    const auto press_desktop = [&] {
      const ClientLayout layout = compute_client_layout(ctx, viewerWindow);
      const CardGridMetrics grid = compute_card_grid(ctx, layout.listRect);
      const RECT card = card_rect_for_slot(layout.listRect, grid, 0);
      const int cx = (card.left + card.right) / 2, cy = (card.top + card.bottom) / 2;
      PostMessageW(viewerWindow, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
      PostMessageW(viewerWindow, WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
    };
    // The picker (whatever draws it: GDI before the first swap chain, the swap chain after).
    const auto picker_shot = [&](const std::wstring& name) {
      const Shot a = capture(name);
      if (a.ok && a.pickerShare > 0.15) return a;
      return capture_gdi(name);
    };
    // Waits for the product renderer to present a picture after `t0`: picker closed, a successful present.
    const auto wait_presented = [&](uint64_t presentsBefore, uint64_t t0, int budgetMs, uint64_t* latencyMs) {
      uint64_t at = 0;
      const bool ok = pump_until([&] {
        if (presents() > presentsBefore && !ctx.picker.visible.load() &&
            ctx.frameBuf.lastPresentedVersion.load(std::memory_order_relaxed) != 0) {
          at = qpc_now_us();
          return true;
        }
        return false;
      }, budgetMs);
      *latencyMs = ok ? (at - t0) / 1000 : 0;
      return ok;
    };
    const auto shown_after = [&](const char* name, const std::wstring& shot, const Shot& pickerShot) {
      pump_until([] { return false; }, 300);
      const Shot streamShot = capture(shot);
      check((std::string("E1 PS ") + name + ": SCREENSHOT -- the window shows the picture (not the picker, not a blank)").c_str(),
            streamShot.ok && streamShot.pickerShare < 0.05 && streamShot.lumaSpread >= 8 && streamShot.colors > 64 &&
                pickerShot.pickerShare > 0.15,
            shot_text(streamShot) + " (picker before: " + shot_text(pickerShot) + ")");
      const uint64_t presentsAt = presents();
      pump_until([] { return false; }, 3000);
      const uint64_t refreshed = presents() - presentsAt;
      check((std::string("E1 PS ") + name + ": THE STILL SCREEN KEEPS BEING PRESENTED (>= 4 presents in the next 3 s)").c_str(),
            refreshed >= 4, std::to_string(refreshed) + " presents");
    };
    const auto viewer_text = [&] { return std::string(); };
    (void)viewer_text;

    // (0) rel-0163 r3: the first pick BEFORE the host's role is known (its first pong is 4 s late): held, not sent;
    //     run once when the role is known -- at the logon screen, as a local selection.
    pump_until([] { return false; }, 700);  // past the picker's mis-click guard
    const bool roleUnknown = !ctx.session.hostRoleKnown.load();
    const Shot pickerShot0 = picker_shot(L"E1-0-picker-before-role.bmp");
    const uint64_t p0 = presents();
    const uint64_t t0 = qpc_now_us();
    press_desktop();
    pump_until([] { return false; }, 300);
    check("E1 PS (0): pressed BEFORE the host's role is known -- the pick is held (picker stays, nothing sent blind)",
          roleUnknown && ctx.sel.deferredPick.load() == 1 && ctx.picker.visible.load() && !ctx.sel.pending.load(),
          std::string("roleKnown=") + (ctx.session.hostRoleKnown.load() ? "1" : "0") +
              " deferred=" + std::to_string(ctx.sel.deferredPick.load()));
    uint64_t lat0 = 0;
    const bool shown0 = wait_presented(p0, t0, 9000, &lat0);
    const bool roleKnownMs = ctx.session.hostRoleKnown.load();
    check("E1 PS (0): ...once the role is known (logon screen) it runs ONCE as a local selection and the product renderer presents",
          shown0 && roleKnownMs && ctx.sel.deferredPick.load() == 0,
          shown0 ? std::to_string(lat0) + " ms after the press (the pong was held 4 s)" : std::string("no present in 9 s"));
    if (shown0) shown_after("(0)", L"E1-0b-presented-after-role.bmp", pickerShot0);

    // (1) a WINDOW pick at the logon screen is refused (the host takes no WindowSelect there); nothing changes.
    set_picker_visible_and_sync_stream(ctx, true);  // what the toolbar's "targets" does
    pump_until([] { return false; }, 700);
    const bool windowTaken = begin_pc_target_selection(ctx, 0x12345678ull, "window_select_requested");
    check("E1 PS (1): a window pick at the logon screen is refused -- no transaction, the picker stays",
          !windowTaken && !ctx.sel.pending.load() && ctx.picker.visible.load() &&
              ctx.picker.windowPanel.Snapshot().status == "window_select_unavailable_prelogin",
          ctx.picker.windowPanel.Snapshot().status);

    // (2) Codex E1: picker 10 s (the worker's capture idles, then detaches) -> the desktop card -> presented <= 1 s.
    pump_until([] { return false; }, 10000);
    const Shot pickerShot = picker_shot(L"E1-1-picker.bmp");
    check("E1: screenshot 1 -- the picker (negative control for the stream screenshots)",
          pickerShot.ok && pickerShot.pickerShare > 0.15, shot_text(pickerShot));
    const uint64_t p2 = presents();
    const uint64_t t2 = qpc_now_us();
    press_desktop();
    uint64_t lat2 = 0;
    const bool shown2 = wait_presented(p2, t2, 4000, &lat2);
    check("E1 PS: THE DESKTOP CARD WAS PRESSED -> THE PRODUCT RENDERER PRESENTED THE STILL SCREEN WITHIN 1 s",
          shown2 && lat2 <= 1000,
          shown2 ? std::to_string(lat2) + " ms (d3dPresentSuccess " + std::to_string(p2) + " -> " + std::to_string(presents()) + ")"
                 : std::string("no successful present in 4 s"));
    if (shown2) shown_after("(2)", L"E1-2-presented.bmp", pickerShot);

    // (3) again: the picker 10 s more, the desktop again (repeated viewing; the idle reattach again).
    set_picker_visible_and_sync_stream(ctx, true);
    pump_until([] { return false; }, 10000);
    const Shot pickerShot3 = picker_shot(L"E1-3-picker-again.bmp");
    const uint64_t p3 = presents();
    const uint64_t t3 = qpc_now_us();
    press_desktop();
    uint64_t lat3 = 0;
    const bool shown3 = wait_presented(p3, t3, 4000, &lat3);
    check("E1 PS (3): the desktop again after 10 s in the picker: PRESENTED WITHIN 1 s",
          shown3 && lat3 <= 1000, shown3 ? std::to_string(lat3) + " ms" : std::string("no present in 4 s"));
    if (shown3) shown_after("(3)", L"E1-4-presented-again.bmp", pickerShot3);
'''
s = s[:start] + body + s[end:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
