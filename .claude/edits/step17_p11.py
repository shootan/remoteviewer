import re
import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'

def replace_n(path, old, new, n):
    raw = open(path, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    if bom: raw = raw[3:]
    crlf = b'\r\n' in raw
    t = raw.decode('utf-8').replace('\r\n', '\n')
    c = t.count(old)
    if c != n:
        print(f'FAIL {path}: expected {n} occurrences, found {c} for:\n{old[:160]}')
        sys.exit(1)
    t = t.replace(old, new)
    out = t.replace('\n', '\r\n') if crlf else t
    data = out.encode('utf-8')
    if bom: data = b'\xef\xbb\xbf' + data
    open(path, 'wb').write(data)
    print(f'ok {path} ({n}x)')

# ---------------- codec: epoch rides the FIFO ----------------
edit(S + r'\mf_h264_codec.hpp', [
(r"""  bool synthetic = false;
};

struct DecodedFrameNv12 {""",
 r"""  bool synthetic = false;
  // The host's flush epoch the input was accepted in (0 = untagged). Same FIFO: lets the emit
  // stage tell a pre-flush AU from the first AU of the new epoch (P11, host_pre_flush_hold.hpp).
  uint64_t inputEpoch = 0;
};

struct DecodedFrameNv12 {"""),
(r"""  void set_next_input_synthetic(bool synthetic) { nextInputSynthetic_ = synthetic; }
  void shutdown();""",
 r"""  void set_next_input_synthetic(bool synthetic) { nextInputSynthetic_ = synthetic; }
  // Flush epoch of the NEXT input (CaptureState::inputEpoch); same FIFO, same reason. (P11)
  void set_next_input_epoch(uint64_t epoch) { nextInputEpoch_ = epoch; }
  void shutdown();"""),
(r"""  std::deque<bool> pendingInputSynthetic_;  // lockstep with pendingInputSampleTimesHns_ (0.2.97)
  bool nextInputSynthetic_ = false;""",
 r"""  std::deque<bool> pendingInputSynthetic_;  // lockstep with pendingInputSampleTimesHns_ (0.2.97)
  bool nextInputSynthetic_ = false;
  std::deque<uint64_t> pendingInputEpoch_;  // lockstep too (P11)
  uint64_t nextInputEpoch_ = 0;"""),
])

edit(S + r'\mf_h264_codec.cpp', [
(r"""        int64_t normalizedAuSampleTimeHns = sampleTime;
        bool auSynthetic = nextInputSynthetic_;  // FIFO empty -> this call's input (old behaviour)
        if (!pendingInputSampleTimesHns_.empty()) {
          normalizedAuSampleTimeHns = pendingInputSampleTimesHns_.front();
          if (!pendingInputSynthetic_.empty()) auSynthetic = pendingInputSynthetic_.front();
          pendingInputSampleTimesHns_.pop_front();
          if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();
        } else {""",
 r"""        int64_t normalizedAuSampleTimeHns = sampleTime;
        bool auSynthetic = nextInputSynthetic_;  // FIFO empty -> this call's input (old behaviour)
        uint64_t auEpoch = nextInputEpoch_;
        if (!pendingInputSampleTimesHns_.empty()) {
          normalizedAuSampleTimeHns = pendingInputSampleTimesHns_.front();
          if (!pendingInputSynthetic_.empty()) auSynthetic = pendingInputSynthetic_.front();
          if (!pendingInputEpoch_.empty()) auEpoch = pendingInputEpoch_.front();
          pendingInputSampleTimesHns_.pop_front();
          if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();
          if (!pendingInputEpoch_.empty()) pendingInputEpoch_.pop_front();
        } else {"""),
(r"""        au.synthetic = auSynthetic;""",
 r"""        au.synthetic = auSynthetic;
        au.inputEpoch = auEpoch;"""),
(r"""  pendingInputSampleTimesHns_.push_back(sampleTime);
  pendingInputSynthetic_.push_back(nextInputSynthetic_);
  constexpr size_t kPendingEncoderTimestampMax = 64;
  if (pendingInputSampleTimesHns_.size() > kPendingEncoderTimestampMax) {
    pendingInputSampleTimesHns_.pop_front();
    if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();""",
 r"""  pendingInputSampleTimesHns_.push_back(sampleTime);
  pendingInputSynthetic_.push_back(nextInputSynthetic_);
  pendingInputEpoch_.push_back(nextInputEpoch_);
  constexpr size_t kPendingEncoderTimestampMax = 64;
  if (pendingInputSampleTimesHns_.size() > kPendingEncoderTimestampMax) {
    pendingInputSampleTimesHns_.pop_front();
    if (!pendingInputSynthetic_.empty()) pendingInputSynthetic_.pop_front();
    if (!pendingInputEpoch_.empty()) pendingInputEpoch_.pop_front();"""),
])
# the encoder's two FIFO resets (initialize / shutdown); the decoder's own FIFO is separate
replace_n(S + r'\mf_h264_codec.cpp',
"""  pendingInputSampleTimesHns_.clear();
  pendingInputSynthetic_.clear();
  frameIndex_ = 0;
  sequenceHeaderAnnexb_.clear();""",
"""  pendingInputSampleTimesHns_.clear();
  pendingInputSynthetic_.clear();
  pendingInputEpoch_.clear();
  frameIndex_ = 0;
  sequenceHeaderAnnexb_.clear();""", 2)

# ---------------- capture state: epoch + cache provenance ----------------
edit(S + r'\host_capture_session.hpp', [
(r"""  std::atomic<uint64_t> streamGenerationState{1};""",
 r"""  std::atomic<uint64_t> streamGenerationState{1};
  // Flush epoch (P11): bumped by EncoderState::ResetTimelineAnchors, i.e. on every capture flush,
  // restart and confirmed geometry change. Tags encoder inputs (H264Encoder FIFO) so an AU the
  // async MFT emits from a pre-flush input is recognised at the emit stage and held, and tags the
  // bootstrap cache so a pre-flush picture is never re-served as a kick.
  std::atomic<uint64_t> inputEpoch{1};"""),
(r"""  uint32_t consoleSessionId = 0;  // WTS active console session at capture time
};""",
 r"""  uint32_t consoleSessionId = 0;  // WTS active console session at capture time
  bool desktopDefault = true;     // the interactive desktop was Default (no UAC / lock screen) at capture
  uint64_t inputEpoch = 0;        // CaptureState::inputEpoch at capture (P11)
};"""),
(r"""  void LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration, uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height);""",
 r"""  void LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration, uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height, uint64_t auEpoch = 0, uint64_t currentEpoch = 0);"""),
])

edit(S + r'\host_capture_session.cpp', [
(r"""    capture.bootstrapCache.srcCaptureHeight = meta.height;""",
 r"""    capture.bootstrapCache.srcCaptureHeight = meta.height;
    // Provenance the kick checks (P11): which desktop these pixels are of, and which flush epoch.
    // The shared desktop query (250 ms cache) is enough here; the kick re-checks live.
    capture.bootstrapCache.desktopDefault = interactive_desktop_is_default();
    capture.bootstrapCache.inputEpoch = capture.inputEpoch.load(std::memory_order_acquire);"""),
(r"""  // Re-check secure/lock state live (the shared query caches ~250ms; do not trust it here).
  if (!interactive_desktop_is_default_uncached()) return false;""",
 r"""  // The picture may be re-served only on the desktop it was captured from and within the flush
  // epoch it was captured in (host_kick.hpp kick_cache_admissible). A Default-desktop picture
  // never goes out over a UAC prompt -- the rule this replaced -- but a picture captured ON the
  // secure desktop may, and that is what keeps the async MFT's one-frame hold near the 150 ms
  // kick delay during a prompt instead of the 0.5-0.8 s the 2026-09-07 15:15 trace showed: the
  // held real frame then surfaced 800 ms late as the resume anchor and the fresh frames behind
  // it read as a decode backlog (P11). Live query: the shared one caches ~250 ms.
  if (!kick_cache_admissible(snap.desktopDefault, interactive_desktop_is_default_uncached(), snap.inputEpoch,
                             capture.inputEpoch.load(std::memory_order_acquire))) {
    return false;
  }"""),
(r"""void CaptureState::LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration, uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height) {""",
 r"""void CaptureState::LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration, uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height, uint64_t auEpoch, uint64_t currentEpoch) {"""),
(r"""            << " size=" << width << "x" << height << "\n";""",
 r"""            << " size=" << width << "x" << height
            // P11: whose input this AU is (auEpoch < curEpoch would be a pre-flush picture) and how
            // long it sat between its capture stamp and the send-ready time (not a wire delay).
            << " auEpoch=" << auEpoch << " curEpoch=" << currentEpoch
            << " holdUs=" << ((sendStartUs >= captureStampUs) ? (sendStartUs - captureStampUs) : 0) << "\n";"""),
])

# ---------------- encoder state: epoch bump + hold ----------------
p = S + r'\host_encoder_manager.hpp'
t = open(p, 'rb').read().decode('utf-8')
assert '#include "host_pre_flush_hold.hpp"' not in t
lines = t.split('\n')
last_inc = max(i for i, l in enumerate(lines) if l.startswith('#include "'))
lines.insert(last_inc + 1, '#include "host_pre_flush_hold.hpp"')
open(p, 'wb').write('\n'.join(lines).encode('utf-8'))
print('include added to host_encoder_manager.hpp')
edit(p, [
(r"""  bool forceKeyNext = true;
  uint64_t forceKeySubmittedAtUs = 0;""",
 r"""  bool forceKeyNext = true;
  uint64_t forceKeySubmittedAtUs = 0;
  // Pre-flush AU hold (P11, host_pre_flush_hold.hpp): AUs the async MFT emits after a flush from
  // inputs it accepted before it. preFlushReleasing guards the re-entrant emit of a released hold.
  PreFlushHold preFlushHold;
  bool preFlushReleasing = false;"""),
(r"""  void ResetTimelineAnchors(CaptureState& capture) {
    capture.timelineOriginUs = -1;
    auTimelineOriginUs = -1;
  }""",
 r"""  void ResetTimelineAnchors(CaptureState& capture) {
    capture.timelineOriginUs = -1;
    auTimelineOriginUs = -1;
    // Every caller is a flush / restart / confirmed geometry change that also forces the next
    // input to be an IDR: the boundary after which an AU from an older input is pre-flush (P11).
    capture.inputEpoch.fetch_add(1, std::memory_order_acq_rel);
  }"""),
])

# ---------------- encode stage: tag inputs, hold pre-flush AUs, log ----------------
edit(S + r'\host_stage_encode_send_h264.cpp', [
(r"""    encoder.codec.set_next_input_synthetic(servedBootstrap);
    kick.NoteEncoderStamp(encodeInputUs, servedBootstrap);""",
 r"""    encoder.codec.set_next_input_synthetic(servedBootstrap);
    encoder.codec.set_next_input_epoch(capture.inputEpoch.load(std::memory_order_acquire));
    kick.NoteEncoderStamp(encodeInputUs, servedBootstrap);"""),
])

edit(S + r'\host_stage_encode_send_h264_au.cpp', [
(r"""  if (au.bytes.empty()) return AuFlow::Continue;
  const int64_t auCaptureUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : static_cast<int64_t>(encodeInputUs);""",
 r"""  if (au.bytes.empty()) return AuFlow::Continue;
  // P11 -- an AU from an input the encoder accepted BEFORE the last flush. An async MFT returns
  // it during the FIRST call after the flush: the pre-switch picture, its true old stamp, flagged
  // real, on the new generation (2026-09-07 15:15:59 seq 24484, 820 ms behind its send time; the
  // viewer made it the resume anchor and read the fresh frames behind it as an 848 ms backlog).
  // Hold it; the first AU of the new epoch decides (host_pre_flush_hold.hpp): the forced IDR
  // makes it redundant -> discarded; no IDR -> released first, in order, chain intact.
  const uint64_t inputEpochNow = capture.inputEpoch.load(std::memory_order_acquire);
  if (!encoder.preFlushReleasing) {
    const int64_t heldStampUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : 0;
    const uint64_t heldEpoch = au.inputEpoch;
    const bool heldKey = au.keyFrame;
    std::vector<H264AccessUnit> release;
    switch (pre_flush_decide(encoder.preFlushHold, au, inputEpochNow, &release)) {
      case PreFlushVerdict::Emit:
        break;
      case PreFlushVerdict::Hold:
        std::cout << "[native-video-host] pre-flush-au held auEpoch=" << heldEpoch << " curEpoch=" << inputEpochNow
                  << " auCaptureUs=" << heldStampUs
                  << " holdUs=" << ((encodeStartUs >= static_cast<uint64_t>(heldStampUs)) ? (encodeStartUs - static_cast<uint64_t>(heldStampUs)) : 0)
                  << " key=" << (heldKey ? 1 : 0) << " held=" << encoder.preFlushHold.held.size() << "\n";
        return AuFlow::Continue;
      case PreFlushVerdict::DiscardHeld:
        std::cout << "[native-video-host] pre-flush-au discarded behind the new epoch's keyframe curEpoch=" << inputEpochNow
                  << " discardedTotal=" << encoder.preFlushHold.discardedTotal << "\n";
        break;
      case PreFlushVerdict::ReleaseHeld: {
        std::cout << "[native-video-host] pre-flush-au released n=" << release.size()
                  << " (no keyframe followed the flush) curEpoch=" << inputEpochNow << "\n";
        encoder.preFlushReleasing = true;
        for (auto& heldAu : release) {
          const AuFlow flow = encode_send_h264_emit_au(hx, tc, b, heldAu);
          if (flow == AuFlow::Break) {
            encoder.preFlushReleasing = false;
            return flow;
          }
        }
        encoder.preFlushReleasing = false;
        break;
      }
    }
  }
  const int64_t auCaptureUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : static_cast<int64_t>(encodeInputUs);"""),
(r"""      streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height);""",
 r"""      streamGeneration, sendStartUs, hdr.captureQpcUs, hdr.width, hdr.height, au.inputEpoch, inputEpochNow);"""),
])
replace_n(S + r'\host_stage_encode_send_h264_au.cpp',
"""<< " encodeInputUs=" << encodeInputUs""",
"""<< " encodeInputUs=" << encodeInputUs
              << " auEpoch=" << au.inputEpoch << " curEpoch=" << inputEpochNow""", 2)

# ---------------- kick policy ----------------
edit(S + r'\host_kick.hpp', [
(r"""};

}  // namespace remote60::native_poc""",
 r"""};

// Whether the cached picture may be re-served as a kick / static refresh right now (P11). Pure;
// host_encode_epoch_test. The picture must be OF the desktop the user sees now -- a Default-desktop
// picture never goes out over a UAC prompt, and a prompt's picture never after it closed -- and
// from the current flush epoch, so a pre-flush picture is not resurrected after a restart. This
// replaced a flat "never on the secure desktop", which left the async MFT's one-frame hold unbounded
// during a prompt (0.5-0.8 s on 2026-09-07 15:15) and produced the old-stamped resume anchor.
inline bool kick_cache_admissible(bool cacheDesktopDefault, bool desktopDefaultNow, uint64_t cacheEpoch,
                                  uint64_t epochNow) {
  return cacheDesktopDefault == desktopDefaultNow && cacheEpoch == epochNow;
}

}  // namespace remote60::native_poc"""),
])

# ---------------- CMake ----------------
edit(ROOT + r'\apps\native_poc\CMakeLists.txt', [
(r"""add_executable(remote60_host_kick_test
  src/host_kick_test.cpp
)
target_include_directories(remote60_host_kick_test PRIVATE src)
""",
 r"""add_executable(remote60_host_kick_test
  src/host_kick_test.cpp
)
target_include_directories(remote60_host_kick_test PRIVATE src)

# P11 on the real MF H264Encoder: the flush epoch rides the accepted-input FIFO, a pre-flush AU
# surfacing after the flush is held and discarded behind the forced IDR (released in order when no
# IDR comes), a kick bounds the async MFT's hold, and the kick admissibility rule.
add_executable(remote60_host_encode_epoch_test
  src/mf_h264_codec.cpp
  src/host_encode_epoch_test.cpp
)
target_include_directories(remote60_host_encode_epoch_test PRIVATE src)
target_link_libraries(remote60_host_encode_epoch_test PRIVATE
  d3d11
  mfplat
  mf
  mfuuid
  wmcodecdspuuid
  ole32
)
"""),
])

# ---------------- viewer recovery test: the two trace shapes ----------------
edit(S + r'\viewer_udp_recovery_test.cpp', [
(r"""// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap.""",
 r"""// S13: the two 2026-09-07 15:15 trace shapes as the host emitted them BEFORE the P11 host fix.
// (a) sparse real frames (a still secure desktop, no kicks), 800 ms of nothing, then the input the
// encoder held surfaces -- captured 30 ms after the last real, sent 800 ms later -- followed by
// fresh frames 2 ms apart (seq 24469 -> 24471); (b) the same, but the frame after the old one is
// the flush-forced IDR with a fresh stamp (seq 24484 -> 24486 -> 24488). The viewer's gate is not
// changed by P11; this pins its cost on that input: at most one congestion episode and one
// keyframe request per shape, back to Normal, no keyframe wait, no storm.
void scenario_pre_fix_host_shapes_cost_is_bounded() {
  std::printf("[S13] pre-fix host shapes (old real after silence -> fresh burst; -> IDR -> fresh): bounded cost, recovers\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  for (int shape = 0; shape < 2; ++shape) {
    // A still screen served sparsely: one real frame every 250 ms, no kicks.
    for (int i = 0; i < 4; ++i) {
      (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      sleep_ms(250);
    }
    const uint64_t lastRealUs = qpc_now_us();
    (void)host.SendFrame(false, false, lastRealUs, false);
    idle(host, rig, 800);  // the hold: nothing on the wire
    const uint64_t requestsBefore = host.keyframe_requests();
    const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
    // The held input surfaces 800 ms late with its true stamp, then the fresh burst.
    (void)host.SendFrame(false, false, lastRealUs + 30000, false);
    const uint64_t burstUs = qpc_now_us();
    (void)host.SendFrame(shape == 1, false, burstUs, shape == 1);
    for (int i = 1; i < 4; ++i) (void)host.SendFrame(false, false, burstUs + static_cast<uint64_t>(i) * 2000, false);
    pump(host, rig, 900);
    const uint64_t requests = host.keyframe_requests() - requestsBefore;
    const uint64_t transitions = rig.gate.congestionTransitionCount - transitionsBefore;
    std::printf("  shape %s: congestion transitions +%llu, keyframe requests +%llu, state %s\n",
                shape == 0 ? "old-real->fresh" : "old-real->IDR->fresh",
                static_cast<unsigned long long>(transitions), static_cast<unsigned long long>(requests),
                state_name(rig).c_str());
    CHECK(requests <= 1, "at most one keyframe request (got " + std::to_string(requests) + ")");
    CHECK(transitions <= 3, "at most one congestion episode (transitions " + std::to_string(transitions) + ")");
    CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "back to Normal (state " + state_name(rig) + ")");
    CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  }
}

// S14: the same two moments as the P11 host now emits them. (a) the held real is flushed out by a
// kick 150 ms after its capture (synthetic, fresh stamp), the screen stays still 800 ms, then the
// fresh burst; (b) after the flush the FIRST AU on the wire is the forced IDR with a fresh stamp,
// the pre-flush picture never goes out. Neither may cost a congestion episode or a request.
void scenario_post_fix_host_shapes_no_congestion() {
  std::printf("[S14] post-fix host shapes (kick bounds the hold; IDR first after the flush): no congestion, no request\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  for (int shape = 0; shape < 2; ++shape) {
    for (int i = 0; i < 4; ++i) {
      (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      sleep_ms(250);
    }
    const uint64_t lastRealUs = qpc_now_us();
    (void)host.SendFrame(false, false, lastRealUs, false);
    sleep_ms(150);
    if (shape == 0) (void)host.SendFrame(false, true, qpc_now_us(), false);  // the trailing kick
    idle(host, rig, 800);
    const uint64_t requestsBefore = host.keyframe_requests();
    const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
    const uint64_t burstUs = qpc_now_us();
    (void)host.SendFrame(shape == 1, false, burstUs, shape == 1);  // (b): the forced IDR comes first
    for (int i = 1; i < 4; ++i) (void)host.SendFrame(false, false, burstUs + static_cast<uint64_t>(i) * 2000, false);
    pump(host, rig, 600);
    const uint64_t requests = host.keyframe_requests() - requestsBefore;
    const uint64_t transitions = rig.gate.congestionTransitionCount - transitionsBefore;
    CHECK(transitions == 0, std::string("no congestion transition (") + (shape == 0 ? "kick" : "IDR-first") +
                                ", transitions +" + std::to_string(transitions) + ")");
    CHECK(requests == 0, "no keyframe request (got " + std::to_string(requests) + ")");
    CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
  }
}

// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap."""),
(r"""  scenario_corrupted_idr_does_not_wedge_or_storm();
""",
 r"""  scenario_corrupted_idr_does_not_wedge_or_storm();
  scenario_pre_fix_host_shapes_cost_is_bounded();
  scenario_post_fix_host_shapes_no_congestion();
"""),
])
print('all P11 edits ok')
