// fec-single-chunk-stride, before/after under loss: the same frames, the same seeded losses, the
// padded layout against the tight one -- what a receiver in the field gets out of each.
//
// A product-equivalent pipeline, deterministic end to end:
//   real H264Encoder (once per content class)         -> one fixed AU stream per class
//   real chunker: send_udp_chunks_timed                -> loopback UDP
//   real NACK cache + replay: SenderState::StoreAu / RetransmitAu (host_encoded_sender.cpp),
//     called the way the host's sender and reader threads call them, without the threads
//   seeded loss, decided per datagram IDENTITY (seq, chunk, parity, occurrence) -- so the two
//     layouts, whose frames have the same datagram count, lose exactly the same datagrams
//   real shared-session receive policy: SessionVideoPipeline (assembler + in-order hold + NACK
//     scheduler + sequence-gap rule -- what the Android session runs; the Windows viewer runs the
//     same assembler and scheduler) on a virtual clock
//   real H264Decoder on what it delivers.
//
// Under the SAME losses the receiver's every verdict is expected to be the same under both
// layouts (udp_fec_single_chunk_test shows this per frame; this is the stream-level check with
// NACK, hold and decode in the loop), so what differs is only the wire: fewer parity bytes. The
// gate (fec_overhead_plan 2026-09-28, "A gate"): lossless -> same delivered frames, same decoded
// pictures, fewer bytes; seeded loss / burst / lost NACKs -> no worse on delivered frames, gaps,
// keyframe requests, FEC repairs, and still fewer bytes.
//
// Build: remote60_udp_fec_single_chunk_loss_test (CMake). Prints one LOSSRUN line per run, then
// PASS/FAIL per check and "udp_fec_single_chunk_loss_test: PASS" with exit 0.
// pure-logic + loopback network + the Media Foundation H.264 codec (gpu-or-software); no product process.

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
#include <map>
#include <string>
#include <vector>

#include <mfapi.h>

#include "host_args.hpp"
#include "host_encoded_sender.hpp"
#include "host_net_io.hpp"
#include "mf_h264_codec.hpp"
#include "native_video_client_shared_core.hpp"
#include "native_video_transport.hpp"
#include "poc_protocol.hpp"
#include "session_video_pipeline.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mfplat.lib")

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
constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 360;
constexpr uint32_t kFps = 60;
constexpr uint32_t kFrames = 240;          // 4 s at 60 fps, per class
constexpr uint32_t kKeyint = 120;          // two IDRs per stream
constexpr uint64_t kFrameUs = 1000000 / kFps;

uint64_t fnv1a(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}
uint64_t fnv1a_u64(uint64_t h, uint64_t v) { return fnv1a(h, &v, sizeof(v)); }

// ------------------------------------------------------------------------------- content

enum class Content { StaticText, LowMotion, Video };
const char* content_name(Content c) {
  switch (c) {
    case Content::StaticText: return "static";
    case Content::LowMotion: return "lowmotion";
    case Content::Video: return "video";
  }
  return "?";
}

