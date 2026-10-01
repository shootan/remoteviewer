// See host_encoded_sender.hpp for the module summary. The bodies below are the former
// start_encoded_sender / pump_udp_hello lambdas of native_video_host_main.cpp, moved verbatim
// (host split refactor Phase 2-3). "sender" aliases *this so the moved text reads unchanged.

#include <winsock2.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#include "host_args.hpp"
#include "host_encoded_sender.hpp"
#include "host_main_loop_mailbox.hpp"
#include "host_encoder_manager.hpp"
#include "host_net_io.hpp"
#include "host_session.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

void SenderState::StartWireCap(uint64_t capBps, uint32_t mtu, bool enabled) {
  wireCapEnabled = enabled;
  wireCapMtu = mtu;
  if (!wireLimiter) {
    wireLimiter = std::make_unique<WireLimiter>(
        [] { return qpc_now_us(); },
        // A cancellable sleep: wait in 1 ms slices so a Cancel (epoch roll / peer change) or Stop
        // (shutdown) breaks the wait within a slice; udp_pace_wait_until gives the sub-ms precision
        // the per-datagram spacing needs (one ~1200 B datagram is ~2 ms apart at 6 Mbps).
        [this](uint64_t deadlineUs, uint64_t seq) -> bool {
          for (;;) {
            const uint64_t now = qpc_now_us();
            if (now >= deadlineUs) return true;
            if (wireLimiter->cancel_seq() != seq || wireLimiter->stopped()) return false;
            udp_pace_wait_until(std::min<uint64_t>(deadlineUs, now + 1000ULL));
          }
        });
  }
  const uint32_t lmax = clamp_udp_mtu(mtu) + 28u;
  wireLimiter->SetRate(enabled ? capBps : 0ULL, lmax);
  wireCapBps.store(enabled ? capBps : 0ULL, std::memory_order_relaxed);
}

void SenderState::UpdateWireCap(uint64_t capBps) {
  if (!wireLimiter) return;
  const uint32_t lmax = clamp_udp_mtu(wireCapMtu) + 28u;
  wireLimiter->SetRate(wireCapEnabled ? capBps : 0ULL, lmax);
  wireCapBps.store(wireCapEnabled ? capBps : 0ULL, std::memory_order_relaxed);
}

bool SenderState::NackFallbackTryAcquire(uint64_t bytes) {
  const uint64_t nowUs = static_cast<uint64_t>(qpc_now_us());
  const uint32_t peakBps = pacePeakBps.load(std::memory_order_relaxed);  // bits/s; 0 = unpaced
  const uint64_t rateBytesPerSec =
      ((peakBps > 0 ? static_cast<uint64_t>(peakBps) : 48000000ULL) / 8ULL) * 15ULL / 100ULL;
  const uint64_t capBytes = rateBytesPerSec / 2ULL + 65536ULL;  // ~0.5s burst + one frame's slack
  std::lock_guard<std::mutex> lk(nackBudgetMu);
  if (nackBudgetLastUs == 0) {
    nackBudgetTokensBytes = capBytes;  // prime the bucket on first use
  } else if (nowUs > nackBudgetLastUs) {
    nackBudgetTokensBytes += (nowUs - nackBudgetLastUs) * rateBytesPerSec / 1000000ULL;
    if (nackBudgetTokensBytes > capBytes) nackBudgetTokensBytes = capBytes;
  }
  nackBudgetLastUs = nowUs;
  if (nackBudgetTokensBytes < bytes) return false;
  nackBudgetTokensBytes -= bytes;
  return true;
}

void SenderState::StoreAu(uint64_t generation, uint32_t seq, const UdpVideoChunkHeader& baseHeader,
                          uint32_t mtu, bool tightSingleChunk, const uint8_t* payload,
                          size_t payloadSize) {
  if (!nackEnabled.load(std::memory_order_relaxed) || !payload || payloadSize == 0) return;
  std::lock_guard<std::mutex> lk(nackCacheMu);
  nackCache.emplace_back();
  CachedAu& e = nackCache.back();
  e.generation = generation;
  e.seq = seq;
  e.mtu = mtu;
  e.tightSingleChunk = tightSingleChunk;
  e.baseHeader = baseHeader;
  e.payload.assign(payload, payload + payloadSize);
  while (nackCache.size() > kNackCacheMaxAus) nackCache.pop_front();
}

