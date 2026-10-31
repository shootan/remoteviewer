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

# ------------------------------------------------------------------ gate unit: the commit boundary, the ack+panel boundary
edit('viewer_selection_gate_test.cpp', [
    ('''  {
    // Not pending: nothing to convert or bind.
    SelectionGateState g;
    CHECK(!g.ConvertToLocal(g.epoch.load()));
    CHECK(!g.BindLocalGeneration(g.epoch.load(), 4));
  }
}''',
     '''  {
    // Not pending: nothing to convert or bind.
    SelectionGateState g;
    CHECK(!g.ConvertToLocal(g.epoch.load()));
    CHECK(!g.BindLocalGeneration(g.epoch.load(), 4));
  }
  {
    // r5 (Codex R3): the FINAL adoption. A local op's frame with no decoded keyframe yet (candidate 0) is not
    // adopted and binds nothing; with one, it binds and is adopted; after the op was cancelled and B began, A's
    // frame is refused (and is never re-read as an ordinary frame); B adopts its own.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    uint64_t gen = 0;
    CHECK(!g.CommitFrame(a, true, 0, 4, &gen));
    CHECK(g.expectedGeneration.load() == 0);
    g.Clear();
    g.BeginLocal();
    const uint64_t b = g.epoch.load();
    CHECK(!g.CommitFrame(a, true, 4, 4, &gen));  // A's picture after B began
    CHECK(g.expectedGeneration.load() == 0 && g.localFramesRefused.load() >= 1);
    g.Clear();
    CHECK(!g.CommitFrame(b, true, 4, 4, &gen));  // B's picture after B was cancelled (nothing pending)
    g.BeginLocal();
    const uint64_t c = g.epoch.load();
    CHECK(g.CommitFrame(c, true, 6, 6, &gen) && gen == 6 && g.expectedGeneration.load() == 6);
    CHECK(g.CommitFrame(c, true, 6, 6, &gen) && gen == 6);  // later frames of the bound op
    CHECK(g.RecordReveal(gen, c));
    CHECK(g.CommitReveal());
    g.Clear();  // what the reveal's UI handler does
    CHECK(g.CommitFrame(c, true, 6, 6, &gen));  // the same op, revealed: its stream goes on
    CHECK(g.CommitFrame(0, false, 0, 9, &gen) && gen == 9);  // an ordinary frame: as before
  }
  {
    // r5 (Codex R4): the host ack and the panel are one boundary -- a local op: the panel is never applied.
    SelectionGateState g;
    int panelApplied = 0;
    g.BeginLocal();
    CHECK(g.ApplyHostAck([&] { ++panelApplied; return std::make_pair(true, uint64_t{5}); }) == SelectionAck::IgnoredLocal);
    CHECK(g.ApplyHostAck([&] { ++panelApplied; return std::make_pair(false, uint64_t{0}); }) == SelectionAck::IgnoredLocal);
    CHECK(panelApplied == 0 && g.expectedGeneration.load() == 0 && g.pending.load());
    g.Clear();
    g.Begin();  // an ordinary op: its own ack applies the panel and the gate
    CHECK(g.ApplyHostAck([&] { ++panelApplied; return std::make_pair(true, uint64_t{5}); }) == SelectionAck::Acked);
    CHECK(panelApplied == 1 && g.expectedGeneration.load() == 5 && !g.awaitingAck.load());
  }
}'''),
])

