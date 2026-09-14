#pragma once

// The viewer's frame gate: the per-frame decisions of the recv thread, with time and the measured
// lags as arguments so they can be driven from a test (viewer_frame_gate_test.cpp).
//
// Role:    admit() -- stale-frame drop with the reference-chain anchor, the congestion state
//          machine (Normal / Congested / Recovering: catch-up entry with re-entry throttle, keyframe
//          exit, healthy-streak recovery, recovery timeout), and the keyframe-wait drop;
//          note_decode_failure / note_timestamp_overflow / note_decode_ok / note_reference_sync /
//          note_decode_empty / clear_empty_streak -- the post-decode bookkeeping (decoder-wedge
//          rebuild threshold, empty-output streak recovery); plus the queue-depth histogram, the
//          congestion transition (with its log line), the congestion stats fields and the timeline
//          alignment helper.
// Thread:  recv only. The decisions mutate FrameGateState / RecvStats and act on the decoder and
//          the keyframe request path only through FrameGateSink, which the receiver implements
//          (and the test records).
// Input:   FrameGateInputs (frame header facts, packet time, present anchor, picker suppression).
// Output:  a FrameGateVerdict per frame, state/counter updates, sink calls, log lines.
// Callers: VideoReceiver::process_h264_frame / run_udp / flush_stats_if_due.
//
// Bodies are the corresponding blocks of process_h264_frame (native_video_client_main.cpp ->
// viewer_video_receiver.cpp), verbatim except that the frame/time locals come from `in`/`lag`, the
// decoder and keyframe calls go through the sink, and the early `return true`s became verdicts
// (viewer split refactor Phase 2-2).

#include "viewer_common.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_recv_stats.hpp"

namespace remote60::native_poc::viewer {

// What the gate needs to do to the decoder / control path; the receiver implements it against
// DecoderState + request_keyframe, the unit test records the calls.
struct FrameGateSink {
  virtual ~FrameGateSink() = default;
  virtual void reset_decoder() = 0;                    // dec.decoder.reset()
  virtual bool rebuild_decoder() = 0;                  // dec.decoder.initialize(decoderW, decoderH, fpsHint)
  virtual void request_keyframe(uint16_t reason) = 0;  // viewer_log request_keyframe(reason)
};

// The facts about one frame the gate reads (filled by process_h264_frame).
struct FrameGateInputs {
  uint64_t captureQpcUs = 0;
  uint32_t seq = 0;
  bool keyFrame = false;
  uint64_t packetNowUs = 0;
  uint64_t recvGapUs = 0;           // from note_packet()
  uint64_t presentedCapUs = 0;      // FrameBuffer::lastPresentedCaptureUs
  uint64_t decodedCapUs = 0;        // last real decoder output, independent of UI paint progress
  // The host's send stamp (wire). With captureQpcUs -- both host clock -- it says how long the
  // host held this picture before sending it; 0 = unknown (older host / test). (P11)
  uint64_t sendQpcUs = 0;
  bool catchupSuppressed = false;   // picker visible || packetNowUs < catchupSuppressUntilUs
  // The host re-encoded its cached picture (kick / static refresh). Decoded and shown like any
  // frame, but it must not feed the congestion trigger or the queue-depth histogram: its capture
  // stamp is the kick time, not a measurement of the pipeline. (F-10.)
  bool synthetic = false;
};

// The two lag estimates admit() computes for this frame (also fed to the post-decode notes).
struct FrameGateLag {
  uint64_t streamLagUs = 0;
  uint64_t decodeQueueLagEstimateUs = 0;
};

enum class FrameGateVerdict : uint8_t {
  Decode = 0,            // hand the access unit to the decoder
  DropStale = 1,         // behind the present anchor / the newest capture
  DropCongested = 2,     // catch-up: non-key frame while Congested
  DropWaitingKeyframe = 3,  // waiting for the next IDR (caller flushes the stats line)
};

class FrameGate {
 public:
  FrameGate(FrameGateState& gate, RecvStats& st, FrameGateSink& sink) : gate(gate), st(st), sink(sink) {}

  // Packet arrival bookkeeping: returns the frame's recvGapUs and resets the lag-trigger streak on
  // sparse arrival. A real frame's gap is measured since the last REAL frame (kicks and static
  // refreshes do not shorten it); a synthetic frame's gap is since any frame.
  uint64_t note_packet(uint64_t packetNowUs, bool synthetic = false);
  // Pre-decode gating (stale / congestion / keyframe wait). `lag` receives the estimates.
  FrameGateVerdict admit(const FrameGateInputs& in, FrameGateLag* lag);
  // decode_access_unit failed.
  void note_decode_failure(const FrameGateInputs& in, const FrameGateLag& lag);
  // decode_access_unit succeeded: the wedge streak is clear.
  void note_decode_ok();
  // the decoder reported a timestamp-queue overflow.
  void note_timestamp_overflow(const FrameGateInputs& in, const FrameGateLag& lag);
  // the frame decoded: the keyframe wait ends, an IDR advances the reference anchor.
  void note_reference_sync(const FrameGateInputs& in);
  // decode_access_unit produced no output frame.
  void note_decode_empty(const FrameGateInputs& in, const FrameGateLag& lag);
  // a frame came out: the empty-output streak ends.
  void clear_empty_streak();
  // The clock: called by the receive loop after every datagram AND on every receive timeout. While
  // the gate waits for an IDR (waitForKeyFrame or Congested) it re-asks on a backoff schedule
  // (reason 7, recovery_timer) so recovery does not depend on frames arriving; otherwise it clears
  // the wait bookkeeping. Cheap when idle.
  // `repairInProgress`: a NACK retransmit is still being chased for the AU that would otherwise
  // need this IDR; the re-ask waits so the two recoveries do not race (Codex condition 2).
  void tick(uint64_t nowUs, bool repairInProgress = false);

  // formerly VideoReceiver members, verbatim
  uint32_t queue_depth_frames(uint64_t lagUs);
  void sample_queue_depth(uint64_t lagUs);
  void transition_congestion_state(ClientCongestionState nextState, uint64_t nowUs, const char* reason,
                                   uint64_t streamLagUs, uint64_t decodeQueueLagEstimateUs, uint32_t seq);
  void append_congestion_fields(std::ostream& os);
  static uint64_t aligned_lag_us(uint64_t remoteTsUs, uint64_t localNowUs,
                                 bool& timelineReady, uint64_t& remoteBaseUs, uint64_t& localBaseUs);

 private:
  FrameGateState& gate;
  RecvStats& st;
  FrameGateSink& sink;
};

}  // namespace remote60::native_poc::viewer
