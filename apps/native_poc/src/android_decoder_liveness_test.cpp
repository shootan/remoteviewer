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
//   remote60_android_decoder_liveness_test [--hold-ms N]      (default 8000)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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

  std::puts("\n--- [3] DETACH while the codec is being created: the UI waits (D1 a), RX and control do not");
  {
    AndroidVideoDecoderSink sink;
    sink.SetSurface(env, sa());
    hold(fake_ndk::kHoldCreate);
    LetGoAfter safety(gHoldMs + 4000);
    sink.OnEncodedH264Frame(frame(0, true, 0x84));
    const bool blocked = wait_until(held_now, 2000);
    Callers c(sink, 0, 0x85, 0x03);
    std::atomic<int64_t> detachUs{-1};
    std::thread ui([&] {
      const auto t0 = Clock::now();
      sink.SetSurface(env, nullptr);
      detachUs.store(us_since(t0));
    });
    c.start();
    const int waitMs = std::min(gHoldMs, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
    const bool uiStillWaiting = detachUs.load() < 0;
    let_go();
    ui.join();
    c.finish();
    const Snapshot s = snap();
    check("[3] the create was really held", blocked);
    check("[3] the UI's detach waited for the codec to let go (the app needs the surface free)", uiStillWaiting,
          "detach took " + std::to_string(detachUs.load() / 1000) + " ms");
    check("[3] meanwhile RX and control returned within the bound", c.rx.v < kCallBoundUs && c.control.v < kCallBoundUs,
          "rx " + c.rx.str() + " control " + c.control.str());
    check("[3] after the detach no codec is alive", s.started - s.deleted <= 0 && s.created == s.deleted,
          "created=" + std::to_string(s.created) + " started=" + std::to_string(s.started) +
              " deleted=" + std::to_string(s.deleted));
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
    check("[6] the IDR that was dropped is owed again, and a key is asked for", owed != 0 && asked);
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
    const size_t q0 = snap().queued;
    sink.OnEncodedH264Frame(frame(0, true, 0x8D));
    check("[7] the next IDR makes a codec and is queued",
          wait_until([&] { return !queued_from(q0).empty() && queued_from(q0).front().first == 0x8D; }, 2000));
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

  std::printf("\nandroid_decoder_liveness_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