# ------------------------------------------------------------------ isolated window proc: R1-run and R4 latches
edit('viewer_window_proc_isolated_test.cpp', [
    ('''  // rel-0163 r4 (Codex R2): an unsendable notice names the op it was made for''',
     '''  // rel-0163 r5 (Codex R1-run): the run the pick was made on is taken first. A reconnect between the role read and
  // the hold: the held pick stays the OLD run's -- not run on the new connection (no Begin, no stream-on).
  forget_the_selection();
  ctx.session.hostRoleKnown.store(false, std::memory_order_release);
  ctx.session.hostCaptureFlags.store(remote60::native_poc::kCaptureFlagPreloginV1, std::memory_order_relaxed);
  const uint64_t runA = ctx.session.controlRunGen.load();
  ctx.sel.testAfterRoleRead = [&] {
    ctx.session.controlRunGen.fetch_add(1, std::memory_order_seq_cst);  // run B (a reconnect) ...
    ctx.session.hostRoleKnown.store(true, std::memory_order_seq_cst);  // ... whose pong came
    remote60::native_poc::viewer::note_host_role_known(ctx, /*firstTime=*/true);
  };
  const uint64_t epochR = ctx.sel.epoch.load();
  ctx.control.streamState.Reset();
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  click_the_card();
  ctx.sel.testAfterRoleRead = nullptr;
  PostMessageW(hwnd, remote60::native_poc::viewer::kMsgRunDeferredPick, 0, 0);
  pump(100);
  remote60::native_poc::StreamStateRequest streamReq{};
  const bool streamAsked = ctx.control.streamState.ConsumePending(&streamReq);
  ok(ctx.sel.deferredRun.load() == runA && ctx.sel.epoch.load() == epochR && !ctx.sel.pending.load() && !streamAsked,
     "r5 R1-run: a reconnect between the role read and the hold -- the old run's pick is not run on the new one");
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  click_the_card();  // pressed on run B, the role known
  ok(ctx.sel.epoch.load() == epochR + 1 && ctx.sel.pending.load() && ctx.sel.localAckless.load(),
     "r5 R1-run: a pick pressed on the new run runs (once)");

  // rel-0163 r5 (Codex R4): a local op begins right before an old host ack's boundary -- the ack changes NOTHING,
  // the panel included; an ordinary op's own ack still applies.
  forget_the_selection();
  ctx.session.hostCaptureFlags.store(0, std::memory_order_relaxed);
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  click_the_card();  // an ordinary op, waiting for its ack
  const uint64_t opAck = ctx.sel.epoch.load();
  remote60::native_poc::ControlWindowSelectedMessage lateAck{};
  lateAck.ok = 1;
  lateAck.windowId = 0;
  lateAck.streamGeneration = 9;
  lateAck.seq = ctx.picker.windowPanel.Snapshot().lastSelectSeq;
  ctx.sel.testBeforeAckBoundary = [&] { (void)ctx.sel.ConvertToLocal(opAck); };  // the op became local right here
  const auto panelBefore = ctx.picker.windowPanel.Snapshot();
  remote60::native_poc::viewer::apply_window_selected_result(ctx, lateAck);
  ctx.sel.testBeforeAckBoundary = nullptr;
  const auto panelAfter = ctx.picker.windowPanel.Snapshot();
  ok(ctx.sel.localAckless.load() && ctx.sel.pending.load() && ctx.sel.expectedGeneration.load() == 0 &&
         panelAfter.status == panelBefore.status && panelAfter.selectedId == panelBefore.selectedId &&
         panelAfter.selectedTitle == panelBefore.selectedTitle && panelAfter.selectionLocked == panelBefore.selectionLocked,
     "r5 R4: an old ack at a local op -- gate and panel (status, selection, title, lock) unchanged",
     panelBefore.status + " -> " + panelAfter.status);
  forget_the_selection();
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  click_the_card();  // an ordinary op again
  lateAck.seq = ctx.picker.windowPanel.Snapshot().lastSelectSeq;
  remote60::native_poc::viewer::apply_window_selected_result(ctx, lateAck);
  ok(ctx.sel.pending.load() && !ctx.sel.awaitingAck.load() && ctx.sel.expectedGeneration.load() == 9,
     "r5 R4: an ordinary op's own ack still applies (acked, generation 9)");
  forget_the_selection();

  // rel-0163 r4 (Codex R2): an unsendable notice names the op it was made for'''),
])
print('ok')
