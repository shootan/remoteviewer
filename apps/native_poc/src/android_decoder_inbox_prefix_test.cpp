// apk-inbox C r4 (Codex codex_apk_inbox_prefix_design_go_r1.md): the keep-prefix prototype against today's C, on the
// PRODUCT sink + the stand-in NDK, the SAME schedule for both policies (SetPrefixPolicyForTest: one binary, one
// definition, a runtime switch -- default OFF). Judged by an INDEPENDENT reference oracle that reads only what the
// codec was handed (fake_ndk's queued payloads: the NAL type and the AU seq each test frame carries) and knows nothing
// of the inbox: an IDR is decodable; a delta is decodable only if it is the very next AU after the last decodable one
// since an IDR on this codec instance. "corrupt" = a delta handed to the codec that is not decodable.
//   [P0] no loss: the same inputs both ways, nothing recovered, nothing corrupt
//   [P1] a burst while the codec is busy inside its API (the inbox overflows with no IDR): today's C drops the whole
//        queue; keep-prefix feeds the valid prefix -- more decodable inputs, none corrupt
//   [P2] no input buffer for a while (< the age budget): backpressure keeps the queue; no hot loop
//   [P3] no input buffer past the age budget: nothing past the budget is fed; recovery; none corrupt
//   [P4] an IDR arrives while a prefix is kept: the IDR wins (case 1), none corrupt
//   [P5] the codec reset while a prefix is kept: no delta reaches the new codec before an IDR
//   [P6] a hole in the queued AUs (an assembler loss the sink was not told of): not provable -> today's case 3
//   [P7] sustained overload: bounded inbox, bounded asks; afterwards the current frame again
// Tag: pure-logic (stand-in NDK). Tests only.
//
//   remote60_android_decoder_inbox_prefix_test

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
uint32_t gSeq = 0;
// a test AU: its NAL (IDR 0x65 with SPS/PPS / delta 0x41), then the marker EE EE and its seq (4 bytes LE) last
UdpH264AssembledFrame au(uint64_t gen, bool key, uint32_t seq) {
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
    put({0x65});
  } else {
    put({0x41});
  }
  const uint8_t m[] = {0xEE, 0xEE, uint8_t(seq), uint8_t(seq >> 8), uint8_t(seq >> 16), uint8_t(seq >> 24)};
  f.payload.insert(f.payload.end(), m, m + 6);
  return f;
}
UdpH264AssembledFrame next(uint64_t gen, bool key) { return au(gen, key, ++gSeq); }

template <class F>
bool wait_until(F&& f, int ms) {
  const auto end = Clock::now() + std::chrono::milliseconds(ms);
  while (Clock::now() < end) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return f();
}
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
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
uint64_t field(const std::string& status, const char* key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = status.find(k);
  return at == std::string::npos ? ~0ull : std::strtoull(status.c_str() + at + k.size(), nullptr, 10);
}
ControlWindowSelectedMessage answer(uint64_t gen) {
  ControlWindowSelectedMessage m{};
  m.flags = 0x1u;
  m.streamGeneration = gen;
  return m;
}
JNIEnv* env = nullptr;
int surfaceA = 1;

