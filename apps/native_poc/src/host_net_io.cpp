// See host_net_io.hpp for the module summary. Bodies below are moved verbatim from
// native_video_host_main.cpp (host split refactor Phase 0-5); no logic change.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "host_net_io.hpp"
#include "host_wire_limiter.hpp"
#include "host_burst_ledger.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

// The bytes the OS adds below the UDP payload on IPv4: 20 (IP) + 8 (UDP). The wire cap charges each
// datagram its payload length plus this, so the account is the IP-layer bytes of the video stream,
// not just its UDP payload. (bitrate-hard-cap r1, plan point 1.)
constexpr uint64_t kWireIpUdpHeaderBytes = 28;

ULONG resolve_bind_address(const std::string& bindAddress) {
  if (bindAddress.empty()) return htonl(INADDR_ANY);
  in_addr parsed{};
  if (inet_pton(AF_INET, bindAddress.c_str(), &parsed) == 1) return parsed.s_addr;
  std::cerr << "[native-video-host] invalid --bind-address " << bindAddress << ", using 0.0.0.0\n";
  return htonl(INADDR_ANY);
}

bool send_all_timed(SOCKET s, const void* data, size_t len, uint64_t* outUs,
                    uint64_t* outCallCount) {
  const char* p = reinterpret_cast<const char*>(data);
  size_t sent = 0;
  uint64_t calls = 0;
  const uint64_t startUs = qpc_now_us();
  while (sent < len) {
    const uint64_t callStartUs = qpc_now_us();
    const int n = send(s, p + sent, static_cast<int>(len - sent), 0);
    const uint64_t callDoneUs = qpc_now_us();
    if (n <= 0) return false;
    ++calls;
    sent += static_cast<size_t>(n);
  }
  const uint64_t doneUs = qpc_now_us();
  if (outUs) *outUs = (doneUs >= startUs) ? (doneUs - startUs) : 0;
  if (outCallCount) *outCallCount = calls;
  return true;
}

void udp_pace_wait_until(uint64_t targetUs) {
  // A yield loop burns most of one logical core because a 1200-byte datagram at the normal
  // pacing rate is only a few hundred microseconds apart. A reusable high-resolution
  // waitable timer keeps the sender asleep without falling back to the ~15.6ms legacy timer
  // quantum. Retain a short yield tail because setting a kernel timer for a few dozen
  // microseconds costs more than it saves.
  struct ThreadWaitTimer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* high resolution */,
                                            TIMER_MODIFY_STATE | SYNCHRONIZE);
    ~ThreadWaitTimer() {
      if (handle) CloseHandle(handle);
    }
  };
  thread_local ThreadWaitTimer timer;
  for (;;) {
    const uint64_t nowUs = qpc_now_us();
    if (nowUs >= targetUs) return;
    const uint64_t remainUs = targetUs - nowUs;
    if (timer.handle && remainUs > 100) {
      LARGE_INTEGER due{};
      due.QuadPart = -static_cast<LONGLONG>(std::max<uint64_t>(1, remainUs - 50) * 10ULL);
      if (SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)) {
        (void)WaitForSingleObject(timer.handle, INFINITE);
      } else {
        std::this_thread::yield();
      }
    } else {
      std::this_thread::yield();
    }
  }
}

// Returns the per-frame send budget in microseconds, or 0 when the frame should go out as
// fast as possible (small frames are not worth the pacing overhead).
uint64_t udp_pace_budget_us(const UdpEgressConfig& egress, size_t payloadSize,
                            uint32_t chunkCount, bool keyFrame) {
  uint32_t peakBps = egress.pacePeakBps;
  if (keyFrame && peakBps != 0) {
    // IDRs are much larger than delta frames. Pacing one at only a small multiple of the
    // average bitrate blocks the sender for several frame periods, fills the latest-wins
    // queue, and triggers another IDR -- a self-sustaining low-FPS loop. Keep pacing, but
    // give recovery frames enough wire rate to finish inside roughly one 60 Hz interval.
    peakBps = std::max(peakBps, egress.keyframePacePeakBps);
  }
  if (peakBps == 0 || chunkCount <= 8) return 0;
  return (static_cast<uint64_t>(payloadSize) * 8ULL * 1000000ULL) / static_cast<uint64_t>(peakBps);
}

