// apk-inbox C (design caplife/design_apk_decoder_inbox_r2.md r3; Codex codex_apk_inbox_c_impl_go_r1.md "C 제출과 검증
// 조건 1"): the APK's decoder sink (android_video_decoder.cpp, the PRODUCT source) against the stand-in NDK, on the
// boundaries C adds that android_decoder_inbox_test [1]-[6] does not reach:
//   [C1] order: a duplicate and an older AU are dropped (not fed, not a loss); seq wrap decodes through
//   [C2] an IDR over 4 MiB: refused, a loss -- one episode, one ask; the next IDR recovers it
//   [C3] a candidate IDR lost again (held for a buffer, thrown away with the codec): the SAME episode resumes with its
//        budget; nothing is asked while the candidate was held
//   [C4] t0 / +1 / +3 / +7 s from sends, then the final window, then exhausted -- and a later IDR still decodes; an
//        explicit pick starts over
//   [C5] D8 at the sink: a descriptor after a pick / a binding change / a commit; merged causes partly invalidated;
//        moot while a candidate is held (not an attempt); codec-up / abort causes
//   [C6] the Android binding adapter: a parked / replaced PC asks nothing, and its earlier asks are not valid
//   [C7] a sustained overload (the worker held 4 s at 30 fps, an IDR every 30): frames / bytes bounded, RX never waits,
//        asks bounded -- and once the codec takes input again it starts from the NEWEST IDR (the current picture)
//   [C8] the codec's create held 3 s (slow lifecycle) while frames and losses come: RX never waits, asks bounded
// Each Rx want is told back as SENT at once (as the session does when its limiter takes it). Tag: pure-logic + threads.
//
//   remote60_android_decoder_inbox_c_test

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "android_video_decoder.hpp"
#include "entry_sink_adapter.hpp"
#include "fake_ndk.hpp"
#include "poc_protocol.hpp"

using remote60::android_direct::AndroidVideoDecoderSink;
using remote60::android_direct::EntrySinkAdapter;
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
constexpr int64_t kCallBoundUs = 50000;
int64_t us_since(Clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
}
uint32_t gSeq = 0;
UdpH264AssembledFrame frame_at(uint32_t seq, uint64_t gen, bool key, uint8_t tag, size_t pad = 0) {
  UdpH264AssembledFrame f;
  f.header.width = 1280;
  f.header.height = 720;
  f.header.flags = key ? 0x1u : 0u;
  f.header.streamGeneration = gen;
  f.header.seq = seq;
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
UdpH264AssembledFrame frame(uint64_t gen, bool key, uint8_t tag, size_t pad = 0) {
  return frame_at(++gSeq, gen, key, tag, pad);
}
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
void input_available(bool on) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  fake_ndk::controls().inputAvailable = on;
}
size_t queued() {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  return fake_ndk::controls().queued.size();
}
std::vector<uint8_t> tags_from(size_t from) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  std::vector<uint8_t> out;
  const auto& q = fake_ndk::controls().queued;
  for (size_t i = from; i < q.size(); ++i) out.push_back(q[i].empty() ? 0 : q[i].back());
  return out;
}
uint64_t field(const std::string& status, const char* key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = status.find(k);
  return at == std::string::npos ? ~0ull : std::strtoull(status.c_str() + at + k.size(), nullptr, 10);
}
std::string sfield(const std::string& status, const char* key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = status.find(k);
  if (at == std::string::npos) return "?";
  const size_t from = at + k.size();
  return status.substr(from, status.find(' ', from) - from);
}
ControlWindowSelectedMessage answer(uint64_t gen) {
  ControlWindowSelectedMessage m{};
  m.flags = 0x1u;
  m.streamGeneration = gen;
  return m;
}
JNIEnv* env = nullptr;
int surfaceA = 1;

