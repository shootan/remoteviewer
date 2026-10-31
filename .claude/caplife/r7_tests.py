S = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = S + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('viewer_selection_gate_test.cpp', [
    ('''  {
    // r6 (R3 1): a permitted picture keeps the gate's lock until the caller lets it go -- a Clear/Begin waits.''',
     '''  {
    // r7 (Codex R3 token): the ticket is one snapshot. A local A's ticket, then A cancelled and B begun: A's picture
    // is refused. An ORDINARY ticket (nothing pending), then a local B begun: that ordinary picture is not mixed into
    // B either. B's own ticket is local and B's picture is adopted.
    SelectionGateState g;
    uint64_t gen = 0;
    g.BeginLocal();
    const SelectionGateState::FrameTicket ta = g.TakeFrameTicket();
    CHECK(ta.local && ta.epoch == g.epoch.load());
    g.Clear();
    g.BeginLocal();
    CHECK(!g.CommitFrame(ta.epoch, ta.local, 5, 5, &gen, nullptr));
    g.Clear();
    const SelectionGateState::FrameTicket to = g.TakeFrameTicket();
    CHECK(!to.local);
    CHECK(g.CommitFrame(to.epoch, to.local, 0, 3, &gen, nullptr));  // ordinary, nothing pending: as before
    g.BeginLocal();
    CHECK(!g.CommitFrame(to.epoch, to.local, 0, 3, &gen, nullptr));  // ...but not into the local op begun after it
    const SelectionGateState::FrameTicket tb = g.TakeFrameTicket();
    CHECK(tb.local && tb.epoch != to.epoch);
    CHECK(g.CommitFrame(tb.epoch, tb.local, 6, 6, &gen, nullptr) && gen == 6);
  }
  {
    // r6 (R3 1): a permitted picture keeps the gate's lock until the caller lets it go -- a Clear/Begin waits.'''),
])

edit('viewer_prelogin_login_e2e_test.cpp', [
    ('''    check("E1 PS (5) R3: ...then B begins and completes on its own keyframe", shown5 && ctx.sel.activeStreamGeneration.load() != 0);
''',
     '''    check("E1 PS (5) R3: ...then B begins and completes on its own keyframe", shown5 && ctx.sel.activeStreamGeneration.load() != 0);

    // (6) rel-0163 r7 (Codex R3 token): at the TICKET boundary. A local pick A; right after a frame took A's ticket
    //     (local, A's epoch) A is cancelled and B begins. That frame must be refused at its commit -- neither
    //     written as A's nor re-read as an ordinary frame -- and B completes on its own ticket and keyframe.
    set_picker_visible_and_sync_stream(ctx, true);
    pump_until([] { return false; }, 1500);
    std::atomic<bool> fired6{false};
    std::atomic<uint64_t> opA6{0}, refused6{0}, version6{0};
    ctx.sel.testAfterFrameTicket = [&](uint64_t epoch, bool local) {
      if (fired6.load() || !local) return;
      opA6 = epoch;
      refused6 = ctx.sel.localFramesRefused.load();
      {
        std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
        version6 = ctx.frameBuf.frame.version;
      }
      ctx.sel.Clear();       // A cancelled ...
      ctx.sel.BeginLocal();  // ... B begun
      fired6 = true;
    };
    const uint64_t p6 = presents();
    press_desktop();
    const bool fired6ok = pump_until([&] { return fired6.load(); }, 8000);
    ctx.sel.testAfterFrameTicket = nullptr;
    // That frame's own outcome: it reaches its commit (refused) or drops earlier (B's AdmitGeneration / decoder) --
    // either way it is never written; B's first write needs B's own keyframe, which comes later.
    pump_until([&] { return ctx.sel.localFramesRefused.load() > refused6.load(); }, 1500);
    uint64_t version6After = 0;
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
      version6After = ctx.frameBuf.frame.version;
    }
    check("E1 PS (6) R3: A cancelled / B begun right after a frame took A's ticket -- that frame is NOT written",
          fired6ok && version6After == version6.load(),
          "version " + std::to_string(version6.load()) + " -> " + std::to_string(version6After) + ", refused " +
              std::to_string(refused6.load()) + " -> " + std::to_string(ctx.sel.localFramesRefused.load()));
    uint64_t lat6 = 0;
    const bool shown6 = wait_presented(p6, qpc_now_us(), 12000, &lat6);
    check("E1 PS (6) R3: ...B completes on its own ticket and keyframe", shown6 && ctx.sel.epoch.load() != opA6.load());
'''),
])
print('ok')
