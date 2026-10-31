import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

SRC = r'D:\remote\remote\apps\native_poc\src'
BSN = chr(92) + 'n'

# ---- the NACK env default lives in one place the product and the test both read ----
edit(SRC + r'\viewer_udp_session.hpp', [
("""namespace remote60::native_poc::viewer {

// The Hello the Windows viewer sends:""",
"""namespace remote60::native_poc::viewer {

// REMOTE60_NATIVE_VIDEO_NACK's default: on. The receive-path test passes this same constant, so a
// changed default is visible on the fake host's wire (the Hello's feature bits) in that test.
constexpr bool kVideoNackEnabledDefault = true;

// The Hello the Windows viewer sends:"""),
])
edit(SRC + r'\viewer_startup.cpp', [
("""  ctx.videoNackEnabled = env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK", 1, 0, 1) != 0;
""",
"""  ctx.videoNackEnabled =
      env_u32_clamped("REMOTE60_NATIVE_VIDEO_NACK", kVideoNackEnabledDefault ? 1u : 0u, 0, 1) != 0;
"""),
])

# ---- recovery integration test: product negotiation helpers, wire observation, S11, S12 ----
edit(SRC + r'\viewer_udp_recovery_test.cpp', [
("""#include "viewer_state.hpp"
#include "viewer_video_receiver.hpp"
""",
"""#include "viewer_state.hpp"
#include "viewer_udp_session.hpp"
#include "viewer_video_receiver.hpp"
"""),
("""  uint16_t port() const { return port_; }
  bool peer_known() const { return peerKnown_.load(); }
""",
"""  uint16_t port() const { return port_; }
  bool peer_known() const { return peerKnown_.load(); }
  // The feature bits the viewer's Hello actually carried on the wire.
  uint32_t hello_features() const { return helloFeatures_.load(); }
  // Zero the next IDR's payload: the assembler completes it, the decoder cannot use it.
  void CorruptNextKey() { corruptNextKey_ = true; }
  uint64_t corrupted_keys() const { return corruptedKeys_; }
"""),
("""    CachedAu au;
    au.seq = ++seq_;
    au.payload = bytes;
""",
"""    CachedAu au;
    au.seq = ++seq_;
    au.payload = bytes;
    if (key && corruptNextKey_.exchange(false)) {
      std::fill(au.payload.begin(), au.payload.end(), 0);
      ++corruptedKeys_;
    }
"""),
("""  uint64_t keyframesSent_ = 0;
  uint64_t droppedChunks_ = 0;
};
""",
"""  uint64_t keyframesSent_ = 0;
  uint64_t droppedChunks_ = 0;
  std::atomic<bool> corruptNextKey_{false};
  uint64_t corruptedKeys_ = 0;
};
"""),
("""    // The same handshake connect_media_socket runs, with the same options.
    UdpHelloOptions hello;
    hello.budgetMs = 2000;
    hello.sliceMaxMs = 200;
    hello.retrySleepMs = 20;
    hello.requestNack = requestNack;
    uint32_t ackFeatures = 0;
    std::string error;
    if (!udp_hello_handshake(s, hello, nullptr, &error, &ackFeatures)) {
      std::printf("  viewer: hello failed: %s\\n", error.c_str());
      return false;
    }
    (void)set_recv_timeout(s, recvTimeoutMs);
    ctx.session.sock = s;
    ctx.session.udpHelloAckFeatures = ackFeatures;
    ctx.session.hostSupportsNack = requestNack && (ackFeatures & kUdpFeatureVideoNack) != 0;
""",
"""    // The product's own negotiation (viewer_udp_session.hpp, what connect_media_socket runs):
    // the Hello options, the ack bits kept, the receive timeout armed. Not a test-only handshake,
    // so a product default that stops requesting NACK fails S1 here (Codex condition 5).
    UdpHelloOptions hello = viewer_udp_hello_options(std::string(), requestNack);
    hello.budgetMs = 2000;  // a local fake host answers at once; keep a failed run short
    uint32_t ackFeatures = 0;
    std::string error;
    if (!udp_hello_handshake(s, hello, nullptr, &error, &ackFeatures)) {
      std::printf("  viewer: hello failed: %s\\n", error.c_str());
      return false;
    }
    (void)viewer_arm_udp_recv_timeout(s, recvTimeoutMs);
    ctx.session.sock = s;
    viewer_apply_udp_hello_ack(ackFeatures, requestNack, ctx.session);
"""),
("""bool start_session(FakeHost& host, ViewerRig& rig, const LossPlan& plan, bool requestNack = true,
                   uint64_t holdUs = 120000) {
""",
"""bool start_session(FakeHost& host, ViewerRig& rig, const LossPlan& plan,
                   bool requestNack = kVideoNackEnabledDefault, uint64_t holdUs = 120000) {
"""),
("""  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  CHECK(rig.hostSupportsNack(), "NACK negotiated");
  const uint64_t requestsBefore = host.keyframe_requests();
  pump(host, rig, 1500);
  const uint32_t t = target.load();
  CHECK(t != 0, "a multi-chunk P frame was chosen for the loss");
  CHECK(host.nacks_for(t) >= 1, "viewer NACKed seq " + std::to_string(t) + " (nacks=" + std::to_string(host.nacks_for(t)) + ")");
""",
"""  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  // Observed on the wire, not assumed: the product's default Hello asks for NACK.
  CHECK((host.hello_features() & kUdpFeatureVideoNack) != 0, "the Hello carried kUdpFeatureVideoNack");
  CHECK((rig.ctx.session.udpHelloAckFeatures & kUdpFeatureVideoNack) != 0, "the HelloAck bits were kept");
  CHECK(rig.hostSupportsNack(), "NACK negotiated");
  const uint64_t requestsBefore = host.keyframe_requests();
  pump(host, rig, 1500);
  const uint32_t t = target.load();
  CHECK(t != 0, "a multi-chunk P frame was chosen for the loss");
  CHECK(host.nacks_for(t) >= 1, "viewer NACKed seq " + std::to_string(t) + " (nacks=" + std::to_string(host.nacks_for(t)) + ")");
"""),
("""  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  CHECK(!rig.hostSupportsNack(), "NACK not negotiated");
""",
"""  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  CHECK((host.hello_features() & kUdpFeatureVideoNack) != 0, "the viewer still asked (the host declined)");
  CHECK(!rig.hostSupportsNack(), "NACK not negotiated");
"""),
("""}  // namespace

int main() {
  std::cout.setf(std::ios::unitbuf);
""",
"""// S11: the decoder without a reset across a gap. The receiver no longer flushes the decoder when
// a complete IDR closes a seq gap; the MFT's pending-input FIFO (timestamp + synthetic provenance)
// must still map every output to its own input, and nothing decoded before the IDR may surface
// after it. Checked on the real H264Encoder/H264Decoder, no sockets. (Codex condition 3.)
void scenario_decoder_provenance_without_reset_across_gap() {
  std::printf("[S11] decoder: skip a P, feed the IDR without reset -> outputs keep their own stamps, none leak past the IDR\\n");
  H264Encoder enc;
  if (!enc.initialize(kWidth, kHeight, 60, 3000000, 600)) { std::printf("  encoder init failed\\n"); ++gFailures; return; }
  H264Decoder dec;
  if (!dec.initialize(kWidth, kHeight, 60)) { std::printf("  decoder init failed\\n"); ++gFailures; return; }
  std::vector<uint8_t> nv12(static_cast<size_t>(kWidth) * kHeight * 3 / 2, 96);
  struct Au { std::vector<uint8_t> bytes; bool key; int64_t timeHns; bool synthetic; };
  std::vector<Au> aus;
  const int64_t base = 5000000;  // 0.5 s, in 100 ns units
  for (int i = 0; i < 8; ++i) {
    for (uint32_t row = 0; row < kHeight; ++row) nv12[row * kWidth + ((i * 40) % kWidth)] = 235;
    const bool synthetic = (i == 2);
    const bool forceKey = (i == 5);
    enc.set_next_input_synthetic(synthetic);
    std::vector<H264AccessUnit> units;
    const int64_t t = base + static_cast<int64_t>(i) * 166667;
    if (!enc.encode_frame(nv12, forceKey, t, &units)) { std::printf("  encode failed\\n"); ++gFailures; return; }
    for (auto& u : units) aus.push_back(Au{u.bytes, u.keyFrame, u.sampleTimeHns, u.synthetic});
  }
  // Drain what an async encoder may still hold, so the IDR forced at 5 is present.
  for (int extra = 0; extra < 4 && aus.size() < 8; ++extra) {
    std::vector<H264AccessUnit> units;
    enc.set_next_input_synthetic(false);
    if (!enc.encode_frame(nv12, false, base + static_cast<int64_t>(8 + extra) * 166667, &units)) break;
    for (auto& u : units) aus.push_back(Au{u.bytes, u.keyFrame, u.sampleTimeHns, u.synthetic});
  }
  size_t idrIndex = 0;
  for (size_t i = 1; i < aus.size(); ++i) if (aus[i].key) { idrIndex = i; break; }
  CHECK(idrIndex >= 3, "an IDR was produced past the first frames (index " + std::to_string(idrIndex) + ")");
  if (idrIndex < 3) return;
  const size_t skipped = idrIndex - 1;  // the P just before the IDR is "lost"
  std::vector<int64_t> outTimes;
  std::vector<bool> outSynthetic;
  auto decode = [&](const Au& au) {
    std::vector<DecodedFrameNv12> out;
    bool overflow = false;
    dec.set_next_input_synthetic(au.synthetic);
    const bool ok = dec.decode_access_unit(au.bytes, au.key, au.timeHns, &out, &overflow);
    for (auto& f : out) { outTimes.push_back(f.sampleTimeHns); outSynthetic.push_back(f.synthetic); }
    return ok;
  };
  for (size_t i = 0; i < aus.size(); ++i) {
    if (i == skipped) continue;  // the gap; no reset follows
    CHECK(decode(aus[i]), "decode of AU " + std::to_string(i) + " ok");
  }
  // Every output carries the stamp of one of its inputs, in input order, never the skipped one.
  CHECK(!outTimes.empty(), "the decoder produced output");
  bool monotonic = true;
  bool onlyKnown = true;
  bool skippedLeaked = false;
  for (size_t i = 0; i < outTimes.size(); ++i) {
    if (i > 0 && outTimes[i] <= outTimes[i - 1]) monotonic = false;
    bool known = false;
    for (size_t k = 0; k < aus.size(); ++k) {
      if (k == skipped) continue;
      if (aus[k].timeHns == outTimes[i]) { known = true; if (outSynthetic[i] != aus[k].synthetic) onlyKnown = false; }
    }
    if (!known) onlyKnown = false;
    if (outTimes[i] == aus[skipped].timeHns) skippedLeaked = true;
  }
  CHECK(monotonic, "output stamps are in input order across the gap");
  CHECK(onlyKnown, "every output stamp + synthetic flag matches its own input");
  CHECK(!skippedLeaked, "the lost frame's stamp never surfaces");
  bool idrSeen = false;
  bool preIdrAfterIdr = false;
  for (const int64_t t : outTimes) {
    if (t == aus[idrIndex].timeHns) idrSeen = true;
    else if (idrSeen && t < aus[idrIndex].timeHns) preIdrAfterIdr = true;
  }
  CHECK(idrSeen, "the IDR decoded");
  CHECK(!preIdrAfterIdr, "nothing decoded before the IDR surfaces after it");
  dec.shutdown();
  enc.shutdown();
}

// S12: a complete but undecodable IDR (payload zeroed) must still recover through the decoder's
// own failure path -- an IDR request -- and the next good IDR resumes the stream. The
// no-extra-request rule for a complete IDR (S7) must not swallow this case. (Codex condition 3.)
void scenario_corrupted_idr_recovers_via_decode_failure() {
  std::printf("[S12] corrupted IDR: decode failure asks again, the next IDR resumes\\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  host.SkipSeq();  // the gap the IDR would otherwise close silently
  host.CorruptNextKey();
  (void)host.SendFrame(true, false, qpc_now_us());
  CHECK(host.corrupted_keys() == 1, "a zeroed IDR went out");
  const bool asked = pump_until(host, rig, 4000, [&]() { return host.keyframe_requests() > requestsBefore; });
  CHECK(asked, "the undecodable IDR produced a keyframe request (reason " + std::to_string(host.last_keyframe_reason()) + ")");
  const bool resumed = pump_until(host, rig, 5000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.gate.congestionState == ClientCongestionState::Normal;
  });
  CHECK(resumed, "resumed after the next good IDR (state " + state_name(rig) + ")");
  CHECK(host.keyframe_requests() <= requestsBefore + 4, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
}

}  // namespace

int main() {
  std::cout.setf(std::ios::unitbuf);
"""),
("""  scenario_synthetic_idle_then_real_burst_no_false_congestion();
  MFShutdown();
""",
"""  scenario_synthetic_idle_then_real_burst_no_false_congestion();
  scenario_decoder_provenance_without_reset_across_gap();
  scenario_corrupted_idr_recovers_via_decode_failure();
  MFShutdown();
"""),
])

