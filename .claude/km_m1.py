import sys
root = sys.argv[1]

def edit(p, pairs):
    p = root + '/' + p
    raw = open(p, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    s = raw.decode('utf-8-sig').replace('\r\n', '\n')
    for o, n in pairs:
        if s.count(o) != 1:
            raise SystemExit('count %d in %s: %r' % (s.count(o), p, o[:80]))
        s = s.replace(o, n)
    open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.replace('\n', '\r\n').encode('utf-8'))

edit('apps/native_poc/src/session_table.hpp', [
('''//   bool Alive()                its session is connected
''', '''//   bool Alive()                its session is not ended (connecting or connected)
//   int Established()           1 = its authenticated Hello and control are up (Connected), 0 = still connecting,
//                               -1 = ended (Disconnected / Error) -- the only proof a commit may evict for
//   uint64_t Generation()       its session's connection generation (which Connect made it)
'''),
('''  // The new connection is up. Its eviction (if the limit needed one) happens now; returns the closed handles.
  std::vector<uint64_t> Commit(uint64_t handle, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    std::vector<uint64_t> closed = registry_.Commit(handle);
    for (uint64_t v : closed) DetachAndCloseLocked(v, "lru", nowMs);
    RegistryEntry re;
    if (registry_.Get(handle, &re) && re.phase == EntryPhase::Parked) committedMs_[handle] = nowMs;
    return closed;
  }
''', '''  struct CommitResult {
    enum Kind { Committed, Pending, Failed } kind = Failed;
    std::vector<uint64_t> closed;  // Committed: the entries its reservation evicted (LRU), closed now
    std::string why;               // Failed: why (the entry is given back -- nothing evicted for it)
  };

  // The new connection is up: its eviction (if the limit needed one) happens now -- and ONLY once its session is
  // established (authenticated Hello + control: Connected) on the connection generation the connect made
  // (keepalive-multi r2 M1). Connect returns as soon as its worker starts; a commit then would evict a parked PC
  // for a connection whose Hello may still be refused or time out. Pending = not yet established: nothing changes,
  // the caller asks again (no wait here). Failed = it ended before it was established, or it is not this
  // connection any more: it is given back (Fail), and no parked PC was touched.
  CommitResult Commit(uint64_t handle, uint64_t generation, uint64_t nowMs) {
    CommitResult out;
    std::lock_guard<std::mutex> tl(transitionMu_);
    std::shared_ptr<Entry> e = Find(handle);
    RegistryEntry re;
    if (!e || !registry_.Get(handle, &re) || re.phase != EntryPhase::Connecting) {
      out.why = "not_connecting";
      return out;
    }
    if (generation != 0 && e->Generation() != generation) {
      out.why = "generation_changed";
      FailLocked(handle, out.why, nowMs);
      return out;
    }
    const int est = e->Established();
    if (est == 0) {
      out.kind = CommitResult::Pending;
      return out;
    }
    if (est < 0) {
      out.why = "not_established";
      FailLocked(handle, out.why, nowMs);
      return out;
    }
    out.closed = registry_.Commit(handle);
    for (uint64_t v : out.closed) DetachAndCloseLocked(v, "lru", nowMs);
    if (registry_.Get(handle, &re) && re.phase == EntryPhase::Parked) committedMs_[handle] = nowMs;
    out.kind = CommitResult::Committed;
    return out;
  }
'''),
('''  void Fail(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    std::shared_ptr<Entry> e = Detach(handle);''', '''  void Fail(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    std::lock_guard<std::mutex> tl(transitionMu_);
    FailLocked(handle, reason, nowMs);
  }

  // Counts for the record (multi r2 M1): live = kept within the limit (not closing, not marked to give way),
  // reserved = connections not yet established (counted in live), marked = parked PCs a reservation will close
  // if its connection is established, closing = teardowns still running.
  struct Counts {
    uint32_t live = 0, reserved = 0, marked = 0, closing = 0;
  };
  Counts CountsNow() const {
    Counts c;
    for (const auto& re : registry_.Snapshot()) {
      if (re.phase == EntryPhase::Closing) continue;
      if (re.evictionFor != 0) {
        ++c.marked;
        continue;
      }
      ++c.live;
      if (re.phase == EntryPhase::Connecting) ++c.reserved;
    }
    c.closing = static_cast<uint32_t>(PendingCloseCount());
    return c;
  }

 private:
  void FailLocked(uint64_t handle, const std::string& reason, uint64_t nowMs) {
    std::shared_ptr<Entry> e = Detach(handle);'''),
('''    if (e) {
      RecordEnded(handle, std::string(), reason, nowMs);
      closer_([e] { e->Close(); });
    }
  }
''', '''    if (e) {
      RecordEnded(handle, std::string(), reason, nowMs);
      closer_([e] { e->Close(); });
    }
  }

 public:
'''),
])