struct Rx {
  ClientEncodedFrameSink& sink;
  int wants = 0;
  int64_t maxCallUs = 0;
  std::vector<int64_t> wantMs;  // since `t0`
  Clock::time_point t0 = Clock::now();
  bool sendBack = true;
  void send(UdpH264AssembledFrame&& f) {
    const auto c0 = Clock::now();
    sink.OnEncodedH264Frame(std::move(f));
    maxCallUs = std::max(maxCallUs, us_since(c0));
    poll();
  }
  void poll() {
    DecoderKeyRequest r;
    if (sink.TakeDecoderKeyRequest(&r)) {
      ++wants;
      wantMs.push_back(us_since(t0) / 1000);
      if (sendBack) sink.OnDecoderKeyRequestSent(r, 0);
    }
  }
};

uint64_t start(AndroidVideoDecoderSink& sink, Rx& rx, uint64_t sel, uint64_t gen) {
  sink.SetSurface(env, &surfaceA);
  sink.PrepareForWindowSelection(sel);
  sink.OnWindowSelectionControlResultFor(answer(gen), sel);
  const size_t q0 = queued();
  rx.send(frame(gen, true, 0x80));
  wait_until([&] { return queued() > q0; }, 2000);
  for (int i = 0; i < 3; ++i) {
    rx.send(frame(gen, false, 0x01));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 1000);
  rx.wants = 0;
  rx.wantMs.clear();
  return gen;
}

// A loss made deterministically: the worker held in its input dequeue while 9 deltas arrive (8 + 1 > the bound).
void make_loss(AndroidVideoDecoderSink& sink, Rx& rx, uint64_t g) {
  hold(fake_ndk::kHoldDequeueInput);
  rx.send(frame(g, false, 0x20));
  wait_until(held_now, 2000);
  for (int i = 0; i < 9; ++i) rx.send(frame(g, false, 0x21));
  let_go();
  wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 1000);
}
}  // namespace

