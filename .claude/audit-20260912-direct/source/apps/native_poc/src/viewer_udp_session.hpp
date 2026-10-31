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

// The Hello the Windows viewer sends: the same exchange the Android session performs (F-09), at
// this viewer's cadence (up to ten seconds of Hello, 200 ms per wait, 50 ms between tries), the
// directory capability riding along, and -- since the Windows NACK wiring -- the request for
// selective retransmit whenever the env policy allows it.
inline remote60::native_poc::UdpHelloOptions viewer_udp_hello_options(const std::string& authToken,
                                                                       bool videoNackEnabled) {
  remote60::native_poc::UdpHelloOptions hello;
  hello.authToken = authToken;
  hello.budgetMs = 10000;
  hello.sliceMaxMs = 200;
  hello.retrySleepMs = 50;
  hello.requestNack = videoNackEnabled;
  return hello;
}

// Keep what the HelloAck said: the recv thread only NACKs against a host that acknowledged
// kUdpFeatureVideoNack (an old host never sets it and is never sent one).
inline void viewer_apply_udp_hello_ack(uint32_t ackFeatures, bool videoNackEnabled,
                                       SessionState& session) {
  session.udpHelloAckFeatures = ackFeatures;
  session.hostSupportsNack =
      videoNackEnabled && (ackFeatures & remote60::native_poc::kUdpFeatureVideoNack) != 0;
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
