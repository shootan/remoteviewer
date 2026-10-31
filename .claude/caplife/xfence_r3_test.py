p = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/client_session_xbutton_fence_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''//     reconnect          a press refused on one connection is not remembered on the next.''',
    '''//     reconnect          a press refused on one connection is not remembered on the next.
//   r3 (codex_apk_xfence_src_r2.md N1) -- a down SENT to an X host owes its release, whatever becomes of a later down
//   of the same button:
//     owed / entry       a later down refused at the entry (no X): back to X, the up goes -- exactly one X up; while
//                        still no X, none.
//     owed / send        a later down admitted, then dropped at sending (no X): back to X, the up goes -- exactly one;
//                        while still no X, none.
//     In each, the debt ends with that answered up: a park afterwards confirms (nothing held), X2 and left unaffected.''')

rep('''// The ledger alone: the same transitions, both sides, no session.''', '''// r3: a down the host received, then a later down of the same button refused / dropped.
// `atEntry`: that later down refused at the entry (queued while the Pong said no X); else admitted (X) and dropped at
// sending (a no-X Pong in between). `xBack`: the host takes X again before the user's up.
void owed_kept(bool atEntry, bool xBack) {
  const char* path = atEntry ? "entry" : "send";
  std::printf("\\n--- r3: owed release / a later down refused at the %s / up %s ---\\n", path, xBack ? "after X is back" : "while no X");
  FakeHost h;
  if (!h.Start()) return check("owed: fake host starts", false);
  n::ClientSessionController c;
  const std::string tag = std::string("owed/") + path + (xBack ? " X back" : " no X") + ": ";
  check((tag + "connected to an X host, pause honoured").c_str(), connect_and_know_role(c, h, kX));
  const bool d1 = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool sent1 = wait_until([&] { return h.InputCount() >= 1; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));  // its answer (ACK) in: the debt is recorded
  const size_t mark = h.InputCount();
  bool d2 = false;
  bool phase = true;
  if (atEntry) {
    phase = hold_next_pong(h) && release_pong_as(h, c, 0);
    d2 = c.QueueInputEvent(2, 11, 11, 0, 5, n::kMouseWireX1);  // refused at the entry
  } else {
    phase = hold_next_pong(h);
    d2 = c.QueueInputEvent(2, 11, 11, 0, 5, n::kMouseWireX1);  // admitted (X) ...
    phase = phase && release_pong_as(h, c, 0);                 // ... dropped at sending (no X)
  }
  const bool left = c.QueueInputEvent(2, 12, 12, 0, 1, n::kMouseWireLeft) && c.QueueInputEvent(3, 12, 12, 0, 1, 0);
  const bool leftSent = wait_until([&] { return h.InputCount() >= mark + 2; }, 3000);
  if (xBack) phase = phase && hold_next_pong(h) && release_pong_as(h, c, kX);
  const bool up = c.QueueInputEvent(3, 13, 13, 0, 5, 0);
  if (xBack) wait_until([&] { return h.InputCount() >= mark + 3; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const Tally t = tally(h.Inputs(), mark);
  const std::string d = t.str() + " d2=" + std::to_string(d2) + " up=" + std::to_string(up) + " X1up=" +
                        std::to_string(t.count(3, 5)) + " X1down=" + std::to_string(t.count(2, 5));
  check((tag + "the first X1 down reached the X host").c_str(), d1 && sent1 && phase);
  check((tag + (atEntry ? "the later X1 down refused at the entry" : "the later X1 down admitted, then dropped at sending (not on the wire)")).c_str(),
        (atEntry ? !d2 : d2) && t.count(2, 5) == 0 && left && leftSent && t.count(2, 1) == 1 && t.count(3, 1) == 1, d);
  if (xBack) {
    check((tag + "X back: the up goes -- exactly ONE X1 up on the wire (the owed release, not hidden by the later down)").c_str(),
          up && t.count(3, 5) == 1 && t.xEdges == 1, d);
    // the debt ended with that answered up: a park now finds nothing held -- it confirms, and sends no further X up
    const size_t mark2 = h.InputCount();
    const bool parked = c.BeginPark() && wait_until([&] { return c.ParkPhase() == n::ClientParkPhase::Parked; }, 3000);
    const Tally t2 = tally(h.Inputs(), mark2);
    check((tag + "the debt is paid: a park confirms with nothing to release (no further X up)").c_str(),
          parked && t2.count(3, 5) == 0 && c.ParkReleasedCount() == 0,
          t2.str() + " parkReleased=" + std::to_string(c.ParkReleasedCount()));
    if (parked) {
      (void)c.ResumeFromPark();
      (void)c.RequestStreamActive(true);
    }
    const size_t mark3 = h.InputCount();
    const bool x2 = c.QueueInputEvent(2, 20, 20, 0, 6, n::kMouseWireX2) && c.QueueInputEvent(3, 20, 20, 0, 6, 0);
    const bool x2sent = wait_until([&] { return h.InputCount() >= mark3 + 2; }, 3000);
    const Tally t3 = tally(h.Inputs(), mark3);
    check((tag + "X2 unaffected: its press goes (down and up)").c_str(),
          x2 && x2sent && t3.count(2, 6) == 1 && t3.count(3, 6) == 1, t3.str());
  } else {
    check((tag + "still no X: the up is NOT sent (0 X up, no exception to a host without X)").c_str(),
          !up && t.count(3, 5) == 0 && t.xEdges == 0 && t.xBits == 0, d);
  }
  c.Disconnect();
}

// The ledger alone: the same transitions, both sides, no session.''')

