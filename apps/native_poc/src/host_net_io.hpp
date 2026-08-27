#pragma once

// Socket I/O primitives and the UDP video chunk sender (pacing + XOR FEC) used by the host.
//
// Role:    bind-address parsing, timed TCP send (send_all/recv_all/recv_discard/WinsockScope come
//          from native_socket.hpp -- the host used byte-identical private copies before the split)
//          with optional timing (SendPathStats), and send_udp_chunks*: split one encoded AU into
//          MTU-sized datagrams, pace them against the configured peak bitrate, append one XOR
//          parity datagram per FEC group (consecutive or interleaved), and abort mid-frame when the
//          media epoch rolls over (UdpSendOutcome::EpochChanged).
// Thread:  send_* are called on the sender thread (UDP) or main/control threads (TCP); recv_* on
//          the reader/control threads. The three gUdp* atomics are written by the control/reader
//          threads (hello, runtime tune) and read by the sender -- relaxed loads, no ordering
//          requirement beyond "eventually".
// Input:   sockets, peer address, payload + UdpVideoChunkHeader template, MTU, live epoch.
// Output:  bytes on the wire; SendPathStats accumulators; UdpSendOutcome.
// Callers: native_video_host_main.cpp (sender thread, UDP reader, control session, main loop).
//
// Extracted verbatim from native_video_host_main.cpp (host split refactor Phase 0-5). Definitions
// live in host_net_io.cpp; the former file-scope globals became inline variables here so the main
// loop keeps using them unqualified. Behavior is byte-identical.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "native_socket.hpp"  // WinsockScope (the host used a byte-identical private copy; now the shared one)
#include "poc_protocol.hpp"

namespace remote60::native_poc {

/** Network-order address for bind(); 0.0.0.0 when unset. A typo must not bind nowhere silently. */
ULONG resolve_bind_address(const std::string& bindAddress);


struct SendPathStats {
  uint64_t headerUs = 0;
  uint64_t payloadUs = 0;
  uint64_t headerCallCount = 0;
  uint64_t payloadCallCount = 0;
  uint64_t payloadChunkCount = 0;
  uint64_t payloadChunkMaxUs = 0;
};

bool send_all_timed(SOCKET s, const void* data, size_t len, uint64_t* outUs,
                    uint64_t* outCallCount);

// Peak send rate used to spread one frame's datagrams over time, as a multiple of the
// configured average bitrate. Sending a whole keyframe as an unthrottled burst overruns the
// Wi-Fi buffer on the AP and on the phone, which is the usual cause of the picture breaking
// up on an otherwise healthy link.
// Must exceed the largest datagram the peer can send. A datagram that does not fit is dropped
// with WSAEMSGSIZE, which looked like a handshake failure the first time it happened.
constexpr size_t kUdpReceiveBufferBytes = 4096;

// What the wire needs to know about one send, passed explicitly instead of read from process
// globals. These three used to be inline globals here -- live cross-thread state written by the
// main loop (pace peak, from ApplyTarget), the UDP handshake (FEC layout, from the viewer's
// hello) and startup (keyframe peak), and read by the sender thread. SenderState owns them now
// and the sender snapshots them once per dequeue, so a send is a pure function of its arguments
// and tests stop leaking configuration into each other. (Ledger H-22.)
struct UdpEgressConfig {
  uint32_t pacePeakBps = 0;                 // 0 disables intra-frame pacing
  uint32_t keyframePacePeakBps = 100000000;
  // Set from the viewer's hello. Older viewers do not advertise it and must keep receiving the
  // consecutive layout they know how to repair.
  bool fecInterleaved = false;
};

void udp_pace_wait_until(uint64_t targetUs);

// Returns the per-frame send budget in microseconds, or 0 when the frame should go out as
// fast as possible (small frames are not worth the pacing overhead).
uint64_t udp_pace_budget_us(const UdpEgressConfig& egress, size_t payloadSize, uint32_t chunkCount,
                            bool keyFrame);

// Result of a chunked UDP video send. EpochChanged means a session rollover bumped the media epoch
// mid-frame, so the remaining chunks were aborted rather than interleaved into the new session --
// the caller must NOT treat this as a transport failure (the rollover already cleared the queue and
// re-armed the barrier). TransportError is a real sendto failure on the current epoch.
enum class UdpSendOutcome { Sent, TransportError, EpochChanged };

// liveEpoch/itemEpoch let a rollover abort a chunked send mid-frame: if the live media epoch no
// longer matches the epoch this frame was stamped for, the remaining data/parity packets are the
// old session's and must not reach a freshly attached decoder. nullptr liveEpoch disables the check.
UdpSendOutcome send_udp_chunks_impl(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                    size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                    uint32_t mtuBytes, SendPathStats* stats,
                                    const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch,
                                    const UdpEgressConfig& egress);

bool send_udp_chunks(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                     uint32_t mtuBytes, const UdpEgressConfig& egress = {});

UdpSendOutcome send_udp_chunks_timed(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                     uint32_t mtuBytes, SendPathStats* stats,
                                     const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch,
                                     const UdpEgressConfig& egress);

}  // namespace remote60::native_poc
