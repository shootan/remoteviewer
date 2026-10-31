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

# ================= host: capture epoch + first-frame log =================
edit(S + r'\host_capture_session.cpp', [
(r"""void CaptureState::LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration, uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height) {""",
 r"""void CaptureState::LogFirstSentGeneration(CaptureResources& res, HostStats& stats, const char* path, uint64_t streamGeneration, uint64_t sendStartUs, uint64_t captureStampUs, uint32_t width, uint32_t height, uint64_t auEpoch, uint64_t currentEpoch) {"""),
(r"""            << " size=" << width << "x" << height
            << "\n";""",
 r"""            << " size=" << width << "x" << height
            // P11: whose input this AU is (auEpoch < curEpoch would be a pre-flush picture) and how
            // long it sat between its capture stamp and the send-ready time (not a wire delay).
            << " auEpoch=" << auEpoch << " curEpoch=" << currentEpoch
            << " holdUs=" << ((sendStartUs >= captureStampUs) ? (sendStartUs - captureStampUs) : 0) << "\n";"""),
])

# ================= host: encoder state =================
p = S + r'\host_encoder_manager.hpp'
t = open(p, 'rb').read().decode('utf-8')
assert '#include "host_epoch_gate.hpp"' not in t
lines = t.split('\n')
last_inc = max(i for i, l in enumerate(lines) if l.startswith('#include "'))
lines.insert(last_inc + 1, '#include "host_epoch_gate.hpp"')
open(p, 'wb').write('\n'.join(lines).encode('utf-8'))
print('include added to host_encoder_manager.hpp')
edit(p, [
(r"""  bool forceKeyNext = true;
  uint64_t forceKeySubmittedAtUs = 0;""",
 r"""  bool forceKeyNext = true;
  uint64_t forceKeySubmittedAtUs = 0;
  // Epoch gate (P11, host_epoch_gate.hpp): after a flush, nothing encoded from a pre-flush input
  // goes out and the first AU sent is the new epoch's IDR.
  EpochGate epochGate;"""),
(r"""  void ResetTimelineAnchors(CaptureState& capture) {
    capture.timelineOriginUs = -1;
    auTimelineOriginUs = -1;
  }""",
 r"""  void ResetTimelineAnchors(CaptureState& capture) {
    capture.timelineOriginUs = -1;
    auTimelineOriginUs = -1;
    // Every caller is a flush / restart / confirmed geometry change / encoder reset: the boundary
    // after which an AU from an older input is pre-flush. Most callers also force the next input
    // to be an IDR; the epoch gate re-forces it itself if one does not (P11).
    capture.inputEpoch.fetch_add(1, std::memory_order_acq_rel);
  }"""),
])

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
  // P11 -- the epoch gate. An async MFT returns an OLDER input's AU during the current call, so
  // the first call after a flush emits the pre-flush picture: true old capture stamp, flagged real,
  // on the new generation (2026-09-07 15:15:59 seq 24484, 820 ms behind its send time; the viewer
  // made it the resume anchor and read the fresh frames behind it as an 848 ms backlog). Nothing
  // from an older epoch goes out, and the first AU of the new epoch must be its IDR: a P before
  // it is dropped and the key re-forced; past the bound the encoder is rebuilt.
  const uint64_t inputEpochNow = capture.inputEpoch.load(std::memory_order_acquire);
  {
    const int64_t gateStampUs = (au.sampleTimeHns > 0) ? (au.sampleTimeHns / 10) : 0;
    const uint64_t gateHoldUs =
        (encodeStartUs >= static_cast<uint64_t>(gateStampUs)) ? (encodeStartUs - static_cast<uint64_t>(gateStampUs)) : 0;
    const EpochVerdict verdict =
        epoch_gate_judge(encoder.epochGate, inputEpochNow, au.inputEpoch, au.keyFrame, au.bytes.size(), encodeStartUs);
    switch (verdict) {
      case EpochVerdict::Emit:
        break;
      case EpochVerdict::AcceptKey:
        std::cout << "[native-video-host] epoch-gate key-accepted curEpoch=" << inputEpochNow
                  << " waitedUs=" << (encodeStartUs - encoder.epochGate.awaitSinceUs)
                  << " droppedOld=" << encoder.epochGate.droppedOldEpoch
                  << " droppedNonKey=" << encoder.epochGate.droppedAwaitingKey << "\n";
        break;
      case EpochVerdict::DropOldEpoch:
        std::cout << "[native-video-host] epoch-gate dropped-old auEpoch=" << au.inputEpoch << " curEpoch=" << inputEpochNow
                  << " auCaptureUs=" << gateStampUs << " holdUs=" << gateHoldUs << " key=" << (au.keyFrame ? 1 : 0)
                  << " bytes=" << au.bytes.size() << "\n";
        return AuFlow::Continue;
      case EpochVerdict::DropAwaitingKey:
        // The forced IDR did not come (ignored or delayed): ask again; this P would reference a
        // chain the viewer will never have.
        encoder.forceKeyNext = true;
        std::cout << "[native-video-host] epoch-gate dropped-nonkey curEpoch=" << inputEpochNow
                  << " auCaptureUs=" << gateStampUs << " reforce=1 awaitDropped=" << encoder.epochGate.awaitDropped << "\n";
        return AuFlow::Continue;
      case EpochVerdict::ResetEncoder: {
        // Bound exceeded (kAwaitMaxUs / kAwaitMaxDropped without the new epoch's IDR): the same
        // rebuild the stale-output guard does; a fresh MFT starts with an IDR.
        std::cout << "[native-video-host] epoch-gate encoder reset curEpoch=" << inputEpochNow
                  << " awaitDropped=" << encoder.epochGate.awaitDropped << " resets=" << encoder.epochGate.resetsRequested << "\n";
        encoder.codec.shutdown();
        if (!encoder.codec.initialize(encoder.activeEncodeW, encoder.activeEncodeH, encoder.activeFps, encoder.activeBitrate, encoder.activeKeyint)) {
          std::cerr << "[native-video-host] encoder reinitialize failed (epoch gate)\n";
          sendFailed = true;
          return AuFlow::Break;
        }
        encoder.ResetTimelineAnchors(capture);  // a new epoch again; the gate re-judges for it
        encoder.ResetStarvationEpisode();
        encoder.forceKeySubmittedAtUs = 0;
        ++encoder.resetCount;
        encoder.consecutiveStaleFrames = 0;
        encoder.forceKeyNext = true;
        epoch_gate_note_reset(encoder.epochGate, qpc_now_us());
        encoderResetTriggered = true;
        return AuFlow::Break;
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

# ================= CMake =================
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
# surfacing after the flush is reproduced and then gated (the new epoch's first send is its IDR),
# the gate's rules and bounds on fabricated AUs, and the hold as observed on this backend.
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

# ================= viewer: resume anchor must be a fresh frame =================
edit(S + r'\viewer_frame_gate.hpp', [
(r"""  uint64_t presentedCapUs = 0;      // FrameBuffer::lastPresentedCaptureUs
  bool catchupSuppressed = false;   // picker visible || packetNowUs < catchupSuppressUntilUs""",
 r"""  uint64_t presentedCapUs = 0;      // FrameBuffer::lastPresentedCaptureUs
  // The host's send stamp (wire). With captureQpcUs -- both host clock -- it says how long the
  // host held this picture before sending it; 0 = unknown (older host / test). (P11)
  uint64_t sendQpcUs = 0;
  bool catchupSuppressed = false;   // picker visible || packetNowUs < catchupSuppressUntilUs"""),
])
edit(S + r'\viewer_frame_gate_state.hpp', [
(r"""  uint64_t presentAnchorFloorUs = 0;""",
 r"""  uint64_t presentAnchorFloorUs = 0;
  // P11 (2026-09-07 15:15:57 / :59): the frame that ends a silence can be a picture the host had
  // been HOLDING -- its capture stamp far behind its send stamp, both host-side: the async
  // encoder's pre-flush output after a UAC switch, or the last capture kept through a still
  // secure desktop. Anchoring the floor to it made the fresh frames right behind it read as a
  // 0.5-0.8 s decode backlog (Congested + IDR, presentBacklog 0). Such a resume frame is shown but
  // leaves the anchor PENDING; the first fresh frame after it (hold <= resumeAnchorMaxHoldUs)
  // anchors. Bounded: after resumeAnchorPendingMaxFrames or resumeAnchorPendingMaxUs without a
  // fresh frame -- a genuine backlog holds every frame -- the old rule resumes and the lag trips.
  uint64_t resumeAnchorMaxHoldUs = 300000;
  uint32_t resumeAnchorPendingMaxFrames = 8;
  uint64_t resumeAnchorPendingMaxUs = 500000;
  bool resumeAnchorPending = false;
  uint64_t resumeAnchorPendingSinceUs = 0;
  uint32_t resumeAnchorPendingFrames = 0;
  uint64_t heldResumeFrames = 0;   // telemetry: resume frames judged held
  uint64_t heldResumeExpired = 0;  // telemetry: pendings that hit the bound (the backlog path)"""),
])
edit(S + r'\viewer_frame_gate.cpp', [
(r"""  const bool congestionHealthy = (gate.congestionState == ClientCongestionState::Normal);
  if (in.recvGapUs > 250000 && !in.synthetic && congestionHealthy &&
      in.captureQpcUs > gate.presentAnchorFloorUs) {
    gate.presentAnchorFloorUs = in.captureQpcUs;
  }
  const uint64_t effectivePresentedCapUs =
      congestionHealthy ? std::max(in.presentedCapUs, gate.presentAnchorFloorUs) : in.presentedCapUs;""",
 r"""  const bool congestionHealthy = (gate.congestionState == ClientCongestionState::Normal);
  if (!congestionHealthy) gate.resumeAnchorPending = false;
  // P11: the resume frame must be FRESH to anchor. hostHoldUs is host-clock only (send - capture):
  // a frame the host sat on for longer than resumeAnchorMaxHoldUs is a held picture (the async
  // encoder's pre-flush output after a UAC switch; the last capture kept through a still secure
  // desktop), not where the content is now. It is shown, but the anchor waits for the first fresh
  // frame behind it; while pending the entry estimate treats the incoming stamp as the floor.
  const uint64_t hostHoldUs = (in.sendQpcUs > in.captureQpcUs) ? (in.sendQpcUs - in.captureQpcUs) : 0;
  const bool fresh = (in.sendQpcUs == 0) || (hostHoldUs <= gate.resumeAnchorMaxHoldUs);
  bool anchorHere = false;
  if (in.recvGapUs > 250000 && !in.synthetic && congestionHealthy) {
    if (fresh) {
      anchorHere = true;
      gate.resumeAnchorPending = false;
    } else {
      gate.resumeAnchorPending = true;
      gate.resumeAnchorPendingSinceUs = in.packetNowUs;
      gate.resumeAnchorPendingFrames = 0;
      ++gate.heldResumeFrames;
    }
  } else if (gate.resumeAnchorPending && !in.synthetic && congestionHealthy) {
    ++gate.resumeAnchorPendingFrames;
    if (fresh) {
      anchorHere = true;
      gate.resumeAnchorPending = false;
    } else if (gate.resumeAnchorPendingFrames >= gate.resumeAnchorPendingMaxFrames ||
               in.packetNowUs >= gate.resumeAnchorPendingSinceUs + gate.resumeAnchorPendingMaxUs) {
      // Every frame since the silence was held: a genuine backlog. Back to the old rule.
      gate.resumeAnchorPending = false;
      ++gate.heldResumeExpired;
    }
  }
  if (anchorHere && in.captureQpcUs > gate.presentAnchorFloorUs) {
    gate.presentAnchorFloorUs = in.captureQpcUs;
  }
  const uint64_t entryFloorUs = gate.resumeAnchorPending ? std::max(gate.presentAnchorFloorUs, in.captureQpcUs)
                                                         : gate.presentAnchorFloorUs;
  const uint64_t effectivePresentedCapUs =
      congestionHealthy ? std::max(in.presentedCapUs, entryFloorUs) : in.presentedCapUs;"""),
])
edit(S + r'\viewer_video_receiver_frame.cpp', [
(r"""    in.packetNowUs = packetNowUs;
    in.recvGapUs = recvGapUs;
    in.presentedCapUs = ctx.frameBuf.lastPresentedCaptureUs.load(std::memory_order_relaxed);""",
 r"""    in.packetNowUs = packetNowUs;
    in.recvGapUs = recvGapUs;
    in.sendQpcUs = h.sendQpcUs;
    in.presentedCapUs = ctx.frameBuf.lastPresentedCaptureUs.load(std::memory_order_relaxed);"""),
])

# ---- frame gate unit tests ----
edit(S + r'\viewer_frame_gate_test.cpp', [
(r"""  FrameGateVerdict feed(uint64_t t, uint64_t captureUs, bool key, uint64_t presented, bool suppressed,
                        FrameGateLag* lag = nullptr, FrameGateInputs* inOut = nullptr,
                        bool synthetic = false) {
    FrameGateInputs in{};
    in.captureQpcUs = captureUs;""",
 r"""  FrameGateVerdict feed(uint64_t t, uint64_t captureUs, bool key, uint64_t presented, bool suppressed,
                        FrameGateLag* lag = nullptr, FrameGateInputs* inOut = nullptr,
                        bool synthetic = false, uint64_t sendUs = 0) {
    FrameGateInputs in{};
    in.captureQpcUs = captureUs;
    in.sendQpcUs = sendUs;  // 0 = unknown: the pre-P11 rules"""),
(r"""void test_idle_resume_no_false_congestion() {""",
 r"""// P11 (field 2026-09-07 15:15): after a silence the first frame is one the host HELD -- capture
// stamp 800 ms behind its send stamp -- and fresh frames follow 2 ms apart. The held frame must
// not anchor the floor (no false backlog); the first fresh frame does; nothing is requested.
void test_held_resume_frame_does_not_anchor() {
  std::printf("[T-P11a] held resume frame after a silence: shown, not the anchor; the fresh frame anchors; no congestion\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in, false, t + 10 * kMs) == FrameGateVerdict::Decode);
  r.decoded(in);
  // A still screen served sparsely: fresh frames 260 ms apart.
  uint64_t cap = t;
  for (int i = 1; i <= 4; ++i) {
    const uint64_t at = t + i * 260 * kMs;
    const uint64_t prev = cap;
    cap = at;
    CHECK(r.feed(at, cap, false, prev, false, nullptr, &in, false, cap + 20 * kMs) == FrameGateVerdict::Decode);
  }
  const uint64_t lastRealCap = cap;
  // 800 ms of nothing, then the held picture: captured 30 ms after the last real, sent 800 ms later.
  const uint64_t resumeAt = lastRealCap + 830 * kMs;
  const uint64_t heldCap = lastRealCap + 30 * kMs;
  CHECK(r.feed(resumeAt, heldCap, false, lastRealCap, false, nullptr, &in, false, resumeAt) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs == lastRealCap);  // not moved to the held stamp
  CHECK(r.gate.heldResumeFrames == 1);
  // The fresh burst, 2 ms apart, captured ~15 ms before arrival; the renderer shows the held one.
  for (int i = 0; i < 4; ++i) {
    const uint64_t at = resumeAt + 5 * kMs + i * 2 * kMs;
    const uint64_t c = at - 15 * kMs;
    FrameGateLag lag{};
    CHECK(r.feed(at, c, false, heldCap, false, &lag, &in, false, at) == FrameGateVerdict::Decode);
    CHECK(lag.decodeQueueLagEstimateUs < 100 * kMs);
  }
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs >= resumeAt - 15 * kMs);  // anchored on the first fresh frame
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.gate.lagTriggerStreak == 0);
  CHECK(r.sink.requests(1) == 0);
}

// P11 guard: a genuine backlog after a silence -- every frame arrives held (send 800 ms after its
// capture), densely -- must still trip the lag rule once the pending bound is spent.
void test_backlog_after_silence_still_trips() {
  std::printf("[T-P11b] genuine backlog after a silence (every frame held): still trips after the pending bound\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feed(t, t, true, 0, false, nullptr, &in, false, t + 10 * kMs) == FrameGateVerdict::Decode);
  r.decoded(in);
  const uint64_t silenceEnd = t + 800 * kMs;
  bool tripped = false;
  for (int i = 0; i < 16 && !tripped; ++i) {
    const uint64_t at = silenceEnd + i * 2 * kMs;              // dense arrival
    const uint64_t c = t + 16 * kMs + i * 16 * kMs;            // captures from just after the key...
    const uint64_t sendUs = c + 800 * kMs;                     // ...each held 800 ms by the host
    (void)r.feed(at, c, false, t, false, nullptr, &in, false, sendUs);
    tripped = r.gate.congestionState != ClientCongestionState::Normal;
  }
  CHECK(tripped);
  CHECK(r.gate.heldResumeExpired == 1);
  CHECK(r.sink.requests(1) >= 1);
}

void test_idle_resume_no_false_congestion() {"""),
])
replace_n(S + r'\viewer_frame_gate_test.cpp',
"""  test_keyframe_wait_drops_do_not_enter_congested();
""",
"""  test_keyframe_wait_drops_do_not_enter_congested();
  test_held_resume_frame_does_not_anchor();
  test_backlog_after_silence_still_trips();
""", 1)

# ================= viewer recovery test: product gate on the fake host, the two shapes =================
R = S + r'\viewer_udp_recovery_test.cpp'
edit(R, [
(r"""  // Encode the next picture and put it on the wire. `noisy` makes a big AU (a large IDR / a
  // multi-chunk P), `synthetic` re-sends the previous picture flagged as a host kick / refresh.
  // Returns the seq the AU went out with (0 if the encoder produced nothing this call).
  uint32_t SendFrame(bool forceKey, bool synthetic, uint64_t captureUs, bool noisy = true) {
    if (!synthetic) NextPicture(noisy);
    std::vector<H264AccessUnit> units;
    enc_.set_next_input_synthetic(synthetic);
    if (!enc_.encode_frame(nv12_, forceKey, static_cast<int64_t>(captureUs) * 10, &units)) {
      std::printf("  fake host: encode_frame failed\n");
      return 0;
    }
    uint32_t lastSeq = 0;
    for (auto& au : units) {
      lastSeq = SendAu(au.bytes, au.keyFrame, synthetic, captureUs);
      if (au.keyFrame) ++keyframesSent_;
    }
    return lastSeq;
  }""",
 r"""  // Encode the next picture and put it on the wire. `noisy` makes a big AU (a large IDR / a
  // multi-chunk P), `synthetic` re-sends the previous picture flagged as a host kick / refresh.
  // Returns the seq the AU went out with (0 if the encoder produced nothing this call).
  //
  // Every AU passes the PRODUCT's epoch gate (host_epoch_gate.hpp) on the product's own FIFO tags
  // (P11), exactly as host_stage_encode_send_h264_au.cpp does: after Flush() nothing from the old
  // epoch goes out and the first AU on the wire is the new epoch's IDR. With SetStampFromAu(true)
  // the wire capture stamp is the AU's own input stamp (the product's hdr.captureQpcUs), so a
  // picture the async encoder held goes out with its true old stamp -- the field shape.
  uint32_t SendFrame(bool forceKey, bool synthetic, uint64_t captureUs, bool noisy = true) {
    if (!synthetic) NextPicture(noisy);
    std::vector<H264AccessUnit> units;
    enc_.set_next_input_synthetic(synthetic);
    enc_.set_next_input_epoch(epoch_);
    const bool key = forceKey || forceKeyPending_;
    forceKeyPending_ = false;
    if (!enc_.encode_frame(nv12_, key, static_cast<int64_t>(captureUs) * 10, &units)) {
      std::printf("  fake host: encode_frame failed\n");
      return 0;
    }
    uint32_t lastSeq = 0;
    for (auto& au : units) {
      switch (epoch_gate_judge(gate_, epoch_, au.inputEpoch, au.keyFrame, au.bytes.size(), qpc_now_us())) {
        case EpochVerdict::DropOldEpoch: ++gateDroppedOld_; continue;
        case EpochVerdict::DropAwaitingKey: ++gateDroppedNonKey_; forceKeyPending_ = true; continue;
        case EpochVerdict::ResetEncoder: ++gateResets_; forceKeyPending_ = true; continue;
        case EpochVerdict::AcceptKey: ++gateKeysAccepted_; break;
        case EpochVerdict::Emit: break;
      }
      const uint64_t wireCaptureUs =
          (stampFromAu_ && au.sampleTimeHns > 0) ? static_cast<uint64_t>(au.sampleTimeHns / 10) : captureUs;
      lastSeq = SendAu(au.bytes, au.keyFrame, stampFromAu_ ? au.synthetic : synthetic, wireCaptureUs);
      if (flushPending_) {
        firstSeqAfterFlush_ = lastSeq;
        firstAfterFlushKey_ = au.keyFrame;
        flushPending_ = false;
      }
      if (au.keyFrame) ++keyframesSent_;
    }
    return lastSeq;
  }

  // What every product flush caller does: a new input epoch and a forced key on the next input.
  void Flush() {
    ++epoch_;
    forceKeyPending_ = true;
    flushPending_ = true;
    firstSeqAfterFlush_ = 0;
    firstAfterFlushKey_ = false;
  }
  void SetStampFromAu(bool on) { stampFromAu_ = on; }
  uint64_t gate_dropped_old() const { return gateDroppedOld_; }
  uint64_t gate_dropped_nonkey() const { return gateDroppedNonKey_; }
  uint64_t gate_keys_accepted() const { return gateKeysAccepted_; }
  uint64_t gate_resets() const { return gateResets_; }
  uint32_t first_seq_after_flush() const { return firstSeqAfterFlush_; }
  bool first_after_flush_key() const { return firstAfterFlushKey_; }
  const char* encoder_backend() const { return enc_.backend_name(); }"""),
(r"""  H264Encoder enc_;
  std::vector<uint8_t> nv12_;
  uint32_t frameIndex_ = 0;
  uint32_t seq_ = 0;""",
 r"""  H264Encoder enc_;
  std::vector<uint8_t> nv12_;
  uint32_t frameIndex_ = 0;
  uint32_t seq_ = 0;
  EpochGate gate_;
  uint64_t epoch_ = 1;
  bool forceKeyPending_ = false;
  bool flushPending_ = false;
  bool stampFromAu_ = false;
  uint64_t gateDroppedOld_ = 0;
  uint64_t gateDroppedNonKey_ = 0;
  uint64_t gateKeysAccepted_ = 0;
  uint64_t gateResets_ = 0;
  uint32_t firstSeqAfterFlush_ = 0;
  bool firstAfterFlushKey_ = false;"""),
(r"""// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap.""",
 r"""// S13: the two 2026-09-07 15:15 trace shapes as the host emitted them BEFORE the P11 host fix,
// generated by the fake host: (a) sparse real frames (a still secure desktop, no kicks), 800 ms
// of nothing, then the picture the encoder held surfaces -- captured 30 ms after the last real,
// sent 800 ms later -- followed by fresh frames 2 ms apart (seq 24469 -> 24471); (b) the same but
// the frame after the old one is an IDR with a fresh stamp (seq 24484 -> 24486 -> 24488). The
// viewer's P11 rule (a held resume frame does not anchor) must make both cost nothing: no
// congestion transition, no keyframe request, Normal.
void scenario_pre_fix_host_shapes_no_false_congestion() {
  std::printf("[S13] pre-fix host shapes (old real after silence -> fresh burst; -> IDR -> fresh): viewer stays Normal\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  for (int shape = 0; shape < 2; ++shape) {
    for (int i = 0; i < 4; ++i) {
      (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      sleep_ms(260);
    }
    const uint64_t lastRealUs = qpc_now_us();
    (void)host.SendFrame(false, false, lastRealUs, false);
    idle(host, rig, 800);  // the hold: nothing on the wire
    const uint64_t requestsBefore = host.keyframe_requests();
    const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
    (void)host.SendFrame(false, false, lastRealUs + 30000, false);  // the held picture, its true old stamp
    const uint64_t burstUs = qpc_now_us();
    (void)host.SendFrame(shape == 1, false, burstUs, shape == 1);
    for (int i = 1; i < 4; ++i) (void)host.SendFrame(false, false, burstUs + static_cast<uint64_t>(i) * 2000, false);
    pump(host, rig, 700);
    const uint64_t requests = host.keyframe_requests() - requestsBefore;
    const uint64_t transitions = rig.gate.congestionTransitionCount - transitionsBefore;
    std::printf("  shape %s: congestion transitions +%llu, keyframe requests +%llu, heldResume=%llu, state %s\n",
                shape == 0 ? "old-real->fresh" : "old-real->IDR->fresh",
                static_cast<unsigned long long>(transitions), static_cast<unsigned long long>(requests),
                static_cast<unsigned long long>(rig.gate.heldResumeFrames), state_name(rig).c_str());
    CHECK(transitions == 0, "no congestion transition (got " + std::to_string(transitions) + ")");
    CHECK(requests == 0, "no keyframe request (got " + std::to_string(requests) + ")");
    CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
    CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  }
  CHECK(rig.gate.heldResumeFrames >= 1, "the viewer saw the held resume frame(s) (" + std::to_string(rig.gate.heldResumeFrames) + ")");
}

// S14: the product's host-side rule end to end. The fake host runs the real H264Encoder and the
// product's epoch gate on the product's FIFO tags, with wire stamps taken from each AU's own
// input (as hdr.captureQpcUs is). A still screen, a flush (what every backend switch / restart
// does: new epoch, forced key), then frames: on this backend the encoder returns the pre-flush
// picture during the first post-flush call -- the gate must drop it, the first AU on the wire
// after the flush must be the new epoch's IDR, and the viewer must accept it without a request,
// a congestion episode or a keyframe wait.
void scenario_host_epoch_gate_end_to_end() {
  std::printf("[S14] flush -> the gate drops the pre-flush AU, the IDR goes first, the viewer accepts it clean\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  host.SetStampFromAu(true);
  pump(host, rig, 300);
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < 4; ++i) {
      (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      sleep_ms(260);
    }
    (void)host.SendFrame(false, false, qpc_now_us(), false);  // the last pre-flush real input
    idle(host, rig, 800);
    const uint64_t requestsBefore = host.keyframe_requests();
    const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
    const uint64_t droppedBefore = host.gate_dropped_old();
    const uint64_t keysBefore = host.gate_keys_accepted();
    host.Flush();
    for (int i = 0; i < 6; ++i) {
      (void)host.SendFrame(false, false, qpc_now_us(), i == 0);
      sleep_ms(2);
    }
    pump(host, rig, 700);
    const uint64_t droppedOld = host.gate_dropped_old() - droppedBefore;
    std::printf("  round %d (%s): pre-flush AUs dropped by the gate %llu, keys accepted %llu, first seq after flush %u key=%d, transitions +%llu, requests +%llu\n",
                round, host.encoder_backend(), static_cast<unsigned long long>(droppedOld),
                static_cast<unsigned long long>(host.gate_keys_accepted() - keysBefore), host.first_seq_after_flush(),
                host.first_after_flush_key() ? 1 : 0,
                static_cast<unsigned long long>(rig.gate.congestionTransitionCount - transitionsBefore),
                static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore));
    CHECK(host.gate_keys_accepted() - keysBefore == 1, "the new epoch's IDR was accepted once");
    CHECK(host.first_seq_after_flush() != 0 && host.first_after_flush_key(), "the first AU on the wire after the flush is a keyframe");
    CHECK(host.gate_resets() == 0, "no encoder reset was needed");
    CHECK(rig.gate.congestionTransitionCount == transitionsBefore, "no congestion transition");
    CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request");
    CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
    CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
    CHECK(rig.maxPublishedSeq.load() > host.first_seq_after_flush(), "the viewer decoded past the flush");
  }
}

// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap."""),
(r"""  scenario_corrupted_idr_does_not_wedge_or_storm();
""",
 r"""  scenario_corrupted_idr_does_not_wedge_or_storm();
  scenario_pre_fix_host_shapes_no_false_congestion();
  scenario_host_epoch_gate_end_to_end();
"""),
])
# the fake host needs the gate header
replace_n(R, """#include "mf_h264_codec.hpp"
""", """#include "host_epoch_gate.hpp"
#include "mf_h264_codec.hpp"
""", 1)
print('all P11 v2 edits ok')
