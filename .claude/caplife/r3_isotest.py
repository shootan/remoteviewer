p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_window_proc_isolated_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old = '''  // ---------------------------------------------------------------- the gate open
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  forget_the_selection();
  click_the_card();
  ok(ctx.sel.pending.load(std::memory_order_acquire),
     "with the gate open, a click on the desktop card starts a selection");'''
new = '''  // ---------------------------------------------------------------- rel-0163 r3: the host's role not yet known
  // (no pong yet): the click is held as this connection's intent, nothing is sent, no transaction begins.
  ctx.session.hostRoleKnown.store(false, std::memory_order_release);
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  forget_the_selection();
  click_the_card();
  ok(!ctx.sel.pending.load(std::memory_order_acquire) && ctx.sel.deferredPick.load() == 1,
     "r3: before the host's role is known, a click on the desktop card is held (no transaction, nothing sent)");
  // The rest is an ordinary host whose role is known (its pong came, no logon-screen flag): ack-based picks.
  ctx.session.hostRoleKnown.store(true, std::memory_order_release);
  ctx.session.hostCaptureFlags.store(0, std::memory_order_relaxed);

  // ---------------------------------------------------------------- the gate open
  ctx.input.suppressMouseUntilUs.store(0, std::memory_order_relaxed);
  forget_the_selection();
  ok(ctx.sel.deferredPick.load() == 0, "r3: ...and a cleared selection drops the held click too");
  click_the_card();
  ok(ctx.sel.pending.load(std::memory_order_acquire) && ctx.sel.awaitingAck.load() && !ctx.sel.localAckless.load(),
     "with the gate open, a click on the desktop card starts a selection (an ordinary host: waits for its ack)");'''
assert s.count(old) == 1
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
