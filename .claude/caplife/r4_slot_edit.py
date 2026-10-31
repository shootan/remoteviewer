p = r'C:\work\gnlink-wt\apk-decoder-liveness\apps\native_poc\src\decoder_pending_slot_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
N = '\r\n'
anchor = '  std::printf("\\nRESULT: %s  (%d checks, %d failed)\\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);'
assert s.count(anchor) == 1
add = r'''  // apk-decoder-liveness r4 (Codex codex_apk_decoder_liveness_src_r1.md N1): the queue is a RESERVATION. The receive
  // side's Admit no longer moves an unbound local selection's candidate; the worker reserves it for the frame it is
  // queueing, and a control-side change (Prepare / Reset / answer) makes the reservation stale.
  {
    SelectionAckGate gate;
    gate.Prepare(7);
    gate.BeginLocal(7);
    const auto r = gate.ReserveQueue(101, true);  // the worker starts queueing 101
    const bool admitted = gate.Admit(202, true);  // RX: 202 arrives meanwhile -- it passes, and changes nothing
    check("r4: reserved 101; RX's Admit of 202 passes (to the inbox) without moving the reservation",
          r.valid && admitted && !gate.PermitsQueue(202, true) && gate.PermitsQueue(101, true));
    gate.KeyLost(202);  // RX drops an IDR (inbox full): owed again, the reservation stays
    check("r4: a lost IDR on RX re-owes and leaves the reservation", gate.OwedToken() != 0 && gate.PermitsQueue(101, true) &&
                                                                        !gate.PermitsQueue(202, true));
    check("r4: the commit binds the reserved 101 and pays the owed IDR",
          gate.CommitQueue(r) && gate.LocalBoundFor(7) == 101 && gate.OwedToken() == 0);
    check("r4: then 202 is refused at its own queue", !gate.PermitsQueue(202, true));
  }
  {
    SelectionAckGate gate;
    gate.Prepare(8);
    gate.BeginLocal(8);
    const auto r = gate.ReserveQueue(101, true);
    gate.Prepare(9);  // a new pick while 101 is being queued
    gate.BeginLocal(9);
    check("r4: a reservation made before a new pick is stale: its commit binds and pays nothing",
          !gate.CommitQueue(r) && gate.LocalBoundFor(9) == 0 && gate.OwedToken() != 0);
    gate.AbortQueue(r);  // ...and its abort touches nothing of the new pick
    const auto r2 = gate.ReserveQueue(303, true);
    check("r4: the new pick reserves and binds its own IDR", r2.valid && gate.CommitQueue(r2) && gate.LocalBoundFor(9) == 303);
  }
  {
    SelectionAckGate gate;
    gate.Prepare(10);
    gate.BeginLocal(10);
    const auto r = gate.ReserveQueue(101, true);
    gate.AbortQueue(r);  // no input buffer: the candidacy ends; the next IDR may be any generation
    check("r4: an aborted reservation frees the candidacy", gate.PermitsQueue(202, true) && gate.LocalBoundFor(10) == 0);
    check("r4: a delta never reserves an unbound local selection", !gate.ReserveQueue(202, false).valid);
    gate.Reset();
    check("r4: Reset makes any reservation stale", !gate.ReservationCurrent(r));
  }
  {
    SelectionAckGate gate;  // an answered selection: the reservation is the same gate, its commit pays the owed IDR
    gate.Prepare(11);
    gate.Admit(500, true);  // dropped while the answer is awaited -> owed once answered
    gate.OnAck(true, 500, 11);
    const auto r = gate.ReserveQueue(500, true);
    check("r4: answered: a reserved IDR of the answered generation pays at commit",
          gate.OwedToken() != 0 && gate.CommitQueue(r) && gate.OwedToken() == 0);
    check("r4: answered: another generation does not reserve", !gate.ReserveQueue(501, true).valid);
  }
'''.replace('\n', N)
s = s.replace(anchor, add + anchor)
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
