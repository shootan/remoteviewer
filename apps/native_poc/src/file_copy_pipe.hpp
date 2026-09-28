#pragma once

// The pipe protocol between the elevated host (GNLinkStream) and the Medium-integrity clipboard
// helper (GNLinkClipHelper). Owned by file-copy-helper r1; the network side (control messages
// 65~79, bulk) reaches the helper through this header and nothing else.
//
// Contract (clip_files_debate_2026-09-28.md, 3차 합의; file_copy_plan_r2.md §1/§3): one pipe
// instance `\\.\pipe\GNLinkClip-<128-bit hex>`, the helper proves itself with the 256-bit nonce it
// was given on its command line, and the command set is exactly these messages -- there is no
// "run this path" command. The host names files only as items of a list it received from its own
// clipboard (R->P) or as indices into an offer it published (P->R).
//
// PURE. No Win32, no I/O, no allocation outside std containers: the codec is unit-tested on its
// own and the helper, the host and the tests read one definition. Frames are length-prefixed,
// every field is little-endian and written explicitly (no struct casts), strings are UTF-16 code
// units with a count, and the decoder refuses anything it does not understand, anything past a
// bound, and any payload with bytes left over.
//
//   frame = header(16) + payload(<= kMaxPayloadBytes)
//   header = magic u32 | length u32 | type u16 | version u16 | reserved u32
//
// Directions (H = host, C = helper):
//   C->H Hello            nonce, pid                         first thing after connecting
//   H->C HelloAck                                            the host accepted the handshake
//   H->C Shutdown                                            clear the clipboard and exit
//   H->C PublishRemoteFiles offerId, items[]                 put an async IDataObject on the clipboard
//   C->H PublishResult    offerId, status, count             what the helper did with it
//   H->C ClearRemoteFiles offerId                            take it off (if still ours)
//   C->H PasteBegin       offerId, pasteOp                   a consumer started an async paste
//   H->C PasteDescriptor  offerId, pasteOp, status, items[]  the confirmed (pinned) sizes / times
//   C->H ReadRequest      offerId, pasteOp, index, offset, length
//   H->C ReadData         offerId, pasteOp, index, offset, status, bytes
//   C->H PasteEnd         offerId, pasteOp, reason
//   H->C StatFiles        requestId, paths[]                 R->P: identify the host's CF_HDROP items
//   C->H Stats            requestId, entries[]
//   H->C Pin              pinId, leaseMs, entries[]          open for the paste, verify identity
//   C->H PinResult        pinId, entries[]
//   H->C ReadLocal        pinId, index, offset, length
//   C->H LocalData        pinId, index, offset, status, bytes
//   H->C Unpin            pinId
//   C->H Unpinned         pinId, released

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace remote60::native_poc::file_copy {

constexpr uint32_t kPipeMagic = 0x504C4347u;  // bytes 'G' 'C' 'L' 'P' on the wire
constexpr uint16_t kPipeVersion = 1;
constexpr size_t kFrameHeaderBytes = 16;
constexpr wchar_t kPipeNamePrefix[] = L"\\\\.\\pipe\\GNLinkClip-";
constexpr size_t kPipeNameRandomBytes = 16;  // 128-bit, hex in the pipe name
constexpr size_t kNonceBytes = 32;           // 256-bit, hex on the helper's command line
constexpr uint32_t kMaxFiles = 100;          // per offer / stat / pin (design ceiling, plan §4)
constexpr uint32_t kMaxChunkBytes = 256u * 1024u;          // one ReadData / LocalData payload
constexpr uint32_t kMaxPayloadBytes = kMaxChunkBytes + 512u;  // a chunk plus its fixed fields
constexpr uint32_t kMaxPathUnits = 32767;    // a source path (R->P), UTF-16 units
constexpr uint32_t kMaxNameUnits = 259;      // FILEDESCRIPTORW::cFileName holds MAX_PATH-1
constexpr uint32_t kMaxStatCount = kMaxFiles;

enum class PipeMsg : uint16_t {
  Hello = 1,
  HelloAck = 2,
  Shutdown = 3,
  PublishRemoteFiles = 10,
  ClearRemoteFiles = 11,
  PasteBegin = 12,
  PasteDescriptor = 13,
  ReadRequest = 14,
  ReadData = 15,
  PasteEnd = 16,
  PublishResult = 17,
  StatFiles = 20,
  Stats = 21,
  Pin = 22,
  PinResult = 23,
  ReadLocal = 24,
  LocalData = 25,
  Unpin = 26,
  Unpinned = 27,
};

// Per-file and per-request outcomes. Numeric values are on the wire; add at the end only.
enum class Status : uint16_t {
  Ok = 0,
  NotFound = 1,
  AccessDenied = 2,
  SharingViolation = 3,  // a writer holds the file: refused, no privileged retry
  NotAFile = 4,          // a directory
  Excluded = 5,          // .lnk / .url / reparse point: never followed, never copied
  Replaced = 6,          // the FileId is not the one the offer named (deleted + recreated, renamed over)
  Changed = 7,           // same file, but size / time differ from what the offer said
  LeaseExpired = 8,
  UnknownId = 9,         // no such offer / pasteOp / pin
  ReadError = 10,
  TooMany = 11,
  BadPath = 12,          // device / object-manager / stream syntax, or an empty path
  Aborted = 13,
  Timeout = 14,
  Refused = 15,          // e.g. a name the descriptor may not carry
  BadRequest = 16,       // index / offset / length outside the item
};

enum class EndReason : uint16_t {
  Ended = 0,         // EndOperation with S_OK
  Error = 1,         // EndOperation with a failure, or a Read the host refused
  Idle = 2,          // no stream activity for the idle bound
  Cleared = 3,       // the host cleared the offer while a paste was open
  Released = 4,      // the data object went away (clipboard taken over) mid-paste
  Disconnected = 5,  // the pipe is gone; the helper is exiting
  Superseded = 6,    // a new StartOperation arrived while this one was still open
};

struct FileId {
  uint64_t volumeSerial = 0;
  std::array<uint8_t, 16> id{};
  bool operator==(const FileId& o) const { return volumeSerial == o.volumeSerial && id == o.id; }
  bool operator!=(const FileId& o) const { return !(*this == o); }
};

// What an offer says about one file, and what a paste confirms about it. `mtime` is a Windows
// FILETIME (100 ns since 1601) as a u64; `attributes` is the FILE_ATTRIBUTE_* word.
struct RemoteFileItem {
  std::u16string name;  // basename only; validate_remote_name() says what is accepted
  uint64_t size = 0;
  uint64_t mtime = 0;
  uint32_t attributes = 0;
};

struct StatEntry {
  Status status = Status::Ok;
  FileId id;
  uint64_t size = 0;
  uint64_t mtime = 0;
  uint32_t attributes = 0;
  std::u16string name;  // the basename the offer would carry (empty when status != Ok)
};

struct PinRequestEntry {
  std::u16string path;
  FileId expectedId;
  uint64_t expectedSize = 0;
  uint64_t expectedMtime = 0;
};

struct PinResultEntry {
  Status status = Status::Ok;
  FileId id;
  uint64_t size = 0;
  uint64_t mtime = 0;
};

struct Hello {
  std::array<uint8_t, kNonceBytes> nonce{};
  uint32_t pid = 0;
  uint16_t version = kPipeVersion;
};
struct PublishRemoteFiles {
  uint64_t offerId = 0;
  std::vector<RemoteFileItem> items;
};
struct PublishResult {
  uint64_t offerId = 0;
  Status status = Status::Ok;
  uint32_t count = 0;
};
struct ClearRemoteFiles {
  uint64_t offerId = 0;
};
struct PasteBegin {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
};
struct PasteDescriptor {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  Status status = Status::Ok;
  std::vector<RemoteFileItem> items;
};
struct ReadRequest {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  uint32_t fileIndex = 0;
  uint64_t offset = 0;
  uint32_t length = 0;
};
struct ReadData {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  uint32_t fileIndex = 0;
  uint64_t offset = 0;
  Status status = Status::Ok;
  std::vector<uint8_t> data;
};
struct PasteEnd {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  EndReason reason = EndReason::Ended;
};
struct StatFiles {
  uint64_t requestId = 0;
  std::vector<std::u16string> paths;
};
struct Stats {
  uint64_t requestId = 0;
  std::vector<StatEntry> entries;
};
struct Pin {
  uint64_t pinId = 0;
  uint32_t leaseMs = 0;
  std::vector<PinRequestEntry> entries;
};
struct PinResult {
  uint64_t pinId = 0;
  std::vector<PinResultEntry> entries;
};
struct ReadLocal {
  uint64_t pinId = 0;
  uint32_t fileIndex = 0;
  uint64_t offset = 0;
  uint32_t length = 0;
};
struct LocalData {
  uint64_t pinId = 0;
  uint32_t fileIndex = 0;
  uint64_t offset = 0;
  Status status = Status::Ok;
  std::vector<uint8_t> data;
};
struct Unpin {
  uint64_t pinId = 0;
};
struct Unpinned {
  uint64_t pinId = 0;
  uint32_t released = 0;
};

struct PipeFrame {
  PipeMsg type = PipeMsg::Hello;
  std::vector<uint8_t> payload;
};

// ------------------------------------------------------------------------------ byte codec

class ByteWriter {
 public:
  void u8(uint8_t v) { out_.push_back(v); }
  void u16(uint16_t v) {
    out_.push_back(static_cast<uint8_t>(v));
    out_.push_back(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) out_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void bytes(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    out_.insert(out_.end(), b, b + n);
  }
  // u32 count of UTF-16 units, then the units.
  void str16(const std::u16string& s) {
    u32(static_cast<uint32_t>(s.size()));
    for (char16_t c : s) u16(static_cast<uint16_t>(c));
  }
  void blob(const std::vector<uint8_t>& b) {
    u32(static_cast<uint32_t>(b.size()));
    bytes(b.data(), b.size());
  }
  void file_id(const FileId& f) {
    u64(f.volumeSerial);
    bytes(f.id.data(), f.id.size());
  }
  std::vector<uint8_t> take() { return std::move(out_); }
  const std::vector<uint8_t>& view() const { return out_; }

 private:
  std::vector<uint8_t> out_;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  explicit ByteReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}
  bool ok() const { return ok_; }
  bool done() const { return ok_ && pos_ == n_; }
  size_t remaining() const { return n_ - pos_; }
  bool u8(uint8_t* v) {
    if (!need(1)) return false;
    *v = p_[pos_++];
    return true;
  }
  bool u16(uint16_t* v) {
    if (!need(2)) return false;
    *v = static_cast<uint16_t>(p_[pos_] | (p_[pos_ + 1] << 8));
    pos_ += 2;
    return true;
  }
  bool u32(uint32_t* v) {
    if (!need(4)) return false;
    uint32_t r = 0;
    for (int i = 0; i < 4; ++i) r |= static_cast<uint32_t>(p_[pos_ + i]) << (8 * i);
    pos_ += 4;
    *v = r;
    return true;
  }
  bool u64(uint64_t* v) {
    if (!need(8)) return false;
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r |= static_cast<uint64_t>(p_[pos_ + i]) << (8 * i);
    pos_ += 8;
    *v = r;
    return true;
  }
  bool bytes(void* dst, size_t n) {
    if (!need(n)) return false;
    std::memcpy(dst, p_ + pos_, n);
    pos_ += n;
    return true;
  }
  bool str16(std::u16string* s, uint32_t maxUnits) {
    uint32_t count = 0;
    if (!u32(&count)) return false;
    if (count > maxUnits || !need(static_cast<size_t>(count) * 2)) return fail();
    s->resize(count);
    for (uint32_t i = 0; i < count; ++i) {
      uint16_t c = 0;
      u16(&c);
      (*s)[i] = static_cast<char16_t>(c);
    }
    return true;
  }
  bool blob(std::vector<uint8_t>* b, uint32_t maxBytes) {
    uint32_t count = 0;
    if (!u32(&count)) return false;
    if (count > maxBytes || !need(count)) return fail();
    b->assign(p_ + pos_, p_ + pos_ + count);
    pos_ += count;
    return true;
  }
  bool file_id(FileId* f) { return u64(&f->volumeSerial) && bytes(f->id.data(), f->id.size()); }

 private:
  bool need(size_t n) {
    if (!ok_ || n > n_ - pos_) return fail();
    return true;
  }
  bool fail() {
    ok_ = false;
    return false;
  }
  const uint8_t* p_;
  size_t n_;
  size_t pos_ = 0;
  bool ok_ = true;
};

// ------------------------------------------------------------------------------ frame

/** header + payload, ready for the pipe. False when the payload is over the bound. */
inline bool encode_frame(const PipeFrame& frame, std::vector<uint8_t>* wire) {
  if (frame.payload.size() > kMaxPayloadBytes) return false;
  ByteWriter w;
  w.u32(kPipeMagic);
  w.u32(static_cast<uint32_t>(frame.payload.size()));
  w.u16(static_cast<uint16_t>(frame.type));
  w.u16(kPipeVersion);
  w.u32(0);
  w.bytes(frame.payload.data(), frame.payload.size());
  *wire = w.take();
  return true;
}

/** The 16 header bytes. False on a bad magic / version / a length over the bound. */
inline bool decode_frame_header(const uint8_t* header16, PipeMsg* type, uint32_t* length) {
  ByteReader r(header16, kFrameHeaderBytes);
  uint32_t magic = 0, len = 0, reserved = 0;
  uint16_t t = 0, version = 0;
  if (!r.u32(&magic) || !r.u32(&len) || !r.u16(&t) || !r.u16(&version) || !r.u32(&reserved)) return false;
  if (magic != kPipeMagic || version != kPipeVersion || len > kMaxPayloadBytes) return false;
  *type = static_cast<PipeMsg>(t);
  *length = len;
  return true;
}

// ------------------------------------------------------------------------------ names

/**
 * Whether a basename may travel in an offer and land in a FILEDESCRIPTORW. Refuses separators,
 * a drive / stream colon, control characters, the characters Windows forbids in names, ".", "..",
 * a trailing space or dot, the reserved device names (with or without an extension), an empty
 * name, and anything over kMaxNameUnits. The host sanitises on its side too; this is the
 * receiving end's own check and the reason a bad offer never reaches OleSetClipboard.
 */
inline bool validate_remote_name(const std::u16string& name) {
  if (name.empty() || name.size() > kMaxNameUnits) return false;
  if (name == u"." || name == u"..") return false;
  for (char16_t c : name) {
    if (c < 0x20 || c == u'\\' || c == u'/' || c == u':' || c == u'*' || c == u'?' || c == u'"' ||
        c == u'<' || c == u'>' || c == u'|' || c == 0x7F) {
      return false;
    }
  }
  const char16_t last = name.back();
  if (last == u' ' || last == u'.') return false;
  // Reserved device names: the stem before the first '.' compared case-insensitively.
  std::u16string stem = name.substr(0, name.find(u'.'));
  for (auto& c : stem) {
    if (c >= u'a' && c <= u'z') c = static_cast<char16_t>(c - (u'a' - u'A'));
  }
  static const char16_t* const reserved[] = {u"CON", u"PRN", u"AUX", u"NUL", u"CONIN$", u"CONOUT$"};
  for (const char16_t* r : reserved) {
    if (stem == r) return false;
  }
  if (stem.size() == 4 && (stem.compare(0, 3, u"COM") == 0 || stem.compare(0, 3, u"LPT") == 0) &&
      stem[3] >= u'1' && stem[3] <= u'9') {
    return false;
  }
  return true;
}

// ------------------------------------------------------------------------------ messages

namespace detail {
inline void put_item(ByteWriter& w, const RemoteFileItem& it) {
  w.str16(it.name);
  w.u64(it.size);
  w.u64(it.mtime);
  w.u32(it.attributes);
}
inline bool get_item(ByteReader& r, RemoteFileItem* it) {
  return r.str16(&it->name, kMaxNameUnits) && r.u64(&it->size) && r.u64(&it->mtime) && r.u32(&it->attributes);
}
inline bool get_count(ByteReader& r, uint32_t maxCount, uint32_t* count) {
  if (!r.u32(count)) return false;
  return *count <= maxCount;
}
template <class T>
bool finish(const PipeFrame& f, PipeMsg expected, const ByteReader& r, T* /*unused*/) {
  return f.type == expected && r.done();
}
}  // namespace detail

inline PipeFrame encode(const Hello& m) {
  ByteWriter w;
  w.bytes(m.nonce.data(), m.nonce.size());
  w.u32(m.pid);
  w.u16(m.version);
  return {PipeMsg::Hello, w.take()};
}
inline bool decode(const PipeFrame& f, Hello* m) {
  if (f.type != PipeMsg::Hello) return false;
  ByteReader r(f.payload);
  return r.bytes(m->nonce.data(), m->nonce.size()) && r.u32(&m->pid) && r.u16(&m->version) && r.done();
}

inline PipeFrame encode_hello_ack() { return {PipeMsg::HelloAck, {}}; }
inline PipeFrame encode_shutdown() { return {PipeMsg::Shutdown, {}}; }

inline PipeFrame encode(const PublishRemoteFiles& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u32(static_cast<uint32_t>(m.items.size()));
  for (const auto& it : m.items) detail::put_item(w, it);
  return {PipeMsg::PublishRemoteFiles, w.take()};
}
inline bool decode(const PipeFrame& f, PublishRemoteFiles* m) {
  if (f.type != PipeMsg::PublishRemoteFiles) return false;
  ByteReader r(f.payload);
  uint32_t count = 0;
  if (!r.u64(&m->offerId) || !detail::get_count(r, kMaxFiles, &count)) return false;
  m->items.resize(count);
  for (auto& it : m->items) {
    if (!detail::get_item(r, &it)) return false;
  }
  return r.done();
}

inline PipeFrame encode(const PublishResult& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u16(static_cast<uint16_t>(m.status));
  w.u32(m.count);
  return {PipeMsg::PublishResult, w.take()};
}
inline bool decode(const PipeFrame& f, PublishResult* m) {
  if (f.type != PipeMsg::PublishResult) return false;
  ByteReader r(f.payload);
  uint16_t s = 0;
  if (!r.u64(&m->offerId) || !r.u16(&s) || !r.u32(&m->count)) return false;
  m->status = static_cast<Status>(s);
  return r.done();
}

inline PipeFrame encode(const ClearRemoteFiles& m) {
  ByteWriter w;
  w.u64(m.offerId);
  return {PipeMsg::ClearRemoteFiles, w.take()};
}
inline bool decode(const PipeFrame& f, ClearRemoteFiles* m) {
  if (f.type != PipeMsg::ClearRemoteFiles) return false;
  ByteReader r(f.payload);
  return r.u64(&m->offerId) && r.done();
}

inline PipeFrame encode(const PasteBegin& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  return {PipeMsg::PasteBegin, w.take()};
}
inline bool decode(const PipeFrame& f, PasteBegin* m) {
  if (f.type != PipeMsg::PasteBegin) return false;
  ByteReader r(f.payload);
  return r.u64(&m->offerId) && r.u64(&m->pasteOp) && r.done();
}

inline PipeFrame encode(const PasteDescriptor& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u16(static_cast<uint16_t>(m.status));
  w.u32(static_cast<uint32_t>(m.items.size()));
  for (const auto& it : m.items) detail::put_item(w, it);
  return {PipeMsg::PasteDescriptor, w.take()};
}
inline bool decode(const PipeFrame& f, PasteDescriptor* m) {
  if (f.type != PipeMsg::PasteDescriptor) return false;
  ByteReader r(f.payload);
  uint16_t s = 0;
  uint32_t count = 0;
  if (!r.u64(&m->offerId) || !r.u64(&m->pasteOp) || !r.u16(&s) || !detail::get_count(r, kMaxFiles, &count)) return false;
  m->status = static_cast<Status>(s);
  m->items.resize(count);
  for (auto& it : m->items) {
    if (!detail::get_item(r, &it)) return false;
  }
  return r.done();
}

inline PipeFrame encode(const ReadRequest& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u32(m.fileIndex);
  w.u64(m.offset);
  w.u32(m.length);
  return {PipeMsg::ReadRequest, w.take()};
}
inline bool decode(const PipeFrame& f, ReadRequest* m) {
  if (f.type != PipeMsg::ReadRequest) return false;
  ByteReader r(f.payload);
  if (!r.u64(&m->offerId) || !r.u64(&m->pasteOp) || !r.u32(&m->fileIndex) || !r.u64(&m->offset) || !r.u32(&m->length)) return false;
  return m->length <= kMaxChunkBytes && r.done();
}

inline PipeFrame encode(const ReadData& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u32(m.fileIndex);
  w.u64(m.offset);
  w.u16(static_cast<uint16_t>(m.status));
  w.blob(m.data);
  return {PipeMsg::ReadData, w.take()};
}
inline bool decode(const PipeFrame& f, ReadData* m) {
  if (f.type != PipeMsg::ReadData) return false;
  ByteReader r(f.payload);
  uint16_t s = 0;
  if (!r.u64(&m->offerId) || !r.u64(&m->pasteOp) || !r.u32(&m->fileIndex) || !r.u64(&m->offset) || !r.u16(&s) ||
      !r.blob(&m->data, kMaxChunkBytes)) {
    return false;
  }
  m->status = static_cast<Status>(s);
  return r.done();
}

inline PipeFrame encode(const PasteEnd& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u16(static_cast<uint16_t>(m.reason));
  return {PipeMsg::PasteEnd, w.take()};
}
inline bool decode(const PipeFrame& f, PasteEnd* m) {
  if (f.type != PipeMsg::PasteEnd) return false;
  ByteReader r(f.payload);
  uint16_t reason = 0;
  if (!r.u64(&m->offerId) || !r.u64(&m->pasteOp) || !r.u16(&reason)) return false;
  m->reason = static_cast<EndReason>(reason);
  return r.done();
}

inline PipeFrame encode(const StatFiles& m) {
  ByteWriter w;
  w.u64(m.requestId);
  w.u32(static_cast<uint32_t>(m.paths.size()));
  for (const auto& p : m.paths) w.str16(p);
  return {PipeMsg::StatFiles, w.take()};
}
inline bool decode(const PipeFrame& f, StatFiles* m) {
  if (f.type != PipeMsg::StatFiles) return false;
  ByteReader r(f.payload);
  uint32_t count = 0;
  if (!r.u64(&m->requestId) || !detail::get_count(r, kMaxStatCount, &count)) return false;
  m->paths.resize(count);
  for (auto& p : m->paths) {
    if (!r.str16(&p, kMaxPathUnits)) return false;
  }
  return r.done();
}

inline PipeFrame encode(const Stats& m) {
  ByteWriter w;
  w.u64(m.requestId);
  w.u32(static_cast<uint32_t>(m.entries.size()));
  for (const auto& e : m.entries) {
    w.u16(static_cast<uint16_t>(e.status));
    w.file_id(e.id);
    w.u64(e.size);
    w.u64(e.mtime);
    w.u32(e.attributes);
    w.str16(e.name);
  }
  return {PipeMsg::Stats, w.take()};
}
inline bool decode(const PipeFrame& f, Stats* m) {
  if (f.type != PipeMsg::Stats) return false;
  ByteReader r(f.payload);
  uint32_t count = 0;
  if (!r.u64(&m->requestId) || !detail::get_count(r, kMaxStatCount, &count)) return false;
  m->entries.resize(count);
  for (auto& e : m->entries) {
    uint16_t s = 0;
    if (!r.u16(&s) || !r.file_id(&e.id) || !r.u64(&e.size) || !r.u64(&e.mtime) || !r.u32(&e.attributes) ||
        !r.str16(&e.name, kMaxNameUnits)) {
      return false;
    }
    e.status = static_cast<Status>(s);
  }
  return r.done();
}

inline PipeFrame encode(const Pin& m) {
  ByteWriter w;
  w.u64(m.pinId);
  w.u32(m.leaseMs);
  w.u32(static_cast<uint32_t>(m.entries.size()));
  for (const auto& e : m.entries) {
    w.str16(e.path);
    w.file_id(e.expectedId);
    w.u64(e.expectedSize);
    w.u64(e.expectedMtime);
  }
  return {PipeMsg::Pin, w.take()};
}
inline bool decode(const PipeFrame& f, Pin* m) {
  if (f.type != PipeMsg::Pin) return false;
  ByteReader r(f.payload);
  uint32_t count = 0;
  if (!r.u64(&m->pinId) || !r.u32(&m->leaseMs) || !detail::get_count(r, kMaxFiles, &count)) return false;
  m->entries.resize(count);
  for (auto& e : m->entries) {
    if (!r.str16(&e.path, kMaxPathUnits) || !r.file_id(&e.expectedId) || !r.u64(&e.expectedSize) ||
        !r.u64(&e.expectedMtime)) {
      return false;
    }
  }
  return r.done();
}

inline PipeFrame encode(const PinResult& m) {
  ByteWriter w;
  w.u64(m.pinId);
  w.u32(static_cast<uint32_t>(m.entries.size()));
  for (const auto& e : m.entries) {
    w.u16(static_cast<uint16_t>(e.status));
    w.file_id(e.id);
    w.u64(e.size);
    w.u64(e.mtime);
  }
  return {PipeMsg::PinResult, w.take()};
}
inline bool decode(const PipeFrame& f, PinResult* m) {
  if (f.type != PipeMsg::PinResult) return false;
  ByteReader r(f.payload);
  uint32_t count = 0;
  if (!r.u64(&m->pinId) || !detail::get_count(r, kMaxFiles, &count)) return false;
  m->entries.resize(count);
  for (auto& e : m->entries) {
    uint16_t s = 0;
    if (!r.u16(&s) || !r.file_id(&e.id) || !r.u64(&e.size) || !r.u64(&e.mtime)) return false;
    e.status = static_cast<Status>(s);
  }
  return r.done();
}

inline PipeFrame encode(const ReadLocal& m) {
  ByteWriter w;
  w.u64(m.pinId);
  w.u32(m.fileIndex);
  w.u64(m.offset);
  w.u32(m.length);
  return {PipeMsg::ReadLocal, w.take()};
}
inline bool decode(const PipeFrame& f, ReadLocal* m) {
  if (f.type != PipeMsg::ReadLocal) return false;
  ByteReader r(f.payload);
  if (!r.u64(&m->pinId) || !r.u32(&m->fileIndex) || !r.u64(&m->offset) || !r.u32(&m->length)) return false;
  return m->length <= kMaxChunkBytes && r.done();
}

inline PipeFrame encode(const LocalData& m) {
  ByteWriter w;
  w.u64(m.pinId);
  w.u32(m.fileIndex);
  w.u64(m.offset);
  w.u16(static_cast<uint16_t>(m.status));
  w.blob(m.data);
  return {PipeMsg::LocalData, w.take()};
}
inline bool decode(const PipeFrame& f, LocalData* m) {
  if (f.type != PipeMsg::LocalData) return false;
  ByteReader r(f.payload);
  uint16_t s = 0;
  if (!r.u64(&m->pinId) || !r.u32(&m->fileIndex) || !r.u64(&m->offset) || !r.u16(&s) || !r.blob(&m->data, kMaxChunkBytes)) {
    return false;
  }
  m->status = static_cast<Status>(s);
  return r.done();
}

inline PipeFrame encode(const Unpin& m) {
  ByteWriter w;
  w.u64(m.pinId);
  return {PipeMsg::Unpin, w.take()};
}
inline bool decode(const PipeFrame& f, Unpin* m) {
  if (f.type != PipeMsg::Unpin) return false;
  ByteReader r(f.payload);
  return r.u64(&m->pinId) && r.done();
}

inline PipeFrame encode(const Unpinned& m) {
  ByteWriter w;
  w.u64(m.pinId);
  w.u32(m.released);
  return {PipeMsg::Unpinned, w.take()};
}
inline bool decode(const PipeFrame& f, Unpinned* m) {
  if (f.type != PipeMsg::Unpinned) return false;
  ByteReader r(f.payload);
  return r.u64(&m->pinId) && r.u32(&m->released) && r.done();
}

// ------------------------------------------------------------------------------ hex

inline std::wstring hex_encode(const uint8_t* bytes, size_t n) {
  static const wchar_t* digits = L"0123456789abcdef";
  std::wstring s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    s.push_back(digits[bytes[i] >> 4]);
    s.push_back(digits[bytes[i] & 0xF]);
  }
  return s;
}

