p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_entry_client_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')
a = """  check("...the cancelled one's Pong still adopts nothing", count(e.OnStandbyPacket(pong(77, gen, 0, true), 3005),
        Effect::Activated) == 0 && e.OnHelloAck(3006).empty());
}
"""
b = """  check("...the cancelled one's Pong still adopts nothing", count(e.OnStandbyPacket(pong(77, gen, 0, true), 3005),
        Effect::Activated) == 0 && e.OnHelloAck(3006).empty());
  // r3 (Codex scope2 N1): A cancelled -> B started -> A's late HelloAck -> B's Pong. The HelloAck carries no generation
  // (the host's session-admission answer; its features are validated by the lease before it gets here): it completes
  // only the activation in flight, and only together with THAT activation's own Pong.
  const uint32_t genA = a2->clientGen;
  e.CancelActivation(3100);
  fx = e.Activate(3200);
  const auto* b = sent(fx, n::UdpPacketKind::StandbyActivate);
  const uint32_t genB = b ? b->clientGen : 0;
  check("A cancelled, B in flight with a larger generation", genB > genA && e.state() == Client::State::Activating);
  check("...A's late HelloAck: not active (no Pong of B yet)", count(e.OnHelloAck(3210), Effect::Activated) == 0 &&
        e.state() == Client::State::Activating);
  check("...A's late granted Pong: nothing", count(e.OnStandbyPacket(pong(77, genA, 0, true), 3220), Effect::Activated) == 0 &&
        e.state() == Client::State::Activating);
  check("...B's own granted Pong: active (once)", count(e.OnStandbyPacket(pong(77, genB, 0, true), 3230), Effect::Activated) == 1 &&
        e.state() == Client::State::Active);
}
"""
assert s.count(a) == 1
s = s.replace(a, b)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
