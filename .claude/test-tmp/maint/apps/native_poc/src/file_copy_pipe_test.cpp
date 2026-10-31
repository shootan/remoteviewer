// The helper pipe codec (file_copy_pipe.hpp): every message round-trips, every bound refuses, a
// truncated or padded payload is rejected, the frame header is checked, and the names that may
// not travel in an offer are refused. Pure: no pipe, no Win32. (file-copy-helper r1)
//
// Build: remote60_file_copy_pipe_test (CMake).

#include <cstdio>
#include <string>
#include <vector>

#include "file_copy_pipe.hpp"

using namespace remote60::native_poc::file_copy;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

FileId make_id(uint64_t serial, uint8_t seed) {
  FileId f;
  f.volumeSerial = serial;
  for (size_t i = 0; i < f.id.size(); ++i) f.id[i] = static_cast<uint8_t>(seed + i);
  return f;
}

RemoteFileItem item(const char16_t* name, uint64_t size, uint64_t mtime, uint32_t attrs) {
  RemoteFileItem it;
  it.name = name;
  it.size = size;
  it.mtime = mtime;
  it.attributes = attrs;
  return it;
}

bool same_item(const RemoteFileItem& a, const RemoteFileItem& b) {
  return a.name == b.name && a.size == b.size && a.mtime == b.mtime && a.attributes == b.attributes;
}

// A frame through the wire encoding and back: header decoded, payload split off.
bool through_wire(const PipeFrame& in, PipeFrame* out) {
  std::vector<uint8_t> wire;
  if (!encode_frame(in, &wire)) return false;
  if (wire.size() < kFrameHeaderBytes) return false;
  uint32_t length = 0;
  if (!decode_frame_header(wire.data(), &out->type, &length)) return false;
  if (wire.size() != kFrameHeaderBytes + length) return false;
  out->payload.assign(wire.begin() + kFrameHeaderBytes, wire.end());
  return true;
}

template <class T>
bool truncated_rejected(const PipeFrame& f, T* scratch) {
  if (f.payload.empty()) return true;
  PipeFrame t = f;
  t.payload.pop_back();
  return !decode(t, scratch);
}
template <class T>
bool padded_rejected(const PipeFrame& f, T* scratch) {
  PipeFrame t = f;
  t.payload.push_back(0);
  return !decode(t, scratch);
}

