// fec-single-chunk-stride (2026-09-28): a frame that fits in one datagram is chunked with its own
// size as the stride, so its XOR parity datagram is the frame's size and not a padded MTU chunk.
// On a still screen the parity used to cost several times the video itself.
//
// What runs is the host's real chunker (send_udp_chunks_timed / send_udp_chunk_indices in
// host_net_io.cpp) over a loopback UDP socket, and what judges its datagrams is the real shared
// assembler (UdpH264FrameAssembler, native_video_client_shared_core.cpp) -- the code the Windows
// viewer and the Android session assemble with today, unchanged by this work. Nothing here mirrors
// the layout: every expectation about what a receiver accepts is the receiver's own verdict.
//
//   - geometry: stride / chunkCount / packetCount at 1, maxChunk-1, maxChunk, maxChunk+1, and
//     that frames of two chunks or more are byte for byte what the padded layout sends;
//   - wire: a single-chunk frame is exactly two datagrams, data and a parity that is a replica of
//     the data, header + payload each; the padded layout is what it was (parity = MTU chunk);
//   - the receiver: for every boundary size, both layouts, immediate and in-order-hold delivery --
//     lossless, data lost (parity repairs it), parity lost, both lost (the next frame shows the
//     gap), parity before data, a duplicate, a generation switch -- the shared assembler reaches
//     the same verdicts under both layouts, and the payload it delivers is the frame;
//   - the NACK replay: send_udp_chunk_indices with the stride policy the frame was sent with
//     produces the original datagram again; with the other policy the assembler discards the
//     assembly it was meant to repair, which is why the cache carries the policy.
//
// Build: remote60_udp_fec_single_chunk_test (CMake). Run: prints PASS/FAIL per check and
// "udp_fec_single_chunk_test: PASS" with exit 0. pure-logic + loopback network; no product process.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "host_net_io.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"

#pragma comment(lib, "ws2_32.lib")

using namespace remote60::native_poc;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool ok, const std::string& what, const std::string& detail = std::string()) {
  ++gChecks;
  if (ok) {
    std::printf("PASS %s\n", what.c_str());
  } else {
    std::printf("FAIL %s%s%s\n", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    ++gFailures;
  }
}

constexpr uint32_t kMtu = 1200;
const uint32_t kHeader = static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
uint32_t max_chunk() { return clamp_udp_mtu(kMtu) - kHeader; }

std::vector<uint8_t> make_payload(size_t n, uint32_t seed) {
  std::vector<uint8_t> p(n);
  uint32_t s = seed * 2654435761u + 12345u;
  for (size_t i = 0; i < n; ++i) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    p[i] = static_cast<uint8_t>(s >> 24);
  }
  return p;
}

struct Datagram {
  UdpVideoChunkHeader h{};
  std::vector<uint8_t> bytes;  // the whole datagram, header included
  bool parity() const { return (h.flags & 0x10u) != 0; }
  const uint8_t* body() const { return bytes.data() + sizeof(UdpVideoChunkHeader); }
  uint32_t body_size() const { return static_cast<uint32_t>(bytes.size() - sizeof(UdpVideoChunkHeader)); }
};

class Loop {
 public:
  SOCKET tx = INVALID_SOCKET;
  SOCKET rx = INVALID_SOCKET;
  sockaddr_in rxAddr{};

