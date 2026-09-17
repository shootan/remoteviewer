// Unit test for the session-liveness verdict (viewer_recv_liveness.hpp): a stalled recv thread, a
// silent link and a dead session are told apart from the recv heartbeat and the control state,
// with the ages the watchdog logs. No threads, no sockets.
//
// Build: remote60_viewer_liveness_test (CMake). Run: prints "viewer_liveness_test: PASS", exit 0.

#include <chrono>
#include <cstdio>
#include <thread>

#include "viewer_recv_liveness.hpp"
#include "viewer_thread_join.hpp"

using namespace remote60::native_poc::viewer;

namespace {

int gFailures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
      ++gFailures;                                                     \
    }                                                                  \
  } while (0)

constexpr uint64_t kMs = 1000;
constexpr uint64_t kS = 1000 * kMs;

SessionLivenessSample healthy(uint64_t now) {
  SessionLivenessSample s;
  s.nowUs = now;
  s.stage = RecvStage::Recv;
  s.stageEnterUs = now - 10 * kMs;
  s.loopIterations = 1000;
  s.lastDatagramUs = now - 5 * kMs;
  s.lastAssembledUs = now - 20 * kMs;
  s.lastDecodeReturnUs = now - 20 * kMs;
  s.lastPublishUs = now - 20 * kMs;
  s.controlConnected = true;
  s.tunnelClosed = false;
  s.controlGoneSinceUs = 0;
  return s;
}

}  // namespace