int main() {
  std::printf("android_decoder_inbox_c_test (apk-inbox C: the boundaries C adds)\n");

  std::puts("\n--- [C1] order: duplicate, older, wrap");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 21, 201);
    const uint32_t s = gSeq;
    const size_t q0 = queued();
    rx.send(frame_at(s + 1, g, false, 0x31));
    rx.send(frame_at(s + 1, g, false, 0x32));  // the same AU again
    rx.send(frame_at(s, g, false, 0x33));      // an older one
    rx.send(frame_at(s + 2, g, false, 0x34));
    gSeq = s + 2;
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 1000);
    const auto t = tags_from(q0);
    const std::string st = sink.DebugStatus();
    check("[C1] duplicate and older AU dropped (not fed), counted, not a loss (no ask)",
          t == std::vector<uint8_t>{0x31, 0x34} && field(st, "chainDup") == 1 && field(st, "chainStale") == 1 &&
              rx.wants == 0 && field(st, "episodes") == 0,
          "fed=" + std::to_string(t.size()) + " dup=" + std::to_string(field(st, "chainDup")) +
              " stale=" + std::to_string(field(st, "chainStale")));
    sink.SetSurface(env, nullptr);
  }
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    gSeq = 0xFFFFFFF0u;
    const uint64_t g = start(sink, rx, 22, 202);
    const size_t q0 = queued();
    for (int i = 0; i < 20; ++i) {  // crosses 0xFFFFFFFF -> 0
      rx.send(frame(g, false, 0x35));
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 1000);
    const std::string st = sink.DebugStatus();
    check("[C1] seq wrap: all 20 fed in order, nothing stale, no ask", tags_from(q0).size() == 20 &&
          field(st, "chainStale") == 0 && rx.wants == 0, "fed=" + std::to_string(tags_from(q0).size()) + " seq now=" +
          std::to_string(gSeq));
    sink.SetSurface(env, nullptr);
    gSeq = 1000;
  }

  std::puts("\n--- [C2] an IDR over 4 MiB");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 23, 203);
    const size_t q0 = queued();
    rx.send(frame(g, true, 0x40, (4u << 20) + 16));
    for (int i = 0; i < 5; ++i) rx.send(frame(g, false, 0x41));
    const std::string st = sink.DebugStatus();
    // The refused IDR is of the answered generation: the selection is owed one again (gate KeyLost, r5 N1) and the
    // session asks for it (8 x 1 s). That duty IS this IDR -- the episode opens but adds no ask of its own (one
    // logical exchange, not 4 + 8).
    check("[C2] refused (a loss): one episode; the IDR is owed again and asked through that duty -- the episode adds no "
          "ask; its deltas refused, nothing fed",
          rx.wants == 0 && sink.KeyframeOwedFor() != 0 && field(st, "episodes") == 1 && field(st, "inboxRefused") == 1 &&
              tags_from(q0).empty(),
          "wants=" + std::to_string(rx.wants) + " owed=" + std::to_string(sink.KeyframeOwedFor()) + " refused=" +
              std::to_string(field(st, "inboxRefused")) + " gapDrops=" + std::to_string(field(st, "gapDrops")));
    rx.send(frame(g, true, 0x42));
    wait_until([&] { return !tags_from(q0).empty(); }, 1000);
    const std::string st2 = sink.DebugStatus();
    check("[C2] the next IDR is fed and ends the episode", !tags_from(q0).empty() && tags_from(q0).front() == 0x42 &&
          sfield(st2, "recovery") == "idle" && field(st2, "recClosed") == 1);
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [C3] a candidate IDR lost again: the same episode resumes");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 24, 204);
    rx.t0 = Clock::now();
    make_loss(sink, rx, g);
    const uint64_t ep = field(sink.DebugStatus(), "episode");
    check("[C3] the loss: one ask at once", rx.wants == 1);
    input_available(false);
    rx.send(frame(g, true, 0x50));  // the candidate: no input buffer -> held in the pending slot
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 1; }, 1000);
    const auto tHeld = Clock::now();
    while (us_since(tHeld) < 1500000) {  // past the +1 s due time
      rx.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check("[C3] nothing asked while the candidate is held (past the +1 s due)", rx.wants == 1,
          "wants=" + std::to_string(rx.wants));
    sink.OnVideoDiscontinuity();  // the codec goes -- and the held IDR with it
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 0; }, 1000);
    const auto tGone = Clock::now();
    while (us_since(tGone) < 1500000) {
      rx.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const std::string st = sink.DebugStatus();
    // The lost candidate was the answered generation's IDR: KeyLost (r5 N1) owes it again -- the session's owed asks
    // carry it. The episode is the SAME one (no new episode, no budget reset) and adds no ask of its own on top; its
    // own resume-with-budget is the pure test's (decoder_chain_inbox_test "lost again").
    check("[C3] candidate gone: the SAME episode stays open (no new one), the IDR is owed again and asked through that "
          "duty -- the episode adds no ask",
          rx.wants == 1 && sink.KeyframeOwedFor() != 0 && field(st, "episode") == ep && field(st, "episodes") == 1 &&
              sfield(st, "recovery") == "open" && field(st, "asks") == 1,
          "wants=" + std::to_string(rx.wants) + " owed=" + std::to_string(sink.KeyframeOwedFor()) + " asks=" +
              std::to_string(field(st, "asks")) + " episodes=" + std::to_string(field(st, "episodes")));
    input_available(true);
    const size_t q0 = queued();
    rx.send(frame(g, true, 0x51));
    wait_until([&] { return !tags_from(q0).empty(); }, 1500);
    check("[C3] the next IDR is fed and closes it", sfield(sink.DebugStatus(), "recovery") == "idle" &&
          !tags_from(q0).empty() && tags_from(q0).front() == 0x51);
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [C4] the schedule, the final window, exhausted -- and still decodable");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 25, 205);
    rx.t0 = Clock::now();
    make_loss(sink, rx, g);
    // no IDR comes: the host's deltas keep arriving (refused by the tail gap)
    bool exhaustedSeen = false;
    int64_t exhaustedMs = -1;
    while (us_since(rx.t0) < 12500000) {
      rx.send(frame(g, false, 0x60));
      if (!exhaustedSeen && sfield(sink.DebugStatus(), "recovery") == "exhausted") {
        exhaustedSeen = true;
        exhaustedMs = us_since(rx.t0) / 1000;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::string times;
    for (int64_t m : rx.wantMs) times += (times.empty() ? "" : ",") + std::to_string(m);
    auto close_to = [&](size_t i, int64_t ms) { return i < rx.wantMs.size() && std::llabs(rx.wantMs[i] - ms) <= 250; };
    check("[C4] exactly 4 asks at ~0 / 1 / 3 / 7 s (from the sends), none after", rx.wants == 4 && close_to(0, 0) &&
          close_to(1, 1000) && close_to(2, 3000) && close_to(3, 7000), "ask ms=" + times);
    check("[C4] exhausted after the 4 s final window (~11 s), not before", exhaustedSeen && exhaustedMs >= 10800 &&
          exhaustedMs <= 11600, "exhausted at " + std::to_string(exhaustedMs) + " ms");
    const size_t q0 = queued();
    rx.send(frame(g, true, 0x61));  // the host's periodic IDR
    for (int i = 0; i < 3; ++i) rx.send(frame(g, false, 0x62));
    wait_until([&] { return tags_from(q0).size() >= 4; }, 1500);
    const std::string st = sink.DebugStatus();
    check("[C4] exhausted blocks nothing: the next IDR + its deltas are fed, the state ends (idle)",
          tags_from(q0).size() == 4 && tags_from(q0).front() == 0x61 && sfield(st, "recovery") == "idle" &&
              field(st, "recExhausted") == 1, "fed=" + std::to_string(tags_from(q0).size()));
    // the explicit path: a new pick, then a new loss -> a new episode asks again
    const uint64_t g2 = start(sink, rx, 26, 206);
    rx.t0 = Clock::now();
    make_loss(sink, rx, g2);
    check("[C4] after a new pick a new loss is a new episode with its own asks", rx.wants == 1 &&
          sfield(sink.DebugStatus(), "recovery") == "open");
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [C5] D8 at the sink: which causes still hold");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    rx.sendBack = false;
    const uint64_t g = start(sink, rx, 27, 207);
    make_loss(sink, rx, g);
    DecoderKeyRequest rec;
    // the want was taken by make_loss's Rx; take a fresh descriptor for this episode
    rec.causes = kKeyCauseRecovery;
    rec.sinkEpoch = field(sink.DebugStatus(), "keyEpoch");
    rec.episodeId = field(sink.DebugStatus(), "episode");
    check("[C5] a recovery ask of the open episode, no candidate: valid", sink.DecoderKeyRequestValidCauses(rec) ==
          kKeyCauseRecovery);
    // a candidate arrives (held for a buffer): the same ask is moot now -- not an attempt
    input_available(false);
    rx.send(frame(g, true, 0x70));
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 1; }, 1000);
    check("[C5] a usable IDR preserved: the recovery cause is moot (dropped, counted)",
          sink.DecoderKeyRequestValidCauses(rec) == 0 && field(sink.DebugStatus(), "recMoot") == 1,
          "state: pending=" + sfield(sink.DebugStatus(), "pending") + " recovery=" + sfield(sink.DebugStatus(), "recovery") + " inbox=" + sfield(sink.DebugStatus(), "inbox") + " in=" + sfield(sink.DebugStatus(), "in") + " episode=" + sfield(sink.DebugStatus(), "episode") + " codecState=" + sfield(sink.DebugStatus(), "codecState") + " awaitsKey=" + sfield(sink.DebugStatus(), "awaitsKey"));
    input_available(true);
    wait_until([&] { return sfield(sink.DebugStatus(), "recovery") == "idle"; }, 1000);
    check("[C5] the candidate committed: the episode's old ask is not valid", sink.DecoderKeyRequestValidCauses(rec) == 0);
    // merged causes, partly invalidated
    make_loss(sink, rx, g);
    DecoderKeyRequest merged;
    merged.causes = kKeyCauseRecovery | kKeyCauseOwed | kKeyCausePipeline;
    merged.sinkEpoch = field(sink.DebugStatus(), "keyEpoch");
    merged.episodeId = field(sink.DebugStatus(), "episode");
    merged.owedToken = 999999;  // not the gate's token (nothing owed)
    check("[C5] recovery + a stale owed token + pipeline: recovery and pipeline stay, the stale owed goes",
          sink.DecoderKeyRequestValidCauses(merged) == (kKeyCauseRecovery | kKeyCausePipeline));
    sink.PrepareForWindowSelection(28);  // a new pick
    check("[C5] after a new pick: the sink-scoped causes go, the pipeline cause stays",
          sink.DecoderKeyRequestValidCauses(merged) == kKeyCausePipeline &&
              sink.DecoderKeyRequestValidCauses(rec) == 0);
    sink.OnDecoderKeyRequestSent(merged, 0);  // a late send of the old ask
    check("[C5] a late send of the old episode's ask starts nothing in the new state",
          sfield(sink.DebugStatus(), "recovery") == "idle" && field(sink.DebugStatus(), "asks") == 0);
    // abort: its own ask, valid until the next change
    sink.OnWindowSelectionControlResultFor(answer(208), 28);
    sink.AbortWindowSelection();
    DecoderKeyRequest ab;
    check("[C5] an abort hands out its own ask (selection-abort cause)", sink.TakeDecoderKeyRequest(&ab) &&
          ab.causes == kKeyCauseSelectionAbort && sink.DecoderKeyRequestValidCauses(ab) == kKeyCauseSelectionAbort);
    sink.PrepareForWindowSelection(29);
    check("[C5] ... and is not valid after the next pick", sink.DecoderKeyRequestValidCauses(ab) == 0);
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [C6] the binding adapter: owner / park / replacement");
  {
    AndroidVideoDecoderSink sink;
    EntrySinkAdapter a(&sink), b(&sink);
    a.SetToken(101);
    b.SetToken(202);
    sink.Bind(101);
    Rx rx{a};
    rx.sendBack = false;
    sink.SetSurface(env, &surfaceA);
    sink.PrepareForWindowSelectionAs(101, 30);
    sink.OnWindowSelectionControlResultForAs(101, answer(301), 30);
    const size_t q0 = queued();
    rx.send(frame(301, true, 0x80));
    wait_until([&] { return queued() > q0; }, 2000);
    hold(fake_ndk::kHoldDequeueInput);
    rx.send(frame(301, false, 0x20));
    wait_until(held_now, 2000);
    for (int i = 0; i < 9; ++i) a.OnEncodedH264Frame(frame(301, false, 0x21));
    let_go();
    DecoderKeyRequest ra;
    const bool aAsked = a.TakeDecoderKeyRequest(&ra);
    DecoderKeyRequest rb;
    check("[C6] PC A (on screen) asks; PC B (not bound) asks nothing", aAsked && (ra.causes & kKeyCauseRecovery) != 0 &&
          !b.TakeDecoderKeyRequest(&rb));
    check("[C6] B cannot judge A's ask valid (only session-scoped causes for a non-owner)",
          b.DecoderKeyRequestValidCauses(ra) == 0 && a.DecoderKeyRequestValidCauses(ra) == kKeyCauseRecovery);
    sink.Bind(202);  // B goes on screen (A parked)
    check("[C6] after B is bound, A's earlier ask is not valid (from either adapter)",
          a.DecoderKeyRequestValidCauses(ra) == 0 && b.DecoderKeyRequestValidCauses(ra) == 0);
    a.OnDecoderKeyRequestSent(ra, 0);
    DecoderKeyRequest r2;
    check("[C6] A's late send changes nothing; A asks nothing more", field(sink.DebugStatus(), "asks") == 0 &&
          !a.TakeDecoderKeyRequest(&r2));
    sink.Unbind(202);
    check("[C6] unbound: nobody asks", !a.TakeDecoderKeyRequest(&r2) && !b.TakeDecoderKeyRequest(&r2));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [C7] a sustained overload: bounded, and back to the current picture");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 31, 311);
    hold(fake_ndk::kHoldDequeueInput);
    rx.send(frame(g, false, 0x0A));
    wait_until(held_now, 2000);
    uint64_t maxInbox = 0, maxBytes = 0;
    uint8_t lastKeyTag = 0;
    rx.t0 = Clock::now();
    for (int i = 0; i < 120; ++i) {  // 4 s at 30 fps, an IDR every 30 (a different tag each)
      const bool key = (i % 30) == 29;
      const uint8_t tag = key ? static_cast<uint8_t>(0x90 + i / 30) : 0x0B;
      if (key) lastKeyTag = tag;
      rx.send(frame(g, key, tag, key ? 400000 : 20000));
      const std::string st = sink.DebugStatus();
      maxInbox = std::max(maxInbox, field(st, "inbox"));
      maxBytes = std::max(maxBytes, field(st, "inboxBytes"));
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    const std::string st = sink.DebugStatus();
    const size_t q0 = queued();
    let_go();
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 2000);
    const auto after = tags_from(q0);
    check("[C7] frames <= 8, bytes <= 4 MiB, RX never waited", maxInbox <= 8 && maxBytes <= (4u << 20) &&
          rx.maxCallUs < kCallBoundUs, "inbox max=" + std::to_string(maxInbox) + " bytes max=" + std::to_string(maxBytes) +
          " rx max=" + std::to_string(rx.maxCallUs / 1000) + "ms");
    check("[C7] asks bounded by the episode (<= 4) over the 4 s overload", rx.wants <= 4 && field(st, "episodes") <= 1,
          "wants=" + std::to_string(rx.wants) + " episodes=" + std::to_string(field(st, "episodes")));
    // the frame held in the codec's dequeue goes first; after it the inbox's head
    const auto firstKey = std::find_if(after.begin(), after.end(), [](uint8_t t) { return t >= 0x90; });
    check("[C7] the codec restarts at the NEWEST IDR (the current picture), not an old one",
          firstKey != after.end() && *firstKey == lastKeyTag,
          "first IDR fed=" + std::to_string(firstKey != after.end() ? *firstKey : 0) + " newest=" +
              std::to_string(lastKeyTag));
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [C8] a slow codec create (3 s) while frames and losses come");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    sink.SetSurface(env, &surfaceA);
    sink.PrepareForWindowSelection(32);
    sink.OnWindowSelectionControlResultFor(answer(321), 32);
    hold(fake_ndk::kHoldCreate);
    rx.t0 = Clock::now();
    for (int i = 0; i < 90; ++i) {  // 3 s
      rx.send(frame(321, (i % 30) == 0, (i % 30) == 0 ? 0xA0 : 0x0C));
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    const size_t q0 = queued();
    let_go();
    for (int i = 0; i < 15; ++i) {
      rx.send(frame(321, (i % 30) == 0, (i % 30) == 0 ? 0xA1 : 0x0D));
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    wait_until([&] { return !tags_from(q0).empty(); }, 2000);
    check("[C8] RX never waited for the create", rx.maxCallUs < kCallBoundUs, std::to_string(rx.maxCallUs / 1000) + " ms");
    check("[C8] asks bounded (<= 4) while the codec could not be made", rx.wants <= 4,
          "wants=" + std::to_string(rx.wants));
    check("[C8] once made, it decodes from an IDR", !tags_from(q0).empty() && tags_from(q0).front() >= 0xA0);
    sink.SetSurface(env, nullptr);
  }

  std::puts("\n--- [C9] C r2 N1-b (Codex's product-sink sequence): a pending IDR replaces a delta, commits -- the next delta goes in");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 33, 331);  // IDR1 (0x80) is in the codec
    const size_t q0 = queued();
    const uint64_t ep0 = field(sink.DebugStatus(), "episodes");
    input_available(false);
    rx.send(frame(g, false, 0xB2));  // delta2: no input buffer -> pending
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 1; }, 1000);
    rx.send(frame(g, true, 0xB3));   // IDR3 replaces delta2 (the slot's loss: delta2)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const std::string mid = sink.DebugStatus();
    input_available(true);
    wait_until([&] { for (uint8_t t : tags_from(q0)) if (t == 0xB3) return true; return false; }, 1500);
    rx.send(frame(g, false, 0xB4));  // delta4, IDR3's suffix
    wait_until([&] { return tags_from(q0).size() >= 2; }, 1500);
    const std::string st = sink.DebugStatus();
    const auto t = tags_from(q0);
    check("[C9] N1-b: the codec takes IDR3 then delta4 (fed 0xB3, 0xB4); no tail gap; no new episode after the commit",
          t == std::vector<uint8_t>{0xB3, 0xB4} && field(st, "tailGap") == 0 && sfield(st, "recovery") == "idle" &&
              field(st, "episodes") <= ep0 + 1 && field(st, "gapDrops") == 0,
          "fed=" + std::to_string(t.size()) + " tailGap(mid)=" + std::to_string(field(mid, "tailGap")) + " tailGap=" +
              std::to_string(field(st, "tailGap")) + " episodes " + std::to_string(ep0) + "->" +
              std::to_string(field(st, "episodes")) + " gapDrops=" + std::to_string(field(st, "gapDrops")));
    // a loss AFTER IDR3 keeps its gap
    make_loss(sink, rx, g);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // the delta held in the dequeue goes in first
    const size_t q1 = queued();
    rx.send(frame(g, false, 0xB5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t1 = tags_from(q1);
    check("[C9] a loss after IDR3 keeps its own gap: the next delta is refused, not fed",
          field(sink.DebugStatus(), "tailGap") == 1 && std::find(t1.begin(), t1.end(), 0xB5) == t1.end(),
          "tailGap=" + std::to_string(field(sink.DebugStatus(), "tailGap")) + " fed after=" + std::to_string(t1.size()));
    sink.SetSurface(env, nullptr);
  }
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 34, 341);
    hold(fake_ndk::kHoldDequeueInput);
    rx.send(frame(g, true, 0xC3));  // IDR going in (held in its dequeue)
    wait_until(held_now, 2000);
    for (int i = 0; i < 9; ++i) rx.send(frame(g, false, 0xC4));  // a loss after it: the gap opens past it
    sink.OnVideoDiscontinuity();    // the codec reset before that IDR commits
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const size_t q1 = queued();
    rx.send(frame(g, false, 0xC5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check("[C9] a reset before the IDR commits does not clear the later loss's gap (the next delta refused)",
          field(sink.DebugStatus(), "tailGap") == 1 && tags_from(q1).empty(),
          "tailGap=" + std::to_string(field(sink.DebugStatus(), "tailGap")));
    sink.SetSurface(env, nullptr);
  }
  std::puts("\n--- [C10] C r2 N1-a at the sink: the pending slot drops a delta while the codec keeps its references");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 35, 351);
    const size_t q0 = queued();
    input_available(false);
    rx.send(frame(g, false, 0xD1));  // d1: held for an input buffer (before any loss: valid)
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 1; }, 1000);
    hold(fake_ndk::kHoldDequeueInput);
    input_available(true);
    wait_until(held_now, 2000);      // the worker's retry of d1 is now held in its dequeue
    rx.send(frame(g, false, 0xD2));  // d2: the slot will drop it (a loss at d2)
    rx.send(frame(g, false, 0xD3));  // d3: references d2 -- must never reach the codec
    rx.send(frame(g, true, 0xD4));   // IDR4: the chain restarts here
    rx.send(frame(g, false, 0xD5));  // d5: IDR4's suffix
    input_available(false);          // the held dequeue answers "no buffer": d1 stays pending
    let_go();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    input_available(true);
    wait_until([&] { return tags_from(q0).size() >= 3; }, 2000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto t = tags_from(q0);
    const std::string st = sink.DebugStatus();
    // The pending slot's own rule (apk-0232 r2, unchanged by C) also acts here: while d1 is held, IDR4 replaces it and
    // d5 (arriving while IDR4 is held) is dropped. What C r2 adds: the slot's loss at d2 TRIMS d3 out of the queue
    // (chainTrim) -- it is never handed to the worker. (In this sequence the slot would drop d3 as well; the r1-vs-r2
    // contrast of the trim itself is the pure test's Codex counterexample.)
    check("[C10] N1-a: the loss at d2 trims d3 out of the queue; the codec's next input is IDR4, never d3",
          !t.empty() && t.front() == 0xD4 && std::find(t.begin(), t.end(), 0xD3) == t.end() && field(st, "chainTrim") >= 1,
          "fed=" + [&] { std::string s; for (uint8_t x : t) s += std::to_string(x) + ","; return s; }() +
              " chainTrim=" + std::to_string(field(st, "chainTrim")) + " tailGap=" + std::to_string(field(st, "tailGap")));
    sink.SetSurface(env, nullptr);
  }
  std::puts("\n--- [C11] C r2 §8 (a): the owed duty's asks spent -- covered, a final window, exhausted; a late IDR clears it");
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 36, 361);
    input_available(false);
    rx.send(frame(g, true, 0xE1));  // an IDR of the answered generation, held for a buffer ...
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 1; }, 1000);
    sink.OnVideoDiscontinuity();    // ... and lost with the codec: KeyLost -> owed
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 0; }, 1000);
    input_available(true);
    const uint64_t token = sink.KeyframeOwedFor();
    sink.OnDecoderKeyOwedExhausted(token + 1000, 0);  // a stale notice (another duty)
    const std::string s0 = sink.DebugStatus();
    check("[C11] owed armed; a stale token's notice changes nothing", token != 0 && field(s0, "covered") == 0 &&
          field(s0, "coveredStale") == 1);
    sink.OnDecoderKeyOwedExhausted(token, 0);
    DecoderKeyRequest r;
    const bool asked = sink.TakeDecoderKeyRequest(&r);
    const std::string s1 = sink.DebugStatus();
    check("[C11] the current token's notice: covered, NOT yet exhausted (the final window), and no ask of the sink's own",
          field(s1, "covered") == 1 && field(s1, "coveredExh") == 0 && !asked);
    std::this_thread::sleep_for(std::chrono::milliseconds(4400));
    const bool asked2 = sink.TakeDecoderKeyRequest(&r);
    const std::string s2 = sink.DebugStatus();
    check("[C11] after the 4 s final window with no usable IDR: covered-exhausted; still no ask; the gate still owes (not "
          "falsely paid)", field(s2, "coveredExh") == 1 && !asked2 && sink.KeyframeOwedFor() == token);
    const size_t q0 = queued();
    rx.send(frame(g, true, 0xE2));  // the host's late IDR
    rx.send(frame(g, false, 0xE3));
    wait_until([&] { return tags_from(q0).size() >= 2; }, 1500);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const std::string s3 = sink.DebugStatus();
    check("[C11] the late IDR is fed (exhausted blocks nothing), pays the duty and ends the covered state",
          tags_from(q0).size() >= 2 && tags_from(q0).front() == 0xE2 && sink.KeyframeOwedFor() == 0 &&
              field(s3, "covered") == 0 && field(s3, "coveredExh") == 0);
    sink.SetSurface(env, nullptr);
  }
  {
    AndroidVideoDecoderSink sink;
    Rx rx{sink};
    const uint64_t g = start(sink, rx, 37, 371);
    input_available(false);
    rx.send(frame(g, true, 0xE4));
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 1; }, 1000);
    sink.OnVideoDiscontinuity();
    wait_until([&] { return field(sink.DebugStatus(), "pending") == 0; }, 1000);
    input_available(true);
    const uint64_t token = sink.KeyframeOwedFor();
    sink.OnDecoderKeyOwedExhausted(token, 0);
    sink.SetSurface(env, &surfaceA);  // a new surface: the duty is re-armed (a new token, the session's 8 again)
    const std::string st = sink.DebugStatus();
    check("[C11] a new surface re-arms the duty (new token): the covered state of the old one is gone",
          sink.KeyframeOwedFor() != 0 && sink.KeyframeOwedFor() != token && field(st, "covered") == 0);
    sink.SetSurface(env, nullptr);
  }
  std::printf("\nandroid_decoder_inbox_c_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
