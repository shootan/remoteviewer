// apk-decoder-liveness r1 (Codex codex_apk_decoder_liveness_design_r1.md (3), contrast 1): the APK's decoder sink
// (android_video_decoder.cpp, the PRODUCT source, unchanged) against the stand-in NDK whose codec can be made SLOW on
// purpose -- create / configure / start / stop held for as long as the test says (fake_ndk::Barrier), longer than
// the control channel's retransmit budget (24 x 250 ms = 6 s). No device, no Host. Tag: pure-logic + threads.
//
// The receive thread (which also carries the UDP control channel), the session's control thread and the UI call
// the sink while the codec is held. The contract: none of them waits for it -- each call returns within a bound;
// the codec is touched by one thread only (none of those); a codec made for a surface / owner / selection that went
// away meanwhile is never used; the frame inbox is bounded; after the codec comes back the first input is an IDR of
// the right owner on the right surface; a failed codec is reported and the calls go on; teardown under a held codec
// ends and leaks nothing.
//
// NEGATIVE: built against the sink of 9f9a91c (REMOTE60_LIVENESS_SINK_DIR, the codec created under the one lock the
// callers take), the bounds FAIL -- the receive thread is held for the whole hold. See report_decoder_liveness_r1.md.
//
// r4 (Codex codex_apk_decoder_liveness_src_r1.md): [9]-[11] the gate decision against the actual queue (N1: the
// input dequeue / queue held while RX or a new pick moves on; [9] is Codex's review schedule), [12] the inbox byte
// cap for every frame (N2), [13] a window that could not be had (D1). NEGATIVE: the 7a79b09 sink + gate.
// r5 (Codex src r4 N3): [14] a failed create then deltas -- one shared budget (no delta tries before the deadline), then
// terminal (no try at all) until a new surface. NEGATIVE: the 0a108b3 sink (41 creates in 2 s).
//
//   remote60_android_decoder_liveness_test [--hold-ms N]      (default 8000)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "android_video_decoder.hpp"
#include "fake_ndk.hpp"
#include "poc_protocol.hpp"

using remote60::android_direct::AndroidVideoDecoderSink;
using namespace remote60::native_poc;
using Clock = std::chrono::steady_clock;

namespace {
int gFailures = 0, gChecks = 0;
void check(const std::string& name, bool cond, const std::string& detail = {}) {
  ++gChecks;
  std::printf("%s  %s%s%s\n", cond ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
  if (!cond) ++gFailures;
}

int gHoldMs = 8000;
constexpr int64_t kCallBoundUs = 50000;  // a caller may be delayed by the sink's short state lock, never by a codec

int64_t us_since(Clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
}

// An Annex-B access unit: SPS + PPS + an IDR slice, or one non-IDR slice. `tag` marks it in the codec's input.
UdpH264AssembledFrame frame(uint64_t gen, bool key, uint8_t tag, size_t pad = 0) {
  UdpH264AssembledFrame f;
  f.header.width = 1280;
  f.header.height = 720;
  f.header.flags = key ? 0x1u : 0u;
  f.header.streamGeneration = gen;
  // apk-inbox C: the AU seq increasing, as the session's assembler delivers it (decoder_chain_inbox.hpp ORDER). Before
  // C it was left 0 -- to an order-aware inbox every frame would be the same AU again.
  static uint32_t fixtureSeq = 0;
  f.header.seq = ++fixtureSeq;
  static std::atomic<uint64_t> capture{1000000};
  f.header.captureQpcUs = capture.fetch_add(33333);
  const uint8_t sc[] = {0, 0, 0, 1};
  auto put = [&](std::initializer_list<uint8_t> nal) {
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.insert(f.payload.end(), nal.begin(), nal.end());
  };
  if (key) {
    put({0x67, 0x42, 0x00, 0x28, 0xAB});
    put({0x68, 0xCE, 0x3C, 0x80});
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.push_back(0x65);
  } else {
    f.payload.insert(f.payload.end(), sc, sc + 4);
    f.payload.push_back(0x41);
  }
  f.payload.insert(f.payload.end(), pad, 0x55);
  f.payload.push_back(tag);
  f.payload.push_back(tag);
  return f;
}

struct Snapshot {
  size_t queued = 0;
  int created = 0, started = 0, deleted = 0, acquired = 0, released = 0;
};
Snapshot snap() {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  const auto& c = fake_ndk::controls();
  return {c.queued.size(), c.codecsCreated, c.codecsStarted, c.codecsDeleted, c.windowsAcquired, c.windowsReleased};
}
// The tag of each payload queued from index `from`, with the window of the codec it went into.
std::vector<std::pair<uint8_t, const void*>> queued_from(size_t from) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  const auto& c = fake_ndk::controls();
  std::vector<std::pair<uint8_t, const void*>> out;
  for (size_t i = from; i < c.queued.size(); ++i) {
    const auto& p = c.queued[i];
    out.emplace_back(p.empty() ? 0 : p.back(), i < c.queuedWindow.size() ? c.queuedWindow[i] : nullptr);
  }
  return out;
}
bool is_key_payload_tag(uint8_t tag) { return (tag & 0x80u) != 0; }  // the tests give IDRs tags >= 0x80

template <class F>
bool wait_until(F&& f, int ms) {
  const auto end = Clock::now() + std::chrono::milliseconds(ms);
  while (Clock::now() < end) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return f();
}

void hold(uint32_t which) {
  fake_ndk::barrier().which.store(which);
  fake_ndk::barrier().held.store(true);
}
void let_go() { fake_ndk::barrier().held.store(false); }
bool held_now() { return fake_ndk::barrier().waiting.load() > 0; }

// Lets go after `ms` on a thread of its own -- whatever a blocked caller does (the old sink blocks the test's own
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
};

