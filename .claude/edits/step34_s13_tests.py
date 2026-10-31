import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'
T = S + r'\viewer_frame_gate_test.cpp'

edit(T, [
# receiver-faithful helpers on the Rig
(r"""  // A frame that decoded: what the receiver does on the Decode verdict when the decoder succeeds.
  void decoded(const FrameGateInputs& in) {
    fg.note_decode_ok();
    fg.note_reference_sync(in);
    fg.clear_empty_streak();
  }
};""",
 r"""  // A frame that decoded: what the receiver does on the Decode verdict when the decoder succeeds
  // (viewer_video_receiver_frame.cpp: note_decode_ok -> note_reference_sync for EVERY decoded
  // frame, key or not -> clear_empty_streak). Tests that model the real path call this after
  // every Decode verdict, not only after keyframes.
  void decoded(const FrameGateInputs& in) {
    fg.note_decode_ok();
    fg.note_reference_sync(in);
    fg.clear_empty_streak();
  }
  // The decoder accepted the AU but produced no picture (async decoder holding it): the receiver
  // still calls note_reference_sync, then note_decode_empty.
  void decodedEmpty(const FrameGateInputs& in, const FrameGateLag& lag) {
    fg.note_decode_ok();
    fg.note_reference_sync(in);
    fg.note_decode_empty(in, lag);
  }
  // The decoder rejected the AU.
  void decodeFailed(const FrameGateInputs& in, const FrameGateLag& lag) { fg.note_decode_failure(in, lag); }
  // Receiver-faithful feed: admit, and on Decode do what the receiver does after a successful decode.
  FrameGateVerdict feedDecoded(uint64_t t, uint64_t captureUs, bool key, uint64_t presented, uint64_t sendUs,
                               FrameGateLag* lag = nullptr, FrameGateInputs* inOut = nullptr) {
    FrameGateInputs in{};
    FrameGateLag l{};
    const FrameGateVerdict v = feed(t, captureUs, key, presented, false, &l, &in, false, sendUs);
    if (v == FrameGateVerdict::Decode) decoded(in);
    if (lag) *lag = l;
    if (inOut) *inOut = in;
    return v;
  }
};"""),
# T-P11a: receiver-faithful (decoded after every Decode)
(r"""void test_held_resume_frame_does_not_anchor() {
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
}""",
 r"""void test_held_resume_frame_does_not_anchor() {
  std::printf("[T-P11a] held resume frame after a silence: shown, not the anchor; the fresh frame anchors; no congestion (receiver-faithful)\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  CHECK(r.feedDecoded(t, t, true, 0, t + 10 * kMs) == FrameGateVerdict::Decode);
  // A still screen served sparsely: fresh frames 260 ms apart, each decoded.
  uint64_t cap = t;
  for (int i = 1; i <= 4; ++i) {
    const uint64_t at = t + i * 260 * kMs;
    const uint64_t prev = cap;
    cap = at;
    CHECK(r.feedDecoded(at, cap, false, prev, cap + 20 * kMs) == FrameGateVerdict::Decode);
  }
  const uint64_t lastRealCap = cap;
  // 800 ms of nothing, then the held picture: captured 30 ms after the last real, sent 800 ms
  // later -- and DECODED (non-key), as the receiver does before the next frame is judged.
  const uint64_t resumeAt = lastRealCap + 830 * kMs;
  const uint64_t heldCap = lastRealCap + 30 * kMs;
  CHECK(r.feedDecoded(resumeAt, heldCap, false, lastRealCap, resumeAt) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs == lastRealCap);  // not moved to the held stamp
  CHECK(r.gate.heldResumeFrames == 1);
  // The fresh burst, 2 ms apart, captured ~15 ms before arrival; the renderer shows the held one.
  for (int i = 0; i < 4; ++i) {
    const uint64_t at = resumeAt + 5 * kMs + i * 2 * kMs;
    const uint64_t c = at - 15 * kMs;
    FrameGateLag lag{};
    CHECK(r.feedDecoded(at, c, false, heldCap, at, &lag) == FrameGateVerdict::Decode);
    CHECK(lag.decodeQueueLagEstimateUs < 100 * kMs);
  }
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs >= resumeAt - 15 * kMs);  // anchored on the first fresh frame
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.gate.lagTriggerStreak == 0);
  CHECK(r.sink.requests(1) == 0);
}

// (g) The S13 sweep failure, pinned at the gate: the held resume frame is decoded (non-key) --
// which is exactly what the receiver does before the next frame -- and pending must survive
// that decode, so the first fresh frame of the burst anchors and nothing trips.
void test_held_resume_decoded_nonkey_keeps_pending() {
  std::printf("[T-P11g] held resume frame decoded as a non-key: pending survives; first fresh frame anchors; no congestion\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feedDecoded(t, t, true, 0, t + 10 * kMs) == FrameGateVerdict::Decode);
  const uint64_t heldAt = t + 830 * kMs;
  const uint64_t heldCap = t + 30 * kMs;
  CHECK(r.feed(heldAt, heldCap, false, t, false, nullptr, &in, false, heldAt) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);
  r.decoded(in);  // the receiver decodes it: non-key
  CHECK(r.gate.resumeAnchorPending);  // FAILS on 1287883: note_reference_sync cleared it
  // The fresh burst: the renderer has not presented anything newer than the held frame.
  for (int i = 0; i < 4; ++i) {
    const uint64_t at = heldAt + 5 * kMs + i * 2 * kMs;
    FrameGateLag lag{};
    CHECK(r.feedDecoded(at, at - 15 * kMs, false, heldCap, at, &lag) == FrameGateVerdict::Decode);
    CHECK(lag.decodeQueueLagEstimateUs < 100 * kMs);
  }
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs >= heldAt - 15 * kMs);
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.gate.lagTriggerStreak == 0);
  CHECK(r.sink.requests(1) == 0);
}

// (h) Same, but the decoder accepted the held frame and returned no picture yet (async decoder):
// the receiver still calls note_reference_sync then note_decode_empty. Pending must survive.
void test_held_resume_accepted_empty_keeps_pending() {
  std::printf("[T-P11h] held resume frame accepted with empty output: pending survives; fresh frame anchors\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  FrameGateLag lag{};
  CHECK(r.feedDecoded(t, t, true, 0, t + 10 * kMs) == FrameGateVerdict::Decode);
  const uint64_t heldAt = t + 830 * kMs;
  const uint64_t heldCap = t + 30 * kMs;
  CHECK(r.feed(heldAt, heldCap, false, t, false, &lag, &in, false, heldAt) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);
  r.decodedEmpty(in, lag);
  CHECK(r.gate.resumeAnchorPending);
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);  // one empty output is not a wedge
  for (int i = 0; i < 4; ++i) {
    const uint64_t at = heldAt + 5 * kMs + i * 2 * kMs;
    FrameGateLag l{};
    CHECK(r.feedDecoded(at, at - 15 * kMs, false, heldCap, at, &l) == FrameGateVerdict::Decode);
    CHECK(l.decodeQueueLagEstimateUs < 100 * kMs);
  }
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.sink.requests(1) == 0);
}

// (j) Keyframes: a FRESH IDR ends pending in admit (it is the anchor); a HELD IDR (hold > 300 ms)
// is a held picture like any other and keeps pending -- the fresh P behind it anchors. (Variant R:
// note_reference_sync does not touch pending. Under variant K only the held-IDR assertion flips.)
void test_fresh_idr_ends_pending_held_idr_keeps_it() {
  std::printf("[T-P11j] fresh IDR ends pending; a held IDR keeps it and the fresh P behind it anchors\n");
  Rig r;
  uint64_t t = 8000 * kMs;
  FrameGateInputs in{};
  CHECK(r.feedDecoded(t, t, true, 0, t + 10 * kMs) == FrameGateVerdict::Decode);
  // Held resume frame, decoded (non-key) -> pending.
  const uint64_t held1 = t + 830 * kMs;
  CHECK(r.feedDecoded(held1, t + 30 * kMs, false, t, held1) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);
  // A fresh IDR 5 ms later: admit anchors on it and ends pending; the decode keeps it ended.
  const uint64_t freshKeyAt = held1 + 5 * kMs;
  CHECK(r.feedDecoded(freshKeyAt, freshKeyAt - 10 * kMs, true, t + 30 * kMs, freshKeyAt) == FrameGateVerdict::Decode);
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(r.gate.presentAnchorFloorUs == freshKeyAt - 10 * kMs);
  // Another silence, another held resume frame -> pending again.
  const uint64_t held2 = freshKeyAt + 900 * kMs;
  const uint64_t held2Cap = freshKeyAt + 20 * kMs;
  CHECK(r.feedDecoded(held2, held2Cap, false, freshKeyAt - 10 * kMs, held2) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);
  // A HELD IDR 3 ms later (captured right after the held P, sent now: hold ~880 ms). Decoded as a
  // key. Under variant R pending stays: an old picture's IDR is not where the content is now.
  const uint64_t heldKeyAt = held2 + 3 * kMs;
  CHECK(r.feedDecoded(heldKeyAt, held2Cap + 16 * kMs, true, held2Cap, heldKeyAt) == FrameGateVerdict::Decode);
  CHECK(r.gate.resumeAnchorPending);  // variant R (FAILS on 1287883; flips only if Codex chooses variant K)
  CHECK(r.gate.lastDecodedKeyCaptureUs == held2Cap + 16 * kMs);  // the reference-chain anchor still moved
  // The fresh P behind it anchors and ends pending; no congestion.
  const uint64_t freshAt = heldKeyAt + 2 * kMs;
  FrameGateLag lag{};
  CHECK(r.feedDecoded(freshAt, freshAt - 15 * kMs, false, held2Cap + 16 * kMs, freshAt, &lag) == FrameGateVerdict::Decode);
  CHECK(!r.gate.resumeAnchorPending);
  CHECK(lag.decodeQueueLagEstimateUs < 100 * kMs);
  CHECK(r.gate.congestionState == ClientCongestionState::Normal);
  CHECK(r.sink.requests(1) == 0);
}"""),
# T-P11b: receiver-faithful
(r"""  const uint64_t silenceEnd = t + 800 * kMs;
  bool tripped = false;
  for (int i = 0; i < 16 && !tripped; ++i) {
    const uint64_t at = silenceEnd + i * 2 * kMs;              // dense arrival
    const uint64_t c = t + 400 * kMs + i * 16 * kMs;           // captures 400+ ms past the stuck anchor...
    const uint64_t sendUs = c + 800 * kMs;                     // ...each held 800 ms by the host
    (void)r.feed(at, c, false, t, false, nullptr, &in, false, sendUs);
    tripped = r.gate.congestionState != ClientCongestionState::Normal;
  }""",
 r"""  const uint64_t silenceEnd = t + 800 * kMs;
  bool tripped = false;
  for (int i = 0; i < 16 && !tripped; ++i) {
    const uint64_t at = silenceEnd + i * 2 * kMs;              // dense arrival
    const uint64_t c = t + 400 * kMs + i * 16 * kMs;           // captures 400+ ms past the stuck anchor...
    const uint64_t sendUs = c + 800 * kMs;                     // ...each held 800 ms by the host
    (void)r.feedDecoded(at, c, false, t, sendUs, nullptr, &in);  // decoded like the receiver does
    tripped = r.gate.congestionState != ClientCongestionState::Normal;
  }"""),
(r"""  test_held_resume_frame_does_not_anchor();
  test_backlog_after_silence_still_trips();
  test_small_host_hold_renderer_stalled_still_trips();""",
 r"""  test_held_resume_frame_does_not_anchor();
  test_held_resume_decoded_nonkey_keeps_pending();
  test_held_resume_accepted_empty_keeps_pending();
  test_fresh_idr_ends_pending_held_idr_keeps_it();
  test_backlog_after_silence_still_trips();
  test_small_host_hold_renderer_stalled_still_trips();"""),
])

