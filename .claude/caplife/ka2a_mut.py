import sys, shutil
p = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/standby_entry_client.hpp'
bak = 'D:/remote/remote/.claude/caplife/ka2a_hpp.bak'
MUT = {
    'relAlways': [('    if (state_ == State::Parked) fx.push_back(SendOf(Packet(UdpPacketKind::StandbyRelease, 0)));',
                   '    fx.push_back(SendOf(Packet(UdpPacketKind::StandbyRelease, 0)));  // MUTANT also while active')],
    'relKeep': [('    if (state_ != State::Unsupported) state_ = State::Idle;  // an older host stays known as such',
                 '    // MUTANT: stays parked (keeps pinging)')],
    'bareAck': [('    gotHelloAck_ = true;\n    if (gotPong_) Complete(&fx);', '    gotHelloAck_ = true;\n    Complete(&fx);  // MUTANT')],
    'anyGen': [('      if (state_ != State::Activating || p.clientGen != activateGen_) return fx;  // late, cancelled or another one',
                '      if (state_ != State::Activating) return fx;  // MUTANT any generation')],
    'noDead': [('        if (nowMs - lastPongMs_ > kPingDeadMs) {', '        if (false) {  // MUTANT never lost')],
    'legacy': [('            state_ = State::Unsupported;\n            fx.push_back(Fx(Effect::Unsupported, "no answer to StandbyHello"));',
                '            Wait(nowMs, "no answer", &fx);  // MUTANT: try again (a legacy fallback would follow)')],
    'newGenRetx': [('    fx->push_back(SendOf(Packet(UdpPacketKind::StandbyActivate, activateGen_)));',
                    '    if (activateSent_ > 0) gens_.Next(&activateGen_);  // MUTANT a new generation per retransmit\n'
                    '    fx->push_back(SendOf(Packet(UdpPacketKind::StandbyActivate, activateGen_)));')],
    'noSave': [('    if (!store_.Save(deviceId_, next)) return false;\n    last_ = next;', '    last_ = next;  // MUTANT not saved')],
    'busyAtOnce': [('    waitUntilMs_ = nowMs + BackoffMs(busyBackoff_);', '    waitUntilMs_ = nowMs;  // MUTANT no backoff')],
}
m, act = sys.argv[1], sys.argv[2]
if act == 'apply':
    shutil.copyfile(p, bak)
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in MUT[m]:
        assert s.count(old) == 1, (m, s.count(old), old[:60])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))
else:
    shutil.copyfile(bak, p)
print(m, act, 'ok')
