#pragma once

// Progress heartbeat of the receive thread, and the session-liveness verdict built on it.
//
// Role:    RecvLiveness -- what the recv thread is doing right now (stage + since when) and when
//          it last moved a datagram, an assembled AU, a decode and a publish; SessionLiveness* --
//          the UI-thread watchdog's pure decision over a sample of those clocks plus the control
//          channel's state. Written for history #390 item 5: the 09-05 11:20 freeze (last present
//          11:20:42.749, control peer-lost 11:20:49.566, host still sending 60 fps) left no way to
//          tell a recv thread wedged in decode/publish from a link that stopped delivering -- both
//          end in peer-lost, because the control tunnel's Tick/OnPacket run on the recv thread.
//          The verdict names the difference; the watchdog logs it and ends a provably dead session.
// Thread:  the recv thread writes RecvLiveness (relaxed atomics, one writer); the UI timer reads
//          them and owns `watch`; evaluate_session_liveness is pure (viewer_liveness_test.cpp).
// Input:   qpc timestamps from the receive path; control connected / tunnel closed flags.
// Output:  a SessionLivenessVerdict per poll: recv stalled / link silent / session dead, with ages.
// Callers: VideoReceiver (writes), poll_session_liveness (viewer_session_watchdog.cpp, reads).
//
// Standalone on purpose (no viewer_common.hpp): the verdict test links nothing else.

#include <atomic>
#include <cstdint>

namespace remote60::native_poc::viewer {

enum class RecvStage : uint32_t {
  Starting = 0,  // the thread has not entered its loop yet
  Recv = 1,      // in / around recv() (25 ms timeout on UDP)
  Control = 2,   // control tunnel tick / packet dispatch
  Assembly = 3,  // assembler push / in-order delivery
  Decode = 4,    // decode_access_unit
  Publish = 5,   // FrameBuffer publish + paint request
  Exited = 6,    // the loop ended
};

inline const char* recv_stage_name(RecvStage s) {
  switch (s) {
    case RecvStage::Starting: return "starting";
    case RecvStage::Recv: return "recv";
    case RecvStage::Control: return "control";
    case RecvStage::Assembly: return "assembly";
    case RecvStage::Decode: return "decode";
    case RecvStage::Publish: return "publish";
    case RecvStage::Exited: return "exited";
    default: return "unknown";
  }
}

// UI-thread bookkeeping of the watchdog (poll cadence, log rate limits, control history).
struct SessionWatchdogState {
  uint64_t nextPollUs = 0;
  bool controlEverConnected = false;
  uint64_t controlGoneSinceUs = 0;  // first poll that saw control gone after it had been up
  uint64_t lastStallLogUs = 0;
  uint64_t lastSilentLogUs = 0;
  bool deadReported = false;
  uint64_t streamExpectedSinceUs = 0;
};

struct RecvLiveness {
  // cross-thread: recv writes (relaxed), UI reads.
  std::atomic<uint64_t> loopIterations{0};     // receive-loop passes, timeouts included
  std::atomic<uint64_t> lastLoopUs{0};         // the last pass
  std::atomic<uint64_t> lastDatagramUs{0};     // last datagram of any kind
  std::atomic<uint64_t> lastVideoChunkUs{0};   // last accepted video chunk
  std::atomic<uint64_t> lastAssembledUs{0};    // last AU handed to the gate
  std::atomic<uint64_t> lastDecodeReturnUs{0}; // last decode_access_unit return (ok or not)
  std::atomic<uint64_t> lastPublishUs{0};      // last frame handed to the FrameBuffer
  std::atomic<uint32_t> stage{static_cast<uint32_t>(RecvStage::Starting)};
  std::atomic<uint64_t> stageEnterUs{0};
  // UI thread only.
  SessionWatchdogState watch;

