#pragma once

// The UDP video session's negotiation, in one place for the product and its receive-path test.
//
// Role:    what connect_media_socket sends in the Hello and keeps from the HelloAck, and the
//          receive timeout it arms afterwards. The Windows viewer shipped 0.2.95/96 without ever
//          requesting kUdpFeatureVideoNack because these three lines lived only in the startup
//          step nothing exercised; the receive-path integration test (viewer_udp_recovery_test)
//          now calls the same functions, so a product default that regresses fails the test
//          instead of hiding behind a test-only handshake.
// Thread:  main (startup); the test's rig thread.
// Input:   the directory capability, the env policy, the HelloAck feature bits.
// Output:  UdpHelloOptions; SessionState.udpHelloAckFeatures / hostSupportsNack; SO_RCVTIMEO.
// Callers: viewer_startup.cpp (connect_media_socket), viewer_udp_recovery_test.cpp.

#include <string>

#include "native_video_client_tcp_control.hpp"
#include "viewer_common.hpp"
#include "viewer_session_state.hpp"

namespace remote60::native_poc::viewer {

// REMOTE60_NATIVE_VIDEO_NACK's default: on. The receive-path test passes this same constant, so a
// changed default is visible on the fake host's wire (the Hello's feature bits) in that test.
constexpr bool kVideoNackEnabledDefault = true;

/**
 * How long the viewer keeps saying hello.
 *
 * Thirty seconds, and it is a UX CEILING rather than a fix. It does not make a host answer;
 * it stops the viewer giving up while the answer is still coming. What actually makes the
 * host answer is the server waking it until it collects the capability (C2 F3).
 *
 * Thirty is not a round number picked for comfort. It is the capability's own life:
 *
 *   server  PUNCH_TTL_MS      30s   a pendingPunch expires unless a heartbeat collects it
 *   host    authorizedPeers_  30s   from the moment it collects one
 *
 * So the whole connect budget lines up like this, and the total is what the ceiling is set
 * against:
 *
 *   observe      6 x 300ms   1.8s   DirectoryRendezvous::Observe
 *   /api/connect 8s max             directory_session_client, one 409 re-observe possible
 *   PunchAny     4s                 all candidates at once, the relay answers at 2.5s
 *   hello        <= 30s             this, bounded by the capability TTL above
 *
 * Past thirty seconds there is nothing left to say hello WITH -- the capability the hello
 * carries has expired at both ends -- so a longer budget would spend the time re-sending
 * something that can only be refused. What has to happen instead is a fresh /api/connect,
 * which is the shell's retry, and the fence above is what stops the old attempt racing it.
 */
constexpr uint32_t kViewerHelloBudgetMs = 30000;

// The Hello the Windows viewer sends: the same exchange the Android session performs (F-09),
// 200 ms per wait, 50 ms between tries, the directory capability riding along, and -- since
// the Windows NACK wiring -- the request for selective retransmit whenever the env policy
// allows it.
inline remote60::native_poc::UdpHelloOptions viewer_udp_hello_options(const std::string& authToken,
                                                                       bool videoNackEnabled) {
  remote60::native_poc::UdpHelloOptions hello;
  hello.authToken = authToken;
  hello.budgetMs = kViewerHelloBudgetMs;
  hello.sliceMaxMs = 200;
  hello.retrySleepMs = 50;
  hello.requestNack = videoNackEnabled;
  // Control resume (item 8, C3). Asked for unconditionally: it costs one bit, the host
  // answers old viewers exactly as before, and a viewer that did not ask could not be
  // helped by the feature at all.
  hello.requestControlResume = true;
  return hello;
}

// Keep what the HelloAck said: the recv thread only NACKs against a host that acknowledged
// kUdpFeatureVideoNack (an old host never sets it and is never sent one).
inline void viewer_apply_udp_hello_ack(uint32_t ackFeatures, bool videoNackEnabled,
                                       SessionState& session) {
  session.udpHelloAckFeatures = ackFeatures;
  session.hostSupportsNack =
      videoNackEnabled && (ackFeatures & remote60::native_poc::kUdpFeatureVideoNack) != 0;
  session.hostSupportsControlResume =
      (ackFeatures & remote60::native_poc::kUdpFeatureControlResume) != 0;
}

// The receive timeout every UDP session runs with (direct and tunnelled alike): the clock of the
// NACK rounds, the in-order hold, the keyframe recovery timer and the control tunnel's
// retransmits on a quiet link. The direct path used to block forever.
inline bool viewer_arm_udp_recv_timeout(SOCKET sock, uint32_t timeoutMs) {
  const DWORD value = timeoutMs;
  return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value),
                    sizeof(value)) == 0;
}

}  // namespace remote60::native_poc::viewer
