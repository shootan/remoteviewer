// clip-image direction A: the transfer state machines and the bulk pacer (plan r1 ⑵⑧, r2 8-1..8-4, §2).
//
// Pure where it can be: ClipImageReceiver / ClipImageSender take every time as an argument, so the
// stall and budget rules are checked without waiting. The pacer is measured on a real thread and a
// real high-resolution timer, against a send function that only timestamps.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "bulk_pacer.hpp"
#include "clip_image_transfer.hpp"

using namespace remote60::native_poc;

namespace {
int g_failed = 0;
int g_checks = 0;
void check(const std::string& what, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

ClipImageOffer offer(uint32_t w, uint32_t h, uint32_t png, uint32_t text = 0) {
  ClipImageOffer o;
  o.transferId = 0x1122334455667788ull;
  o.formats = kClipImageFormatPng | (text ? kClipImageFormatText : 0);
  o.pngBytes = png;
  o.textUtf16 = text;
  o.width = w;
  o.height = h;
  return o;
}

ClipBulkChunkHeader chunk_for(const ClipBulkPullMessage& p) {
  ClipBulkChunkHeader h{};
  h.header.type = static_cast<uint16_t>(MessageType::ClipBulkChunk);
  h.header.size = sizeof(h);
  h.transferId = p.transferId;
  h.epochTag = p.epochTag;
  h.bulkGen = p.bulkGen;
  h.offset = p.offset;
  h.len = p.len;
  return h;
}

std::vector<uint8_t> data_dgram(uint32_t stream, uint32_t seq, uint16_t frag, size_t bytes = 1100) {
  std::vector<uint8_t> d(sizeof(UdpControlChunkHeader) + bytes, 0);
  UdpControlChunkHeader h{};
  h.streamId = stream;
  h.messageSeq = seq;
  h.fragIndex = frag;
  h.fragCount = 16;
  std::memcpy(d.data(), &h, sizeof(h));
  return d;
}
}  // namespace

int main() {
  const uint64_t MiB = 1024ull * 1024ull;
  // ------------------------------------------------------------------ offer verdicts (8-2, 9)
  {
    ClipImageReceiver r(0xABCD1234u);
    uint64_t t = 1000000;
    check("disabled host: Disabled", r.OnOffer(offer(10, 10, 100), 7, false, 1, t).verdict == ClipImageVerdict::Disabled);
    ClipImageOffer bad = offer(10, 10, 100);
    bad.transferId = 0;
    check("transferId 0: BadRequest", r.OnOffer(bad, 7, true, 1, t).verdict == ClipImageVerdict::BadRequest);
    bad = offer(10, 10, 100);
    bad.formats = kClipImageFormatText;
    check("no PNG in the formats: BadRequest", r.OnOffer(bad, 7, true, 1, t).verdict == ClipImageVerdict::BadRequest);
    bad = offer(10, 10, 100, 0);
    bad.formats |= kClipImageFormatText;
    check("text flag without text units: BadRequest", r.OnOffer(bad, 7, true, 1, t).verdict == ClipImageVerdict::BadRequest);
    bad = offer(10, 10, 100, 5);
    bad.formats = kClipImageFormatPng;
    check("text units without the text flag: BadRequest", r.OnOffer(bad, 7, true, 1, t).verdict == ClipImageVerdict::BadRequest);
    bad = offer(10, 10, 100);
    bad.formats |= 0x80;
    check("an unknown format bit: BadRequest", r.OnOffer(bad, 7, true, 1, t).verdict == ClipImageVerdict::BadRequest);
    check("pngBytes under a PNG signature: BadRequest", r.OnOffer(offer(10, 10, 7), 7, true, 1, t).verdict == ClipImageVerdict::BadRequest);
    check("zero width: BadDims", r.OnOffer(offer(0, 10, 100), 7, true, 1, t).verdict == ClipImageVerdict::BadDims);
    check("8193 x 1: TooLarge", r.OnOffer(offer(8193, 1, 100), 7, true, 1, t).verdict == ClipImageVerdict::TooLarge);
    check("8192 x 2049 (decoded over 64 MiB): TooLarge",
          r.OnOffer(offer(8192, 2049, 100), 7, true, 1, t).verdict == ClipImageVerdict::TooLarge);
    check("8192 x 4096 highly compressed: refused on its decoded size, not its bytes",
          r.OnOffer(offer(8192, 4096, 30000), 7, true, 1, t).verdict == ClipImageVerdict::TooLarge);
    check("PNG 16 MiB + 1: TooLarge",
          r.OnOffer(offer(100, 100, static_cast<uint32_t>(16 * MiB + 1)), 7, true, 1, t).verdict == ClipImageVerdict::TooLarge);
    check("text over 512 Ki units: TooLarge",
          r.OnOffer(offer(100, 100, 1000, 512 * 1024 + 1), 7, true, 1, t).verdict == ClipImageVerdict::TooLarge);
    check("nothing was accepted by any refusal", !r.Active() && r.heldBytes() == 0);
    // The boundary that passes: 8192 x 2048 with a 16 MiB PNG = 32 + 1 + 64 + 1 MiB of peak.
    const auto ok = r.OnOffer(offer(8192, 2048, static_cast<uint32_t>(16 * MiB)), 7, true, 1, t);
    check("8192 x 2048, PNG 16 MiB: Accept (receiver peak 98 MiB <= 100)", ok.verdict == ClipImageVerdict::Accept);
    check("epochTag = process random << 32 | session epoch", ok.epochTag == ((0xABCD1234ull << 32) | 7));
    check("a bulk generation was issued (never 0 in its low bits)", (ok.bulkGen & kBulkGenMask) != 0);
    check("the declared total, and only that, is allocated", r.package().size() == 16 * MiB);
    check("one transfer per session: a second offer is Busy",
          r.OnOffer(offer(10, 10, 100), 7, true, 1, t).verdict == ClipImageVerdict::Busy);
  }
  // ------------------------------------------------------------------ pulls and chunks (8-3)
  {
    ClipImageReceiver r(1, 2);
    const uint32_t total = 3 * kClipImageChunkBytes + 100;  // 3 full chunks and a short one
    uint64_t t = 1000000;
    const auto acc = r.OnOffer(offer(64, 64, total), 9, true, 42, t);
    std::vector<ClipBulkPullMessage> pulls;
    ClipBulkPullMessage p{};
    while (r.NextPull(&p)) pulls.push_back(p);
    check("the pull window keeps 2 outstanding", pulls.size() == 2 && r.outstandingPulls() == 2);
    check("opening pulls carry no trigger", pulls[0].triggerOffset == 0xFFFFFFFFu);
    check("pulls are 16 KiB each, in order", pulls[0].offset == 0 && pulls[1].offset == kClipImageChunkBytes &&
                                                 pulls[0].len == kClipImageChunkBytes);
    std::vector<uint8_t> bytes(kClipImageChunkBytes, 0x5A);
    auto h = chunk_for(pulls[0]);
    auto tamper = [&](auto mutate, const char* what) {
      ClipBulkChunkHeader x = h;
      size_t len = x.len;
      mutate(x, len);
      const auto res = r.OnChunk(x, bytes.data(), len, t);
      check(std::string("dropped, and nothing changes: ") + what,
            res == ClipImageReceiver::ChunkResult::Dropped && r.received() == 0 && r.state() == ClipImageState::Pulling);
    };
    tamper([](ClipBulkChunkHeader& x, size_t&) { x.transferId ^= 1; }, "another transferId");
    tamper([](ClipBulkChunkHeader& x, size_t&) { x.epochTag ^= 1ull << 40; }, "an old epochTag (host restarted)");
    tamper([](ClipBulkChunkHeader& x, size_t&) { x.epochTag ^= 1; }, "an old session epoch");
    tamper([](ClipBulkChunkHeader& x, size_t&) { x.bulkGen ^= 1; }, "an old bulk generation");
    tamper([](ClipBulkChunkHeader& x, size_t&) { x.offset = 3 * kClipImageChunkBytes; }, "a range nobody asked for");
    tamper([](ClipBulkChunkHeader& x, size_t&) { x.offset += 1; }, "a shifted offset");
    tamper([](ClipBulkChunkHeader& x, size_t& n) { x.len -= 1; n -= 1; }, "a shorter length than asked");
    tamper([](ClipBulkChunkHeader&, size_t& n) { n -= 1; }, "data shorter than its header says");
    tamper([](ClipBulkChunkHeader& x, size_t&) { x.len = 0; }, "zero length");
    check("the good chunk is accepted", r.OnChunk(h, bytes.data(), h.len, t) == ClipImageReceiver::ChunkResult::Accepted &&
                                            r.received() == kClipImageChunkBytes);
    check("the same chunk twice is dropped (answered once)",
          r.OnChunk(h, bytes.data(), h.len, t) == ClipImageReceiver::ChunkResult::Dropped && r.received() == kClipImageChunkBytes);
    ClipBulkPullMessage next{};
    check("a completed chunk releases one pull, naming it as the trigger",
          r.NextPull(&next, h.offset) && next.triggerOffset == 0 && next.offset == 2 * kClipImageChunkBytes &&
              next.bytesReceived == kClipImageChunkBytes);
    check("status reports progress for the current id",
          r.Status(0x1122334455667788ull).bytesReceived == kClipImageChunkBytes &&
              r.Status(0x1122334455667788ull).state == static_cast<uint8_t>(ClipImageState::Pulling));
    check("status for another id is Unknown", r.Status(99).state == static_cast<uint8_t>(ClipImageState::Unknown));
    auto other = r.Cancel(99, acc.epochTag, ClipImageReason::User);
    check("a cancel naming another id answers Unknown and leaves the transfer alone",
          other.state == static_cast<uint8_t>(ClipImageState::Unknown) && r.state() == ClipImageState::Pulling);
    (void)r.Cancel(0x1122334455667788ull, acc.epochTag ^ 1, ClipImageReason::User);
    check("a cancel with a stale epochTag does not cancel", r.state() == ClipImageState::Pulling);
    // Finish the rest.
    auto h1 = chunk_for(pulls[1]);
    r.OnChunk(h1, bytes.data(), h1.len, t);
    auto h2 = chunk_for(next);
    r.OnChunk(h2, bytes.data(), h2.len, t);
    ClipBulkPullMessage last{};
    r.NextPull(&last, h2.offset);
    check("the last pull asks for exactly the remainder", last.len == 100 && last.offset == 3 * kClipImageChunkBytes);
    auto h3 = chunk_for(last);
    check("the last chunk completes the package",
          r.OnChunk(h3, bytes.data(), h3.len, t) == ClipImageReceiver::ChunkResult::Complete &&
              r.state() == ClipImageState::Verifying && r.localSeqAtAccept() == 42);
    check("nothing more is pulled once complete", !r.NextPull(&last));
    check("chunks after completion are dropped", r.OnChunk(h3, bytes.data(), h3.len, t) == ClipImageReceiver::ChunkResult::Dropped);
    // A cancel while verifying/publishing is honoured by the worker, not by tearing the decode down.
    (void)r.Cancel(0x1122334455667788ull, acc.epochTag, ClipImageReason::User);
    check("cancel while verifying marks it", r.CancelRequested() && r.state() == ClipImageState::Verifying);
    r.BeginPublishing();
    r.Finish(ClipImageState::Failed, ClipImageReason::DecodeFailed);
    check("...and a failure then reads as the cancel", r.state() == ClipImageState::Cancelled && r.heldBytes() == 0);
    check("a new offer is accepted once the old one ended",
          r.OnOffer(offer(10, 10, 100), 9, true, 1, t).verdict == ClipImageVerdict::Accept);
  }
  // ------------------------------------------------------------------ published beats a late cancel
  {
    ClipImageReceiver r(1);
    const auto acc = r.OnOffer(offer(8, 8, 100), 1, true, 5, 0);
    ClipBulkPullMessage p{};
    r.NextPull(&p);
    std::vector<uint8_t> b(100, 1);
    auto h = chunk_for(p);
    r.OnChunk(h, b.data(), 100, 0);
    r.BeginPublishing();
    (void)r.Cancel(0x1122334455667788ull, acc.epochTag, ClipImageReason::User);
    r.Finish(ClipImageState::Published, ClipImageReason::None);
    check("a publish that already happened is reported as published", r.state() == ClipImageState::Published);
  }
  // ------------------------------------------------------------------ stall, budget, session (8-1, 7)
  {
    ClipImageReceiver r(1);
    const uint64_t t0 = 5000000;
    r.OnOffer(offer(64, 64, 1000), 1, true, 1, t0);
    check("29.9 s without a chunk is not yet a stall", !r.Tick(t0 + 29900000) && r.state() == ClipImageState::Pulling);
    check("30 s without a chunk: failed(stalled)", r.Tick(t0 + 30000000) && r.state() == ClipImageState::Failed &&
                                                       r.reason() == ClipImageReason::Stalled && r.heldBytes() == 0);
    ClipImageReceiver b(1);
    b.OnOffer(offer(64, 64, 40000), 1, true, 1, 0);
    ClipBulkPullMessage p{};
    b.NextPull(&p);
    std::vector<uint8_t> d(p.len, 0);
    const uint64_t budget = clip_image_total_timeout_us(40000);
    check("budget = bits / 128 kbps + 30 s", budget == 40000ull * 8 * 1000000 / 128000 + 30000000);
    // One chunk at 20 s is progress, so 30 s later is not a stall -- but the wall-clock budget
    // (32.5 s for this size) still ends it.
    check("progress at 20 s", b.OnChunk(chunk_for(p), d.data(), d.size(), 20000000) ==
                                  ClipImageReceiver::ChunkResult::Accepted);
    check("not over before the budget", !b.Tick(budget - 1) && b.state() == ClipImageState::Pulling);
    check("the wall-clock budget ends it even while chunks arrive",
          b.Tick(budget) && b.state() == ClipImageState::Failed && b.reason() == ClipImageReason::Timeout);
    check("the budget is capped at 20 min", clip_image_total_timeout_us(64 * MiB) == 20ull * 60 * 1000000);
    ClipImageReceiver s(1);
    s.OnOffer(offer(64, 64, 1000), 1, true, 1, 0);
    s.OnSessionEnd();
    check("a session end cancels and frees at once", s.state() == ClipImageState::Cancelled &&
                                                         s.reason() == ClipImageReason::Session && s.heldBytes() == 0);
  }
  // ------------------------------------------------------------------ the sender
  {
    ClipImageSender s;
    auto pkg = std::make_shared<std::vector<uint8_t>>(40000, 7);
    ClipImageOffer o = offer(10, 10, 40000);
    s.Begin(pkg, o, 0);
    ControlClipImageOfferReplyMessage rep{};
    rep.transferId = o.transferId;
    rep.verdict = static_cast<uint8_t>(ClipImageVerdict::Busy);
    check("a refusal drops the package", !s.OnOfferReply(rep) && !s.Active());
    s.Begin(pkg, o, 0);
    rep.verdict = 0;
    rep.epochTag = 77;
    rep.bulkGen = 5;
    check("an accept starts serving", s.OnOfferReply(rep) && s.phase() == ClipImageSender::Phase::Serving);
    ClipBulkPullMessage p{};
    p.transferId = o.transferId;
    p.epochTag = 77;
    p.bulkGen = 5;
    p.offset = 0;
    p.len = kClipImageChunkBytes;
    ClipImageSender::Served out;
    auto refused = [&](ClipBulkPullMessage x, const char* what) {
      check(std::string("pull not answered: ") + what, !s.OnPull(x, 0, &out));
    };
    ClipBulkPullMessage x = p;
    x.epochTag = 76;
    refused(x, "old epochTag");
    x = p;
    x.bulkGen = 4;
    refused(x, "old generation");
    x = p;
    x.transferId ^= 1;
    refused(x, "another transfer");
    x = p;
    x.offset = 40000 - 10;
    refused(x, "past the end");
    x = p;
    x.offset = 0xFFFFFFF0u;
    refused(x, "offset + len overflowing 32 bits");
    x = p;
    x.len = kClipImageMaxChunkBytes + 1;
    refused(x, "a chunk larger than any sender answers");
    check("a good pull is answered with exactly the bytes asked", s.OnPull(p, 10, &out) && out.header.len == p.len &&
                                                                      out.data == pkg->data() && !out.completedChunk);
    ClipBulkPullMessage n = p;
    n.offset = kClipImageChunkBytes;
    n.triggerOffset = 0;
    check("a pull triggered by a chunk this side sent is that chunk's round trip",
          s.OnPull(n, 20, &out) && out.completedChunk && out.completedBytes == kClipImageChunkBytes);
    check("the same trigger does not count twice", s.OnPull(n, 30, &out) && !out.completedChunk);
  }
  // ------------------------------------------------------------------ token bucket
  {
    BulkTokenBucket b;
    b.Refill(1000000, 1000000);
    check("the first refill only starts the clock", b.tokens() == 0);
    b.Refill(1001000, 1000000);  // 1 ms at 1 Mbps = 125 bytes
    check("tokens accrue at rate / 8 per second", b.tokens() > 124.9 && b.tokens() < 125.1);
    b.Refill(3001000, 1000000);
    check("a long idle saves at most the fixed burst", b.tokens() == BulkTokenBucket::Capacity(1000000));
    check("the burst is fixed (agreed): four datagrams at 64 kbps and at 16 Mbps alike",
          BulkTokenBucket::Capacity(64000) == 6000.0 && BulkTokenBucket::Capacity(16000000) == 6000.0);
    b.Refill(3002000, 0);
    check("a pause empties the bucket", b.tokens() == 0 && b.WaitUs(1200, 0) == UINT64_MAX);
    check("the wait for 1200 bytes at 64 kbps is 150 ms", b.WaitUs(1200, 64000) == 150001);
    b.TakeOwed(500);
    check("acknowledgements may run the bucket into debt", b.tokens() == -500 && !b.TryTake(1));
  }
  // ------------------------------------------------------------------ the pacer, on a real timer
  {
    std::mutex mu;
    std::vector<uint64_t> sentAt;
    std::vector<std::vector<uint8_t>> sent;
    std::atomic<uint32_t> rate{2000000};
    BulkPacer pacer;
    pacer.Start(
        [&](const void* d, size_t n) {
          std::lock_guard<std::mutex> l(mu);
          sentAt.push_back(BulkPacer::NowUs());
          sent.emplace_back(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
          return true;
        },
        [&](uint64_t) { return rate.load(); }, nullptr, nullptr);
    const size_t N = 200;
    for (uint32_t i = 0; i < N; ++i) {
      auto d = data_dgram(0x40000005u, 1 + i / 16, static_cast<uint16_t>(i % 16));
      pacer.Enqueue(d.data(), d.size());
    }
    const uint64_t bytesEach = sizeof(UdpControlChunkHeader) + 1100;
    // Measured over the whole run: ~ N * bytes * 8 / rate.
    for (int i = 0; i < 400 && pacer.Queued() > 0; ++i) Sleep(10);
    {
      std::lock_guard<std::mutex> l(mu);
      const double spanS = (sentAt.back() - sentAt.front()) / 1e6;
      const double achieved = (sent.size() - 1) * bytesEach * 8 / spanS;
      char buf[160];
      std::snprintf(buf, sizeof(buf), "the pacer holds 2 Mbps (achieved %.0f bps over %.3f s, within 10%%)", achieved, spanS);
      check(buf, sent.size() == N && achieved > 1800000 && achieved < 2200000);
      // No burst: the gap between datagrams stays near bytes / rate, not 15.6 ms timer quanta.
      uint64_t maxGap = 0;
      for (size_t i = 1; i < sentAt.size(); ++i) maxGap = (std::max)(maxGap, sentAt[i] - sentAt[i - 1]);
      std::snprintf(buf, sizeof(buf), "no 15.6 ms timer quanta: the largest gap is %llu us (one datagram = %llu us)",
                    static_cast<unsigned long long>(maxGap), static_cast<unsigned long long>(bytesEach * 8 * 1000000 / 2000000));
      check(buf, maxGap < 10000);
    }
    // Priority, coalescing and resend accounting, paused so nothing leaves meanwhile.
    rate.store(0);
    Sleep(60);
    {
      std::lock_guard<std::mutex> l(mu);
      sent.clear();
      sentAt.clear();
    }
    auto d1 = data_dgram(0x40000005u, 100, 0);
    auto d2 = data_dgram(0x40000005u, 100, 1);
    pacer.Enqueue(d1.data(), d1.size());
    pacer.Enqueue(d2.data(), d2.size());
    pacer.Enqueue(d1.data(), d1.size());  // the channel's retry of a datagram still queued
    UdpControlAckPacket ack{};
    ack.kind = static_cast<uint16_t>(UdpPacketKind::ControlAck);
    pacer.Enqueue(&ack, sizeof(ack));
    Sleep(100);
    {
      std::lock_guard<std::mutex> l(mu);
      check("paused: data waits, but the acknowledgement goes out at once",
            sent.size() == 1 && sent[0].size() == sizeof(ack));
    }
    check("a retry of a still-queued datagram is coalesced, not queued twice",
          pacer.Queued() == 2 && pacer.GetStats().resendsCoalesced == 1);
    rate.store(4000000);
    for (int i = 0; i < 100 && pacer.Queued() > 0; ++i) Sleep(5);
    const uint64_t before = pacer.GetStats().resendsAfterTransmit;
    pacer.Enqueue(d1.data(), d1.size());  // a retry of one that already left: loss evidence
    for (int i = 0; i < 100 && pacer.Queued() > 0; ++i) Sleep(5);
    check("a resend of a datagram that already left counts as loss",
          pacer.GetStats().resendsAfterTransmit == before + 1);
    pacer.Stop();
  }
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
