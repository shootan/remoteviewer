p = 'C:/work/gnlink-wt/feat-apk-0233/apps/native_poc/src/client_session_xbutton_fence_test.cpp'
s = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in s
if crlf:
    s = s.replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:100], s.count(a))
    s = s.replace(a, b)


rep('''//   action  -- the send-time fence on a hand-built ControlOutboundAction''', '''//   r2 (codex_apk_xfence_src_r1.md X1 / X2) -- the capability CHANGING under one press, by the fake host's Pong
//   phases (a Pong held back, its flags switched):
//     unknown -> X       a down refused before the first Pong: after the X Pong its up and its held bit stay off
//                        the wire; the next press goes whole.
//     X -> no X (send)   a press queued while the host took X, sent after a Pong without X: the down, its moves'
//                        bit and its up are all dropped at the send fence; back to X, the next press goes.
//     X -> no X (owed)   a down SENT to an X host, then no X: its owed release is not sent (no exception), the
//                        park cannot finish and ends by its own bound (not confirmed), the session ends.
//     reconnect          a press refused on one connection is not remembered on the next.
//   ledger  -- ClientMouseXPressLedger itself, both sides, the same transitions without a session.
//   action  -- the send-time fence on a hand-built ControlOutboundAction''')

rep('''struct FakeHost {
  std::atomic<uint32_t> pongFlags{0};''', '''struct FakeHost {
  std::atomic<uint32_t> pongFlags{0};
  std::atomic<bool> holdPongs{false};  // r2: a Ping is answered only once released (the client's loop waits on it)
  std::atomic<int> heldPings{0};''')
rep('''  void Stop() {
    stop = true;
    n::shutdown_socket(&accepted);''', '''  void Stop() {
    stop = true;
    holdPongs = false;
    n::shutdown_socket(&accepted);''')
rep('''        const auto ping = Decode<n::ControlPingMessage>(h, body);
        n::ControlPongMessage pong{};''', '''        const auto ping = Decode<n::ControlPingMessage>(h, body);
        if (holdPongs.load()) {
          ++heldPings;
          while (holdPongs.load() && !stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        n::ControlPongMessage pong{};''')

