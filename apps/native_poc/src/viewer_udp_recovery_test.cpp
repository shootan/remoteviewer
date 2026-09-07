// Windows receive-path integration test (Windows NACK wiring / recovery state machine).
//
// What runs is the product's own receive chain, not a model of it: VideoReceiver::run_udp
// (viewer_video_receiver.cpp) -> UdpH264FrameAssembler (in-order hold) -> FrameGate -> the Media
// Foundation H264Decoder -> FrameBuffer, on a real UDP socket, driven by an in-process fake host
// that encodes real H.264 with the same H264Encoder the product host uses, chunks AUs with the
// host's wire geometry, and applies a per-scenario plan: drop / reorder / pace chunks, answer or
// ignore NACKs, honour or withhold keyframe requests, advertise or hide kUdpFeatureVideoNack.
// The viewer's keyframe requests are read from ctx.control.keyframeRequests exactly where the
// control thread reads them; the present anchor is advanced by a stand-in for the render thread.
// Only the two UI-thread entry points the path calls (request_video_paint, post_pc_selection_reveal)
// are stubbed. No window, no swap chain, no capture backend -- so the result does not depend on an
// RDP session being attached, unlike the GNLinkStream+GNLinkViewer e2e.
//
// Build: remote60_viewer_udp_recovery_test (CMake). Run: prints one PASS/FAIL line per scenario
// and "viewer_udp_recovery_test: PASS" with exit 0 when every scenario passed.

#include "viewer_common.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "host_args.hpp"
#include "host_encoded_sender.hpp"
#include "host_epoch_gate.hpp"
#include "host_main_loop_mailbox.hpp"
#include "host_session.hpp"
#include "mf_h264_codec.hpp"
#include "native_socket.hpp"
#include "native_video_client_tcp_control.hpp"
#include "poc_protocol.hpp"
#include "viewer_args.hpp"
#include "viewer_constants.hpp"
#include "viewer_decoder_state.hpp"
#include "viewer_frame_gate_state.hpp"
#include "viewer_picker.hpp"
#include "viewer_present.hpp"
#include "viewer_state.hpp"
#include "viewer_udp_session.hpp"
#include "viewer_video_receiver.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::viewer;

// The receive path posts to the UI thread through these two; there is no window here.
namespace remote60::native_poc::viewer {
void request_video_paint(ViewerState& ctx, HWND hwnd) {
  (void)ctx;
  (void)hwnd;
}
void post_pc_selection_reveal(ViewerState& ctx, uint64_t readyGeneration, uint64_t readyEpoch) {
  (void)ctx;
  (void)readyGeneration;
  (void)readyEpoch;
}
}  // namespace remote60::native_poc::viewer