void test_round_trips() {
  std::printf("--- every message round-trips through the wire, and refuses a byte less or more ---\n");
  {
    Hello m;
    for (size_t i = 0; i < m.nonce.size(); ++i) m.nonce[i] = static_cast<uint8_t>(0xA0 + i);
    m.pid = 4321;
    PipeFrame w;
    Hello d;
    check("Hello", through_wire(encode(m), &w) && decode(w, &d) && d.nonce == m.nonce && d.pid == 4321 && d.version == kPipeVersion);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
    check("HelloAck / Shutdown are empty frames",
          through_wire(encode_hello_ack(), &w) && w.type == PipeMsg::HelloAck && w.payload.empty() &&
              through_wire(encode_shutdown(), &w) && w.type == PipeMsg::Shutdown && w.payload.empty());
  }
  {
    PublishRemoteFiles m;
    m.offerId = 0x1122334455667788ull;
    m.items = {item(u"a.bin", 1ull << 33, 132000000000000000ull, 0x20), item(u"한글 이름.txt", 5, 7, 0x80)};
    PipeFrame w;
    PublishRemoteFiles d;
    check("PublishRemoteFiles (2 items, one > 4 GiB, one non-ASCII)",
          through_wire(encode(m), &w) && decode(w, &d) && d.offerId == m.offerId && d.items.size() == 2 &&
              same_item(d.items[0], m.items[0]) && same_item(d.items[1], m.items[1]));
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
    PublishRemoteFiles empty;
    empty.offerId = 9;
    check("...zero items round-trips (the helper refuses it, the codec does not)",
          through_wire(encode(empty), &w) && decode(w, &d) && d.items.empty());
    // r7 (D2): the publish bound to a clipboard revision is its own message (18), version 2.
    check("PublishRemoteFiles goes as 10 and is not 'if unchanged'",
          encode(m).type == PipeMsg::PublishRemoteFiles && decode(encode(m), &d) && !d.ifUnchanged);
    PublishRemoteFiles g = m;
    g.ifUnchanged = true;
    g.expectSeq = 0xA1B2C3D4u;
    check("PublishRemoteFilesIfUnchanged (18) round-trips with its revision",
          through_wire(encode(g), &w) && w.type == PipeMsg::PublishRemoteFilesIfUnchanged && decode(w, &d) &&
              d.ifUnchanged && d.expectSeq == 0xA1B2C3D4u && d.items.size() == 2 && same_item(d.items[1], m.items[1]));
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
    PipeFrame as10 = w;
    as10.type = PipeMsg::PublishRemoteFiles;  // 18's body read as 10: the revision is a trailing extra
    check("...18's body is not a valid 10 (no silent drop of the revision)", !decode(as10, &d));
    check("the pipe version is 2 (a version-1 helper is refused at Hello, never sent 18)", kPipeVersion == 2);
  }
  {
    PublishResult m;
    m.offerId = 5;
    m.status = Status::TooMany;
    m.count = 101;
    PipeFrame w;
    PublishResult d;
    check("PublishResult", through_wire(encode(m), &w) && decode(w, &d) && d.offerId == 5 && d.status == Status::TooMany && d.count == 101);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    ClearRemoteFiles m;
    m.offerId = 77;
    PipeFrame w;
    ClearRemoteFiles d;
    check("ClearRemoteFiles", through_wire(encode(m), &w) && decode(w, &d) && d.offerId == 77);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    PasteBegin m;
    m.offerId = 1;
    m.pasteOp = 2;
    PipeFrame w;
    PasteBegin d;
    check("PasteBegin", through_wire(encode(m), &w) && decode(w, &d) && d.offerId == 1 && d.pasteOp == 2);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    PasteDescriptor m;
    m.offerId = 1;
    m.pasteOp = 2;
    m.status = Status::Changed;
    m.items = {item(u"x", 3, 4, 5)};
    PipeFrame w;
    PasteDescriptor d;
    check("PasteDescriptor", through_wire(encode(m), &w) && decode(w, &d) && d.status == Status::Changed && d.items.size() == 1 &&
                                 same_item(d.items[0], m.items[0]));
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    ReadRequest m;
    m.offerId = 1;
    m.pasteOp = 2;
    m.fileIndex = 3;
    m.offset = 0xFFFFFFFF00ull;
    m.length = kMaxChunkBytes;
    PipeFrame w;
    ReadRequest d;
    check("ReadRequest (offset above 32 bits, length at the chunk bound)",
          through_wire(encode(m), &w) && decode(w, &d) && d.fileIndex == 3 && d.offset == m.offset && d.length == kMaxChunkBytes);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    ReadData m;
    m.offerId = 1;
    m.pasteOp = 2;
    m.fileIndex = 0;
    m.offset = 262144;
    m.status = Status::Ok;
    m.data.resize(kMaxChunkBytes);
    for (size_t i = 0; i < m.data.size(); ++i) m.data[i] = static_cast<uint8_t>(i * 131);
    PipeFrame w;
    ReadData d;
    check("ReadData with a full 256 KiB chunk", through_wire(encode(m), &w) && decode(w, &d) && d.data == m.data && d.offset == 262144 &&
                                                   d.status == Status::Ok);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
    ReadData err;
    err.status = Status::ReadError;
    check("...an error reply carries no bytes", through_wire(encode(err), &w) && decode(w, &d) && d.status == Status::ReadError && d.data.empty());
  }
  {
    PasteEnd m;
    m.offerId = 1;
    m.pasteOp = 2;
    m.reason = EndReason::Idle;
    PipeFrame w;
    PasteEnd d;
    check("PasteEnd", through_wire(encode(m), &w) && decode(w, &d) && d.reason == EndReason::Idle);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    StatFiles m;
    m.requestId = 8;
    m.paths = {u"C:\\Users\\me\\a.bin", u"\\\\server\\share\\b.txt", u""};
    PipeFrame w;
    StatFiles d;
    check("StatFiles (3 paths, one empty)", through_wire(encode(m), &w) && decode(w, &d) && d.requestId == 8 && d.paths == m.paths);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    Stats m;
    m.requestId = 8;
    StatEntry ok;
    ok.status = Status::Ok;
    ok.id = make_id(0xABCD, 1);
    ok.size = 10;
    ok.mtime = 20;
    ok.attributes = 0x20;
    ok.name = u"a.bin";
    StatEntry bad;
    bad.status = Status::NotFound;
    m.entries = {ok, bad};
    PipeFrame w;
    Stats d;
    check("Stats", through_wire(encode(m), &w) && decode(w, &d) && d.entries.size() == 2 && d.entries[0].status == Status::Ok &&
                       d.entries[0].id == ok.id && d.entries[0].size == 10 && d.entries[0].mtime == 20 && d.entries[0].attributes == 0x20 &&
                       d.entries[0].name == u"a.bin" && d.entries[1].status == Status::NotFound && d.entries[1].name.empty());
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    Pin m;
    m.pinId = 3;
    m.leaseMs = 60000;
    PinRequestEntry e;
    e.path = u"C:\\a.bin";
    e.expectedId = make_id(1, 9);
    e.expectedSize = 100;
    e.expectedMtime = 200;
    m.entries = {e};
    PipeFrame w;
    Pin d;
    check("Pin", through_wire(encode(m), &w) && decode(w, &d) && d.pinId == 3 && d.leaseMs == 60000 && d.entries.size() == 1 &&
                     d.entries[0].path == e.path && d.entries[0].expectedId == e.expectedId && d.entries[0].expectedSize == 100 &&
                     d.entries[0].expectedMtime == 200);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    PinResult m;
    m.pinId = 3;
    PinResultEntry a;
    a.status = Status::Ok;
    a.id = make_id(2, 3);
    a.size = 5;
    a.mtime = 6;
    PinResultEntry b;
    b.status = Status::Replaced;
    m.entries = {a, b};
    PipeFrame w;
    PinResult d;
    check("PinResult", through_wire(encode(m), &w) && decode(w, &d) && d.entries.size() == 2 && d.entries[0].id == a.id &&
                           d.entries[1].status == Status::Replaced);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    ReadLocal m;
    m.pinId = 3;
    m.fileIndex = 1;
    m.offset = 1 << 20;
    m.length = 4096;
    PipeFrame w;
    ReadLocal d;
    check("ReadLocal", through_wire(encode(m), &w) && decode(w, &d) && d.pinId == 3 && d.fileIndex == 1 && d.offset == (1u << 20) && d.length == 4096);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    LocalData m;
    m.pinId = 3;
    m.fileIndex = 1;
    m.offset = 7;
    m.status = Status::Ok;
    m.data = {1, 2, 3};
    PipeFrame w;
    LocalData d;
    check("LocalData", through_wire(encode(m), &w) && decode(w, &d) && d.data == m.data && d.offset == 7);
    check("...truncated / padded refused", truncated_rejected(w, &d) && padded_rejected(w, &d));
  }
  {
    Unpin m;
    m.pinId = 44;
    Unpinned u;
    u.pinId = 44;
    u.released = 2;
    PipeFrame w;
    Unpin d;
    Unpinned du;
    check("Unpin / Unpinned", through_wire(encode(m), &w) && decode(w, &d) && d.pinId == 44 && through_wire(encode(u), &w) &&
                                  decode(w, &du) && du.pinId == 44 && du.released == 2);
  }
}

