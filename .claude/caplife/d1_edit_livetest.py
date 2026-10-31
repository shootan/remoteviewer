P = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/android_decoder_liveness_test.cpp'
s = open(P, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:80], s.count(a))
    s = s.replace(a, b)


rep('#include <thread>\n#include <vector>\n', '#include <thread>\n#include <type_traits>\n#include <vector>\n')
rep('''uint64_t field(const std::string& status, const char* key) {''', '''// r3 D1: the same calls against the sink of 9f9a91c (void SetSurface, no released seq) -- for the negative runs.
constexpr int64_t kNoResult = INT64_MIN;
template <class S>
int64_t set_surface(S& s, jobject surface) {
  if constexpr (std::is_void_v<decltype(s.SetSurface(env, surface))>) {
    s.SetSurface(env, surface);
    return kNoResult;
  } else {
    return s.SetSurface(env, surface);
  }
}
template <class S, class = void>
struct HasReleasedSeq : std::false_type {};
template <class S>
struct HasReleasedSeq<S, std::void_t<decltype(std::declval<const S&>().SurfaceReleasedSeq())>> : std::true_type {};
template <class S>
uint64_t released_seq(const S& s) {
  if constexpr (HasReleasedSeq<S>::value) return s.SurfaceReleasedSeq();
  else return 0;
}

uint64_t field(const std::string& status, const char* key) {''')
start = s.index('  std::puts("\\n--- [3] DETACH while the codec is being created')
end = s.index('  std::puts("\\n--- [4] the OWNER changes')
s = s[:start] + '''  std::puts("\\n--- [3] DETACH while the codec is being created (r3 D1): bounded for the UI, none for RX / control");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(0, true, 0x84));
    const bool blocked = wait_until(held_now, 2000);
    const auto heldAt = Clock::now();
    Callers c(sink, 0, 0x85, 0x03);
    c.start();
    std::atomic<int64_t> detachUs{-1};
    std::atomic<int64_t> detachResult{0};
    std::thread ui([&] {
      const auto t0 = Clock::now();
      detachResult.store(set_surface(sink, nullptr));
      detachUs.store(us_since(t0));
    });
    // the UI's detach must come back by itself, bounded (2 s), while the codec is still held
    const bool uiBack = wait_until([&] { return detachUs.load() >= 0; }, 4000);
    ui.join();
    const int64_t r1 = detachResult.load();
    // a second detach while the first is pending: not waited for again (never stacked)
    const auto t2 = Clock::now();
    const int64_t r2 = set_surface(sink, nullptr);
    const int64_t secondUs = us_since(t2);
    // a new producer while the detach is pending: refused, nothing configured on it
    const Snapshot w0 = snap();
    const int64_t r3 = set_surface(sink, sb());
    const Snapshot w1 = snap();
    const uint64_t releasedBefore = released_seq(sink);
    const bool stillHeld = held_now() && us_since(heldAt) < static_cast<int64_t>(gHoldMs) * 1000;
    while (us_since(heldAt) < static_cast<int64_t>(gHoldMs) * 1000) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const Snapshot beforeLetGo = snap();
    let_go();
    const uint64_t want = static_cast<uint64_t>(r2 < 0 ? -r2 : (r2 > 0 ? r2 : 0));
    const bool lateDone = wait_until([&] { return want != 0 && released_seq(sink) >= want; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    c.finish();
    const Snapshot afterLate = snap();
    const void* configuredAfterLate = nullptr;
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      configuredAfterLate = fake_ndk::controls().lastConfiguredWindow;
    }
    check("[3] the create was really held", blocked);
    check("[3] the UI's detach came back within its deadline (<= 2.5 s) while the codec was held, as NOT done",
          uiBack && detachUs.load() <= 2500000 && detachUs.load() >= 1800000 && r1 < 0 && r1 != kNoResult,
          "detach took " + std::to_string(detachUs.load() / 1000) + " ms result=" + std::to_string(r1));
    check("[3] meanwhile RX and control returned within the bound", c.rx.v < kCallBoundUs && c.control.v < kCallBoundUs,
          "rx " + c.rx.str() + " control " + c.control.str());
    check("[3] a second detach while the first is pending is not waited for again", r2 < 0 && r2 != kNoResult &&
          secondUs < kCallBoundUs, "took " + std::to_string(secondUs / 1000) + " ms result=" + std::to_string(r2));
    check("[3] a new surface while the detach is pending is refused (0), its window given back at once",
          r3 == 0 && w1.acquired - w0.acquired == 1 && w1.released - w0.released == 1,
          "result=" + std::to_string(r3));
    check("[3] not reported done while the codec is still held", stillHeld && releasedBefore < want,
          "released=" + std::to_string(releasedBefore) + " want=" + std::to_string(want));
    check("[3] the late completion is reported (released seq reaches the detach) once the codec lets go", lateDone,
          "released=" + std::to_string(released_seq(sink)));
    check("[3] the late completion connects nothing: no codec started, none alive, nothing configured on B",
          afterLate.started == beforeLetGo.started && afterLate.created == afterLate.deleted && configuredAfterLate != sb(),
          "created=" + std::to_string(afterLate.created) + " started=" + std::to_string(afterLate.started) +
              " deleted=" + std::to_string(afterLate.deleted));
    // after it: the next surface is taken and used
    const size_t q0 = snap().queued;
    const int64_t r4 = set_surface(sink, sb());
    sink.OnEncodedH264Frame(frame(0, true, 0x8F));
    check("[3] once done, the next surface is taken and the next IDR goes into a codec on it",
          r4 > 0 && wait_until([&] { auto q = queued_from(q0); return !q.empty() && q.back().second == sb(); }, 2000),
          "result=" + std::to_string(r4));
    set_surface(sink, nullptr);
  }

''' + s[end:]
open(P, 'w', encoding='utf-8').write(s)
print('ok live test')

Q = 'C:/work/gnlink-wt/apk-decoder-liveness/apps/native_poc/src/android_decoder_settled_sink.hpp'
t = open(Q, encoding='utf-8').read()
a = '''  void SetSurface(JNIEnv* env, jobject surface) {
    Base::SetSurface(env, surface);
    Settle();
  }'''
assert t.count(a) == 1
t = t.replace(a, '''  int64_t SetSurface(JNIEnv* env, jobject surface) {
    const int64_t r = Base::SetSurface(env, surface);
    Settle();
    return r;
  }''')
open(Q, 'w', encoding='utf-8').write(t)
print('ok settled')