struct MaxUs {
  std::atomic<int64_t> v{0};
  std::atomic<int64_t> n{0};
  void add(int64_t us) {
    n.fetch_add(1);
    int64_t cur = v.load();
    while (us > cur && !v.compare_exchange_weak(cur, us)) {
    }
  }
  std::string str() const { return "max=" + std::to_string(v.load() / 1000) + "ms n=" + std::to_string(n.load()); }
};
template <class F>
auto timed(MaxUs& m, F&& f) {
  const auto t0 = Clock::now();
  struct Done {
    MaxUs& m;
    Clock::time_point t0;
    ~Done() { m.add(us_since(t0)); }
  } done{m, t0};
  return f();
}

ControlWindowSelectedMessage answer(uint64_t gen) {
  ControlWindowSelectedMessage m{};
  m.flags = 0x1u;
  m.streamGeneration = gen;
  return m;
}

int surfaceA = 1, surfaceB = 2;
jobject sa() { return &surfaceA; }
jobject sb() { return &surfaceB; }
JNIEnv* env = nullptr;

// The three callers while the codec is held: RX (frames at 30 fps, an IDR every second, then the key request the
// session asks right after each frame), control (the session's hooks every 20 ms), UI (the queries every 50 ms).
struct Callers {
  AndroidVideoDecoderSink& sink;
  uint64_t gen;
  uint8_t keyTag, deltaTag;
  std::atomic<bool> stop{false};
  MaxUs rx, control, ui;
  std::thread::id rxId, controlId, uiId;
  std::thread trx, tcontrol, tui;
  Callers(AndroidVideoDecoderSink& s, uint64_t g, uint8_t k, uint8_t d) : sink(s), gen(g), keyTag(k), deltaTag(d) {}
  void start() {
    trx = std::thread([this] {
      rxId = std::this_thread::get_id();
      for (int i = 0; !stop.load(); ++i) {
        const bool key = (i % 30) == 0;
        timed(rx, [&] { sink.OnEncodedH264Frame(frame(gen, key, key ? keyTag : deltaTag)); });
        timed(rx, [&] { return sink.ConsumeDecoderKeyframeRequest(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
      }
    });
    tcontrol = std::thread([this] {
      controlId = std::this_thread::get_id();
      while (!stop.load()) {
        ClientPresentationStats p{};
        ClientDecodeStats d{};
        timed(control, [&] { return sink.KeyframeOwedFor(); });
        timed(control, [&] { return sink.CurrentSelectionTag(); });
        timed(control, [&] { return sink.ConsumeDecoderKeyframeRequest(); });
        timed(control, [&] { return sink.DrainPresentationStats(&p); });
        timed(control, [&] { return sink.DrainDecodeStats(&d); });
        timed(control, [&] { return sink.DecodeGeneration(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    });
    tui = std::thread([this] {
      uiId = std::this_thread::get_id();
      while (!stop.load()) {
        timed(ui, [&] { return sink.DebugStatus(); });
        timed(ui, [&] { return sink.VideoSizePacked(); });
        timed(ui, [&] { return sink.ReadySelectionGeneration(); });
        timed(ui, [&] { return sink.LastOutputPresentationUs(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
  }
  void finish() {
    stop.store(true);
    if (trx.joinable()) trx.join();
    if (tcontrol.joinable()) tcontrol.join();
    if (tui.joinable()) tui.join();
  }
  ~Callers() { finish(); }
};

// r3 D1: the same calls against the sink of 9f9a91c (void SetSurface, no released seq) -- for the negative runs.
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

uint64_t field(const std::string& status, const char* key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = status.find(k);
  if (at == std::string::npos) return ~0ull;
  return std::strtoull(status.c_str() + at + k.size(), nullptr, 10);
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--hold-ms") == 0) gHoldMs = std::atoi(argv[i + 1]);
  }
  std::printf("android_decoder_liveness_test  hold=%d ms  bound=%lld ms\n", gHoldMs,
              static_cast<long long>(kCallBoundUs / 1000));

  std::puts("\n--- [0] healthy: no hold -- the baseline of the same calls");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    Callers c(sink, 0, 0x80, 0x01);
    c.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    c.finish();
    check("[0] baseline: RX / control / UI calls all return within the bound",
          c.rx.v < kCallBoundUs && c.control.v < kCallBoundUs && c.ui.v < kCallBoundUs,
          "rx " + c.rx.str() + " control " + c.control.str() + " ui " + c.ui.str());
    check("[0] baseline: frames reach the codec", wait_until([] { return snap().queued > 0; }, 1000));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [1] the codec's CREATE held longer than the retransmit budget, while RX / control / UI go on");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    const Snapshot s0 = snap();
    Callers c(sink, 0, 0x81, 0x02);
    c.start();
    const bool blocked = wait_until(held_now, 2000);
    const auto heldAt = Clock::now();
    uint64_t inboxMax = ~0ull;  // never read (an old sink reports no inbox)
    while (us_since(heldAt) < static_cast<int64_t>(gHoldMs) * 1000) {
      const uint64_t inbox = field(sink.DebugStatus(), "inbox");
      if (inbox != ~0ull && (inboxMax == ~0ull || inbox > inboxMax)) inboxMax = inbox;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const int64_t heldUs = us_since(heldAt);
    const size_t queuedAtRelease = snap().queued;
    let_go();
    const bool fresh = wait_until([&] { return snap().queued > queuedAtRelease; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));  // the next periodic IDR too
    c.finish();
    check("[1] the create was really held, for at least the hold", blocked && heldUs >= static_cast<int64_t>(gHoldMs) * 1000,
          "held " + std::to_string(heldUs / 1000) + " ms");
    check("[1] RX: every frame hand-over and key-request call returned within the bound", c.rx.v < kCallBoundUs, c.rx.str());
    check("[1] control: every hook returned within the bound", c.control.v < kCallBoundUs, c.control.str());
    check("[1] UI: every query returned within the bound", c.ui.v < kCallBoundUs, c.ui.str());
    check("[1] the frame inbox stayed bounded (<= 8) while the codec was held", inboxMax != ~0ull && inboxMax <= 8,
          "inbox max=" + std::to_string(inboxMax));
    const auto after = queued_from(queuedAtRelease);
    check("[1] after the hold: frames reach the codec again", fresh, "queued after=" + std::to_string(after.size()));
    check("[1] ...and the codec's FIRST input is an IDR, into the codec on surface A",
          !after.empty() && is_key_payload_tag(after.front().first) && after.front().second == sa(),
          after.empty() ? "none" : "tag=" + std::to_string(after.front().first));
    std::set<std::thread::id> threads;
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      threads = fake_ndk::controls().codecThreads;
    }
    const bool callerTouched = threads.count(c.rxId) || threads.count(c.controlId) || threads.count(c.uiId) ||
                               threads.count(std::this_thread::get_id());
    check("[1] no RX / control / UI / test thread ever called the codec", !callerTouched,
          "codec threads=" + std::to_string(threads.size()));
    (void)s0;
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [2] the surface REPLACED while its codec is being created: the old one's codec is never used");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    const size_t q0 = snap().queued;
    MaxUs rx, uiSwap;
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(0, true, 0x82)); });  // csd + IDR: the worker starts a codec for A
    const bool blocked = wait_until(held_now, 2000);
    timed(uiSwap, [&] { sink.SetSurface(env, sb()); });  // the UI moves to B meanwhile
    let_go();
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(0, true, 0x83)); });
    wait_until([&] { return snap().queued > q0; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto after = queued_from(q0);
    bool anyOnA = false;
    for (const auto& q : after) anyOnA = anyOnA || q.second == sa();
    check("[2] the create for A was really held", blocked);
    check("[2] the UI's move to B returned within the bound (it does not wait for A's codec)", uiSwap.v < kCallBoundUs,
          uiSwap.str());
    check("[2] RX returned within the bound", rx.v < kCallBoundUs, rx.str());
    check("[2] nothing was fed to a codec on surface A after the move; frames go to B",
          !after.empty() && !anyOnA && after.back().second == sb(), "queued=" + std::to_string(after.size()));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [3] DETACH while the codec is being created (r3 D1): bounded for the UI, none for RX / control");
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
    // (the held create itself finishes and is discarded as void -- made, never fed, deleted)
    check("[3] the late completion connects nothing: no codec alive, nothing fed, nothing configured on B",
          afterLate.queued == beforeLetGo.queued && afterLate.created == afterLate.deleted && configuredAfterLate != sb(),
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

  std::puts("\n--- [4] the OWNER changes while its codec is being created: the old owner's frames never reach a codec");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.Bind(11);
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    const size_t q0 = snap().queued;
    sink.OnEncodedH264FrameAs(11, frame(0, true, 0x86));  // PC 11's IDR: its codec is being made
    const bool blocked = wait_until(held_now, 2000);
    MaxUs bind;
    timed(bind, [&] { sink.Bind(12); });                 // PC 12 is put on screen
    sink.OnEncodedH264FrameAs(11, frame(0, false, 0x04));  // 11's late delta
    let_go();
    sink.OnEncodedH264FrameAs(12, frame(0, true, 0x87));
    sink.OnEncodedH264FrameAs(12, frame(0, false, 0x05));
    wait_until([&] { return snap().queued >= q0 + 2; }, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto after = queued_from(q0);
    bool any11 = false;
    for (const auto& q : after) any11 = any11 || q.first == 0x86 || q.first == 0x04;
    check("[4] the create was really held", blocked);
    check("[4] Bind returned within the bound", bind.v < kCallBoundUs, bind.str());
    check("[4] nothing of PC 11 reached a codec; PC 12's IDR then delta did",
          !any11 && after.size() >= 2 && after[0].first == 0x87 && after[1].first == 0x05,
          "queued=" + std::to_string(after.size()));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [5] a NEWER selection while the old one's codec is being created: never 'ready' on an old picture");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(9);
    sink.OnWindowSelectionControlResultFor(answer(30), 9);
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(30, true, 0x88));
    const bool blocked = wait_until(held_now, 2000);
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::Controls::Output o;  // a picture the old codec would hand out
      o.presentationTimeUs = 1;
      fake_ndk::controls().outputs.push_back(o);
    }
    MaxUs prepare;
    timed(prepare, [&] { sink.PrepareForWindowSelection(10); });  // the user picks another window meanwhile
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check("[5] the create was really held", blocked);
    check("[5] the newer Prepare returned within the bound", prepare.v < kCallBoundUs, prepare.str());
    check("[5] the newer selection is not made ready by the old selection's codec / picture",
          sink.ReadySelectionGeneration() != 10, "ready=" + std::to_string(sink.ReadySelectionGeneration()));
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().outputs.clear();
    }
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [6] the inbox SATURATES while the codec is held: bounded, the lost IDR owed and asked for");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(41);
    sink.OnWindowSelectionControlResultFor(answer(40), 41);
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(40, true, 0x89));  // starts the (held) codec
    const bool blocked = wait_until(held_now, 2000);
    MaxUs rx;
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(40, true, 0x8A, 1500000)); });  // a big IDR, then a flood
    for (int i = 0; i < 200; ++i) timed(rx, [&] { sink.OnEncodedH264Frame(frame(40, false, 0x06, 20000)); });
    const std::string st = sink.DebugStatus();
    const uint64_t inbox = field(st, "inbox"), drops = field(st, "inboxDrops");
    const uint64_t owed = sink.KeyframeOwedFor();
    const bool asked = sink.ConsumeDecoderKeyframeRequest();
    const size_t qBefore = snap().queued;
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sink.OnEncodedH264Frame(frame(40, true, 0x8B));
    sink.OnEncodedH264Frame(frame(40, false, 0x07));
    wait_until([&] { return !queued_from(qBefore).empty() && queued_from(qBefore).back().first == 0x07; }, 3000);
    const auto after = queued_from(qBefore);
    bool flood = false;
    for (const auto& q : after) flood = flood || q.first == 0x06;
    check("[6] the create was really held", blocked);
    check("[6] RX never waited while flooding", rx.v < kCallBoundUs, rx.str());
    check("[6] the inbox held at most 8 frames; the rest were dropped and counted",
          inbox != ~0ull && inbox <= 8 && drops != ~0ull && drops >= 190,
          "inbox=" + std::to_string(inbox) + " drops=" + std::to_string(drops));
    // apk-inbox C (design r3 §1, replacing r1's drop-the-oldest): the inbox keeps the IDR (0x8A) with the suffix that
    // fits and cuts the newest -- no IDR is lost, so nothing is owed. The loss is at the TAIL (after that suffix): to go
    // on past it the decoder needs an IDR newer than the cut, so the one episode asks once. (r1-r5 here: the IDR was the
    // oldest frame, dropped, owed and asked for.)
    check("[6] C: the IDR is KEPT (nothing owed); the tail loss is ONE episode with ONE ask (for an IDR after the cut)",
          owed == 0 && asked && field(st, "episodes") == 1 && field(st, "tailGap") == 1,
          "owed=" + std::to_string(owed) + " asked=" + std::to_string(asked ? 1 : 0) + " episodes=" +
              std::to_string(field(st, "episodes")) + " tailGap=" + std::to_string(field(st, "tailGap")));
    check("[6] after the hold no delta of the gap is fed before the next IDR; that IDR pays the debt",
          !flood && !after.empty() && sink.KeyframeOwedFor() == 0, "queued=" + std::to_string(after.size()));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [7] the codec FAILS to be created: reported, the calls go on, then it comes back");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().createFails = true;
    }
    MaxUs rx;
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(0, true, 0x8C)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const std::string st = sink.DebugStatus();
    check("[7] a failed create is reported (codecState=failed, codecFail >= 1)",
          st.find(" codecState=failed") != std::string::npos && field(st, "codecFail") >= 1, st.substr(st.find(" codec=")));
    check("[7] RX returned within the bound", rx.v < kCallBoundUs, rx.str());
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().createFails = false;
    }
    // r5: after a failure nothing tries before the shared deadline (1 s) -- then the worker's own try makes the
    // codec, and the next IDR goes in
    const bool made = wait_until([&] { return sink.DebugStatus().find(" codec=on") != std::string::npos; }, 2500);
    const size_t q0 = snap().queued;
    sink.OnEncodedH264Frame(frame(0, true, 0x8D));
    check("[7] the codec is made again by the retry (<= 2.5 s), and the next IDR is queued",
          made && wait_until([&] { return !queued_from(q0).empty() && queued_from(q0).front().first == 0x8D; }, 2000));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [8] TEARDOWN while the codec is being created: it ends once the codec lets go, nothing leaks");
  {
    const Snapshot s0 = snap();
    auto sink = std::make_unique<AndroidVideoDecoderSink>();
    sink->SetSurface(env, sa());
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink->OnEncodedH264Frame(frame(0, true, 0x8E));
    const bool blocked = wait_until(held_now, 2000);
    std::atomic<bool> gone{false};
    std::thread t([&] {
      sink.reset();
      gone.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const bool waitedForCodec = !gone.load();
    let_go();
    const bool ended = wait_until([&] { return gone.load(); }, 3000);
    t.join();
    const Snapshot s1 = snap();
    check("[8] the create was really held", blocked);
    check("[8] teardown waits for the held codec, then ends", waitedForCodec && ended);
    check("[8] every codec made was deleted; every window taken was let go",
          s1.created - s0.created == s1.deleted - s0.deleted && s1.acquired - s0.acquired == s1.released - s0.released,
          "created=" + std::to_string(s1.created - s0.created) + " deleted=" + std::to_string(s1.deleted - s0.deleted) +
              " windows " + std::to_string(s1.acquired - s0.acquired) + "/" + std::to_string(s1.released - s0.released));
  }

  // ---- r4 (Codex codex_apk_decoder_liveness_src_r1.md): N1 the gate decision and the actual queue, N2 the byte cap,
  // D1 a window that could not be had. The worker calls the codec with its lock released; another thread runs while
  // it is inside dequeueInputBuffer (kHoldDequeueInput, the bit Codex's review probe used) or queueInputBuffer.
  // Obsolete = a frame in the codec whose generation is not the one the selection ended bound / answered to.
  auto tagGen = [](uint8_t tag) -> uint64_t {
    switch (tag) {
      case 0xA1: return 101;
      case 0xB2: return 202;
      default: return 0;
    }
  };

  std::puts("\n--- [9] N1 (Codex's schedule): candidate 101's dequeue held, RX brings 202 -- one generation in the codec");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(42);
    sink.BeginLocalSelectionFor(42);
    const size_t q0 = snap().queued;
    hold(fake_ndk::kHoldDequeueInput);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(101, true, 0xA1));
    const bool blocked = wait_until(held_now, 2000);
    MaxUs rx;
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(202, true, 0xB2)); });  // admitted on RX while 101 is in the dequeue
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    let_go();
    wait_until([&] { return !queued_from(q0).empty(); }, 2000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // whatever else would go in, goes in
    const auto after = queued_from(q0);
    const uint64_t bound = sink.LocalSelectionBoundFor(42);
    std::string tags;
    bool obsolete = false, b2 = false;
    for (const auto& q : after) {
      char t[8];
      std::snprintf(t, sizeof(t), "%02X ", q.first);
      tags += t;
      obsolete = obsolete || tagGen(q.first) != bound;
      b2 = b2 || q.first == 0xB2;
    }
    check("[9] the dequeue of 101 was really held while 202 was handed over", blocked);
    check("[9] RX returned within the bound", rx.v < kCallBoundUs, rx.str());
    check("[9] the selection is bound, and NOTHING of another generation went into the codec",
          bound != 0 && !after.empty() && !obsolete, "bound=" + std::to_string(bound) + " queued=" + tags);
    check("[9] first IDR into the codec binds (101); 202, refused at its own queue, never went in",
          bound == 101 && !b2, "bound=" + std::to_string(bound) + " queued=" + tags);
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [9b] N1 mirror: 101 waits for an input buffer (held in the slot), 202 replaces it -- 202 binds");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(43);
    sink.BeginLocalSelectionFor(43);
    const size_t q0 = snap().queued;
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().inputAvailable = false;
    }
    sink.OnEncodedH264Frame(frame(101, true, 0xA1));
    const bool pending = wait_until([&] { return field(sink.DebugStatus(), "pending") == 1; }, 2000);
    sink.OnEncodedH264Frame(frame(202, true, 0xB2));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().inputAvailable = true;
    }
    wait_until([&] { return !queued_from(q0).empty(); }, 2000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto after = queued_from(q0);
    const uint64_t bound = sink.LocalSelectionBoundFor(43);
    check("[9b] 101 was held for an input buffer", pending);
    // (the same IDR may go in again: the decoder's bootstrap replay of the selection's IDR while no picture came)
    bool onlyB2 = !after.empty();
    std::string tags;
    for (const auto& q : after) {
      onlyB2 = onlyB2 && q.first == 0xB2;
      char t[8];
      std::snprintf(t, sizeof(t), "%02X ", q.first);
      tags += t;
    }
    check("[9b] only 202 went in, and it binds", bound == 202 && onlyB2,
          "bound=" + std::to_string(bound) + " queued=" + tags);
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [10] N1: a NEW pick while the old selection's frame is in the dequeue -- that frame never goes in");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(50);
    sink.OnWindowSelectionControlResultFor(answer(60), 50);
    sink.OnEncodedH264Frame(frame(60, true, 0xC0));  // a codec for the answered selection, its IDR in
    wait_until([&] { return snap().started > 0 && field(sink.DebugStatus(), "in") >= 1; }, 2000);
    const Snapshot s0 = snap();
    const size_t q0 = s0.queued;
    hold(fake_ndk::kHoldDequeueInput);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(60, false, 0x31));  // the old selection's delta: in the dequeue
    const bool blocked = wait_until(held_now, 2000);
    MaxUs prep;
    timed(prep, [&] { sink.PrepareForWindowSelection(51); });  // the user picks another window meanwhile
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto during = queued_from(q0);
    sink.OnWindowSelectionControlResultFor(answer(61), 51);
    sink.OnEncodedH264Frame(frame(61, true, 0xC1));
    wait_until([&] { return !queued_from(q0).empty() && queued_from(q0).back().first == 0xC1; }, 2000);
    const auto after = queued_from(q0);
    const Snapshot s1 = snap();
    check("[10] the dequeue was really held when the new pick came", blocked);
    check("[10] the new Prepare returned within the bound", prep.v < kCallBoundUs, prep.str());
    check("[10] the old selection's frame was NOT queued after the pick", during.empty(),
          "queued meanwhile=" + std::to_string(during.size()));
    check("[10] the new selection's IDR is the next input, into a NEW codec",
          after.size() == 1 && after[0].first == 0xC1 && s1.created > s0.created,
          "queued=" + std::to_string(after.size()) + " codecs made " + std::to_string(s1.created - s0.created));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [11] N1: a NEW pick while the old frame's QUEUE call runs -- it went into a voided codec only");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(70);
    sink.OnWindowSelectionControlResultFor(answer(80), 70);
    const Snapshot s0 = snap();
    hold(fake_ndk::kHoldQueueInput);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(80, true, 0xC2));
    const bool blocked = wait_until(held_now, 2000);
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::Controls::Output o;  // a picture that codec would hand out
      o.presentationTimeUs = 1;
      fake_ndk::controls().outputs.push_back(o);
    }
    sink.PrepareForWindowSelection(71);
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const Snapshot s1 = snap();
    const uint64_t readyAfter = sink.ReadySelectionGeneration();
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().outputs.clear();
    }
    const size_t q1 = s1.queued;
    sink.OnWindowSelectionControlResultFor(answer(81), 71);
    sink.OnEncodedH264Frame(frame(81, true, 0xC3));
    wait_until([&] { return !queued_from(q1).empty(); }, 2000);
    const Snapshot s2 = snap();
    check("[11] the queue call was really held when the new pick came", blocked);
    check("[11] the started exchange completed into the old codec, which was then deleted (voided)",
          s1.queued == s0.queued + 1 && s1.deleted > s0.deleted,
          "queued+" + std::to_string(s1.queued - s0.queued) + " deleted+" + std::to_string(s1.deleted - s0.deleted));
    check("[11] the new selection is not made ready by it (no picture of that codec counts)", readyAfter != 71,
          "ready=" + std::to_string(readyAfter));
    check("[11] the new selection's IDR goes into a codec made after it",
          s2.queued == q1 + 1 && s2.created > s1.created,
          "codecs made " + std::to_string(s2.created - s0.created));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [12] N2: the inbox's byte cap holds for every frame -- one over the cap is refused and owed");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(90);
    sink.OnWindowSelectionControlResultFor(answer(91), 90);
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(91, true, 0xD0));  // its codec is being made (held): nothing leaves the inbox
    const bool blocked = wait_until(held_now, 2000);
    MaxUs rx;
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(91, true, 0xD1, 5u << 20)); });  // 5 MB > the 4 MB cap
    const std::string st1 = sink.DebugStatus();
    const uint64_t owedAfterBig = sink.KeyframeOwedFor();
    const bool askedAfterBig = sink.ConsumeDecoderKeyframeRequest();
    uint64_t bytesMax = 0;
    for (int i = 0; i < 6; ++i) {  // big frames, each within the cap: the total stays within it
      timed(rx, [&] { sink.OnEncodedH264Frame(frame(91, true, static_cast<uint8_t>(0xD2 + i), 1500000)); });
      const uint64_t b = field(sink.DebugStatus(), "inboxBytes");
      bytesMax = std::max<uint64_t>(bytesMax, b);
    }
    // a new pick with its own answer: what was refused / dropped for 90 is not owed by 92
    sink.PrepareForWindowSelection(92);
    const uint64_t owedAtPrepare = sink.KeyframeOwedFor();
    sink.OnWindowSelectionControlResultFor(answer(93), 92);
    const uint64_t owedNew = sink.KeyframeOwedFor();
    let_go();
    const size_t q0 = snap().queued;
    sink.OnEncodedH264Frame(frame(93, true, 0xDF));
    const bool newIn = wait_until([&] { auto q = queued_from(q0); return !q.empty() && q.front().first == 0xDF; }, 2000);
    check("[12] the create was really held", blocked);
    check("[12] RX returned within the bound, the 5 MB frame included", rx.v < kCallBoundUs, rx.str());
    check("[12] the frame over the cap was refused at the door (inboxBytes 0, counted)",
          field(st1, "inboxBytes") == 0 && field(st1, "inboxRefused") == 1,
          "inboxBytes=" + std::to_string(field(st1, "inboxBytes")) + " refused=" + std::to_string(field(st1, "inboxRefused")));
    // apk-inbox C (design r3 §3): the refused IDR is owed again, and the session's owed asks (8 x 1 s) carry it -- that
    // duty IS this IDR, so the sink's recovery episode adds no second ask on top (one logical exchange, not 4 + 8).
    check("[12] ...and the IDR it was is owed again (asked through that duty; the sink adds no second ask)",
          owedAfterBig != 0 && !askedAfterBig,
          "owed=" + std::to_string(owedAfterBig) + " sink ask=" + std::to_string(askedAfterBig ? 1 : 0));
    check("[12] frames within the cap: the inbox never held more than 4 MB", bytesMax != ~0ull && bytesMax <= (4u << 20),
          "max=" + std::to_string(bytesMax));
    check("[12] a new pick owes nothing of the old one's losses; its IDR goes in",
          owedAtPrepare == 0 && owedNew == 0 && newIn,
          "owed at prepare=" + std::to_string(owedAtPrepare) + " after answer=" + std::to_string(owedNew));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [13] D1: an attach whose window cannot be had is refused (0) -- nothing of the current surface changes");
  {
    AndroidVideoDecoderSink sink;
    const int64_t r0 = set_surface(sink, sa());
    sink.OnEncodedH264Frame(frame(0, true, 0xE0));
    wait_until([&] { return !queued_from(0).empty() && queued_from(0).back().first == 0xE0; }, 2000);
    const uint64_t rel0 = released_seq(sink);
    const Snapshot s0 = snap();
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().windowAcquireFails = true;
    }
    const int64_t r1 = set_surface(sink, sb());
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().windowAcquireFails = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const size_t q0 = snap().queued;
    sink.OnEncodedH264Frame(frame(0, false, 0x32));
    const bool stillOnA = wait_until([&] { auto q = queued_from(q0); return !q.empty() && q.back().second == sa(); }, 2000);
    const Snapshot s1 = snap();
    const std::string st = sink.DebugStatus();
    const int64_t r2 = set_surface(sink, sb());
    check("[13] the failed attach answers 0 (refused), not a detach seq", r0 > 0 && r1 == 0, "result=" + std::to_string(r1));
    check("[13] the current surface and its codec go on: the next frame goes into the codec on A, none deleted",
          stillOnA && s1.deleted == s0.deleted && st.find("surface=on") != std::string::npos,
          "deleted+" + std::to_string(s1.deleted - s0.deleted));
    check("[13] no detach was started (released seq unchanged) and the next real attach is taken",
          released_seq(sink) == rel0 && r2 > 0, "next attach=" + std::to_string(r2));
    set_surface(sink, nullptr);
  }

  std::puts("\n--- [14] r5 N3 (Codex src r4's schedule): a failed create, then deltas -- one budget, no delta before the deadline");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    sink.PrepareForWindowSelection(95);
    sink.OnWindowSelectionControlResultFor(answer(303), 95);
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().createFails = true;
    }
    auto failures = [&] { return field(sink.DebugStatus(), "codecFail"); };
    MaxUs rx;
    timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, true, 0xF0)); });  // SPS/PPS + IDR: its create fails
    for (int i = 0; i < 40; ++i) {                                        // then 40 deltas, 50 ms apart (2 s)
      timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, false, 0x33)); });
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const uint64_t in2s = failures();
    // the frames go on (a delta every 100 ms, an IDR every second) until the budget is spent
    const auto t0 = Clock::now();
    bool terminal = false;
    for (int i = 0; us_since(t0) < 30000000 && !terminal; ++i) {
      timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, (i % 10) == 0, (i % 10) == 0 ? 0xF1 : 0x34)); });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      terminal = sink.DebugStatus().find(" codecState=failed_terminal") != std::string::npos;
    }
    const int64_t terminalAfterMs = us_since(t0) / 1000;
    const uint64_t atTerminal = failures();
    // terminal: the create could succeed now, frames keep coming -- still nothing tries
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::controls().createFails = false;
    }
    const int createdBefore = snap().created;
    for (int i = 0; i < 20; ++i) {
      timed(rx, [&] { sink.OnEncodedH264Frame(frame(303, (i % 10) == 0, (i % 10) == 0 ? 0xF2 : 0x35)); });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const uint64_t afterTerminal = failures();
    const bool noTryWhileTerminal = snap().created == createdBefore && afterTerminal == atTerminal &&
                                    sink.DebugStatus().find(" codec=off") != std::string::npos;
    // the explicit way back: a new surface
    const size_t q0 = snap().queued;
    sink.SetSurface(env, sb());
    sink.OnEncodedH264Frame(frame(303, true, 0xF3));
    const bool back = wait_until([&] { auto q = queued_from(q0); return !q.empty() && q.front().first == 0xF3 &&
                                                                    q.front().second == sb(); }, 3000);
    check("[14] RX never waited", rx.v < kCallBoundUs, rx.str());
    check("[14] in the first 2 s (1 IDR + 40 deltas) at most 3 creates were tried (the first + the 1 s deadline)",
          in2s != ~0ull && in2s <= 3, "codecFail=" + std::to_string(in2s));
    check("[14] the budget ends in TERMINAL after exactly 1 + 20 tries (deltas, IDRs and the timer all counted)",
          terminal && atTerminal == 21,
          "codecFail=" + std::to_string(atTerminal) + " after " + std::to_string(terminalAfterMs) + " ms");
    check("[14] terminal means no try at all: frames (IDRs too) make no codec even once one could be made",
          noTryWhileTerminal, "codecFail=" + std::to_string(afterTerminal));
    check("[14] a new surface starts over: its IDR goes into a codec on it", back);
    sink.SetSurface(env, nullptr);
  }

  std::printf("\nandroid_decoder_liveness_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