uint32_t xorshift(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

// A text-like page: light background, lines of dark glyph blocks. `frame` moves nothing for the
// static class; low motion adds a caret that blinks and one small block that creeps; video puts a
// band of fresh noise across the middle every frame (the worst case the encoder sees).
void paint(Content content, uint32_t frame, std::vector<uint8_t>& nv12) {
  uint8_t* y = nv12.data();
  uint8_t* uv = nv12.data() + static_cast<size_t>(kWidth) * kHeight;
  std::fill(y, y + static_cast<size_t>(kWidth) * kHeight, 220);
  std::fill(uv, uv + static_cast<size_t>(kWidth) * kHeight / 2, 128);
  uint32_t s = 0x1234567u;  // the page itself never changes between frames
  for (uint32_t line = 0; line < 22; ++line) {
    const uint32_t top = 12 + line * 15;
    for (uint32_t col = 0; col < 76; ++col) {
      const uint32_t r = xorshift(s);
      if ((r & 7u) == 0) continue;  // a space
      const uint32_t left = 10 + col * 8;
      for (uint32_t yy = top; yy < top + 10 && yy < kHeight; ++yy) {
        for (uint32_t xx = left; xx < left + 6 && xx < kWidth; ++xx) {
          // glyph-ish: a few pixels per cell stay light so it does not code as flat blocks
          const bool ink = ((r >> ((yy - top) % 8)) & 1u) || ((xx - left) == 2);
          if (ink) y[yy * kWidth + xx] = 30;
        }
      }
    }
  }
  if (content == Content::LowMotion) {
    if ((frame / 30) % 2 == 0) {  // a caret, blinking every half second
      for (uint32_t yy = 12; yy < 24; ++yy) for (uint32_t xx = 620; xx < 623; ++xx) y[yy * kWidth + xx] = 20;
    }
    const uint32_t bx = (frame * 2) % (kWidth - 16);  // a 16x16 block creeping along the bottom
    for (uint32_t yy = kHeight - 20; yy < kHeight - 4; ++yy)
      for (uint32_t xx = bx; xx < bx + 16; ++xx) y[yy * kWidth + xx] = 90;
  }
  if (content == Content::Video) {
    uint32_t state = 0x9e3779b9u ^ (frame + 1);
    for (uint32_t row = kHeight / 4; row < kHeight * 3 / 4; ++row) {
      for (uint32_t xx = 0; xx < kWidth; ++xx) {
        state = state * 1664525u + 1013904223u;
        y[row * kWidth + xx] = static_cast<uint8_t>(16 + (state >> 24) % 220);
      }
    }
    const uint32_t barX = (frame * 12) % (kWidth - 32);
    for (uint32_t row = 0; row < kHeight / 4; ++row)
      for (uint32_t xx = barX; xx < barX + 32; ++xx) y[row * kWidth + xx] = 235;
  }
}

struct Au {
  std::vector<uint8_t> bytes;
  bool key = false;
  int64_t timeHns = 0;
};

// What the encoder is asked for per class. The hardware encoder fills a CBR budget even for an
// unchanging picture (measured here: 1.5 Mb/s at 60 fps gave ~5 KB P frames of a still page), so
// the still and the low-motion classes are encoded at the small budgets a still screen actually
// gets on the wire -- the host's static-scene gating and its refresh frames put ~200-1000 byte
// frames there (fec_overhead_plan: e2e payload 185,863 over 873 parity datagrams). What matters
// to the chunker is the size distribution, printed per class as STREAM.
uint32_t bitrate_for(Content c) {
  switch (c) {
    case Content::StaticText: return 200000;
    case Content::LowMotion: return 400000;
    case Content::Video: return 1500000;
  }
  return 1500000;
}

// The fixed AU stream of a class: encoded once, used by every run of that class.
bool encode_stream(Content content, std::vector<Au>* out, std::string* backend) {
  H264Encoder enc;
  if (!enc.initialize(kWidth, kHeight, kFps, bitrate_for(content), kKeyint)) return false;
  *backend = enc.backend_name();
  std::vector<uint8_t> nv12(static_cast<size_t>(kWidth) * kHeight * 3 / 2, 128);
  const int64_t base = 10000000;  // 1 s, in 100 ns units
  for (uint32_t i = 0; i < kFrames; ++i) {
    paint(content, i, nv12);
    std::vector<H264AccessUnit> units;
    if (!enc.encode_frame(nv12, i == 0, base + static_cast<int64_t>(i) * 166667, &units)) return false;
    for (auto& u : units) out->push_back(Au{u.bytes, u.keyFrame, u.sampleTimeHns});
  }
  // An asynchronous encoder may still hold the last few inputs.
  for (uint32_t extra = 0; extra < 8 && out->size() < kFrames; ++extra) {
    std::vector<H264AccessUnit> units;
    paint(content, kFrames - 1, nv12);
    if (!enc.encode_frame(nv12, false, base + static_cast<int64_t>(kFrames + extra) * 166667, &units)) break;
    for (auto& u : units) out->push_back(Au{u.bytes, u.keyFrame, u.sampleTimeHns});
  }
  if (out->size() > kFrames) out->resize(kFrames);
  enc.shutdown();
  return !out->empty() && out->front().key;
}

// ------------------------------------------------------------------------------ the wire

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
    if (bind(tx, reinterpret_cast<sockaddr*>(&any), sizeof(any)) != 0) return false;
    if (bind(rx, reinterpret_cast<sockaddr*>(&any), sizeof(any)) != 0) return false;
    int len = sizeof(rxAddr);
    if (getsockname(rx, reinterpret_cast<sockaddr*>(&rxAddr), &len) != 0) return false;
    const DWORD timeoutMs = 5;
    (void)setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    const int rcvBuf = 8 * 1024 * 1024;
    (void)setsockopt(rx, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    return true;
  }
  void Stop() {
    if (tx != INVALID_SOCKET) closesocket(tx);
    if (rx != INVALID_SOCKET) closesocket(rx);
    tx = rx = INVALID_SOCKET;
  }
  // Exactly `count` datagrams (loopback delivers them all), or fewer after `maxMs`.
  std::vector<std::vector<uint8_t>> DrainCount(size_t count, uint32_t maxMs = 2000) {
    std::vector<std::vector<uint8_t>> out;
    std::vector<uint8_t> buf(4096);
    const auto start = std::chrono::steady_clock::now();
    while (out.size() < count) {
      const int n = recv(rx, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
      if (n > 0) out.emplace_back(buf.begin(), buf.begin() + n);
      if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(maxMs)) break;
    }
    return out;
  }
};