  void Enter(RecvStage s, uint64_t nowUs) {
    stage.store(static_cast<uint32_t>(s), std::memory_order_relaxed);
    stageEnterUs.store(nowUs, std::memory_order_relaxed);
  }
  RecvStage current_stage() const {
    return static_cast<RecvStage>(stage.load(std::memory_order_relaxed));
  }
};

// One poll's inputs, taken by the watchdog on the UI thread.
struct SessionLivenessSample {
  uint64_t nowUs = 0;
  RecvStage stage = RecvStage::Starting;
  uint64_t stageEnterUs = 0;
  uint64_t loopIterations = 0;
  uint64_t lastDatagramUs = 0;
  uint64_t lastAssembledUs = 0;
  uint64_t lastDecodeReturnUs = 0;
  uint64_t lastPublishUs = 0;
  bool controlConnected = false;       // ctx.control.connected
  bool tunnelClosed = false;           // control over UDP and the tunnel reported closed
  uint64_t controlGoneSinceUs = 0;     // 0 = control is up (or never was)
  bool streamExpected = false;
  bool controlRequired = false;
  uint64_t streamExpectedSinceUs = 0;
};

struct SessionLivenessConfig {
  uint64_t stallUs = 2000000;        // the recv thread inside one stage this long = stalled
  uint64_t silentLinkUs = 3000000;   // no datagram this long while control is up = silent link
  uint64_t deadSessionUs = 5000000;  // control gone AND no publish this long = dead; 0 = never
  uint64_t outputTimeoutUs = 15000000;
  uint64_t stuckThreadUs = 8000000;
};

struct SessionLivenessVerdict {
  bool recvStalled = false;  // the recv thread has not left its stage for stallUs
  bool linkSilent = false;   // the loop cycles, nothing arrives, control believes it is up
  bool sessionDead = false;  // control gone for good and video not progressing
  uint64_t stageAgeUs = 0;
  uint64_t datagramAgeUs = 0;
  uint64_t publishAgeUs = 0;
  uint64_t controlGoneUs = 0;
};

inline uint64_t liveness_age_us(uint64_t nowUs, uint64_t thenUs) {
  return (thenUs > 0 && nowUs >= thenUs) ? (nowUs - thenUs) : 0;
}

inline SessionLivenessVerdict evaluate_session_liveness(const SessionLivenessSample& s,
                                                        const SessionLivenessConfig& c) {
  SessionLivenessVerdict v;
  v.stageAgeUs = liveness_age_us(s.nowUs, s.stageEnterUs);
  v.datagramAgeUs = liveness_age_us(s.nowUs, s.lastDatagramUs);
  v.publishAgeUs = liveness_age_us(s.nowUs, s.lastPublishUs);
  v.controlGoneUs = liveness_age_us(s.nowUs, s.controlGoneSinceUs);
  const bool running = s.stage != RecvStage::Starting && s.stage != RecvStage::Exited;
  // Wedged: the thread has sat in one stage past the limit. In Recv that means recv() is not
  // returning although its timeout is 25 ms; in Decode / Publish the decoder or a lock holds it.
  v.recvStalled = running && s.stageEnterUs > 0 && v.stageAgeUs >= c.stallUs;
  // Silent: the loop keeps cycling on timeouts but nothing has arrived for a while, although
  // control still believes it is connected -- delivery stopped, not the thread.
  v.linkSilent = running && !v.recvStalled && s.controlConnected && s.lastDatagramUs > 0 &&
                 v.datagramAgeUs >= c.silentLinkUs;
  // Dead: control is gone for good (the control thread exited; on the tunnel that is peer-lost,
  // and nothing reconnects it) for deadSessionUs, and in that time no frame was published (or the
  // recv loop itself ended). Such a session answers nothing and never will; keeping its last
  // picture up is the field's "had to restart the client".
  const bool controlGone = s.controlGoneSinceUs > 0 && (!s.controlConnected || s.tunnelClosed);
  const bool videoStopped = (s.stage == RecvStage::Exited) ||
                            (s.lastPublishUs == 0) || (v.publishAgeUs >= c.deadSessionUs);
  v.sessionDead = c.deadSessionUs > 0 && controlGone && v.controlGoneUs >= c.deadSessionUs &&
                  videoStopped;
  const uint64_t outputBase = s.lastPublishUs > s.streamExpectedSinceUs
                                 ? s.lastPublishUs : s.streamExpectedSinceUs;
  const bool outputLost = s.streamExpected && outputBase > 0 &&
                         liveness_age_us(s.nowUs, outputBase) >= c.outputTimeoutUs;
  const bool threadLost = v.recvStalled && v.stageAgeUs >= c.stuckThreadUs;
  if (c.deadSessionUs > 0 && (outputLost || threadLost)) v.sessionDead = true;
  if (c.deadSessionUs > 0 && s.controlRequired && controlGone && v.controlGoneUs >= c.deadSessionUs)
    v.sessionDead = true;
  return v;
}

}  // namespace remote60::native_poc::viewer
