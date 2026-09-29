// file_copy_wire.hpp + file_copy_net_rules.hpp: the codec and the rules, no network, no files.
// (t-zdmsd4gb r1 step 1)

#include <cstdio>
#include <string>
#include <vector>

#include "file_copy_net_rules.hpp"
#include "file_copy_wire.hpp"

using namespace remote60::native_poc;
using namespace remote60::native_poc::file_copy::net;

namespace {
int g_checks = 0, g_failed = 0;
void check(const std::string& name, bool ok) {
  ++g_checks;
  if (!ok) ++g_failed;
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name.c_str());
}
OfferItem item(uint32_t index, const std::u16string& name, uint64_t size) {
  OfferItem it;
  it.index = index;
  it.name = name;
  it.size = size;
  it.mtime = 0x01DA000000000000ull + index;
  it.attributes = 0x20;
  return it;
}
template <class T>
bool round_trip(const T& in, T* out) {
  return parse(body(in), out);
}
}  // namespace

int main() {
  // ------------------------------------------------------------------ codec round trips
  {
    Offer o;
    o.epochTag = 0x1122334455667788ull;
    o.offerId = 42;
    o.revision = 7;
    o.items = {item(0, u"report.pdf", 1234567), item(3, u"사진 1.jpg", 0), item(9, u"a..b", 5)};
    Offer back;
    check("Offer round trip", round_trip(o, &back) && back.epochTag == o.epochTag && back.offerId == 42 &&
                                  back.revision == 7 && back.items.size() == 3 && back.items[1].name == u"사진 1.jpg" &&
                                  back.items[2].index == 9 && back.items[0].size == 1234567);
    std::vector<uint8_t> b = body(o);
    std::vector<uint8_t> cut(b.begin(), b.end() - 1);
    check("Offer: one byte short is refused", !parse(cut, &back));
    b.push_back(0);
    check("Offer: one byte over is refused", !parse(b, &back));
    Offer many;
    for (uint32_t i = 0; i < kMaxOfferFiles + 1; ++i) many.items.push_back(item(i, u"f" + std::u16string(1, u'a' + i % 26) + u".bin", 1));
    check("Offer: 101 items are refused by the decoder", !parse(body(many), &back));
    Offer longName;
    longName.items = {item(0, std::u16string(file_copy::kMaxNameUnits + 1, u'x'), 1)};
    check("Offer: a name over 259 units is refused by the decoder", !parse(body(longName), &back));
  }
  {
    OfferReply r{42, Verdict::HelperUnavailable}, back;
    check("OfferReply round trip", round_trip(r, &back) && back.offerId == 42 && back.verdict == Verdict::HelperUnavailable);
    std::vector<uint8_t> b = body(r);
    b[8] = 9;  // past the last Verdict
    check("OfferReply: an unknown verdict is refused", !parse(b, &back));
  }
  {
    OfferQueryReply same;
    same.epochTag = 5;
    same.revision = 3;
    same.unchanged = true;
    OfferQueryReply back;
    check("OfferQueryReply unchanged: no list travels", round_trip(same, &back) && back.unchanged && back.items.empty() &&
                                                            body(same).size() == 17);
    OfferQueryReply fresh = same;
    fresh.unchanged = false;
    fresh.offerId = 77;
    fresh.items = {item(0, u"x.txt", 3)};
    check("OfferQueryReply changed: the list travels once", round_trip(fresh, &back) && !back.unchanged &&
                                                                back.offerId == 77 && back.items.size() == 1);
  }
  {
    PasteQueryReply r{42, PasteState::Begun, 9, PasteEndReason::None}, back;
    check("PasteQueryReply round trip", round_trip(r, &back) && back.state == PasteState::Begun && back.pasteOp == 9);
    std::vector<uint8_t> b = body(r);
    b[8] = 6;
    check("PasteQueryReply: an unknown state is refused", !parse(b, &back));
  }
  {
    Prepare p;
    p.direction = Direction::PtoR;
    p.offerId = 1;
    p.pasteOp = 2;
    p.items = {{0, 0, 10, 11, 0x20}, {1, 6, 0, 0, 0}};
    Prepare back;
    check("Prepare round trip", round_trip(p, &back) && back.direction == Direction::PtoR && back.items.size() == 2 &&
                                    back.items[1].status == 6);
    std::vector<uint8_t> b = body(p);
    b[0] = 3;
    check("Prepare: an unknown direction is refused", !parse(b, &back));
    PrepareReply pr;
    pr.direction = Direction::RtoP;
    pr.offerId = 1;
    pr.pasteOp = 2;
    pr.verdict = Verdict::Busy;
    pr.epochTag = 99;
    pr.bulkGen = 5;
    PrepareReply prb;
    check("PrepareReply round trip", round_trip(pr, &prb) && prb.verdict == Verdict::Busy && prb.bulkGen == 5 && prb.epochTag == 99);
  }
  {
    End e{1, 0, PasteEndReason::Cancelled}, eb;
    check("End round trip (pasteOp 0 = the whole offer)", round_trip(e, &eb) && eb.pasteOp == 0 && eb.reason == PasteEndReason::Cancelled);
    StatusReply s{1, 2, PasteState::Active, PasteEndReason::None, 1ull << 33}, sb;
    check("StatusReply round trip (64-bit byte count)", round_trip(s, &sb) && sb.bytesDelivered == (1ull << 33));
  }
  {
    std::vector<uint8_t> big(kMaxControlPayload + 1), out;
    check("frame_control refuses a body over the bound", !frame_control(FileMsg::Offer, 1, big, &out));
    std::vector<uint8_t> ok(10, 7);
    check("frame_control: header + body", frame_control(FileMsg::Offer, 5, ok, &out) && out.size() == sizeof(FileControlHeader) + 10);
    FileControlHeader h{};
    std::memcpy(&h, out.data(), sizeof(h));
    check("...the header names the type, its own size, the seq and the body length",
          h.header.type == 65 && h.header.size == sizeof(FileControlHeader) && h.seq == 5 && h.payloadBytes == 10);
    check("65~76 are file control, 64 and 77 are not", is_file_control(65) && is_file_control(76) && !is_file_control(64) &&
                                                           !is_file_control(77));
  }
  {
    Pull p;
    p.epochTag = 1;
    p.offerId = 2;
    p.pasteOp = 3;
    p.bulkGen = 4;
    p.fileIndex = 5;
    p.requestId = 6;
    p.offset = (1ull << 32) + 17;  // over 4 GiB into a file: 64-bit
    p.length = kMaxFileChunkBytes;
    const std::vector<uint8_t> w = frame_bulk(p);
    Pull back;
    check("Pull round trip with a 64-bit offset", parse_bulk(w.data(), w.size(), &back) && back.offset == p.offset &&
                                                      back.length == p.length && back.triggerRequestId == kNoTrigger);
    Pull tooLong = p;
    tooLong.length = kMaxFileChunkBytes + 1;
    const std::vector<uint8_t> w2 = frame_bulk(tooLong);
    check("Pull: a length over the chunk bound is refused", !parse_bulk(w2.data(), w2.size(), &back));
    Chunk c;
    c.data = {1, 2, 3};
    sha256(c.data.data(), c.data.size(), &c.sha256);
    const std::vector<uint8_t> wc = frame_bulk(c);
    Chunk cb;
    check("Chunk round trip", parse_bulk(wc.data(), wc.size(), &cb) && cb.data == c.data && cb.sha256 == c.sha256);
    check("a Chunk is not a Pull", !parse_bulk(wc.data(), wc.size(), &back));
    std::vector<uint8_t> extra = wc;
    extra.push_back(0);
    check("Chunk: a trailing byte is refused", !parse_bulk(extra.data(), extra.size(), &cb));
    check("bulk_type: an image pull (63) is not a file message", [] {
      MessageHeader h{};
      h.type = 63;
      h.size = sizeof(MessageHeader);
      return bulk_type(reinterpret_cast<const uint8_t*>(&h), sizeof(h)) == 0;
    }());
  }

  // ------------------------------------------------------------------ rules
  check("range: inside", range_ok(100, 10, 60, 64));
  check("range: to the end exactly", range_ok(100, 36, 64, 64));
  check("range: one past the end", !range_ok(100, 37, 64, 64));
  check("range: offset past the end", !range_ok(100, 101, 0, 64));
  check("range: zero at the end", range_ok(100, 100, 0, 64));
  check("range: over the chunk bound", !range_ok(1000, 0, 65, 64));
  check("range: offset near 2^64 cannot wrap", !range_ok(100, ~0ull - 5, 10, 64));
  check("range: size near 2^64 and a big offset", range_ok(~0ull, ~0ull - 64, 64, 64) && !range_ok(~0ull, ~0ull - 63, 64, 64));
  {
    std::string why;
    check("offer: ordinary names", check_offer_items({item(0, u"a.txt", 1), item(1, u"a..b", 1), item(2, u"Ünïcödé.bin", 1)}, &why) ==
                                       Verdict::Accept);
    check("offer: '..' is refused", check_offer_items({item(0, u"..", 1)}, &why) == Verdict::BadRequest);
    check("offer: a separator is refused", check_offer_items({item(0, u"dir\\a.txt", 1)}, &why) == Verdict::BadRequest);
    check("offer: a stream colon is refused", check_offer_items({item(0, u"a.txt:zone", 1)}, &why) == Verdict::BadRequest);
    check("offer: a device name is refused", check_offer_items({item(0, u"com1.txt", 1)}, &why) == Verdict::BadRequest);
    check("offer: a trailing dot is refused", check_offer_items({item(0, u"a.", 1)}, &why) == Verdict::BadRequest);
    check("offer: two names equal ignoring case are refused",
          check_offer_items({item(0, u"Report.PDF", 1), item(1, u"report.pdf", 1)}, &why) == Verdict::BadRequest);
    check("offer: non-ASCII names equal ignoring case are refused",
          check_offer_items({item(0, u"ÄRGER.txt", 1), item(1, u"ärger.TXT", 1)}, &why) == Verdict::BadRequest);
    check("offer: an index twice is refused", check_offer_items({item(3, u"a", 1), item(3, u"b", 1)}, &why) == Verdict::BadRequest);
    std::vector<OfferItem> hundred, more;
    for (uint32_t i = 0; i < 100; ++i) hundred.push_back(item(i, u"f" + std::u16string(1, u'A' + i / 26) + std::u16string(1, u'a' + i % 26), 1));
    more = hundred;
    more.push_back(item(100, u"zz", 1));
    check("offer: 100 files", check_offer_items(hundred, &why) == Verdict::Accept);
    check("offer: 101 files", check_offer_items(more, &why) == Verdict::TooMany);
    check("offer: exactly 4 GiB in total", check_offer_items({item(0, u"a", 2ull << 30), item(1, u"b", 2ull << 30)}, &why) == Verdict::Accept);
    check("offer: 4 GiB + 1", check_offer_items({item(0, u"a", 2ull << 30), item(1, u"b", (2ull << 30) + 1)}, &why) == Verdict::TooLarge);
    check("offer: sizes that would wrap a 64-bit sum", check_offer_items({item(0, u"a", 1ull << 63), item(1, u"b", 1ull << 63)}, &why) ==
                                                           Verdict::TooLarge);
  }
  {
    Pull asked;
    asked.epochTag = 1;
    asked.offerId = 2;
    asked.pasteOp = 3;
    asked.bulkGen = 4;
    asked.fileIndex = 5;
    asked.requestId = 6;
    asked.offset = 700;
    asked.length = 4;
    Chunk c;
    c.epochTag = 1;
    c.offerId = 2;
    c.pasteOp = 3;
    c.bulkGen = 4;
    c.fileIndex = 5;
    c.requestId = 6;
    c.offset = 700;
    c.data = {9, 8, 7, 6};
    sha256(c.data.data(), c.data.size(), &c.sha256);
    check("chunk: exactly what was asked", check_chunk(asked, c) == ChunkCheck::Ok);
    Chunk x = c;
    x.bulkGen = 3;  // an older generation's answer
    check("chunk: an older generation is not this pull", check_chunk(asked, x) == ChunkCheck::WrongIdentity);
    x = c;
    x.requestId = 5;
    check("chunk: another request's answer", check_chunk(asked, x) == ChunkCheck::WrongIdentity);
    x = c;
    x.offset = 704;
    check("chunk: another range", check_chunk(asked, x) == ChunkCheck::WrongIdentity);
    x = c;
    x.fileIndex = 4;
    check("chunk: another file", check_chunk(asked, x) == ChunkCheck::WrongIdentity);
    x = c;
    x.epochTag = 2;
    check("chunk: another session", check_chunk(asked, x) == ChunkCheck::WrongIdentity);
    x = c;
    x.data.pop_back();
    sha256(x.data.data(), x.data.size(), &x.sha256);
    check("chunk: short (even with a matching hash)", check_chunk(asked, x) == ChunkCheck::WrongLength);
    x = c;
    x.data[2] ^= 1;
    check("chunk: one flipped bit", check_chunk(asked, x) == ChunkCheck::WrongHash);
  }
  {
    CoverageTracker seq(10);
    seq.OnVerified(0, 4);
    seq.OnVerified(4, 6);
    check("coverage: 0..size in order, once -> whole file verified", seq.whole_file_verified());
    CoverageTracker seek(10);
    seek.OnVerified(4, 6);
    seek.OnVerified(0, 4);
    check("coverage: out of order (a Seek) -> chunk verified only", !seek.whole_file_verified());
    CoverageTracker twice(10);
    twice.OnVerified(0, 4);
    twice.OnVerified(0, 4);
    twice.OnVerified(4, 6);
    check("coverage: a range read twice -> chunk verified only", !twice.whole_file_verified());
    CoverageTracker part(10);
    part.OnVerified(0, 4);
    check("coverage: not to the end -> not whole", !part.whole_file_verified() && part.sequential());
    CoverageTracker empty(0);
    check("coverage: an empty file is whole once its size 0 is confirmed", empty.whole_file_verified());
  }

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
