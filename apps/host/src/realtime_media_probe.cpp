#include "realtime_media_probe.hpp"

#include <opus/opus.h>
#include <rtc/rtc.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>

#include "common/h264_rtp_packetizer.hpp"
#include "common/opus_rtp_packetizer.hpp"

namespace remote60::host {
namespace {

std::vector<uint8_t> build_rtp_packet(uint8_t payloadType, bool marker, uint16_t seq, uint32_t ts, uint32_t ssrc,
                                      const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out;
  out.resize(12 + payload.size());
  out[0] = 0x80;
  out[1] = static_cast<uint8_t>((marker ? 0x80 : 0x00) | (payloadType & 0x7F));
  out[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(seq & 0xFF);
  out[4] = static_cast<uint8_t>((ts >> 24) & 0xFF);
  out[5] = static_cast<uint8_t>((ts >> 16) & 0xFF);
  out[6] = static_cast<uint8_t>((ts >> 8) & 0xFF);
  out[7] = static_cast<uint8_t>(ts & 0xFF);
  out[8] = static_cast<uint8_t>((ssrc >> 24) & 0xFF);
  out[9] = static_cast<uint8_t>((ssrc >> 16) & 0xFF);
  out[10] = static_cast<uint8_t>((ssrc >> 8) & 0xFF);
  out[11] = static_cast<uint8_t>(ssrc & 0xFF);
  std::copy(payload.begin(), payload.end(), out.begin() + 12);
  return out;
}

}  // namespace

RealtimeMediaProbeStats run_realtime_media_probe() {
  RealtimeMediaProbeStats out;

  std::shared_ptr<rtc::PeerConnection> sender;
  std::shared_ptr<rtc::PeerConnection> receiver;
  std::shared_ptr<rtc::Track> vtrack;
  std::shared_ptr<rtc::Track> atrack;
  std::shared_ptr<rtc::Track> rvtrack;
  std::shared_ptr<rtc::Track> ratrack;

  std::atomic<bool> senderConnected{false};
  std::atomic<bool> receiverConnected{false};
  std::atomic<bool> videoTrackOpen{false};
  std::atomic<bool> audioTrackOpen{false};
  std::atomic<bool> receiverRemoteSet{false};
  std::atomic<bool> senderRemoteSet{false};
  std::atomic<bool> answerApplied{false};
  std::atomic<uint64_t> candCount{0};
  std::mutex candMutex;
  std::vector<rtc::Candidate> pendingToSender;
  std::vector<rtc::Candidate> pendingToReceiver;

  try {
    rtc::InitLogger(rtc::LogLevel::Warning, nullptr);
    rtc::Configuration cfg;
    sender = std::make_shared<rtc::PeerConnection>(cfg);
    receiver = std::make_shared<rtc::PeerConnection>(cfg);
    out.peerConnectionCreated = static_cast<bool>(sender) && static_cast<bool>(receiver);
    out.libdatachannelOk = out.peerConnectionCreated;

    // Deterministic offer/answer flow to avoid signaling-state races.
    sender->onLocalDescription([&](rtc::Description d) {
      if (d.type() == rtc::Description::Type::Offer) {
        out.localDescriptionSet = true;
        const auto sdp = std::string(d);
        {
          std::ofstream ofs("logs/realtime_offer.sdp", std::ios::trunc);
          ofs << sdp;
        }
        receiver->setRemoteDescription(d);
        receiverRemoteSet = true;
        {
          std::lock_guard<std::mutex> lock(candMutex);
          for (const auto& pc : pendingToReceiver) receiver->addRemoteCandidate(pc);
          pendingToReceiver.clear();
        }
        receiver->setLocalDescription();  // create answer exactly once after offer
      }
    });
    receiver->onLocalDescription([&](rtc::Description d) {
      if (d.type() == rtc::Description::Type::Answer) {
        {
          std::ofstream ofs("logs/realtime_answer.sdp", std::ios::trunc);
          ofs << std::string(d);
        }
        sender->setRemoteDescription(d);
        senderRemoteSet = true;
        {
          std::lock_guard<std::mutex> lock(candMutex);
          for (const auto& pc : pendingToSender) sender->addRemoteCandidate(pc);
          pendingToSender.clear();
        }
        answerApplied = true;
      }
    });

    sender->onLocalCandidate([&](rtc::Candidate c) {
      ++candCount;
      if (receiverRemoteSet.load()) {
        receiver->addRemoteCandidate(c);
      } else {
        std::lock_guard<std::mutex> lock(candMutex);
        pendingToReceiver.push_back(c);
      }
    });
    receiver->onLocalCandidate([&](rtc::Candidate c) {
      ++candCount;
      if (senderRemoteSet.load()) {
        sender->addRemoteCandidate(c);
      } else {
        std::lock_guard<std::mutex> lock(candMutex);
        pendingToSender.push_back(c);
      }
    });

    sender->onStateChange([&](rtc::PeerConnection::State s) {
      if (s == rtc::PeerConnection::State::Connected) senderConnected = true;
    });
    receiver->onStateChange([&](rtc::PeerConnection::State s) {
      if (s == rtc::PeerConnection::State::Connected) receiverConnected = true;
    });

    // Force m-line alignment by pre-creating recvonly transceivers on receiver.
    rtc::Description::Video recvVideo("video", rtc::Description::Direction::RecvOnly);
    recvVideo.addH264Codec(96);
    rvtrack = receiver->addTrack(recvVideo);

    rtc::Description::Audio recvAudio("audio", rtc::Description::Direction::RecvOnly);
    recvAudio.addOpusCodec(111);
    ratrack = receiver->addTrack(recvAudio);

    rtc::Description::Video video("video", rtc::Description::Direction::SendRecv);
    video.addH264Codec(96);
    video.addSSRC(0x22334455, "video-cname", "remote60", "v0");
    vtrack = sender->addTrack(video);

    rtc::Description::Audio audio("audio", rtc::Description::Direction::SendRecv);
    audio.addOpusCodec(111);
    audio.addSSRC(0x11223344, "audio-cname", "remote60", "a0");
    atrack = sender->addTrack(audio);

    out.videoTrackCreated = static_cast<bool>(vtrack);
    out.audioTrackCreated = static_cast<bool>(atrack);

    if (vtrack) {
      vtrack->onOpen([&]() { videoTrackOpen = true; });
    }
    if (atrack) {
      atrack->onOpen([&]() { audioTrackOpen = true; });
    }

    // Create data channel only after media transceivers are in place.
    // Otherwise auto-offer may race and omit media m-lines.
    auto dc = sender->createDataChannel("input");
    out.dataChannelCreated = static_cast<bool>(dc);

    // Generate a single deterministic offer now that tracks/datachannel are all configured.
    sender->setLocalDescription();
  } catch (...) {
    out.detail = "libdatachannel_init_failed";
    return out;
  }

  int err = 0;
  OpusEncoder* enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
  if (err != OPUS_OK || !enc) {
    out.detail = "opus_encoder_create_failed";
    return out;
  }
  int lookahead = 0;
  if (opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead)) == OPUS_OK) out.opusLookahead = lookahead;
  opus_encoder_destroy(enc);
  out.opusOk = true;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    if (senderConnected.load() && receiverConnected.load()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  out.loopbackConnected = senderConnected.load() && receiverConnected.load();
  out.candidatesExchanged = candCount.load();

  bool senderHasMedia = false;
  bool receiverHasMedia = false;
  const auto mediaDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < mediaDeadline) {
    const bool vOpen = videoTrackOpen.load() || ((vtrack != nullptr) ? vtrack->isOpen() : false);
    const bool aOpen = audioTrackOpen.load() || ((atrack != nullptr) ? atrack->isOpen() : false);
    videoTrackOpen = vOpen;
    audioTrackOpen = aOpen;
    senderHasMedia = sender ? sender->hasMedia() : false;
    receiverHasMedia = receiver ? receiver->hasMedia() : false;
    if (answerApplied.load() && senderConnected.load() && receiverConnected.load() && vOpen && aOpen) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  bool rtpPushException = false;
  remote60::common::H264RtpPacketizer h264(1188);
  std::vector<uint8_t> annexb = {
      0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
      0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
      0x00,0x00,0x00,0x01,0x65};
  annexb.resize(annexb.size() + 1800, 0xaa);

  remote60::common::OpusRtpPacketizer opusP;

  int streamOpusErr = OPUS_OK;
  OpusEncoder* streamEnc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &streamOpusErr);
  if (!streamEnc || streamOpusErr != OPUS_OK) {
    out.detail = "opus_stream_encoder_create_failed";
    return out;
  }
  opus_encoder_ctl(streamEnc, OPUS_SET_BITRATE(64000));
  opus_encoder_ctl(streamEnc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

  if (out.loopbackConnected && answerApplied.load()) {
    uint16_t vseq = 3000;
    uint16_t aseq = 4000;
    uint32_t vts = 90000;
    uint32_t ats = 48000;

    const int kAudioFrame = 960;  // 20ms @ 48k
    std::vector<float> pcm(kAudioFrame * 2, 0.0f);
    std::vector<uint8_t> encoded(4000);
    double phase = 0.0;

    const auto runtimeUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    int tick = 0;
    while (std::chrono::steady_clock::now() < runtimeUntil) {
      // Audio: 20ms pacing, real Opus encoding path
      {
        for (int i = 0; i < kAudioFrame; ++i) {
          const float s = static_cast<float>(0.10 * std::sin(phase));
          phase += (2.0 * 3.14159265358979323846 * 440.0) / 48000.0;
          pcm[i * 2 + 0] = s;
          pcm[i * 2 + 1] = s;
        }

        const int n = opus_encode_float(streamEnc, pcm.data(), kAudioFrame, encoded.data(), static_cast<opus_int32>(encoded.size()));
        if (n > 0) {
          std::vector<uint8_t> payload(encoded.begin(), encoded.begin() + n);
          auto op = opusP.packetize_20ms_frame(payload, aseq, ats);
          ++out.audioRtpTried;
          auto raw = build_rtp_packet(111, false, op.sequence, op.timestamp, 0x11223344, op.payload);
          try {
            if ((audioTrackOpen.load() || (atrack && atrack->isOpen())) && atrack != nullptr &&
                atrack->send(reinterpret_cast<const rtc::byte*>(raw.data()), raw.size())) {
              ++out.audioRtpSent;
            }
          } catch (...) {
            rtpPushException = true;
          }
        }
      }

      // Video: every 100ms (~10fps)
      if ((tick % 5) == 0) {
        auto vpkts = h264.packetize_annexb(annexb, vts, vseq);
        vts += 9000;
        for (const auto& p : vpkts) {
          ++out.videoRtpTried;
          auto raw = build_rtp_packet(96, p.marker, p.sequence, p.timestamp, 0x22334455, p.payload);
          try {
            if ((videoTrackOpen.load() || (vtrack && vtrack->isOpen())) && vtrack != nullptr &&
                vtrack->send(reinterpret_cast<const rtc::byte*>(raw.data()), raw.size())) {
              ++out.videoRtpSent;
            }
          } catch (...) {
            rtpPushException = true;
          }
        }
      }

      ++tick;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  if ((out.videoRtpSent == 0 || out.audioRtpSent == 0) && out.loopbackConnected && answerApplied.load()) {
    const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < retryDeadline && (out.videoRtpSent == 0 || out.audioRtpSent == 0)) {
      const bool vOpen = (vtrack != nullptr) && vtrack->isOpen();
      const bool aOpen = (atrack != nullptr) && atrack->isOpen();
      videoTrackOpen = videoTrackOpen.load() || vOpen;
      audioTrackOpen = audioTrackOpen.load() || aOpen;
      if (vOpen && out.videoRtpSent == 0) {
        uint16_t seqRetry = 6000;
        auto retryPkts = h264.packetize_annexb(annexb, 180000, seqRetry);
        for (const auto& p : retryPkts) {
          auto raw = build_rtp_packet(96, p.marker, p.sequence, p.timestamp, 0x22334455, p.payload);
          ++out.videoRtpTried;
          if (vtrack->send(reinterpret_cast<const rtc::byte*>(raw.data()), raw.size())) ++out.videoRtpSent;
        }
      }
      if (aOpen && out.audioRtpSent == 0) {
        uint16_t aseqRetry = 7000;
        uint32_t atsRetry = 96000;
        const int kAudioFrame = 960;
        std::vector<float> pcm(kAudioFrame * 2, 0.0f);
        std::vector<uint8_t> encoded(4000);
        double phase = 0.0;
        for (int i = 0; i < 6; ++i) {
          for (int j = 0; j < kAudioFrame; ++j) {
            const float s = static_cast<float>(0.10 * std::sin(phase));
            phase += (2.0 * 3.14159265358979323846 * 440.0) / 48000.0;
            pcm[j * 2 + 0] = s;
            pcm[j * 2 + 1] = s;
          }
          const int n = opus_encode_float(streamEnc, pcm.data(), kAudioFrame, encoded.data(), static_cast<opus_int32>(encoded.size()));
          if (n <= 0) continue;
          std::vector<uint8_t> payload(encoded.begin(), encoded.begin() + n);
          auto op = opusP.packetize_20ms_frame(payload, aseqRetry, atsRetry);
          auto raw = build_rtp_packet(111, false, op.sequence, op.timestamp, 0x11223344, op.payload);
          ++out.audioRtpTried;
          if (atrack->send(reinterpret_cast<const rtc::byte*>(raw.data()), raw.size())) ++out.audioRtpSent;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
  }

  opus_encoder_destroy(streamEnc);

  if (!out.peerConnectionCreated || !out.dataChannelCreated || !out.videoTrackCreated || !out.audioTrackCreated ||
      !out.localDescriptionSet) {
    out.detail = "webrtc_objects_incomplete";
  } else if (rtpPushException) {
    out.detail = "ok_rtp_push_exception";
  } else if (!out.loopbackConnected) {
    out.detail = "ok_not_connected";
  } else if (!answerApplied.load()) {
    out.detail = "ok_answer_not_applied";
  } else if (!videoTrackOpen.load() || !audioTrackOpen.load()) {
    out.detail = "ok_tracks_not_open";
  } else if (!senderHasMedia || !receiverHasMedia) {
    out.detail = "ok_media_sections_not_ready";
  } else if (out.videoRtpSent == 0 && out.audioRtpSent == 0) {
    out.detail = "ok_no_rtp_sent";
  } else {
    out.detail = "ok";
  }

  return out;
}

}  // namespace remote60::host