// ------------------------------------------------------------------------------- the run

enum class Policy { NackHold, LegacyImmediate };
const char* policy_name(Policy p) { return p == Policy::NackHold ? "nack+hold" : "legacy"; }

struct LossModel {
  const char* name = "none";
  uint32_t uniformPermille = 0;  // drop probability per datagram
  uint32_t burstEvery = 0;       // every N arrivals drop `burstLen` in a row
  uint32_t burstLen = 0;
  uint32_t nackLossPercent = 0;  // NACK packets lost on the way back (feedback loss)
};

struct RunResult {
  uint32_t sent = 0;
  uint32_t singleChunkFrames = 0;
  uint32_t delivered = 0;
  uint64_t deliveredHash = 1469598103934665603ull;
  uint32_t keyReq = 0;
  uint32_t disc = 0;
  uint32_t nacksSent = 0;
  uint32_t nacksServed = 0;
  uint32_t fec = 0;
  uint32_t giveUps = 0;
  uint32_t gaps = 0;
  uint32_t maxGap = 0;
  uint32_t dropped = 0;
  uint32_t droppedNacks = 0;
  uint64_t wireData = 0, wireParity = 0, wireHeader = 0, wireReplay = 0, wireDatagrams = 0;
  uint64_t wireTotal() const { return wireData + wireParity + wireHeader + wireReplay; }
  uint32_t decoded = 0;
  uint32_t decodeErrors = 0;
  uint64_t decodedHash = 1469598103934665603ull;
  std::string decoderBackend;
  // The NACK cache carries the stride policy: a replay asked of the host for a single-chunk AU it
  // still caches must repeat the original stride (0 = no single-chunk AU among the cached ones).
  uint32_t replayProbeSeq = 0;
  uint32_t replayProbePayload = 0;
  uint32_t replayProbeStride = 0;
};

