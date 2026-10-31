p = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/android_decoder_liveness_test.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('''// Lets go after `ms` on a thread of its own -- whatever a blocked caller does, the test ends.
struct LetGoAfter {
  std::thread t;
  explicit LetGoAfter(int ms) : t([ms] {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    let_go();
  }) {}
  ~LetGoAfter() {
    if (t.joinable()) t.join();
  }
};''', '''// Lets go after `ms` on a thread of its own -- whatever a blocked caller does (the old sink blocks the test's own
// thread inside a frame call), the test ends. Cancelled when the case ends first.
struct LetGoAfter {
  std::mutex m;
  std::condition_variable cv;
  bool cancelled = false;
  std::thread t;
  explicit LetGoAfter(int ms) {
    t = std::thread([this, ms] {
      std::unique_lock<std::mutex> lk(m);
      if (!cv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return cancelled; })) let_go();
    });
  }
  ~LetGoAfter() {
    {
      std::lock_guard<std::mutex> lk(m);
      cancelled = true;
    }
    cv.notify_all();
    if (t.joinable()) t.join();
  }
};''')
rep('#include <atomic>\n#include <chrono>\n', '#include <atomic>\n#include <chrono>\n#include <condition_variable>\n')
rep('#include <memory>\n', '#include <memory>\n#include <mutex>\n#include <set>\n')
rep('''    hold(fake_ndk::kHoldCreate);
    const Snapshot s0 = snap();
    Callers c(sink, 0, 0x81, 0x02);''', '''    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    const Snapshot s0 = snap();
    Callers c(sink, 0, 0x81, 0x02);''')
rep('''    uint64_t inboxMax = 0;
    while (us_since(heldAt) < static_cast<int64_t>(gHoldMs) * 1000) {
      const uint64_t inbox = field(sink.DebugStatus(), "inbox");
      if (inbox != ~0ull && inbox > inboxMax) inboxMax = inbox;''', '''    uint64_t inboxMax = ~0ull;  // never read (an old sink reports no inbox)
    while (us_since(heldAt) < static_cast<int64_t>(gHoldMs) * 1000) {
      const uint64_t inbox = field(sink.DebugStatus(), "inbox");
      if (inbox != ~0ull && (inboxMax == ~0ull || inbox > inboxMax)) inboxMax = inbox;''')
rep('''    hold(fake_ndk::kHoldCreate);
    sink.OnEncodedH264Frame(frame(0, true, 0x84));''', '''    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(0, true, 0x84));''')
rep('''[5] the SELECTION is cancelled while its codec is being created: never 'ready' on an old picture''',
    '''[5] a NEWER selection while the old one's codec is being created: never 'ready' on an old picture''')
rep('''    MaxUs abort;
    timed(abort, [&] { sink.AbortWindowSelection(); });
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check("[5] the create was really held", blocked);
    check("[5] Abort returned within the bound", abort.v < kCallBoundUs, abort.str());
    check("[5] the cancelled selection is not ready", sink.ReadySelectionGeneration() == 0);''', '''    MaxUs prepare;
    timed(prepare, [&] { sink.PrepareForWindowSelection(10); });  // the user picks another window meanwhile
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check("[5] the create was really held", blocked);
    check("[5] the newer Prepare returned within the bound", prepare.v < kCallBoundUs, prepare.str());
    check("[5] the newer selection is not made ready by the old selection's codec / picture",
          sink.ReadySelectionGeneration() != 10, "ready=" + std::to_string(sink.ReadySelectionGeneration()));''')
rep('''    sink->SetSurface(env, sa());
    hold(fake_ndk::kHoldCreate);
    sink->OnEncodedH264Frame(frame(0, true, 0x8E));''', '''    sink->SetSurface(env, sa());
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink->OnEncodedH264Frame(frame(0, true, 0x8E));''')
open(p, 'w', encoding='utf-8').write(s)
print('ok')
