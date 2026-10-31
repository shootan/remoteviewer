SRC = r'C:\work\gnlink-wt\apk-inbox-c\apps\native_poc\src'


def load(p):
    raw = open(p, 'rb').read()
    return raw.decode('utf-8').replace('\r\n', '\n'), b'\r\n' in raw


def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))


# ---- [C12] the product sink judges merges of descriptors in the sink's own states
p = SRC + r'\android_decoder_inbox_c_test.cpp'
s, crlf = load(p)
anchor = '''  std::printf("\\nandroid_decoder_inbox_c_test: %d checks'''
assert s.count(anchor) == 1
c12 = r'''  std::puts("\n--- [C12] C r3 N2 (Codex's sequences): a late, OLDER ask merged after a newer one -- the sink still takes it");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    rx.sendBack = false;
    const uint64_t g = start(sink, rx, 41, 411);
    // (1) the same epoch: episode e then, after its IDR committed, episode e+1
    make_loss(sink, rx, g);
    DecoderKeyRequest older;
    older.causes = kKeyCauseRecovery;
    older.sinkEpoch = field(sink.DebugStatus(), "keyEpoch");
    older.episodeId = field(sink.DebugStatus(), "episode");
    rx.send(frame(g, true, 0x91));  // its IDR commits: the episode closes
    wait_until([&] { return sfield(sink.DebugStatus(), "recovery") == "idle"; }, 1500);
    make_loss(sink, rx, g);
    DecoderKeyRequest newer;
    newer.causes = kKeyCauseRecovery;
    newer.sinkEpoch = field(sink.DebugStatus(), "keyEpoch");
    newer.episodeId = field(sink.DebugStatus(), "episode");
    DecoderKeyRequest merged = newer;
    merged.Merge(older);  // published late
    check("[C12] (1) episode e+1 published, then the late episode-e ask: the sink still takes the merged ask (recovery)",
          newer.episodeId > older.episodeId && sink.DecoderKeyRequestValidCauses(newer) == kKeyCauseRecovery &&
              sink.DecoderKeyRequestValidCauses(older) == 0 &&
              sink.DecoderKeyRequestValidCauses(merged) == kKeyCauseRecovery,
          "episodes " + std::to_string(older.episodeId) + "/" + std::to_string(newer.episodeId) + " merged episode=" +
              std::to_string(merged.episodeId) + " valid=" + std::to_string(sink.DecoderKeyRequestValidCauses(merged)));
    // (2) a newer epoch: two aborts, a pick between them -- both descriptors handed out by the sink itself
    sink.PrepareForWindowSelection(42);
    sink.OnWindowSelectionControlResultFor(answer(421), 42);
    sink.AbortWindowSelection();
    DecoderKeyRequest ab1, ab2;
    const bool t1 = sink.TakeDecoderKeyRequest(&ab1);
    sink.PrepareForWindowSelection(43);
    sink.OnWindowSelectionControlResultFor(answer(431), 43);
    sink.AbortWindowSelection();
    const bool t2 = sink.TakeDecoderKeyRequest(&ab2);
    DecoderKeyRequest m2 = ab2;
    m2.Merge(ab1);  // the older epoch's ask, published late
    check("[C12] (2) the newer epoch's abort ask published, then the late older-epoch one: the merged ask still valid",
          t1 && t2 && ab2.sinkEpoch > ab1.sinkEpoch && (ab1.causes & kKeyCauseSelectionAbort) != 0 &&
              sink.DecoderKeyRequestValidCauses(ab1) == 0 &&
              sink.DecoderKeyRequestValidCauses(m2) == kKeyCauseSelectionAbort && m2.sinkEpoch == ab2.sinkEpoch,
          "epochs " + std::to_string(ab1.sinkEpoch) + "/" + std::to_string(ab2.sinkEpoch) + " merged epoch=" +
              std::to_string(m2.sinkEpoch) + " valid=" + std::to_string(sink.DecoderKeyRequestValidCauses(m2)));
    DecoderKeyRequest m3 = ab1;
    m3.Merge(ab2);  // the issue order
    check("[C12] in the issue order the same two merge to the newer one (valid)",
          sink.DecoderKeyRequestValidCauses(m3) == kKeyCauseSelectionAbort);
    sink.SetSurface(env, nullptr);
  }

'''
s = s.replace(anchor, c12 + anchor)
save(p, s, crlf)