void test_bounds() {
  std::printf("\n--- the bounds refuse ---\n");
  {
    PublishRemoteFiles m;
    m.items.assign(kMaxFiles + 1, item(u"x", 1, 1, 0));
    PublishRemoteFiles d;
    check("PublishRemoteFiles with 101 items is refused", !decode(encode(m), &d));
    m.items.assign(kMaxFiles, item(u"x", 1, 1, 0));
    check("...100 is accepted", decode(encode(m), &d) && d.items.size() == kMaxFiles);
  }
  {
    PublishRemoteFiles m;
    m.items = {item(u"", 1, 1, 0)};
    m.items[0].name.assign(kMaxNameUnits + 1, u'a');
    PublishRemoteFiles d;
    check("a name of 260 units is refused by the codec", !decode(encode(m), &d));
    m.items[0].name.assign(kMaxNameUnits, u'a');
    check("...259 is accepted", decode(encode(m), &d));
  }
  {
    StatFiles m;
    m.paths = {std::u16string(kMaxPathUnits + 1, u'p')};
    StatFiles d;
    check("a path of 32768 units is refused", !decode(encode(m), &d));
    m.paths = {std::u16string(kMaxPathUnits, u'p')};
    check("...32767 is accepted", decode(encode(m), &d));
    m.paths.assign(kMaxStatCount + 1, u"x");
    check("...101 paths refused", !decode(encode(m), &d));
  }
  {
    ReadRequest m;
    m.length = kMaxChunkBytes + 1;
    ReadRequest d;
    check("ReadRequest length over the chunk bound is refused", !decode(encode(m), &d));
    ReadLocal l;
    l.length = kMaxChunkBytes + 1;
    ReadLocal dl;
    check("ReadLocal likewise", !decode(encode(l), &dl));
  }
  {
    ReadData m;
    m.data.resize(kMaxChunkBytes + 1);
    ReadData d;
    check("ReadData with 256 KiB + 1 is refused", !decode(encode(m), &d));
    std::vector<uint8_t> wire;
    PipeFrame f;
    f.type = PipeMsg::ReadData;
    f.payload.resize(kMaxPayloadBytes + 1);
    check("a frame over kMaxPayloadBytes cannot be encoded", !encode_frame(f, &wire));
    f.payload.resize(kMaxPayloadBytes);
    check("...at the bound it can", encode_frame(f, &wire) && wire.size() == kFrameHeaderBytes + kMaxPayloadBytes);
  }
  {
    PipeFrame f;
    f.type = PipeMsg::PasteBegin;
    f.payload = {1, 2, 3};
    std::vector<uint8_t> wire;
    encode_frame(f, &wire);
    PipeMsg t;
    uint32_t len = 0;
    check("a good header decodes", decode_frame_header(wire.data(), &t, &len) && t == PipeMsg::PasteBegin && len == 3);
    std::vector<uint8_t> bad = wire;
    bad[0] ^= 0xFF;
    check("a wrong magic is refused", !decode_frame_header(bad.data(), &t, &len));
    bad = wire;
    bad[10] = 9;  // version
    check("a wrong version is refused", !decode_frame_header(bad.data(), &t, &len));
    bad = wire;
    bad[4] = 0xFF;
    bad[5] = 0xFF;
    bad[6] = 0xFF;
    bad[7] = 0x7F;  // length 0x7FFFFFFF
    check("a length over the bound is refused", !decode_frame_header(bad.data(), &t, &len));
  }
  {
    Hello h;
    PipeFrame f = encode(h);
    PasteBegin d;
    check("a frame decoded as the wrong message is refused", !decode(f, &d));
  }
}

