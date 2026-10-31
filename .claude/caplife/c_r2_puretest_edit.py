p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\decoder_chain_inbox_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''  {
    Inbox in = make();
    in.Offer(d(100));
    int dropped = 0;
    for (int i = 0; i < 29; ++i) dropped += in.Offer(d(5)).admit != ChainAdmit::Admitted ? 1 : 0;
    const bool stillDropped = in.Offer(d(6)).admit == ChainAdmit::Stale;  // the 30th in a row
    const bool resynced = in.Offer(d(7)).admit == ChainAdmit::Admitted && in.orderResyncs() == 1;
    const bool after = in.Offer(d(8)).admit == ChainAdmit::Admitted && in.Offer(d(8)).admit == ChainAdmit::Duplicate;
    check("safety valve: 30 older AUs in a row are dropped, then the order restarts from the next (counted), and is "
          "kept from there", dropped == 29 && stillDropped && resynced && after);
  }
''', '''  {
    // C r2 (Codex N3): no automatic re-ordering. Codex's probe on 5fe90a9: seq 100, then seq 99 x31 -> the 31st Admitted.
    Inbox in = make();
    in.Offer(d(100));
    int admitted = 0;
    for (int i = 0; i < 40; ++i) admitted += in.Offer(d(99)).admit == ChainAdmit::Admitted ? 1 : 0;
    for (int i = 0; i < 40; ++i) admitted += in.Offer(d(100)).admit == ChainAdmit::Admitted ? 1 : 0;
    check("N3: 40 older and 40 duplicate AUs in a row: every one dropped (no automatic resync)", admitted == 0 &&
          in.stale() == 40 && in.duplicates() == 40, "admitted=" + std::to_string(admitted));
    check("N3: normal increase goes on after them", in.Offer(d(101)).admit == ChainAdmit::Admitted);
    in.Clear();  // an explicit reset (a new pick / binding / stream)
    const bool fresh = in.Offer(k(5)).admit == ChainAdmit::Admitted && in.Offer(d(6)).admit == ChainAdmit::Admitted;
    check("N3: after an explicit reset the order starts over at the new IDR", fresh);
  }
''')
rep('''  std::puts("\\n--- episode: schedule");''', '''  {
    // C r2 (Codex N1-a): Codex's counterexample on 5fe90a9 -- delta12, delta13, IDR14 queued, MarkLoss(11): the first Pop
    // was delta12 (key 0, gap 0): the broken suffix handed out ahead of the recovery IDR.
    Inbox in = make();
    in.Offer(d(12));
    in.Offer(d(13));
    in.Offer(k(14));
    in.Offer(d(15));
    const size_t dropped = in.MarkLoss(11);
    Au first;
    const bool popped = in.Pop(&first);
    check("N1-a: a loss at 11 with delta12, delta13 queued before IDR14: they are dropped, the first Pop is IDR14, no gap",
          dropped == 2 && popped && first.seq == 14 && first.key && !in.gapOpen() && seqs(drain(in)) == "15",
          "dropped=" + std::to_string(dropped) + " first=" + std::to_string(first.seq));
  }
  {
    Inbox in = make();
    in.Offer(d(12));
    in.Offer(d(13));
    const size_t dropped = in.MarkLoss(11);
    check("N1-a: no IDR queued after the loss: every queued delta dropped, the gap opens after the newest (13)",
          dropped == 2 && in.frames() == 0 && in.gapOpen() && in.gapAfterSeq() == 13 &&
              in.Offer(d(14)).admit == ChainAdmit::GapDelta);
  }
  {
    Inbox in = make();
    in.Offer(d(4));
    in.Offer(d(5));
    const size_t dropped = in.MarkLoss(2, true);
    check("N1-b: a usable IDR (3) held outside the inbox: the queued 4, 5 are its suffix -- kept, no gap",
          dropped == 0 && !in.gapOpen() && seqs(drain(in)) == "4,5");
  }
  {
    Inbox in = make();
    in.MarkLoss(2);
    in.OnKeyCommitted(3);
    const bool closed = !in.gapOpen() && in.Offer(d(4)).admit == ChainAdmit::Admitted;
    Inbox later = make();
    later.MarkLoss(2);
    later.Offer(d(9));  // refused: the gap moves to 9
    later.OnKeyCommitted(3);
    check("N1-b: an IDR committed after the gap's loss point closes it (4 admitted); one committed BEFORE a later loss "
          "(9) leaves that loss's gap", closed && later.gapOpen() && later.gapAfterSeq() == 9);
  }

  std::puts("\\n--- episode: schedule");''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