/** Exactly 2*n hex digits into `out`; false otherwise. */
inline bool hex_decode(const std::wstring& s, uint8_t* out, size_t n) {
  if (s.size() != n * 2) return false;
  auto nib = [](wchar_t c, uint8_t* v) {
    if (c >= L'0' && c <= L'9') { *v = static_cast<uint8_t>(c - L'0'); return true; }
    if (c >= L'a' && c <= L'f') { *v = static_cast<uint8_t>(c - L'a' + 10); return true; }
    if (c >= L'A' && c <= L'F') { *v = static_cast<uint8_t>(c - L'A' + 10); return true; }
    return false;
  };
  for (size_t i = 0; i < n; ++i) {
    uint8_t hi = 0, lo = 0;
    if (!nib(s[2 * i], &hi) || !nib(s[2 * i + 1], &lo)) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

inline std::wstring pipe_name_for(const std::array<uint8_t, kPipeNameRandomBytes>& random) {
  return std::wstring(kPipeNamePrefix) + hex_encode(random.data(), random.size());
}

inline const char* status_name(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::NotFound: return "not-found";
    case Status::AccessDenied: return "access-denied";
    case Status::SharingViolation: return "sharing-violation";
    case Status::NotAFile: return "not-a-file";
    case Status::Excluded: return "excluded";
    case Status::Replaced: return "replaced";
    case Status::Changed: return "changed";
    case Status::LeaseExpired: return "lease-expired";
    case Status::UnknownId: return "unknown-id";
    case Status::ReadError: return "read-error";
    case Status::TooMany: return "too-many";
    case Status::BadPath: return "bad-path";
    case Status::Aborted: return "aborted";
    case Status::Timeout: return "timeout";
    case Status::Refused: return "refused";
    case Status::BadRequest: return "bad-request";
  }
  return "?";
}

inline const char* end_reason_name(EndReason r) {
  switch (r) {
    case EndReason::Ended: return "ended";
    case EndReason::Error: return "error";
    case EndReason::Idle: return "idle";
    case EndReason::Cleared: return "cleared";
    case EndReason::Released: return "released";
    case EndReason::Disconnected: return "disconnected";
    case EndReason::Superseded: return "superseded";
  }
  return "?";
}

}  // namespace remote60::native_poc::file_copy
