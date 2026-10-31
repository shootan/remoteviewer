import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep
NL = chr(92) + 'n'

def edit(name, pairs):
    p = base + name
    b = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in b
    s = b.replace('\r\n', '\n')
    for a, c in pairs:
        assert s.count(a) == 1, (name, s.count(a), a[:90])
        s = s.replace(a, c)
    open(p, 'wb').write((s.replace('\n', '\r\n') if crlf else s).encode('utf-8'))

edit('viewer_control_state.hpp', [(
'''// ka-pc-impl r5 (Codex R3-a): the control thread's count of the input exchanges it RUNS (the UI thread's owner fence
// reads them; viewer_owner_fence.hpp fence_release_verdict). An acknowledgement is the host's InputAck, nothing less.
struct InputExchangeCounters {
  std::atomic<uint64_t> taken{0};   // an input action whose exchange was started
  std::atomic<uint64_t> acked{0};   // ... that ended with the host's InputAck
  std::atomic<uint64_t> failed{0};  // ... that ended any other way (a failed exchange, a wrong reply)
};
''', '''// ka-pc-impl r6 (Codex R3-a, F1, F2): the owner fence's ONE state, under ONE lock -- see viewer_owner_fence.hpp for
// every operation on it (they are the only code that touches these fields).
struct OwnerFenceGate {
  mutable std::mutex mu;
  bool fenced = false;           // the owner ended this session: only a release may start a write
  bool releaseExchange = false;  // the exchange the control thread is running is a counted release
  bool inFlight = false;         // an input taken off the queue whose exchange has not ended
  uint64_t taken = 0, acked = 0, failed = 0, skipped = 0;      // input exchanges: started / InputAck / not / not sent
  uint64_t taken0 = 0, acked0 = 0, failed0 = 0, skipped0 = 0;  // as they stood when the fence was raised
};
'''), (
'''  std::atomic<bool> inputInFlight{false};
  InputExchangeCounters inputExchanges;
''', '''  OwnerFenceGate fenceGate;
''')])

edit('viewer_session_state.hpp', [(
'''  // r5 (Codex R3-a): the input-exchange counters as the fence found them, before its releases (UI thread only)
  uint64_t fenceTaken0 = 0, fenceAcked0 = 0, fenceFailed0 = 0;''', '')])

# ---------------------------------------------------------------- the fence and its verdict (UI thread)
edit('viewer_window_proc.cpp', [(
'''        // 0. r5 (Codex R3-b): what has not left yet and is not a release is cancelled at once -- every queued down /
        //    text / move / wheel, the pending clipboard text (and the clipboard sync: no send, no poll), a typed
        //    password waiting to be sealed (zeroed). Image / file offers stop. The counters are taken now, before
        //    the releases, for the fence's verdict (R3-a).
        const uint32_t droppedInputs = owner_fence_cancel_pending(
            ctx.control.inputQueue, ctx.control.clipboard, ctx.session.preloginCredMu, ctx.session.preloginCredPassword,
            ctx.session.preloginCredRequested);
        ctx.control.clipImage.Stop();
        ctx.control.fileCopy.Stop();
        {
          const FenceCounterSnapshot at = snapshot(ctx.control.inputExchanges);
          ctx.session.fenceTaken0 = at.taken;
          ctx.session.fenceAcked0 = at.acked;
          ctx.session.fenceFailed0 = at.failed;
        }''', '''        // 0. r6 (Codex F1/F2): the gate FIRST -- from this moment nothing but a counted release may start a control
        //    write, no bulk (image / file) datagram goes, and the input-exchange baseline is taken in the same critical
        //    section. Nothing here waits on a worker: the image / file workers are stopped by the session's own teardown
        //    (shutdown_viewer); until then their sends are refused by the gate.
        fence_raise(ctx.control.fenceGate);
        //    r5 (Codex R3-b): what has not left yet and is not a release is cancelled at once -- every queued down /
        //    text / move / wheel, the pending clipboard text (and the clipboard sync: no send, no poll), a typed
        //    password waiting to be sealed (zeroed).
        const uint32_t droppedInputs = owner_fence_cancel_pending(
            ctx.control.inputQueue, ctx.control.clipboard, ctx.session.preloginCredMu, ctx.session.preloginCredPassword,
            ctx.session.preloginCredRequested);'''), (
'''        // r5 (Codex R3-a): confirmed only by the host's InputAck for every release exchange since the fence -- an empty
        // queue or an idle channel is not an acknowledgement (a failed exchange leaves both empty)
        const FenceVerdict v = fence_release_verdict(
            FenceCounterSnapshot{ctx.session.fenceTaken0, ctx.session.fenceAcked0, ctx.session.fenceFailed0},
            snapshot(ctx.control.inputExchanges), ctx.control.inputQueue.Empty(), ctx.control.inputInFlight.load());''',
'''        // r5/r6 (Codex R3-a, F1): confirmed only by the host's InputAck for every input exchange since the fence, all of
        // it read in one critical section with the control thread's takes (an empty queue or an idle channel is not an
        // acknowledgement)
        const FenceVerdict v = fence_verdict(ctx.control.fenceGate, ctx.control.inputQueue);''')])

# ---------------------------------------------------------------- bulk datagrams
edit('viewer_clip_image_wiring.cpp', [(
'''  ctx.control.clipImage.Start(
      [&ctx](const void* data, size_t len) -> bool {''', '''  ctx.control.clipImage.Start(
      [&ctx](const void* data, size_t len) -> bool {
        if (!gate_bulk_send_allowed(ctx.control.fenceGate)) return false;  // r6 (Codex F2): none after the owner fence'''), (
'''  ctx.control.fileCopy.Start(
      [&ctx](const void* data, size_t len) -> bool {''', '''  ctx.control.fileCopy.Start(
      [&ctx](const void* data, size_t len) -> bool {
        if (!gate_bulk_send_allowed(ctx.control.fenceGate)) return false;  // r6 (Codex F2): none after the owner fence''')])
print('ok')