void SenderState::RetransmitAu(SOCKET sock, const sockaddr_in& peer, uint64_t generation,
                               uint32_t seq, const uint16_t* missing, uint16_t count) {
  if (!nackEnabled.load(std::memory_order_relaxed) || !missing || count == 0) return;
  nackRequests.fetch_add(1, std::memory_order_relaxed);
  // Copy out what we need under the cache lock, then send outside it (sendto can block briefly).
  std::vector<uint8_t> payload;
  UdpVideoChunkHeader baseHeader{};
  uint32_t mtu = 0;
  bool tightSingleChunk = true;
  {
    std::lock_guard<std::mutex> lk(nackCacheMu);
    auto it = std::find_if(nackCache.rbegin(), nackCache.rend(), [&](const CachedAu& e) {
      return e.generation == generation && e.seq == seq;
    });
    if (it == nackCache.rend()) {
      nackMisses.fetch_add(1, std::memory_order_relaxed);
      // The AU may be too old (rolled out of the cache) OR still being sent (not cached yet). Defer
      // the request: if this (gen,seq) is cached soon (StoreAu), ServeDeferredNacks replays it once;
      // if it never caches, the bounded buffer drops it and the client's IDR fallback recovers. (r2)
      DeferNack(generation, seq, missing, count);
      return;
    }
    payload = it->payload;
    baseHeader = it->baseHeader;
    mtu = it->mtu;
    tightSingleChunk = it->tightSingleChunk;
  }
  // Retransmit byte budget. With the cap ON (bitrate-hard-cap r1): the replay spends the SAME wire
  // bucket as the live send, non-blockingly (TryAcquire inside send_udp_chunk_indices) -- a chunk that
  // does not fit is suppressed and the client's IDR fallback recovers it, so a NACK storm cannot push
  // the stream over the cap and there is no separate retransmit lane around it. With the cap OFF (r2):
  // the shared bucket is inactive, so the old flood defence gates the whole replay here -- ~15% of the
  // live rate, ~0.5 s burst -- exactly as before the cap existed. The reader thread never blocks.
  const bool capEnforcing = wireLimiter && wireLimiter->enabled();
  if (!capEnforcing) {
    const uint64_t estBytes = static_cast<uint64_t>(count) * static_cast<uint64_t>(mtu ? mtu : 1400);
    if (!NackFallbackTryAcquire(estBytes)) {
      nackSuppressed.fetch_add(1, std::memory_order_relaxed);
      return;  // over the cap-off fallback budget -> client's IDR fallback handles it
    }
  }
  WireEgress wire = MakeWireEgress();
  uint64_t replayBytes = 0;
  uint64_t replayDatagrams = 0;
  uint64_t replaySuppressed = 0;
  (void)send_udp_chunk_indices(sock, peer, payload.data(), payload.size(), baseHeader, mtu,
                               tightSingleChunk, missing, count, &replayBytes, &replayDatagrams,
                               &wire, &replaySuppressed);
  txNackBytes.fetch_add(replayBytes, std::memory_order_relaxed);
  txNackDatagrams.fetch_add(replayDatagrams, std::memory_order_relaxed);
  nackRetransmitChunks.fetch_add(replayDatagrams, std::memory_order_relaxed);
  if (replaySuppressed > 0) nackSuppressed.fetch_add(1, std::memory_order_relaxed);
}

void SenderState::DeferNack(uint64_t generation, uint32_t seq, const uint16_t* missing, uint16_t count) {
  if (!nackEnabled.load(std::memory_order_relaxed) || !missing || count == 0) return;
  std::lock_guard<std::mutex> lk(pendingNackMu);
  // Merge into an existing entry for this (gen,seq), else add one (dropping the oldest past the bound).
  auto it = std::find_if(pendingNacks.begin(), pendingNacks.end(),
                         [&](const PendingNack& p) { return p.generation == generation && p.seq == seq; });
  if (it == pendingNacks.end()) {
    if (pendingNacks.size() >= kMaxPendingNacks) pendingNacks.pop_front();
    pendingNacks.push_back(PendingNack{generation, seq, {}});
    it = std::prev(pendingNacks.end());
  }
  for (uint16_t i = 0; i < count && it->missing.size() < kUdpVideoNackMaxMissing; ++i) {
    const uint16_t idx = missing[i];
    if (std::find(it->missing.begin(), it->missing.end(), idx) == it->missing.end()) it->missing.push_back(idx);
  }
}