rep('''// ---- the send-time fence on an action ---''', '''// ---- r2: the capability changing under one press ----------------------------------------------------------
constexpr uint32_t kX = n::kCaptureFlagMouseXButtonsV1;

// The client's control loop is waiting on a Ping the fake host holds: nothing is sent until the Pong is released.
bool hold_next_pong(FakeHost& h) {
  const int before = h.heldPings.load();
  h.holdPongs = true;
  return wait_until([&] { return h.heldPings.load() > before; }, 3000);
}
bool release_pong_as(FakeHost& h, n::ClientSessionController& c, uint32_t flags) {
  h.pongFlags = flags;
  h.holdPongs = false;
  return wait_until([&] { return c.HostCaptureFlags() == flags; }, 3000);
}

void unknown_to_x() {
  std::printf("\\n--- r2: a down refused before the first Pong, then the X Pong ---\\n");
  FakeHost h;
  h.holdPongs = true;  // the first Pong held: the role is unknown
  if (!h.Start()) return check("unknown->X: fake host starts", false);
  n::ClientSessionController c;
  n::ClientSessionConnectArgs a{};
  a.host = "127.0.0.1";
  a.videoPort = h.videoPort;
  a.controlPort = h.controlPort;
  a.controlIntervalMs = 50;
  const bool up = c.Connect(a) && wait_until([&] {
    const auto s = c.Snapshot();
    return s.state == n::ClientSessionState::Connected && s.controlLoopActive && h.heldPings.load() > 0;
  }, 5000);
  check("unknown->X: connected, the first Pong held (role unknown)", up && c.HostCaptureFlags() == 0);
  // a macro press as nativeMacroPump hands it (no 0x7 mask): X1 down, then its moves carry the X1 bit
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool known = release_pong_as(h, c, kX);
  const bool mv1 = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX1);
  const bool mv2 = c.QueueInputEvent(1, 12, 12, 0, 0, n::kMouseWireX1 | n::kMouseWireLeft);
  const bool upX = c.QueueInputEvent(3, 12, 12, 0, 5, 0);
  // the next press, after that up: judged afresh -- the host takes X now
  const bool down2 = c.QueueInputEvent(2, 20, 20, 0, 5, n::kMouseWireX1);
  const bool mv3 = c.QueueInputEvent(1, 21, 21, 0, 0, n::kMouseWireX1);
  const bool up2 = c.QueueInputEvent(3, 21, 21, 0, 5, 0);
  const bool sent = wait_until([&] { return h.InputCount() >= 5; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const auto all = h.Inputs();
  const std::string d = "down=" + std::to_string(down) + " up=" + std::to_string(upX) + " mv=" + std::to_string(mv1) +
                        std::to_string(mv2) + " next=" + std::to_string(down2) + std::to_string(mv3) + std::to_string(up2) +
                        " wire=" + std::to_string(all.size());
  check("unknown->X: the refused press stays refused after the X Pong -- its up refused, its moves sent without X1",
        up && known && !down && !upX && mv1 && mv2 && all.size() >= 2 && all[0].kind == 1 && all[0].buttons == 0 &&
            all[1].kind == 1 && all[1].buttons == n::kMouseWireLeft,
        d);
  check("unknown->X: the next press goes whole (X1 down, its X1 bit, X1 up) -- and nothing else X on the wire",
        sent && down2 && mv3 && up2 && all.size() == 5 && all[2].kind == 2 && all[2].keyCode == 5 &&
            all[3].buttons == n::kMouseWireX1 && all[4].kind == 3 && all[4].keyCode == 5,
        d);
  c.Disconnect();
}

void x_to_nox_at_send() {
  std::printf("\\n--- r2: a press queued while the host took X, sent after a Pong without X ---\\n");
  FakeHost h;
  if (!h.Start()) return check("X->noX send: fake host starts", false);
  n::ClientSessionController c;
  check("X->noX send: connected to an X host", connect_and_know_role(c, h, kX));
  const bool held = hold_next_pong(h);  // the loop now waits on this Ping: what is queued next is sent after its Pong
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 6, n::kMouseWireX2);
  const bool mv = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX2 | n::kMouseWireLeft);
  const bool upX = c.QueueInputEvent(3, 11, 11, 0, 6, 0);
  const bool lclick = c.QueueInputEvent(2, 12, 12, 0, 1, n::kMouseWireLeft) && c.QueueInputEvent(3, 12, 12, 0, 1, 0);
  const uint64_t heldBack0 = c.MouseXButtonsHeldBack();
  const bool noX = release_pong_as(h, c, 0);
  const bool sent = wait_until([&] { return h.InputCount() >= 3; }, 3000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t = tally(h.Inputs());
  check("X->noX send: all queued while X (accepted); after the no-X Pong the down and its up are dropped at sending, "
        "the move's X2 bit masked; the left click goes",
        held && down && mv && upX && lclick && noX && sent && t.total == 3 && t.xEdges == 0 && t.xBits == 0 &&
            t.all[0].kind == 1 && t.all[0].buttons == n::kMouseWireLeft && t.count(2, 1) == 1 && t.count(3, 1) == 1 &&
            c.MouseXButtonsHeldBack() == heldBack0 + 2,
        t.str() + " heldBack+" + std::to_string(c.MouseXButtonsHeldBack() - heldBack0));
  // back to X: the next press is judged afresh
  const bool held2 = hold_next_pong(h);
  const bool xAgain = release_pong_as(h, c, kX);
  const size_t mark = h.InputCount();
  const bool d2 = c.QueueInputEvent(2, 20, 20, 0, 6, n::kMouseWireX2);
  const bool u2 = c.QueueInputEvent(3, 20, 20, 0, 6, 0);
  const bool sent2 = wait_until([&] { return h.InputCount() >= mark + 2; }, 3000);
  const Tally t2 = tally(h.Inputs(), mark);
  check("X->noX send: back to X, the next X2 press goes (down and up)",
        held2 && xAgain && d2 && u2 && sent2 && t2.total == 2 && t2.count(2, 6) == 1 && t2.count(3, 6) == 1, t2.str());
  c.Disconnect();
}

void x_to_nox_owed() {
  std::printf("\\n--- r2: a down sent to an X host, then a Pong without X: its owed release ---\\n");
  FakeHost h;
  if (!h.Start()) return check("X->noX owed: fake host starts", false);
  n::ClientSessionController c;
  check("X->noX owed: connected to an X host, pause honoured", connect_and_know_role(c, h, kX));
  const bool down = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool sent = wait_until([&] { return h.InputCount() >= 1; }, 3000);
  const bool held = hold_next_pong(h);
  const bool noX = release_pong_as(h, c, 0);
  const size_t mark = h.InputCount();
  const bool upNow = c.QueueInputEvent(3, 10, 10, 0, 5, 0);  // the user lets go: a host without X gets no X up
  const bool began = c.BeginPark();
  const bool ended = wait_until([&] {
    const auto s = c.Snapshot();
    return s.state != n::ClientSessionState::Connected || !s.controlLoopActive;
  }, 5000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const Tally t = tally(h.Inputs(), mark);
  const std::string err = c.Snapshot().lastError;
  check("X->noX owed: the X1 down went to the X host",
        down && sent && h.Inputs().size() >= 1 && h.Inputs()[0].kind == 2 && h.Inputs()[0].keyCode == 5);
  check("X->noX owed: after the no-X Pong no X up is sent (the user's up refused, the park's release dropped -- no "
        "exception to a host without X)",
        held && noX && !upNow && t.xEdges == 0 && t.xBits == 0, t.str() + " parkReleased=" + std::to_string(c.ParkReleasedCount()));
  check("X->noX owed: the park does not call itself confirmed -- it ends by its bound and the session ends",
        began && ended && err.find("park not confirmed") != std::string::npos && c.ParkPhase() != n::ClientParkPhase::Parked,
        "lastError=" + err + " phase=" + std::to_string(static_cast<int>(c.ParkPhase())));
  c.Disconnect();
}

void reconnect_forgets() {
  std::printf("\\n--- r2: a press refused on one connection is not remembered on the next ---\\n");
  n::ClientSessionController c;
  {
    FakeHost h;
    if (!h.Start()) return check("reconnect: fake host 1 starts", false);
    check("reconnect: connection 1 to a host without X", connect_and_know_role(c, h, 0));
    check("reconnect: an X1 down refused there (its up never comes on that connection)",
          !c.QueueInputEvent(2, 10, 10, 0, 5, 0));
    c.Disconnect();
  }
  FakeHost h2;
  if (!h2.Start()) return check("reconnect: fake host 2 starts", false);
  check("reconnect: connection 2 to an X host", connect_and_know_role(c, h2, kX));
  const bool d = c.QueueInputEvent(2, 10, 10, 0, 5, n::kMouseWireX1);
  const bool mv = c.QueueInputEvent(1, 11, 11, 0, 0, n::kMouseWireX1);
  const bool u = c.QueueInputEvent(3, 11, 11, 0, 5, 0);
  const bool sent = wait_until([&] { return h2.InputCount() >= 3; }, 3000);
  const Tally t = tally(h2.Inputs());
  check("reconnect: on connection 2 a fresh X1 press goes whole (no refusal carried over)",
        d && mv && u && sent && t.total == 3 && t.xEdges == 2 && t.xBits == 1, t.str());
  c.Disconnect();
}

// The ledger alone: the same transitions, both sides, no session.
void ledger_pure() {
  std::printf("\\n--- r2: ClientMouseXPressLedger ---\\n");
  using L = n::ClientMouseXPressLedger;
  n::ControlOutboundAction a{};
  a.kind = n::ControlOutboundActionKind::InputEvent;
  auto ev = [&](uint16_t kind, uint32_t key, uint16_t buttons) {
    a.inputEvent.kind = kind;
    a.inputEvent.keyCode = key;
    a.inputEvent.buttons = buttons;
    return &a;
  };
  {
    L l;
    uint16_t b = 0x8;
    const bool d = l.AdmitQueued(false, 2, 5, &b);
    uint16_t m = 0x9;
    const bool mv = l.AdmitQueued(true, 1, 0, &m);
    uint16_t u = 0;
    const bool up = l.AdmitQueued(true, 3, 5, &u);
    uint16_t d2b = 0x8;
    const bool d2 = l.AdmitQueued(true, 2, 5, &d2b);
    uint16_t u2b = 0;
    const bool u2 = l.AdmitQueued(true, 3, 5, &u2b);
    check("ledger entry: refused down (no X) -> X: move without X1, up refused; the next press goes",
          !d && mv && m == 0x1 && !up && d2 && d2b == 0x8 && u2 && !l.EntryRefused(5));
  }
  {
    L l;
    const bool d = l.AdmitSendAction(false, ev(2, 6, 0x10));
    const bool mv = l.AdmitSendAction(true, ev(1, 0, 0x11));
    const uint16_t mvb = a.inputEvent.buttons;
    const bool up = l.AdmitSendAction(true, ev(3, 6, 0));
    const bool d2 = l.AdmitSendAction(true, ev(2, 6, 0x10));
    l.OnSentAction(a);
    const bool owed = l.SendOwed(6);
    const bool u2 = l.AdmitSendAction(true, ev(3, 6, 0));
    l.OnSentAction(a);
    check("ledger send: a down dropped (no X) -> X: move without X2, up dropped; the next press goes and is owed until its up",
          !d && mv && mvb == 0x1 && !up && d2 && owed && u2 && !l.SendOwed(6));
  }
  {
    L l;
    (void)l.AdmitSendAction(true, ev(2, 5, 0x8));
    l.OnSentAction(a);
    const bool upNoX = l.AdmitSendAction(false, ev(3, 5, 0));
    const bool stillOwed = l.SendOwed(5);
    const bool upX = l.AdmitSendAction(true, ev(3, 5, 0));
    check("ledger send: a SENT down's release -- refused to a host without X (still owed, no exception), sent once X is back",
          !upNoX && stillOwed && upX);
  }
  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);
    (void)l.AdmitSendAction(false, ev(2, 6, 0));
    l.Reset();
    uint16_t u = 0;
    check("ledger reset: nothing refused or dropped remains", !l.EntryRefused(5) && !l.SendDropped(6) &&
          l.AdmitQueued(true, 3, 5, &u));
  }
  {
    L l;
    uint16_t b = 0;
    (void)l.AdmitQueued(false, 2, 5, &b);  // X1 refused
    uint16_t x2 = 0x10;
    const bool d2 = l.AdmitQueued(true, 2, 6, &x2);
    uint16_t lk = 0x1;
    const bool left = l.AdmitQueued(true, 2, 1, &lk);
    check("ledger: a refused X1 press does not touch X2 or the left button", d2 && x2 == 0x10 && left && lk == 0x1);
  }
}

// ---- the send-time fence on an action ---''')

rep('''  action_fence();
  for (bool takesX : {false, true}) {''', '''  action_fence();
  ledger_pure();
  unknown_to_x();
  x_to_nox_at_send();
  x_to_nox_owed();
  reconnect_forgets();
  for (bool takesX : {false, true}) {''')

if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
