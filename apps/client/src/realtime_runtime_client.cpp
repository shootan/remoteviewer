#include "realtime_runtime_client.hpp"

#if REMOTE60_HAS_REALTIME_MEDIA
#include <rtc/rtc.hpp>
#endif

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/input_protocol.hpp"
#include "common/signaling_protocol.hpp"
#include "common/ws_rtc_signaling_client.hpp"

namespace remote60::client {
namespace {

std::string trim_copy(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
  return s.substr(b, e - b);
}

std::string env_or(const char* key, const std::string& fallback) {
  const char* v = std::getenv(key);
  if (!v || !*v) return fallback;
  return std::string(v);
}

rtc::Configuration make_rtc_config_from_env() {
  rtc::Configuration cfg;
  const std::string raw = env_or("REMOTE60_ICE_SERVERS", "");
  std::string token;
  token.reserve(128);
  auto flush_token = [&]() {
    const std::string t = trim_copy(token);
    token.clear();
    if (t.empty()) return;
    try {
      cfg.iceServers.emplace_back(t);
    } catch (...) {
    }
  };
  for (char c : raw) {
    if (c == ',' || c == ';') {
      flush_token();
    } else {
      token.push_back(c);
    }
  }
  flush_token();
  return cfg;
}

}  // namespace

RuntimeViewStats run_runtime_view_client(int seconds) {
  RuntimeViewStats out;
#if !REMOTE60_HAS_REALTIME_MEDIA
  out.detail = "realtime_disabled";
  return out;
#else
  using remote60::common::WsRtcSignalingClient;
  using remote60::common::signaling::Message;
  using remote60::common::signaling::MessageType;

  rtc::InitLogger(rtc::LogLevel::Warning, nullptr);
  rtc::Configuration cfg = make_rtc_config_from_env();
  const std::string signalingUrl = env_or("REMOTE60_SIGNALING_URL", "ws://127.0.0.1:3000");
  auto pc = std::make_shared<rtc::PeerConnection>(cfg);
  out.webrtcReady = static_cast<bool>(pc);
  if (!pc) {
    out.detail = "pc_create_failed";
    return out;
  }

  std::atomic<bool> connected{false};
  std::atomic<bool> gotVideo{false};
  std::atomic<bool> gotAudio{false};
  std::atomic<bool> inputOpen{false};
  std::atomic<uint64_t> packets{0};
  std::atomic<uint64_t> frames{0};
  std::atomic<uint64_t> audioPackets{0};
  std::atomic<uint64_t> inputEventsSent{0};
  std::atomic<uint64_t> inputAcks{0};
  std::atomic<bool> signalingReady{false};
  std::atomic<bool> registered{false};
  std::atomic<bool> peerOnline{false};
  std::atomic<bool> offerStarted{false};
  std::atomic<bool> localOfferCreated{false};
  struct PendingCandidate {
    std::string candidate;
    std::string mid;
    std::optional<int> mline;
  };
  std::mutex pendingMu;
  std::string pendingOffer;
  std::vector<PendingCandidate> pendingCandidates;
  std::shared_ptr<rtc::DataChannel> dc;

  WsRtcSignalingClient sig;
  auto bind_video_track_handlers = [&](const std::shared_ptr<rtc::Track>& tr, const char* source) {
    if (!tr) return;
    std::cout << "[client][runtime] bind_video_track source=" << source
              << " isOpen=" << (tr->isOpen() ? 1 : 0)
              << " mid=" << tr->mid() << "\n";
    if (tr->isOpen()) gotVideo = true;
    tr->onOpen([&, tr]() {
      gotVideo = true;
      std::cout << "[client][runtime] track onOpen fired isOpen=" << (tr->isOpen() ? 1 : 0)
                << " mid=" << tr->mid() << "\n";
    });
    tr->onMessage([&](rtc::message_variant data) {
      if (const auto* b = std::get_if<rtc::binary>(&data)) {
        if (b->size() >= 12) {
          ++packets;
          const uint8_t m = static_cast<uint8_t>((*b)[1]);
          if ((m & 0x80) != 0) ++frames;
        }
      }
    });
  };
  auto bind_audio_track_handlers = [&](const std::shared_ptr<rtc::Track>& tr, const char* source) {
    if (!tr) return;
    std::cout << "[client][runtime] bind_audio_track source=" << source
              << " isOpen=" << (tr->isOpen() ? 1 : 0)
              << " mid=" << tr->mid() << "\n";
    if (tr->isOpen()) gotAudio = true;
    tr->onOpen([&, tr]() {
      gotAudio = true;
      std::cout << "[client][runtime] audio track onOpen fired isOpen=" << (tr->isOpen() ? 1 : 0)
                << " mid=" << tr->mid() << "\n";
    });
    tr->onMessage([&](rtc::message_variant data) {
      if (const auto* b = std::get_if<rtc::binary>(&data)) {
        if (b->size() >= 12) ++audioPackets;
      }
    });
  };

  sig.set_on_message([&](const Message& m) {
    if (m.type == MessageType::Registered) {
      registered = true;
    } else if (m.type == MessageType::PeerState) {
      peerOnline = m.peerOnline.value_or(false);
    } else if (m.type == MessageType::Answer && m.sdp.rfind("v=0", 0) == 0) {
      try {
        std::filesystem::create_directories("logs");
        std::ofstream ofs("logs/runtime_answer_from_host_latest.sdp", std::ios::trunc);
        ofs << m.sdp;
      } catch (...) {
      }
      std::cout << "[client][runtime] answer_sdp has_sendonly=" << (m.sdp.find("a=sendonly") != std::string::npos ? 1 : 0)
                << " has_sendrecv=" << (m.sdp.find("a=sendrecv") != std::string::npos ? 1 : 0)
                << " has_inactive=" << (m.sdp.find("a=inactive") != std::string::npos ? 1 : 0) << "\n";
      try { pc->setRemoteDescription(rtc::Description(m.sdp, "answer")); } catch (...) {}
    } else if (m.type == MessageType::Ice && !m.candidate.empty()) {
      try {
        std::string mid = m.candidateMid;
        if (mid.empty() && m.candidateMLineIndex.has_value()) mid = std::to_string(*m.candidateMLineIndex);
        if (mid.empty()) mid = "0";
        std::cout << "[client][runtime] ice_apply remote_candidate mid=" << mid
                  << " mline=" << (m.candidateMLineIndex.has_value() ? std::to_string(*m.candidateMLineIndex) : "")
                  << " bytes=" << m.candidate.size() << "\n";
        pc->addRemoteCandidate(rtc::Candidate(m.candidate, mid));
      } catch (...) {}
    }
  });

  pc->onStateChange([&](rtc::PeerConnection::State s) {
    if (s == rtc::PeerConnection::State::Connected) connected = true;
  });

  pc->onTrack([&](std::shared_ptr<rtc::Track> tr) {
    if (!tr) return;
    const std::string mid = tr->mid();
    std::cout << "[client][runtime] onTrack fired isOpen=" << (tr->isOpen() ? 1 : 0)
              << " mid=" << mid << "\n";
    if (mid == "audio") {
      bind_audio_track_handlers(tr, "onTrack");
    } else {
      bind_video_track_handlers(tr, "onTrack");
    }
  });

  pc->onLocalDescription([&](rtc::Description d) {
    if (d.type() != rtc::Description::Type::Offer) return;
    localOfferCreated = true;
    const std::string sdp = std::string(d);
    try {
      std::filesystem::create_directories("logs");
      std::ofstream ofs("logs/runtime_offer_from_client_latest.sdp", std::ios::trunc);
      ofs << sdp;
    } catch (...) {
    }
    if (signalingReady.load()) {
      Message offer; offer.type = MessageType::Offer; offer.sdp = sdp;
      if (!sig.send_message(offer)) {
        std::lock_guard<std::mutex> lk(pendingMu);
        pendingOffer = sdp;
      }
    } else {
      std::lock_guard<std::mutex> lk(pendingMu);
      pendingOffer = sdp;
    }
  });
  pc->onLocalCandidate([&](rtc::Candidate c) {
    const std::string cand = std::string(c);
    std::string mid = c.mid();
    if (mid.empty()) mid = "0";
    std::optional<int> mline;
    try {
      size_t pos = 0;
      const int parsed = std::stoi(mid, &pos);
      if (pos == mid.size()) mline = parsed;
    } catch (...) {
    }
    std::cout << "[client][runtime] ice_send local_candidate mid=" << mid
              << " mline=" << (mline.has_value() ? std::to_string(*mline) : "")
              << " bytes=" << cand.size() << "\n";
    if (signalingReady.load() && registered.load()) {
      Message ice;
      ice.type = MessageType::Ice;
      ice.candidate = cand;
      ice.candidateMid = mid;
      ice.candidateMLineIndex = mline;
      sig.send_message(ice);
    } else {
      std::lock_guard<std::mutex> lk(pendingMu);
      pendingCandidates.push_back(PendingCandidate{cand, mid, mline});
    }
  });

  rtc::Description::Video v("video", rtc::Description::Direction::RecvOnly);
  v.addH264Codec(96);
  auto videoTrack = pc->addTrack(v);
  if (!videoTrack) {
    out.detail = "video_track_add_failed";
    return out;
  }
  bind_video_track_handlers(videoTrack, "local_recvonly_track");

  rtc::Description::Audio a("audio", rtc::Description::Direction::RecvOnly);
  a.addOpusCodec(111);
  auto audioTrack = pc->addTrack(a);
  if (!audioTrack) {
    out.detail = "audio_track_add_failed";
    return out;
  }
  bind_audio_track_handlers(audioTrack, "local_recvonly_track");

  dc = pc->createDataChannel("input");
  if (dc) {
    dc->onOpen([&]() {
      inputOpen = true;
      remote60::common::input::Event ev;
      ev.type = remote60::common::input::EventType::MouseMove;
      ev.x = 100;
      ev.y = 100;
      dc->send(remote60::common::input::to_json(ev));
      ++inputEventsSent;
      dc->send("input_ping");
    });
    dc->onMessage([&](rtc::message_variant data) {
      std::string text;
      if (const auto* s = std::get_if<std::string>(&data)) {
        text = *s;
      } else if (const auto* b = std::get_if<rtc::binary>(&data)) {
        text.assign(reinterpret_cast<const char*>(b->data()), b->size());
      } else {
        return;
      }
      if (text.find("input_ack") != std::string::npos) ++inputAcks;
    });
  }

  if (!sig.connect(signalingUrl) || !sig.register_role("client")) {
    out.detail = "signaling_failed";
    return out;
  }
  signalingReady = true;

  const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < readyDeadline) {
    if (registered.load() && peerOnline.load()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  if (!localOfferCreated.load() && !offerStarted.exchange(true)) {
    try { pc->setLocalDescription(); } catch (...) {}
  }

  {
    std::lock_guard<std::mutex> lk(pendingMu);
    if (!pendingOffer.empty()) {
      Message offer; offer.type = MessageType::Offer; offer.sdp = pendingOffer;
      sig.send_message(offer);
      pendingOffer.clear();
    }
    for (const auto& cand : pendingCandidates) {
      Message ice;
      ice.type = MessageType::Ice;
      ice.candidate = cand.candidate;
      ice.candidateMid = cand.mid;
      ice.candidateMLineIndex = cand.mline;
      sig.send_message(ice);
    }
    pendingCandidates.clear();
  }

  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  auto nextInputTick = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  int inputTick = 0;
  while (std::chrono::steady_clock::now() < until) {
    if (dc && inputOpen.load() && std::chrono::steady_clock::now() >= nextInputTick) {
      remote60::common::input::Event ev;
      ev.type = remote60::common::input::EventType::MouseMove;
      ev.x = 120 + ((inputTick % 5) * 20);
      ev.y = 120 + ((inputTick % 3) * 15);
      dc->send(remote60::common::input::to_json(ev));
      ++inputEventsSent;
      ++inputTick;
      nextInputTick += std::chrono::seconds(1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  sig.disconnect();
  out.connected = connected.load();
  out.gotVideoTrack = gotVideo.load();
  out.gotAudioTrack = gotAudio.load();
  out.inputChannelOpen = inputOpen.load();
  out.videoRtpPackets = packets.load();
  out.videoFrames = frames.load();
  out.audioRtpPackets = audioPackets.load();
  out.inputEventsSent = inputEventsSent.load();
  out.inputAcks = inputAcks.load();
  out.detail = (out.connected && out.videoFrames > 0) ? "ok" : "no_video_frames";
  return out;
#endif
}

}  // namespace remote60::client
