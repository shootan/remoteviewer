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
#include <chrono>
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
  burstLedger.SetRate(enabled ? capBps : 0ULL);  // r4 B1: same rate; 0 (cap off) makes the ledger inert
  wireCapBps.store(enabled ? capBps : 0ULL, std::memory_order_relaxed);
}

void SenderState::UpdateWireCap(uint64_t capBps) {
  if (!wireLimiter) return;
  const uint32_t lmax = clamp_udp_mtu(wireCapMtu) + 28u;
  wireLimiter->SetRate(wireCapEnabled ? capBps : 0ULL, lmax);
  burstLedger.SetRate(wireCapEnabled ? capBps : 0ULL);  // r4 B1
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
                          uint32_t mtu, bool tightSingleChunk, uint64_t mediaEpoch, uint64_t inputEpoch,
                          const sockaddr_in& peer, const uint8_t* payload, size_t payloadSize) {
  if (!nackEnabled.load(std::memory_order_relaxed) || !payload || payloadSize == 0) return;
  std::lock_guard<std::mutex> lk(nackCacheMu);
  nackCache.emplace_back();
  CachedAu& e = nackCache.back();
  e.generation = generation;
  e.seq = seq;
  e.mtu = mtu;
  e.tightSingleChunk = tightSingleChunk;
  e.mediaEpoch = mediaEpoch;           // r4 R2: session ownership
  e.inputEpoch = inputEpoch;           // r5 G2: input-epoch fence at replay send time
  e.peerIpNet = peer.sin_addr.s_addr;
  e.peerPortNet = peer.sin_port;
  e.startedOnWire = false;             // r4 R2: not replayable until the original sends a datagram
  e.baseHeader = baseHeader;
  e.payload = std::make_shared<std::vector<uint8_t>>(payload, payload + payloadSize);
  while (nackCache.size() > kNackCacheMaxAus) nackCache.pop_front();
}

void SenderState::MarkAuStartedOnWire(uint64_t generation, uint32_t seq) {
  std::lock_guard<std::mutex> lk(nackCacheMu);
  for (auto it = nackCache.rbegin(); it != nackCache.rend(); ++it) {
    if (it->generation == generation && it->seq == seq) {
      it->startedOnWire = true;
      return;
    }
  }
}

void SenderState::ClearReplayStateForRollover() {
  // A rollover ends the old session: its cached AUs and pending NACKs must never reach the new peer.
  {
    std::lock_guard<std::mutex> lk(pendingReplayMu);
    pendingReplays.clear();
    pendingReplayCount.store(0, std::memory_order_relaxed);
  }
  std::lock_guard<std::mutex> clk(nackCacheMu);
  nackCache.clear();
}

void SenderState::RetransmitAu(SOCKET sock, const sockaddr_in& peer, uint64_t generation,
                               uint32_t seq, const uint16_t* missing, uint16_t count) {
  // r3 F1 / r4 R2: the reader only RECORDS the request (tagged with its peer + the current media
  // epoch); the sender thread replays it (DrainPendingReplays) through the same wire budget, retrying
  // across loops until the chunks are actually sent, the deadline passes, or the session ends. A
  // single reader-side TryAcquire (r2) could suppress the whole replay under a full bucket and lose it.
  (void)sock;  // the sender replays to the recorded peer on its own thread
  if (!nackEnabled.load(std::memory_order_relaxed) || !missing || count == 0) return;
  nackRequests.fetch_add(1, std::memory_order_relaxed);
  RecordReplayRequest(generation, seq, peer, mediaSessionEpoch.load(std::memory_order_acquire), missing, count);
  // r5 G3: raise the wake flag UNDER sender.mu (the cv's mutex) before notifying, so a request that
  // lands between the sender's predicate check and its sleep is not lost. The atomic count alone (on a
  // different mutex) did not close that check->sleep window.
  {
    std::lock_guard<std::mutex> lk(mu);
    newReplayWork = true;
  }
  cv.notify_one();
}

