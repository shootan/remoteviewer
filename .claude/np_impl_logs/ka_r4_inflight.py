import os
base = r'C:\work\gnlink-wt\ka-pc\apps\native_poc\src' + os.sep

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
'''  ClientInputQueue inputQueue;
''', '''  ClientInputQueue inputQueue;
  // ka-pc-impl r4 (Codex R3): raised by the control thread BEFORE it takes the next action while input is queued,
  // lowered once that action's exchange (its ack) is over -- so "the queue is empty and nothing is in flight" means
  // every queued input was acknowledged by the host. The owner fence waits on it.
  std::atomic<bool> inputInFlight{false};
''')])
edit('viewer_control_client.cpp', [(
'''#endif
    if (ctx.control.scheduler.NextAction(
            nowUs, capture_client_control_metrics_snapshot(ctx), &ctx.picker.windowPanel,''', '''#endif
    // r4 (Codex R3): raised before the dequeue (see ViewerControlState::inputInFlight), lowered after the exchange.
    if (!ctx.control.inputQueue.Empty()) ctx.control.inputInFlight.store(true);
    struct InFlightDone {
      std::atomic<bool>* f;
      ~InFlightDone() { f->store(false); }
    } inFlightDone{&ctx.control.inputInFlight};
    if (ctx.control.scheduler.NextAction(
            nowUs, capture_client_control_metrics_snapshot(ctx), &ctx.picker.windowPanel,''')])
edit('viewer_window_proc.cpp', [(
'''        const bool delivered = ctx.control.inputQueue.Empty() && !ctx.control.udpControl.TxPending();''',
'''        // every queued input taken AND acknowledged (input actions are request / response: the host acks each one)
        const bool delivered = ctx.control.inputQueue.Empty() && !ctx.control.inputInFlight.load() &&
                               !ctx.control.udpControl.TxPending();''')])
print('ok')
