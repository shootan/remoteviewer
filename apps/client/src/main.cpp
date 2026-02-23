#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <windows.h>

#include "common/signaling_protocol.hpp"
#include "common/version.hpp"
#include "common/ws_rtc_signaling_client.hpp"
#include "realtime_runtime_client.hpp"

namespace {

enum class ReadyState {
  Init,
  Connected,
  Registered,
  PeerReady,
  OfferSent,
  Answered,
};

int run_single_attempt(bool videoProbe, bool audioProbe, bool inputProbe, bool realtimeSessionProbe) {
  using remote60::common::WsRtcSignalingClient;
  using remote60::common::signaling::Message;
  using remote60::common::signaling::MessageType;

  std::atomic<bool> got_answer = false;
  std::atomic<bool> peer_online = false;
  std::atomic<bool> registered = false;
  std::atomic<bool> video_probe_ok = false;
  std::atomic<bool> audio_probe_ok = false;
  std::atomic<bool> input_probe_ok = false;
  std::atomic<bool> realtime_session_ok = false;
  std::atomic<ReadyState> ready{ReadyState::Init};
  const std::string signalingUrl = []() {
    const char* v = std::getenv("REMOTE60_SIGNALING_URL");
    return (v && *v) ? std::string(v) : std::string("ws://127.0.0.1:3000");
  }();

  const std::string requestId = "cli-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64());

  WsRtcSignalingClient signaling;
  signaling.set_on_state([&](const std::string& s) {
    std::cout << "[client] state=" << s << "\n";
    if (s.rfind("connected", 0) == 0) ready = ReadyState::Connected;
  });
  signaling.set_on_message([&](const Message& m) {
    std::cout << "[client] recv=" << remote60::common::signaling::to_string(m.type);
    if (!m.requestId.empty()) std::cout << " requestId=" << m.requestId;
    if (!m.session.empty()) std::cout << " session=" << m.session;
    std::cout << "\n";

    if (m.type == MessageType::Error) {
      std::cout << "[client] error code=" << m.code << " reason=" << m.reason << "\n";
    }

    if (m.type == MessageType::Registered) {
      registered = true;
      ready = ReadyState::Registered;
    }
    if (m.type == MessageType::PeerState) {
      const bool online = m.peerOnline.value_or(false);
      peer_online = online;
      if (online && registered.load()) ready = ReadyState::PeerReady;
      std::cout << "[client] peerOnline=" << (online ? 1 : 0) << "\n";
    }
    if (m.type == MessageType::Answer) {
      got_answer = true;
      ready = ReadyState::Answered;
      std::cout << "[client] answer.sdp=" << m.sdp << "\n";
      if (videoProbe && m.sdp.rfind("video-ok", 0) == 0) video_probe_ok = true;
      if (audioProbe && m.sdp.rfind("audio-ok", 0) == 0) audio_probe_ok = true;
      if (inputProbe && m.sdp.rfind("input-ok", 0) == 0) input_probe_ok = true;
      if (realtimeSessionProbe && m.sdp.rfind("realtime-session-ok", 0) == 0) realtime_session_ok = true;
    }
  });

  if (!signaling.connect(signalingUrl)) return 101;
  if (!signaling.register_role("client")) return 102;

  const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (std::chrono::steady_clock::now() < readyDeadline) {
    if (registered.load() && peer_online.load()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!registered.load()) {
    std::cout << "[client] not_ready code=E_NOT_REGISTERED\n";
    signaling.disconnect();
    return 201;
  }
  if (!peer_online.load()) {
    std::cout << "[client] not_ready code=E_PEER_OFFLINE\n";
    signaling.disconnect();
    return 202;
  }

  Message offer;
  offer.type = MessageType::Offer;
  offer.requestId = requestId;
  offer.sdp = videoProbe ? "video-probe"
                         : (audioProbe ? "audio-probe"
                                       : (inputProbe ? "input-probe"
                                                     : (realtimeSessionProbe ? "realtime-session-probe"
                                                                            : "ping-from-client")));
  signaling.send_message(offer);
  ready = ReadyState::OfferSent;
  std::cout << "[client] sent=offer requestId=" << requestId << "\n";

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    if (got_answer.load()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  signaling.disconnect();

  if (!got_answer.load()) return 203;
  if (videoProbe && !video_probe_ok.load()) return 204;
  if (audioProbe && !audio_probe_ok.load()) return 205;
  if (inputProbe && !input_probe_ok.load()) return 206;
  if (realtimeSessionProbe && !realtime_session_ok.load()) return 207;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--runtime-view") {
    int seconds = 10;
    if (argc > 2) {
      const int parsed = std::atoi(argv[2]);
      seconds = (parsed < 1) ? 1 : parsed;
    }
    const auto s = remote60::client::run_runtime_view_client(seconds);
    std::cout << "[client] runtime_view detail=" << s.detail
              << " webrtcReady=" << s.webrtcReady
              << " connected=" << s.connected
              << " gotVideoTrack=" << s.gotVideoTrack
              << " gotAudioTrack=" << s.gotAudioTrack
              << " inputChannelOpen=" << s.inputChannelOpen
              << " videoRtpPackets=" << s.videoRtpPackets
              << " videoFrames=" << s.videoFrames
              << " audioRtpPackets=" << s.audioRtpPackets
              << " inputEventsSent=" << s.inputEventsSent
              << " inputAcks=" << s.inputAcks << "\n";
    std::cout << "remote60_client boot ok (" << remote60::common::version() << ")\n";
    return (s.detail == "ok") ? 0 : 208;
  }

  const bool videoProbe = (argc > 1 && std::string(argv[1]) == "--video-e2e-probe");
  const bool audioProbe = (argc > 1 && std::string(argv[1]) == "--audio-e2e-probe");
  const bool inputProbe = (argc > 1 && std::string(argv[1]) == "--input-e2e-probe");
  const bool realtimeSessionProbe = (argc > 1 && std::string(argv[1]) == "--realtime-session-probe");

  constexpr int kMaxAttempts = 5;
  int rc = 203;
  for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
    std::cout << "[client] attempt=" << attempt << "/" << kMaxAttempts << "\n";
    rc = run_single_attempt(videoProbe, audioProbe, inputProbe, realtimeSessionProbe);
    if (rc == 0) break;
    if (attempt < kMaxAttempts) {
      std::cout << "[client] reconnect_in=2000ms reason=" << rc << "\n";
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }

  std::cout << "remote60_client boot ok (" << remote60::common::version() << ")\n";
  return rc;
}