void SenderState::RecordReplayRequest(uint64_t generation, uint32_t seq, const sockaddr_in& peer,
                                      uint64_t mediaEpoch, const uint16_t* missing, uint16_t count) {
  if (!nackEnabled.load(std::memory_order_relaxed) || !missing || count == 0) return;
  std::lock_guard<std::mutex> lk(pendingReplayMu);
  auto it = std::find_if(pendingReplays.begin(), pendingReplays.end(),
                         [&](const PendingReplay& p) { return p.generation == generation && p.seq == seq; });
  if (it == pendingReplays.end()) {
    if (pendingReplays.size() >= kMaxPendingReplays) pendingReplays.pop_front();
    PendingReplay p;
    p.generation = generation;
    p.seq = seq;
    p.createdUs = static_cast<uint64_t>(qpc_now_us());
    p.mediaEpoch = mediaEpoch;
    p.peerIpNet = peer.sin_addr.s_addr;
    p.peerPortNet = peer.sin_port;
    pendingReplays.push_back(std::move(p));
    it = std::prev(pendingReplays.end());
  }
  for (uint16_t i = 0; i < count && it->missing.size() < kUdpVideoNackMaxMissing; ++i) {
    const uint16_t idx = missing[i];
    if (std::find(it->missing.begin(), it->missing.end(), idx) == it->missing.end()) it->missing.push_back(idx);
  }
  pendingReplayCount.store(pendingReplays.size(), std::memory_order_relaxed);
}

