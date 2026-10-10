// Encoder-bakeoff r4 -- REAL send-path trace. MEASUREMENT-ONLY, test-only target, not shipped.
//
// The r2 "product_trace" was a single-threaded wire MODEL (retracted). This assembles the REAL leaf
// components of the product send path and runs them over a loopback socket, because the production
// encode_send_h264 stage itself is not independently instantiable (it needs a live HostContext with
// ~20 state structs + WGC/DXGI capture), and the five host_*_e2e_test targets only spawn
// GNLinkStream.exe as a black box (verified). Real components used here, unchanged:
//   - H264Encoder (encode_frame, scripted NV12 scene)                 [mf_h264_codec]
//   - decide_encode_admission (the real pre-encode skip gate)         [host_encode_admission.hpp]
//   - SenderState + WireLimiter + the real sender thread StartThread  [host_encoded_sender]
//     (chunking, FEC, NACK cache, token-bucket wire cap, sendto)
//   - UdpH264FrameAssembler receiver (real reassembly)                [native_video_client_shared_core]
//   - RateGovernor (real OnSecond ladder)                             [host_rate_governor.hpp]
// NOT reused (and so NOT claimed as product behaviour): the HostContext orchestration and
// stats_tick_h264 (both need the un-instantiable HostContext, and the tick's clock is hard-wired to
// qpc). The governor is fed the PRODUCT's generated-load estimate formula
// (txAUbytes + skips*avgSentFrameBytes; host_stage_stats_h264.cpp:183,208) computed here from the
// real per-second counters on a REAL elapsed clock. This is an estimate-formula replication, flagged.
//
// cap ON (default) arms the real WireLimiter at the target; cap OFF genuinely disables the limiter
// (no re-injected R limit -- the sender's own pacing applies), same scripted supply either way.
// No renderer is run, so receiver timing is reported as PUBLISH/DECODE-ready frame gap, NOT display.
// This PC is AMD -> results are an AMD-path observation. No product default changed.
//
// Usage: remote60_host_encoder_realpath_trace [rateKbps=3000] [fps=30] [phaseSec=8] [cap=on|off]

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "bakeoff_common.hpp"
#include "host_args.hpp"
#include "host_encode_admission.hpp"
#include "host_encoded_sender.hpp"
#include "host_main_loop_mailbox.hpp"
#include "host_net_io.hpp"
#include "host_rate_governor.hpp"
#include "host_session.hpp"
#include "native_video_client_shared_core.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace remote60::native_poc;