# recovery S13: deterministic shape (a') with the present anchor pinned during the burst
edit(S + r'\viewer_udp_recovery_test.cpp', [
(r"""  if (!start_session(host, rig, plan)) { ++gFailures; return; }
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
                shape == 0 ? "old-real->fresh" : "old-real->IDR->fresh",""",
 r"""  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  // shape 2 = (a') the same old-real->fresh burst with the present anchor PINNED while the burst
  // is judged: the renderer has not shown the held frame yet, so nothing but the gate's own rule
  // can keep the fresh frames from reading as an 0.8 s backlog. Deterministic where shapes 0/1
  // depend on the present thread's timing (the sweep-load failure of 2026-09-07).
  for (int shape = 0; shape < 3; ++shape) {
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
    if (shape == 2) rig.pinPresentAnchor = true;
    (void)host.SendFrame(false, false, lastRealUs + 30000, false);  // the held picture, its true old stamp
    const uint64_t burstUs = qpc_now_us();
    (void)host.SendFrame(shape == 1, false, burstUs, shape == 1);
    for (int i = 1; i < 4; ++i) (void)host.SendFrame(false, false, burstUs + static_cast<uint64_t>(i) * 2000, false);
    if (shape == 2) {
      sleep_ms(60);  // the burst is judged with the anchor still pinned
      rig.pinPresentAnchor = false;
    }
    pump(host, rig, 700);
    const uint64_t requests = host.keyframe_requests() - requestsBefore;
    const uint64_t transitions = rig.gate.congestionTransitionCount - transitionsBefore;
    std::printf("  shape %s: congestion transitions +%llu, keyframe requests +%llu, heldResume=%llu, state %s\n",
                shape == 0 ? "old-real->fresh" : shape == 1 ? "old-real->IDR->fresh" : "old-real->fresh (present anchor pinned)","""),
])
print('step34 tests ok')
