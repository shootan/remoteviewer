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
#include <functional>
#include <string>

#include "native_socket.hpp"  // WinsockScope (the host used a byte-identical private copy; now the shared one)
#include "poc_protocol.hpp"
#include "host_burst_ledger.hpp"  // BurstLedger + BurstAuId (r4 B1)

namespace remote60::native_poc {

class WireLimiter;  // host_wire_limiter.hpp

// The hard wire-rate cap wired into the send path (bitrate-hard-cap r1). Both are optional and
// null in the ordinary call: `limiter` null leaves the legacy behaviour (pacing only, no cap);
// `sink` null sends through the real socket. A test passes a limiter driven by a fake clock and a
// sink that records (time, length, kind) so the same send code is measured deterministically.
struct WireEgress {
  WireLimiter* limiter = nullptr;  // charged datagramLen + 28 (IP+UDP) before each datagram
  // stutter-keyframe r4 B1: the common 2s-window burst ledger. When the cap is ON, EVERY datagram that
  // actually leaves is Record(len+28)'d here 1:1 (data, parity, NACK retransmit) so the rolling-window
  // average A(t-2s,t] reflects the true wire bytes. Only touched on the sender thread while the cap is
  // enabled (the reader thread sends only when the cap is off), so it needs no lock. null = not tracked.
  BurstLedger* burstLedger = nullptr;
  // r4 B1 (grant/burst): this AU holds a burst grant (a clamp-detected real self-IDR), owned by
  // burstOwner (media epoch, generation, input epoch, seq). A datagram the grant covers skips the
  // strict token wait and is peak-paced; the window admission still applies. GrantCoverage/DebitGrant
  // check burstOwner so only this AU's own datagrams spend its grant.
  bool auHasGrant = false;
  BurstAuId burstOwner;
  // Replaces sendto when set. datagram = header+payload bytes, len its length, parity true for an
  // FEC datagram (header flag 0x10). Return > 0 to mean "sent" (the byte count), <= 0 a failure.
  std::function<int(const uint8_t* datagram, int len, bool parity)> sink;
  // Input-epoch fence re-checked AFTER the first datagram's token wait (bitrate-hard-cap r3 F3): the
  // sender's first-datagram permission point is before the limiter wait, so without this an AU that
  // was permitted and then waited for tokens would start on the wire after a flush changed the input
  // epoch. When set, the first datagram is permitted only if `*inputEpoch == itemInputEpoch` AFTER its
  // pacing/token wait; otherwise the send aborts as an epoch change (no datagram goes out). Later
  // datagrams keep the existing mid-AU behaviour (an AU already started completes). null = inactive.
  const std::atomic<uint64_t>* inputEpoch = nullptr;
  uint64_t itemInputEpoch = 0;
  // Actual-wire accounting (r3 F4): every datagram that actually leaves (len + 28) is added here,
  // by kind, the instant the send succeeds -- separate from the limiter's pre-send reservation and
  // reflected even when the AU is later aborted/partial. null = not collected.
  uint64_t* outWireDataBytes = nullptr;
  uint64_t* outWireParityBytes = nullptr;
  uint64_t* outWireDatagrams = nullptr;
  // Blocking-acquire replay (bitrate-hard-cap r4 R1): make send_udp_chunk_indices wait for tokens
  // (limiter Acquire against mediaEpoch/itemMediaEpoch) instead of the non-blocking TryAcquire, so the
  // sender-thread interleave gets a fair share of a bucket a saturated original is draining. Only the
  // sender thread sets this (the reader never blocks on the wire). Ignored when limiter is null.
  bool blockingAcquire = false;
  const std::atomic<uint64_t>* mediaEpoch = nullptr;
  uint64_t itemMediaEpoch = 0;
  // Interleave hook (bitrate-hard-cap r4 R1): called AFTER each datagram of this AU actually leaves,
  // on the sender thread, so a long cap-paced AU still services NACK replays from the SAME bucket
  // while it is going out -- not only between whole AUs. The sender sets this to a bounded drain (at
  // most a few replay chunks per original datagram, so original data keeps priority and the shared
  // cap still bounds the sum). It runs outside any limiter lock. null = no interleave (legacy/tests).
  std::function<void()> betweenDatagrams;
  // r8 D2 (Codex 597aca2): injected clock + wait for a DETERMINISTIC test of the unified admission loop.
  // Null in production -> the loop uses qpc_now_us() and udp_pace_wait_until (real clock/sleep). A test
  // sets both (and points WireLimiter at the SAME clock) so the loop's now, the ledger window timestamps,
  // and the pacing wait share ONE controllable time axis -- this is what makes the D1 busy-spin and the
  // grant->quota->tail sequence reproducible without a real-clock race. Same injection pattern as
  // WireLimiter's clock; null here means no behaviour change (the shipped host never sets them).
  std::function<uint64_t()> nowFn;       // admission-loop clock (ledger window timestamps too)
  std::function<void(uint64_t)> waitFn;  // admission-loop cancellable wait (advances the fake clock in tests)
  // r10 (Codex 597aca2 release note): reproduce the r6 defect in the REAL send path for a negative
  // control -- a NORMAL (non-grant) datagram skips ONLY the 2s-window admission gate while still being
  // strict-rate paced by the limiter, accounted (CommitSent) and sent through the sink. false in
  // production (the shipped loop always admits); a test sets it to show that bypassing the window makes
  // the SAME 2r+L assertion FAIL. Grant-covered datagrams are unaffected. Same null-default test-seam
  // category as nowFn/waitFn above -- it does NOT change the shipped behaviour.
  bool bypassWindowForTest = false;
};

/** Network-order address for bind(); 0.0.0.0 when unset. A typo must not bind nowhere silently. */
ULONG resolve_bind_address(const std::string& bindAddress);


struct SendPathStats {
  uint64_t headerUs = 0;
  uint64_t payloadUs = 0;
  uint64_t headerCallCount = 0;
  uint64_t payloadCallCount = 0;
  uint64_t payloadChunkCount = 0;
  uint64_t payloadChunkMaxUs = 0;
  // What went on the wire, by kind (quality r1). Datagram payload only -- IP/UDP headers are the
  // caller's estimate. dataBytes is the AU itself; parityBytes the FEC datagrams' payload (always a
  // full chunk stride, even for a short last group -- and the stride of a frame that fits in one
  // datagram is that frame's size, see UdpChunkGeometry); headerBytes the UdpVideoChunkHeader of
  // every datagram, data and parity alike.
  uint64_t dataBytes = 0;
  uint64_t parityBytes = 0;
  uint64_t headerBytes = 0;
  uint64_t datagrams = 0;
  uint64_t parityDatagrams = 0;
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
  // A frame that fits in one datagram is chunked with chunkStride = its own size instead of the
  // MTU chunk (fec-single-chunk-stride, 2026-09-28). Its one XOR parity datagram must be exactly
  // chunkStride bytes for every receiver in the field, so with the MTU stride a 200-byte P frame
  // was followed by a 1112-byte parity: on a still screen parity cost several times the video.
  // With the tight stride the parity is the frame's size -- still a full replica that repairs the
  // loss of the data datagram. Nothing on the wire says which stride was used; the receiver's
  // checks (ceil(payloadSize / chunkStride) == chunkCount, chunkOffset == index * chunkStride,
  // parity chunkSize == chunkStride) hold for both, so no negotiation is involved. Frames of two
  // chunks or more keep the MTU stride, byte for byte. false restores the padded layout
  // (REMOTE60_NATIVE_FEC_SINGLE_CHUNK_STRIDE=0, the field rollback lever).
  bool fecSingleChunkTightStride = true;
};

// One frame's datagram layout. Computed once per send and shared by the original send, the NACK
// replay of that frame and the pacing budget, so the three can never disagree on a stride: a
// replayed chunk whose chunkStride differs from the assembly's makes the receiver discard the
// whole assembly as malformed.
struct UdpChunkGeometry {
  bool valid = false;          // false: no payload, MTU too small for a header, or > 65535 chunks
  uint32_t maxChunk = 0;       // MTU minus the chunk header: the most one datagram carries
  uint32_t chunkStride = 0;    // UdpVideoChunkHeader::chunkStride of every datagram of the frame
  uint32_t chunkCount = 0;     // data datagrams
  uint32_t fecGroupCount = 0;  // parity datagrams: one per kUdpVideoFecGroupSize data chunks
  uint32_t packetCount = 0;    // chunkCount + fecGroupCount (what pacing spreads the budget over)
  uint64_t parityBytes = 0;    // fecGroupCount * chunkStride: every parity datagram is one stride
};
UdpChunkGeometry udp_chunk_geometry(size_t payloadSize, uint32_t mtuBytes, bool tightSingleChunk);

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
                                    const UdpEgressConfig& egress, const WireEgress* wire = nullptr);

