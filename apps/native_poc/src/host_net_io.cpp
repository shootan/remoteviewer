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
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "time_utils.hpp"

namespace remote60::native_poc {

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
uint64_t udp_pace_budget_us(size_t payloadSize, uint32_t chunkCount, bool keyFrame) {
  uint32_t peakBps = gUdpPacePeakBitrateBps.load(std::memory_order_relaxed);
  if (keyFrame && peakBps != 0) {
    // IDRs are much larger than delta frames. Pacing one at only a small multiple of the
    // average bitrate blocks the sender for several frame periods, fills the latest-wins
    // queue, and triggers another IDR -- a self-sustaining low-FPS loop. Keep pacing, but
    // give recovery frames enough wire rate to finish inside roughly one 60 Hz interval.
    peakBps = std::max(peakBps,
                       gUdpKeyframePacePeakBitrateBps.load(std::memory_order_relaxed));
  }
  if (peakBps == 0 || chunkCount <= 8) return 0;
  return (static_cast<uint64_t>(payloadSize) * 8ULL * 1000000ULL) / static_cast<uint64_t>(peakBps);
}

// liveEpoch/itemEpoch let a rollover abort a chunked send mid-frame: if the live media epoch no
// longer matches the epoch this frame was stamped for, the remaining data/parity packets are the
// old session's and must not reach a freshly attached decoder. nullptr liveEpoch disables the check.
UdpSendOutcome send_udp_chunks_impl(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                    size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                    uint32_t mtuBytes, SendPathStats* stats,
                                    const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch) {
  if (!payload || payloadSize == 0 || s == INVALID_SOCKET) return UdpSendOutcome::TransportError;
  if (payloadSize > std::numeric_limits<uint32_t>::max()) return UdpSendOutcome::TransportError;
  const uint64_t startUs = qpc_now_us();
  const uint32_t safeMtu = clamp_udp_mtu(mtuBytes);
  if (safeMtu <= sizeof(UdpVideoChunkHeader)) return UdpSendOutcome::TransportError;
  const uint32_t maxChunk = safeMtu - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::vector<uint8_t> datagram(safeMtu);
  const uint32_t chunkCount =
      static_cast<uint32_t>((payloadSize + maxChunk - 1) / maxChunk);
  if (chunkCount == 0 || chunkCount > std::numeric_limits<uint16_t>::max())
    return UdpSendOutcome::TransportError;
  const auto epoch_changed = [&]() {
    return liveEpoch && liveEpoch->load(std::memory_order_relaxed) != itemEpoch;
  };
  const uint32_t fecGroupCount =
      (chunkCount + remote60::native_poc::kUdpVideoFecGroupSize - 1u) /
      remote60::native_poc::kUdpVideoFecGroupSize;
  const uint32_t packetCount = chunkCount + fecGroupCount;
  const uint64_t pacedPayloadBytes =
      static_cast<uint64_t>(payloadSize) + static_cast<uint64_t>(fecGroupCount) * maxChunk;
  const uint64_t budgetUs =
      udp_pace_budget_us(static_cast<size_t>(pacedPayloadBytes), packetCount,
                         (baseHeader.flags & 0x1u) != 0);
  uint32_t packetOrdinal = 0;

  auto send_packet = [&](const UdpVideoChunkHeader& header, const uint8_t* bytes,
                         uint32_t byteCount) -> bool {
    if (budgetUs > 0 && packetOrdinal > 0) {
      udp_pace_wait_until(startUs + (budgetUs * packetOrdinal) / packetCount);
    }
    ++packetOrdinal;
    std::memcpy(datagram.data(), &header, sizeof(header));
    std::memcpy(datagram.data() + sizeof(header), bytes, byteCount);
    const uint64_t callStartUs = stats ? qpc_now_us() : 0;
    const int n = sendto(s, reinterpret_cast<const char*>(datagram.data()),
                         static_cast<int>(sizeof(header) + byteCount), 0,
                         reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
    if (n <= 0) return false;
    if (stats) {
      const uint64_t callDoneUs = qpc_now_us();
      const uint64_t callUs = callDoneUs >= callStartUs ? callDoneUs - callStartUs : 0;
      ++stats->payloadChunkCount;
      ++stats->payloadCallCount;
      stats->payloadUs += callUs;
      stats->payloadChunkMaxUs = std::max(stats->payloadChunkMaxUs, callUs);
    }
    return true;
  };

  for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
    if (epoch_changed()) return UdpSendOutcome::EpochChanged;
    const size_t offset = static_cast<size_t>(chunkIndex) * maxChunk;
    const uint32_t chunkSize =
        static_cast<uint32_t>(std::min<size_t>(maxChunk, payloadSize - offset));
    UdpVideoChunkHeader h = baseHeader;
    h.chunkOffset = static_cast<uint32_t>(offset);
    h.chunkSize = chunkSize;
    h.chunkIndex = static_cast<uint16_t>(chunkIndex);
    h.chunkCount = static_cast<uint16_t>(chunkCount);
    h.chunkStride = maxChunk;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u | 0x10u));
    if (offset == 0) h.flags |= 0x2u;
    if (offset + chunkSize >= payloadSize) h.flags |= 0x4u;
    if (!send_packet(h, payload + offset, chunkSize)) return UdpSendOutcome::TransportError;
  }

  // One XOR parity datagram per eight data datagrams repairs one loss in every group. The
  // parity is sent after the frame data so a short Wi-Fi burst is less likely to erase a data
  // packet and its repair packet together.
  //
  // Which eight matters more than how many. Wi-Fi drops packets in bursts, so grouping eight
  // consecutive chunks puts the whole burst in one group, where a single parity repairs
  // nothing. Interleaving -- group g holds chunks g, g+G, g+2G ... -- spreads a burst of up
  // to G across G groups, one loss each, all recoverable, at exactly the same cost.
  const bool interleaved = gUdpVideoFecInterleaved.load(std::memory_order_relaxed);
  std::vector<uint8_t> parity(maxChunk, 0);
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
      const size_t offset = static_cast<size_t>(chunkIndex) * maxChunk;
      const uint32_t chunkSize =
          static_cast<uint32_t>(std::min<size_t>(maxChunk, payloadSize - offset));
      for (uint32_t i = 0; i < chunkSize; ++i) parity[i] ^= payload[offset + i];
    }
    UdpVideoChunkHeader h = baseHeader;
    h.flags &= static_cast<uint16_t>(~(0x2u | 0x4u));
    h.flags |= 0x10u;
    if (interleaved) h.flags |= 0x20u;
    h.chunkOffset = firstChunk * maxChunk;
    h.chunkSize = maxChunk;
    h.chunkIndex = static_cast<uint16_t>(firstChunk);
    h.chunkCount = static_cast<uint16_t>(chunkCount);
    h.chunkStride = maxChunk;
    if (!send_packet(h, parity.data(), maxChunk)) return UdpSendOutcome::TransportError;
  }

  if (stats) {
    const uint64_t doneUs = qpc_now_us();
    stats->payloadUs = doneUs >= startUs ? doneUs - startUs : stats->payloadUs;
  }
  return UdpSendOutcome::Sent;
}

bool send_udp_chunks(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                     uint32_t mtuBytes) {
  return send_udp_chunks_impl(s, peer, payload, payloadSize, baseHeader, mtuBytes, nullptr,
                              nullptr, 0) == UdpSendOutcome::Sent;
}

UdpSendOutcome send_udp_chunks_timed(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                     uint32_t mtuBytes, SendPathStats* stats,
                                     const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch) {
  return send_udp_chunks_impl(s, peer, payload, payloadSize, baseHeader, mtuBytes, stats, liveEpoch,
                              itemEpoch);
}

}  // namespace remote60::native_poc