UdpChunkGeometry udp_chunk_geometry(size_t payloadSize, uint32_t mtuBytes, bool tightSingleChunk) {
  UdpChunkGeometry g;
  if (payloadSize == 0 || payloadSize > std::numeric_limits<uint32_t>::max()) return g;
  const uint32_t safeMtu = clamp_udp_mtu(mtuBytes);
  if (safeMtu <= sizeof(UdpVideoChunkHeader)) return g;
  g.maxChunk = safeMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  // A frame that fits in one datagram is one chunk whichever stride is written in the header; the
  // stride only decides how long its parity datagram is (the receiver requires parity chunkSize ==
  // chunkStride). Writing the frame's own size makes the parity a replica of the frame and nothing
  // more. Anything larger keeps the MTU stride, so the datagrams of a multi-chunk frame are byte
  // for byte what they were before the tight stride existed.
  g.chunkStride = (tightSingleChunk && payloadSize <= g.maxChunk) ? static_cast<uint32_t>(payloadSize)
                                                                  : g.maxChunk;
  const uint64_t chunkCount =
      (static_cast<uint64_t>(payloadSize) + g.chunkStride - 1u) / g.chunkStride;
  if (chunkCount == 0 || chunkCount > std::numeric_limits<uint16_t>::max()) return g;
  g.chunkCount = static_cast<uint32_t>(chunkCount);
  g.fecGroupCount = (g.chunkCount + remote60::native_poc::kUdpVideoFecGroupSize - 1u) /
                    remote60::native_poc::kUdpVideoFecGroupSize;
  g.packetCount = g.chunkCount + g.fecGroupCount;
  g.parityBytes = static_cast<uint64_t>(g.fecGroupCount) * g.chunkStride;
  g.valid = true;
  return g;
}

