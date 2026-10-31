import os

R = 'C:/work/gnlink-wt/ka-r3/apps/native_poc/src/'


def edit(name, reps):
    p = R + name
    s = open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    if crlf:
        s = s.replace('\r\n', '\n')
    for a, b in reps:
        assert s.count(a) == 1, (name, a[:90], s.count(a))
        s = s.replace(a, b)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('ok', name)


edit('standby_lease.hpp', [
    ("""    // r2 D1: the old session's tail is still arriving (its video / control from the target reached this socket within
    // kTailQuietMs). An observation of what the host still sends -- not a proof that its grant ended when it stops.
    bool tailActive = false;
    uint64_t strays = 0;  // such datagrams so far (the tail's cost, counted apart)
  };""",
     """    // r2 D1: datagrams from the target that are not standby traffic, a HelloAck or a Punch reached this socket within
    // kTailQuietMs -- an observation of what the target still sends (a session's video / control, or anything else it
    // sends there), not a proof that its grant ended when it stops, and not a count of "video" either.
    bool tailActive = false;
    uint64_t strays = 0;  // such datagrams so far (counted apart)
    // r3 (Codex codex_ka_e_src_r2.md D1): the host's Punch trains to this socket (a first transition's punches: 25 a
    // train), counted on their own -- r2 counted them as strays, so a punch alone made "a tail"
    uint64_t punches = 0;
  };"""),
    ("""    s.strays = strays_;
    s.tailActive""", """    s.strays = strays_;
    s.punches = punches_;
    s.tailActive"""),
    ("""      bool stray = false;
      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr, &stray);""",
     """      bool stray = false;
      bool punch = false;
      const bool got = Receive(s, 20, aimed ? &want : nullptr, &p, nullptr, &stray, &punch);"""),
    ("""      if (stray) {
        ++strays_;
        lastStrayMs_ = now_();
      }""", """      if (stray) {
        ++strays_;
        lastStrayMs_ = now_();
      }
      if (punch) ++punches_;"""),
    ("""  // *stray (optional): a datagram from `want` that is neither -- the video / control of a session that used this socket
  // and that the host still sends (r2 D1: the end of a session's tail is observed, never assumed).
  static bool Receive(SocketHandle s, uint32_t waitMs, const sockaddr_in* want, UdpStandbyPacket* sb, UdpHelloPacket* ack,
                      bool* stray = nullptr) {""",
     """  // *stray (optional): a datagram from `want` that is none of these and no Punch -- e.g. the video / control of a session
  // that used this socket and that the host still sends (r2 D1: observed, never assumed). *punch (optional): a Punch
  // from `want` (r3: counted apart, never a stray).
  static bool Receive(SocketHandle s, uint32_t waitMs, const sockaddr_in* want, UdpStandbyPacket* sb, UdpHelloPacket* ack,
                      bool* stray = nullptr, bool* punch = nullptr) {"""),
    ("""    if (magic != kMagic) return false;
    if (n == static_cast<int>(sizeof(UdpStandbyPacket))""",
     """    if (magic != kMagic) return false;
    if (kind == static_cast<uint16_t>(UdpPacketKind::Punch)) {
      if (stray) *stray = false;
      if (punch) *punch = true;
      return false;
    }
    if (n == static_cast<int>(sizeof(UdpStandbyPacket))"""),
    ("""  uint64_t strays_ = 0;           // datagrams from the target that were not standby traffic (a session's tail)""",
     """  uint64_t strays_ = 0;           // datagrams from the target that were not standby / HelloAck / Punch (r3)
  uint64_t punches_ = 0;          // Punches from the target (r3: apart from the strays)"""),
])

edit('standby_runner.hpp', [
    ("""    bool tailActive = false;       // r2 D1: the old session's datagrams still arrive (StandbyLease::Snapshot)
    uint64_t strays = 0;
  };""", """    bool tailActive = false;       // r2 D1: the old session's datagrams still arrive (StandbyLease::Snapshot)
    uint64_t strays = 0;
    uint64_t punches = 0;          // r3 D1: the host's Punches, apart from the strays
  };"""),
    ("""    s.status.strays = n.strays;
    return s.status;""", """    s.status.strays = n.strays;
    s.status.punches = n.punches;
    return s.status;"""),
    ("""  void ReleaseAll() {
    std::map<std::string, std::shared_ptr<Slot>> all;
    {
      std::lock_guard<std::mutex> lock(mu_);
      all.swap(leases_);
    }
    for (auto& kv : all) Stop(kv.second);
  }""", """  void ReleaseAll() {
    std::map<std::string, std::shared_ptr<Slot>> all;
    {
      std::lock_guard<std::mutex> lock(mu_);
      all.swap(leases_);
    }
    for (auto& kv : all) Stop(kv.second);
  }

  // r3 (Codex codex_ka_e_src_r2.md R2): the leases of ONE owner -- every key starting with `prefix` (non-empty) -- and
  // no other. An app screen's late cleanup reclaims its own leases and never those of the screen after it, which share
  // this process's runner. How many went.
  size_t ReleaseWhere(const std::string& prefix) {
    if (prefix.empty()) return 0;
    std::vector<std::shared_ptr<Slot>> mine;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto it = leases_.begin(); it != leases_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
          mine.push_back(it->second);
          it = leases_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (auto& slot : mine) Stop(slot);
    return mine.size();
  }"""),
])

