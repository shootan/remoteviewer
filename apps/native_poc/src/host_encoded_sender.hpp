#pragma once

// Encoded-frame sender queue/thread state (EncodedSendItem, SenderState).
//
// Host split refactor Phase 2-0: this state moved out of native_video_host_main.cpp verbatim so
// it can be read on its own; the struct comment below documents role and thread ownership.
// Phase 2 turns it into the class that owns the matching main() lambdas.

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "native_video_transport.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

struct Args;
struct EncoderState;
struct SessionState;

// H4: the encode thread hands encoded frames to this sender instead of pacing the wire
// inline. Pacing a 60ms keyframe used to stall the next frame's encode start directly.
// Depth is 2: an arriving keyframe supersedes the whole backlog, and a delta that would
// overflow the queue drops the backlog and requests a fresh keyframe -- an encoded delta
// must never be skipped silently or the reference chain corrupts until the next IDR.
struct EncodedSendItem {
  std::vector<uint8_t> bytes;
  UdpVideoChunkHeader udpHdr{};
  bool keyFrame = false;
  uint64_t frameIntervalUs = 0;
  // qpc time the encode/main thread handed this AU to the sender queue. The sender subtracts it
  // from its actual wire-start to expose queueWaitUs -- the gap between "AU ready" and "bytes on
  // the wire" -- so a stutter can be pinned to AU supply vs the sender/wire, not guessed.
  uint64_t enqueueUs = 0;
  // Media epoch live when this item was handed to the sender. streamGeneration is a
  // target-selection id that does NOT change on a session rollover, so it cannot fence a delta
  // encoded for the previous client. The sender drops any dequeued item whose mediaEpoch no
  // longer matches the current one (see mediaSessionEpoch).
  uint64_t mediaEpoch = 0;
};

// Encoded-frame sender (Phase 1-2 state struct). The encode/main thread enqueues AUs; the sender
// thread paces them onto the wire. Depth is 2: a keyframe supersedes the backlog, an overflowing
// delta drops the backlog and requests a fresh keyframe, and deltas are held back until that key
// passes (waitingForKey). mediaSessionEpoch fences items of a previous session at dequeue. See the
// comment blocks in main() (H4 sender, session media barrier, IDR telemetry) for the rationale.
// thread: queue/peer/waitingForKey under mu (main + sender + reader rollover); the atomics are the
// cross-thread signals (sender -> main: sendFailed/recoveryPending/requestKey; reader -> main:
// udpPeer*); the plain sent*/udpTx*/heldFrames/nonKeyAu*/firstKeyEnqueuedUs counters are
// main-thread stats-interval accumulators.
struct SenderState {
  // Config (REMOTE60_NATIVE_SENDER_MAX_CADENCE_HOLD_US), fixed after startup.
  uint32_t maxCadenceHoldUs = 0;
  bool cadenceSmoothing = false;
  // UDP peer as the reader thread sees it (the render loop picks up changes through the atomics).
  sockaddr_in udpPeer{};
  bool udpPeerReady = false;
  std::atomic<uint32_t> udpPeerIpNet{0};
  std::atomic<uint16_t> udpPeerPortNet{0};
  std::atomic<bool> udpPeerChanged{false};
  // Queue + peer the sender thread writes to (all under mu).
  std::mutex mu;
  std::condition_variable cv;
  std::deque<EncodedSendItem> queue;
  sockaddr_in peer{};
  bool peerReady = false;
  bool waitingForKey = false;  // deltas held back until the requested keyframe passes
  // Session media barrier: bumped (under mu) by the rollover transaction; starts at 1 like clientSession.epoch.
  std::atomic<uint64_t> mediaSessionEpoch{1};
  std::atomic<bool> stop{false};
  std::atomic<bool> sendFailed{false};
  std::atomic<bool> requestKey{false};
  // Sender -> main: a same-epoch transport error re-armed the barrier; main forces the next key
  // and arms the trailing kick (the only recovery signal that works on a static screen).
  std::atomic<bool> recoveryPending{false};
  std::atomic<uint64_t> barrierRearmCount{0};  // same-epoch send-failure barrier re-arms (telemetry)
  std::atomic<uint64_t> dropCount{0};
  std::atomic<uint64_t> txFrames{0};
  std::atomic<uint64_t> txChunks{0};
  std::atomic<uint64_t> txBytes{0};
  std::atomic<uint64_t> txNoPeer{0};
  std::atomic<uint64_t> lastSendStartUs{0};
  std::atomic<uint64_t> sendDurSumUs{0};
  std::atomic<uint64_t> sendDurMaxUs{0};
  std::atomic<uint64_t> sendCount{0};
  // IDR telemetry per media epoch (sender thread writes; reset by the rollover). Diagnostic only.
  std::atomic<uint64_t> firstKeyWireUs{0};
  std::atomic<uint64_t> lastKeyAuBytes{0};
  std::atomic<uint64_t> lastKeyAuChunks{0};
  std::thread thread;
  // Main-thread stats-interval accumulators (reset every stats print).
  uint64_t sentFrames = 0;
  uint64_t sentBytes = 0;
  uint64_t heldFrames = 0;             // AUs the queue policy discarded (backlog resync / waiting for IDR)
  uint64_t nonKeyAuWhileWaiting = 0;   // delta AUs seen while the barrier was closed
  uint64_t firstKeyEnqueuedUs = 0;     // wire-time stamp of the first key enqueued this media epoch
  uint64_t udpTxFrames = 0;
  uint64_t udpTxChunks = 0;
  uint64_t udpTxBytes = 0;
  uint64_t udpTxFail = 0;
  uint64_t udpTxNoPeer = 0;

  // --- behaviour (Phase 2-3: former main() lambdas start_encoded_sender / pump_udp_hello;
  //     bodies in host_encoded_sender.cpp) ---
  // Start the sender thread (UDP + H.264 only). It dequeues AUs, paces them onto the wire and
  // drives the media barrier; see the thread body for the full policy.
  void StartThread(VideoTransport transport, bool useH264, const Args& args, SessionState& clientSession);
  // Consume a reader-thread peer change: swap the peer and roll the media session over in one
  // transaction (drop backlog, hold deltas until an IDR, bump the epoch, force a keyframe).
  void PumpUdpHello(VideoTransport transport, EncoderState& encoder);
};

}  // namespace remote60::native_poc
