p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_selection_gate_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
old = '''}  // namespace

int main() {
  test_pick_ack_first_frame_reveal();
  test_ack_failure_and_not_pending();
  test_reveal_races();'''
new = '''// rel-0163 r3 (Codex codex_rel_0163_e1_decision.md): the local (logon-screen desktop) transaction.
void test_local_ackless_selection() {
  {
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    CHECK(g.pending.load() && !g.awaitingAck.load() && g.localAckless.load());
    CHECK(g.LocalUnbound());
    // Before its keyframe binds a generation: frames pass the gate (the decoder waits for a keyframe anyway),
    // but nothing can reveal -- no acked-pending state, a recorded candidate does not commit.
    CHECK(g.AdmitGeneration(7) == SelectionAdmit::Accept);
    CHECK(!g.AckedSelectionPending());
    CHECK(g.RecordReveal(7, op));
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    // The first admitted keyframe binds generation 7, once.
    CHECK(g.BindLocalGeneration(7));
    CHECK(!g.LocalUnbound());
    CHECK(!g.BindLocalGeneration(9));
    CHECK(g.expectedGeneration.load() == 7);
    CHECK(g.AckedSelectionPending());
    // Another generation drops now, as after an ack.
    CHECK(g.AdmitGeneration(9) == SelectionAdmit::DropWrongGeneration);
    CHECK(g.AdmitGeneration(7) == SelectionAdmit::Accept);
    // A candidate of another generation, or of an older op, does not commit; the right one does.
    CHECK(g.RecordReveal(9, op));
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    CHECK(g.RecordReveal(7, op - 1));
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    CHECK(g.RecordReveal(7, op));
    CHECK(g.CommitReveal());
    CHECK(g.activeStreamGeneration.load() == 7);
  }
  {
    // Generation 0 is never bound (nothing names it).
    SelectionGateState g;
    g.BeginLocal();
    CHECK(!g.BindLocalGeneration(0));
    CHECK(g.LocalUnbound());
  }
  {
    // Cancel (Clear) ends it: no bind, no reveal, the local mode is gone.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    g.Clear();
    CHECK(!g.localAckless.load() && !g.LocalUnbound());
    CHECK(!g.BindLocalGeneration(7));
    CHECK(g.RecordReveal(7, op));
    CHECK(!g.CommitReveal());
  }
  {
    // A new pick after a local one is an ordinary (acked) transaction again.
    SelectionGateState g;
    g.BeginLocal();
    g.Clear();
    g.Begin();
    CHECK(!g.localAckless.load() && g.awaitingAck.load());
    CHECK(g.AdmitGeneration(7) == SelectionAdmit::DropAwaitingAck);
  }
  {
    // An ordinary op whose WindowSelect(0) was not sendable continues as local -- that op only.
    SelectionGateState g;
    g.Begin();
    const uint64_t op = g.epoch.load();
    CHECK(!g.ConvertToLocal(op + 1));  // another op
    CHECK(g.awaitingAck.load());
    CHECK(g.ConvertToLocal(op));
    CHECK(!g.awaitingAck.load() && g.localAckless.load() && g.LocalUnbound() && g.epoch.load() == op);
    CHECK(!g.ConvertToLocal(op));  // not twice
    CHECK(g.BindLocalGeneration(3));
    CHECK(g.RecordReveal(3, op));
    CHECK(g.CommitReveal());
  }
  {
    // A late WindowSelected ack never completes a local op as if it were the host's.
    SelectionGateState g;
    g.BeginLocal();
    CHECK(g.ApplyAck(true, 5) == SelectionAck::Acked);  // (an ack has no op of its own: ...)
    // ...it sets the generation the ordinary way; the op still reveals only on that generation's frame.
    CHECK(g.AdmitGeneration(6) == SelectionAdmit::DropWrongGeneration);
  }
  {
    // Not pending: nothing to convert or bind.
    SelectionGateState g;
    CHECK(!g.ConvertToLocal(g.epoch.load()));
    CHECK(!g.BindLocalGeneration(4));
  }
}

}  // namespace

int main() {
  test_pick_ack_first_frame_reveal();
  test_ack_failure_and_not_pending();
  test_reveal_races();
  test_local_ackless_selection();'''
assert s.count(old) == 1
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