edit('standby_lease_test.cpp', [
    ("""  check("...it stops: no tail any more (an observation, not a proof the grant ended), the count kept",
        !after.tailActive && after.strays == during.strays && lease.state() == State::Parked);
}""", """  check("...it stops: no tail any more (an observation, not a proof the grant ended), the count kept",
        !after.tailActive && after.strays == during.strays && lease.state() == State::Parked);

  // r3 (Codex codex_ka_e_src_r2.md D1, its tail_punch_probe: one Punch made strays=1 tail=1 with no session ever): a
  // Punch is counted on its own, never as a stray, and makes no tail
  std::printf("\\n--- r3 D1: the host's Punches are not a tail ---\\n");
  n::StandbyLease fresh(gens, n::kUdpFeatureVideoFec, host.capability(), mono_ms);
  check("a new lease parks, no session ever", park(fresh) && fresh.snapshot().strays == 0 && fresh.snapshot().punches == 0);
  n::UdpHelloPacket punch{};
  punch.magic = n::kMagic;
  punch.kind = static_cast<uint16_t>(n::UdpPacketKind::Punch);
  punch.size = static_cast<uint16_t>(sizeof(punch));
  for (int i = 0; i < 5; ++i) {
    host.SendRaw(&punch, sizeof(punch));
    fresh.Pump(20);
  }
  const auto pp = fresh.snapshot();
  check("5 Punches: punches 5, strays 0, no tail", pp.punches == 5 && pp.strays == 0 && !pp.tailActive,
        "punches=" + std::to_string(pp.punches) + " strays=" + std::to_string(pp.strays) +
            " tail=" + std::to_string(pp.tailActive));
}"""),
    ("""  stale = nullptr;
  check("...the lease pings again on its own", wait_until([&] { return host.pings > pings0; }, 7000) && parked("k3"));
  runner.ReleaseAll();
  check("ReleaseAll: every slot gone", runner.size() == 0 && n::StandbyRunner::liveSlots() == slots0);
}""", """  stale = nullptr;
  check("...the lease pings again on its own", wait_until([&] { return host.pings > pings0; }, 7000) && parked("k3"));
  runner.ReleaseAll();
  check("ReleaseAll: every slot gone", runner.size() == 0 && n::StandbyRunner::liveSlots() == slots0);

  // r3 (Codex codex_ka_e_src_r2.md R2): two app screens share this process's runner; the old one's late cleanup takes
  // its own leases only -- the new screen's lease (even of the same PC) stays, and the old ones are reclaimed, not leaked
  std::printf("\\n--- r3 R2: one owner's cleanup takes its own leases only ---\\n");
  check("old owner o1: two leases park", runner.Start("o1|pcA") && runner.Start("o1|pcB") && parked("o1|pcA") &&
        parked("o1|pcB"));
  check("new owner o2: a lease of the same PC parks beside them", runner.Start("o2|pcA") && parked("o2|pcA"));
  check("prefix \\"o1\\" alone would also match \\"o10|..\\": a lease of owner o10 parks too",
        runner.Start("o10|pcC") && parked("o10|pcC"));
  const int rel0 = host.releases;
  const size_t gone = runner.ReleaseWhere("o1|");
  check("ReleaseWhere(\\"o1|\\"): its two leases go (released at the host), o2's and o10's stay parked",
        gone == 2 && runner.size() == 2 && !runner.StatusOf("o1|pcA").exists && !runner.StatusOf("o1|pcB").exists &&
            parked("o2|pcA") && parked("o10|pcC") &&
            wait_until([&] { return host.releases >= rel0 + 2; }, 3000),
        "gone=" + std::to_string(gone) + " size=" + std::to_string(runner.size()) +
            " releases=" + std::to_string(host.releases - rel0));
  check("...a late Provide / status of the old owner's key touches nothing",
        !runner.Provide("o1|pcA", n::StandbyCapability{}) && !runner.StatusOf("o1|pcA").exists && parked("o2|pcA"));
  check("ReleaseWhere(\\"\\") releases nothing", runner.ReleaseWhere("") == 0 && runner.size() == 2);
  runner.ReleaseAll();
  check("...ReleaseAll (the runner's end): every slot gone", runner.size() == 0 && n::StandbyRunner::liveSlots() == slots0);
}"""),
])
