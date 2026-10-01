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
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "host_net_io.hpp"
#include "host_wire_limiter.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc {

struct Args;
struct EncoderState;
struct MainLoopMailbox;
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
  // Flush epoch of the input this AU was encoded from (H264AccessUnit::inputEpoch, P11). The
  // sender drops a dequeued item whose epoch is behind CaptureState::inputEpoch: an AU queued
  // before a flush must not start on the wire after it (host_epoch_gate.hpp states the scope --
  // one AU already being chunked when the flush happens completes). 0 = untagged (raw path).
  uint64_t inputEpoch = 0;
};

// Encoded-frame sender (Phase 1-2 state struct). The encode/main thread enqueues AUs; the sender
// thread paces them onto the wire. Depth is 2: a keyframe supersedes the backlog, an overflowing
// delta drops the backlog and requests a fresh keyframe, and deltas are held back until that key
// passes (waitingForKey). mediaSessionEpoch fences items of a previous session at dequeue. See the
// comment blocks in main() (H4 sender, session media barrier, IDR telemetry) for the rationale.
// thread: queue/peer/waitingForKey under mu (main + sender + reader rollover); the atomics are the
// cross-thread signals (sender -> main: sendFailed; reader -> main: udpPeer*); keyframe requests
// go through MainLoopMailbox; the plain sent*/udpTx*/heldFrames/nonKeyAu*/firstKeyEnqueuedUs
// counters are main-thread stats-interval accumulators.
struct SenderState {
  // UDP pacing env config (REMOTE60_NATIVE_H264_NO_PACING / UDP_PACE_PEAK_* / UDP_KEYFRAME_PACE_PEAK_BPS),
  // fixed after startup; EncoderState::ApplyTarget derives the live pacing budget from these.
  bool noPacingH264 = false;
  uint32_t udpPacePeakPercent = 0;
  uint32_t udpPacePeakFloorBps = 0;
  uint32_t udpKeyframePacePeakBps = 0;
  // Config (REMOTE60_NATIVE_SENDER_MAX_CADENCE_HOLD_US), fixed after startup.
  uint32_t maxCadenceHoldUs = 0;
  bool cadenceSmoothing = false;
  // cross-thread: live egress parameters. pacePeakBps is written by the main loop
  // (EncoderState::ApplyTarget), fecInterleaved by the UDP handshake / control thread from the
  // viewer's hello, keyframePacePeakBps once at startup; the sender thread reads all three. These
  // were process globals in host_net_io.hpp until ledger H-22 -- the fields above are the policy
  // INPUTS (percent/floor), these are the derived and negotiated values.
  std::atomic<uint32_t> pacePeakBps{0};
  std::atomic<uint32_t> keyframePacePeakBps{100000000};
  std::atomic<bool> fecInterleaved{false};
  // Single-chunk frames chunked with their own size as the stride (UdpEgressConfig::
  // fecSingleChunkTightStride). Fixed at startup from REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE
  // (unset / 1 = on, 0 = the padded layout); read by the sender through EgressSnapshot.
  std::atomic<bool> fecSingleChunkTightStride{true};
  // ---- Video NACK (selective retransmit) ----
  // Set from the client's Hello: true only when the client advertised kUdpFeatureVideoNack, so an
  // old client is never handed retransmits it did not ask for. Reader thread writes, sender reads.
  std::atomic<bool> nackEnabled{false};
  struct CachedAu {
    uint64_t generation = 0;
    uint32_t seq = 0;
    uint32_t mtu = 0;
    // The stride policy the AU was first sent with, so the replay computes the same geometry
    // (udp_chunk_geometry) -- a replayed chunk with a different chunkStride makes the receiver
    // discard the assembly it was meant to repair.
    bool tightSingleChunk = true;
    // Session ownership (bitrate-hard-cap r4 R2): the (media epoch, peer) this AU belongs to. A
    // replay is fenced to the SAME session -- a cached AU of peer A must never go out to peer B after
    // a rollover. mediaEpoch is the sender.mediaSessionEpoch the AU was stamped under.
    uint64_t mediaEpoch = 0;
    uint32_t peerIpNet = 0;
    uint16_t peerPortNet = 0;
    // The input epoch the AU was encoded from (r5 G2): a replay is fenced to it at send time, so a
    // cached AU whose input epoch is now stale (an input flush happened after it was cached) is not
    // replayed -- the original's mid-AU exception is not extended to a new replay of a flushed AU.
    uint64_t inputEpoch = 0;
    // The first-packet fence (r4 R2): the AU is cached at send-START (so an in-flight NACK is a hit),
    // but it must not be replayable until its original send has actually put at least one datagram on
    // the wire. An AU the F3 input fence aborted with 0 datagrams stays false and is never replayed.
    bool startedOnWire = false;
    UdpVideoChunkHeader baseHeader{};
    // shared so Drain can snapshot the payload under the cache lock and send OUTSIDE it (r5 G3: the
    // reader must not block behind a replay's token wait), without copying the whole AU per chunk.
    std::shared_ptr<std::vector<uint8_t>> payload;
  };
  static constexpr size_t kNackCacheMaxAus = 24;
  std::mutex nackCacheMu;
  std::deque<CachedAu> nackCache;  // most-recent at back; bounded by kNackCacheMaxAus
  std::atomic<uint64_t> nackRetransmitChunks{0};  // telemetry: chunks replayed answering NACKs
  std::atomic<uint64_t> nackRequests{0};          // telemetry: NACK packets served
  std::atomic<uint64_t> nackMisses{0};            // telemetry: NACKs for an AU no longer cached
  std::atomic<uint64_t> nackSuppressed{0};        // telemetry: retransmits skipped over the budget
  // ---- Hard wire-rate cap (bitrate-hard-cap r1) ----
  // One token bucket for everything this stream puts on the wire -- data, parity, NACK replay --
  // charged each datagram's length + 28 (IP/UDP). The sender thread waits on it (never drops a
  // video chunk); the reader thread spends non-blockingly for NACK (a replay that does not fit is
  // suppressed, as the old 15%-of-peak bucket did, but now from the SAME budget so there is no lane
  // around the cap). Constructed lazily by StartWireCap with the qpc clock and a cancellable sleep;
  // null until then (cap inactive -> legacy pacing-only behaviour). Rate follows the active bitrate
  // through UpdateWireCap (ApplyTarget); a rate change never refills the bucket (plan point 4).
  std::unique_ptr<WireLimiter> wireLimiter;
  bool wireCapEnabled = false;  // REMOTE60_NATIVE_WIRE_CAP (default on); fixed after startup
  // Cap-OFF fallback NACK budget (bitrate-hard-cap r2): when the hard cap is off (kill-switch, or no
  // limiter) the shared bucket does not bound the replay, so the old flood defence stays -- a token
  // bucket at ~15% of the live send rate, ~0.5 s burst. With the cap ON the shared wire bucket bounds
  // NACK instead and this is unused. Guarded by nackBudgetMu; refilled on demand.
  std::mutex nackBudgetMu;
  uint64_t nackBudgetTokensBytes = 0;
  uint64_t nackBudgetLastUs = 0;
  // Non-blocking: spend `bytes` from the cap-off fallback bucket, false if over budget. (reader thread)
  bool NackFallbackTryAcquire(uint64_t bytes);
  // Pending replays (bitrate-hard-cap r3 F1): a NACK does NOT replay on the reader thread. The reader
  // only RECORDS the request here (bounded, merged per (generation, seq), with a deadline); the sender
  // thread drains the list every loop and replays the missing chunks through the SAME wire budget,
  // keeping a request until its chunks have ACTUALLY been sent, or its deadline passes, or its epoch
  // ends. A single reader-side TryAcquire (r2) could suppress the whole replay under a full cap bucket
  // and drop the request for ever; this retries across loops as tokens refill, so a real hole lost
  // during a cap-paced send is recovered once the AU is cached and the budget allows. The reader never
  // blocks; the sender never blocks indefinitely (bounded list, per-datagram non-blocking spend,
  // deadline). Merge, max count and stop/epoch cancellation are kept.
  struct PendingReplay {
    uint64_t generation = 0;
    uint32_t seq = 0;
    uint64_t createdUs = 0;
    // Session ownership (r4 R2): the (media epoch, peer) the NACK was recorded under. The sender
    // drops the request on a rollover (mediaEpoch mismatch) and only ever replays to this peer.
    uint64_t mediaEpoch = 0;
    uint32_t peerIpNet = 0;
    uint16_t peerPortNet = 0;
    std::vector<uint16_t> missing;  // remaining ascending, deduped, capped at kUdpVideoNackMaxMissing
  };
  static constexpr size_t kMaxPendingReplays = 8;         // bounded: oldest dropped past this
  static constexpr uint64_t kReplayDeadlineUs = 1'000'000;  // give up a replay request after this
  std::mutex pendingReplayMu;
  std::deque<PendingReplay> pendingReplays;
  // A lock-free count of pending replays, a HINT for the sender to choose a bounded timed wait (for
  // deadline/uncached cleanup) vs an indefinite one. Written under pendingReplayMu; read relaxed. It
  // is NOT the wake signal -- that is newReplayWork below, set under sender.mu to close the lost-wake
  // the atomic-on-a-different-mutex had (r5 G3).
  std::atomic<size_t> pendingReplayCount{0};
  std::atomic<uint64_t> replayServedChunks{0};   // telemetry: chunks actually replayed
  std::atomic<uint64_t> replayDroppedRequests{0};  // telemetry: requests dropped at the deadline
  // Reader thread: record a NACK request (merged per (gen,seq), bounded) for `peer` under the current
  // media epoch, and wake the sender. Does NOT send.
  void RecordReplayRequest(uint64_t generation, uint32_t seq, const sockaddr_in& peer,
                           uint64_t mediaEpoch, const uint16_t* missing, uint16_t count);
  // The wake signal for a newly recorded replay (r5 G3). Set true by RecordReplayRequest UNDER
  // sender.mu (the same mutex the sender's cv waits on) and notified, so a record that lands between
  // the sender's predicate check and its sleep is never missed. The sender clears it when it wakes.
  bool newReplayWork = false;
  // Sender thread: replay what is cached, owned by `currentEpoch`/peer, started on the wire, and fits
  // the budget now; drop requests of a past epoch or past their deadline. maxChunks bounds one call's
  // replay work (0 = unlimited, the between-AU drain; a small N is the per-datagram interleave). The
  // `peer`/`currentEpoch` are the session the sender is serving now. Returns whether any request remains.
  bool DrainPendingReplays(SOCKET sock, const sockaddr_in& peer, uint64_t nowUs, uint64_t currentEpoch,
                           size_t maxChunks = 0);
  // Sender thread: mark a cached AU as having started on the wire (the first-packet fence, r4 R2).
  void MarkAuStartedOnWire(uint64_t generation, uint32_t seq);
  // Sender thread (rollover): drop every pending replay and cached AU of the old session.
  void ClearReplayStateForRollover();
  uint32_t wireCapMtu = 1200;   // clamp_udp_mtu(args.udpMtu); the Lmax source for the bucket depth
  std::atomic<uint64_t> wireCapBps{0};  // the cap now in force (telemetry; 0 = disabled)
  // Build the limiter (idempotent) and set the cap. enabled=false sets rate 0 (cap off).
  void StartWireCap(uint64_t capBps, uint32_t mtu, bool enabled);
  // A new active bitrate (ApplyTarget / ABR / governor fps step): move the cap, preserving credit.
  void UpdateWireCap(uint64_t capBps);
  // The egress passed to the send path: the limiter when the cap is active, no test sink.
  WireEgress MakeWireEgress() {
    WireEgress w;
    w.limiter = wireLimiter.get();
    return w;
  }
  // Cache one just-sent AU for possible retransmit; drops the oldest past the bound. (sender thread)
  // `tightSingleChunk` is the egress.fecSingleChunkTightStride the AU was chunked with. The AU is
  // tagged with its session (media epoch, peer) so a replay is fenced to the same session (r4 R2);
  // it is cached at send-START with startedOnWire=false and marked true once its first datagram goes
  // out (MarkAuStartedOnWire) -- an aborted 0-packet AU stays unreplayable.
  void StoreAu(uint64_t generation, uint32_t seq, const UdpVideoChunkHeader& baseHeader,
               uint32_t mtu, bool tightSingleChunk, uint64_t mediaEpoch, uint64_t inputEpoch,
               const sockaddr_in& peer, const uint8_t* payload, size_t payloadSize);
  // Answer a NACK: replay the requested chunks of (generation, seq) if still cached. (reader thread)
  void RetransmitAu(SOCKET sock, const sockaddr_in& peer, uint64_t generation, uint32_t seq,
                    const uint16_t* missing, uint16_t count);
  // Taken once per dequeue so one frame is paced and chunked by a single consistent set.
  UdpEgressConfig EgressSnapshot() const {
    UdpEgressConfig c;
    c.pacePeakBps = pacePeakBps.load(std::memory_order_relaxed);
    c.keyframePacePeakBps = keyframePacePeakBps.load(std::memory_order_relaxed);
    c.fecInterleaved = fecInterleaved.load(std::memory_order_relaxed);
    c.fecSingleChunkTightStride = fecSingleChunkTightStride.load(std::memory_order_relaxed);
    return c;
  }
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
  // The host's flush epoch (CaptureState::inputEpoch), read by the sender thread to fence AUs of
  // another epoch (P11). nullptr = the fence is inactive (legacy / tests), the only case in which
  // an untagged item passes. With the fence active an item is sent only if its epoch EQUALS the
  // current one at the permission point -- old, untagged (0) and future epochs are all dropped.
  //
  // Permission point: right before the first datagram, after pacing. What the fence guarantees is
  // therefore: no AU whose first datagram was not yet permitted when the flush landed starts on
  // the wire after it. The exception is one AU per flush whose first datagram WAS permitted before
  // the flush (the window between that check and the first sendto is a few microseconds and is not
  // closed; the AU then completes with the metadata it was encoded with -- nothing is relabelled).
  // A single sender thread means at most one such AU exists at a time.
  const std::atomic<uint64_t>* inputEpochRef = nullptr;
  std::atomic<uint64_t> inputEpochDropCount{0};
  // Test seam: called after pacing, right before the permission check for the first datagram, so a
  // test can hold the sender there, move the epoch and resume. Never set in production.
  std::function<void(const EncodedSendItem&)> beforeFirstDatagramHook;
  std::atomic<bool> stop{false};
  std::atomic<bool> sendFailed{false};
  // requestKey / recoveryPending used to live here: two flags for "the stream needs an IDR",
  // consumed at two different points in the tick because one of them was only read after a real
  // frame had been popped. Both are RequestKeyframe posts on the mailbox now. (Phase 4.)
  std::atomic<uint64_t> barrierRearmCount{0};  // same-epoch send-failure barrier re-arms (telemetry)
  std::atomic<uint64_t> dropCount{0};
  std::atomic<uint64_t> txFrames{0};
  std::atomic<uint64_t> txChunks{0};
  std::atomic<uint64_t> txBytes{0};
  // Per-flow wire accounting (quality r1), cumulative like txBytes. txBytes stays what it always
  // was -- AU payload bytes -- so every existing reader keeps its meaning; these are the rest of
  // what leaves the socket. Datagram payloads; IP/UDP headers are estimated at the stats line.
  std::atomic<uint64_t> txParityBytes{0};       // FEC parity datagram payload
  std::atomic<uint64_t> txChunkHeaderBytes{0};  // UdpVideoChunkHeader on every video datagram
  std::atomic<uint64_t> txVideoDatagrams{0};    // data + parity
  std::atomic<uint64_t> txNackBytes{0};         // NACK replays, header included
  std::atomic<uint64_t> txNackDatagrams{0};
  // Actual-wire accounting (bitrate-hard-cap r3 F4): bytes (len + 28) and datagrams that ACTUALLY
  // left the socket for the live data/parity path, accumulated per datagram the instant it succeeds
  // -- so a partially-sent AU (epoch cancel / transport error) still counts what went out, unlike
  // txBytes/txParityBytes which are payload totals added only on a whole-AU Sent. These are the
  // figure to reconcile against an OS/receiver observation; they are distinct from the limiter's
  // pre-send RESERVATION (WireLimiter::spent_bytes, charged before each datagram).
  std::atomic<uint64_t> txActualWireBytes{0};
  std::atomic<uint64_t> txActualWireDatagrams{0};
  std::atomic<uint64_t> txControlBytes{0};      // control channel as sent (UDP: channel datagrams
                                                // incl. its fragment headers/retransmits; TCP: payload)
  std::atomic<uint64_t> txControlDatagrams{0};  // UDP control datagrams (0 on TCP)
  std::atomic<uint64_t> txNoPeer{0};
  std::atomic<uint64_t> lastSendStartUs{0};
  std::atomic<uint64_t> sendDurSumUs{0};
  std::atomic<uint64_t> sendDurMaxUs{0};
  std::atomic<uint64_t> sendCount{0};
  // A key AU is being put on the wire now for the current media epoch -- set by the sender thread for
  // the whole duration of a key AU's (cap-paced) send, cleared when it finishes or aborts, and on a
  // rollover. The encode gate reads it so a repeated recovery request does not force a SECOND IDR
  // while one is still on the wire: at a low cap a 155-208 KB IDR takes 0.2-1.4 s to send, far longer
  // than the encode-side force-key latch, so without this a frame-gate re-ask could pile IDRs up.
  // (bitrate-hard-cap r2, plan r2 point 4 -- the recovery IDR's encode->queue->wire lifetime.)
  std::atomic<bool> keyAuOnWire{false};
  std::atomic<uint64_t> keyAuOnWireSinceUs{0};
  // The key AU on the wire is identified by (media epoch, generation, INPUT epoch, SEQ) -- r3 F2 gave
  // it (media epoch, generation); r5 G1 adds input epoch + seq so a stale flag cannot suppress a NEW
  // key the SAME media/gen but a newer input epoch needs (an input flush bumps inputEpoch only), and
  // so one key's wire handover clears only ITS OWN flag, not a different key enqueued meanwhile.
  std::atomic<uint64_t> keyAuOnWireMediaEpoch{0};
  std::atomic<uint64_t> keyAuOnWireGeneration{0};
  std::atomic<uint64_t> keyAuOnWireInputEpoch{0};
  std::atomic<uint32_t> keyAuOnWireSeq{0};
  // A key AU that is ENQUEUED but not yet on the wire (r4 R3; identity extended r5 G1). keyAuOnWire
  // only covers the wire send; a key can sit in the sender queue behind a long preceding AU, and during
  // that wait a recovery re-request would force ANOTHER key. This closes the encode->queue gap. It is
  // ended on EVERY exit of its own key: wire entry (-> keyAuOnWire), input-fence/dequeue drop of that
  // key, a queue clear that discards it, and rollover -- each clearing ONLY the matching (gen,seq)
  // owner, never a different key's flag. The 6 s safety bound stays as a last resort.
  std::atomic<bool> keyAuQueued{false};
  std::atomic<uint64_t> keyAuQueuedSinceUs{0};
  std::atomic<uint64_t> keyAuQueuedMediaEpoch{0};
  std::atomic<uint64_t> keyAuQueuedGeneration{0};
  std::atomic<uint64_t> keyAuQueuedInputEpoch{0};
  std::atomic<uint32_t> keyAuQueuedSeq{0};
  // r5 G1 ownership helpers (sender thread). MarkKeyQueued/OnWire stamp identity; the Clear* end a
  // key's lifetime only when it owns the current flag, so discarding an invalid key is a real
  // completion (not a 6 s wait) and never erases a different key's flag.
  void MarkKeyQueued(uint64_t mediaEpoch, uint64_t generation, uint64_t inputEpoch, uint32_t seq, uint64_t nowUs) {
    keyAuQueuedMediaEpoch.store(mediaEpoch, std::memory_order_relaxed);
    keyAuQueuedGeneration.store(generation, std::memory_order_relaxed);
    keyAuQueuedInputEpoch.store(inputEpoch, std::memory_order_relaxed);
    keyAuQueuedSeq.store(seq, std::memory_order_relaxed);
    keyAuQueuedSinceUs.store(nowUs, std::memory_order_release);
    keyAuQueued.store(true, std::memory_order_release);
  }
  void MarkKeyOnWire(uint64_t mediaEpoch, uint64_t generation, uint64_t inputEpoch, uint32_t seq, uint64_t nowUs) {
    keyAuOnWireMediaEpoch.store(mediaEpoch, std::memory_order_relaxed);
    keyAuOnWireGeneration.store(generation, std::memory_order_relaxed);
    keyAuOnWireInputEpoch.store(inputEpoch, std::memory_order_relaxed);
    keyAuOnWireSeq.store(seq, std::memory_order_relaxed);
    keyAuOnWireSinceUs.store(nowUs, std::memory_order_release);
    keyAuOnWire.store(true, std::memory_order_release);
    // The key left the queue for the wire: end the queued state ONLY if it is this same key.
    if (keyAuQueued.load(std::memory_order_acquire) && keyAuQueuedSeq.load(std::memory_order_relaxed) == seq &&
        keyAuQueuedGeneration.load(std::memory_order_relaxed) == generation)
      keyAuQueued.store(false, std::memory_order_release);
  }
  void ClearKeyQueuedIfSeq(uint64_t generation, uint32_t seq) {
    if (keyAuQueued.load(std::memory_order_acquire) && keyAuQueuedSeq.load(std::memory_order_relaxed) == seq &&
        keyAuQueuedGeneration.load(std::memory_order_relaxed) == generation)
      keyAuQueued.store(false, std::memory_order_release);
  }
  void ClearKeyOnWireIfSeq(uint64_t generation, uint32_t seq) {
    if (keyAuOnWire.load(std::memory_order_acquire) && keyAuOnWireSeq.load(std::memory_order_relaxed) == seq &&
        keyAuOnWireGeneration.load(std::memory_order_relaxed) == generation)
      keyAuOnWire.store(false, std::memory_order_release);
  }
  // IDR telemetry per media epoch (sender thread writes; reset by the rollover). Diagnostic only.
  std::atomic<uint64_t> firstKeyWireUs{0};
  std::atomic<uint64_t> lastKeyAuBytes{0};
  std::atomic<uint64_t> lastKeyAuChunks{0};
  std::thread thread;
  // Where the sender posts RequestKeyframe. Bound by StartThread; null when no sender runs
  // (TCP / raw), where nothing on this path executes anyway.
  MainLoopMailbox* mailbox = nullptr;
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
  void StartThread(VideoTransport transport, bool useH264, const Args& args,
                   SessionState& clientSession, MainLoopMailbox& mailbox);
  // Consume a reader-thread peer change: swap the peer and roll the media session over in one
  // transaction (drop backlog, hold deltas until an IDR, bump the epoch, force a keyframe).
  void PumpUdpHello(VideoTransport transport, EncoderState& encoder);
};

}  // namespace remote60::native_poc