# ---- shared_core_test: keyframe release, byte cap, seq wrap, duplicate retransmit, tail budget ----
edit(SRC + r'\native_video_client_shared_core_test.cpp', [
("""  // Legacy (no hold) is unchanged: AU 2 completing discards the half AU 1 and reports the gap.
  UdpH264FrameAssembler legacy;
""",
"""  // A complete IDR behind a broken chain is the recovery point: everything ahead of it is given
  // up at once and the IDR goes out immediately, carrying the gap (Codex condition 1).
  UdpH264FrameAssembler c;
  c.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(c, 20, 0, 8, 4, false, t0);          // incomplete P
  (void)push_chunk(c, 21, 0, 4, 4, false, t0 + 1000);   // complete P behind it
  (void)push_chunk(c, 22, 0, 4, 4, true, t0 + 2000);    // complete IDR
  if (!expect(c.PopDelivery(t0 + 2000, true, &out) && out.frame.header.seq == 22 &&
                  out.droppedPreviousIncomplete && c.PendingCount() == 0,
              "hold: a complete IDR releases the broken chain ahead of it at once")) return false;
  if (!expect(!c.PopDelivery(t0 + 2000, true, &out), "hold: nothing left behind the IDR")) return false;

  // The byte cap: three 8-byte assemblies fit in 24 bytes, the fourth evicts the oldest.
  UdpH264FrameAssembler d;
  d.ConfigureInOrderHold(100000, 8, 24);
  (void)push_chunk(d, 30, 0, 8, 4, false, t0);
  (void)push_chunk(d, 31, 0, 8, 4, false, t0 + 1000);
  r = push_chunk(d, 32, 0, 8, 4, false, t0 + 2000);
  if (!expect(!r.droppedPreviousIncomplete && d.HeldBytes() == 24, "hold: within the byte cap")) return false;
  r = push_chunk(d, 33, 0, 8, 4, false, t0 + 3000);
  if (!expect(r.droppedPreviousIncomplete && d.HeldBytes() == 24 && d.PendingCount() == 3,
              "hold: the byte cap evicts the oldest")) return false;
  (void)push_chunk(d, 31, 1, 8, 4, false, t0 + 4000);
  if (!expect(d.PopDelivery(t0 + 4000, true, &out) && out.frame.header.seq == 31,
              "hold: AU 30 was the eviction, AU 31 survived")) return false;

  // Sequence wrap: 0xFFFFFFFF then 0 are contiguous, in that order.
  UdpH264FrameAssembler w;
  w.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(w, 0u, 0, 4, 4, false, t0 + 1000);
  (void)push_chunk(w, 0xFFFFFFFFu, 0, 4, 4, false, t0);
  if (!expect(w.PopDelivery(t0 + 1000, true, &out) && out.frame.header.seq == 0xFFFFFFFFu,
              "hold: the pre-wrap AU goes first")) return false;
  if (!expect(w.PopDelivery(t0 + 1000, true, &out) && out.frame.header.seq == 0 &&
                  !out.droppedPreviousIncomplete,
              "hold: the wrapped AU follows without a gap")) return false;

  // A duplicate (retransmit of a chunk that did arrive) is absorbed, and the payload is intact.
  UdpH264FrameAssembler dup;
  dup.ConfigureInOrderHold(100000, 8);
  (void)push_chunk(dup, 40, 0, 8, 4, false, t0);
  r = push_chunk(dup, 40, 0, 8, 4, false, t0 + 1000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Partial, "hold: duplicate chunk keeps the AU partial")) return false;
  r = push_chunk(dup, 40, 1, 8, 4, false, t0 + 2000);
  if (!expect(r.disposition == UdpH264AssemblyDisposition::Queued && dup.PopDelivery(t0 + 2000, true, &out) &&
                  out.frame.payload.size() == 8 && out.frame.payload[4] == static_cast<uint8_t>(40 * 16 + 4),
              "hold: the AU completes once and its payload is intact")) return false;

  // Legacy (no hold) is unchanged: AU 2 completing discards the half AU 1 and reports the gap.
  UdpH264FrameAssembler legacy;
"""),
("""  // Blocker change restarts the schedule: AU 30 exhausted its rounds, AU 31 gets fresh ones.
""",
"""  // The tail phase has its own round budget: rounds spent on a hole while the tail was still in
  // flight do not leave the tail with none (Codex condition 2).
  a.Reset();
  s.Reset();
  const uint64_t t5 = t0 + 2000000;
  (void)push_chunk(a, 50, 0, 24, 4, false, t5);  // 6 chunks: 0 and 2 in, hole 1, tail 3..5
  (void)push_chunk(a, 50, 2, 24, 4, false, t5);
  if (!expect(s.Poll(a, true, t5 + 25000, &pkt) && pkt.missingCount == 1 && pkt.missing[0] == 1,
              "nack: hole round 0")) return false;
  if (!expect(s.busy(), "nack: busy while rounds remain")) return false;
  (void)s.Poll(a, true, t5 + 50000, &pkt);
  (void)s.Poll(a, true, t5 + 75000, &pkt);
  if (!expect(!s.Poll(a, true, t5 + 100000, &pkt) && !s.busy(), "nack: hole rounds spent, not busy")) return false;
  if (!expect(s.Poll(a, true, t5 + 120000, &pkt) && pkt.round == 0 && pkt.missingCount == 4 &&
                  pkt.missing[0] == 1 && pkt.missing[3] == 5,
              "nack: the tail phase starts with a fresh round 0 (hole + tail)")) return false;
  if (!expect(s.busy(), "nack: busy again in the tail phase")) return false;
  (void)s.Poll(a, true, t5 + 145000, &pkt);
  if (!expect(s.Poll(a, true, t5 + 170000, &pkt) && pkt.round == 2, "nack: tail round 2")) return false;
  if (!expect(!s.Poll(a, true, t5 + 195000, &pkt) && !s.busy(), "nack: tail rounds spent")) return false;

  // Blocker change restarts the schedule: AU 30 exhausted its rounds, AU 31 gets fresh ones.
"""),
])