void SenderState::ServeDeferredNacks(SOCKET sock, const sockaddr_in& peer, uint64_t generation, uint32_t seq) {
  std::vector<uint16_t> missing;
  {
    std::lock_guard<std::mutex> lk(pendingNackMu);
    auto it = std::find_if(pendingNacks.begin(), pendingNacks.end(),
                           [&](const PendingNack& p) { return p.generation == generation && p.seq == seq; });
    if (it == pendingNacks.end()) return;
    missing = std::move(it->missing);
    pendingNacks.erase(it);
  }
  if (missing.empty()) return;
  std::sort(missing.begin(), missing.end());
  // Now the AU is in the cache (StoreAu just ran): RetransmitAu finds it and replays through the same
  // wire budget. One post-send recovery for a hole that was requested while the AU was still sending.
  RetransmitAu(sock, peer, generation, seq, missing.data(), static_cast<uint16_t>(missing.size()));
}

void SenderState::StartThread(VideoTransport transport, bool useH264, const Args& args,
                              SessionState& clientSession, MainLoopMailbox& mailbox) {
  SenderState& sender = *this;
  if (transport != VideoTransport::Udp || !useH264) return;
  sender.mailbox = &mailbox;
  sender.thread = std::thread([&]() {
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    uint64_t cadenceScheduledUs = 0;
    uint64_t cadenceGeneration = 0;
    uint64_t cadenceMediaEpoch = 0;
    // Same-epoch barrier-recovery rate tracking. First implementation logs only: a persistent
    // local send failure that keeps re-arming and re-requesting IDRs would otherwise spin. A
    // policy (drop peer / wait re-Hello) is deferred until real-use shows whether it recurs.
    uint64_t recoveryWindowStartUs = 0;
    uint32_t recoveryAttemptsInWindow = 0;
    // Actual-wire telemetry: interval between consecutive wire-starts on THIS (sender) thread, so
    // an uneven picture can be pinned to the wire vs the encode/main AU supply (whose enqueue
    // interval is the separate encode_au_enqueue jitter metric). 0 = no previous send yet.
    uint64_t prevWireStartUs = 0;
    while (true) {
      EncodedSendItem item;
      sockaddr_in peer{};
      bool peerReady = false;
      size_t queueDepthAtDequeue = 0;
      {
        std::unique_lock<std::mutex> lk(sender.mu);
        sender.cv.wait(lk, [&] { return sender.stop.load(std::memory_order_acquire) ||
                                       !sender.queue.empty(); });
        if (sender.queue.empty()) {
          if (sender.stop.load(std::memory_order_acquire)) return;
          continue;
        }
        item = std::move(sender.queue.front());
        sender.queue.pop_front();
        queueDepthAtDequeue = sender.queue.size();
        peer = sender.peer;
        peerReady = sender.peerReady;
      }
      // One consistent set of egress parameters for this frame's pacing and chunking, instead of
      // three process globals re-read at different points inside the send. (Ledger H-22.)
      const UdpEgressConfig egress = sender.EgressSnapshot();
      // Session media barrier: an item stamped for a previous session -- queued before the
      // rollover, or popped in the instant before the swap -- must never reach the new peer.
      // Drop it here so a stale P-frame cannot land on the new decoder.
      if (item.mediaEpoch != sender.mediaSessionEpoch.load(std::memory_order_acquire)) {
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      // Flush epoch fence (P11), early pass: an item of another epoch is dropped here without
      // costing a pacing slot. The decisive check is the permission point right before the first
      // datagram (below) -- a flush can land during pacing, and an AU whose first datagram was not
      // yet permitted then must not start. See SenderState::inputEpochRef for the exact contract.
      auto epoch_fence_drops = [&](const EncodedSendItem& it, const char* where) -> bool {
        if (!sender.inputEpochRef) return false;  // fence inactive: legacy pass-through
        const uint64_t cur = sender.inputEpochRef->load(std::memory_order_acquire);
        if (it.inputEpoch == cur) return false;
        const uint64_t n = sender.inputEpochDropCount.fetch_add(1, std::memory_order_relaxed) + 1;
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        if (n <= 5 || (n % 50) == 0) {
          std::cout << "[native-video-host] sender dropped AU of another epoch at " << where << " seq=" << it.udpHdr.seq
                    << " auEpoch=" << it.inputEpoch << " curEpoch=" << cur
                    << " key=" << (it.keyFrame ? 1 : 0) << " total=" << n << "\n";
        }
        return true;
      };
      if (epoch_fence_drops(item, "dequeue")) continue;
      if (!peerReady) {
        sender.txNoPeer.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      const uint64_t frameIntervalUs =
          std::clamp<uint64_t>(item.frameIntervalUs, 8333ULL, 200000ULL);
      const uint64_t nowUs = qpc_now_us();
      // Optional catch-up smoothing. The schedule is carried in its own variable and
      // advanced as max(now, scheduled + interval): deriving the next deadline from the
      // *actual* send time instead would fold each hold into the following frame's deadline
      // and ratchet the stream progressively further behind live.
      const bool freshCadence = cadenceScheduledUs == 0 ||
                                cadenceGeneration != item.udpHdr.streamGeneration ||
                                cadenceMediaEpoch != item.mediaEpoch ||
                                nowUs > cadenceScheduledUs + frameIntervalUs * 2ULL;
      cadenceGeneration = item.udpHdr.streamGeneration;
      // Re-anchor the pacing clock on a media rollover so a new session's first frame is not
      // held against a deadline inherited from the previous client.
      cadenceMediaEpoch = item.mediaEpoch;
      if (freshCadence) {
        cadenceScheduledUs = nowUs;
      } else if (sender.cadenceSmoothing) {
        const uint64_t earliestSendUs = cadenceScheduledUs + frameIntervalUs;
        if (nowUs < earliestSendUs) {
          udp_pace_wait_until(std::min<uint64_t>(earliestSendUs, nowUs + sender.maxCadenceHoldUs));
        }
        cadenceScheduledUs = std::max<uint64_t>(nowUs, earliestSendUs);
      } else {
        cadenceScheduledUs = nowUs;
      }
      if (sender.beforeFirstDatagramHook) sender.beforeFirstDatagramHook(item);
      // Permission point (P11): the epoch is read once more right before the first datagram. An
      // item that passed the dequeue check but whose epoch moved during pacing is dropped here.
      if (epoch_fence_drops(item, "first-datagram")) {
        cadenceScheduledUs = 0;
        continue;
      }
      const uint64_t sendStartUs = qpc_now_us();
      item.udpHdr.sendQpcUs = sendStartUs;
      SendPathStats pathStats{};
      // The hard wire cap, shared with the NACK path. Null limiter (cap off) leaves the send exactly
      // as it was. Acquire waits for tokens per datagram and aborts (-> EpochChanged) on stop/epoch.
      WireEgress wireEgress = sender.MakeWireEgress();
      // A key AU is on the wire for the whole of this (possibly long, cap-paced) send: the encode
      // gate uses this to not force a duplicate recovery IDR while this one is still going out. (r2)
      if (item.keyFrame) {
        sender.keyAuOnWireSinceUs.store(sendStartUs, std::memory_order_release);
        sender.keyAuOnWire.store(true, std::memory_order_release);
      }
      const UdpSendOutcome outcome =
          send_udp_chunks_timed(clientSession.clientSock, peer, item.bytes.data(), item.bytes.size(),
                                item.udpHdr, args.udpMtu, &pathStats, &sender.mediaSessionEpoch,
                                item.mediaEpoch, egress, &wireEgress);
      if (item.keyFrame) sender.keyAuOnWire.store(false, std::memory_order_release);
      const uint64_t sendDoneUs = qpc_now_us();
      if (outcome == UdpSendOutcome::Sent) {
        // Cache this AU so a client NACK can be answered with just the missing chunks (no-op unless
        // the client negotiated NACK). (video NACK.)
        sender.StoreAu(item.udpHdr.streamGeneration, item.udpHdr.seq, item.udpHdr, args.udpMtu,
                       egress.fecSingleChunkTightStride, item.bytes.data(), item.bytes.size());
        // Now cached: serve any NACK that missed while this AU was still being sent (r2), to the
        // current media peer. One bounded post-send recovery; the shared wire budget still bounds it.
        sender.ServeDeferredNacks(clientSession.clientSock, peer, item.udpHdr.streamGeneration, item.udpHdr.seq);
        const uint64_t durUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
        sender.lastSendStartUs.store(sendStartUs, std::memory_order_relaxed);
        sender.txFrames.fetch_add(1, std::memory_order_relaxed);
        sender.txChunks.fetch_add(pathStats.payloadChunkCount, std::memory_order_relaxed);
        sender.txBytes.fetch_add(item.bytes.size(), std::memory_order_relaxed);
        sender.txParityBytes.fetch_add(pathStats.parityBytes, std::memory_order_relaxed);
        sender.txChunkHeaderBytes.fetch_add(pathStats.headerBytes, std::memory_order_relaxed);
        sender.txVideoDatagrams.fetch_add(pathStats.datagrams, std::memory_order_relaxed);
        sender.sendDurSumUs.fetch_add(durUs, std::memory_order_relaxed);
        sender.sendCount.fetch_add(1, std::memory_order_relaxed);
        uint64_t prevMax = sender.sendDurMaxUs.load(std::memory_order_relaxed);
        while (durUs > prevMax &&
               !sender.sendDurMaxUs.compare_exchange_weak(prevMax, durUs,
                                                         std::memory_order_relaxed)) {
        }
        if (item.keyFrame) {
          // Record when the first key AU of this media epoch reached the wire and the size of
          // the last key sent -- distinguishes "key never produced" from "key lost in assembly".
          uint64_t expectedFirst = 0;
          sender.firstKeyWireUs.compare_exchange_strong(expectedFirst, sendStartUs,
                                                       std::memory_order_relaxed);
          sender.lastKeyAuBytes.store(item.bytes.size(), std::memory_order_relaxed);
          sender.lastKeyAuChunks.store(pathStats.payloadChunkCount, std::memory_order_relaxed);
        }
        // Actual-wire per-frame telemetry. A key frame always logs (end-to-end anchor); a normal
        // frame logs only when its wire-start interval ran >1.5x the target (an actual hitch), so
        // steady 60fps play stays quiet. queueWaitUs = "AU ready" -> "bytes on wire"; wireIntUs =
        // gap since the previous send's wire-start; join to the client by udpHdr.seq, not clocks.
        const uint64_t wireIntUs = prevWireStartUs > 0 && sendStartUs >= prevWireStartUs
                                       ? sendStartUs - prevWireStartUs
                                       : 0;
        const uint64_t queueWaitUs =
            item.enqueueUs > 0 && sendStartUs >= item.enqueueUs ? sendStartUs - item.enqueueUs : 0;
        prevWireStartUs = sendStartUs;
        if (item.keyFrame || (wireIntUs > (frameIntervalUs * 3ULL) / 2ULL)) {
          std::cout << "[native-video-host] wire seq=" << item.udpHdr.seq
                    << " key=" << (item.keyFrame ? 1 : 0) << " bytes=" << item.bytes.size()
                    << " chunks=" << pathStats.payloadChunkCount << " wireIntUs=" << wireIntUs
                    << " targetIntUs=" << frameIntervalUs << " queueWaitUs=" << queueWaitUs
                    << " sendDurUs=" << durUs << " queueDepth=" << queueDepthAtDequeue
                    << " epoch=" << item.mediaEpoch << "\n";
        }
      } else if (outcome == UdpSendOutcome::EpochChanged) {
        // A rollover bumped the media epoch mid-frame; the remaining chunks were aborted so old-
        // epoch data cannot interleave into the new session. The rollover already cleared the
        // queue and re-armed the barrier under sender.mu, so this is NOT a transport failure --
        // just account the aborted item and re-anchor pacing for the new epoch's first frame.
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        cadenceScheduledUs = 0;
        cadenceGeneration = 0;
        cadenceMediaEpoch = 0;
      } else {
        // Real transport error on the current epoch. Any H264 frame -- key OR delta -- that failed
        // to reach the wire breaks the client's reference chain (a delta references a picture the
        // client never fully received), and a barrier that was opened by this frame's key would
        // leave the decoder stuck. Clear the queue and re-arm the barrier, then ask the MAIN loop
        // for a fresh IDR by posting RequestKeyframe{SenderBarrier}. This used to need two flags
        // consumed at two different points -- requestKey was only read after a real frame was
        // popped, which never happens on a static desktop, so recoveryPending existed purely to
        // get the recovery IDR out. One typed request replaces both. (Phase 4.)
        sender.sendFailed.store(true, std::memory_order_release);
        bool rearmed = false;
        {
          std::lock_guard<std::mutex> lk(sender.mu);
          if (sender.mediaSessionEpoch.load(std::memory_order_acquire) == item.mediaEpoch) {
            // sender.dropCount is the authoritative drop tally; sender.heldFrames/sender.sentFrames are
            // owned by the encode thread and must not be touched here (that would be a data race).
            sender.dropCount.fetch_add(sender.queue.size() + 1, std::memory_order_relaxed);
            sender.queue.clear();
            sender.waitingForKey = true;
            rearmed = true;
          } else {
            // A rollover landed between the send and here; it already re-armed. Just drop.
            sender.dropCount.fetch_add(1, std::memory_order_relaxed);
          }
        }
        if (rearmed) {
          sender.firstKeyWireUs.store(0, std::memory_order_relaxed);  // retry epoch's first key reappears
          sender.barrierRearmCount.fetch_add(1, std::memory_order_relaxed);
          sender.mailbox->PostRequestKeyframe(kKeyframeReasonSenderBarrier);
          // Re-anchor pacing: a partial frame consumed part of the schedule and the epoch is
          // unchanged, so freshCadence would not otherwise trip for the recovery IDR.
          cadenceScheduledUs = 0;
          cadenceGeneration = 0;
          cadenceMediaEpoch = 0;
          const uint64_t failUs = qpc_now_us();
          if (recoveryWindowStartUs == 0 || failUs - recoveryWindowStartUs > 5'000'000ULL) {
            recoveryWindowStartUs = failUs;
            recoveryAttemptsInWindow = 0;
          }
          ++recoveryAttemptsInWindow;
          std::cout << "[native-video-host] send-failed barrier re-armed epoch=" << item.mediaEpoch
                    << " keyFrame=" << (item.keyFrame ? 1 : 0)
                    << " attemptsIn5s=" << recoveryAttemptsInWindow << "\n";
          if (recoveryAttemptsInWindow > 3) {
            std::cerr << "[native-video-host] WARN repeated same-epoch send failures ("
                      << recoveryAttemptsInWindow << " in <=5s) -- link may be down\n";
          }
        }
      }
    }
  });
}

void SenderState::PumpUdpHello(VideoTransport transport, EncoderState& encoder) {
  SenderState& sender = *this;
  if (transport != VideoTransport::Udp) return;
  if (!sender.udpPeerChanged.exchange(false, std::memory_order_acq_rel)) return;
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_addr.s_addr = sender.udpPeerIpNet.load(std::memory_order_acquire);
  peer.sin_port = sender.udpPeerPortNet.load(std::memory_order_acquire);
  sender.udpPeer = peer;
  sender.udpPeerReady = true;
  {
    // Session media barrier: the whole rollover is one transaction under the same lock the
    // sender thread dequeues on. Dropping the queue discards every delta still bound for the old
    // session; sender.waitingForKey holds new deltas until a real IDR; bumping the media epoch
    // fences even an item the sender has already popped for the old peer. Without this, a delta
    // queued before the swap goes out to the *new* peer as a P-frame its decoder can never use.
    std::lock_guard<std::mutex> lk(sender.mu);
    sender.dropCount.fetch_add(sender.queue.size(), std::memory_order_relaxed);
    sender.heldFrames += sender.queue.size();
    sender.sentFrames -= std::min<uint64_t>(sender.sentFrames, sender.queue.size());
    sender.queue.clear();
    sender.waitingForKey = true;
    sender.peer = peer;
    sender.peerReady = true;
    sender.mediaSessionEpoch.fetch_add(1, std::memory_order_acq_rel);
    sender.firstKeyWireUs.store(0, std::memory_order_relaxed);
    sender.lastKeyAuBytes.store(0, std::memory_order_relaxed);
    sender.lastKeyAuChunks.store(0, std::memory_order_relaxed);
  }
  // A sender asleep in the limiter for an old-epoch frame must wake to see the new epoch and abort;
  // Acquire re-checks mediaSessionEpoch after this wake. (bitrate-hard-cap r1)
  if (sender.wireLimiter) sender.wireLimiter->Cancel();
  // The new session's key is a fresh recovery, not a duplicate of the old epoch's; clear the guard.
  sender.keyAuOnWire.store(false, std::memory_order_release);
  encoder.RequestKey(kHostKeyReasonPeer);
  sender.firstKeyEnqueuedUs = 0;  // re-anchor the per-epoch IDR telemetry on the new session
  std::cout << "[native-video-host] udp peer updated; media barrier armed epoch="
            << sender.mediaSessionEpoch.load(std::memory_order_acquire) << " forcing keyframe\n";
}

}  // namespace remote60::native_poc