// liveEpoch/itemEpoch let a rollover abort a chunked send mid-frame: if the live media epoch no
// longer matches the epoch this frame was stamped for, the remaining data/parity packets are the
// old session's and must not reach a freshly attached decoder. nullptr liveEpoch disables the check.
UdpSendOutcome send_udp_chunks_impl(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                    size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                    uint32_t mtuBytes, SendPathStats* stats,
                                    const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch,
                                    const UdpEgressConfig& egress, const WireEgress* wire) {
  // With a sink the socket is unused (a test observes datagrams instead); without one a real socket
  // is required.
  const bool haveSink = wire && wire->sink;
  if (!payload || payloadSize == 0 || (s == INVALID_SOCKET && !haveSink)) return UdpSendOutcome::TransportError;
  const uint64_t startUs = qpc_now_us();
  // One geometry for the data chunks, the parity, the pacing budget and (through the NACK cache,
  // which stores the same inputs) the replay of this frame.
  const UdpChunkGeometry geo =
      udp_chunk_geometry(payloadSize, mtuBytes, egress.fecSingleChunkTightStride);
  if (!geo.valid) return UdpSendOutcome::TransportError;
  const uint32_t stride = geo.chunkStride;
  const uint32_t chunkCount = geo.chunkCount;
  std::vector<uint8_t> datagram(sizeof(UdpVideoChunkHeader) + geo.maxChunk);
  const auto epoch_changed = [&]() {
    return liveEpoch && liveEpoch->load(std::memory_order_relaxed) != itemEpoch;
  };
  const uint32_t fecGroupCount = geo.fecGroupCount;
  const uint32_t packetCount = geo.packetCount;
  const uint64_t pacedPayloadBytes = static_cast<uint64_t>(payloadSize) + geo.parityBytes;
  const uint64_t budgetUs =
      udp_pace_budget_us(egress, static_cast<size_t>(pacedPayloadBytes), packetCount,
                         (baseHeader.flags & 0x1u) != 0);
  uint32_t packetOrdinal = 0;
  bool wireAborted = false;  // the limiter cancelled (stop / epoch) -- not a transport error

  auto send_packet = [&](const UdpVideoChunkHeader& header, const uint8_t* bytes,
                         uint32_t byteCount) -> bool {
    if (budgetUs > 0 && packetOrdinal > 0) {
      udp_pace_wait_until(startUs + (budgetUs * packetOrdinal) / packetCount);
    }
    ++packetOrdinal;
    std::memcpy(datagram.data(), &header, sizeof(header));
    std::memcpy(datagram.data() + sizeof(header), bytes, byteCount);
    const int datagramLen = static_cast<int>(sizeof(header) + byteCount);
    const bool parity = (header.flags & 0x10u) != 0;
    // The hard wire-rate cap: wait for tokens for this exact datagram (its length + the IP/UDP
    // header the OS adds) before it goes out. A video chunk is never dropped for want of tokens --
    // it waits -- so the reference chain is preserved; the wait aborts only on stop or an epoch roll.
    const bool isFirstDatagram = packetOrdinal == 1;  // (just incremented; the frame's first send)
    const uint64_t wireBytes = static_cast<uint64_t>(datagramLen) + kWireIpUdpHeaderBytes;
    // r4 B1 (corrected per Codex): in B1 mode (cap on) EVERY datagram passes the 2s rolling-window
    // admission -- Reserve its wire cost, waiting (cancellably) for room if the window is full --
    // before it is paced and sent. A grant lets the owning IDR's covered datagrams peak-pace instead
    // of the strict token rate, but it does NOT bypass the window. The reservation is CommitSent on a
    // real send and CancelUnsent on any abort, so the window reflects exactly what left the wire.
    const bool b1Active = wire && wire->burstLedger && wire->limiter && wire->limiter->enabled();
    bool reserved = false;
    // r4 R6 S1 (Codex 72a22d2): for an ORIGINAL AU, media rollover and Stop abort at EVERY datagram, but
    // an input-only flush must NOT cut an AU that has already started on the wire -- the standing "a
    // started AU completes; the new epoch's IDR follows it" exception (host_epoch_gate / the non-grant
    // Acquire path, which checks media+Stop only). The input fence for an original is the FIRST datagram
    // only (F3, below). (A replay, by contrast, IS input-fenced every datagram -- in send_udp_chunk_indices.)
    const auto mediaStopFence = [&]() -> bool {
      return (wire->limiter && wire->limiter->stopped()) ||
             (liveEpoch && liveEpoch->load(std::memory_order_acquire) != itemEpoch);
    };
    const auto releaseIfReserved = [&]() { if (reserved) wire->burstLedger->CancelUnsent(wireBytes); };
    // r6 (Codex 72a22d2, premature-NACK regression): the 2s window WAIT applies ONLY to a datagram that
    // can exceed the strict rate on its own -- a GRANT-covered IDR datagram, which peak-paces and bypasses
    // the limiter. A NORMAL datagram is already paced at the strict rate by WireLimiter::Acquire and
    // cannot push the 2s window past 2r + one datagram by itself, so it is only ACCOUNTED into the window
    // (CommitSent after it actually leaves), never made to WAIT on it. The earlier code Reserve-waited
    // EVERY datagram, double-pacing the stream so the client NACKed chunks that were merely delayed
    // (b7c9164, with no ledger, did not). Replays ride TryAcquire on top of the original, so they ARE
    // window-gated -- that path (send_udp_chunk_indices) is unchanged.
    const bool grantCovered = b1Active && wire->auHasGrant &&
                              wire->burstLedger->GrantCoverage(wire->burstOwner, wireBytes) >= wireBytes;
    if (grantCovered) {
      for (;;) {  // the window is the grant's ONLY cap (it bypasses the limiter): wait for room
        const uint64_t now = qpc_now_us();
        if (wire->burstLedger->Reserve(now, wireBytes)) { reserved = true; break; }
        if (mediaStopFence()) { wireAborted = true; return false; }  // give up an unsent datagram on fence
        udp_pace_wait_until(std::min<uint64_t>(wire->burstLedger->RoomAtUs(now, wireBytes), now + 2000ULL));
      }
      wire->burstLedger->DebitGrant(wire->burstOwner, wireBytes);
      // Peak-pace this grant-covered datagram (strict rate is bypassed for the grant owner's IDR).
      udp_pace_wait_until(wire->burstLedger->BurstSendDeadlineUs(qpc_now_us(), wireBytes));
      // r4 R5 R2 (Codex e1bc633): the grant skips WireLimiter::Acquire (the normal fence point), so after
      // the peak wait re-check the SAME media+Stop fence (S1: NOT input, so an input-only flush lets an
      // already-started AU complete), cancelling the reservation on a rollover/Stop during the wait.
      if (mediaStopFence()) { releaseIfReserved(); wireAborted = true; return false; }
    } else if (wire && wire->limiter) {
      if (wire->limiter->Acquire(wireBytes, liveEpoch, itemEpoch) == WireLimiter::Acq::Cancelled) {
        wireAborted = true;
        return false;
      }
    }
    // F3 (r3): the real permission point for the FIRST datagram is AFTER all its pacing/token waits.
    if (isFirstDatagram && wire && wire->inputEpoch &&
        wire->inputEpoch->load(std::memory_order_acquire) != wire->itemInputEpoch) {
      releaseIfReserved();
      wireAborted = true;
      return false;
    }
    const uint64_t callStartUs = stats ? qpc_now_us() : 0;
    const int n = haveSink ? wire->sink(datagram.data(), datagramLen, parity)
                           : sendto(s, reinterpret_cast<const char*>(datagram.data()), datagramLen, 0,
                                    reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    if (n <= 0) { releaseIfReserved(); return false; }
    // r4 B1 / r6: account this datagram into the 2s window at the REAL send time (1:1). A grant datagram
    // committed its prior reservation; a normal datagram is accounted directly (it never reserved -- it
    // was limiter-paced, not window-gated). CommitSent handles both (pending underflows to 0 when none).
    if (b1Active) wire->burstLedger->CommitSent(qpc_now_us(), wireBytes);
    // F4 (r3): actual-wire bytes, the instant the datagram leaves -- separate from the limiter's
    // pre-send reservation, and recorded even if the AU is aborted after this point.
    if (wire) {
      if (wire->outWireDatagrams) ++*wire->outWireDatagrams;
      if (parity) {
        if (wire->outWireParityBytes) *wire->outWireParityBytes += static_cast<uint64_t>(datagramLen) + kWireIpUdpHeaderBytes;
      } else {
        if (wire->outWireDataBytes) *wire->outWireDataBytes += static_cast<uint64_t>(datagramLen) + kWireIpUdpHeaderBytes;
      }
    }
    if (stats) {
      ++stats->datagrams;
      stats->headerBytes += sizeof(header);
      if ((header.flags & 0x10u) != 0) {
        ++stats->parityDatagrams;
        stats->parityBytes += byteCount;
      } else {
        stats->dataBytes += byteCount;
      }
      const uint64_t callDoneUs = qpc_now_us();
      const uint64_t callUs = callDoneUs >= callStartUs ? callDoneUs - callStartUs : 0;
      ++stats->payloadChunkCount;
      ++stats->payloadCallCount;
      stats->payloadUs += callUs;
      stats->payloadChunkMaxUs = std::max(stats->payloadChunkMaxUs, callUs);
    }
    // r4 R1: now that this datagram has left, give the sender a chance to interleave a bounded amount
    // of NACK replay from the shared bucket -- so a long cap-paced AU does not starve recovery until it
    // finishes. Runs on the sender thread, outside the limiter lock.
    if (wire && wire->betweenDatagrams) wire->betweenDatagrams();
    return true;
  };

  for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
    if (epoch_changed()) return UdpSendOutcome::EpochChanged;
    const size_t offset = static_cast<size_t>(chunkIndex) * stride;
    const uint32_t chunkSize =
        static_cast<uint32_t>(std::min<size_t>(stride, payloadSize - offset));
    UdpVideoChunkHeader h = baseHeader;
    h.chunkOffset = static_cast<uint32_t>(offset);
    h.chunkSize = chunkSize;
    h.chunkIndex = static_cast<uint16_t>(chunkIndex);
    h.chunkCount = static_cast<uint16_t>(chunkCount);
    h.chunkStride = stride;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u | 0x10u));
    if (offset == 0) h.flags |= 0x2u;
    if (offset + chunkSize >= payloadSize) h.flags |= 0x4u;
    if (!send_packet(h, payload + offset, chunkSize)) return wireAborted ? UdpSendOutcome::EpochChanged : UdpSendOutcome::TransportError;
  }

  // One XOR parity datagram per eight data datagrams repairs one loss in every group. The
  // parity is sent after the frame data so a short Wi-Fi burst is less likely to erase a data
  // packet and its repair packet together.
  //
  // Which eight matters more than how many. Wi-Fi drops packets in bursts, so grouping eight
  // consecutive chunks puts the whole burst in one group, where a single parity repairs
  // nothing. Interleaving -- group g holds chunks g, g+G, g+2G ... -- spreads a burst of up
  // to G across G groups, one loss each, all recoverable, at exactly the same cost.
  //
  // A parity datagram is always one stride long (the receiver rejects anything else), which is
  // why a single-chunk frame's stride is its own size: its parity is then the frame's size too.
  const bool interleaved = egress.fecInterleaved;
  std::vector<uint8_t> parity(stride, 0);
  for (uint32_t group = 0; group < fecGroupCount; ++group) {
    if (epoch_changed()) return UdpSendOutcome::EpochChanged;
    std::fill(parity.begin(), parity.end(), 0);
    const uint32_t firstChunk =
        interleaved ? group : (group * remote60::native_poc::kUdpVideoFecGroupSize);
    const uint32_t step = interleaved ? fecGroupCount : 1u;
    const uint32_t limit =
        interleaved ? chunkCount
                    : std::min<uint32_t>(chunkCount,
                                         firstChunk +
                                             remote60::native_poc::kUdpVideoFecGroupSize);
    for (uint32_t chunkIndex = firstChunk; chunkIndex < limit; chunkIndex += step) {
      const size_t offset = static_cast<size_t>(chunkIndex) * stride;
      const uint32_t chunkSize =
          static_cast<uint32_t>(std::min<size_t>(stride, payloadSize - offset));
      for (uint32_t i = 0; i < chunkSize; ++i) parity[i] ^= payload[offset + i];
    }
    UdpVideoChunkHeader h = baseHeader;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u));
    h.flags |= 0x10u;
    if (interleaved) h.flags |= 0x20u;
    h.chunkOffset = firstChunk * stride;
    h.chunkSize = stride;
    h.chunkIndex = static_cast<uint16_t>(firstChunk);
    h.chunkCount = static_cast<uint16_t>(chunkCount);
    h.chunkStride = stride;
    if (!send_packet(h, parity.data(), stride)) return wireAborted ? UdpSendOutcome::EpochChanged : UdpSendOutcome::TransportError;
  }

  if (stats) {
    const uint64_t doneUs = qpc_now_us();
    stats->payloadUs = doneUs >= startUs ? doneUs - startUs : stats->payloadUs;
  }
  return UdpSendOutcome::Sent;
}