  bool Start() {
    tx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tx == INVALID_SOCKET || rx == INVALID_SOCKET) return false;
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    any.sin_port = 0;
    if (bind(tx, reinterpret_cast<sockaddr*>(&any), sizeof(any)) != 0) return false;
    if (bind(rx, reinterpret_cast<sockaddr*>(&any), sizeof(any)) != 0) return false;
    int len = sizeof(rxAddr);
    if (getsockname(rx, reinterpret_cast<sockaddr*>(&rxAddr), &len) != 0) return false;
    const DWORD timeoutMs = 20;
    (void)setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    const int rcvBuf = 4 * 1024 * 1024;
    (void)setsockopt(rx, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    return true;
  }
  void Stop() {
    if (tx != INVALID_SOCKET) closesocket(tx);
    if (rx != INVALID_SOCKET) closesocket(rx);
    tx = rx = INVALID_SOCKET;
  }
  // Everything that arrives until the socket has been quiet for `quietMs` (bounded by maxMs).
  std::vector<Datagram> Drain(uint32_t quietMs = 40, uint32_t maxMs = 2000) {
    std::vector<Datagram> out;
    std::vector<uint8_t> buf(4096);
    const auto start = std::chrono::steady_clock::now();
    auto last = start;
    for (;;) {
      const int n = recv(rx, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
      const auto now = std::chrono::steady_clock::now();
      if (n >= static_cast<int>(sizeof(UdpVideoChunkHeader))) {
        Datagram d;
        std::memcpy(&d.h, buf.data(), sizeof(d.h));
        d.bytes.assign(buf.begin(), buf.begin() + n);
        out.push_back(std::move(d));
        last = now;
      }
      if (now - last > std::chrono::milliseconds(quietMs)) break;
      if (now - start > std::chrono::milliseconds(maxMs)) break;
    }
    return out;
  }
};

UdpVideoChunkHeader base_header(uint32_t seq, uint64_t generation, bool key, size_t payloadSize) {
  UdpVideoChunkHeader h{};
  h.magic = kMagic;
  h.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
  h.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
  h.seq = seq;
  h.codec = static_cast<uint16_t>(UdpCodec::H264);
  h.flags = key ? 0x1u : 0u;
  h.width = 1920;
  h.height = 1080;
  h.payloadSize = static_cast<uint32_t>(payloadSize);
  h.streamGeneration = generation;
  h.captureQpcUs = 1000000 + seq;
  h.encodeStartQpcUs = 1000010 + seq;
  h.encodeEndQpcUs = 1000020 + seq;
  h.sendQpcUs = 1000030 + seq;
  return h;
}

UdpEgressConfig egress_for(bool tight, bool interleaved) {
  UdpEgressConfig e;
  e.pacePeakBps = 0;  // loopback: no pacing
  e.fecInterleaved = interleaved;
  e.fecSingleChunkTightStride = tight;
  return e;
}

// The host's chunker, as the sender thread calls it, then the datagrams it put on the wire.
std::vector<Datagram> send_frame(Loop& loop, const std::vector<uint8_t>& payload, uint32_t seq,
                                 uint64_t generation, bool key, bool tight, bool interleaved,
                                 SendPathStats* stats = nullptr) {
  const UdpVideoChunkHeader h = base_header(seq, generation, key, payload.size());
  const UdpSendOutcome r = send_udp_chunks_timed(loop.tx, loop.rxAddr, payload.data(), payload.size(),
                                                 h, kMtu, stats, nullptr, 0,
                                                 egress_for(tight, interleaved));
  if (r != UdpSendOutcome::Sent) return {};
  return loop.Drain();
}

// The host's NACK replay of `indices`, as SenderState::RetransmitAu calls it.
std::vector<Datagram> replay_chunks(Loop& loop, const std::vector<uint8_t>& payload, uint32_t seq,
                                    uint64_t generation, bool key, bool tight,
                                    const std::vector<uint16_t>& indices, uint64_t* wireBytes = nullptr) {
  const UdpVideoChunkHeader h = base_header(seq, generation, key, payload.size());
  uint64_t datagrams = 0;
  const UdpSendOutcome r =
      send_udp_chunk_indices(loop.tx, loop.rxAddr, payload.data(), payload.size(), h, kMtu, tight,
                             indices.data(), static_cast<uint16_t>(indices.size()), wireBytes, &datagrams);
  if (r != UdpSendOutcome::Sent) return {};
  return loop.Drain();
}

std::string layout_name(bool tight) { return tight ? "tight" : "padded"; }

// ------------------------------------------------------------------------------------ geometry

void test_geometry() {
  std::printf("\n[geometry] stride / chunkCount / packetCount at the boundaries\n");
  const uint32_t mc = max_chunk();
  check(mc > 0 && mc < 4096, "maxChunk is within the receiver's stride limit", std::to_string(mc));

  struct Case { size_t payload; uint32_t stride; uint32_t chunks; uint32_t groups; };
  const Case tightCases[] = {
      {1, 1, 1, 1},
      {2, 2, 1, 1},
      {100, 100, 1, 1},
      {mc - 1, mc - 1, 1, 1},
      {mc, mc, 1, 1},
      {mc + 1, mc, 2, 1},
      {8 * mc, mc, 8, 1},
      {8 * mc + 1, mc, 9, 2},
      {20000, mc, (20000 + mc - 1) / mc, ((20000 + mc - 1) / mc + 7) / 8},
  };
  for (const Case& c : tightCases) {
    const UdpChunkGeometry g = udp_chunk_geometry(c.payload, kMtu, true);
    const std::string tag = "payload " + std::to_string(c.payload) + " tight";
    check(g.valid, tag + ": valid");
    check(g.chunkStride == c.stride, tag + ": stride " + std::to_string(c.stride), std::to_string(g.chunkStride));
    check(g.chunkCount == c.chunks, tag + ": chunkCount " + std::to_string(c.chunks), std::to_string(g.chunkCount));
    check(g.fecGroupCount == c.groups, tag + ": fecGroupCount " + std::to_string(c.groups), std::to_string(g.fecGroupCount));
    check(g.packetCount == c.chunks + c.groups, tag + ": packetCount = chunks + groups");
    check(g.parityBytes == static_cast<uint64_t>(c.groups) * c.stride, tag + ": parityBytes = groups * stride");
    // The padded layout: same chunkCount / packetCount, stride always the MTU chunk.
    const UdpChunkGeometry p = udp_chunk_geometry(c.payload, kMtu, false);
    check(p.valid && p.chunkStride == mc, tag + ": padded stride is the MTU chunk");
    check(p.chunkCount == g.chunkCount && p.fecGroupCount == g.fecGroupCount && p.packetCount == g.packetCount,
          tag + ": padded and tight agree on chunkCount / groups / packetCount");
    if (c.payload > mc) {
      check(p.parityBytes == g.parityBytes && p.chunkStride == g.chunkStride,
            tag + ": two chunks or more -- tight changes nothing");
    }
  }
  check(!udp_chunk_geometry(0, kMtu, true).valid, "payload 0 is not a frame");
  check(!udp_chunk_geometry(0, kMtu, false).valid, "payload 0 is not a frame (padded)");
  // The pacing budget is computed from the geometry both layouts share: a single-chunk frame is
  // never paced (chunkCount <= 8), a multi-chunk frame gets the same budget either way.
  UdpEgressConfig paced = egress_for(true, false);
  paced.pacePeakBps = 20000000;
  for (size_t payload : {size_t(1), size_t(mc), size_t(mc + 1), size_t(20000), size_t(200000)}) {
    const UdpChunkGeometry t = udp_chunk_geometry(payload, kMtu, true);
    const UdpChunkGeometry p = udp_chunk_geometry(payload, kMtu, false);
    const uint64_t bt = udp_pace_budget_us(paced, static_cast<size_t>(payload + t.parityBytes), t.packetCount, false);
    const uint64_t bp = udp_pace_budget_us(paced, static_cast<size_t>(payload + p.parityBytes), p.packetCount, false);
    if (t.chunkCount <= 8) {
      check(bt == 0 && bp == 0, "payload " + std::to_string(payload) + ": <= 8 chunks, not paced under either layout");
    } else {
      check(bt == bp && bt > 0, "payload " + std::to_string(payload) + ": the same pacing budget under either layout");
    }
  }
}

// ---------------------------------------------------------------------------------------- wire

void test_single_chunk_wire(Loop& loop) {
  std::printf("\n[wire] a single-chunk frame: data + one parity replica, both the frame's size\n");
  const uint32_t mc = max_chunk();
  for (bool interleaved : {false, true}) {
    for (size_t n : {size_t(1), size_t(2), size_t(100), size_t(mc - 1), size_t(mc)}) {
      const auto payload = make_payload(n, static_cast<uint32_t>(n));
      const std::string tag = "payload " + std::to_string(n) + (interleaved ? " interleaved" : " consecutive");
      SendPathStats stats{};
      const auto tight = send_frame(loop, payload, 100, 7, false, true, interleaved, &stats);
      check(tight.size() == 2, tag + ": tight = exactly 2 datagrams", std::to_string(tight.size()));
      if (tight.size() != 2) continue;
      const Datagram& d = tight[0];
      const Datagram& p = tight[1];
      check(!d.parity() && p.parity(), tag + ": data first, parity second");
      check(d.h.chunkStride == n && d.h.chunkSize == n && d.h.chunkCount == 1 && d.h.chunkIndex == 0 &&
                d.h.chunkOffset == 0 && d.h.payloadSize == n,
            tag + ": data header stride=size=payload, count 1, offset 0");
      check((d.h.flags & 0x2u) && (d.h.flags & 0x4u), tag + ": data is first and last chunk");
      check(d.body_size() == n && std::memcmp(d.body(), payload.data(), n) == 0, tag + ": data body is the frame");
      check(p.h.chunkStride == n && p.h.chunkSize == n && p.h.chunkCount == 1 && p.h.chunkIndex == 0 &&
                p.h.chunkOffset == 0,
            tag + ": parity header stride=size=payload (the receiver requires chunkSize == chunkStride)");
      check(((p.h.flags & 0x20u) != 0) == interleaved, tag + ": parity carries the interleaved bit as negotiated");
      check(p.body_size() == n && std::memcmp(p.body(), payload.data(), n) == 0,
            tag + ": the parity of one chunk is a replica of it, no padding");
      check(d.bytes.size() + p.bytes.size() == 2 * (kHeader + n), tag + ": wire bytes = 2 * (header + payload)");
      check(stats.datagrams == 2 && stats.parityDatagrams == 1 && stats.dataBytes == n && stats.parityBytes == n &&
                stats.headerBytes == 2 * kHeader,
            tag + ": stats count data=n parity=n header=2h",
            "data=" + std::to_string(stats.dataBytes) + " parity=" + std::to_string(stats.parityBytes));

      // The padded layout, for the same frame: what every host in the field sends today.
      SendPathStats pstats{};
      const auto padded = send_frame(loop, payload, 100, 7, false, false, interleaved, &pstats);
      check(padded.size() == 2, tag + ": padded = exactly 2 datagrams", std::to_string(padded.size()));
      if (padded.size() != 2) continue;
      check(padded[0].h.chunkStride == mc && padded[0].h.chunkSize == n && padded[0].h.chunkCount == 1,
            tag + ": padded data header stride = MTU chunk, size = payload");
      check(padded[1].h.chunkStride == mc && padded[1].h.chunkSize == mc && padded[1].body_size() == mc,
            tag + ": padded parity is a whole MTU chunk");
      bool zeroTail = true;
      for (uint32_t i = static_cast<uint32_t>(n); i < mc; ++i) zeroTail = zeroTail && padded[1].body()[i] == 0;
      check(std::memcmp(padded[1].body(), payload.data(), n) == 0 && zeroTail,
            tag + ": padded parity = the frame followed by zeros");
      check(pstats.parityBytes == mc && pstats.dataBytes == n, tag + ": padded stats count the padding");
      if (n == mc) {
        check(tight[0].bytes == padded[0].bytes && tight[1].bytes == padded[1].bytes,
              tag + ": at exactly maxChunk the two layouts are the same bytes");
      } else {
        check(padded[0].bytes.size() + padded[1].bytes.size() > tight[0].bytes.size() + tight[1].bytes.size(),
              tag + ": tight puts fewer bytes on the wire than padded",
              std::to_string(padded[0].bytes.size() + padded[1].bytes.size()) + " vs " +
                  std::to_string(tight[0].bytes.size() + tight[1].bytes.size()));
      }
    }
  }
}

void test_multi_chunk_unchanged(Loop& loop) {
  std::printf("\n[wire] frames of two chunks or more are byte for byte the padded layout\n");
  const uint32_t mc = max_chunk();
  for (bool interleaved : {false, true}) {
    for (size_t n : {size_t(mc + 1), size_t(2 * mc), size_t(8 * mc), size_t(8 * mc + 1), size_t(20000), size_t(123457)}) {
      const auto payload = make_payload(n, 99);
      const std::string tag = "payload " + std::to_string(n) + (interleaved ? " interleaved" : " consecutive");
      SendPathStats st{}, sp{};
      const auto tight = send_frame(loop, payload, 200, 3, true, true, interleaved, &st);
      const auto padded = send_frame(loop, payload, 200, 3, true, false, interleaved, &sp);
      const UdpChunkGeometry g = udp_chunk_geometry(n, kMtu, true);
      check(tight.size() == g.packetCount && padded.size() == g.packetCount,
            tag + ": both layouts send packetCount datagrams",
            std::to_string(tight.size()) + "/" + std::to_string(padded.size()) + " vs " + std::to_string(g.packetCount));
      bool same = tight.size() == padded.size();
      for (size_t i = 0; same && i < tight.size(); ++i) same = tight[i].bytes == padded[i].bytes;
      check(same, tag + ": every datagram identical between the layouts");
      check(st.dataBytes == sp.dataBytes && st.parityBytes == sp.parityBytes && st.headerBytes == sp.headerBytes &&
                st.datagrams == sp.datagrams && st.parityDatagrams == sp.parityDatagrams,
            tag + ": identical stats");
    }
  }
}

// ---------------------------------------------------------------------------------- receiver

const char* disp_name(UdpH264AssemblyDisposition d) {
  switch (d) {
    case UdpH264AssemblyDisposition::Ignored: return "Ignored";
    case UdpH264AssemblyDisposition::Partial: return "Partial";
    case UdpH264AssemblyDisposition::Completed: return "Completed";
    case UdpH264AssemblyDisposition::Malformed: return "Malformed";
    case UdpH264AssemblyDisposition::Dropped: return "Dropped";
    case UdpH264AssemblyDisposition::Queued: return "Queued";
  }
  return "?";
}

enum class Loss { None, Data0, Parity, Both, ParityFirst, DuplicateData0, GenerationSwitch };
const char* loss_name(Loss l) {
  switch (l) {
    case Loss::None: return "lossless";
    case Loss::Data0: return "data chunk 0 lost";
    case Loss::Parity: return "parity lost";
    case Loss::Both: return "data 0 and parity lost";
    case Loss::ParityFirst: return "parity before data";
    case Loss::DuplicateData0: return "data chunk 0 twice";
    case Loss::GenerationSwitch: return "generation switch";
  }
  return "?";
}

struct Outcome {
  bool completed = false;
  bool payloadOk = false;
  bool fecRecovered = false;
  bool nextCompleted = false;
  bool nextShowedGap = false;
  size_t pending = 0;
  std::string trace;  // every disposition in order: the receiver's verdicts under this layout
};

// Feeds a frame delivered beforehand (so a later gap is visible), then one frame's datagrams
// under a loss pattern, then a following frame, to a fresh shared assembler -- immediate delivery
// or the Windows viewer's in-order hold -- and records what it did.
Outcome run_scenario(const std::vector<Datagram>& prior, const std::vector<Datagram>& frame,
                     const std::vector<uint8_t>& payload, const std::vector<Datagram>& next,
                     const std::vector<uint8_t>& nextPayload, Loss loss, bool hold) {
  Outcome o;
  UdpH264FrameAssembler a;
  if (hold) a.ConfigureInOrderHold(120000, 8);
  uint64_t now = 5000000;
  const uint32_t seq = frame.empty() ? 0 : frame[0].h.seq;
  const uint32_t priorSeq = prior.empty() ? 0 : prior[0].h.seq;
  auto note = [&](const UdpH264AssemblyStepResult& r) {
    o.trace += disp_name(r.disposition);
    if (r.fecRecovered) o.trace += "+fec";
    if (r.droppedPreviousIncomplete) o.trace += "+gap";
    o.trace += " ";
    if (r.disposition == UdpH264AssemblyDisposition::Completed && r.frame.header.seq == priorSeq) return;
    if (r.fecRecovered) o.fecRecovered = true;
    if (r.disposition == UdpH264AssemblyDisposition::Completed) {
      if (r.frame.header.seq == seq) {
        o.completed = true;
        o.payloadOk = r.frame.payload == payload;
      } else {
        o.nextCompleted = true;
        o.nextShowedGap = r.expectedSeq != r.packetSeq || r.droppedPreviousIncomplete;
        if (r.frame.payload != nextPayload) o.trace += "(next payload wrong) ";
      }
    }
  };
  auto push = [&](const Datagram& d) {
    now += 100;
    note(a.PushDatagram(d.bytes.data(), d.bytes.size(), now));
  };
  auto drain = [&]() {
    if (!hold) return;
    now += 200000;  // past the hold
    UdpH264AssemblyStepResult r{};
    while (a.PopDelivery(now, true, &r)) {
      o.trace += "pop:";
      note(r);
      r = UdpH264AssemblyStepResult{};
    }
  };
  std::vector<const Datagram*> order;
  std::vector<const Datagram*> data, parity;
  for (const Datagram& d : frame) (d.parity() ? parity : data).push_back(&d);
  switch (loss) {
    case Loss::None:
    case Loss::GenerationSwitch:
      for (auto* d : data) order.push_back(d);
      for (auto* p : parity) order.push_back(p);
      break;
    case Loss::Data0:
      for (size_t i = 1; i < data.size(); ++i) order.push_back(data[i]);
      for (auto* p : parity) order.push_back(p);
      break;
    case Loss::Parity:
      for (auto* d : data) order.push_back(d);
      break;
    case Loss::Both:
      for (size_t i = 1; i < data.size(); ++i) order.push_back(data[i]);
      break;
    case Loss::ParityFirst:
      for (auto* p : parity) order.push_back(p);
      for (auto* d : data) order.push_back(d);
      break;
    case Loss::DuplicateData0:
      for (auto* d : data) order.push_back(d);
      if (!data.empty()) order.push_back(data[0]);
      for (auto* p : parity) order.push_back(p);
      break;
  }
  for (const Datagram& d : prior) push(d);
  drain();
  o.trace += "| ";
  for (auto* d : order) push(*d);
  drain();
  o.trace += "| ";
  for (const Datagram& d : next) push(d);
  drain();
  o.pending = a.PendingCount();
  return o;
}

void test_receiver_counter_examples(Loop& loop) {
  std::printf("\n[receiver] the shared assembler under both layouts, every boundary, every loss shape\n");
  const uint32_t mc = max_chunk();
  const Loss losses[] = {Loss::None, Loss::Data0, Loss::Parity, Loss::Both, Loss::ParityFirst,
                         Loss::DuplicateData0, Loss::GenerationSwitch};
  for (size_t n : {size_t(1), size_t(mc - 1), size_t(mc), size_t(mc + 1)}) {
    const auto payload = make_payload(n, 11);
    const auto nextPayload = make_payload(n, 12);
    for (bool hold : {false, true}) {
      for (Loss loss : losses) {
        const uint64_t nextGen = loss == Loss::GenerationSwitch ? 2u : 1u;
        Outcome out[2];
        for (int layout = 0; layout < 2; ++layout) {
          const bool tight = layout == 0;
          const auto prior = send_frame(loop, make_payload(n, 10), 499, 1, true, tight, true);
          const auto frame = send_frame(loop, payload, 500, 1, false, tight, true);
          const auto next = send_frame(loop, nextPayload, 501, nextGen, loss == Loss::GenerationSwitch, tight, true);
          out[layout] = run_scenario(prior, frame, payload, next, nextPayload, loss, hold);
        }
        const std::string tag = "payload " + std::to_string(n) + (hold ? " hold" : " immediate") + " / " + loss_name(loss);
        const Outcome& t = out[0];
        const Outcome& p = out[1];
        check(t.trace == p.trace && t.completed == p.completed && t.payloadOk == p.payloadOk &&
                  t.fecRecovered == p.fecRecovered && t.nextCompleted == p.nextCompleted &&
                  t.nextShowedGap == p.nextShowedGap && t.pending == p.pending,
              tag + ": the receiver's verdicts are the same under tight and padded",
              "tight[" + t.trace + "] padded[" + p.trace + "]");
        // What the tight layout must achieve (and, by the equality above, the padded one does too).
        switch (loss) {
          case Loss::None:
          case Loss::ParityFirst:
          case Loss::DuplicateData0:
          case Loss::GenerationSwitch:
            check(t.completed && t.payloadOk, tag + ": frame delivered byte-identical", t.trace);
            check(t.nextCompleted && !t.nextShowedGap, tag + ": next frame follows without a gap", t.trace);
            break;
          case Loss::Data0:
            check(t.completed && t.payloadOk && t.fecRecovered,
                  tag + ": the parity alone rebuilds the frame (fecRecovered)", t.trace);
            check(t.nextCompleted && !t.nextShowedGap, tag + ": next frame follows without a gap", t.trace);
            break;
          case Loss::Parity:
            check(t.completed && t.payloadOk && !t.fecRecovered, tag + ": the data alone completes the frame", t.trace);
            break;
          case Loss::Both:
            if (n <= mc) {
              // One chunk, both datagrams gone: nothing ever names this seq, so no assembly exists to
              // NACK for (the Codex point: OldestIncomplete cannot see it). The next frame shows the gap.
              check(!t.completed && t.nextCompleted && t.nextShowedGap,
                    tag + ": nothing to repair, the next frame shows the gap", t.trace);
              check(t.pending == 0, tag + ": no assembly is left behind", std::to_string(t.pending));
            } else {
              // Two chunks: the surviving chunk starts an assembly that stays incomplete.
              check(!t.completed, tag + ": unrepairable, not delivered", t.trace);
            }
            break;
        }
      }
    }
  }
}

void test_replay_geometry(Loop& loop) {
  std::printf("\n[replay] the NACK replay repeats the original datagram only with the original stride policy\n");
  const uint32_t mc = max_chunk();
  for (size_t n : {size_t(1), size_t(700), size_t(mc - 1), size_t(mc)}) {
    const auto payload = make_payload(n, 5);
    const std::string tag = "payload " + std::to_string(n);
    for (bool tight : {true, false}) {
      const auto original = send_frame(loop, payload, 900, 4, false, tight, true);
      uint64_t wire = 0;
      const auto replay = replay_chunks(loop, payload, 900, 4, false, tight, {0}, &wire);
      check(original.size() == 2 && replay.size() == 1, tag + " " + layout_name(tight) + ": one chunk replayed");
      if (original.size() != 2 || replay.size() != 1) continue;
      check(replay[0].bytes == original[0].bytes,
            tag + " " + layout_name(tight) + ": the replayed datagram is the original data datagram, byte for byte");
      check(wire == replay[0].bytes.size(), tag + " " + layout_name(tight) + ": replay wire bytes counted header included");
      // A replay after the assembly was given up (the late-repair path): it is a whole frame.
      UdpH264FrameAssembler fresh;
      const auto r = fresh.PushDatagram(replay[0].bytes.data(), replay[0].bytes.size(), 1);
      check(r.disposition == UdpH264AssemblyDisposition::Completed && r.frame.payload == payload,
            tag + " " + layout_name(tight) + ": a fresh assembler completes the frame from the replay alone");
    }
    // Why the cache carries the policy: in the viewer's in-order hold, a frame rebuilt from its
    // parity waits (Queued) for delivery. A replay with the OTHER stride policy then contradicts
    // the assembly's stride and the receiver discards the whole assembly -- the frame is lost by
    // its own repair. With the right policy the replay is a harmless duplicate.
    for (bool tight : {true, false}) {
      if (n == mc) continue;  // at exactly maxChunk both policies produce the same stride
      const auto original = send_frame(loop, payload, 901, 4, false, tight, true);
      const auto wrong = replay_chunks(loop, payload, 901, 4, false, !tight, {0});
      const auto right = replay_chunks(loop, payload, 901, 4, false, tight, {0});
      if (original.size() != 2 || wrong.size() != 1 || right.size() != 1) {
        check(false, tag + ": replay fixtures", "sizes " + std::to_string(original.size()));
        continue;
      }
      {
        UdpH264FrameAssembler a;
        a.ConfigureInOrderHold(120000, 8);
        const auto fromParity = a.PushDatagram(original[1].bytes.data(), original[1].bytes.size(), 1000);
        check(fromParity.disposition == UdpH264AssemblyDisposition::Queued && fromParity.fecRecovered,
              tag + " " + layout_name(tight) + ": parity alone rebuilds the held frame (Queued)", disp_name(fromParity.disposition));
        const auto bad = a.PushDatagram(wrong[0].bytes.data(), wrong[0].bytes.size(), 1100);
        check(bad.disposition == UdpH264AssemblyDisposition::Malformed && a.PendingCount() == 0,
              tag + " " + layout_name(tight) + ": a replay with the other stride policy destroys the held frame (Malformed, nothing pending)",
              std::string(disp_name(bad.disposition)) + " pending=" + std::to_string(a.PendingCount()));
        UdpH264AssemblyStepResult popped{};
        check(!a.PopDelivery(400000, true, &popped), tag + " " + layout_name(tight) + ": ...and nothing is ever delivered");
      }
      {
        UdpH264FrameAssembler a;
        a.ConfigureInOrderHold(120000, 8);
        (void)a.PushDatagram(original[1].bytes.data(), original[1].bytes.size(), 1000);
        const auto ok = a.PushDatagram(right[0].bytes.data(), right[0].bytes.size(), 1100);
        check(ok.disposition != UdpH264AssemblyDisposition::Malformed && a.PendingCount() == 1,
              tag + " " + layout_name(tight) + ": a replay with the original policy is a duplicate, the frame stays",
              disp_name(ok.disposition));
        UdpH264AssemblyStepResult popped{};
        const bool delivered = a.PopDelivery(400000, true, &popped);
        check(delivered && popped.disposition == UdpH264AssemblyDisposition::Completed && popped.frame.payload == payload,
              tag + " " + layout_name(tight) + ": ...and the frame is delivered byte-identical");
      }
    }
  }
  // Multi-chunk replay: unchanged geometry, so a two-chunk frame's chunk 1 replay matches its original.
  {
    const auto payload = make_payload(mc + 1, 6);
    const auto original = send_frame(loop, payload, 902, 4, false, true, true);
    const auto replay = replay_chunks(loop, payload, 902, 4, false, true, {1});
    check(original.size() == 3 && replay.size() == 1 && replay[0].bytes == original[1].bytes,
          "payload maxChunk+1: chunk 1 replay is the original chunk 1 datagram");
  }
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL WSAStartup\nudp_fec_single_chunk_test: FAIL\n");
    return 1;
  }
  Loop loop;
  if (!loop.Start()) {
    std::printf("FAIL loopback sockets\nudp_fec_single_chunk_test: FAIL\n");
    return 1;
  }
  std::printf("udp_fec_single_chunk_test: header=%u mtu=%u maxChunk=%u\n", kHeader, kMtu, max_chunk());

  test_geometry();
  test_single_chunk_wire(loop);
  test_multi_chunk_unchanged(loop);
  test_receiver_counter_examples(loop);
  test_replay_geometry(loop);

  loop.Stop();
  WSACleanup();
  if (gFailures != 0) {
    std::printf("udp_fec_single_chunk_test: FAIL (%d of %d checks)\n", gFailures, gChecks);
    return 1;
  }
  std::printf("udp_fec_single_chunk_test: PASS (%d checks)\n", gChecks);
  return 0;
}