void test_names() {
  std::printf("\n--- names that may travel in an offer ---\n");
  const char16_t* good[] = {u"a.bin", u"한글 이름.txt", u"COM10.txt", u"concept.txt", u"x", u"report (final).docx", u"dot.in.middle"};
  for (const char16_t* n : good) check("accepted: " + std::string(std::u16string(n).size(), '*'), validate_remote_name(n));
  struct Bad {
    const char16_t* name;
    const char* why;
  };
  const Bad bad[] = {{u"", "empty"},          {u"a\\b", "backslash"},  {u"a/b", "slash"},       {u"c:x", "colon"},
                     {u"CON", "CON"},         {u"con.txt", "con.txt"}, {u"COM1", "COM1"},       {u"lpt9.log", "lpt9.log"},
                     {u"con.d.txt.bak", "con.d.txt.bak (the stem before the first dot is CON)"},
                     {u"nul", "nul"},         {u"name.", "trailing dot"}, {u"name ", "trailing space"}, {u".", "dot"},
                     {u"..", "dotdot"},       {u"a?b", "question"},    {u"a*b", "star"},        {u"a|b", "bar"},
                     {u"a<b", "lt"},          {u"a\"b", "quote"},      {u"CONIN$", "CONIN$"}};
  for (const Bad& b : bad) check(std::string("refused: ") + b.why, !validate_remote_name(b.name));
  std::u16string ctl = u"a";
  ctl.push_back(0x01);
  check("refused: control character", !validate_remote_name(ctl));
  check("refused: 260 units", !validate_remote_name(std::u16string(kMaxNameUnits + 1, u'a')));
  check("accepted: 259 units", validate_remote_name(std::u16string(kMaxNameUnits, u'a')));
}

void test_hex_and_names() {
  std::printf("\n--- hex and the pipe name ---\n");
  std::array<uint8_t, kNonceBytes> nonce{};
  for (size_t i = 0; i < nonce.size(); ++i) nonce[i] = static_cast<uint8_t>(i * 37);
  const std::wstring hex = hex_encode(nonce.data(), nonce.size());
  std::array<uint8_t, kNonceBytes> back{};
  check("nonce hex round-trips (64 digits)", hex.size() == 64 && hex_decode(hex, back.data(), back.size()) && back == nonce);
  check("hex of the wrong length is refused", !hex_decode(hex.substr(1), back.data(), back.size()));
  std::wstring corrupt = hex;
  corrupt[5] = L'g';
  check("a non-hex digit is refused", !hex_decode(corrupt, back.data(), back.size()));
  std::array<uint8_t, kPipeNameRandomBytes> r{};
  r[0] = 0xAB;
  r[15] = 0x01;
  const std::wstring name = pipe_name_for(r);
  check("pipe name = prefix + 32 hex", name.rfind(kPipeNamePrefix, 0) == 0 && name.size() == wcslen(kPipeNamePrefix) + 32 &&
                                          name.substr(wcslen(kPipeNamePrefix), 2) == L"ab" && name.substr(name.size() - 2) == L"01");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_round_trips();
  test_bounds();
  test_names();
  test_hex_and_names();
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
