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
#include "host_encoder_manager.hpp"
#include "host_net_io.hpp"
#include "host_session.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

void SenderState::StartThread(VideoTransport transport, bool useH264, const Args& args,
                              SessionState& clientSession) {
  SenderState& sender = *this;
  if (transport != VideoTransport::Udp || !useH264) return;
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
      const uint64_t sendStartUs = qpc_now_us();
      item.udpHdr.sendQpcUs = sendStartUs;
      SendPathStats pathStats{};
      const UdpSendOutcome outcome =
          send_udp_chunks_timed(clientSession.clientSock, peer, item.bytes.data(), item.bytes.size(),
                                item.udpHdr, args.udpMtu, &pathStats, &sender.mediaSessionEpoch,
                                item.mediaEpoch, egress);
      const uint64_t sendDoneUs = qpc_now_us();
      if (outcome == UdpSendOutcome::Sent) {
        const uint64_t durUs = (sendDoneUs >= sendStartUs) ? (sendDoneUs - sendStartUs) : 0;
        sender.lastSendStartUs.store(sendStartUs, std::memory_order_relaxed);
        sender.txFrames.fetch_add(1, std::memory_order_relaxed);
        sender.txChunks.fetch_add(pathStats.payloadChunkCount, std::memory_order_relaxed);
        sender.txBytes.fetch_add(item.bytes.size(), std::memory_order_relaxed);
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
        // for a fresh IDR via sender.recoveryPending: sender.requestKey alone is only consumed after
        // a real frame is popped, so on a static desktop the recovery IDR would never be produced.
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
          sender.requestKey.store(true, std::memory_order_release);
          sender.recoveryPending.store(true, std::memory_order_release);
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
  encoder.forceKeyNext = true;
  sender.firstKeyEnqueuedUs = 0;  // re-anchor the per-epoch IDR telemetry on the new session
  std::cout << "[native-video-host] udp peer updated; media barrier armed epoch="
            << sender.mediaSessionEpoch.load(std::memory_order_acquire) << " forcing keyframe\n";
}

}  // namespace remote60::native_poc
