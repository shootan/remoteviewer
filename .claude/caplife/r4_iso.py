p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_window_proc_isolated_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old = '''  // The rest is an ordinary host whose role is known (its pong came, no logon-screen flag): ack-based picks.
  ctx.session.hostRoleKnown.store(true, std::memory_order_release);
  ctx.session.hostCaptureFlags.store(0, std::memory_order_relaxed);'''
new = '''  // rel-0163 r4 (Codex R1): the first pong lands exactly between the UI's role read and its hold -- the control
  // half finds no held pick (no message), the UI half re-checks the role after holding: the pick runs ONCE, with
  // no second click. A logon-screen host: it runs as a local selection.
  forget_the_selection();
  ctx.session.hostRoleKnown.store(false, std::memory_order_release);
  ctx.session.hostCaptureFlags.store(remote60::native_poc::kCaptureFlagPreloginV1, std::memory_order_relaxed);
  int roleLandings = 0;
  ctx.sel.testAfterRoleRead = [&] {
    ++roleLandings;
    ctx.session.hostRoleKnown.store(true, std::memory_order_seq_cst);
    remote60::native_poc::viewer::note_host_role_known(ctx, /*firstTime=*/true);  // sees no held pick yet
  };
  const uint64_t epochBefore = ctx.sel.epoch.load();
  click_the_card();
  ctx.sel.testAfterRoleRead = nullptr;
  pump(100);
  ok(roleLandings == 1 && ctx.sel.pending.load() && ctx.sel.localAckless.load() && ctx.sel.deferredPick.load() == 0 &&
         ctx.sel.epoch.load() == epochBefore + 1,
     "r4 R1: a pong between the role read and the hold -- the pick still runs, once, with no second click");
  PostMessageW(hwnd, remote60::native_poc::viewer::kMsgRunDeferredPick, 0, 0);  // a stray / duplicate wake
  pump(60);
  ok(ctx.sel.epoch.load() == epochBefore + 1, "r4 R1: ...a second wake runs nothing (the pick was taken once)");
  // A pick held on an EARLIER connection is that connection's: its wake on this one runs nothing.
  forget_the_selection();
  ctx.sel.deferredRun.store(ctx.session.controlRunGen.load() + 7, std::memory_order_release);
  ctx.sel.deferredPick.store(1, std::memory_order_release);
  const uint64_t epochOld = ctx.sel.epoch.load();
  PostMessageW(hwnd, remote60::native_poc::viewer::kMsgRunDeferredPick, 0, 0);
  pump(60);
  ok(ctx.sel.epoch.load() == epochOld && !ctx.sel.pending.load(), "r4 R1: an older connection's held pick runs nothing");

  // rel-0163 r4 (Codex R2): an unsendable notice names the op it was made for -- an older op's notice never
  // converts or clears the current one.
  forget_the_selection();
  ctx.session.hostCaptureFlags.store(0, std::memory_order_relaxed);  // an ordinary host: an acked op A
  click_the_card();
  const uint64_t opA = ctx.sel.epoch.load();
  ok(ctx.sel.pending.load() && ctx.sel.awaitingAck.load(), "r4 R2: op A (ordinary, waits for its ack)");
  forget_the_selection();
  ctx.session.hostCaptureFlags.store(remote60::native_poc::kCaptureFlagPreloginV1, std::memory_order_relaxed);
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  click_the_card();
  const uint64_t opB = ctx.sel.epoch.load();
  remote60::native_poc::viewer::resolve_unsendable_pc_selection(ctx, 0, opA);
  remote60::native_poc::viewer::resolve_unsendable_pc_selection(ctx, 0x1234, opA);
  ok(opB != opA && ctx.sel.pending.load() && ctx.sel.localAckless.load() && ctx.sel.LocalUnbound(opB),
     "r4 R2: A's notices (desktop and window) after A was cancelled and local B began: B untouched");
  forget_the_selection();
  ctx.session.hostCaptureFlags.store(0, std::memory_order_relaxed);
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  click_the_card();
  const uint64_t opC = ctx.sel.epoch.load();
  remote60::native_poc::viewer::resolve_unsendable_pc_selection(ctx, 0, opC);
  ok(ctx.sel.pending.load() && ctx.sel.localAckless.load() && ctx.sel.LocalUnbound(opC),
     "r4 R2: the current op's own desktop notice continues it as local (once)");
  forget_the_selection();
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  click_the_card();
  const uint64_t opD = ctx.sel.epoch.load();
  remote60::native_poc::viewer::resolve_unsendable_pc_selection(ctx, 0x1234, opD);
  ok(!ctx.sel.pending.load(), "r4 R2: the current op's own window notice refuses it (no waiting forever)");

  // The rest is an ordinary host whose role is known (its pong came, no logon-screen flag): ack-based picks.
  ctx.session.hostRoleKnown.store(true, std::memory_order_release);
  ctx.session.hostCaptureFlags.store(0, std::memory_order_relaxed);'''
assert s.count(old) == 1
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