RunResult run_stream(Loop& loop, const std::vector<Au>& aus, bool tight, Policy policy,
                     const LossModel& model, uint32_t seed) {
  RunResult r;
  const bool nack = policy == Policy::NackHold;

  // The host side, without its threads: the chunker and the NACK cache/replay.
  SenderState sender;
  sender.nackEnabled.store(nack, std::memory_order_relaxed);
  sender.pacePeakBps.store(0, std::memory_order_relaxed);
  UdpEgressConfig egress;
  egress.pacePeakBps = 0;
  egress.fecInterleaved = true;
  egress.fecSingleChunkTightStride = tight;

  // The receiver side: the shared session policy on a virtual clock.
  SessionVideoPipelineConfig cfg;
  cfg.nackEnabled = nack;
  cfg.holdUs = nack ? 120000 : 0;
  cfg.maxConcurrent = 8;
  std::vector<UdpVideoNackPacket> pendingNacks;
  uint32_t lastDeliveredSeq = 0;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> deliveredAus;  // seq, payload
  SessionVideoPipeline::Callbacks cb;
  cb.deliver = [&](UdpH264AssembledFrame&& f) {
    ++r.delivered;
    r.deliveredHash = fnv1a_u64(r.deliveredHash, f.header.seq);
    r.deliveredHash = fnv1a(r.deliveredHash, f.payload.data(), f.payload.size());
    if (lastDeliveredSeq != 0 && f.header.seq > lastDeliveredSeq + 1) {
      ++r.gaps;
      r.maxGap = std::max(r.maxGap, f.header.seq - lastDeliveredSeq - 1);
    }
    lastDeliveredSeq = f.header.seq;
    deliveredAus.emplace_back(f.header.seq, std::move(f.payload));
  };
  cb.requestKeyframe = [&] { ++r.keyReq; };
  cb.discontinuity = [&] { ++r.disc; };
  cb.sendNack = [&](const UdpVideoNackPacket& p) {
    ++r.nacksSent;
    pendingNacks.push_back(p);
  };
  SessionVideoPipeline pipeline(cfg, cb);

  // Loss by identity: the same datagram (same seq / chunk / parity, same occurrence) is lost or
  // kept regardless of which layout produced it.
  std::map<uint64_t, uint32_t> occurrences;
  uint64_t arrivals = 0;
  auto drop_decision = [&](const UdpVideoChunkHeader& h) {
    const uint64_t id = (static_cast<uint64_t>(h.seq) << 24) | (static_cast<uint64_t>(h.chunkIndex) << 1) |
                        ((h.flags & 0x10u) ? 1u : 0u);
    const uint32_t occ = ++occurrences[id];
    ++arrivals;
    if (model.uniformPermille > 0) {
      uint64_t x = fnv1a_u64(0xcbf29ce484222325ull ^ seed, id);
      x = fnv1a_u64(x, occ);
      if ((x % 1000u) < model.uniformPermille) return true;
    }
    if (model.burstEvery > 0) {
      if (((arrivals + static_cast<uint64_t>(seed) * 37u) % model.burstEvery) < model.burstLen) return true;
    }
    return false;
  };
  auto feed = [&](const std::vector<std::vector<uint8_t>>& datagrams, uint64_t nowUs, bool replay) {
    for (const auto& d : datagrams) {
      if (d.size() < sizeof(UdpVideoChunkHeader)) continue;
      UdpVideoChunkHeader h{};
      std::memcpy(&h, d.data(), sizeof(h));
      if (replay) r.wireReplay += d.size();
      if (drop_decision(h)) {
        ++r.dropped;
        continue;
      }
      pipeline.OnDatagram(d.data(), d.size(), nowUs);
    }
  };
  auto serve_nacks = [&](uint64_t nowUs) {
    std::vector<UdpVideoNackPacket> batch;
    batch.swap(pendingNacks);
    for (const UdpVideoNackPacket& p : batch) {
      if (model.nackLossPercent > 0) {
        const uint64_t x = fnv1a_u64(fnv1a_u64(0x100u ^ seed, p.seq), r.nacksSent + r.droppedNacks);
        if ((x % 100u) < model.nackLossPercent) {
          ++r.droppedNacks;
          continue;
        }
      }
      ++r.nacksServed;
      const uint16_t count = std::min<uint16_t>(p.missingCount, kUdpVideoNackMaxMissing);
      // What the host's reader thread does with a NACK (host_startup_control.cpp).
      sender.RetransmitAu(loop.tx, loop.rxAddr, p.streamGeneration, p.seq, p.missing, count);
      // The replay is at most `count` datagrams; RetransmitAu may also send nothing (budget, miss).
      const auto replays = loop.DrainCount(count, 30);
      feed(replays, nowUs, true);
    }
  };

  uint64_t now = 1000000;
  for (size_t i = 0; i < aus.size(); ++i) {
    const Au& au = aus[i];
    const uint32_t seq = static_cast<uint32_t>(i + 1);
    UdpVideoChunkHeader h{};
    h.magic = kMagic;
    h.kind = static_cast<uint16_t>(UdpPacketKind::VideoChunk);
    h.size = static_cast<uint16_t>(sizeof(UdpVideoChunkHeader));
    h.seq = seq;
    h.codec = static_cast<uint16_t>(UdpCodec::H264);
    h.flags = au.key ? 0x1u : 0u;
    h.width = kWidth;
    h.height = kHeight;
    h.payloadSize = static_cast<uint32_t>(au.bytes.size());
    h.streamGeneration = 1;
    h.captureQpcUs = now;
    h.encodeStartQpcUs = now;
    h.encodeEndQpcUs = now + 2000;
    h.sendQpcUs = now + 2100;
    SendPathStats st{};
    const UdpSendOutcome outcome = send_udp_chunks_timed(loop.tx, loop.rxAddr, au.bytes.data(), au.bytes.size(),
                                                         h, kMtu, &st, nullptr, 0, egress);
    if (outcome != UdpSendOutcome::Sent) {
      std::printf("  send failed at seq %u\n", seq);
      break;
    }
    // What the sender thread does right after a successful send (host_encoded_sender.cpp).
    sender.StoreAu(1, seq, h, kMtu, egress.fecSingleChunkTightStride, au.bytes.data(), au.bytes.size());
    ++r.sent;
    if (st.payloadChunkCount == 2) ++r.singleChunkFrames;  // one data + one parity datagram
    r.wireData += st.dataBytes;
    r.wireParity += st.parityBytes;
    r.wireHeader += st.headerBytes;
    r.wireDatagrams += st.datagrams;
    const auto datagrams = loop.DrainCount(static_cast<size_t>(st.datagrams));
    if (datagrams.size() != st.datagrams) {
      std::printf("  loopback lost datagrams at seq %u (%zu of %llu)\n", seq, datagrams.size(),
                  static_cast<unsigned long long>(st.datagrams));
    }
    feed(datagrams, now, false);
    serve_nacks(now);
    // To the next frame, one millisecond at a time: the hold, the NACK graces and rounds run on
    // this clock exactly as they would on the receiver's.
    for (uint32_t t = 0; t < 16; ++t) {
      now += 1000;
      pipeline.OnTick(now);
      serve_nacks(now);
    }
    now += kFrameUs - 16000;
  }
  // The tail: let the hold expire and the last NACK rounds play out.
  for (uint32_t t = 0; t < 400; ++t) {
    now += 1000;
    pipeline.OnTick(now);
    serve_nacks(now);
  }
  r.fec = static_cast<uint32_t>(pipeline.stats().fecRecoveredChunks);
  r.giveUps = static_cast<uint32_t>(pipeline.stats().stuckHeadGiveUps);

  // Ask the host's cache to replay chunk 0 of the newest single-chunk AU it still holds, the way
  // its reader thread answers a NACK: the replayed datagram must carry the stride that AU was first
  // sent with (payload size under tight, the MTU chunk under padded).
  if (nack) {
    const uint32_t maxChunk = clamp_udp_mtu(kMtu) - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
    const size_t oldestCached = aus.size() > SenderState::kNackCacheMaxAus ? aus.size() - SenderState::kNackCacheMaxAus : 0;
    for (size_t i = aus.size(); i > oldestCached; --i) {
      if (aus[i - 1].bytes.size() > maxChunk) continue;
      r.replayProbeSeq = static_cast<uint32_t>(i);
      r.replayProbePayload = static_cast<uint32_t>(aus[i - 1].bytes.size());
      break;
    }
    if (r.replayProbeSeq != 0) {
      (void)loop.DrainCount(64, 5);  // nothing should be pending; clear anything that is
      const uint16_t index = 0;
      sender.RetransmitAu(loop.tx, loop.rxAddr, 1, r.replayProbeSeq, &index, 1);
      const auto replay = loop.DrainCount(1, 200);
      if (replay.size() == 1 && replay[0].size() >= sizeof(UdpVideoChunkHeader)) {
        UdpVideoChunkHeader h{};
        std::memcpy(&h, replay[0].data(), sizeof(h));
        if (h.seq == r.replayProbeSeq && (h.flags & 0x10u) == 0) r.replayProbeStride = h.chunkStride;
      }
    }
  }

  // What the delivered stream decodes to.
  H264Decoder dec;
  if (dec.initialize(kWidth, kHeight, kFps)) {
    r.decoderBackend = dec.backend_name();
    for (const auto& d : deliveredAus) {
      const Au& au = aus[d.first - 1];
      std::vector<DecodedFrameNv12> out;
      bool overflow = false;
      if (!dec.decode_access_unit(d.second, au.key, au.timeHns, &out, &overflow)) ++r.decodeErrors;
      for (const auto& f : out) {
        ++r.decoded;
        r.decodedHash = fnv1a_u64(r.decodedHash, static_cast<uint64_t>(f.sampleTimeHns));
        // The display aperture only: the coded plane's padding rows (360 -> 368) are not content
        // and are not stable between two decodes of the same stream.
        const size_t stride = f.width;
        const size_t rows = std::min<size_t>(f.visibleHeight, f.height);
        const size_t cols = std::min<size_t>(f.visibleWidth, f.width);
        for (size_t y = 0; y < rows; ++y) {
          const size_t at = (static_cast<size_t>(f.visibleTop) + y) * stride + f.visibleLeft;
          if (at + cols <= f.bytes.size()) r.decodedHash = fnv1a(r.decodedHash, f.bytes.data() + at, cols);
        }
        const size_t uvBase = static_cast<size_t>(f.height) * stride;
        for (size_t y = 0; y < rows / 2; ++y) {
          const size_t at = uvBase + (static_cast<size_t>(f.visibleTop) / 2 + y) * stride + (f.visibleLeft & ~1u);
          if (at + cols <= f.bytes.size()) r.decodedHash = fnv1a(r.decodedHash, f.bytes.data() + at, cols);
        }
      }
    }
    dec.shutdown();
  } else {
    r.decodeErrors = 0xFFFFu;
  }
  return r;
}