// ---- the independent oracle: only the codec's inputs ----
struct Fed {
  bool key = false;
  uint32_t seq = 0;
};
std::vector<Fed> fed_from(size_t from) {
  std::lock_guard<std::mutex> lk(fake_ndk::mu());
  std::vector<Fed> out;
  const auto& q = fake_ndk::controls().queued;
  for (size_t i = from; i < q.size(); ++i) {
    const auto& p = q[i];
    Fed f;
    for (size_t j = 0; j + 4 < p.size(); ++j) {
      if (p[j] == 0 && p[j + 1] == 0 && p[j + 2] == 0 && p[j + 3] == 1 && (p[j + 4] & 0x1F) == 5) f.key = true;
    }
    if (p.size() >= 6 && p[p.size() - 6] == 0xEE && p[p.size() - 5] == 0xEE) {
      f.seq = uint32_t(p[p.size() - 4]) | uint32_t(p[p.size() - 3]) << 8 | uint32_t(p[p.size() - 2]) << 16 |
              uint32_t(p[p.size() - 1]) << 24;
    }
    out.push_back(f);
  }
  return out;
}
struct Verdict {
  int decodable = 0, corrupt = 0, keys = 0, deltas = 0;
  uint32_t lastSeq = 0;
};
// `resetAt`: indexes (into the list) where a new codec instance begins -- the chain starts over there
Verdict oracle(const std::vector<Fed>& in, bool chainAtStart, uint32_t lastAtStart, std::vector<size_t> resetAt = {}) {
  Verdict v;
  bool chain = chainAtStart;
  uint32_t last = lastAtStart;
  for (size_t i = 0; i < in.size(); ++i) {
    if (std::find(resetAt.begin(), resetAt.end(), i) != resetAt.end()) chain = false;
    const Fed& f = in[i];
    if (f.key) {
      ++v.keys;
      ++v.decodable;
      chain = true;
      last = f.seq;
    } else {
      ++v.deltas;
      if (chain && f.seq == last + 1) {
        ++v.decodable;
        last = f.seq;
      } else {
        ++v.corrupt;
        chain = false;
      }
    }
    v.lastSeq = f.seq;
  }
  return v;
}
std::string list(const std::vector<Fed>& in) {
  std::string out;
  for (const Fed& f : in) out += std::to_string(f.seq) + (f.key ? "K " : " ");
  return out;
}
std::string show(const Verdict& v) {
  return "decodable=" + std::to_string(v.decodable) + " corrupt=" + std::to_string(v.corrupt) + " keys=" +
         std::to_string(v.keys) + " deltas=" + std::to_string(v.deltas) + " lastSeq=" + std::to_string(v.lastSeq);
}

// one fresh sink per run: surface, a selection answered, its IDR + 3 deltas fed and decoded; the policy set before
struct Run {
  AndroidVideoDecoderSink sink;
  uint64_t gen;
  size_t q0 = 0;         // the queue index the schedule starts at
  uint32_t seqAtStart = 0;
  explicit Run(bool prefix, uint64_t sel, uint64_t g) : gen(g) {
    input_available(true);
    let_go();
    sink.SetPrefixPolicyForTest(prefix);
    sink.SetTargetFps(30);  // the age budget: 8 / 30 fps -> 267 ms
    sink.SetSurface(env, &surfaceA);
    sink.PrepareForWindowSelection(sel);
    sink.OnWindowSelectionControlResultFor(answer(g), sel);
    const size_t a = queued();
    send(next(g, true));
    wait_until([&] { return queued() > a; }, 2000);
    // the codec shows its first picture (a format, then one output): the selection is on screen, as on a device. With
    // none at all the sink's bootstrap replay (apk-0232: re-queue the selection's IDR while no picture came) would
    // re-feed an OLD IDR in the middle of these schedules -- a stand-in artefact, not either policy.
    {
      std::lock_guard<std::mutex> lk(fake_ndk::mu());
      fake_ndk::Controls::Output fmt;
      fmt.formatChange = true;
      fmt.width = 1280;
      fmt.height = 720;
      fake_ndk::controls().outputs.push_back(fmt);
      fake_ndk::Controls::Output pic;
      pic.presentationTimeUs = 1;
      fake_ndk::controls().outputs.push_back(pic);
    }
    for (int i = 0; i < 3; ++i) {
      send(next(g, false));
      sleep_ms(5);
    }
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0 && queued() >= a + 4; }, 1000);
    q0 = queued();
    seqAtStart = gSeq;
  }
  ~Run() {
    let_go();
    input_available(true);
    sink.SetSurface(env, nullptr);
  }
  void send(UdpH264AssembledFrame&& f) {
    sink.OnEncodedH264Frame(std::move(f));
    DecoderKeyRequest r;
    while (sink.TakeDecoderKeyRequest(&r)) sink.OnDecoderKeyRequestSent(r, 0);
  }
  void deltas(int n, int gapMs) {
    for (int i = 0; i < n; ++i) {
      send(next(gen, false));
      if (gapMs) sleep_ms(gapMs);
    }
  }
  // the host's answer: an IDR and `n` paced deltas, then everything handed out
  void recover(int n) {
    send(next(gen, true));
    sleep_ms(10);
    deltas(n, 5);
    wait_until([&] { return field(sink.DebugStatus(), "inbox") == 0; }, 1500);
    sleep_ms(30);
  }
  Verdict verdict(std::vector<size_t> resetAt = {}) { return oracle(fed_from(q0), true, seqAtStart, resetAt); }
};

