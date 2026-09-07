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
    reader_ = std::thread([this]() { ReaderLoop(); });
    return true;
  }

  void Stop() {
    stop_ = true;
    if (reader_.joinable()) reader_.join();
    if (sock_ != INVALID_SOCKET) {
      closesocket(sock_);
      sock_ = INVALID_SOCKET;
    }
    enc_.shutdown();
  }

  uint16_t port() const { return port_; }
  bool peer_known() const { return peerKnown_.load(); }

  // Encode the next picture and put it on the wire. `noisy` makes a big AU (a large IDR / a
  // multi-chunk P), `synthetic` re-sends the previous picture flagged as a host kick / refresh.
  // Returns the seq the AU went out with (0 if the encoder produced nothing this call).
  uint32_t SendFrame(bool forceKey, bool synthetic, uint64_t captureUs, bool noisy = true) {
    if (!synthetic) NextPicture(noisy);
    std::vector<H264AccessUnit> units;
    enc_.set_next_input_synthetic(synthetic);
    if (!enc_.encode_frame(nv12_, forceKey, static_cast<int64_t>(captureUs) * 10, &units)) {
      std::printf("  fake host: encode_frame failed\n");
      return 0;
    }
    uint32_t lastSeq = 0;
    for (auto& au : units) {
      lastSeq = SendAu(au.bytes, au.keyFrame, synthetic, captureUs);
      if (au.keyFrame) ++keyframesSent_;
    }
    return lastSeq;
  }

  // The product host consumes a seq for a frame its sender queue then discards (EnqueueKey clears
  // the queue; HoldForKey drops deltas): the next AU on the wire has a gap without any loss.
  void SkipSeq() { ++seq_; }

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

  uint32_t SendAu(const std::vector<uint8_t>& bytes, bool key, bool synthetic, uint64_t captureUs) {
    CachedAu au;
    au.seq = ++seq_;
    au.payload = bytes;
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
};

// ---------------------------------------------------------------------------------------------
// The viewer under test: the product receive thread on a real socket, plus a stand-in renderer
// ---------------------------------------------------------------------------------------------

struct ViewerRig {
  ViewerState ctx;
  Args args;
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
    // The same handshake connect_media_socket runs, with the same options.
    UdpHelloOptions hello;
    hello.budgetMs = 2000;
    hello.sliceMaxMs = 200;
    hello.retrySleepMs = 20;
    hello.requestNack = requestNack;
    uint32_t ackFeatures = 0;
    std::string error;
    if (!udp_hello_handshake(s, hello, nullptr, &error, &ackFeatures)) {
      std::printf("  viewer: hello failed: %s\n", error.c_str());
      return false;
    }
    (void)set_recv_timeout(s, recvTimeoutMs);
    ctx.session.sock = s;
    ctx.session.udpHelloAckFeatures = ackFeatures;
    ctx.session.hostSupportsNack = requestNack && (ackFeatures & kUdpFeatureVideoNack) != 0;
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
bool start_session(FakeHost& host, ViewerRig& rig, const LossPlan& plan, bool requestNack = true,
                   uint64_t holdUs = 120000) {
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
  MFShutdown();
  if (gFailures == 0) {
    std::printf("viewer_udp_recovery_test: PASS\n");
    return 0;
  }
  std::printf("viewer_udp_recovery_test: FAIL (%d)\n", gFailures);
  return 1;
}