bool send_udp_chunks(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                     uint32_t mtuBytes, const UdpEgressConfig& egress = {});

UdpSendOutcome send_udp_chunks_timed(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                     size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                     uint32_t mtuBytes, SendPathStats* stats,
                                     const std::atomic<uint64_t>* liveEpoch, uint64_t itemEpoch,
                                     const UdpEgressConfig& egress, const WireEgress* wire = nullptr);

// Selective retransmit: re-send only the data chunks named in `indices` for the AU described by
// `payload`/`baseHeader`/`mtuBytes`/`tightSingleChunk`, using the exact same chunk geometry as the
// original send (so the client assembles them into the same frame -- `tightSingleChunk` must be
// the fecSingleChunkTightStride the frame was first sent with; the NACK cache carries it). No FEC
// and no pacing -- it is a handful of small datagrams answering a NACK on a low-RTT path.
// Out-of-range indices are skipped. (video NACK.)
// outWireBytes / outDatagrams (optional): what the replay actually put on the wire, header
// included, for the per-flow byte accounting (quality r1).
// `wire` (bitrate-hard-cap r1): the common limiter + optional sink. Retransmit charges the SAME
// bucket as the live send. By default it uses TryAcquire (non-blocking) -- a chunk that does not fit
// now is left for the next pass. When WireEgress::blockingAcquire is set (r4 R1: the sender-thread
// interleave, where a saturated original would otherwise win every refilled token and starve the
// replay), it uses the blocking Acquire against WireEgress::mediaEpoch/itemMediaEpoch so the replay
// gets a fair share of the bucket and a rollover cancels it. `outSuppressed` counts chunks skipped.
UdpSendOutcome send_udp_chunk_indices(SOCKET s, const sockaddr_in& peer, const uint8_t* payload,
                                      size_t payloadSize, const UdpVideoChunkHeader& baseHeader,
                                      uint32_t mtuBytes, bool tightSingleChunk,
                                      const uint16_t* indices, uint16_t count,
                                      uint64_t* outWireBytes = nullptr,
                                      uint64_t* outDatagrams = nullptr,
                                      const WireEgress* wire = nullptr,
                                      uint64_t* outSuppressed = nullptr,
                                      // r6 H2: set true if the live media/input fence stopped the send
                                      // (a PERMANENT invalidation -- rollover/flush) as opposed to a
                                      // temporary token shortage, so the caller can retire the request
                                      // instead of retrying it and blocking later valid ones.
                                      bool* outFenced = nullptr);

}  // namespace remote60::native_poc
