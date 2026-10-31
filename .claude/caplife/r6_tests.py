S = 'C:\\work\\gnlink-wt\\rel-0163\\apps\\native_poc\\src\\'

def edit(name, pairs):
    p = S + name
    s = open(p, 'rb').read().decode('utf-8')
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for old, new in pairs:
        assert s.count(old) == 1, (name, s.count(old), old[:90])
        s = s.replace(old, new)
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

edit('viewer_selection_gate_test.cpp', [
    ('''    CHECK(!g.CommitFrame(a, true, 0, 4, &gen));''', '''    CHECK(!g.CommitFrame(a, true, 0, 4, &gen, nullptr));'''),
    ('''    CHECK(!g.CommitFrame(a, true, 4, 4, &gen));  // A's picture after B began''', '''    CHECK(!g.CommitFrame(a, true, 4, 4, &gen, nullptr));  // A's picture after B began'''),
    ('''    CHECK(!g.CommitFrame(b, true, 4, 4, &gen));  // B's picture after B was cancelled (nothing pending)''',
     '''    CHECK(!g.CommitFrame(b, true, 4, 4, &gen, nullptr));  // B's picture after B was cancelled (nothing pending)'''),
    ('''    CHECK(g.CommitFrame(c, true, 6, 6, &gen) && gen == 6 && g.expectedGeneration.load() == 6);
    CHECK(g.CommitFrame(c, true, 6, 6, &gen) && gen == 6);  // later frames of the bound op''',
     '''    CHECK(g.CommitFrame(c, true, 6, 6, &gen, nullptr) && gen == 6 && g.expectedGeneration.load() == 6);
    CHECK(g.CommitFrame(c, true, 6, 6, &gen, nullptr) && gen == 6);  // later frames of the bound op'''),
    ('''    CHECK(g.CommitFrame(c, true, 6, 6, &gen));  // the same op, revealed: its stream goes on
    CHECK(g.CommitFrame(0, false, 0, 9, &gen) && gen == 9);  // an ordinary frame: as before
  }''',
     '''    CHECK(g.CommitFrame(c, true, 6, 6, &gen, nullptr));  // the same op, revealed: its stream goes on
    CHECK(g.CommitFrame(0, false, 0, 9, &gen, nullptr) && gen == 9);  // an ordinary frame: as before
  }
  {
    // r6 (Codex R3 2): op R revealed at generation 7; a new local op A begins and is cancelled before its reveal.
    // A's picture is refused (the same epoch as A, but A was never revealed); R's own later picture is refused too
    // (R is no longer the epoch); while R was the epoch, its later pictures went on.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t r = g.epoch.load();
    uint64_t gen = 0;
    CHECK(g.CommitFrame(r, true, 7, 7, &gen, nullptr) && gen == 7);
    CHECK(g.RecordReveal(7, r));
    CHECK(g.CommitReveal());
    g.Clear();
    CHECK(g.CommitFrame(r, true, 7, 7, &gen, nullptr) && gen == 7);  // R revealed: its stream goes on
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    g.Clear();  // A cancelled before its reveal
    CHECK(!g.CommitFrame(a, true, 9, 9, &gen, nullptr));
    CHECK(!g.CommitFrame(r, true, 7, 7, &gen, nullptr));
  }
  {
    // r6 (R3 1): a permitted picture keeps the gate's lock until the caller lets it go -- a Clear/Begin waits.
    SelectionGateState g;
    g.BeginLocal();
    const uint64_t a = g.epoch.load();
    uint64_t gen = 0;
    std::unique_lock<std::mutex> held;
    CHECK(g.CommitFrame(a, true, 5, 5, &gen, &held) && held.owns_lock());
    std::atomic<bool> began{false};
    std::thread other([&] {
      g.Clear();
      g.BeginLocal();
      began = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(!began.load());  // blocked while the write is still being made
    held.unlock();
    other.join();
    CHECK(began.load() && g.epoch.load() == a + 1);
  }'''),
])
p = S + 'viewer_selection_gate_test.cpp'
s = open(p, 'rb').read().decode('utf-8')
if '#include <thread>' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    i = s.index('#include')
    s = s[:i] + '#include <atomic>' + nl + '#include <chrono>' + nl + '#include <mutex>' + nl + '#include <thread>' + nl + s[i:]
    open(p, 'wb').write(s.encode('utf-8'))

edit('viewer_prelogin_login_e2e_test.cpp', [
    ('''    check("E1 PS (4) R3: A cancelled and B begun right before A's first picture's commit -- A's picture REFUSED, not written",
          hookFired && ctx.sel.localFramesRefused.load() > refusedAtFire.load(),''',
     '''    // The refusal is the next commit after the hook; B's own first picture needs B's own keyframe (later), so the
    // version read the moment the refusal is counted is A's not-written one.
    pump_until([&] { return ctx.sel.localFramesRefused.load() > refusedAtFire.load(); }, 3000);
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
      versionAfter = ctx.frameBuf.frame.version;
    }
    check("E1 PS (4) R3: A cancelled and B begun right before A's first picture's commit -- A's picture REFUSED, not written",
          hookFired && ctx.sel.localFramesRefused.load() > refusedAtFire.load() && versionAfter == versionAtFire.load(),'''),
    ('''          "opA=" + std::to_string(opA.load()) + " opB=" + std::to_string(opB.load()) + " versionAfterFire=" +
              std::to_string(versionAfter));
''',
     '''          "opA=" + std::to_string(opA.load()) + " opB=" + std::to_string(opB.load()) + " versionAfterFire=" +
              std::to_string(versionAfter));

    // (5) rel-0163 r6 (Codex R3 1): between a picture's PERMISSION and its WRITE. A local pick A; right after A's first
    //     picture is permitted (the gate's lock held), another thread tries to cancel A and begin B: it must wait
    //     until A's picture is written; only then does B begin. A's write (made while A was live) lands; B completes on
    //     its own keyframe.
    set_picker_visible_and_sync_stream(ctx, true);
    pump_until([] { return false; }, 1500);
    std::atomic<bool> fired5{false}, blocked5{false}, began5{false};
    std::atomic<uint64_t> version5{0};
    std::thread other5;
    ctx.sel.testAfterFramePermit = [&] {
      if (fired5.load() || !ctx.sel.IsLiveLocal(ctx.sel.epoch.load())) return;
      fired5 = true;
      version5 = ctx.frameBuf.frame.version;  // (the FrameBuffer lock is held here by the receive thread itself)
      other5 = std::thread([&] {
        ctx.sel.Clear();
        ctx.sel.BeginLocal();
        began5 = true;
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      blocked5 = !began5.load();
    };
    const uint64_t p5 = presents();
    press_desktop();
    const bool fired5ok = pump_until([&] { return fired5.load() && began5.load(); }, 8000);
    ctx.sel.testAfterFramePermit = nullptr;
    if (other5.joinable()) other5.join();
    uint64_t version5After = 0;
    {
      std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
      version5After = ctx.frameBuf.frame.version;
    }
    check("E1 PS (5) R3: a cancel/new op tried between A's permission and its write WAITS -- A's write lands first",
          fired5ok && blocked5.load() && version5After > version5.load(),
          "blocked=" + std::to_string(blocked5.load() ? 1 : 0) + " version " + std::to_string(version5.load()) + " -> " +
              std::to_string(version5After));
    uint64_t lat5 = 0;
    const bool shown5 = wait_presented(p5, qpc_now_us(), 12000, &lat5);
    check("E1 PS (5) R3: ...then B begins and completes on its own keyframe", shown5 && ctx.sel.activeStreamGeneration.load() != 0);
'''),
])
print('ok')
