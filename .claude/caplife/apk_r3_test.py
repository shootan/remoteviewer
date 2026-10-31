wt = 'C:/work/gnlink-wt/apk-prelogin-select/apps/native_poc/src/'

def edit(rel, pairs):
    p = wt + rel
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (rel, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('client_session_prelogin_select_test.cpp', [
    ('''  void RefuseSelectionFor(uint64_t tag) override {
    std::lock_guard<std::mutex> l(mu_);
    gate_.Refuse(tag);
  }
''', '''  void RefuseSelectionFor(uint64_t tag) override {
    std::lock_guard<std::mutex> l(mu_);
    gate_.Refuse(tag);
  }
  // r3 (N1): as android_video_decoder.cpp -- judged and recorded under the state Prepare/Abort take.
  LocalCommit CommitLocalSelectionFor(uint64_t tag, const std::function<void(uint64_t)>& commit) override {
    std::lock_guard<std::mutex> l(mu_);
    if (tag == 0 || gate_.pending() != tag) return LocalCommit::Abandoned;
    const uint64_t generation = gate_.LocalBoundFor(tag);
    if (generation == 0) return LocalCommit::Unbound;
    commit(generation);
    return LocalCommit::Committed;
  }
'''),
    ('''// P7: a new connection.''', '''// P9 (apk-0232 r3, Codex N1 last boundary): the poll has found the local pick A bound to its IDR's generation and is
// about to record it (test hook) -- there the user cancels A, or prepares a newer pick B. The cancel / Prepare must
// not finish in that gap with A recorded selected after it: either A's record comes first (then the cancel follows
// it), or nothing of A is recorded. With B: B then completes with its OWN IDR and generation.
void p9_commit_vs_cancel() {
  std::printf("\\n--- P9: the poll's completion and a cancel / new Prepare are one boundary ---\\n");
  for (const bool pickB : {false, true}) {
    const char* tagName = pickB ? "P9 (new Prepare B)" : "P9 (cancel)";
    FakeHost h;
    h.pongFlags = kPrelogin;
    if (!h.Start()) return check("P9: fake host starts", false);
    n::ClientSessionController c;
    GateSink sink;
    check((std::string(tagName) + ": the session connects, logon-screen role known").c_str(),
          connect_to(c, h, &sink) && role_known(c, h));
    std::atomic<bool> entered{false}, uiDone{false};
    std::atomic<int> verdictAtUiDone{-1};
    std::thread ui;
    c.SetTestBeforeLocalCommitHook([&](uint64_t tag, uint64_t generation) {
      if (tag != 91 || entered.exchange(true)) return;
      // the user, on the UI thread, in the gap: back out of A (or prepare B)
      ui = std::thread([&] {
        if (pickB) sink.Prepare(92); else sink.Abort();
        verdictAtUiDone = static_cast<int>(verdict(status_of(c)));
        uiDone = true;
      });
      // give the UI's call every chance to finish inside the gap (it cannot, if the gap is one boundary)
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
      while (!uiDone.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      (void)generation;
    });
    sink.Prepare(91);
    sink.NewDecoder();
    c.RequestDesktopMode();  // the role is known: a local selection at once
    wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) >= 1; }, 2500);
    sink.Feed(700, true);    // A's IDR binds it
    check((std::string(tagName) + ": the poll reached A bound, before its record").c_str(),
          wait_until([&] { return entered.load(); }, 3000));
    wait_until([&] { return uiDone.load(); }, 3000);
    if (ui.joinable()) ui.join();
    c.SetTestBeforeLocalCommitHook(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const Verdict finalV = verdict(status_of(c));
    const bool recordedAfterUi = finalV == Verdict::Acked && verdictAtUiDone.load() != static_cast<int>(Verdict::Acked);
    check((std::string(tagName) + ": A is not recorded selected AFTER the user's call finished (A first, or nothing)").c_str(),
          uiDone.load() && !recordedAfterUi,
          "at ui done=" + std::to_string(verdictAtUiDone.load()) + " final=" + status_of(c));
    if (!pickB) {
      check("P9 (cancel): after the cancel the sink holds no selection", sink.CurrentSelectionTag() == 0 && sink.ready() == 0);
    } else {
      c.RequestDesktopMode();
      sink.NewDecoder();
      wait_until([&] { return h.Count(MessageType::ControlRequestKeyFrame) >= 2; }, 2500);
      sink.Feed(701, true);
      const bool acked = wait_until([&] {
        return verdict(status_of(c)) == Verdict::Acked && c.WindowPanelSnapshotCopy().lastSelectStreamGeneration == 701;
      }, 2500);
      check("P9 (new Prepare B): B completes with its OWN IDR and generation", acked && sink.ready() == 92,
            status_of(c) + " gen=" + std::to_string(c.WindowPanelSnapshotCopy().lastSelectStreamGeneration));
    }
    check((std::string(tagName) + ": no WindowSelect on the wire").c_str(), h.Count(MessageType::ControlWindowSelect) == 0);
    c.Disconnect();
    h.Stop();
  }
}

// P7: a new connection.'''),
    ('''  p8_stale_turn();
  std::printf(''', '''  p8_stale_turn();
  p9_commit_vs_cancel();
  std::printf('''),
])
print('ok')
