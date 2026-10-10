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
// (txDelta + skipsDelta*avgSentFrameBytes; host_stage_stats_h264.cpp:160-186) computed here from the
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
  // governor OFF (fixed qp32/fps30) = the full-rate offered-load reference (Codex r5 item 6): with no
  // QP climb, each second's AU + wire load is the raw 30fps offered budget, to compare against the cap
  // + overhead BEFORE blaming burst/queue for the cap-ON skips.
  const bool govOff = [] { const char* e = std::getenv("BAKEOFF_NOGOV"); return e && *e && std::strcmp(e, "0"); }();
  const uint32_t capBitrate = rateKbps * 1000u;  // the wire cap R -- FIXED (test-only decoupling)
  // r7 budget-separation A/B: the encoder mean target, independent of the wire cap here. In the PRODUCT
  // these are coupled -- ApplyTarget calls sender.UpdateWireCap(encoder.activeBitrate)
  // (host_encoder_manager.cpp:146-149) -- so lowering the encoder target would also lower the cap and
  // leave no overhead headroom; applying this candidate therefore needs that coupling split in product.
  const uint32_t encTargetKbps = [&] { const char* e = std::getenv("BAKEOFF_ENC_TARGET_KBPS"); return (e && *e) ? (uint32_t)std::atoi(e) : rateKbps; }();
  const uint32_t encTargetBitrate = encTargetKbps * 1000u;
  const uint32_t keyintFrames = 10u * fps;
  const uint32_t mtu = 1200;
  const uint64_t gen = 7, kEpoch = 1;
  bool invalid = false; const char* invalidWhy = "";

  std::printf("host_encoder_realpath_trace (REAL encoder+admission+SenderState+WireLimiter+assembler; "
              "AMD path): capR=%uk encTarget=%uk fps=%u phase=%us cap=%s gov=%s, idle->motion->idle\n",
              rateKbps, encTargetKbps, fps, phaseSec, capOn ? "ON" : "OFF(real, limiter disabled)",
              govOff ? "OFF(fixed qp32)" : "ON");

  WSADATA wsa{}; WSAStartup(MAKEWORD(2, 2), &wsa);
  MFStartup(MF_VERSION);
  _putenv_s("REMOTE60_NATIVE_MAX_QP", "");            // product default ceiling; governor moves it live
  _putenv_s("REMOTE60_NATIVE_ENCODER_BACKEND", "mft_hw");

  // --- real loopback receiver socket (setup errors are INVALID, Codex r7) ---
  SOCKET rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (rx == INVALID_SOCKET) { std::printf("RESULT: INVALID (rx socket)\n"); MFShutdown(); WSACleanup(); return 2; }
  sockaddr_in rxAddr{}; rxAddr.sin_family = AF_INET; rxAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); rxAddr.sin_port = 0;
  if (bind(rx, reinterpret_cast<sockaddr*>(&rxAddr), sizeof(rxAddr)) != 0) { std::printf("RESULT: INVALID (bind err=%d)\n", WSAGetLastError()); closesocket(rx); MFShutdown(); WSACleanup(); return 2; }
  int alen = sizeof(rxAddr); getsockname(rx, reinterpret_cast<sockaddr*>(&rxAddr), &alen);
  int rcvbuf = 8 * 1024 * 1024; setsockopt(rx, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcvbuf), sizeof(rcvbuf));
  u_long nb = 1; if (ioctlsocket(rx, FIONBIO, &nb) != 0) { std::printf("RESULT: INVALID (FIONBIO)\n"); closesocket(rx); MFShutdown(); WSACleanup(); return 2; }
  SOCKET tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (tx == INVALID_SOCKET) { std::printf("RESULT: INVALID (tx socket)\n"); closesocket(rx); MFShutdown(); WSACleanup(); return 2; }

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
  if (capOn) sender.StartWireCap(capBitrate, mtu, /*enabled=*/true);  // real token-bucket wire cap at R (fixed)
  // cap OFF: leave wireLimiter null (legacy pacing only) -- a genuine cap-off, no re-injected R limit.
  { std::lock_guard<std::mutex> lk(sender.mu); sender.peer = rxAddr; sender.peerReady = true; sender.udpPeer = rxAddr; sender.udpPeerReady = true; }
  Args args; args.udpMtu = mtu;
  SessionState session; session.clientSock = tx;
  MainLoopMailbox mailbox;
  sender.StartThread(VideoTransport::Udp, true, args, session, mailbox);
  if (!sender.thread.joinable()) {  // sender thread failed to start (Codex r7 setup INVALID)
    std::printf("RESULT: INVALID (sender thread not started)\n");
    rxStop.store(true); if (rxThread.joinable()) rxThread.join();
    closesocket(rx); closesocket(tx); MFShutdown(); WSACleanup(); return 2;
  }

  // --- effective config log (Codex r5 items 3/4): contrast with the product's startup/ApplyTarget ---
  std::printf("config: mtu=%u wireCapEnabled=%d wireCapArmed=%d pacePeakBps=%u(0=default; product derives from "
              "ApplyTarget -- NOT set here) keyframePacePeakBps=%u fecInterleaved=%d admissionThreshold=2 "
              "outputHardLimit=6(product host_stage_encode_send_h264; NOT reused -- see note) epoch=%llu "
              "nackWired=NO fpsLeverWiredToApplyTarget=NO enqueueStaging=direct-push(no supersede/waitingForKey/"
              "DropAndResync) clampBurst=default-off\n",
              mtu, (int)sender.wireCapEnabled, (int)(sender.wireLimiter && sender.wireLimiter->enabled()),
              sender.pacePeakBps.load(), sender.keyframePacePeakBps.load(), (int)sender.fecInterleaved.load(),
              (unsigned long long)kEpoch);

  // --- real encoder ---
  H264Encoder enc;
  if (!enc.initialize(bakeoff::kW, bakeoff::kH, fps, encTargetBitrate, keyintFrames)) {
    std::printf("RESULT: INVALID (encoder init failed)\n");
    sender.stop.store(true, std::memory_order_release); sender.cv.notify_all();
    if (sender.wireLimiter) sender.wireLimiter->Stop();
    if (sender.thread.joinable()) sender.thread.join();      // joinable thread cleaned up (Codex r5)
    rxStop.store(true); if (rxThread.joinable()) rxThread.join();
    MFShutdown(); WSACleanup(); return 2;
  }
  RateGovernor gov;

  std::vector<uint8_t> nv12(bakeoff::kNv12, 128);
  const uint32_t totalFrames = 3 * phaseSec * fps;
  const uint64_t frameUs = 1'000'000ULL / fps;

  // per-second accumulators (real elapsed). offered = pre-gate inputs; admitted = gate-passed encodes;
  // AU = access units the encoder emitted (Codex r5 item 5: these three are distinct).
  uint64_t secStartUs = qpc_now_us();
  uint64_t secAuBytes = 0; uint32_t secAuOut = 0, secSkips = 0, secOffered = 0, secAdmitted = 0;
  uint64_t lastWireBytes = 0, lastDatagrams = 0;
  uint32_t curMaxQp = 32, curFps = fps, fpsAcc = 0, seqNo = 0;
  // governor-input state, replicated EXACTLY from host_stage_stats_h264.cpp:160-186 (real counters):
  uint64_t gTotalSkips = 0, gPrevTxBytes = 0, gPrevTxFrames = 0, gPrevSkips = 0, gPrevAvgSent = 0, gPrevTickUs = 0;
  const uint32_t gUserFps = fps;  // ceiling; target/userFps change would governor.Reset() (fixed scenario)

  // header: wire_kbps is RECEIVED UDP payload + 28B/datagram IPv4 overhead, / real elapsed; asmAU_fps is
  // UdpH264FrameAssembler Completed-AU reassembly rate (NO decoder/FrameGate/FrameBuffer/renderer, NO NACK
  // round-trip); auGapMs/silenceMs are reassembly inter-arrival and age-since-last.
  std::printf("\n sec|phase |offered admit AUout skips| realAU_kbps govEst_kbps wire_kbps(ovh) | queueMax | "
              "maxQp reqFps| asmAU_fps auGapMs silenceMs\n");

  uint32_t queueMaxSec = 0; bool encodeFailed = false;
  // Codex r7: actually COUNT the product staging conditions (don't claim "<=3 so nothing fired").
  uint64_t keySupersedeWouldFire = 0;   // a key AU pushed while the queue was non-empty (product would supersede backlog)
  uint64_t outputHardLimitWouldFire = 0; // an AU pushed at queue depth >= 6 (product output hard-limit)
  uint32_t queueMaxRun = 0;
  uint64_t deadline = qpc_now_us();
  for (uint32_t i = 0; i < totalFrames; ++i) {
    deadline += frameUs;
    const uint32_t phase = i / (phaseSec * fps);
    const std::string scene = (phase == 1) ? "motion" : "static";
    bakeoff::fill_scene(scene, nv12, i);

    fpsAcc += curFps; const bool fpsTick = fpsAcc >= fps; if (fpsTick) fpsAcc -= fps;  // fps lever: supply sampling only (NOT ApplyTarget/codec reinit)
    const bool key = (i == 0);
    if (fpsTick) {
      ++secOffered;  // offered BEFORE the gate (Codex r5: not "accepted")
      uint32_t qdepth; { std::lock_guard<std::mutex> lk(sender.mu); qdepth = (uint32_t)sender.queue.size(); }
      if (qdepth > queueMaxSec) queueMaxSec = qdepth;
      EncodeAdmissionInputs ad; ad.wireCapActive = capOn; ad.keyWanted = key; ad.senderQueueDepth = qdepth; ad.senderQueueMax = 2;
      if (decide_encode_admission(ad) == EncodeAdmission::SkipOverloaded) {
        ++secSkips; ++gTotalSkips;  // cumulative, like HostStats::wireOverloadSkipCount
      } else {
        ++secAdmitted;
        std::vector<H264AccessUnit> units;
        if (!enc.encode_frame(nv12, key, (int64_t)i * (int64_t)(10'000'000LL / fps), &units)) encodeFailed = true;
        for (auto& u : units) {
          secAuBytes += u.bytes.size(); ++secAuOut;
          // NOTE (Codex r5 item 3): direct push -- the product's staging / key supersede / waitingForKey /
          // DropAndResync / epoch ownership (host_stage_encode_send_h264_au.cpp:420-514) is NOT reused; the
          // output hard-limit is 6 there (distinct from the input admission threshold 2). The counts
          // below record whether those product conditions WOULD have fired (observed, not asserted).
          const bool auKey = u.keyFrame || u.rawIdr;
          uint32_t depthAfter;
          { std::lock_guard<std::mutex> lk(sender.mu);
            const uint32_t depthBefore = (uint32_t)sender.queue.size();
            if (auKey && depthBefore > 0) ++keySupersedeWouldFire;      // product: key supersedes backlog
            if (depthBefore >= 6) ++outputHardLimitWouldFire;          // product: output hard-limit 6
            sender.queue.push_back(make_item(++seqNo, gen, u.bytes, auKey, kEpoch, fps));
            depthAfter = (uint32_t)sender.queue.size(); }
          if (depthAfter > queueMaxSec) queueMaxSec = depthAfter;       // Codex r7: max AFTER push, not only pre-gate
          if (depthAfter > queueMaxRun) queueMaxRun = depthAfter;
          sender.cv.notify_one();
        }
      }
    }

    const uint64_t nowUs = qpc_now_us();
    if (nowUs - secStartUs >= 1'000'000ULL) {
      const uint64_t wb = wireBytes.load(), dg = datagrams.load();
      // ---- governor input, EXACTLY as host_stage_stats_h264.cpp:160-186 (real sender counters) ----
      const bool wireCapActive = sender.wireCapEnabled && sender.wireLimiter && sender.wireLimiter->enabled();
      const uint64_t txNow = sender.txBytes.load(std::memory_order_relaxed);
      const uint64_t txDelta = txNow >= gPrevTxBytes ? txNow - gPrevTxBytes : 0; gPrevTxBytes = txNow;
      const uint64_t framesNow = sender.txFrames.load(std::memory_order_relaxed);
      const uint64_t framesDelta = framesNow >= gPrevTxFrames ? framesNow - gPrevTxFrames : 0; gPrevTxFrames = framesNow;
      const uint64_t skipsDelta = gTotalSkips >= gPrevSkips ? gTotalSkips - gPrevSkips : 0; gPrevSkips = gTotalSkips;
      const uint64_t elapsedUs = (gPrevTickUs > 0 && nowUs > gPrevTickUs) ? nowUs - gPrevTickUs : 1000000ULL; gPrevTickUs = nowUs;
      const double elapsedSec = elapsedUs / 1e6;
      const uint64_t avgSentFrameBytes = framesDelta > 0 ? txDelta / framesDelta : gPrevAvgSent;
      if (framesDelta > 0) gPrevAvgSent = avgSentFrameBytes;
      const uint64_t generatedBytes = wireCapActive ? txDelta + skipsDelta * avgSentFrameBytes : txDelta;  // cap off: txDelta only
      const uint64_t govBytesPerSec = elapsedUs > 0 ? (uint64_t)(generatedBytes * 1000000ULL / elapsedUs) : generatedBytes;
      if (!govOff) {
        const RateDecision dec = gov.OnSecond(govBytesPerSec, encTargetBitrate, gUserFps);  // governor target = encoder target
        if (dec.maxQp != curMaxQp) { enc.set_max_qp(dec.maxQp); curMaxQp = dec.maxQp; }
        curFps = dec.fps;  // supply-sampling lever only; fps step not wired to ApplyTarget (scope note)
      }
      // received wire incl IPv4 28B/datagram, normalised by real elapsed (Codex r5 item 5):
      const uint64_t payloadDelta = wb - lastWireBytes, dgDelta = dg - lastDatagrams;
      const double wireKbpsOvh = (payloadDelta + 28.0 * dgDelta) * 8.0 / 1000.0 / elapsedSec;
      // assembler Completed-AU rate + inter-arrival gap in the HALF-OPEN [secStartUs, nowUs) window,
      // + silence age since the last completed AU (Codex r5 item 5):
      double auGapMs = 0, silenceMs = 0; uint64_t asmAu = 0, lastDelivery = 0;
      { std::lock_guard<std::mutex> lk(gapMu);  // deliveryUs is written by rxThread -- always locked
        for (size_t k = 0; k < deliveryUs.size(); ++k) {
          if (deliveryUs[k] >= secStartUs && deliveryUs[k] < nowUs) {  // half-open [secStart, now)
            ++asmAu;
            if (k > 0) { const double g = (deliveryUs[k] - deliveryUs[k - 1]) / 1000.0; if (g > auGapMs) auGapMs = g; }
          }
        }
        if (!deliveryUs.empty()) lastDelivery = deliveryUs.back(); }
      if (asmAu == 0 && lastDelivery && nowUs > lastDelivery) silenceMs = (nowUs - lastDelivery) / 1000.0;  // guard underflow
      const double asmAuFps = asmAu / elapsedSec;  // Codex r7: a rate, normalised by real elapsed
      std::printf("%4u|%-6s|%7u %5u %5u %5u| %10.0f %11.0f %14.0f | %8u | %5u %6u| %9.1f %7.1f %9.1f %s\n",
                  (i + 1) / fps, scene.c_str(), secOffered, secAdmitted, secAuOut, secSkips,
                  secAuBytes * 8.0 / 1000.0 / elapsedSec, govBytesPerSec * 8.0 / 1000.0, wireKbpsOvh,
                  queueMaxSec, curMaxQp, curFps, asmAuFps, auGapMs, silenceMs,
                  govOff ? "(gov off)" : "");
      secStartUs = nowUs; secAuBytes = 0; secAuOut = secSkips = secOffered = secAdmitted = 0; queueMaxSec = 0;
      lastWireBytes = wb; lastDatagrams = dg;
    }

    const uint64_t n2 = qpc_now_us();
    if (n2 < deadline) std::this_thread::sleep_for(std::chrono::microseconds(deadline - n2));
  }

  // drain briefly so late AUs reach the receiver; report what did not come back (Codex r5 item 5).
  uint64_t deliveredBeforeDrain; { std::lock_guard<std::mutex> lk(gapMu); deliveredBeforeDrain = (uint64_t)deliveryUs.size(); }  // locked (rxThread live)
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  const uint32_t pendingAtStop = enc.pending_input_depth();
  sender.stop.store(true, std::memory_order_release); sender.cv.notify_all();
  if (sender.wireLimiter) sender.wireLimiter->Stop();
  if (sender.thread.joinable()) sender.thread.join();
  rxStop.store(true); if (rxThread.joinable()) rxThread.join();  // rxThread stopped -> deliveryUs now single-owner
  const bool sendFailed = sender.sendFailed.load(std::memory_order_relaxed);
  enc.shutdown();
  if (encodeFailed) { invalid = true; invalidWhy = "encode_frame returned false"; }
  else if (sendFailed) { invalid = true; invalidWhy = "sender sendFailed"; }
  std::printf("\nhost_encoder_realpath_trace: %s%s (datagrams=%llu, assembledAU=%zu, "
              "recoveredDuringDrain=%zu, encoderPendingAtStop=%u, queueMaxRun=%u, "
              "keySupersedeWouldFire=%llu, outputHardLimitWouldFire=%llu)\n",
              invalid ? "INVALID: " : "DONE", invalid ? invalidWhy : "",
              (unsigned long long)datagrams.load(), deliveryUs.size(),
              (size_t)(deliveryUs.size() - deliveredBeforeDrain), pendingAtStop, queueMaxRun,
              (unsigned long long)keySupersedeWouldFire, (unsigned long long)outputHardLimitWouldFire);
  MFShutdown(); WSACleanup();
  return invalid ? 1 : 0;
}