bool send_udp_chunks(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                     uint32_t mtuBytes, const UdpEgressConfig& egress) {
  return send_udp_chunks_impl(s, peer, payload, payloadSize, baseHeader, mtuBytes, nullptr,
                              nullptr, 0, egress) == UdpSendOutcome::Sent;
}

UdpSendOutcome send_udp_chunks_timed(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                     uint32_t mtuBytes, SendPathStats* stats,
                                     const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch,
                                     const UdpEgressConfig& egress, const WireEgress* wire) {
  return send_udp_chunks_impl(s, peer, payload, payloadSize, baseHeader, mtuBytes, stats, liveEpoch,
                              itemEpoch, egress, wire);
}

UdpSendOutcome send_udp_chunk_indices(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                      size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                      uint32_t mtuBytes, bool tightSingleChunk,
                                      const uint16_t* indices, uint16_t count,
                                      uint64_t* outWireBytes,
                                      uint64_t* outDatagrams,
                                      const WireEgress* wire,
                                      uint64_t* outSuppressed,
                                      bool* outFenced) {
  const bool haveSink = wire && wire->sink;
  if (!payload || payloadSize == 0 || (s == INVALID_SOCKET && !haveSink) || !indices || count == 0)
    return UdpSendOutcome::TransportError;
  // The same geometry as the original send: the receiver discards an assembly whose stride a
  // later chunk contradicts, so a replay with the wrong stride would destroy what it repairs.
  const UdpChunkGeometry geo = udp_chunk_geometry(payloadSize, mtuBytes, tightSingleChunk);
  if (!geo.valid) return UdpSendOutcome::TransportError;
  const uint32_t stride = geo.chunkStride;
  const uint32_t chunkCount = geo.chunkCount;
  std::vector<uint8_t> datagram(sizeof(UdpVideoChunkHeader) + geo.maxChunk);
  for (uint16_t i = 0; i < count; ++i) {
    const uint32_t chunkIndex = indices[i];
    if (chunkIndex >= chunkCount) continue;  // stale/garbled request -- skip, never index OOB
    const size_t offset = static_cast<size_t>(chunkIndex) * stride;
    const uint32_t chunkSize =
        static_cast<uint32_t>(std::min<size_t>(stride, payloadSize - offset));
    UdpVideoChunkHeader h = baseHeader;  // same seq / streamGeneration / codec / flags(key) / sizes
    h.chunkOffset = static_cast<uint32_t>(offset);
    h.chunkSize = chunkSize;
    h.chunkIndex = static_cast<uint16_t>(chunkIndex);
    h.chunkCount = static_cast<uint16_t>(chunkCount);
    h.chunkStride = stride;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u | 0x10u));  // recompute first/last, never parity
    if (offset == 0) h.flags |= 0x2u;
    if (offset + chunkSize >= payloadSize) h.flags |= 0x4u;
    const int datagramLen = static_cast<int>(sizeof(h) + chunkSize);
    // r5 G2: the live session fence, checked AT the send (not a stale snapshot) and INDEPENDENT of the
    // token mechanism, so it holds for the fast path (tokens already available), the blocking wait, and
    // the non-blocking TryAcquire alike. A rollover (media epoch moved) or an input flush (input epoch
    // moved) that lands before this datagram commits stops the replay -- it must not reach the new
    // session / the flushed picture. Checked just before acquiring tokens so no token is spent on a
    // datagram that will not go out.
    if (wire) {
      if ((wire->mediaEpoch && wire->mediaEpoch->load(std::memory_order_acquire) != wire->itemMediaEpoch) ||
          (wire->inputEpoch && wire->inputEpoch->load(std::memory_order_acquire) != wire->itemInputEpoch)) {
        if (outFenced) *outFenced = true;  // r6 H2: permanent invalidation, not a token shortage
        break;
      }
    }
    // The common wire cap. By default non-blocking (a replay chunk that does not fit the shared budget
    // now is left out so a NACK burst cannot push the stream over the cap -- the client's keyframe
    // fallback recovers it). When blockingAcquire is set (r4 R1 sender-thread interleave), WAIT for the
    // tokens so the replay gets a fair share of a bucket a saturated original is draining; a rollover
    // (media epoch move) cancels the wait and the replay stops. Not a transport error either way.
    if (wire && wire->limiter) {
      bool got;
      if (wire->blockingAcquire) {
        got = wire->limiter->Acquire(static_cast<uint64_t>(datagramLen) + kWireIpUdpHeaderBytes,
                                     wire->mediaEpoch, wire->itemMediaEpoch) == WireLimiter::Acq::Permitted;
      } else {
        got = wire->limiter->TryAcquire(static_cast<uint64_t>(datagramLen) + kWireIpUdpHeaderBytes);
      }
      if (!got) {
        if (outSuppressed) ++*outSuppressed;
        continue;
      }
      // r5 G2: a blocking Acquire may have WAITED; re-check the fence after it so a rollover/flush that
      // landed during the wait (or slipped past the token fast path) still stops this datagram. The
      // token just spent is given back to the bucket so the budget is not charged for an unsent chunk.
      const bool fencedAfterWait =
          (wire->mediaEpoch && wire->mediaEpoch->load(std::memory_order_acquire) != wire->itemMediaEpoch) ||
          (wire->inputEpoch && wire->inputEpoch->load(std::memory_order_acquire) != wire->itemInputEpoch);
      if (fencedAfterWait) {
        wire->limiter->Refund(static_cast<uint64_t>(datagramLen) + kWireIpUdpHeaderBytes);
        if (outFenced) *outFenced = true;  // r6 H2
        break;
      }
    }
    // r4 B1: a replay is a B1 datagram -- it must also pass the 2s window admission. If the window is
    // full, suppress this replay (the client's IDR fallback covers it) and give the limiter token back,
    // so a replay cannot push the rolling average past 2r (Codex B1-1's 20000-replay counter-example).
    const uint64_t nackWireBytes = static_cast<uint64_t>(datagramLen) + kWireIpUdpHeaderBytes;
    bool nackReserved = false;
    if (wire && wire->burstLedger && wire->limiter && wire->limiter->enabled()) {
      if (!wire->burstLedger->Reserve(qpc_now_us(), nackWireBytes)) {
        wire->limiter->Refund(nackWireBytes);
        if (outSuppressed) ++*outSuppressed;
        continue;
      }
      nackReserved = true;
    }
    std::memcpy(datagram.data(), &h, sizeof(h));
    std::memcpy(datagram.data() + sizeof(h), payload + offset, chunkSize);
    const int sent = haveSink ? wire->sink(datagram.data(), datagramLen, false)
                              : sendto(s, reinterpret_cast<const char*>(datagram.data()), datagramLen, 0,
                                       reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    if (sent > 0) {
      if (outWireBytes) *outWireBytes += static_cast<uint64_t>(sent);
      if (outDatagrams) ++*outDatagrams;
      if (nackReserved) wire->burstLedger->CommitSent(qpc_now_us(), nackWireBytes);  // real send -> window
    } else if (nackReserved) {
      wire->burstLedger->CancelUnsent(nackWireBytes);
    }
  }
  return UdpSendOutcome::Sent;
}

}  // namespace remote60::native_poc
