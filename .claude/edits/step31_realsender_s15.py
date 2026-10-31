import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
S = ROOT + r'\apps\native_poc\src'
R = S + r'\viewer_udp_recovery_test.cpp'

edit(R, [
# includes for the real sender
(r"""#include "host_epoch_gate.hpp"
#include "mf_h264_codec.hpp"
""",
 r"""#include "host_args.hpp"
#include "host_encoded_sender.hpp"
#include "host_epoch_gate.hpp"
#include "host_main_loop_mailbox.hpp"
#include "host_session.hpp"
#include "mf_h264_codec.hpp"
"""),
# plan flag
(r"""  uint32_t keyInterChunkDelayUs = 0;  // pace an IDR's chunks (the large-IDR tail scenario)""",
 r"""  uint32_t keyInterChunkDelayUs = 0;  // pace an IDR's chunks (the large-IDR tail scenario)
  // Route every AU through the PRODUCT's sender thread (SenderState::StartThread, with its input-
  // epoch fence) instead of the fake host's direct sendto: what the viewer then receives is the
  // real sender's output stream. The loss plan / NACK answers do not apply in this mode.
  bool realSender = false;"""),
# Start: real sender
(r"""    nv12_.assign(static_cast<size_t>(kWidth) * kHeight * 3 / 2, 128);
    stop_ = false;
    reader_ = std::thread([this]() { ReaderLoop(); });
    return true;
  }""",
 r"""    nv12_.assign(static_cast<size_t>(kWidth) * kHeight * 3 / 2, 128);
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
  }"""),
# Stop: real sender first
(r"""  void Stop() {
    stop_ = true;
    if (reader_.joinable()) reader_.join();
    if (sock_ != INVALID_SOCKET) {
      closesocket(sock_);
      sock_ = INVALID_SOCKET;
    }
    enc_.shutdown();
  }""",
 r"""  void Stop() {
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
  uint64_t real_sender_tx_frames() const { return realSender_.txFrames.load(); }"""),
# Flush uses the atomic epoch
(r"""  void Flush() {
    ++epoch_;
    forceKeyPending_ = true;""",
 r"""  void Flush() {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    forceKeyPending_ = true;"""),
# SendFrame: epoch reads + pass the AU epoch to SendAu
(r"""    enc_.set_next_input_synthetic(synthetic);
    enc_.set_next_input_epoch(epoch_);""",
 r"""    enc_.set_next_input_synthetic(synthetic);
    enc_.set_next_input_epoch(epoch_.load(std::memory_order_acquire));"""),
(r"""      switch (epoch_gate_judge(gate_, epoch_, au.inputEpoch, au.keyFrame, au.bytes.size(), qpc_now_us())) {""",
 r"""      switch (epoch_gate_judge(gate_, epoch_.load(std::memory_order_acquire), au.inputEpoch, au.keyFrame, au.bytes.size(), qpc_now_us())) {"""),
(r"""      lastSeq = SendAu(au.bytes, au.keyFrame, stampFromAu_ ? au.synthetic : synthetic, wireCaptureUs);""",
 r"""      lastSeq = SendAu(au.bytes, au.keyFrame, stampFromAu_ ? au.synthetic : synthetic, wireCaptureUs, au.inputEpoch);"""),
# SendAu: signature + real-sender enqueue
(r"""  uint32_t SendAu(const std::vector<uint8_t>& bytes, bool key, bool synthetic, uint64_t captureUs) {""",
 r"""  uint32_t SendAu(const std::vector<uint8_t>& bytes, bool key, bool synthetic, uint64_t captureUs, uint64_t auEpoch = 0) {"""),
(r"""    {
      std::lock_guard<std::mutex> lk(cacheMu_);
      cache_.push_back(au);
      while (cache_.size() > 24) cache_.pop_front();
    }
    SendChunks(au, nullptr, 0);
    return au.seq;
  }""",
 r"""    {
      std::lock_guard<std::mutex> lk(cacheMu_);
      cache_.push_back(au);
      while (cache_.size() > 24) cache_.pop_front();
    }
    if (plan_.realSender) {
      // The product's sender does the chunking, pacing and the input-epoch fence; this is the
      // same hand-off host_stage_encode_send_h264_au.cpp makes.
      EncodedSendItem item;
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
  }"""),
# ReaderLoop: hand the peer to the real sender
(r"""          peerKnown_ = true;
          helloFeatures_ = hello.features;""",
 r"""          peerKnown_ = true;
          if (plan_.realSender) {
            std::lock_guard<std::mutex> lk(realSender_.mu);
            realSender_.peer = from;
            realSender_.peerReady = true;
          }
          helloFeatures_ = hello.features;"""),
# members
(r"""  EpochGate gate_;
  uint64_t epoch_ = 1;
  bool forceKeyPending_ = false;""",
 r"""  EpochGate gate_;
  std::atomic<uint64_t> epoch_{1};
  bool forceKeyPending_ = false;
  // Real-sender mode (LossPlan::realSender).
  Args realArgs_;
  SessionState realSession_;
  MainLoopMailbox realMailbox_;
  SenderState realSender_;
  std::mutex holdMu_;
  std::condition_variable holdCv_;
  bool holdArmed_ = false;
  bool holdHeld_ = false;
  bool holdRelease_ = false;"""),
# S15
(r"""// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap.""",
 r"""// S15: the sender boundary end to end -- the PRODUCT's sender thread carries the fake host's AUs,
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

// S12: a complete IDR the decoder cannot use (payload zeroed) behind a seq gap."""),
(r"""  scenario_pre_fix_host_shapes_no_false_congestion();
  scenario_host_epoch_gate_end_to_end();
""",
 r"""  scenario_pre_fix_host_shapes_no_false_congestion();
  scenario_host_epoch_gate_end_to_end();
  scenario_real_sender_flush_boundary_end_to_end();
"""),
])

# CMake: the recovery test now links the real sender
edit(ROOT + r'\apps\native_poc\CMakeLists.txt', [
(r"""add_executable(remote60_viewer_udp_recovery_test
  src/viewer_video_receiver.cpp""",
 r"""add_executable(remote60_viewer_udp_recovery_test
  src/host_encoded_sender.cpp
  src/host_net_io.cpp
  src/directory_client.cpp
  src/viewer_video_receiver.cpp"""),
(r"""target_link_libraries(remote60_viewer_udp_recovery_test PRIVATE
  common
  ws2_32""",
 r"""target_link_libraries(remote60_viewer_udp_recovery_test PRIVATE
  capture
  common
  ws2_32
  runtimeobject
  windowsapp
  advapi32"""),
])
print('step31 edits ok')