// [P1] the codec busy inside its API (dequeueInputBuffer held, mu_ released) while 12 deltas arrive
Verdict burst(bool prefix, std::string* st, uint64_t sel) {
  Run r(prefix, sel, sel * 10 + 1);
  hold(fake_ndk::kHoldDequeueInput);
  r.deltas(1, 0);
  wait_until(held_now, 2000);
  r.deltas(12, 1);
  sleep_ms(20);
  let_go();
  wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1500);
  sleep_ms(30);
  r.recover(5);
  *st = r.sink.DebugStatus();
  return r.verdict();
}
// [P2] / [P3] no input buffer for `stallMs`, deltas arriving 33 ms apart meanwhile
std::vector<uint32_t> gStale;  // [P3] AUs whose residence was over the budget (+ margin) when the buffers came back
Verdict nobuf(bool prefix, int stallMs, std::string* st, uint64_t* passesDuring, uint64_t sel,
              std::vector<Fed>* fedOut = nullptr) {
  Run r(prefix, sel, sel * 10 + 1);
  input_available(false);
  const uint64_t p0 = field(r.sink.DebugStatus(), "passes");
  const auto t0 = Clock::now();
  std::vector<std::pair<uint32_t, Clock::time_point>> sentAt;
  while (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < stallMs) {
    r.deltas(1, 0);
    sentAt.push_back({gSeq, Clock::now()});
    sleep_ms(33);
  }
  *passesDuring = field(r.sink.DebugStatus(), "passes") - p0;
  const auto back = Clock::now();
  gStale.clear();
  for (const auto& [seq, at] : sentAt) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(back - at).count() > 267 + 60) gStale.push_back(seq);
  }
  input_available(true);
  wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1500);
  sleep_ms(30);
  r.recover(5);
  *st = r.sink.DebugStatus();
  if (fedOut) *fedOut = fed_from(r.q0);
  return r.verdict();
}
}  // namespace