bool SenderState::DrainPendingReplays(SOCKET sock, const sockaddr_in& peer, uint64_t nowUs,
                                      uint64_t currentEpoch, size_t maxChunks) {
  // r5 G2/G3: one chunk per iteration is PICKED under pendingReplayMu (with a cheap shared_ptr payload
  // snapshot), the lock is RELEASED, then the chunk is sent with a live media+input fence checked at
  // the actual send and a blocking token wait -- so (a) the reader is never blocked behind a replay's
  // token wait, and (b) a rollover/flush that lands during the wait fences the send. The served chunk
  // is popped under the lock again only if its request still owns this session.
  const bool capEnforcing = wireLimiter && wireLimiter->enabled();
  size_t servedThisCall = 0;
  for (;;) {
    if (maxChunks != 0 && servedThisCall >= maxChunks) break;  // r4 R1 interleave bound
    uint64_t gen = 0, itemInputEpoch = 0;
    uint32_t seq = 0;
    uint16_t idx = 0;
    std::shared_ptr<std::vector<uint8_t>> payload;
    UdpVideoChunkHeader baseHeader{};
    uint32_t mtu = 0;
    bool tight = true;
    bool have = false;
    {
      std::lock_guard<std::mutex> lk(pendingReplayMu);
      // Retire finished-session and deadline-expired requests first (r4 R2 / r3 deadline).
      for (auto it = pendingReplays.begin(); it != pendingReplays.end();) {
        if (it->mediaEpoch != currentEpoch) {
          it = pendingReplays.erase(it);
        } else if (it->missing.empty()) {
          it = pendingReplays.erase(it);
        } else if (nowUs >= it->createdUs && nowUs - it->createdUs > kReplayDeadlineUs) {
          replayDroppedRequests.fetch_add(1, std::memory_order_relaxed);
          it = pendingReplays.erase(it);
        } else {
          ++it;
        }
      }
      // Pick the first request whose AU is cached, this-session, and started on the wire (r4 R2).
      for (auto& req : pendingReplays) {
        std::lock_guard<std::mutex> clk(nackCacheMu);
        auto c = std::find_if(nackCache.rbegin(), nackCache.rend(), [&](const CachedAu& e) {
          return e.generation == req.generation && e.seq == req.seq && e.mediaEpoch == currentEpoch &&
                 e.peerIpNet == req.peerIpNet && e.peerPortNet == req.peerPortNet && e.startedOnWire;
        });
        if (c == nackCache.rend()) continue;  // uncached/unstarted -> keep, try a later request
        gen = req.generation;
        seq = req.seq;
        idx = req.missing.front();
        payload = c->payload;  // shared_ptr: no AU copy
        baseHeader = c->baseHeader;
        mtu = c->mtu;
        tight = c->tightSingleChunk;
        itemInputEpoch = c->inputEpoch;
        have = true;
        break;
      }
      pendingReplayCount.store(pendingReplays.size(), std::memory_order_relaxed);
    }
    if (!have || !payload) break;  // nothing sendable now (all uncached, or empty)

    // Cap-off keeps the non-blocking fallback budget; if it is exhausted, stop this pass.
    if (!capEnforcing && !NackFallbackTryAcquire((mtu ? mtu : 1400) + 28u)) break;

    // r6 H2: the live media+input fence is wired REGARDLESS of the cap (a kill-switch-off stream must
    // still not replay across a rollover/flush). Only the token mechanism is cap-gated.
    WireEgress wire;
    wire.mediaEpoch = &mediaSessionEpoch;  // live media fence AT the send (fast + slow path)
    wire.itemMediaEpoch = currentEpoch;
    wire.inputEpoch = inputEpochRef;       // live input fence -- a flushed AU's replay stops
    wire.itemInputEpoch = itemInputEpoch;
    if (capEnforcing) {
      wire.limiter = wireLimiter.get();
      // Interleave (maxChunks != 0, during a saturated original send) blocks for a fair share of the
      // bucket -- the lock is released here, so the reader is NOT blocked behind that wait (r5 G3).
      // Idle/between-AU (maxChunks == 0) stays non-blocking: tokens are free when no original drains
      // them, and a token-blocked idle replay is retried by the sender's 50 ms heartbeat, not a spin.
      wire.blockingAcquire = (maxChunks != 0);
    }
    uint64_t wb = 0, dg = 0, sup = 0;
    bool fenced = false;
    send_udp_chunk_indices(sock, peer, payload->data(), payload->size(), baseHeader, mtu, tight, &idx, 1, &wb, &dg,
                           &wire, &sup, &fenced);

    // Settle. r6 H2: distinguish a PERMANENT invalidation (fenced: rollover/flush) from a temporary
    // budget shortage. A fenced request is retired at once so it cannot occupy the head and starve a
    // later valid request for up to its 1 s deadline; a budget shortage is kept and we stop this pass.
    bool retireFenced = false;
    {
      std::lock_guard<std::mutex> lk(pendingReplayMu);
      auto it = std::find_if(pendingReplays.begin(), pendingReplays.end(), [&](const PendingReplay& p) {
        return p.generation == gen && p.seq == seq && p.mediaEpoch == currentEpoch;
      });
      if (dg > 0 && it != pendingReplays.end() && !it->missing.empty() && it->missing.front() == idx) {
        it->missing.erase(it->missing.begin());
        if (it->missing.empty()) pendingReplays.erase(it);
      } else if (fenced && it != pendingReplays.end()) {
        pendingReplays.erase(it);  // flushed/rolled -> the client's IDR fallback recovers it
        retireFenced = true;
      }
      pendingReplayCount.store(pendingReplays.size(), std::memory_order_relaxed);
    }
    if (dg == 0) {
      if (retireFenced) continue;  // served nothing, but the head is retired -> try the next request
      break;                       // temporary budget shortage -> stop this pass, keep the request
    }
    ++servedThisCall;
    replayServedChunks.fetch_add(1, std::memory_order_relaxed);
    txNackBytes.fetch_add(wb, std::memory_order_relaxed);
    txNackDatagrams.fetch_add(dg, std::memory_order_relaxed);
    nackRetransmitChunks.fetch_add(dg, std::memory_order_relaxed);
  }
  return pendingReplayCount.load(std::memory_order_relaxed) != 0;
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
        // Wake on stop, a queued AU, or NEW replay work (r5 G3). The wake SIGNAL is newReplayWork, set
        // by RecordReplayRequest under THIS mutex and notified, so a request that lands between the
        // predicate check and the sleep is not lost (the r4 atomic-count-on-a-different-mutex could
        // miss it). "A pending replay exists" is deliberately NOT a ready() condition -- otherwise a
        // token-blocked or uncached request would make wait_for return at once and busy-spin. Instead,
        // when replays are still pending we wait with a bounded heartbeat (for deadline/uncached
        // cleanup); the token wait itself happens inside Drain's blocking Acquire, not here.
        const auto ready = [&] {
          return sender.stop.load(std::memory_order_acquire) || !sender.queue.empty() || sender.newReplayWork;
        };
        const bool pendingHint = sender.pendingReplayCount.load(std::memory_order_relaxed) != 0;
        if (pendingHint) {
          sender.cv.wait_for(lk, std::chrono::milliseconds(50), ready);
        } else {
          sender.cv.wait(lk, ready);
        }
        sender.newReplayWork = false;  // consumed
        peer = sender.peer;
        peerReady = sender.peerReady;
        if (sender.queue.empty()) {
          if (sender.stop.load(std::memory_order_acquire)) return;
          // No AU to send. Drain pending replays (cap-paced; the blocking token wait is inside Drain,
          // which holds no lock while waiting, so the reader is not blocked). This serves a NACK that
          // arrived while idle and retries one the bucket suppressed earlier once tokens refill.
          const bool canDrain = peerReady && sender.pendingReplayCount.load(std::memory_order_relaxed) != 0;
          lk.unlock();
          if (canDrain) {
            sender.DrainPendingReplays(clientSession.clientSock, peer, qpc_now_us(),
                                       sender.mediaSessionEpoch.load(std::memory_order_acquire));
          }
          continue;
        }
        item = std::move(sender.queue.front());
        sender.queue.pop_front();
        queueDepthAtDequeue = sender.queue.size();
      }
      // One consistent set of egress parameters for this frame's pacing and chunking, instead of
      // three process globals re-read at different points inside the send. (Ledger H-22.)
      const UdpEgressConfig egress = sender.EgressSnapshot();
      // Session media barrier: an item stamped for a previous session -- queued before the
      // rollover, or popped in the instant before the swap -- must never reach the new peer.
      // Drop it here so a stale P-frame cannot land on the new decoder.
      if (item.mediaEpoch != sender.mediaSessionEpoch.load(std::memory_order_acquire)) {
        sender.dropCount.fetch_add(1, std::memory_order_relaxed);
        // r5 G1: a discarded key ends its queued state (its own entry only), so the new session's IDR
        // is not suppressed by this dropped one.
        if (item.keyFrame) sender.ClearKeyQueuedIfSeq(item.udpHdr.streamGeneration, item.udpHdr.seq);
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
        // r5 G1: dropping a key at the input fence is that key's completion -- end its queued state so a
        // new key the fresh input epoch needs is not suppressed as "already in flight" (its own entry
        // only, by seq/generation, never a different key's).
        if (it.keyFrame) sender.ClearKeyQueuedIfSeq(it.udpHdr.streamGeneration, it.udpHdr.seq);
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
      // F3 (r3): the first datagram's input-epoch fence is re-checked after its token wait.
      wireEgress.inputEpoch = sender.inputEpochRef;
      wireEgress.itemInputEpoch = item.inputEpoch;
      // F4 (r3): actual bytes/datagrams that leave, accumulated even if the AU aborts partway.
      uint64_t wireData = 0, wireParity = 0, wireDg = 0;
      wireEgress.outWireDataBytes = &wireData;
      wireEgress.outWireParityBytes = &wireParity;
      wireEgress.outWireDatagrams = &wireDg;
      // r4 R1: interleave NACK replay WHILE this (possibly ~1.4 s) AU goes out, from the same bucket,
      // bounded to a few chunks per original datagram so original data keeps priority. On the first
      // datagram this also lifts the first-packet fence for THIS AU (r4 R2) so an in-flight NACK for a
      // chunk already on the wire becomes replayable; an AU the F3 fence aborts at 0 datagrams never
      // runs this and stays unreplayable.
      bool auStartedMark = false;
      wireEgress.betweenDatagrams = [&]() {
        if (!auStartedMark) {
          sender.MarkAuStartedOnWire(item.udpHdr.streamGeneration, item.udpHdr.seq);
          auStartedMark = true;
        }
        sender.DrainPendingReplays(clientSession.clientSock, peer, qpc_now_us(),
                                   sender.mediaSessionEpoch.load(std::memory_order_acquire), /*maxChunks=*/2);
      };
      // A key AU is on the wire for the whole of this (possibly long, cap-paced) send: the encode
      // gate uses this to not force a duplicate recovery IDR while this one is still going out. (r2)
      if (item.keyFrame) {
        // Tag the on-wire key by its full identity (media epoch, generation, input epoch, seq) so the
        // gate's "a key is already in flight" is scoped to THIS key (r3 F2 + r5 G1), and hand the
        // queued state over to on-wire ONLY for this same key (so a different key enqueued meanwhile
        // keeps its flag).
        sender.MarkKeyOnWire(item.mediaEpoch, item.udpHdr.streamGeneration, item.inputEpoch,
                             item.udpHdr.seq, sendStartUs);
      }
      // Cache this AU BEFORE it is sent (bitrate-hard-cap r2): a cap-paced large AU takes up to ~1.4 s
      // to go out, and a NACK that arrives during that window must find it in the cache and be replayed
      // without waiting for the whole send, which can be later than the receiver's stuck-head give-up
      // (its last data chunk, not the parity tail, is what keeps it alive). The replay runs on THIS
      // sender thread, interleaved between the original's datagrams (betweenDatagrams above), bounded by
      // the shared wire budget (the receiver dedupes any overlap). It is tagged with this AU's session
      // (media epoch, peer) and is not replayable until its first datagram is actually on the wire
      // (r4 R2). No-op unless the client negotiated NACK. (was: cached after a successful send.)
      sender.StoreAu(item.udpHdr.streamGeneration, item.udpHdr.seq, item.udpHdr, args.udpMtu,
                     egress.fecSingleChunkTightStride, item.mediaEpoch, item.inputEpoch, peer,
                     item.bytes.data(), item.bytes.size());
      // Before this send, drain any replay for an EARLIER (already-started) AU that a full bucket
      // suppressed -- unlimited here (between-AU), the interleave hook bounds the during-send work.
      sender.DrainPendingReplays(clientSession.clientSock, peer, qpc_now_us(),
                                 sender.mediaSessionEpoch.load(std::memory_order_acquire));
      const UdpSendOutcome outcome =
          send_udp_chunks_timed(clientSession.clientSock, peer, item.bytes.data(), item.bytes.size(),
                                item.udpHdr, args.udpMtu, &pathStats, &sender.mediaSessionEpoch,
                                item.mediaEpoch, egress, &wireEgress);
      // Clear the on-wire flag only if it is still THIS key (a rollover may have handed it elsewhere).
      if (item.keyFrame) sender.ClearKeyOnWireIfSeq(item.udpHdr.streamGeneration, item.udpHdr.seq);
      // F4 (r3): count what ACTUALLY left, regardless of the outcome (a partial/aborted AU still put
      // these bytes on the wire). Distinct from the payload totals below, which are per whole-AU Sent.
      sender.txActualWireBytes.fetch_add(wireData + wireParity, std::memory_order_relaxed);
      sender.txActualWireDatagrams.fetch_add(wireDg, std::memory_order_relaxed);
      const uint64_t sendDoneUs = qpc_now_us();
      if (outcome == UdpSendOutcome::Sent) {
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
            // r5 G1: the queue (and any key waiting in it) is discarded on this barrier re-arm, so end
            // the queued-key state -- the re-armed IDR requested below must not be suppressed.
            sender.ClearKeyQueuedAll();
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
  // r4 R2: the old session's cached AUs and pending NACKs must never reach the new peer. Drop them on
  // the rollover (the per-replay epoch/peer fence is a second line; this frees the memory at once).
  sender.ClearReplayStateForRollover();
  // The new session's key is a fresh recovery, not a duplicate of the old epoch's; clear the guards.
  sender.ClearKeyStateAll();
  encoder.RequestKey(kHostKeyReasonPeer);
  sender.firstKeyEnqueuedUs = 0;  // re-anchor the per-epoch IDR telemetry on the new session
  std::cout << "[native-video-host] udp peer updated; media barrier armed epoch="
            << sender.mediaSessionEpoch.load(std::memory_order_acquire) << " forcing keyframe\n";
}

}  // namespace remote60::native_poc