void print_run(const char* cls, bool tight, Policy policy, const LossModel& model, uint32_t seed, const RunResult& r) {
  std::printf("LOSSRUN class=%s layout=%s policy=%s model=%s seed=%u sent=%u single=%u delivered=%u keyReq=%u disc=%u"
              " nack=%u served=%u nackLost=%u fec=%u giveUp=%u gaps=%u maxGap=%u dropped=%u"
              " wireData=%llu wireParity=%llu wireHdr=%llu wireReplay=%llu wireTotal=%llu datagrams=%llu"
              " decoded=%u decErr=%u dhash=%016llx phash=%016llx\n",
              cls, tight ? "tight" : "padded", policy_name(policy), model.name, seed, r.sent, r.singleChunkFrames,
              r.delivered, r.keyReq, r.disc, r.nacksSent, r.nacksServed, r.droppedNacks, r.fec, r.giveUps, r.gaps,
              r.maxGap, r.dropped, static_cast<unsigned long long>(r.wireData),
              static_cast<unsigned long long>(r.wireParity), static_cast<unsigned long long>(r.wireHeader),
              static_cast<unsigned long long>(r.wireReplay), static_cast<unsigned long long>(r.wireTotal()),
              static_cast<unsigned long long>(r.wireDatagrams), r.decoded, r.decodeErrors,
              static_cast<unsigned long long>(r.decodedHash), static_cast<unsigned long long>(r.deliveredHash));
}

