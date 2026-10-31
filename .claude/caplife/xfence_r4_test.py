p = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/client_session_xbutton_fence_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''//     In each, the debt ends with that answered up: a park afterwards confirms (nothing held), X2 and left unaffected.''',
    '''//     In each, the debt ends with that answered up: a park afterwards confirms (nothing held), X2 and left unaffected.
//   r4 (codex_apk_xfence_src_r3.md N1) -- a down QUEUED while the host took X, not sent yet; a no-X Pong; its up
//   refused at the entry; an X Pong; then the sender reaches the down: it must not go alone (no orphan down). The
//   loop is held on two Pings in a row (a Ping goes before queued input once it is due), so the down stays queued
//   through both Pongs.''')

rep('''  std::atomic<bool> holdPongs{false};  // r2: a Ping is answered only once released (the client's loop waits on it)
  std::atomic<int> heldPings{0};''', '''  std::atomic<bool> holdPongs{false};  // r2: a Ping is answered only once released (the client's loop waits on it)
  std::atomic<int> heldPings{0};
  std::atomic<int> releaseOne{0};  // r4: answers ONE held Ping (the next stays held while holdPongs)''')
rep('''        if (holdPongs.load()) {
          ++heldPings;
          while (holdPongs.load() && !stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }''', '''        if (holdPongs.load()) {
          ++heldPings;
          while (holdPongs.load() && !stop.load()) {
            int one = releaseOne.load();
            if (one > 0 && releaseOne.compare_exchange_strong(one, one - 1)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
        }''')

rep('''// The ledger alone: the same transitions, both sides, no session.''', '''// r4: a queued (unsent) down whose up is refused at the entry, then X again before the sender reaches the down.
void queued_down_refused_up() {
  std::printf("\\n--- r4: a queued X1 down, its up refused (no X), X again before the sender takes the down ---\\n");
  FakeHost h;
  if (!h.Start()) return check("r4 orphan: fake host starts", false);
  n::ClientSessionController c;
  check("r4 orphan: connected to an X host, pause honoured", connect_and_know_role(c, h, kX));
  const size_t mark = h.InputCount();
  const bool heldA = hold_next_pong(h);  // the loop waits on Ping A
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);   // admitted (X), queued, not sent
  const bool mv = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX1 | n::kMouseWireLeft);  // the press held
  std::this_thread::sleep_for(std::chrono::milliseconds(150));  // past the Ping interval: Ping B goes before input
  const int beforeB = h.heldPings.load();
  h.pongFlags = 0;
  h.releaseOne = 1;  // Pong A (no X); Ping B is held next
  const bool noX = wait_until([&] { return c.HostCaptureFlags() == 0; }, 3000);
  const bool heldB = wait_until([&] { return h.heldPings.load() > beforeB; }, 3000);
  const size_t whileHeld = h.InputCount() - mark;  // the down is still queued (nothing sent since)
  const bool up = c.QueueInputEvent(3, 11, 11, 0, 5, 0);  // no X now: refused at the entry
  h.pongFlags = kX;
  h.holdPongs = false;  // Pong B (X) and every later one
  const bool xAgain = wait_until([&] { return c.HostCaptureFlags() == kX; }, 3000);
  const bool flowed = wait_until([&] { return h.InputCount() >= mark + 1; }, 3000);  // the move
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const Tally t = tally(h.Inputs(), mark);
  const std::string d = t.str() + " down=" + std::to_string(down) + " up=" + std::to_string(up) +
                        " sentWhileHeld=" + std::to_string(whileHeld) + " X1down=" + std::to_string(t.count(2, 5)) +
                        " X1up=" + std::to_string(t.count(3, 5));
  check("r4 orphan: the phases held -- down and move queued under X, Pong A no X, the up refused, Pong B X, nothing "
        "sent in between",
        heldA && down && mv && noX && heldB && whileHeld == 0 && !up && xAgain, d);
  check("r4 orphan: no orphan down -- the cancelled X1 down is not sent, no X1 up either, the move goes without X1",
        flowed && t.count(2, 5) == 0 && t.count(3, 5) == 0 && t.xBits == 0 && t.all.size() >= 1 && t.all[0].kind == 1 &&
            t.all[0].buttons == n::kMouseWireLeft,
        d);
  const size_t mark2 = h.InputCount();
  const bool parked = c.BeginPark() && wait_until([&] { return c.ParkPhase() == n::ClientParkPhase::Parked; }, 3000);
  const Tally t2 = tally(h.Inputs(), mark2);
  check("r4 orphan: nothing held or owed -- a park confirms with nothing to release", parked && t2.total == 0 &&
        c.ParkReleasedCount() == 0, t2.str() + " parkReleased=" + std::to_string(c.ParkReleasedCount()));
  if (parked) {
    (void)c.ResumeFromPark();
    (void)c.RequestStreamActive(true);
  }
  const size_t mark3 = h.InputCount();
  const bool d2 = c.QueueInputEvent(2, 20, 20, 0, 5, n::kMouseWireX1);
  const bool u2 = c.QueueInputEvent(3, 20, 20, 0, 5, 0);  // a quick pair, both queued: whole
  const bool sent2 = wait_until([&] { return h.InputCount() >= mark3 + 2; }, 3000);
  const Tally t3 = tally(h.Inputs(), mark3);
  check("r4 orphan: the next X1 press (a quick pair, both queued) goes whole", d2 && u2 && sent2 &&
        t3.count(2, 5) == 1 && t3.count(3, 5) == 1, t3.str());
  c.Disconnect();
}

// The ledger alone: the same transitions, both sides, no session.''')