int main() {
  const SessionLivenessConfig cfg;  // 2 s stall, 3 s silent, 5 s dead
  const uint64_t now = 100 * kS;

  std::printf("[L1] healthy: nothing flagged\n");
  {
    const auto v = evaluate_session_liveness(healthy(now), cfg);
    CHECK(!v.recvStalled && !v.linkSilent && !v.sessionDead);
    CHECK(v.datagramAgeUs == 5 * kMs);
  }

  std::printf("[L2] recv thread wedged in decode for 3 s: stalled, not dead while control is up\n");
  {
    auto s = healthy(now);
    s.stage = RecvStage::Decode;
    s.stageEnterUs = now - 3 * kS;
    s.lastDatagramUs = now - 3 * kS;
    s.lastPublishUs = now - 3 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(v.recvStalled);
    CHECK(v.stageAgeUs == 3 * kS);
    CHECK(!v.linkSilent);  // a stall is not a silent link
    CHECK(!v.sessionDead);
  }

  std::printf("[L3] recv() not returning for 3 s (25 ms timeout): stalled too\n");
  {
    auto s = healthy(now);
    s.stage = RecvStage::Recv;
    s.stageEnterUs = now - 3 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(v.recvStalled);
  }

  std::printf("[L4] loop cycling, no datagram for 4 s, control up: silent link\n");
  {
    auto s = healthy(now);
    s.lastDatagramUs = now - 4 * kS;
    s.lastPublishUs = now - 4 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(v.linkSilent);
    CHECK(!v.recvStalled);
    CHECK(!v.sessionDead);
    CHECK(v.datagramAgeUs == 4 * kS);
  }

  std::printf("[L5] control gone (tunnel peer-lost) 6 s, no publish 6 s: dead\n");
  {
    auto s = healthy(now);
    s.controlConnected = false;
    s.tunnelClosed = true;
    s.controlGoneSinceUs = now - 6 * kS;
    s.lastDatagramUs = now - 6 * kS;
    s.lastPublishUs = now - 6 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(v.sessionDead);
    CHECK(v.controlGoneUs == 6 * kS);
    CHECK(!v.linkSilent);  // control is not up, so this is not the silent-link case
  }

  std::printf("[L6] control gone 6 s but frames still publish: not dead\n");
  {
    auto s = healthy(now);
    s.controlConnected = false;
    s.controlGoneSinceUs = now - 6 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(!v.sessionDead);
  }

  std::printf("[L7] control gone only 3 s: not dead yet\n");
  {
    auto s = healthy(now);
    s.controlConnected = false;
    s.controlGoneSinceUs = now - 3 * kS;
    s.lastPublishUs = now - 6 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(!v.sessionDead);
  }

  std::printf("[L8] recv loop exited + control gone 6 s: dead even with a recent publish\n");
  {
    auto s = healthy(now);
    s.stage = RecvStage::Exited;
    s.controlConnected = false;
    s.controlGoneSinceUs = now - 6 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(v.sessionDead);
    CHECK(!v.recvStalled);  // an exited loop is not a stall
  }

  std::printf("[L9] never connected: control absence is not a death; deadSessionUs=0 never kills\n");
  {
    auto s = healthy(now);
    s.controlConnected = false;
    s.controlGoneSinceUs = 0;  // the watchdog only stamps this after control had been up
    s.lastPublishUs = 0;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(!v.sessionDead);
    auto s2 = healthy(now);
    s2.controlConnected = false;
    s2.tunnelClosed = true;
    s2.controlGoneSinceUs = now - 60 * kS;
    s2.lastPublishUs = now - 60 * kS;
    SessionLivenessConfig off = cfg;
    off.deadSessionUs = 0;
    CHECK(!evaluate_session_liveness(s2, off).sessionDead);
    CHECK(evaluate_session_liveness(s2, cfg).sessionDead);
  }

  std::printf("[L10] a UAC pause: video stops but control keeps answering -> silent link at most, never dead\n");
  {
    auto s = healthy(now);
    s.lastDatagramUs = now - 20 * kS;
    s.lastPublishUs = now - 20 * kS;
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(v.linkSilent);
    CHECK(!v.sessionDead);
  }

  std::printf("[J1] join_with_timeout: a thread that does not return within the timeout is reported, not waited for\n");
  {
    auto s = healthy(now);
    s.streamExpected = true; s.streamExpectedSinceUs = now - 20 * kS;
    s.lastPublishUs = now - 20 * kS;
    CHECK(evaluate_session_liveness(s, cfg).sessionDead);
    s.streamExpected = false;  // picker / secure desktop is not a missing output episode
    CHECK(!evaluate_session_liveness(s, cfg).sessionDead);
    // This used to assert the opposite -- "video cannot hide dead input control" -- and end the
    // session after deadSessionUs even with frames arriving. That rule is what threw people out of
    // working sessions: a few seconds of uplink silence is all it takes for the control tunnel to
    // report peer-lost. The intent survives with a deadline instead of an instant: video may hide
    // dead control for a while, but not past controlGoneWithVideoUs.
    s.controlRequired = true; s.controlConnected = false;
    s.controlGoneSinceUs = now - 6 * kS; s.lastPublishUs = now - kMs;
    CHECK(!evaluate_session_liveness(s, cfg).sessionDead);
    s.controlGoneSinceUs = now - 31 * kS;
    CHECK(evaluate_session_liveness(s, cfg).sessionDead);
    s = healthy(now); s.stage = RecvStage::Decode; s.stageEnterUs = now - 9 * kS;
    CHECK(evaluate_session_liveness(s, cfg).sessionDead);
  }
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
    auto wedged = healthy(now);
    wedged.stage = RecvStage::Decode;
    wedged.stageEnterUs = now - 6 * kS;
    wedged.lastPublishUs = now - 6 * kS;
    CHECK(evaluate_session_liveness(wedged, cfg).sessionDead);
    wedged.stage = RecvStage::Recv; // a normally idle source is not a decoder wedge
    CHECK(!evaluate_session_liveness(wedged, cfg).sessionDead);
  }
  // ---------------------------------------------------------------- the bounce, and its bound
  //
  // The field case: the uplink goes quiet for a few seconds, the control tunnel runs out of
  // retransmits and reports peer-lost, and the host drops its end -- while video keeps arriving
  // and decoding perfectly. Ending the session here is what the user experiences as being thrown
  // out of something that was working.
  std::printf("[L13] control lost while VIDEO KEEPS ARRIVING: the session is kept\n");
  {
    auto s = healthy(now);
    s.controlConnected = false;
    s.tunnelClosed = true;
    s.controlRequired = true;            // true for every real session (udp tunnel or control port)
    s.controlGoneSinceUs = now - 8 * kS; // well past deadSessionUs
    s.lastPublishUs = now - 30 * kMs;    // ...but frames are still being published
    const auto v = evaluate_session_liveness(s, cfg);
    CHECK(!v.sessionDead);
    CHECK(v.controlGoneUs == 8 * kS);
  }

  std::printf("[L14] ...but not forever: control gone far longer, with video, still ends it\n");
  {
    // The bound. A picture nobody can click on is not worth keeping indefinitely, so the
    // relaxation has a ceiling rather than being "never notice a dead control channel".
    auto s = healthy(now);
    s.controlConnected = false;
    s.tunnelClosed = true;
    s.controlRequired = true;
    s.controlGoneSinceUs = now - 31 * kS;
    s.lastPublishUs = now - 30 * kMs;  // video still fine
    CHECK(evaluate_session_liveness(s, cfg).sessionDead);
  }

  std::printf("[L15] real death: control gone AND video stopped is still caught quickly\n");
  {
    auto s = healthy(now);
    s.controlConnected = false;
    s.tunnelClosed = true;
    s.controlRequired = true;
    s.controlGoneSinceUs = now - 6 * kS;
    s.lastPublishUs = now - 6 * kS;  // nothing arriving either
    CHECK(evaluate_session_liveness(s, cfg).sessionDead);
  }

  std::printf("[L16] negative controls around the kept session\n");
  {
    // A session that never published anything is not "video still arriving".
    auto never = healthy(now);
    never.controlConnected = false;
    never.tunnelClosed = true;
    never.controlRequired = true;
    never.controlGoneSinceUs = now - 6 * kS;
    never.lastPublishUs = 0;
    CHECK(evaluate_session_liveness(never, cfg).sessionDead);

    // And the long output timeout still ends a session whose picture stopped, control or no.
    auto stalledOutput = healthy(now);
    stalledOutput.streamExpected = true;
    stalledOutput.streamExpectedSinceUs = now - 20 * kS;
    stalledOutput.lastPublishUs = now - 20 * kS;
    CHECK(evaluate_session_liveness(stalledOutput, cfg).sessionDead);
  }

  if (gFailures == 0) {
    std::printf("viewer_liveness_test: PASS\n");
    return 0;
  }
  std::printf("viewer_liveness_test: FAIL (%d)\n", gFailures);
  return 1;
}