std::string u(uint64_t v) { return std::to_string(v); }

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::printf("FAIL WSAStartup\nudp_fec_single_chunk_loss_test: FAIL\n");
    return 1;
  }
  if (FAILED(MFStartup(MF_VERSION))) {
    std::printf("FAIL MFStartup\nudp_fec_single_chunk_loss_test: FAIL\n");
    return 1;
  }
  Loop loop;
  if (!loop.Start()) {
    std::printf("FAIL loopback sockets\nudp_fec_single_chunk_loss_test: FAIL\n");
    return 1;
  }
  const uint32_t maxChunk = clamp_udp_mtu(kMtu) - static_cast<uint32_t>(sizeof(UdpVideoChunkHeader));
  std::printf("udp_fec_single_chunk_loss_test: %ux%u @%u fps, %u frames per class, keyint %u, mtu %u (maxChunk %u)\n",
              kWidth, kHeight, kFps, kFrames, kKeyint, kMtu, maxChunk);

  const LossModel models[] = {
      {"none", 0, 0, 0, 0},
      {"uniform1pct", 10, 0, 0, 0},
      {"uniform3pct", 30, 0, 0, 0},
      {"burst3per400", 0, 400, 3, 0},
      {"uniform1pct+nackloss50", 10, 0, 0, 50},
  };

  for (Content content : {Content::StaticText, Content::LowMotion, Content::Video}) {
    std::vector<Au> aus;
    std::string backend;
    const bool encoded = encode_stream(content, &aus, &backend);
    const char* cls = content_name(content);
    check(encoded && aus.size() >= kFrames - 8, std::string(cls) + ": encoded a fixed AU stream",
          "aus=" + u(aus.size()) + " backend=" + backend);
    if (!encoded || aus.empty()) continue;
    size_t single = 0, upTo4 = 0, upTo8 = 0, over8 = 0, bytes = 0, keys = 0, minAu = SIZE_MAX, maxAu = 0;
    for (const Au& a : aus) {
      const size_t chunks = (a.bytes.size() + maxChunk - 1) / maxChunk;
      if (chunks <= 1) ++single;
      else if (chunks <= 4) ++upTo4;
      else if (chunks <= 8) ++upTo8;
      else ++over8;
      bytes += a.bytes.size();
      minAu = std::min(minAu, a.bytes.size());
      maxAu = std::max(maxAu, a.bytes.size());
      if (a.key) ++keys;
    }
    std::printf("STREAM class=%s encoder=%s bitrate=%u aus=%zu keys=%zu chunks1=%zu chunks2to4=%zu chunks5to8=%zu"
                " chunksOver8=%zu payloadBytes=%zu avgAu=%zu minAu=%zu maxAu=%zu\n",
                cls, backend.c_str(), bitrate_for(content), aus.size(), keys, single, upTo4, upTo8, over8, bytes,
                bytes / aus.size(), minAu, maxAu);
    if (content != Content::Video) {
      check(single * 2 > aus.size(), std::string(cls) + ": most AUs fit one datagram (the case this work is about)",
            u(single) + " of " + u(aus.size()));
    }

    for (Policy policy : {Policy::NackHold, Policy::LegacyImmediate}) {
      for (const LossModel& model : models) {
        const std::vector<uint32_t> seeds =
            (std::string(model.name) == "uniform1pct") ? std::vector<uint32_t>{1, 2, 3} : std::vector<uint32_t>{1};
        for (uint32_t seed : seeds) {
          if (policy == Policy::LegacyImmediate && model.nackLossPercent > 0) continue;  // no NACKs to lose
          const RunResult padded = run_stream(loop, aus, false, policy, model, seed);
          print_run(cls, false, policy, model, seed, padded);
          const RunResult tight = run_stream(loop, aus, true, policy, model, seed);
          print_run(cls, true, policy, model, seed, tight);
          const std::string tag = std::string(cls) + " " + policy_name(policy) + " " + model.name + " seed " + u(seed);

          check(tight.sent == padded.sent && tight.wireDatagrams == padded.wireDatagrams,
                tag + ": same frames, same datagram count under both layouts");
          check(tight.dropped == padded.dropped, tag + ": the same datagrams were lost",
                u(tight.dropped) + " vs " + u(padded.dropped));
          check(tight.delivered == padded.delivered && tight.deliveredHash == padded.deliveredHash,
                tag + ": the receiver delivered the same frames (count + bytes)",
                u(tight.delivered) + " vs " + u(padded.delivered));
          check(tight.keyReq == padded.keyReq && tight.disc == padded.disc,
                tag + ": same keyframe requests and discontinuities",
                "keyReq " + u(tight.keyReq) + "/" + u(padded.keyReq) + " disc " + u(tight.disc) + "/" + u(padded.disc));
          check(tight.gaps == padded.gaps && tight.maxGap == padded.maxGap,
                tag + ": same frame gaps", "gaps " + u(tight.gaps) + "/" + u(padded.gaps) + " maxGap " +
                                               u(tight.maxGap) + "/" + u(padded.maxGap));
          check(tight.nacksSent == padded.nacksSent && tight.fec == padded.fec && tight.giveUps == padded.giveUps,
                tag + ": same NACKs, FEC repairs and give-ups",
                "nack " + u(tight.nacksSent) + "/" + u(padded.nacksSent) + " fec " + u(tight.fec) + "/" +
                    u(padded.fec) + " giveUp " + u(tight.giveUps) + "/" + u(padded.giveUps));
          check(tight.decoded == padded.decoded && tight.decodedHash == padded.decodedHash &&
                    tight.decodeErrors == padded.decodeErrors,
                tag + ": the delivered stream decodes to the same pictures",
                "decoded " + u(tight.decoded) + "/" + u(padded.decoded) + " err " + u(tight.decodeErrors) + "/" +
                    u(padded.decodeErrors));
          check(tight.wireData == padded.wireData && tight.wireHeader == padded.wireHeader,
                tag + ": data and header bytes unchanged");
          if (tight.singleChunkFrames > 0) {
            check(tight.wireParity < padded.wireParity && tight.wireTotal() < padded.wireTotal(),
                  tag + ": fewer parity bytes, fewer wire bytes",
                  "parity " + u(tight.wireParity) + " vs " + u(padded.wireParity) + ", total " +
                      u(tight.wireTotal()) + " vs " + u(padded.wireTotal()));
          } else {
            check(tight.wireParity == padded.wireParity && tight.wireTotal() == padded.wireTotal(),
                  tag + ": no single-chunk frame, identical wire");
          }
          if (policy == Policy::NackHold && tight.replayProbeSeq != 0 && padded.replayProbeSeq != 0) {
            check(tight.replayProbeStride == tight.replayProbePayload,
                  tag + ": the host cache replays a single-chunk AU with the tight stride it was sent with",
                  "seq " + u(tight.replayProbeSeq) + " stride " + u(tight.replayProbeStride) + " payload " +
                      u(tight.replayProbePayload));
            check(padded.replayProbeStride == maxChunk,
                  tag + ": the host cache replays a single-chunk AU with the padded stride it was sent with",
                  "seq " + u(padded.replayProbeSeq) + " stride " + u(padded.replayProbeStride));
          }
          if (model.uniformPermille == 0 && model.burstEvery == 0) {
            check(tight.delivered == tight.sent && tight.gaps == 0 && tight.disc == 0 && tight.keyReq == 0,
                  tag + ": lossless -> every frame delivered, no gap, no request",
                  "delivered " + u(tight.delivered) + " of " + u(tight.sent) + " keyReq " + u(tight.keyReq));
            check(tight.decodeErrors == 0 && tight.decoded + 4 >= tight.delivered,
                  tag + ": lossless -> every delivered AU decodes",
                  "decoded " + u(tight.decoded) + " of " + u(tight.delivered) + " errors " + u(tight.decodeErrors));
          } else if (policy == Policy::NackHold && model.uniformPermille == 10 && model.nackLossPercent == 0) {
            // 1% loss with FEC + NACK: the stream is expected to survive with few gaps at all.
            check(tight.delivered + 8 >= tight.sent, tag + ": 1% loss -> almost every frame delivered",
                  u(tight.delivered) + " of " + u(tight.sent) + " fec " + u(tight.fec) + " nack " + u(tight.nacksSent));
          }
        }
      }
    }
  }

  loop.Stop();
  MFShutdown();
  WSACleanup();
  if (gFailures != 0) {
    std::printf("udp_fec_single_chunk_loss_test: FAIL (%d of %d checks)\n", gFailures, gChecks);
    return 1;
  }
  std::printf("udp_fec_single_chunk_loss_test: PASS (%d checks)\n", gChecks);
  return 0;
}