# ---- [S10] the product session: a late older descriptor into accepted (coalesced) and into held
p = SRC + r'\android_decoder_inbox_session_test.cpp'
s, crlf = load(p)
anchor = '''  std::printf("\\nandroid_decoder_inbox_session_test: %d checks'''
assert s.count(anchor) == 1
s10 = r'''  std::puts("\n--- [S10] C r3 N2: an OLDER descriptor published after a newer one -- accepted (coalesced) and held");
  for (const bool heldCase : {false, true}) {
    const std::string tag = heldCase ? "[S10 held]" : "[S10 accepted]";
    Rig r;
    const bool up = r.Up(heldCase ? 102 : 101);
    check(tag + " connected and ready", up);
    if (!up) {
      r.Down();
      continue;
    }
    r.host.ignoreKeyRequests = true;
    // a valid ask of the sink's CURRENT epoch (selection-abort: valid exactly while the epoch stands, and nothing the
    // sink would ask by itself here) and an older one: the epoch before
    const uint64_t epoch = field(r.sink.DebugStatus(), "keyEpoch");
    n::DecoderKeyRequest newer, older;
    newer.causes = older.causes = n::kKeyCauseSelectionAbort;
    newer.sinkEpoch = epoch;
    older.sinkEpoch = epoch - 1;
    const uint64_t stale0 = r.c.KeyRequestsDroppedStale();
    if (!heldCase) {
      // both taken by the limiter and coalesced in ACCEPTED before the scheduler takes them (its pass held meanwhile)
      r.c.TestConfigureKeyframeLimiter(0, 1, 100);
      std::atomic<int> gate{0};  // 0 off, 1 hold the pass, 2 released
      r.c.SetTestPassBoundaryHook([&](bool) {
        while (gate.load() == 1) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      });
      gate = 1;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));  // the control thread now waits in the hook
      const auto t0 = Clock::now();
      const bool q1 = r.c.TestPublishKey(newer);
      const bool q2 = r.c.TestPublishKey(older);
      gate = 2;
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      r.c.SetTestPassBoundaryHook(nullptr);
      const auto reqs = r.host.RequestsSinceMs(t0);
      check(tag + " newer then older, both accepted and coalesced: the merged ask is still valid -- sent, not dropped",
            q1 && q2 && reqs.size() == 1 && r.c.KeyRequestsDroppedStale() == stale0,
            "queued=" + std::to_string(q1) + "/" + std::to_string(q2) + " host requests=" + list(reqs) + " droppedStale+" +
                std::to_string(r.c.KeyRequestsDroppedStale() - stale0));
    } else {
      // the limiter's interval just spent by a pipeline ask: both go to HELD and merge there
      r.c.TestConfigureKeyframeLimiter(2000000, 1, 100);
      std::this_thread::sleep_for(std::chrono::milliseconds(2500));
      const auto t0 = Clock::now();
      const bool q0 = r.c.TestRequestKey(n::kKeyCausePipeline);
      const bool q1 = r.c.TestPublishKey(newer);
      const bool q2 = r.c.TestPublishKey(older);
      std::this_thread::sleep_for(std::chrono::milliseconds(4000));  // past the interval: the held ask goes
      const auto reqs = r.host.RequestsSinceMs(t0);
      check(tag + " newer then older, both held: the merged held ask is still valid -- it goes once the limiter allows",
            q0 && !q1 && !q2 && reqs.size() == 2 && r.c.KeyRequestsDroppedStale() == stale0,
            "queued=" + std::to_string(q0) + "/" + std::to_string(q1) + "/" + std::to_string(q2) + " host requests=" +
                list(reqs) + " droppedStale+" + std::to_string(r.c.KeyRequestsDroppedStale() - stale0));
    }
    r.Down();
  }

'''
s = s.replace(anchor, s10 + anchor)
save(p, s, crlf)
print('ok')