namespace {

EncodedSendItem make_item(uint32_t seq, uint64_t gen, const std::vector<uint8_t>& bytes, bool key,
                          uint64_t mediaEpoch, uint32_t fps) {
  EncodedSendItem it;
  it.bytes = bytes;
  it.keyFrame = key;
  it.frameIntervalUs = 1'000'000ULL / (fps ? fps : 30);
  it.enqueueUs = qpc_now_us();
  it.mediaEpoch = mediaEpoch;
  it.udpHdr.magic = kMagic;
  it.udpHdr.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  it.udpHdr.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  it.udpHdr.seq = seq;
  it.udpHdr.codec = static_cast<uint16_t>(UdpCodec::H264);
  it.udpHdr.flags = key ? 0x1u : 0u;
  it.udpHdr.width = static_cast<uint16_t>(bakeoff::kW);
  it.udpHdr.height = static_cast<uint16_t>(bakeoff::kH);
  it.udpHdr.payloadSize = static_cast<uint32_t>(bytes.size());
  it.udpHdr.streamGeneration = gen;
  return it;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const uint32_t rateKbps = argc > 1 ? (uint32_t)std::atoi(argv[1]) : 3000u;
  const uint32_t fps = argc > 2 ? (uint32_t)std::max(1, std::atoi(argv[2])) : 30u;
  const uint32_t phaseSec = argc > 3 ? (uint32_t)std::max(2, std::atoi(argv[3])) : 8u;
  const bool capOn = !(argc > 4 && std::string(argv[4]) == "off");
  const uint32_t bitrate = rateKbps * 1000u;
  const uint32_t keyintFrames = 10u * fps;
  const uint32_t mtu = 1200;
  const uint64_t gen = 7, kEpoch = 1;

  std::printf("host_encoder_realpath_trace (REAL encoder+admission+SenderState+WireLimiter+assembler; "
              "AMD path): rate=%uk fps=%u phase=%us cap=%s, idle->motion->idle\n",
              rateKbps, fps, phaseSec, capOn ? "ON" : "OFF(real, limiter disabled)");

  WSADATA wsa{}; WSAStartup(MAKEWORD(2, 2), &wsa);
  MFStartup(MF_VERSION);
  _putenv_s("REMOTE60_NATIVE_MAX_QP", "");            // product default ceiling; governor moves it live
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", "mft_hw");

  // --- real loopback receiver socket ---
  SOCKET rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in rxAddr{}; rxAddr.sin_family = AF_INET; rxAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); rxAddr.sin_port = 0;
  bind(rx, reinterpret_cast<sockaddr*>(&rxAddr), sizeof(rxAddr));
  int alen = sizeof(rxAddr); getsockname(rx, reinterpret_cast<sockaddr*>(&rxAddr), &alen);
  int rcvbuf = 8 * 1024 * 1024; setsockopt(rx, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcvbuf), sizeof(rcvbuf));
  u_long nb = 1; ioctlsocket(rx, FIONBIO, &nb);
  SOCKET tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  // --- real receiver thread: reassemble AUs, record publish-ready frame arrival wall times ---
  std::atomic<bool> rxStop{false};
  std::mutex gapMu; std::vector<uint64_t> deliveryUs; std::atomic<uint64_t> wireBytes{0}, datagrams{0};
  std::thread rxThread([&] {
    UdpH264FrameAssembler asm_;
    char buf[2048];
    while (!rxStop.load(std::memory_order_relaxed)) {
      bool any = false;
      for (;;) {
        const int r = recv(rx, buf, sizeof(buf), 0);
        if (r <= 0) break;
        any = true;
        wireBytes.fetch_add((uint64_t)r, std::memory_order_relaxed);
        datagrams.fetch_add(1, std::memory_order_relaxed);
        const uint64_t nowUs = qpc_now_us();
        // hold off (default): a completed AU is handed out in the PushDatagram result (disposition
        // Completed). record its publish-ready arrival wall time.
        const UdpH264AssemblyStepResult pr = asm_.PushDatagram(reinterpret_cast<const uint8_t*>(buf), (size_t)r, nowUs);
        if (pr.disposition == UdpH264AssemblyDisposition::Completed) {
          std::lock_guard<std::mutex> lk(gapMu); deliveryUs.push_back(qpc_now_us());
        }
        // if in-order hold were on, deliveries would come via PopDelivery instead:
        UdpH264AssemblyStepResult out;
        while (asm_.PopDelivery(nowUs, /*repairNonKey=*/false, &out)) {
          if (out.disposition == UdpH264AssemblyDisposition::Completed) { std::lock_guard<std::mutex> lk(gapMu); deliveryUs.push_back(qpc_now_us()); }
        }
      }
      if (!any) std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  });

  // --- real sender ---
  SenderState sender;
  sender.nackEnabled.store(true, std::memory_order_relaxed);
  if (capOn) sender.StartWireCap(bitrate, mtu, /*enabled=*/true);  // real token-bucket wire cap at target
  // cap OFF: leave wireLimiter null (legacy pacing only) -- a genuine cap-off, no re-injected R limit.
  { std::lock_guard<std::mutex> lk(sender.mu); sender.peer = rxAddr; sender.peerReady = true; sender.udpPeer = rxAddr; sender.udpPeerReady = true; }
  Args args; args.udpMtu = mtu;
  SessionState session; session.clientSock = tx;
  MainLoopMailbox mailbox;
  sender.StartThread(VideoTransport::Udp, true, args, session, mailbox);

  // --- real encoder ---
  H264Encoder enc;
  if (!enc.initialize(bakeoff::kW, bakeoff::kH, fps, bitrate, keyintFrames)) { std::printf("enc init failed\n"); return 2; }
  RateGovernor gov;

  std::vector<uint8_t> nv12(bakeoff::kNv12, 128);
  const uint32_t totalFrames = 3 * phaseSec * fps;
  const uint64_t frameUs = 1'000'000ULL / fps;

  // per-second accumulators (real elapsed)
  uint64_t secStartUs = qpc_now_us();
  uint64_t secAuBytes = 0; uint32_t secEncoded = 0, secSkips = 0, secAccepted = 0;
  uint64_t lastWireBytes = 0;
  double avgAuBytes = (double)bitrate / 8.0 / fps;
  uint32_t curMaxQp = 32, curFps = fps, fpsAcc = 0, seqNo = 0;

  std::printf("\n sec|phase |accepted encoded skips| realAU_kbps genEst_kbps wire_kbps | queue | maxQp fps | "
              "pub_fps maxGapMs\n");

  uint64_t deadline = qpc_now_us();
  for (uint32_t i = 0; i < totalFrames; ++i) {
    deadline += frameUs;
    const uint32_t phase = i / (phaseSec * fps);
    const std::string scene = (phase == 1) ? "motion" : "static";
    bakeoff::fill_scene(scene, nv12, i);

    fpsAcc += curFps; const bool fpsTick = fpsAcc >= fps; if (fpsTick) fpsAcc -= fps;
    const bool key = (i == 0);
    if (fpsTick) {
      ++secAccepted;
      uint32_t qdepth; { std::lock_guard<std::mutex> lk(sender.mu); qdepth = (uint32_t)sender.queue.size(); }
      EncodeAdmissionInputs ad; ad.wireCapActive = capOn; ad.keyWanted = key; ad.senderQueueDepth = qdepth; ad.senderQueueMax = 2;
      if (decide_encode_admission(ad) == EncodeAdmission::SkipOverloaded) {
        ++secSkips;
      } else {
        std::vector<H264AccessUnit> units;
        enc.encode_frame(nv12, key, (int64_t)i * (int64_t)(10'000'000LL / fps), &units);
        for (auto& u : units) {
          secAuBytes += u.bytes.size(); ++secEncoded;
          { std::lock_guard<std::mutex> lk(sender.mu); sender.queue.push_back(make_item(++seqNo, gen, u.bytes, u.keyFrame || u.rawIdr, kEpoch, fps)); }
          sender.cv.notify_one();
        }
      }
    }

    const uint64_t nowUs = qpc_now_us();
    if (nowUs - secStartUs >= 1'000'000ULL) {
      const double elapsedSec = (nowUs - secStartUs) / 1e6;
      const uint64_t wb = wireBytes.load();
      const uint64_t wireDelta = wb - lastWireBytes;
      if (secEncoded) avgAuBytes = (double)secAuBytes / secEncoded;
      // product generated-load estimate (replicated formula, real counters): AU bytes + skipped*avg
      const uint64_t genEstBytes = (uint64_t)(secAuBytes + secSkips * avgAuBytes);
      const RateDecision dec = gov.OnSecond(genEstBytes, bitrate, fps);
      const char* note = "";
      if (dec.maxQp != curMaxQp) { enc.set_max_qp(dec.maxQp); curMaxQp = dec.maxQp; note = "qp"; }
      curFps = dec.fps;
      uint32_t qdepth; { std::lock_guard<std::mutex> lk(sender.mu); qdepth = (uint32_t)sender.queue.size(); }
      // publish-ready (reassembled) frame count + worst inter-delivery gap this second (NOT display)
      double maxGapMs = 0; uint64_t pub = 0;
      { std::lock_guard<std::mutex> lk(gapMu);
        for (size_t k = 1; k < deliveryUs.size(); ++k)
          if (deliveryUs[k] >= secStartUs) { const double g = (deliveryUs[k] - deliveryUs[k - 1]) / 1000.0; if (g > maxGapMs) maxGapMs = g; }
        for (uint64_t t : deliveryUs) if (t >= secStartUs) ++pub; }
      std::printf("%4u|%-6s|%8u %7u %5u| %10.0f %11.0f %9llu | %5u | %5u %3u | %6llu %8.1f %s\n",
                  (i + 1) / fps, scene.c_str(), secAccepted, secEncoded, secSkips,
                  secAuBytes * 8.0 / 1000.0 / elapsedSec, genEstBytes * 8.0 / 1000.0 / elapsedSec,
                  (unsigned long long)(wireDelta * 8 / 1000), qdepth, curMaxQp, curFps,
                  (unsigned long long)pub, maxGapMs, note);
      secStartUs = nowUs; secAuBytes = 0; secEncoded = secSkips = secAccepted = 0;
      lastWireBytes = wb;
    }

    const uint64_t n2 = qpc_now_us();
    if (n2 < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n2));
  }

  // drain briefly so the last AUs reach the receiver
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  sender.stop.store(true, std::memory_order_release); sender.cv.notify_all();
  if (sender.wireLimiter) sender.wireLimiter->Stop();
  if (sender.thread.joinable()) sender.thread.join();
  rxStop.store(true); if (rxThread.joinable()) rxThread.join();
  enc.shutdown();
  std::printf("\nhost_encoder_realpath_trace: DONE (datagrams=%llu, delivered=%zu)\n",
              (unsigned long long)datagrams.load(), deliveryUs.size());
  MFShutdown(); WSACleanup();
  return 0;
}
