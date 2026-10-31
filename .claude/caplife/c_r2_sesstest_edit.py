p = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src\android_decoder_inbox_session_test.cpp'
raw = open(p, 'rb').read()
crlf = b'\r\n' in raw
s = raw.decode('utf-8').replace('\r\n', '\n')


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('''  std::atomic<bool> ignoreKeyRequests{false};
''', '''  std::atomic<bool> ignoreKeyRequests{false};
  std::atomic<bool> suppressIdr{false};  // C r2: no IDR at all (a selection's own included) until cleared
''')
rep('''        const bool key = want > 0 && idrWanted.compare_exchange_strong(want, want - 1);''',
    '''        const bool key = !suppressIdr.load() && want > 0 && idrWanted.compare_exchange_strong(want, want - 1);''')
# S2: the owner change AND a new pick at the accepted point
rep('''        if (std::strcmp(point, "accepted") == 0 && fired.fetch_add(1) == 0) r.sink.Bind(22);  // this PC parked''',
    '''        if (std::strcmp(point, "accepted") == 0 && fired.fetch_add(1) == 0) {
          r.sink.Bind(22);               // this PC parked ...
          r.c.RequestWindowSelect(1001);  // ... and (C r2, Codex N2) a new pick on it at the same point
        }''')

new_cases = r'''
  std::puts("\n--- [S5] C r2 N2: a new pick + owner change right BEFORE the scheduler dequeues a published request");
  {
    Rig r;
    const bool up = r.Up(51);
    check("[S5] connected and ready", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      std::atomic<int> state{0};  // 0 off, 1 armed (waiting for the want), 2 fired
      std::atomic<bool> published{false};
      r.c.SetTestKeyRequestHook([&](const char* point) {
        if (std::strcmp(point, "publish") == 0 && state.load() == 1) published = true;  // sink-only work allowed here
      });
      r.c.SetTestPassBoundaryHook([&](bool) {
        if (state.load() != 1) return;
        // the control thread waits here (before its pass reaches NextAction) for the RX thread to publish the episode's
        // request; then the pick and the owner change land between that publish and the dequeue
        const auto t0 = Clock::now();
        while (!published.load() && ms_between(t0, Clock::now()) < 3000) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        r.sink.Bind(52);
        r.c.RequestWindowSelect(1001);
        state = 2;
      });
      const uint64_t stale0 = r.c.KeyRequestsDroppedStale(), cancel0 = r.c.KeyRequestsDroppedCancelled();
      const auto t0 = Clock::now();
      state = 1;
      r.Loss();
      std::this_thread::sleep_for(std::chrono::milliseconds(2500));
      const auto reqs = r.host.RequestsSinceMs(t0);
      check("[S5] the published request was cancelled with its pending flag -- the host received NO key request",
            state.load() == 2 && published.load() && reqs.empty(),
            "published=" + std::to_string(published.load() ? 1 : 0) + " host requests=" + list(reqs) +
                " droppedStale+" + std::to_string(r.c.KeyRequestsDroppedStale() - stale0) + " cancelled+" +
                std::to_string(r.c.KeyRequestsDroppedCancelled() - cancel0));
      r.c.SetTestPassBoundaryHook(nullptr);
    }
    r.Down();
  }

  std::puts("\n--- [S6] C r2 N2: a new valid request held WHILE the held check asks the sink -- not erased");
  {
    Rig r;
    const bool up = r.Up(61);
    check("[S6] connected and ready", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      r.c.TestConfigureKeyframeLimiter(3000000, 1, 100);
      std::this_thread::sleep_for(std::chrono::milliseconds(3500));
      std::atomic<int> fired{0};
      r.c.SetTestKeyRequestHook([&](const char* point) {
        if (std::strcmp(point, "held") == 0 && fired.fetch_add(1) == 0) {
          r.sink.Bind(62);                             // the held recovery ask becomes stale ...
          r.c.TestRequestKey(kKeyCausePipeline);       // ... and a NEW valid one is held meanwhile (min interval)
        }
      });
      const auto t0 = Clock::now();
      r.Loss();
      std::this_thread::sleep_for(std::chrono::milliseconds(6500));  // past the limiter's next interval
      const auto reqs = r.host.RequestsSinceMs(t0);
      check("[S6] the stale held ask is dropped but the pipeline ask held meanwhile is NOT erased: it goes once the "
            "limiter allows (2 requests: the first, then the pipeline one)",
            fired.load() >= 1 && reqs.size() == 2, "hook=" + std::to_string(fired.load()) + " host requests=" + list(reqs));
    }
    r.Down();
  }

  std::puts("\n--- [S7] C r2 N2: the owner changes INSIDE the publish (between the limiter and the descriptor)");
  {
    Rig r;
    const bool up = r.Up(71);
    check("[S7] connected and ready", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      std::atomic<int> fired{0};
      r.c.SetTestKeyRequestHook([&](const char* point) {
        if (std::strcmp(point, "publish") == 0 && fired.fetch_add(1) == 0) r.sink.Bind(72);  // the sink only
      });
      const uint64_t stale0 = r.c.KeyRequestsDroppedStale();
      const auto t0 = Clock::now();
      r.Loss();
      std::this_thread::sleep_for(std::chrono::milliseconds(2500));
      const auto reqs = r.host.RequestsSinceMs(t0);
      check("[S7] the request published as the owner changed carries its descriptor and is judged stale: not sent",
            fired.load() >= 1 && reqs.empty() && r.c.KeyRequestsDroppedStale() > stale0,
            "hook=" + std::to_string(fired.load()) + " host requests=" + list(reqs));
    }
    r.Down();
  }

  std::puts("\n--- [S8] C r2 §8 (a): the owed IDR's 8 asks spent -> the sink covered-exhausted, no more asks; a late IDR recovers");
  {
    Rig r;
    const bool up = r.Up(81);
    check("[S8] connected and ready", up);
    if (up) {
      r.host.ignoreKeyRequests = true;
      r.host.suppressIdr = true;  // the new selection's IDR never comes: the duty is owed and asked for
      const auto t0 = Clock::now();
      r.sink.PrepareForWindowSelectionAs(81, 2);
      r.c.RequestWindowSelect(1001);
      std::this_thread::sleep_for(std::chrono::milliseconds(9500));  // 8 owed asks, 1 s apart
      const auto asks = r.host.RequestsSinceMs(t0);
      const std::string mid = r.sink.DebugStatus();
      std::this_thread::sleep_for(std::chrono::milliseconds(5500));  // the final window (4 s) and past it
      const auto asks2 = r.host.RequestsSinceMs(t0);
      const std::string st = r.sink.DebugStatus();
      check("[S8] the owed asks stop at the session's 8 (limiter-queued); the sink is told: covered",
            asks.size() >= 1 && asks.size() <= 8 && field(mid, "covered") == 1,
            "host requests=" + std::to_string(asks.size()) + " covered=" + sfield(mid, "covered"));
      check("[S8] after the final window: covered-exhausted, NO further request (no new episode budget)",
            field(st, "coveredExh") == 1 && asks2.size() == asks.size() && field(st, "asks") == 0,
            "coveredExh=" + sfield(st, "coveredExh") + " host requests " + std::to_string(asks.size()) + "->" +
                std::to_string(asks2.size()));
      const size_t q0 = queued_count();
      r.host.suppressIdr = false;
      r.host.idrWanted = 1;  // the host's late / periodic IDR
      const bool decoded = wait_until([&] {
        for (uint8_t t : queued_tags_from(q0))
          if (t == idr_tag(r.host.generation.load())) return true;
        return false;
      }, 2000);
      wait_until([&] { return field(r.sink.DebugStatus(), "covered") == 0; }, 1000);
      check("[S8] the late IDR is decoded, pays the duty and ends the covered state", decoded &&
            field(r.sink.DebugStatus(), "covered") == 0 && field(r.sink.DebugStatus(), "coveredExh") == 0);
    }
    r.Down();
  }

  std::printf("\nandroid_decoder_inbox_session_test: %d checks'''
rep('''
  std::printf("\\nandroid_decoder_inbox_session_test: %d checks''', new_cases)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))
print('ok')