int main() {
  std::printf("android_decoder_inbox_prefix_test (C r4 keep-prefix prototype vs today's C, one oracle)\n");

  std::puts("\n--- [P0] no loss: the same inputs, nothing recovered, nothing corrupt");
  for (const bool prefix : {false, true}) {
    Run r(prefix, 100 + prefix, 1001 + prefix * 10);
    r.deltas(20, 10);
    wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1000);
    sleep_ms(20);
    const Verdict v = r.verdict();
    const std::string st = r.sink.DebugStatus();
    check(std::string("[P0 ") + (prefix ? "prefix" : "current") + "] 20 deltas in order: all decodable, none corrupt, "
          "no recovery", v.decodable == 20 && v.corrupt == 0 && field(st, "episodes") == 0, show(v));
  }

  std::puts("\n--- [P1] a burst while the codec is busy (the inbox overflows with no IDR)");
  std::string s0, s1;
  const Verdict b0 = burst(false, &s0, 110);
  const Verdict b1 = burst(true, &s1, 111);
  check("[P1 current] today's C: the queue dropped whole (case 3), the next IDR recovers; none corrupt",
        b0.corrupt == 0 && field(s0, "prefixKept") == 0, show(b0) + " inboxDrops=" + std::to_string(field(s0, "inboxDrops")));
  check("[P1 prefix] the valid prefix kept (case 3') and fed after the codec came back; none corrupt",
        b1.corrupt == 0 && field(s1, "prefixKept") >= 1 && field(s1, "prefixKeptFrames") >= 7,
        show(b1) + " prefixKept=" + std::to_string(field(s1, "prefixKept")) + " frames=" +
            std::to_string(field(s1, "prefixKeptFrames")) + " tailLost=" + std::to_string(field(s1, "tailLost")));
  check("[P1] the same schedule: keep-prefix hands the codec MORE decodable inputs than today's C (oracle)",
        b1.decodable > b0.decodable, std::to_string(b0.decodable) + " -> " + std::to_string(b1.decodable));
  check("[P1] both recover to the newest AU sent (the host's IDR and its deltas)", b0.lastSeq != 0 && b1.lastSeq != 0);

  std::puts("\n--- [P2] no input buffer for 150 ms (< the 267 ms budget)");
  uint64_t pp0 = 0, pp1 = 0;
  const Verdict n0 = nobuf(false, 150, &s0, &pp0, 120);
  const Verdict n1 = nobuf(true, 150, &s1, &pp1, 121);
  check("[P2 current] none corrupt", n0.corrupt == 0, show(n0));
  check("[P2 prefix] backpressure: the queue waits behind the held frame and is fed after; none corrupt; more decodable",
        n1.corrupt == 0 && field(s1, "backpressure") >= 1 && n1.decodable >= n0.decodable,
        show(n1) + " vs current " + std::to_string(n0.decodable) + " backpressure=" + std::to_string(field(s1, "backpressure")));
  check("[P2 prefix] no hot loop: worker passes during the 150 ms stall stay at the pump interval's rate (<= 2 per 3 ms)",
        pp1 <= 150 / 3 * 2 + 10, "passes=" + std::to_string(pp1) + " (current " + std::to_string(pp0) + ")");

  std::puts("\n--- [P3] no input buffer for 700 ms (past the budget)");
  std::vector<Fed> fa1, fa0;
  const uint32_t a0start = gSeq;
  const Verdict a0 = nobuf(false, 700, &s0, &pp0, 130, &fa0);
  const Verdict a1 = nobuf(true, 700, &s1, &pp1, 131, &fa1);
  const std::vector<uint32_t> stale1 = gStale;
  int staleFed = 0;
  for (const Fed& f : fa1) {
    if (std::find(stale1.begin(), stale1.end(), f.seq) != stale1.end()) ++staleFed;
  }
  check("[P3 prefix] no AU that had waited longer than the budget (267 ms + 60 ms margin) when the buffers came back was "
        "handed to the codec", !stale1.empty() && staleFed == 0,
        "stale AUs=" + std::to_string(stale1.size()) + " of them fed=" + std::to_string(staleFed));
  check("[P3 current] none corrupt", a0.corrupt == 0,
        show(a0) + " bootstrapReplay=" + std::to_string(field(s0, "bootstrapReplay")) + (a0.corrupt ? " fed(seq,K=IDR) after start " + std::to_string(a0start) + ": " + list(fa0) +
                                     " status: " + s0.substr(s0.find(" inbox="), 400) : std::string()));
  check("[P3 prefix] what waited past the budget was not fed (age expiry at the slot / the Pop / the overflow); none "
        "corrupt; recovered", a1.corrupt == 0 && (field(s1, "ageExpSlot") + field(s1, "ageExpPop") + field(s1, "prefixRefused")) >= 1,
        show(a1) + " ageExpSlot=" + std::to_string(field(s1, "ageExpSlot")) + " ageExpPop=" + std::to_string(field(s1, "ageExpPop")) +
            " prefixRefused=" + std::to_string(field(s1, "prefixRefused")));
  check("[P3 prefix] no hot loop over 700 ms", pp1 <= 700 / 3 * 2 + 10, "passes=" + std::to_string(pp1));

  std::puts("\n--- [P4] an IDR arrives while a prefix is kept");
  {
    Run r(true, 140, 1401);
    hold(fake_ndk::kHoldDequeueInput);
    r.deltas(1, 0);
    wait_until(held_now, 2000);
    r.deltas(10, 1);  // a prefix kept, the tail lost
    r.send(next(r.gen, true));  // the host's IDR while still busy
    r.deltas(2, 1);
    sleep_ms(10);
    let_go();
    wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1500);
    sleep_ms(30);
    const Verdict v = r.verdict();
    check("[P4] the IDR is fed (case 1 keeps it with its suffix); none corrupt", v.corrupt == 0 && v.keys >= 1, show(v));
  }

  std::puts("\n--- [P5] the codec reset while a prefix is kept");
  {
    Run r(true, 150, 1501);
    hold(fake_ndk::kHoldDequeueInput);
    r.deltas(1, 0);
    wait_until(held_now, 2000);
    r.deltas(10, 1);
    const size_t at = queued() - r.q0;  // the next input goes to the new codec
    r.sink.OnVideoDiscontinuity();      // references gone
    let_go();
    wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1500);
    sleep_ms(30);
    r.recover(3);
    const auto in = fed_from(r.q0);
    const uint32_t inflight = r.seqAtStart + 1;  // the AU the worker had taken before the reset (old codec, if any)
    size_t firstNew = in.size();
    bool deltaBeforeKey = false;
    for (size_t i = at; i < in.size(); ++i) {
      if (in[i].seq == inflight) continue;
      if (in[i].key) {
        firstNew = i;
        break;
      }
      deltaBeforeKey = true;
    }
    const Verdict v = firstNew < in.size() ? oracle(std::vector<Fed>(in.begin() + firstNew, in.end()), false, 0)
                                            : Verdict{};
    check("[P5] after the reset no delta reaches the new codec before an IDR (the kept prefix is not fed); from that IDR "
          "on, none corrupt", !deltaBeforeKey && firstNew < in.size() && v.corrupt == 0,
          show(v) + " inputs after the reset point=" + std::to_string(in.size() - at));
  }

  std::puts("\n--- [P6] a hole in the queued AUs (an AU never delivered, the sink not told)");
  for (const bool prefix : {false, true}) {
    Run r(prefix, 160 + prefix, 1601 + prefix * 10);
    hold(fake_ndk::kHoldDequeueInput);
    r.deltas(1, 0);
    wait_until(held_now, 2000);
    r.deltas(4, 1);
    ++gSeq;  // this AU is never sent
    r.deltas(7, 1);
    sleep_ms(10);
    let_go();
    wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1500);
    sleep_ms(30);
    r.recover(3);
    const Verdict v = r.verdict();
    const std::string st = r.sink.DebugStatus();
    check(std::string("[P6 ") + (prefix ? "prefix" : "current") + "] not provable (not consecutive) -> today's case 3: "
          "none corrupt", v.corrupt == 0 && field(st, "prefixKept") == 0, show(v) + " prefixRefused=" +
          std::to_string(field(st, "prefixRefused")));
  }

  std::puts("\n--- [P6b] the AU right after the one in flight never delivered (head != frontier + 1)");
  for (const bool prefix : {false, true}) {
    Run r(prefix, 165 + prefix, 1651 + prefix * 10);
    hold(fake_ndk::kHoldDequeueInput);
    r.deltas(1, 0);
    wait_until(held_now, 2000);
    ++gSeq;  // never sent
    r.deltas(11, 1);
    sleep_ms(10);
    let_go();
    wait_until([&] { return field(r.sink.DebugStatus(), "inbox") == 0; }, 1500);
    sleep_ms(30);
    r.recover(3);
    const Verdict v = r.verdict();
    const std::string st = r.sink.DebugStatus();
    check(std::string("[P6b ") + (prefix ? "prefix" : "current") + "] the queue does not follow the frontier -> not "
          "provable -> today's case 3: none corrupt", v.corrupt == 0 && field(st, "prefixKept") == 0, show(v));
  }

  std::puts("\n--- [P7] sustained overload, then the current frame again");
  for (const bool prefix : {false, true}) {
    Run r(prefix, 170 + prefix, 1701 + prefix * 10);
    uint64_t maxInbox = 0;
    for (int round = 0; round < 5; ++round) {
      hold(fake_ndk::kHoldDequeueInput);
      r.deltas(1, 0);
      wait_until(held_now, 2000);
      for (int i = 0; i < 12; ++i) {
        r.deltas(1, 1);
        maxInbox = std::max<uint64_t>(maxInbox, field(r.sink.DebugStatus(), "inbox"));
      }
      let_go();
      sleep_ms(40);
    }
    const std::string mid = r.sink.DebugStatus();
    r.recover(5);
    const Verdict v = r.verdict();
    const auto in = fed_from(r.q0);
    check(std::string("[P7 ") + (prefix ? "prefix" : "current") + "] bounded (inbox <= 8, asks <= 4 per episode), none "
          "corrupt, and the newest AU sent is the last one fed (the current frame again)",
          maxInbox <= 8 && field(mid, "asks") <= 4 && v.corrupt == 0 && !in.empty() && in.back().seq == gSeq,
          show(v) + " maxInbox=" + std::to_string(maxInbox) + " asks=" + std::to_string(field(mid, "asks")) + " newest=" +
              std::to_string(gSeq));
  }

  std::printf("\nandroid_decoder_inbox_prefix_test: %d checks, %d failed\nRESULT: %s\n", gChecks, gFailures,
              gFailures == 0 ? "PASS" : "FAIL");
  return gFailures == 0 ? 0 : 1;
}