# ledger-level r4 checks, before the last ledger block
rep('''  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);  // X1 refused''', '''  // r4 N1: queued down, its up refused at the entry, X again before the sender: the down is cancelled
  {
    L l;
    uint16_t b = 0x8;
    const bool d = l.AdmitQueued(true, 2, 5, &b);
    l.NoteQueued(2, 5, 41);
    uint16_t u = 0;
    const bool up = l.AdmitQueued(false, 3, 5, &u);  // no X: refused, the queued down goes with it
    const bool cancelled = l.CancelledDowns(5) == 1 && l.PendingDowns(5) == 0;
    n::ControlOutboundAction& x = *ev(2, 5, 0x8);
    x.inputEvent.seq = 41;
    const bool sent = l.AdmitSendAction(true, &x);  // X again: the sender reaches it
    n::ControlOutboundAction& m = *ev(1, 0, 0x9);
    m.inputEvent.seq = 42;
    const bool mvOk = l.AdmitSendAction(true, &m);
    check("ledger r4: a queued down whose up was refused at the entry is cancelled at sending (X again or not), its "
          "bit hidden; not owed",
          d && !up && cancelled && !sent && mvOk && m.inputEvent.buttons == 0x1 && !l.SendOwed(5) && l.CancelledDowns(5) == 0);
  }
  {
    L l;  // a quick pair queued: its up admitted -- the down is NOT cancelled
    uint16_t b = 0x8, u = 0;
    (void)l.AdmitQueued(true, 2, 5, &b);
    l.NoteQueued(2, 5, 7);
    (void)l.AdmitQueued(true, 3, 5, &u);
    l.NoteQueued(3, 5, 8);
    n::ControlOutboundAction& x = *ev(2, 5, 0x8);
    x.inputEvent.seq = 7;
    const bool dn = l.AdmitSendAction(true, &x);
    l.OnSentAction(x);
    n::ControlOutboundAction& y = *ev(3, 5, 0);
    y.inputEvent.seq = 8;
    const bool upS = l.AdmitSendAction(true, &y);
    l.OnSentAction(y);
    check("ledger r4: a quick pair (down and up both queued) goes whole, nothing cancelled",
          dn && upS && !l.SendOwed(5) && l.CancelledDowns(5) == 0 && l.PendingDowns(5) == 0);
  }
  {
    L l;  // a down already TAKEN by the sender (in flight), its up refused: not cancellable -- owed once answered
    uint16_t b = 0x8, u = 0;
    (void)l.AdmitQueued(true, 2, 5, &b);
    l.NoteQueued(2, 5, 9);
    n::ControlOutboundAction& x = *ev(2, 5, 0x8);
    x.inputEvent.seq = 9;
    const bool dn = l.AdmitSendAction(true, &x);  // taken
    const bool up = l.AdmitQueued(false, 3, 5, &u);  // no X now: refused
    l.OnSentAction(x);  // the down's answer
    check("ledger r4: a down in flight when its up is refused stays a debt (owed), nothing cancelled",
          dn && !up && l.SendOwed(5) && l.CancelledDowns(5) == 0);
  }
  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);  // X1 refused''')

rep('''  for (bool atEntry : {true, false}) {
    for (bool xBack : {true, false}) owed_kept(atEntry, xBack);
  }
''', '''  for (bool atEntry : {true, false}) {
    for (bool xBack : {true, false}) owed_kept(atEntry, xBack);
  }
  queued_down_refused_up();
''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
