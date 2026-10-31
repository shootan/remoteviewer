p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_prelogin_login_e2e_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old = '''    if (shown3) shown_after("(3)", L"E1-4-presented-again.bmp", pickerShot3);
'''
new = '''    if (shown3) shown_after("(3)", L"E1-4-presented-again.bmp", pickerShot3);

    // (4) rel-0163 r5 (Codex R3): the FINAL adoption boundary. A local pick A; right before A's first picture is
    //     committed to the FrameBuffer (a test hook on the receive thread), A is cancelled and a new local op B
    //     begins. A's picture must not be adopted (refused at the commit, its version not written) nor bind or
    //     reveal B; B then completes on its OWN decoded keyframe.
    set_picker_visible_and_sync_stream(ctx, true);
    pump_until([] { return false; }, 1500);
    std::atomic<bool> fired{false};
    std::atomic<uint64_t> opA{0}, opB{0}, versionAtFire{0}, refusedAtFire{0};
    ctx.sel.testBeforeFrameCommit = [&] {
      if (fired.load()) return;
      const uint64_t e = ctx.sel.epoch.load();
      if (!ctx.sel.LocalUnbound(e)) return;  // only A's first picture, before any bind
      opA = e;
      {
        std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
        versionAtFire = ctx.frameBuf.frame.version;
      }
      refusedAtFire = ctx.sel.localFramesRefused.load();
      ctx.sel.Clear();       // A cancelled ...
      ctx.sel.BeginLocal();  // ... and a new local op B began
      opB = ctx.sel.epoch.load();
      fired = true;
    };
    const uint64_t p4 = presents();
    press_desktop();
    const bool hookFired = pump_until([&] { return fired.load(); }, 8000);
    pump_until([] { return false; }, 50);
    uint64_t versionAfter = 0;
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
      versionAfter = ctx.frameBuf.frame.version;
    }
    check("E1 PS (4) R3: A cancelled and B begun right before A's first picture's commit -- A's picture REFUSED, not written",
          hookFired && ctx.sel.localFramesRefused.load() > refusedAtFire.load(),
          "versionAtFire=" + std::to_string(versionAtFire.load()) + " refused " + std::to_string(refusedAtFire.load()) + " -> " +
              std::to_string(ctx.sel.localFramesRefused.load()));
    ctx.sel.testBeforeFrameCommit = nullptr;
    // B (pending, picker still up) completes on its own keyframe: bound with B's token, revealed, presented.
    uint64_t lat4 = 0;
    const bool shown4 = wait_presented(p4, qpc_now_us(), 12000, &lat4);
    check("E1 PS (4) R3: ...B then binds and completes on its OWN decoded keyframe (bound with B's op, never A's)",
          shown4 && ctx.sel.activeStreamGeneration.load() != 0 && ctx.sel.epoch.load() == opB.load() && opA.load() != opB.load(),
          "opA=" + std::to_string(opA.load()) + " opB=" + std::to_string(opB.load()) + " versionAfterFire=" +
              std::to_string(versionAfter));
'''
assert s.count(old) == 1
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