rep('''  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);  // X1 refused''', '''  // r3 N1: a SENT down's debt is not hidden by a later down of the button refused at the entry / dropped at sending
  for (bool atEntry : {true, false}) {
    L l;
    uint16_t b = 0x8;
    (void)l.AdmitQueued(true, 2, 5, &b);
    (void)l.AdmitSendAction(true, ev(2, 5, 0x8));
    l.OnSentAction(a);  // sent + answered: owed
    bool later = false;
    if (atEntry) {
      uint16_t b2 = 0x8;
      later = l.AdmitQueued(false, 2, 5, &b2);  // refused at the entry (no X)
    } else {
      uint16_t b2 = 0x8;
      later = l.AdmitQueued(true, 2, 5, &b2) && !l.AdmitSendAction(false, ev(2, 5, 0x8));  // admitted, dropped
    }
    const bool owedKept = l.SendOwed(5);
    uint16_t mv = 0x9;
    const bool mvOk = l.AdmitQueued(true, 1, 0, &mv);  // the host holds X1: its bit is true, kept
    uint16_t u = 0;
    const bool upEntry = l.AdmitQueued(true, 3, 5, &u);
    const bool upSend = l.AdmitSendAction(true, ev(3, 5, 0));
    l.OnSentAction(a);
    const bool paid = !l.SendOwed(5);
    check(atEntry ? "ledger r3: owed X1, a later down refused at the entry -- the owed up still goes once X is back, then paid"
                  : "ledger r3: owed X1, a later down dropped at sending -- the owed up still goes once X is back, then paid",
          (atEntry ? !later : later) && owedKept && mvOk && mv == 0x9 && upEntry && upSend && paid);
    L k;  // the same, the up while still no X: refused at both fences, the debt kept (no exception)
    uint16_t c0 = 0x8;
    (void)k.AdmitQueued(true, 2, 5, &c0);
    (void)k.AdmitSendAction(true, ev(2, 5, 0x8));
    k.OnSentAction(a);
    uint16_t c1 = 0x8;
    if (atEntry) {
      (void)k.AdmitQueued(false, 2, 5, &c1);
    } else {
      (void)k.AdmitQueued(true, 2, 5, &c1);
      (void)k.AdmitSendAction(false, ev(2, 5, 0x8));
    }
    uint16_t c2 = 0;
    const bool upNoXEntry = k.AdmitQueued(false, 3, 5, &c2);
    const bool upNoXSend = k.AdmitSendAction(false, ev(3, 5, 0));
    check(atEntry ? "ledger r3: ...the up while still no X: refused at the entry and at sending, the debt kept"
                  : "ledger r3: ...(send path) the up while still no X: refused at both fences, the debt kept",
          !upNoXEntry && !upNoXSend && k.SendOwed(5));
  }
  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);  // X1 refused''')

rep('''  reconnect_forgets();
''', '''  reconnect_forgets();
  for (bool atEntry : {true, false}) {
    for (bool xBack : {true, false}) owed_kept(atEntry, xBack);
  }
''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