edit('apps/native_poc/src/session_table_test.cpp', [
('''  std::atomic<uint64_t> limitMs{0};
  int ParkPhaseCode() { return phase.load(); }''', '''  std::atomic<uint64_t> limitMs{0};
  std::atomic<int> established{1};  // scripted: 1 Connected, 0 still connecting, -1 ended
  std::atomic<uint64_t> gen{1};
  int Established() { return established.load(); }
  uint64_t Generation() { return gen.load(); }
  int ParkPhaseCode() { return phase.load(); }'''),
('''    auto c = table.Commit(r.handle, now);
    if (closed) *closed = c;''', '''    auto c = table.Commit(r.handle, 0, now).closed;
    if (closed) *closed = c;'''),
('''rig.table.Commit(late.handle, 4).empty()''', '''rig.table.Commit(late.handle, 0, 4).kind == SessionTable<FakeEntry>::CommitResult::Failed'''),
('''  std::printf("\\nsession_table_test: %d passed''', r'''  std::printf("[9] r2 M1: a commit evicts only for an ESTABLISHED connection; one that fails first evicts nothing\n");
  {
    using CR = SessionTable<FakeEntry>::CommitResult;
    Rig rig;  // limit 3
    const uint64_t a = rig.Connect("k|A", 1);
    rig.table.Activate(a, 1);
    const uint64_t b = rig.Connect("k|B", 2);
    rig.Switch(a, b, 2);
    const uint64_t c = rig.Connect("k|C", 3);
    rig.Switch(b, c, 3);
    rig.table.BeginPark(c, 4);
    rig.E("k|C")->phase = 4;  // A, B, C kept; none on screen; A used longest ago
    auto make = [&](const std::string& key) {
      return [&rig, key](uint64_t) {
        auto e = std::make_shared<FakeEntry>();
        e->established = 0;  // its worker started; Hello not answered yet
        e->gen = 7;
        rig.made[key] = e;
        return e;
      };
    };
    auto d = rig.table.Reserve("k|D", false, rig.table.Epoch(), make("k|D"));
    check("D reserved at the limit, A marked as its victim", d.kind == SessionRegistry::Reservation::New && d.victim == a);
    auto p1 = rig.table.Commit(d.handle, 7, 5);
    rig.RunCloses();
    check("D not established yet: commit Pending, A untouched", p1.kind == CR::Pending && rig.E("k|A")->closes == 0 &&
                                                                 rig.table.Find(a) != nullptr);
    auto counts = rig.table.CountsNow();
    check("the record tells reservations from the kept: live 3 (B, C, D reserved) + A marked", counts.live == 3 &&
          counts.reserved == 1 && counts.marked == 1, std::to_string(counts.live) + "/" + std::to_string(counts.reserved) +
          "/" + std::to_string(counts.marked));
    rig.E("k|D")->established = -1;  // D's Hello refused / timed out
    auto f1 = rig.table.Commit(d.handle, 7, 6);
    rig.RunCloses();
    check("D failed before it was established: Failed, A, B, C all kept (0 evicted)",
          f1.kind == CR::Failed && f1.why == "not_established" && rig.E("k|A")->closes == 0 &&
              rig.E("k|B")->closes == 0 && rig.E("k|C")->closes == 0 && rig.table.Find(a) && !rig.table.Find(d.handle));
    auto e2 = rig.table.Reserve("k|E", false, rig.table.Epoch(), make("k|E"));
    check("E reserved: A is the victim again", e2.victim == a);
    check("a commit with another connection's generation is refused (and gives E back)",
          rig.table.Commit(e2.handle, 8, 7).kind == CR::Failed && rig.E("k|A")->closes == 0);
    auto e3 = rig.table.Reserve("k|E", false, rig.table.Epoch(), make("k|E"));
    rig.E("k|E")->established = 1;
    auto ok = rig.table.Commit(e3.handle, 7, 8);
    rig.RunCloses();
    check("E established: Committed, exactly ONE eviction and it is A (LRU)", ok.kind == CR::Committed &&
          ok.closed.size() == 1 && ok.closed[0] == a && rig.E("k|A")->closes == 1 && rig.E("k|B")->closes == 0 &&
          rig.E("k|C")->closes == 0);
    // NEGATIVE: r1's order -- commit as soon as the worker started (the registry alone) -- loses A for a D that fails
    SessionRegistry reg;
    uint64_t hs[3];
    for (int i = 0; i < 3; ++i) {
      auto r = reg.Reserve("n|" + std::to_string(i), false, reg.Epoch());
      reg.Commit(r.handle);
      hs[i] = r.handle;
    }
    auto rd = reg.Reserve("n|D", false, reg.Epoch());
    const auto lost = reg.Commit(rd.handle);  // r1: madeGeneration != 0 -> nativeCommit at once
    reg.Fail(rd.handle);                       // then D's Hello fails
    check("NEGATIVE (r1 order): a D that then fails has already closed a kept PC", lost.size() == 1 && lost[0] == hs[0]);
  }

  std::printf("\nsession_table_test: %d passed'''),
])
print('ok')