namespace {

int gFailures = 0;
#define CHECK(cond, detail)                                                                     \
  do {                                                                                          \
    if (!(cond)) {                                                                              \
      std::printf("  FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #cond, std::string(detail).c_str()); \
      ++gFailures;                                                                              \
    }                                                                                           \
  } while (0)

void sleep_ms(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 360;
constexpr uint32_t kMtu = 1200;
constexpr uint64_t kFrameIntervalUs = 16667;

// ---------------------------------------------------------------------------------------------
// Fake host
// ---------------------------------------------------------------------------------------------

struct LossPlan {
  // Drop this chunk's FIRST transmission (retransmits pass unless dropRetransmits).
  std::function<bool(uint32_t seq, uint16_t chunkIndex, bool key, uint16_t chunkCount)> dropFirstSend;
  bool advertiseNack = true;          // HelloAck carries kUdpFeatureVideoNack
  bool answerNacks = true;            // false: NACKs are counted but never answered
  bool dropRetransmits = false;       // retransmits are sent but "lost" too
  bool honorKeyframeRequests = true;  // false: requests are counted, no IDR is produced for them
  bool reverseChunkOrder = false;     // send every AU's chunks last-to-first
  uint32_t keyInterChunkDelayUs = 0;  // pace an IDR's chunks (the large-IDR tail scenario)
  // Route every AU through the PRODUCT's sender thread (SenderState::StartThread, with its input-
  // epoch fence) instead of the fake host's direct sendto: what the viewer then receives is the
  // real sender's output stream. The loss plan / NACK answers do not apply in this mode.
  bool realSender = false;
};

struct CachedAu {
  uint32_t seq = 0;
  UdpVideoChunkHeader base{};
  std::vector<uint8_t> payload;
};

class FakeHost {
 public:
  ~FakeHost() { Stop(); }

  bool Start(const LossPlan& plan) {
    plan_ = plan;
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(sock_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    int len = sizeof(addr);
    if (getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
    port_ = ntohs(addr.sin_port);
    (void)set_recv_timeout(sock_, 5);
    if (!enc_.initialize(kWidth, kHeight, 60, 3000000, 600)) {
      std::printf("  fake host: H264Encoder::initialize failed\n");
      return false;
    }
    nv12_.assign(static_cast<size_t>(kWidth) * kHeight * 3 / 2, 128);
    stop_ = false;
    if (plan_.realSender) {
      realArgs_.udpMtu = kMtu;
      realSession_.clientSock = sock_;
      realSender_.inputEpochRef = &epoch_;
      realSender_.pacePeakBps.store(0, std::memory_order_relaxed);
      realSender_.StartThread(VideoTransport::Udp, true, realArgs_, realSession_, realMailbox_);
    }
    reader_ = std::thread([this]() { ReaderLoop(); });
    return true;
  }

  void Stop() {
    stop_ = true;
    if (reader_.joinable()) reader_.join();
    if (plan_.realSender && realSender_.thread.joinable()) {
      ReleaseRealSender();
      realSender_.stop.store(true, std::memory_order_release);
      realSender_.cv.notify_all();
      realSender_.thread.join();
    }
    if (sock_ != INVALID_SOCKET) {
      closesocket(sock_);
      sock_ = INVALID_SOCKET;
    }
    enc_.shutdown();
  }

  // Real-sender controls (P11 sender boundary, S15): hold the next AU that reaches the sender's
  // permission point (right before its first datagram) until released, so a flush can be placed
  // exactly there; and the sender's own drop counter.
  void HoldRealSenderNext() {
    std::lock_guard<std::mutex> lk(holdMu_);
    holdArmed_ = true;
    holdHeld_ = false;
    holdRelease_ = false;
    realSender_.beforeFirstDatagramHook = [this](const EncodedSendItem&) {
      std::unique_lock<std::mutex> lk2(holdMu_);
      if (!holdArmed_) return;
      holdArmed_ = false;
      holdHeld_ = true;
      holdCv_.notify_all();
      holdCv_.wait(lk2, [this] { return holdRelease_; });
    };
  }
  bool WaitRealSenderHeld(uint32_t timeoutMs) {
    std::unique_lock<std::mutex> lk(holdMu_);
    return holdCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] { return holdHeld_; });
  }
  void ReleaseRealSender() {
    {
      std::lock_guard<std::mutex> lk(holdMu_);
      holdRelease_ = true;
      holdArmed_ = false;
    }
    holdCv_.notify_all();
  }
  uint64_t real_sender_drops() const { return realSender_.inputEpochDropCount.load(); }
  uint64_t real_sender_tx_frames() const { return realSender_.txFrames.load(); }

  uint16_t port() const { return port_; }
  bool peer_known() const { return peerKnown_.load(); }
  // The feature bits the viewer's Hello actually carried on the wire.
  uint32_t hello_features() const { return helloFeatures_.load(); }
  // Zero the next IDR's payload: the assembler completes it, the decoder cannot use it.
  void CorruptNextKey() { corruptNextKey_ = true; }
  uint64_t corrupted_keys() const { return corruptedKeys_; }

  // Encode the next picture and put it on the wire. `noisy` makes a big AU (a large IDR / a
  // multi-chunk P), `synthetic` re-sends the previous picture flagged as a host kick / refresh.
  // Returns the seq the AU went out with (0 if the encoder produced nothing this call).
  //
  // Every AU passes the PRODUCT's epoch gate (host_epoch_gate.hpp) on the product's own FIFO tags
  // (P11), exactly as host_stage_encode_send_h264_au.cpp does: after Flush() nothing from the old
  // epoch goes out and the first AU on the wire is the new epoch's IDR. With SetStampFromAu(true)
  // the wire capture stamp is the AU's own input stamp (the product's hdr.captureQpcUs), so a
  // picture the async encoder held goes out with its true old stamp -- the field shape.
  uint32_t SendFrame(bool forceKey, bool synthetic, uint64_t captureUs, bool noisy = true) {
    if (!synthetic) NextPicture(noisy);
    std::vector<H264AccessUnit> units;
    enc_.set_next_input_synthetic(synthetic);
    enc_.set_next_input_epoch(epoch_.load(std::memory_order_acquire));
    const bool key = forceKey || forceKeyPending_;
    forceKeyPending_ = false;
    if (!enc_.encode_frame(nv12_, key, static_cast<int64_t>(captureUs) * 10, &units)) {
      std::printf("  fake host: encode_frame failed\n");
      return 0;
    }
    uint32_t lastSeq = 0;
    for (auto& au : units) {
      switch (epoch_gate_judge(gate_, epoch_.load(std::memory_order_acquire), au.inputEpoch, au.keyFrame, au.bytes.size(), qpc_now_us())) {
        case EpochVerdict::DropOldEpoch: ++gateDroppedOld_; continue;
        case EpochVerdict::DropUnknownEpoch: ++gateDroppedUnknown_; continue;
        case EpochVerdict::DropAwaitingKey: ++gateDroppedNonKey_; forceKeyPending_ = true; continue;
        case EpochVerdict::ResetEncoder: ++gateResets_; forceKeyPending_ = true; continue;
        case EpochVerdict::AcceptKey: ++gateKeysAccepted_; break;
        case EpochVerdict::Emit: break;
      }
      const uint64_t wireCaptureUs =
          (stampFromAu_ && au.sampleTimeHns > 0) ? static_cast<uint64_t>(au.sampleTimeHns / 10) : captureUs;
      lastSeq = SendAu(au.bytes, au.keyFrame, stampFromAu_ ? au.synthetic : synthetic, wireCaptureUs, au.inputEpoch);
      if (flushPending_) {
        firstSeqAfterFlush_ = lastSeq;
        firstAfterFlushKey_ = au.keyFrame;
        flushPending_ = false;
      }
      if (au.keyFrame) ++keyframesSent_;
    }
    return lastSeq;
  }

  // What every product flush caller does: a new input epoch and a forced key on the next input.
  void Flush() {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    forceKeyPending_ = true;
    flushPending_ = true;
    firstSeqAfterFlush_ = 0;
    firstAfterFlushKey_ = false;
  }
  void SetStampFromAu(bool on) { stampFromAu_ = on; }
  uint64_t gate_dropped_old() const { return gateDroppedOld_; }
  uint64_t gate_dropped_nonkey() const { return gateDroppedNonKey_; }
  uint64_t gate_keys_accepted() const { return gateKeysAccepted_; }
  uint64_t gate_resets() const { return gateResets_; }
  uint32_t first_seq_after_flush() const { return firstSeqAfterFlush_; }
  bool first_after_flush_key() const { return firstAfterFlushKey_; }
  const char* encoder_backend() const { return enc_.backend_name(); }

  // The product host consumes a seq for a frame its sender queue then discards (EnqueueKey clears
  // the queue; HoldForKey drops deltas): the next AU on the wire has a gap without any loss. The
  // gap is applied right before the next KEYFRAME goes out -- on an asynchronous encoder the call
  // that submits the forced key may first return the previous P, and a gap in front of that P
  // would be a different (lossy) shape than the product's "gap then IDR".
  void SkipSeq() { skipBeforeNextKey_ = true; }

  // What the viewer asked for, read where the control thread reads it. Counts every request.
  bool TakeKeyframeRequest(KeyframeRequestState& requests) {
    uint16_t reason = 0;
    if (!requests.ConsumePending(&reason)) return false;
    ++keyframeRequests_;
    lastKeyframeReason_ = reason;
    return plan_.honorKeyframeRequests;
  }
  void SetHonorKeyframeRequests(bool honor) { plan_.honorKeyframeRequests = honor; }
  void SetAnswerNacks(bool answer) { plan_.answerNacks = answer; }

  uint64_t nacks_received() const { return nacksReceived_.load(); }
  uint64_t nacks_for(uint32_t seq) const {
    std::lock_guard<std::mutex> lk(statsMu_);
    const auto it = nacksPerSeq_.find(seq);
    return it == nacksPerSeq_.end() ? 0 : it->second;
  }
  uint64_t retransmit_chunks() const { return retransmitChunks_.load(); }
  uint64_t keyframe_requests() const { return keyframeRequests_.load(); }
  uint16_t last_keyframe_reason() const { return lastKeyframeReason_.load(); }
  uint64_t keyframes_sent() const { return keyframesSent_; }
  uint64_t dropped_chunks() const { return droppedChunks_; }
  uint32_t last_seq() const { return seq_; }

 private:
  void NextPicture(bool noisy) {
    ++frameIndex_;
    uint8_t* y = nv12_.data();
    // Grey field, a bright bar walking right, and (noisy) a band of fresh noise so every P frame
    // is several chunks and an IDR is a few hundred: loss has somewhere to land.
    std::fill(y, y + static_cast<size_t>(kWidth) * kHeight, 96);
    const uint32_t barX = (frameIndex_ * 12) % (kWidth - 32);
    for (uint32_t row = 0; row < kHeight; ++row) {
      for (uint32_t x = barX; x < barX + 32; ++x) y[row * kWidth + x] = 235;
    }
    if (noisy) {
      uint32_t state = 0x9e3779b9u ^ frameIndex_;
      for (uint32_t row = kHeight / 4; row < kHeight * 3 / 4; ++row) {
        for (uint32_t x = 0; x < kWidth; ++x) {
          state = state * 1664525u + 1013904223u;
          y[row * kWidth + x] = static_cast<uint8_t>(16 + (state >> 24) % 220);
        }
      }
    }
  }

  uint32_t SendAu(const std::vector<uint8_t>& bytes, bool key, bool synthetic, uint64_t captureUs, uint64_t auEpoch = 0) {
    CachedAu au;
    if (key && skipBeforeNextKey_) {
      ++seq_;
      skipBeforeNextKey_ = false;
    }
    au.seq = ++seq_;
    au.payload = bytes;
    if (key && corruptNextKey_.exchange(false)) {
      std::fill(au.payload.begin(), au.payload.end(), 0);
      ++corruptedKeys_;
    }
    au.base.seq = au.seq;
    au.base.codec = static_cast<uint16_t>(UdpCodec::H264);
    au.base.flags = static_cast<uint16_t>((key ? 0x1u : 0u) | (synthetic ? kUdpVideoChunkFlagSynthetic : 0u));
    au.base.width = kWidth;
    au.base.height = kHeight;
    au.base.payloadSize = static_cast<uint32_t>(bytes.size());
    au.base.streamGeneration = 1;
    au.base.captureQpcUs = captureUs;
    au.base.encodeStartQpcUs = captureUs;
    au.base.encodeEndQpcUs = qpc_now_us();
    au.base.sendQpcUs = qpc_now_us();
    {
      std::lock_guard<std::mutex> lk(cacheMu_);
      cache_.push_back(au);
      while (cache_.size() > 24) cache_.pop_front();
    }
    if (plan_.realSender) {
      // The product's sender does the chunking, pacing and the input-epoch fence; this is the
      // same hand-off host_stage_encode_send_h264_au.cpp makes.
      remote60::native_poc::EncodedSendItem item;
      item.bytes = au.payload;
      item.keyFrame = key;
      item.frameIntervalUs = kFrameIntervalUs;
      item.enqueueUs = qpc_now_us();
      item.mediaEpoch = realSender_.mediaSessionEpoch.load(std::memory_order_acquire);
      item.inputEpoch = auEpoch;
      item.udpHdr = au.base;
      {
        std::lock_guard<std::mutex> lk(realSender_.mu);
        realSender_.queue.push_back(std::move(item));
      }
      realSender_.cv.notify_all();
      return au.seq;
    }
    SendChunks(au, nullptr, 0);
    return au.seq;
  }

  // The host's chunk geometry (host_net_io.cpp send_udp_chunks / send_udp_chunk_indices), no parity.
  void SendChunks(const CachedAu& au, const uint16_t* only, uint16_t onlyCount) {
    if (!peerKnown_.load()) return;
    const uint32_t maxChunk = kMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    const uint32_t chunkCount = static_cast<uint32_t>((au.payload.size() + maxChunk - 1) / maxChunk);
    const bool key = (au.base.flags & 0x1u) != 0;
    std::vector<uint16_t> order;
    if (only) {
      order.assign(only, only + onlyCount);
    } else {
      for (uint32_t i = 0; i < chunkCount; ++i) order.push_back(static_cast<uint16_t>(i));
      if (plan_.reverseChunkOrder) std::reverse(order.begin(), order.end());
    }
    std::vector<uint8_t> datagram(kMtu);
    for (const uint16_t idx : order) {
      if (idx >= chunkCount) continue;
      if (!only && plan_.dropFirstSend &&
          plan_.dropFirstSend(au.seq, idx, key, static_cast<uint16_t>(chunkCount))) {
        ++droppedChunks_;
        continue;
      }
      if (only && plan_.dropRetransmits) continue;
      const size_t offset = static_cast<size_t>(idx) * maxChunk;
      const uint32_t chunkSize = static_cast<uint32_t>(std::min<size_t>(maxChunk, au.payload.size() - offset));
      UdpVideoChunkHeader h = au.base;
      h.chunkOffset = static_cast<uint32_t>(offset);
      h.chunkSize = chunkSize;
      h.chunkIndex = idx;
      h.chunkCount = static_cast<uint16_t>(chunkCount);
      h.chunkStride = maxChunk;
      if (offset == 0) h.flags |= 0x2u;
      if (offset + chunkSize >= au.payload.size()) h.flags |= 0x4u;
      h.sendQpcUs = qpc_now_us();
      std::memcpy(datagram.data(), &h, sizeof(h));
      std::memcpy(datagram.data() + sizeof(h), au.payload.data() + offset, chunkSize);
      sockaddr_in peer{};
      {
        std::lock_guard<std::mutex> lk(peerMu_);
        peer = peer_;
      }
      (void)sendto(sock_, reinterpret_cast<const char*>(datagram.data()), static_cast<int>(sizeof(h) + chunkSize), 0,
                   reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
      if (!only && key && plan_.keyInterChunkDelayUs > 0) {
        // Spin, not sleep: a Windows sleep is 1-2 ms at best, which would stretch a 72-chunk IDR
        // past the 120 ms tail grace and turn the scenario into a test of the timer instead.
        const uint64_t untilUs = qpc_now_us() + plan_.keyInterChunkDelayUs;
        while (qpc_now_us() < untilUs) {
        }
      }
    }
  }

  void ReaderLoop() {
    std::vector<uint8_t> rx(2048);
    while (!stop_.load()) {
      sockaddr_in from{};
      int fromLen = sizeof(from);
      const int n = recvfrom(sock_, reinterpret_cast<char*>(rx.data()), static_cast<int>(rx.size()), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (n <= 0) continue;
      if (n >= static_cast<int>(sizeof(UdpHelloPacket))) {
        UdpHelloPacket hello{};
        std::memcpy(&hello, rx.data(), sizeof(hello));
        if (hello.magic == kMagic && hello.kind == static_cast<uint16_t>(UdpPacketKind::Hello)) {
          {
            std::lock_guard<std::mutex> lk(peerMu_);
            peer_ = from;
          }
          peerKnown_ = true;
          if (plan_.realSender) {
            std::lock_guard<std::mutex> lk(realSender_.mu);
            realSender_.peer = from;
            realSender_.peerReady = true;
          }
          helloFeatures_ = hello.features;
          UdpHelloPacket ack{};
          ack.kind = static_cast<uint16_t>(UdpPacketKind::HelloAck);
          ack.features = kUdpFeatureVideoFec | (plan_.advertiseNack ? kUdpFeatureVideoNack : 0u);
          (void)sendto(sock_, reinterpret_cast<const char*>(&ack), sizeof(ack), 0,
                       reinterpret_cast<const sockaddr*>(&from), fromLen);
          continue;
        }
      }
      if (n >= static_cast<int>(sizeof(UdpVideoNackPacket))) {
        UdpVideoNackPacket nack{};
        std::memcpy(&nack, rx.data(), sizeof(nack));
        if (nack.magic == kMagic && nack.kind == static_cast<uint16_t>(UdpPacketKind::VideoNack) &&
            nack.size == sizeof(nack)) {
          ++nacksReceived_;
          {
            std::lock_guard<std::mutex> lk(statsMu_);
            ++nacksPerSeq_[nack.seq];
          }
          if (!plan_.answerNacks) continue;
          CachedAu au;
          bool found = false;
          {
            std::lock_guard<std::mutex> lk(cacheMu_);
            for (auto it = cache_.rbegin(); it != cache_.rend(); ++it) {
              if (it->seq == nack.seq && it->base.streamGeneration == nack.streamGeneration) {
                au = *it;
                found = true;
                break;
              }
            }
          }
          if (!found) continue;
          const uint16_t count = std::min<uint16_t>(nack.missingCount, kUdpVideoNackMaxMissing);
          SendChunks(au, nack.missing, count);
          retransmitChunks_ += count;
          continue;
        }
      }
    }
  }

  LossPlan plan_;
  SOCKET sock_ = INVALID_SOCKET;
  uint16_t port_ = 0;
  std::thread reader_;
  std::atomic<bool> stop_{false};
  std::mutex peerMu_;
  sockaddr_in peer_{};
  std::atomic<bool> peerKnown_{false};
  std::atomic<uint32_t> helloFeatures_{0};
  H264Encoder enc_;
  std::vector<uint8_t> nv12_;
  uint32_t frameIndex_ = 0;
  uint32_t seq_ = 0;
  EpochGate gate_;
  std::atomic<uint64_t> epoch_{1};
  bool forceKeyPending_ = false;
  // Real-sender mode (LossPlan::realSender). Host-side types, spelled out: the viewer namespace
  // has its own Args / SessionState.
  remote60::native_poc::Args realArgs_;
  remote60::native_poc::SessionState realSession_;
  remote60::native_poc::MainLoopMailbox realMailbox_;
  remote60::native_poc::SenderState realSender_;
  std::mutex holdMu_;
  std::condition_variable holdCv_;
  bool holdArmed_ = false;
  bool holdHeld_ = false;
  bool holdRelease_ = false;
  bool skipBeforeNextKey_ = false;
  bool flushPending_ = false;
  bool stampFromAu_ = false;
  uint64_t gateDroppedOld_ = 0;
  uint64_t gateDroppedUnknown_ = 0;
  uint64_t gateDroppedNonKey_ = 0;
  uint64_t gateKeysAccepted_ = 0;
  uint64_t gateResets_ = 0;
  uint32_t firstSeqAfterFlush_ = 0;
  bool firstAfterFlushKey_ = false;
  std::mutex cacheMu_;
  std::deque<CachedAu> cache_;
  mutable std::mutex statsMu_;
  std::unordered_map<uint32_t, uint64_t> nacksPerSeq_;
  std::atomic<uint64_t> nacksReceived_{0};
  std::atomic<uint64_t> retransmitChunks_{0};
  std::atomic<uint64_t> keyframeRequests_{0};
  std::atomic<uint16_t> lastKeyframeReason_{0};
  uint64_t keyframesSent_ = 0;
  uint64_t droppedChunks_ = 0;
  std::atomic<bool> corruptNextKey_{false};
  uint64_t corruptedKeys_ = 0;
};

// ---------------------------------------------------------------------------------------------
// The viewer under test: the product receive thread on a real socket, plus a stand-in renderer
// ---------------------------------------------------------------------------------------------

struct ViewerRig {
  ViewerState ctx;
  viewer::Args args;
  DecoderState dec;
  FrameGateState gate;
  std::optional<VideoReceiver> receiver;
  std::thread recvThread;
  std::thread presentThread;
  std::atomic<bool> stopPresent{false};
  // The render thread's job here: after a publish, stamp the present anchor (real frames only, as
  // viewer_present.cpp does). `pinPresentAnchor` freezes it to model a renderer that has not had
  // its next vsync yet, or a paused present.
  std::atomic<bool> pinPresentAnchor{false};
  std::atomic<uint64_t> publishedCount{0};
  std::atomic<uint32_t> lastPublishedSeq{0};
  std::atomic<uint32_t> maxPublishedSeq{0};
  std::atomic<uint64_t> lastPublishedCaptureUs{0};

  ViewerRig() {
    args.codec = "h264";
    args.transport = "udp";
    args.fpsHint = 60;
    args.udpMtu = kMtu;
    dec.useH264 = true;
    dec.transport = VideoTransport::Udp;
    gate.catchupReenterMinIntervalUs = kCatchupReenterMinIntervalUsDefault;
    gate.staleCaptureDropUs = kStaleCaptureDropUs;
    gate.staleReferenceRecoveryMinIntervalUs = kStaleRecoveryMinIntervalUsDefault;
    gate.congestionRecoverMinUs = kCongestionRecoverMinUsDefault;
    gate.congestionRecoveryTimeoutUs = kCongestionRecoveryTimeoutUsDefault;
    gate.decodeQueueLagDropUs = kDecodeQueueLagDropUs;
    gate.catchupLagDropUs = kCatchupLagDropUs;
    gate.denseArrivalMaxGapUs = kDenseArrivalMaxGapUsDefault;
    gate.lagTriggerStreakMin = kLagTriggerStreakMinDefault;
    gate.recoveryRetryIntervalUs = kKeyRecoveryRetryUsDefault;
    gate.recoveryRetryMaxIntervalUs = kKeyRecoveryRetryMaxUsDefault;
    gate.recoveryRetryDeferMaxUs = kKeyRecoveryDeferMaxUsDefault;
    gate.waitForKeyFrame = true;  // init_decoder: an H.264 session starts waiting for its first IDR
    ctx.picker.visible.store(false, std::memory_order_relaxed);
    ctx.control.keyframeRequests.Reset();
  }

  ~ViewerRig() { Stop(); }

  bool Connect(uint16_t hostPort, bool requestNack, uint64_t holdUs, uint32_t recvTimeoutMs = 25) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(hostPort);
    if (connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    // The product's receive buffer (connect_media_socket): the first IDR's decoder initialisation
    // takes ~100 ms, during which the next frames must fit in the socket, not spill.
    const int recvBuf = 1024 * 1024;
    (void)setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&recvBuf), sizeof(recvBuf));
    // The product's own negotiation (viewer_udp_session.hpp, what connect_media_socket runs):
    // the Hello options, the ack bits kept, the receive timeout armed. Not a test-only handshake,
    // so a product default that stops requesting NACK fails S1 here (Codex condition 5).
    UdpHelloOptions hello = viewer_udp_hello_options(std::string(), requestNack);
    hello.budgetMs = 2000;  // a local fake host answers at once; keep a failed run short
    uint32_t ackFeatures = 0;
    std::string error;
    if (!udp_hello_handshake(s, hello, nullptr, &error, &ackFeatures)) {
      std::printf("  viewer: hello failed: %s\n", error.c_str());
      return false;
    }
    (void)viewer_arm_udp_recv_timeout(s, recvTimeoutMs);
    ctx.session.sock = s;
    viewer_apply_udp_hello_ack(ackFeatures, requestNack, ctx.session);
    VideoReceiver::NackOptions nack;
    nack.enabled = ctx.session.hostSupportsNack;
    nack.holdUs = holdUs;
    receiver.emplace(ctx, args, dec, gate, qpc_now_us(), 0, 0, nack);
    recvThread = std::thread([this]() { receiver->Run(); });
    presentThread = std::thread([this]() { PresentLoop(); });
    return true;
  }

  void Stop() {
    if (!recvThread.joinable() && !presentThread.joinable()) return;
    ctx.session.running = false;
    stopPresent = true;
    if (recvThread.joinable()) recvThread.join();
    if (presentThread.joinable()) presentThread.join();
    if (ctx.session.sock != INVALID_SOCKET) {
      closesocket(ctx.session.sock);
      ctx.session.sock = INVALID_SOCKET;
    }
    dec.decoder.shutdown();
  }

  bool hostSupportsNack() const { return ctx.session.hostSupportsNack; }

 private:
  void PresentLoop() {
    uint64_t seenVersion = 0;
    while (!stopPresent.load()) {
      {
        std::lock_guard<std::mutex> lk(ctx.frameBuf.frame.mu);
        auto& f = ctx.frameBuf.frame;
        if (f.version != seenVersion) {
          seenVersion = f.version;
          ++publishedCount;
          lastPublishedSeq = f.seq;
          if (f.seq > maxPublishedSeq.load()) maxPublishedSeq = f.seq;
          lastPublishedCaptureUs = f.captureUs;
          ctx.frameBuf.lastPresentedVersion.store(f.version, std::memory_order_relaxed);
          if (!f.synthetic && !pinPresentAnchor.load()) {
            ctx.frameBuf.lastPresentedCaptureUs.store(f.captureUs, std::memory_order_relaxed);
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
};

// Pump frames at 60 fps for `ms`, honouring (or just counting) the viewer's keyframe requests.
// `before(seq)` runs before each frame and may return a forced-key flag. Returns frames sent.
uint32_t pump(FakeHost& host, ViewerRig& rig, uint32_t ms, bool firstIsKey = false,
              const std::function<bool(uint32_t nextSeq)>& forceKeyAt = nullptr) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(ms) * 1000ULL;
  uint32_t sent = 0;
  uint64_t nextUs = qpc_now_us();
  while (qpc_now_us() < endUs) {
    const uint64_t nowUs = qpc_now_us();
    if (nowUs < nextUs) {
      std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(nextUs - nowUs, 2000)));
      continue;
    }
    nextUs += kFrameIntervalUs;
    bool key = host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    if (sent == 0 && firstIsKey) key = true;
    if (forceKeyAt && forceKeyAt(host.last_seq() + 1)) key = true;
    (void)host.SendFrame(key, false, nowUs);
    ++sent;
  }
  return sent;
}

// Wait until `pred` or timeout; keeps pumping frames meanwhile.
bool pump_until(FakeHost& host, ViewerRig& rig, uint32_t timeoutMs, const std::function<bool()>& pred) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(timeoutMs) * 1000ULL;
  uint64_t nextUs = qpc_now_us();
  while (qpc_now_us() < endUs) {
    if (pred()) return true;
    const uint64_t nowUs = qpc_now_us();
    if (nowUs >= nextUs) {
      nextUs += kFrameIntervalUs;
      const bool key = host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(key, false, nowUs);
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(nextUs - nowUs, 2000)));
    }
  }
  return pred();
}

// Idle wait (no frames) that still drains keyframe requests so the count is exact.
void idle(FakeHost& host, ViewerRig& rig, uint32_t ms) {
  const uint64_t endUs = qpc_now_us() + static_cast<uint64_t>(ms) * 1000ULL;
  while (qpc_now_us() < endUs) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    sleep_ms(2);
  }
}

std::string state_name(const ViewerRig& rig) { return congestion_state_name(rig.gate.congestionState); }

// Bring a session up: hello, first IDR, a second of clean streaming. Returns false on setup failure.
bool start_session(FakeHost& host, ViewerRig& rig, const LossPlan& plan,
                   bool requestNack = kVideoNackEnabledDefault, uint64_t holdUs = 120000) {
  if (!host.Start(plan)) {
    std::printf("  setup: fake host failed to start\n");
    return false;
  }
  if (!rig.Connect(host.port(), requestNack, holdUs)) {
    std::printf("  setup: viewer failed to connect\n");
    return false;
  }
  if (!pump_until(host, rig, 3000, [&]() { return rig.publishedCount.load() >= 20; })) {
    std::printf("  setup: no decoded frames (published=%llu, keyReq=%llu, nack=%d)\n",
                static_cast<unsigned long long>(rig.publishedCount.load()),
                static_cast<unsigned long long>(host.keyframe_requests()), rig.hostSupportsNack() ? 1 : 0);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------------------------

// S1: one chunk of a multi-chunk P frame is lost. With NACK negotiated the viewer asks for it,
// the host replays it, the frame is delivered in order and NO keyframe is requested.
void scenario_p_chunk_loss_repaired_by_nack() {
  std::printf("[S1] P chunk loss -> NACK repair, no IDR\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> target{0};
  LossPlan plan;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t idx, bool key, uint16_t count) {
    if (!key && count >= 3 && seq >= 40 && target.load() == 0) target = seq;
    return seq == target.load() && idx == 1;
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  // Observed on the wire, not assumed: the product's default Hello asks for NACK.
  CHECK((host.hello_features() & kUdpFeatureVideoNack) != 0, "the Hello carried kUdpFeatureVideoNack");
  CHECK((rig.ctx.session.udpHelloAckFeatures & kUdpFeatureVideoNack) != 0, "the HelloAck bits were kept");
  CHECK(rig.hostSupportsNack(), "NACK negotiated");
  const uint64_t requestsBefore = host.keyframe_requests();
  pump(host, rig, 1500);
  const uint32_t t = target.load();
  CHECK(t != 0, "a multi-chunk P frame was chosen for the loss");
  CHECK(host.nacks_for(t) >= 1, "viewer NACKed seq " + std::to_string(t) + " (nacks=" + std::to_string(host.nacks_for(t)) + ")");
  CHECK(host.retransmit_chunks() >= 1, "host replayed the chunk");
  CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request (got " + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.maxPublishedSeq.load() > t + 10, "stream continued past the loss (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
  // The liveness heartbeat the UI watchdog reads (history #390 item 5) is wired into this path.
  {
    const auto& live = rig.ctx.recvLive;
    const uint64_t nowUs = qpc_now_us();
    CHECK(live.loopIterations.load() > 100, "recv loop heartbeat counted passes");
    CHECK(liveness_age_us(nowUs, live.lastDatagramUs.load()) < 200000, "recent datagram stamp");
    CHECK(liveness_age_us(nowUs, live.lastAssembledUs.load()) < 200000, "recent assembled stamp");
    CHECK(liveness_age_us(nowUs, live.lastDecodeReturnUs.load()) < 200000, "recent decode-return stamp");
    CHECK(liveness_age_us(nowUs, live.lastPublishUs.load()) < 200000, "recent publish stamp");
    CHECK(live.current_stage() == RecvStage::Recv, std::string("recv thread idles in recv (stage ") + recv_stage_name(live.current_stage()) + ")");
    SessionLivenessSample s;
    s.nowUs = nowUs;
    s.stage = live.current_stage();
    s.stageEnterUs = live.stageEnterUs.load();
    s.loopIterations = live.loopIterations.load();
    s.lastDatagramUs = live.lastDatagramUs.load();
    s.lastPublishUs = live.lastPublishUs.load();
    s.controlConnected = true;
    const auto v = evaluate_session_liveness(s, SessionLivenessConfig{});
    CHECK(!v.recvStalled && !v.linkSilent && !v.sessionDead, "healthy verdict on a live session");
  }
  std::printf("  seq=%u nacks=%llu retx=%llu keyReq=%llu published=%llu\n", t,
              static_cast<unsigned long long>(host.nacks_received()), static_cast<unsigned long long>(host.retransmit_chunks()),
              static_cast<unsigned long long>(host.keyframe_requests()), static_cast<unsigned long long>(rig.publishedCount.load()));
}

// S2: the same loss against a host that did not acknowledge NACK (old peer): no NACK is ever
// sent, the seq gap is treated as loss and an IDR request recovers the stream.
void scenario_old_host_without_nack() {
  std::printf("[S2] old host (no NACK bit): no NACK sent, IDR fallback recovers\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> target{0};
  LossPlan plan;
  plan.advertiseNack = false;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t idx, bool key, uint16_t count) {
    if (!key && count >= 3 && seq >= 40 && target.load() == 0) target = seq;
    return seq == target.load() && idx == 1;
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  CHECK((host.hello_features() & kUdpFeatureVideoNack) != 0, "the viewer still asked (the host declined)");
  CHECK(!rig.hostSupportsNack(), "NACK not negotiated");
  const uint64_t requestsBefore = host.keyframe_requests();
  pump(host, rig, 1500);
  const uint32_t t = target.load();
  CHECK(t != 0, "a multi-chunk P frame was chosen for the loss");
  CHECK(host.nacks_received() == 0, "no NACK against an old host");
  CHECK(host.keyframe_requests() > requestsBefore, "IDR requested to recover");
  CHECK(rig.maxPublishedSeq.load() > t + 10, "stream continued (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe at the end");
  std::printf("  seq=%u keyReq=%llu published=%llu\n", t, static_cast<unsigned long long>(host.keyframe_requests()),
              static_cast<unsigned long long>(rig.publishedCount.load()));
}

// S3: a whole P frame is lost (every chunk), the viewer asks for an IDR, and a chunk of THAT IDR
// is lost too. The IDR is the only recovery point, so it must be NACK-repaired even while the
// viewer waits for a keyframe -- and no second IDR request is needed.
void scenario_recovery_idr_chunk_loss_repaired() {
  std::printf("[S3] recovery IDR chunk loss -> NACK repairs the IDR while waiting for a key\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> lostP{0};
  std::atomic<uint32_t> lostIdr{0};
  LossPlan plan;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t idx, bool key, uint16_t count) {
    if (!key && seq >= 40 && lostP.load() == 0) lostP = seq;
    if (seq == lostP.load()) return true;  // the whole frame
    if (key && lostP.load() != 0 && seq > lostP.load() && lostIdr.load() == 0 && count >= 3) lostIdr = seq;
    return seq == lostIdr.load() && idx == 2;
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  pump(host, rig, 2000);
  const uint32_t p = lostP.load();
  const uint32_t k = lostIdr.load();
  CHECK(p != 0 && k != 0, "a P frame was lost and the recovery IDR was damaged");
  CHECK(host.keyframe_requests() >= requestsBefore + 1, "the lost P frame produced an IDR request");
  CHECK(host.nacks_for(k) >= 1, "the damaged IDR " + std::to_string(k) + " was NACKed");
  CHECK(host.keyframe_requests() <= requestsBefore + 2, "no IDR storm (requests " + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.maxPublishedSeq.load() > k + 10, "stream resumed past the IDR (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe at the end");
  std::printf("  lostP=%u idr=%u nacks(idr)=%llu keyReq=%llu published=%llu\n", p, k,
              static_cast<unsigned long long>(host.nacks_for(k)), static_cast<unsigned long long>(host.keyframe_requests()),
              static_cast<unsigned long long>(rig.publishedCount.load()));
}

// S4: a large IDR paced over ~60 ms (longer than the hole grace): its still-in-flight tail must not
// be NACKed, and no keyframe is requested.
void scenario_large_idr_tail_is_not_nacked() {
  std::printf("[S4] large paced IDR: no premature tail NACK\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  plan.keyInterChunkDelayUs = 700;  // ~72 chunks -> ~50 ms on the wire: past the hole grace, inside the tail grace
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint32_t before = host.last_seq();
  // Three unsolicited IDRs a second apart.
  pump(host, rig, 1200, false, [&](uint32_t next) { return (next - before) % 30 == 5; });
  CHECK(host.nacks_received() == 0, "no NACK on a paced lossless IDR (got " + std::to_string(host.nacks_received()) + ")");
  CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request");
  CHECK(host.keyframes_sent() >= 2, "IDRs went out (" + std::to_string(host.keyframes_sent()) + ")");
  CHECK(rig.maxPublishedSeq.load() > before + 40, "stream flowed (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
}

// S5: every AU's chunks arrive last-to-first. Reordering is not loss: no NACK, no IDR request.
void scenario_reordered_chunks() {
  std::printf("[S5] reversed chunk order: no NACK, no IDR\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  plan.reverseChunkOrder = true;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint32_t before = host.last_seq();
  pump(host, rig, 1000);
  CHECK(host.nacks_received() == 0, "no NACK on reordering (got " + std::to_string(host.nacks_received()) + ")");
  CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request");
  CHECK(rig.maxPublishedSeq.load() > before + 40, "stream flowed (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
}

// S6: NACK negotiated but the host never answers (a lost NACK, a rolled-out cache): the viewer tries
// its bounded rounds, then gives the AU up at the hold and recovers through one IDR request.
void scenario_nack_unanswered_falls_back_to_idr() {
  std::printf("[S6] NACK unanswered: bounded rounds, then IDR fallback\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> target{0};
  LossPlan plan;
  plan.answerNacks = false;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t idx, bool key, uint16_t count) {
    if (!key && count >= 3 && seq >= 40 && target.load() == 0) target = seq;
    return seq == target.load() && idx == 1;
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  pump(host, rig, 1500);
  const uint32_t t = target.load();
  CHECK(t != 0, "a multi-chunk P frame was chosen for the loss");
  CHECK(host.nacks_for(t) >= 1 && host.nacks_for(t) <= 3, "bounded NACK rounds for " + std::to_string(t) + " (" + std::to_string(host.nacks_for(t)) + ")");
  CHECK(host.keyframe_requests() >= requestsBefore + 1, "IDR fallback requested");
  CHECK(host.keyframe_requests() <= requestsBefore + 2, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.maxPublishedSeq.load() > t + 10, "stream resumed (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe at the end");
}

// S7: the host consumed seqs it never sent (its sender queue was cleared by an IDR) and the next AU
// on the wire is that complete IDR. The gap is not loss and the IDR is the resync: no decoder reset
// + IDR request may follow, or every host-answered IDR breeds the next request (the 90/90 pattern).
void scenario_completed_idr_after_seq_gap_needs_no_request() {
  std::printf("[S7] complete IDR behind a seq gap: no extra IDR request\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint64_t keyframesBefore = host.keyframes_sent();
  const uint32_t before = host.last_seq();
  for (int i = 0; i < 3; ++i) {
    pump(host, rig, 300);
    host.SkipSeq();
    (void)host.SendFrame(true, false, qpc_now_us());
  }
  pump(host, rig, 500);
  CHECK(host.keyframes_sent() >= keyframesBefore + 3, "three unsolicited IDRs went out");
  CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request (got " + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.maxPublishedSeq.load() > before + 60, "stream flowed (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
}

// S8: a whole P frame is lost, the host never answers the IDR request, and the source stops. The
// viewer must keep asking on the clock -- not per frame, there are none -- and resume once an IDR
// finally comes.
void scenario_keyframe_wait_retries_on_timer_when_source_stops() {
  std::printf("[S8] keyframe wait + source stop: timer-driven re-requests, then recovery\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<uint32_t> lostP{0};
  LossPlan plan;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t, bool key, uint16_t) {
    if (!key && seq >= 40 && lostP.load() == 0) lostP = seq;
    return seq == lostP.load();  // the whole frame
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  host.SetHonorKeyframeRequests(false);
  CHECK(pump_until(host, rig, 3000, [&]() { return rig.gate.waitForKeyFrame; }), "the lost P frame opened a keyframe wait");
  // Let the first (loss-driven, reason 2) request be consumed, then stop the source.
  idle(host, rig, 50);
  const uint64_t requestsAtStop = host.keyframe_requests();
  const uint64_t stopUs = qpc_now_us();
  idle(host, rig, 2600);
  const uint64_t retries = host.keyframe_requests() - requestsAtStop;
  CHECK(retries >= 2 && retries <= 4, "timer re-asks while nothing arrives: " + std::to_string(retries));
  CHECK(rig.gate.waitForKeyFrame, "still waiting: nothing was sent");
  CHECK(rig.gate.recoveryRetryCount >= 2, "gate counted the retries (" + std::to_string(rig.gate.recoveryRetryCount) + ")");
  // The host is back: the next request is honoured and the stream resumes.
  host.SetHonorKeyframeRequests(true);
  const bool resumed = pump_until(host, rig, 5000, [&]() {
    return !rig.gate.waitForKeyFrame && rig.maxPublishedSeq.load() > lostP.load() + 5;
  });
  CHECK(resumed, "resumed after the IDR (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(host.keyframe_requests() <= requestsAtStop + 6, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsAtStop) + " since the stop)");
  (void)stopUs;
}

// S9: a frozen present anchor makes dense frames read as a decode backlog -> Congested + IDR
// request; the first recovery IDR is lost entirely. The viewer must re-ask on the clock and come
// back through the next IDR, instead of dropping every P frame until the host's next spontaneous
// one (the probe's 60 s / 0 re-requests).
void scenario_congested_first_idr_lost_recovers_by_timer() {
  std::printf("[S9] Congested, first recovery IDR lost: timer re-request, recovery\n");
  FakeHost host;
  ViewerRig rig;
  std::atomic<bool> armed{false};
  std::atomic<uint32_t> lostIdr{0};
  LossPlan plan;
  plan.dropFirstSend = [&](uint32_t seq, uint16_t, bool key, uint16_t) {
    if (armed.load() && key && lostIdr.load() == 0) lostIdr = seq;
    return seq == lostIdr.load();  // the whole IDR
  };
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  rig.pinPresentAnchor = true;  // the renderer stops advancing the anchor: lag climbs 16.7 ms per frame
  armed = true;
  CHECK(pump_until(host, rig, 3000, [&]() { return rig.gate.congestionState == ClientCongestionState::Congested; }),
        "entered Congested (state " + state_name(rig) + ")");
  CHECK(pump_until(host, rig, 3000, [&]() { return lostIdr.load() != 0 && host.keyframe_requests() >= requestsBefore + 2; }),
        "the lost IDR was followed by a second request on the clock (requests " + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  rig.pinPresentAnchor = false;
  CHECK(pump_until(host, rig, 5000, [&]() {
          return rig.gate.congestionState == ClientCongestionState::Normal && !rig.gate.waitForKeyFrame;
        }),
        "back to Normal (state " + state_name(rig) + ")");
  CHECK(host.keyframe_requests() <= requestsBefore + 6, "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + ")");
  CHECK(rig.gate.recoveryRetryCount >= 1, "the gate's timer drove the re-ask");
}

// S10: a still screen -- the host re-encodes its cached picture as synthetic frames every 150 ms
// (a trailing kick per input, the 1 Hz refresh) -- then real content resumes as a burst of three
// frames within a few ms, before the renderer has presented any of them. The burst must not be read
// as a decode backlog (Congested + IDR); the field showed exactly this with streamLag ~0.
void scenario_synthetic_idle_then_real_burst_no_false_congestion() {
  std::printf("[S10] synthetic idle (kicks) then real burst: no false congestion\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
  // The renderer showed the last real frame; from here it does not advance the anchor (synthetic
  // frames never do in viewer_present.cpp, and the burst lands before the next vsync).
  sleep_ms(40);
  rig.pinPresentAnchor = true;
  for (int i = 0; i < 12; ++i) {
    (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
    (void)host.SendFrame(false, true, qpc_now_us());
    sleep_ms(150);
  }
  const uint64_t burstStartUs = qpc_now_us();
  for (int i = 0; i < 3; ++i) {
    (void)host.SendFrame(false, false, burstStartUs + static_cast<uint64_t>(i) * kFrameIntervalUs);
  }
  sleep_ms(120);
  CHECK(rig.gate.congestionTransitionCount == transitionsBefore,
        "no congestion transition (state " + state_name(rig) + ", transitions +" +
            std::to_string(rig.gate.congestionTransitionCount - transitionsBefore) + ")");
  CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request");
  rig.pinPresentAnchor = false;
  pump(host, rig, 300);
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
  CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
}

// S11: the decoder without a reset across a gap. The receiver no longer flushes the decoder when
// a complete IDR closes a seq gap; the MFT's pending-input FIFO (timestamp + synthetic provenance)
// must still map every output to its own input, and nothing decoded before the IDR may surface
// after it. Checked on the real H264Encoder/H264Decoder, no sockets. (Codex condition 3.)
void scenario_decoder_provenance_without_reset_across_gap() {
  std::printf("[S11] decoder: skip a P, feed the IDR without reset -> outputs keep their own stamps, none leak past the IDR\n");
  H264Encoder enc;
  if (!enc.initialize(kWidth, kHeight, 60, 3000000, 600)) { std::printf("  encoder init failed\n"); ++gFailures; return; }
  H264Decoder dec;
  if (!dec.initialize(kWidth, kHeight, 60)) { std::printf("  decoder init failed\n"); ++gFailures; return; }
  std::vector<uint8_t> nv12(static_cast<size_t>(kWidth) * kHeight * 3 / 2, 96);
  struct Au { std::vector<uint8_t> bytes; bool key; int64_t timeHns; bool synthetic; };
  std::vector<Au> aus;
  const int64_t base = 5000000;  // 0.5 s, in 100 ns units
  for (int i = 0; i < 8; ++i) {
    for (uint32_t row = 0; row < kHeight; ++row) nv12[row * kWidth + ((i * 40) % kWidth)] = 235;
    const bool synthetic = (i == 2);
    const bool forceKey = (i == 5);
    enc.set_next_input_synthetic(synthetic);
    std::vector<H264AccessUnit> units;
    const int64_t t = base + static_cast<int64_t>(i) * 166667;
    if (!enc.encode_frame(nv12, forceKey, t, &units)) { std::printf("  encode failed\n"); ++gFailures; return; }
    for (auto& u : units) aus.push_back(Au{u.bytes, u.keyFrame, u.sampleTimeHns, u.synthetic});
  }
  // Drain what an async encoder may still hold, so the IDR forced at 5 is present.
  for (int extra = 0; extra < 4 && aus.size() < 8; ++extra) {
    std::vector<H264AccessUnit> units;
    enc.set_next_input_synthetic(false);
    if (!enc.encode_frame(nv12, false, base + static_cast<int64_t>(8 + extra) * 166667, &units)) break;
    for (auto& u : units) aus.push_back(Au{u.bytes, u.keyFrame, u.sampleTimeHns, u.synthetic});
  }
  size_t idrIndex = 0;
  for (size_t i = 1; i < aus.size(); ++i) if (aus[i].key) { idrIndex = i; break; }
  CHECK(idrIndex >= 3, "an IDR was produced past the first frames (index " + std::to_string(idrIndex) + ")");
  if (idrIndex < 3) return;
  const size_t skipped = idrIndex - 1;  // the P just before the IDR is "lost"
  std::vector<int64_t> outTimes;
  std::vector<bool> outSynthetic;
  auto decode = [&](const Au& au) {
    std::vector<DecodedFrameNv12> out;
    bool overflow = false;
    dec.set_next_input_synthetic(au.synthetic);
    const bool ok = dec.decode_access_unit(au.bytes, au.key, au.timeHns, &out, &overflow);
    for (auto& f : out) { outTimes.push_back(f.sampleTimeHns); outSynthetic.push_back(f.synthetic); }
    return ok;
  };
  for (size_t i = 0; i < aus.size(); ++i) {
    if (i == skipped) continue;  // the gap; no reset follows
    CHECK(decode(aus[i]), "decode of AU " + std::to_string(i) + " ok");
  }
  // Every output carries the stamp of one of its inputs, in input order, never the skipped one.
  CHECK(!outTimes.empty(), "the decoder produced output");
  bool monotonic = true;
  bool onlyKnown = true;
  bool skippedLeaked = false;
  for (size_t i = 0; i < outTimes.size(); ++i) {
    if (i > 0 && outTimes[i] <= outTimes[i - 1]) monotonic = false;
    bool known = false;
    for (size_t k = 0; k < aus.size(); ++k) {
      if (k == skipped) continue;
      if (aus[k].timeHns == outTimes[i]) { known = true; if (outSynthetic[i] != aus[k].synthetic) onlyKnown = false; }
    }
    if (!known) onlyKnown = false;
    if (outTimes[i] == aus[skipped].timeHns) skippedLeaked = true;
  }
  CHECK(monotonic, "output stamps are in input order across the gap");
  CHECK(onlyKnown, "every output stamp + synthetic flag matches its own input");
  CHECK(!skippedLeaked, "the lost frame's stamp never surfaces");
  bool idrSeen = false;
  bool preIdrAfterIdr = false;
  for (const int64_t t : outTimes) {
    if (t == aus[idrIndex].timeHns) idrSeen = true;
    else if (idrSeen && t < aus[idrIndex].timeHns) preIdrAfterIdr = true;
  }
  CHECK(idrSeen, "the IDR decoded");
  CHECK(!preIdrAfterIdr, "nothing decoded before the IDR surfaces after it");
  dec.shutdown();
  enc.shutdown();
}

// S13: the two 2026-09-07 15:15 trace shapes as the host emitted them BEFORE the P11 host fix,
// generated by the fake host: (a) sparse real frames (a still secure desktop, no kicks), 800 ms
// of nothing, then the picture the encoder held surfaces -- captured 30 ms after the last real,
// sent 800 ms later -- followed by fresh frames 2 ms apart (seq 24469 -> 24471); (b) the same but
// the frame after the old one is an IDR with a fresh stamp (seq 24484 -> 24486 -> 24488). The
// viewer's P11 rule (a held resume frame does not anchor) must make both cost nothing: no
// congestion transition, no keyframe request, Normal.
void scenario_pre_fix_host_shapes_no_false_congestion() {
  std::printf("[S13] pre-fix host shapes (old real after silence -> fresh burst; -> IDR -> fresh): viewer stays Normal\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  // shape 2 = (a') the same old-real->fresh burst with the present anchor PINNED while the burst
  // is judged: the renderer has not shown the held frame yet, so nothing but the gate's own rule
  // can keep the fresh frames from reading as an 0.8 s backlog. Deterministic where shapes 0/1
  // depend on the present thread's timing (the sweep-load failure of 2026-09-07).
  for (int shape = 0; shape < 3; ++shape) {
    for (int i = 0; i < 4; ++i) {
      (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      sleep_ms(260);
    }
    const uint64_t lastRealUs = qpc_now_us();
    (void)host.SendFrame(false, false, lastRealUs, false);
    idle(host, rig, 800);  // the hold: nothing on the wire
    const uint64_t requestsBefore = host.keyframe_requests();
    const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
    if (shape == 2) rig.pinPresentAnchor = true;
    (void)host.SendFrame(false, false, lastRealUs + 30000, false);  // the held picture, its true old stamp
    const uint64_t burstUs = qpc_now_us();
    (void)host.SendFrame(shape == 1, false, burstUs, shape == 1);
    for (int i = 1; i < 4; ++i) (void)host.SendFrame(false, false, burstUs + static_cast<uint64_t>(i) * 2000, false);
    if (shape == 2) {
      sleep_ms(60);  // the burst is judged with the anchor still pinned
      rig.pinPresentAnchor = false;
    }
    pump(host, rig, 700);
    const uint64_t requests = host.keyframe_requests() - requestsBefore;
    const uint64_t transitions = rig.gate.congestionTransitionCount - transitionsBefore;
    std::printf("  shape %s: congestion transitions +%llu, keyframe requests +%llu, heldResume=%llu, state %s\n",
                shape == 0 ? "old-real->fresh" : shape == 1 ? "old-real->IDR->fresh" : "old-real->fresh (present anchor pinned)",
                static_cast<unsigned long long>(transitions), static_cast<unsigned long long>(requests),
                static_cast<unsigned long long>(rig.gate.heldResumeFrames), state_name(rig).c_str());
    CHECK(transitions == 0, "no congestion transition (got " + std::to_string(transitions) + ")");
    CHECK(requests == 0, "no keyframe request (got " + std::to_string(requests) + ")");
    CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
    CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
  }
  CHECK(rig.gate.heldResumeFrames >= 1, "the viewer saw the held resume frame(s) (" + std::to_string(rig.gate.heldResumeFrames) + ")");
}

// S14: the product's host-side rule end to end. The fake host runs the real H264Encoder and the
// product's epoch gate on the product's FIFO tags, with wire stamps taken from each AU's own
// input (as hdr.captureQpcUs is). A still screen, a flush (what every backend switch / restart
// does: new epoch, forced key), then frames: on this backend the encoder returns the pre-flush
// picture during the first post-flush call -- the gate must drop it, the first AU on the wire
// after the flush must be the new epoch's IDR, and the viewer must accept it without a request,
// a congestion episode or a keyframe wait.
void scenario_host_epoch_gate_end_to_end() {
  std::printf("[S14] flush -> the gate drops the pre-flush AU, the IDR goes first, the viewer accepts it clean\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  host.SetStampFromAu(true);
  pump(host, rig, 300);
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < 4; ++i) {
      (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      sleep_ms(260);
    }
    (void)host.SendFrame(false, false, qpc_now_us(), false);  // the last pre-flush real input
    idle(host, rig, 800);
    const uint64_t requestsBefore = host.keyframe_requests();
    const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
    const uint64_t droppedBefore = host.gate_dropped_old();
    const uint64_t keysBefore = host.gate_keys_accepted();
    host.Flush();
    for (int i = 0; i < 6; ++i) {
      (void)host.SendFrame(false, false, qpc_now_us(), i == 0);
      sleep_ms(2);
    }
    pump(host, rig, 700);
    const uint64_t droppedOld = host.gate_dropped_old() - droppedBefore;
    std::printf("  round %d (%s): pre-flush AUs dropped by the gate %llu, keys accepted %llu, first seq after flush %u key=%d, transitions +%llu, requests +%llu\n",
                round, host.encoder_backend(), static_cast<unsigned long long>(droppedOld),
                static_cast<unsigned long long>(host.gate_keys_accepted() - keysBefore), host.first_seq_after_flush(),
                host.first_after_flush_key() ? 1 : 0,
                static_cast<unsigned long long>(rig.gate.congestionTransitionCount - transitionsBefore),
                static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore));
    CHECK(host.gate_keys_accepted() - keysBefore == 1, "the new epoch's IDR was accepted once");
    CHECK(host.first_seq_after_flush() != 0 && host.first_after_flush_key(), "the first AU on the wire after the flush is a keyframe");
    CHECK(host.gate_resets() == 0, "no encoder reset was needed");
    CHECK(rig.gate.congestionTransitionCount == transitionsBefore, "no congestion transition");
    CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request");
    CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
    CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
    CHECK(rig.maxPublishedSeq.load() > host.first_seq_after_flush(), "the viewer decoded past the flush");
  }
}

// S15: the sender boundary end to end -- the PRODUCT's sender thread carries the fake host's AUs,
// with its input-epoch fence, and the viewer receives the real sender's output. Two moments per
// round: (a) an old AU held at the sender's permission point (right before its first datagram)
// plus more old AUs queued behind it when the flush lands -> none of them starts, the new epoch's
// IDR is the next AU on the wire; (b) a large old AU already on the wire when the flush lands ->
// it completes (the documented exception) and the IDR follows. The viewer must take both without
// a congestion episode or a keyframe request (the held-resume rule covers the completed old AU's
// stamp), decode past the flush, and see the new epoch's IDR with its own generation.
void scenario_real_sender_flush_boundary_end_to_end() {
  std::printf("[S15] real sender + flush: held/queued old AUs never start, in-flight completes, IDR next, viewer clean\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  plan.realSender = true;
  plan.advertiseNack = false;  // the fake host's NACK answers do not apply in real-sender mode
  if (!start_session(host, rig, plan, /*requestNack=*/false)) { ++gFailures; return; }
  host.SetStampFromAu(true);
  pump(host, rig, 300);
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < 4; ++i) {
      (void)host.TakeKeyframeRequest(rig.ctx.control.keyframeRequests);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      sleep_ms(260);
    }
    const uint64_t requestsBefore = host.keyframe_requests();
    const uint64_t transitionsBefore = rig.gate.congestionTransitionCount;
    const uint64_t dropsBefore = host.real_sender_drops();
    const uint32_t seqBefore = host.last_seq();
    if (round == 0) {
      // (a) hold the next AU at the permission point, queue two more behind it, flush, release.
      host.HoldRealSenderNext();
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      CHECK(host.WaitRealSenderHeld(2000), "the sender reached the permission point and is held");
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      (void)host.SendFrame(false, false, qpc_now_us(), false);
      host.Flush();
      host.ReleaseRealSender();
    } else {
      // (b) a large old AU on the wire, the flush right behind it.
      (void)host.SendFrame(true, false, qpc_now_us(), true);
      sleep_ms(1);
      host.Flush();
    }
    for (int i = 0; i < 6; ++i) {
      (void)host.SendFrame(false, false, qpc_now_us(), i == 0);
      sleep_ms(2);
    }
    pump(host, rig, 700);
    const uint64_t drops = host.real_sender_drops() - dropsBefore;
    std::printf("  round %d: sender dropped %llu AU(s) of the old epoch, first seq after flush %u key=%d, transitions +%llu, requests +%llu, published max seq %u\n",
                round, static_cast<unsigned long long>(drops), host.first_seq_after_flush(),
                host.first_after_flush_key() ? 1 : 0,
                static_cast<unsigned long long>(rig.gate.congestionTransitionCount - transitionsBefore),
                static_cast<unsigned long long>(host.keyframe_requests() - requestsBefore), rig.maxPublishedSeq.load());
    if (round == 0) CHECK(drops >= 1, "the held AU (and what queued behind it) never started");
    CHECK(host.first_seq_after_flush() != 0 && host.first_after_flush_key(), "the first AU the gate let out after the flush is a keyframe");
    CHECK(rig.gate.congestionTransitionCount == transitionsBefore, "no congestion transition");
    CHECK(host.keyframe_requests() == requestsBefore, "no keyframe request");
    CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
    CHECK(!rig.gate.waitForKeyFrame, "not waiting for a keyframe");
    CHECK(rig.maxPublishedSeq.load() > seqBefore + 6, "the viewer decoded past the flush (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  }
}

// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap. Observed on this
// machine's hardware MFT: the zeroed AU is swallowed without an error and without an output, and
// the P frames behind it decode against the references the (un-reset) decoder still holds. So
// there is no decoder-reported failure to recover from -- and no request must be manufactured
// either (that was the 90/90 churn). What is guaranteed: the gate is not stuck waiting, nothing
// storms, and the next good IDR is decoded and shown. A decoder that DOES report the failure
// takes the reason-4 path, which viewer_frame_gate_test covers (test_decode_failure_rebuild_
// threshold); a run of empty outputs takes the reason-5 path. (Codex condition 3.)
void scenario_corrupted_idr_does_not_wedge_or_storm() {
  std::printf("[S12] zeroed complete IDR behind a gap: no wedge, no storm, the next good IDR shows\n");
  FakeHost host;
  ViewerRig rig;
  LossPlan plan;
  if (!start_session(host, rig, plan)) { ++gFailures; return; }
  const uint64_t requestsBefore = host.keyframe_requests();
  host.SkipSeq();  // the gap the IDR closes
  host.CorruptNextKey();
  (void)host.SendFrame(true, false, qpc_now_us());
  CHECK(host.corrupted_keys() == 1, "a zeroed IDR went out");
  pump(host, rig, 700);
  CHECK(!rig.gate.waitForKeyFrame, "not left waiting for a keyframe (state " + state_name(rig) + ")");
  CHECK(host.keyframe_requests() <= requestsBefore + 2,
        "no IDR storm (" + std::to_string(host.keyframe_requests() - requestsBefore) + " requests)");
  // The next good IDR is decoded and published.
  const uint64_t publishedBefore = rig.publishedCount.load();
  const uint32_t goodIdr = host.SendFrame(true, false, qpc_now_us());
  CHECK(goodIdr != 0, "a good IDR went out");
  const bool shown = pump_until(host, rig, 3000, [&]() {
    return rig.publishedCount.load() > publishedBefore && rig.maxPublishedSeq.load() >= goodIdr;
  });
  CHECK(shown, "the good IDR was decoded and published (max seq " + std::to_string(rig.maxPublishedSeq.load()) + ")");
  CHECK(rig.gate.congestionState == ClientCongestionState::Normal, "state " + state_name(rig));
}

}  // namespace

int main() {
  std::cout.setf(std::ios::unitbuf);
  WinsockScope ws;
  if (!ws.ok) {
    std::printf("WSAStartup failed\n");
    return 2;
  }
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("MFStartup failed\n");
    return 2;
  }
  scenario_p_chunk_loss_repaired_by_nack();
  scenario_old_host_without_nack();
  scenario_recovery_idr_chunk_loss_repaired();
  scenario_large_idr_tail_is_not_nacked();
  scenario_reordered_chunks();
  scenario_nack_unanswered_falls_back_to_idr();
  scenario_completed_idr_after_seq_gap_needs_no_request();
  scenario_keyframe_wait_retries_on_timer_when_source_stops();
  scenario_congested_first_idr_lost_recovers_by_timer();
  scenario_synthetic_idle_then_real_burst_no_false_congestion();
  scenario_decoder_provenance_without_reset_across_gap();
  scenario_corrupted_idr_does_not_wedge_or_storm();
  scenario_pre_fix_host_shapes_no_false_congestion();
  scenario_host_epoch_gate_end_to_end();
  scenario_real_sender_flush_boundary_end_to_end();
  MFShutdown();
  if (gFailures == 0) {
    std::printf("viewer_udp_recovery_test: PASS\n");
    return 0;
  }
  std::printf("viewer_udp_recovery_test: FAIL (%d)\n", gFailures);
  return 1;
}
