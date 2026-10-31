p = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\viewer_selection_gate_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
start = s.index('// rel-0163 r3 (Codex codex_rel_0163_e1_decision.md): the local (logon-screen desktop) transaction.')
end = s.index('}  // namespace\n\nint main() {')
new = '''// rel-0163 r3/r4 (Codex codex_rel_0163_e1_decision.md, codex_rel_0163_src_r3.md): the local (logon-screen
// desktop) transaction -- bound by op token, never by a live read.
void test_local_ackless_selection() {
  {
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    CHECK(g.pending.load() && !g.awaitingAck.load() && g.localAckless.load());
    CHECK(g.LocalUnbound(op) && g.IsLiveLocal(op));
    // Before its decoded keyframe binds a generation: frames pass the gate (the decoder waits for a keyframe
    // anyway), but nothing can reveal -- no acked-pending state, a recorded candidate does not commit.
    CHECK(g.AdmitGeneration(7) == SelectionAdmit::Accept);
    CHECK(!g.AckedSelectionPending());
    CHECK(g.RecordReveal(7, op));
    CHECK(!g.CommitReveal());
    g.ReleaseRevealLatch();
    // Bound for THIS op, once.
    CHECK(!g.BindLocalGeneration(op + 1, 7));  // another op's token
    CHECK(g.BindLocalGeneration(op, 7));
    CHECK(!g.LocalUnbound(op));
    CHECK(!g.BindLocalGeneration(op, 9));
    CHECK(g.expectedGeneration.load() == 7);
    CHECK(g.AckedSelectionPending());
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
    // R3 (ABA): op A's frame held -> A cancelled -> B begins (unbound again) -> A's frame comes back: it binds
    // nothing of B's, and is not B's live op; B binds with its own token.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    g.Clear();
    g.BeginLocal();
    const uint64_t b = g.epoch.load();
    CHECK(a != b);
    CHECK(!g.BindLocalGeneration(a, 5));
    CHECK(!g.IsLiveLocal(a) && g.IsLiveLocal(b) && g.LocalUnbound(b));
    CHECK(g.RecordReveal(5, a));
    CHECK(!g.CommitReveal());  // A's candidate never reveals B
    g.ReleaseRevealLatch();
    CHECK(g.BindLocalGeneration(b, 6));
    CHECK(g.RecordReveal(6, b));
    CHECK(g.CommitReveal());
  }
  {
    // Generation 0 is never bound (nothing names it).
    SelectionGateState g;
    g.BeginLocal();
    CHECK(!g.BindLocalGeneration(g.epoch.load(), 0));
    CHECK(g.LocalUnbound(g.epoch.load()));
  }
  {
    // Cancel (Clear) ends it: no bind, no reveal, the local mode is gone.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    g.Clear();
    CHECK(!g.localAckless.load() && !g.LocalUnbound(op) && !g.IsLiveLocal(op));
    CHECK(!g.BindLocalGeneration(op, 7));
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
    // R2: an ordinary op whose WindowSelect(0) was not sendable continues as local -- by ITS tag only; another
    // op's tag changes nothing.
    SelectionGateState g;
    g.Begin();
    const uint64_t op = g.epoch.load();
    CHECK(!g.ConvertToLocal(op + 1));
    CHECK(!g.ConvertToLocal(op - 1));
    CHECK(g.awaitingAck.load() && !g.localAckless.load());
    CHECK(g.ConvertToLocal(op));
    CHECK(!g.awaitingAck.load() && g.localAckless.load() && g.LocalUnbound(op) && g.epoch.load() == op);
    CHECK(!g.ConvertToLocal(op));  // not twice
    CHECK(g.BindLocalGeneration(op, 3));
    CHECK(g.RecordReveal(3, op));
    CHECK(g.CommitReveal());
  }
  {
    // R2: A's unsendable notice after A was cancelled and a local B began: B is untouched.
    SelectionGateState g;
    g.Begin();
    const uint64_t a = g.epoch.load();
    g.Clear();
    g.BeginLocal();
    const uint64_t b = g.epoch.load();
    CHECK(!g.ConvertToLocal(a));
    CHECK(g.pending.load() && g.localAckless.load() && g.LocalUnbound(b));
  }
  {
    // R4: a late host ack -- success or failure, before or after the bind -- never touches a local op.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t op = g.epoch.load();
    CHECK(g.ApplyAck(true, 5) == SelectionAck::IgnoredLocal);
    CHECK(g.ApplyAck(false, 0) == SelectionAck::IgnoredLocal);
    CHECK(g.expectedGeneration.load() == 0 && g.pending.load() && g.localAckless.load() && g.LocalUnbound(op));
    CHECK(g.BindLocalGeneration(op, 8));
    CHECK(g.ApplyAck(true, 5) == SelectionAck::IgnoredLocal);
    CHECK(g.ApplyAck(false, 0) == SelectionAck::IgnoredLocal);
    CHECK(g.expectedGeneration.load() == 8 && g.pending.load());
    // ...and the local op still completes on its own frame.
    CHECK(g.RecordReveal(8, op));
    CHECK(g.CommitReveal());
  }
  {
    // Control: an ordinary op's own ack still works.
    SelectionGateState g;
    g.Begin();
    CHECK(g.ApplyAck(true, 4) == SelectionAck::Acked);
    CHECK(g.RecordReveal(4, g.epoch.load()));
    CHECK(g.CommitReveal());
  }
  {
    // Not pending: nothing to convert or bind.
    SelectionGateState g;
    CHECK(!g.ConvertToLocal(g.epoch.load()));
    CHECK(!g.BindLocalGeneration(g.epoch.load(), 4));
  }
}

'''
s = s[:start] + new + s[end:]
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
