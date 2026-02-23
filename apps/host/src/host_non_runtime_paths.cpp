#include "host_non_runtime_paths.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#include <vector>
#include <windows.h>

#include "audio_runtime.hpp"
#include "capture_bootstrap.hpp"
#include "capture_runtime.hpp"
#include "common/h264_rtp_packetizer.hpp"
#include "common/input_protocol.hpp"
#include "common/opus_rtp_packetizer.hpp"
#include "common/signaling_protocol.hpp"
#include "common/version.hpp"
#include "common/ws_rtc_signaling_client.hpp"
#include "mf_h264_encoder.hpp"
#if REMOTE60_HAS_REALTIME_MEDIA
#include "realtime_media_probe.hpp"
#endif

namespace remote60::host {
namespace {

std::mutex g_log_mutex;

std::string now_local_string() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto tt = clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &tt);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

void trace_log(const std::string& msg) {
  const std::string line = "[" + now_local_string() + "][host][pid=" + std::to_string(GetCurrentProcessId()) + "] " + msg;
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::cout << line << "\n";

  try {
    const std::filesystem::path dir = "logs";
    std::filesystem::create_directories(dir);
    std::ofstream ofs(dir / "host_trace.log", std::ios::app);
    ofs << line << "\n";
  } catch (...) {
  }
}

bool apply_input_event(const remote60::common::input::Event& e, bool dryRun) {
  if (e.type == remote60::common::input::EventType::MouseMove) {
    if (dryRun) return true;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = e.x;
    in.mi.dy = e.y;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    return SendInput(1, &in, sizeof(INPUT)) == 1;
  }
  if (e.type == remote60::common::input::EventType::KeyDown || e.type == remote60::common::input::EventType::KeyUp) {
    if (dryRun) return true;
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = static_cast<WORD>(e.keyCode);
    in.ki.dwFlags = (e.type == remote60::common::input::EventType::KeyUp) ? KEYEVENTF_KEYUP : 0;
    return SendInput(1, &in, sizeof(INPUT)) == 1;
  }
  return false;
}

int run_legacy_signaling_bootstrap() {
  using remote60::common::WsRtcSignalingClient;
  using remote60::common::signaling::Message;
  using remote60::common::signaling::MessageType;

  std::atomic<bool> got_offer = false;
  std::atomic<bool> sent_video_probe_answer = false;
  std::atomic<bool> sent_input_probe_answer = false;
  std::atomic<bool> handled_realtime_session_probe = false;
  const std::string signalingUrl = []() {
    const char* v = std::getenv("REMOTE60_SIGNALING_URL");
    return (v && *v) ? std::string(v) : std::string("ws://127.0.0.1:3000");
  }();

  WsRtcSignalingClient signaling;
  signaling.set_on_state([](const std::string& s) { trace_log(std::string("state=") + s); });
  signaling.set_on_message([&](const Message& m) {
    trace_log(std::string("recv=") + remote60::common::signaling::to_string(m.type) +
              " sdp=" + m.sdp + " requestId=" + m.requestId + " session=" + m.session);
    if (m.type == MessageType::Offer) {
      got_offer = true;
      Message answer;
      answer.type = MessageType::Answer;

      if (m.sdp == "video-probe") {
        remote60::common::H264RtpPacketizer p(1188);
        std::vector<uint8_t> annexb = {
            0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
            0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
            0x00,0x00,0x00,0x01,0x65};
        annexb.resize(annexb.size() + 3000, 0xaa);
        auto initial = p.build_initial_nalus_annexb(annexb);
        uint16_t seq = 100;
        auto pkts = p.packetize_annexb(annexb, 90000, seq);
        uint16_t seq2 = 500;
        auto initPkts = p.packetize_annexb(initial, 90000, seq2);

        std::ostringstream oss;
        oss << "video-ok packets=" << pkts.size() << " initialPackets=" << initPkts.size()
            << " initialBytes=" << initial.size();
        answer.sdp = oss.str();
        sent_video_probe_answer = true;
      } else if (m.sdp == "audio-probe") {
        answer.sdp = "audio-ok probe=ready";
      } else if (m.sdp == "input-probe") {
        remote60::common::input::Event ev;
        ev.type = remote60::common::input::EventType::MouseMove;
        ev.x = 0;
        ev.y = 0;
        const bool ok = apply_input_event(ev, false);
        answer.sdp = ok ? "input-ok sendinput=1" : "input-fail sendinput=0";
        sent_input_probe_answer = ok;
      } else if (m.sdp == "realtime-session-probe") {
        handled_realtime_session_probe = true;
#if REMOTE60_HAS_REALTIME_MEDIA
        remote60::host::RealtimeMediaProbeStats r;
        for (int attempt = 0; attempt < 3; ++attempt) {
          r = remote60::host::run_realtime_media_probe();
          if (r.detail == "ok" && r.videoRtpSent > 0 && r.audioRtpSent > 0) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
        std::ostringstream oss;
        oss << "realtime-session-ok detail=" << r.detail
            << " connected=" << r.loopbackConnected
            << " vSent=" << r.videoRtpSent
            << " aSent=" << r.audioRtpSent;
        answer.sdp = oss.str();
#else
        answer.sdp = "realtime-session-fail no_realtime_media";
#endif
      } else {
        answer.sdp = "pong-from-host";
      }

      answer.requestId = m.requestId;
      signaling.send_message(answer);
      trace_log("sent=answer sdp=" + answer.sdp + " requestId=" + answer.requestId);
    }
  });

  if (!signaling.connect(signalingUrl)) return 1;
  if (!signaling.register_role("host")) return 1;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    if (got_offer.load()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (got_offer.load()) {
    const auto grace = handled_realtime_session_probe.load() ? 1500 : 250;
    std::this_thread::sleep_for(std::chrono::milliseconds(grace));
  }

  signaling.disconnect();

  trace_log(std::string("boot complete version=") + remote60::common::version() +
            " got_offer=" + (got_offer.load() ? "1" : "0"));
  std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
  return got_offer.load() ? 0 : 2;
}

}  // namespace

std::optional<int> run_non_runtime_path(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--runtime-stream") return std::nullopt;
  if (argc > 1 && std::string(argv[1]) == "--runtime-server") return std::nullopt;

  if (argc > 1 && std::string(argv[1]) == "--capture-probe") {
    const auto r = remote60::host::probe_capture_stack();
    std::cout << "[host] capture_probe " << r.detail << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (r.d3d11Ok && r.wgcSupported) ? 0 : 3;
  }

  if (argc > 1 && std::string(argv[1]) == "--mf-enc-probe") {
    const auto e = remote60::host::run_mf_h264_encoder_probe();
    std::cout << "[host] mf_h264_probe detail=" << e.detail << " mfStartup=" << e.mfStartup
              << " mftCreated=" << e.mftCreated << " inputTypeSet=" << e.inputTypeSet
              << " outputTypeSet=" << e.outputTypeSet << " inputTypeCount=" << e.inputTypeCount
              << " outputTypeCount=" << e.outputTypeCount << " bitrateKbps=" << e.bitrateKbps
              << " gopFrames=" << e.gopFrames << " codecApiBitrateSet=" << e.codecApiBitrateSet
              << " codecApiGopSet=" << e.codecApiGopSet << " codecApiLowLatencySet=" << e.codecApiLowLatencySet
              << " beginStreaming=" << e.beginStreaming << " startOfStream=" << e.startOfStream
              << " processOutputNeedMoreInput=" << e.processOutputNeedMoreInput << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (e.detail == "ok") ? 0 : 5;
  }

  if (argc > 1 && std::string(argv[1]) == "--rtp-pack-probe") {
    remote60::common::H264RtpPacketizer p(1188);
    std::vector<uint8_t> annexb = {
        0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
        0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
        0x00,0x00,0x00,0x01,0x65};
    annexb.resize(annexb.size() + 3000, 0xaa);
    uint16_t seq = 100;
    auto packets = p.packetize_annexb(annexb, 90000, seq);
    auto initial = p.build_initial_nalus_annexb(annexb);
    uint16_t seq2 = 500;
    auto initPackets = p.packetize_annexb(initial, 90000, seq2);
    std::cout << "[host] rtp_pack_probe packets=" << packets.size();
    if (!packets.empty()) {
      std::cout << " firstSeq=" << packets.front().sequence << " lastSeq=" << packets.back().sequence
                << " lastMarker=" << packets.back().marker;
    }
    std::cout << " initialBytes=" << initial.size() << " initialPackets=" << initPackets.size() << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (packets.empty() || initPackets.empty()) ? 6 : 0;
  }

  if (argc > 1 && std::string(argv[1]) == "--rtp-size-probe") {
    constexpr uint32_t kLinkMtu = 1200;
    constexpr uint32_t kRtpHeader = 12;
    constexpr uint32_t kMaxPayload = kLinkMtu - kRtpHeader;

    remote60::common::H264RtpPacketizer vp(kMaxPayload);
    std::vector<uint8_t> annexb = {
        0x00,0x00,0x00,0x01,0x67,0x42,0x00,0x1f,
        0x00,0x00,0x00,0x01,0x68,0xce,0x06,0xe2,
        0x00,0x00,0x00,0x01,0x65};
    annexb.resize(annexb.size() + 3000, 0xaa);
    uint16_t vseq = 100;
    auto vpkts = vp.packetize_annexb(annexb, 90000, vseq);

    size_t vMin = SIZE_MAX, vMax = 0, vSum = 0;
    size_t vOver = 0;
    for (const auto& p : vpkts) {
      const size_t n = p.payload.size();
      vMin = (std::min)(vMin, n);
      vMax = (std::max)(vMax, n);
      vSum += n;
      if (n > kMaxPayload) ++vOver;
    }
    const double vAvg = vpkts.empty() ? 0.0 : static_cast<double>(vSum) / static_cast<double>(vpkts.size());

    remote60::common::OpusRtpPacketizer ap;
    size_t aMin = SIZE_MAX, aMax = 0, aSum = 0;
    size_t aOver = 0;
    uint16_t aseq = 1000;
    uint32_t ats = 48000;
    for (int i = 0; i < 20; ++i) {
      std::vector<uint8_t> fakeOpus(80, 0x5a);
      auto pkt = ap.packetize_20ms_frame(fakeOpus, aseq, ats);
      const size_t n = pkt.payload.size();
      aMin = (std::min)(aMin, n);
      aMax = (std::max)(aMax, n);
      aSum += n;
      if (n > kMaxPayload) ++aOver;
    }
    const double aAvg = static_cast<double>(aSum) / 20.0;

    std::cout << "[host] rtp_size_probe"
              << " mtu=" << kLinkMtu
              << " maxPayload=" << kMaxPayload
              << " videoPackets=" << vpkts.size()
              << " videoMin=" << (vpkts.empty() ? 0 : vMin)
              << " videoMax=" << vMax
              << " videoAvg=" << vAvg
              << " videoOverMtu=" << vOver
              << " audioPackets=20"
              << " audioMin=" << aMin
              << " audioMax=" << aMax
              << " audioAvg=" << aAvg
              << " audioOverMtu=" << aOver << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (vOver == 0 && aOver == 0) ? 0 : 12;
  }

  if (argc > 1 && std::string(argv[1]) == "--audio-loop-probe") {
    int seconds = 3;
    if (argc > 2) {
      const int parsed = std::atoi(argv[2]);
      seconds = (parsed < 1) ? 1 : parsed;
    }
    const auto a = remote60::host::run_audio_loopback_probe_seconds(seconds);
    std::cout << "[host] audio_loop_probe detail=" << a.detail << " deviceOk=" << a.deviceOk
              << " clientOk=" << a.clientOk << " loopbackStarted=" << a.loopbackStarted
              << " sampleRate=" << a.sampleRate << " channels=" << a.channels
              << " packets=" << a.packets << " pcmFrames=" << a.pcmFrames
              << " opusFrames20ms=" << a.opusFrames20ms << " opusBytes=" << a.opusBytes
              << " rtpPackets=" << a.rtpPackets << " firstSeq=" << a.firstSeq
              << " lastSeq=" << a.lastSeq << " firstTs=" << a.firstTimestamp
              << " lastTs=" << a.lastTimestamp << " elapsed=" << a.elapsedSec << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (a.detail == "ok") ? 0 : 7;
  }

  if (argc > 1 && std::string(argv[1]) == "--input-probe") {
    const std::string sample = "{\"type\":\"mouse_move\",\"x\":100,\"y\":240}";
    auto parsed = remote60::common::input::from_json(sample);
    std::cout << "[host] input_probe parsed=" << (parsed.has_value() ? 1 : 0);
    if (parsed) {
      auto re = remote60::common::input::to_json(*parsed);
      std::cout << " type=" << remote60::common::input::to_string(parsed->type)
                << " x=" << parsed->x << " y=" << parsed->y
                << " echo=" << re;
    }
    std::cout << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return parsed.has_value() ? 0 : 8;
  }

  if (argc > 1 && std::string(argv[1]) == "--realtime-media-probe") {
#if REMOTE60_HAS_REALTIME_MEDIA
    remote60::host::RealtimeMediaProbeStats r;
    for (int attempt = 0; attempt < 3; ++attempt) {
      r = remote60::host::run_realtime_media_probe();
      if (r.detail == "ok" && r.videoRtpSent > 0 && r.audioRtpSent > 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }
    std::ostringstream oss;
    oss << "realtime_media_probe detail=" << r.detail
        << " libdatachannelOk=" << r.libdatachannelOk
        << " opusOk=" << r.opusOk
        << " peerConnectionCreated=" << r.peerConnectionCreated
        << " dataChannelCreated=" << r.dataChannelCreated
        << " videoTrackCreated=" << r.videoTrackCreated
        << " audioTrackCreated=" << r.audioTrackCreated
        << " localDescriptionSet=" << r.localDescriptionSet
        << " loopbackConnected=" << r.loopbackConnected
        << " candidatesExchanged=" << r.candidatesExchanged
        << " videoRtpTried=" << r.videoRtpTried << " videoRtpSent=" << r.videoRtpSent
        << " audioRtpTried=" << r.audioRtpTried << " audioRtpSent=" << r.audioRtpSent
        << " opusLookahead=" << r.opusLookahead;
    trace_log(oss.str());
    std::cout << "[host] " << oss.str() << std::endl;
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (r.detail.rfind("ok", 0) == 0) ? 0 : 10;
#else
    std::cout << "[host] realtime_media_probe detail=disabled_missing_deps\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return 11;
#endif
  }

  if (argc > 1 && std::string(argv[1]) == "--system-probe") {
    const auto c = remote60::host::run_capture_loop_seconds(2);
    const auto a = remote60::host::run_audio_loopback_probe_seconds(2);
    const auto e = remote60::host::run_mf_h264_encoder_probe();
    remote60::common::input::Event ev;
    ev.type = remote60::common::input::EventType::MouseMove;
    const bool inputDryRunOk = apply_input_event(ev, true);

    std::cout << "[host] system_probe"
              << " captureDetail=" << c.detail
              << " timelineFps=" << c.timelineFps
              << " convertedFrames=" << c.convertedFrames
              << " audioDetail=" << a.detail
              << " audioRtpPackets=" << a.rtpPackets
              << " audioOpusFrames=" << a.opusFrames20ms
              << " mfDetail=" << e.detail
              << " mfInputTypeSet=" << e.inputTypeSet
              << " mfOutputTypeSet=" << e.outputTypeSet
              << " inputDryRunOk=" << inputDryRunOk << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (c.detail.rfind("ok", 0) == 0 && a.detail == "ok" && e.detail == "ok" && inputDryRunOk) ? 0 : 9;
  }

  if (argc > 1 && std::string(argv[1]) == "--capture-loop") {
    int seconds = 3;
    if (argc > 2) {
      const int parsed = std::atoi(argv[2]);
      seconds = (parsed < 1) ? 1 : parsed;
    }
    const auto s = remote60::host::run_capture_loop_seconds(seconds);
    std::cout << "[host] capture_loop detail=" << s.detail << " frames=" << s.frames
              << " callbacks=" << s.callbacks << " ticks=" << s.ticks
              << " freshTicks=" << s.freshTicks << " reusedTicks=" << s.reusedTicks
              << " idleTicks=" << s.idleTicks << " convertedFrames=" << s.convertedFrames
              << " convertFailures=" << s.convertFailures << " elapsed=" << s.elapsedSec
              << " captureFps=" << s.fps << " timelineFps=" << s.timelineFps
              << " targetFps=" << s.targetFps << " avgConvertMs=" << s.avgConvertMs << "\n";
    std::cout << "remote60_host boot ok (" << remote60::common::version() << ")\n";
    return (s.detail.rfind("ok", 0) == 0) ? 0 : 4;
  }

  return run_legacy_signaling_bootstrap();
}

}  // namespace remote60::host