# ---- gate test: the timer defers to a NACK repair in progress ----
edit(SRC + r'\viewer_frame_gate_test.cpp', [
("""  // Off switch: interval 0 never asks.
  Rig off;
""",
"""  // A NACK repair in progress holds the re-ask (checked again next tick); telemetry counts it.
  Rig d;
  d.gate.waitForKeyFrame = true;
  d.fg.tick(start);
  d.fg.tick(start + 500 * kMs, /*repairInProgress=*/true);
  CHECK(d.sink.requests(7) == 0);
  CHECK(d.gate.recoveryRetryDeferred == 1);
  d.fg.tick(start + 525 * kMs, false);
  CHECK(d.sink.requests(7) == 1);
  // Off switch: interval 0 never asks.
  Rig off;
"""),
])

# ---- liveness test: the bounded join ----
edit(SRC + r'\viewer_liveness_test.cpp', [
("""#include <cstdio>

#include "viewer_recv_liveness.hpp"
""",
"""#include <chrono>
#include <cstdio>
#include <thread>

#include "viewer_recv_liveness.hpp"
#include "viewer_thread_join.hpp"
"""),
("""  if (gFailures == 0) {
    std::printf("viewer_liveness_test: PASS\\n");
""",
"""  std::printf("[J1] join_with_timeout: a thread that does not return within the timeout is reported, not waited for\\n");
  {
    std::thread slow([]() { std::this_thread::sleep_for(std::chrono::milliseconds(600)); });
    const auto t0 = std::chrono::steady_clock::now();
    const bool joined = join_with_timeout(slow, 100);
    const auto waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(!joined);
    CHECK(waitedMs < 500);
    CHECK(slow.joinable());
    CHECK(join_with_timeout(slow, 5000));  // it does finish; a generous wait joins it
    CHECK(!slow.joinable());
    std::thread none;
    CHECK(join_with_timeout(none, 10));  // nothing to join
  }

  if (gFailures == 0) {
    std::printf("viewer_liveness_test: PASS\\n");
"""),
])
