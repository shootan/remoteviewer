#pragma once

// file-copy network messages: control 65~76 (viewer request -> host answer) and bulk 77/78.
// (t-zdmsd4gb r1; agreement: file_copy_network_debate_2026-09-28.md "Codex 반론/합의" + "검증용 결론")
//
// PURE. No Win32, no I/O: the codec is unit-tested on its own, and the viewer and the host read one
// definition. The body of every message is written field by field, little-endian (file_copy_pipe.hpp's
// ByteWriter / ByteReader -- no struct casts on the wire), and a decoder refuses anything it does not
// understand, anything past a bound, and any body with bytes left over.
//
//   control message = FileControlHeader(20) + body(payloadBytes <= kMaxControlPayload)
//   bulk message    = MessageHeader(8) + body          (one UdpControlChannel message on the bulk stream)
//
// What a message carries names, never paths: an offer lists basenames with sizes and times, and
// every later message names a file by its index in that offer. The side that owns the files maps an
// index back to its own path / pinned handle; nothing on the wire can point the other side at a
// file it did not offer (debate "공통 상태·신뢰 경계").
//
// Integrity (debate D3, 검증용 결론 "최소안"): every FileChunk carries the SHA-256 of ITS bytes, and
// the receiving side checks length, range and hash before those bytes are returned to anyone. A
// mismatch fails that Read ("전송 데이터 검증 실패", not "원본 바뀜"). "Whole file verified" is a
// separate, narrower claim -- every chunk of the whole range checked, not a whole-file SHA
// (file_copy_net_rules.hpp). None of this authenticates a plaintext peer
// (A3): the hash detects transfer errors, nothing more.

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "file_copy_pipe.hpp"
#include "poc_protocol.hpp"

namespace remote60::native_poc::file_copy::net {

using remote60::native_poc::file_copy::ByteReader;
using remote60::native_poc::file_copy::ByteWriter;

// Message numbers (reserved by the verifier: 65~79 file-copy, Pong bit 0x800). Values are on the
// wire; poc_protocol.hpp's MessageType carries the same numbers under the same names.
enum class FileMsg : uint16_t {
  Offer = 65,              // V->H  P->R: these files can be pasted on the remote PC
  OfferReply = 66,         // H->V
  OfferQuery = 67,         // V->H  R->P: is there a (new) remote offer?        (2단계)
  OfferQueryReply = 68,    // H->V
  PasteQuery = 69,         // V->H  P->R: has a paste of my offer begun / ended?
  PasteQueryReply = 70,    // H->V
  Prepare = 71,            // V->H  P->R: the pinned, confirmed descriptor / R->P: pin yours
  PrepareReply = 72,       // H->V
  End = 73,                // V->H  withdraw an offer / end a paste
  EndReply = 74,           // H->V
  Status = 75,             // V->H  where a paste stands
  StatusReply = 76,        // H->V
  Pull = 77,               // bulk, receiver -> sender: send me [offset, offset + len) of file i
  Chunk = 78,              // bulk, sender -> receiver: those bytes and their SHA-256
};

constexpr uint32_t kMaxControlPayload = 64u * 1024u;   // an offer of 100 max-length names fits
constexpr uint32_t kMaxFileChunkBytes = 64u * 1024u;    // one bulk message (the image path's max chunk)
constexpr uint32_t kMaxOfferFiles = kMaxFiles;          // 100 (file_copy_pipe.hpp)
constexpr uint64_t kMaxOfferTotalBytes = 4ull << 30;    // 4 GiB, summed with overflow checks
constexpr uint64_t kNoTrigger = ~0ull;                  // a pull no chunk released (the opening ones)
constexpr size_t kShaBytes = 32;

enum class Direction : uint8_t {
  PtoR = 1,  // the viewer's files, pasted on the remote PC (the viewer serves the bytes)
  RtoP = 2,  // the remote PC's files, pasted on the viewer's PC (the host serves the bytes)
};

enum class Verdict : uint8_t {
  Accept = 0,
  Busy = 1,                // the session's bulk is taken (image / another paste) -- never swapped silently
  TooMany = 2,             // over kMaxOfferFiles
  TooLarge = 3,            // over kMaxOfferTotalBytes
  Disabled = 4,            // file copy is switched off (file_copy_allowed)
  HelperUnavailable = 5,   // the Medium helper cannot run as the interactive user (no fallback)
  BadRequest = 6,          // names / indices / ranges the rules refuse
  UnknownId = 7,           // no such offer / paste (withdrawn, replaced, another session)
  StaleEpoch = 8,          // a message of an earlier session
};

// Where a paste stands, as the host sees it (PasteQueryReply / StatusReply).
enum class PasteState : uint8_t {
  None = 0,       // no paste of this offer has begun
  Begun = 1,      // a consumer started one: prepare it (P->R)
  Active = 2,     // prepared; bytes are moving
  Ended = 3,      // the consumer ended it (EndOperation) -- see the reason
  Failed = 4,     // ended with an error -- see the reason
  Withdrawn = 5,  // the offer is gone (cleared, replaced by a newer offer, the session ended)
};

// Why a paste ended (the helper's EndReason plus what the network adds).
enum class PasteEndReason : uint8_t {
  None = 0,
  Completed = 1,     // the consumer ended the operation successfully
  ConsumerError = 2, // the consumer ended it with a failure, or a Read failed
  Idle = 3,          // no progress for the idle bound (A4)
  Cancelled = 4,     // the user cancelled
  Replaced = 5,      // a source file is not the one offered (FileId)
  Changed = 6,       // a source file's size / time moved
  InUse = 7,         // a writer holds a source file
  Verification = 8,  // a chunk failed its length / range / SHA check
  Disabled = 9,      // switched off meanwhile (A2)
  Session = 10,      // the session ended
  Superseded = 11,   // a new StartOperation replaced it
};

#pragma pack(push, 1)
struct FileControlHeader {
  MessageHeader header{};  // type = FileMsg, size = sizeof(FileControlHeader)
  uint32_t seq = 0;        // the request's; the answer echoes it
  uint32_t payloadBytes = 0;
};
#pragma pack(pop)
static_assert(sizeof(FileControlHeader) == sizeof(MessageHeader) + 8, "file control header drift");

// ------------------------------------------------------------------------------ bodies

struct OfferItem {
  uint32_t index = 0;        // the offering side's own index (its path / handle table)
  std::u16string name;       // basename (validate_remote_name + the list rules)
  uint64_t size = 0;
  uint64_t mtime = 0;        // FILETIME as u64
  uint32_t attributes = 0;
};

struct Offer {
  uint64_t epochTag = 0;     // the session the offer belongs to
  uint64_t offerId = 0;
  uint64_t revision = 0;     // the offering side's clipboard revision (newest wins)
  std::vector<OfferItem> items;
};
struct OfferReply {
  uint64_t offerId = 0;
  Verdict verdict = Verdict::Accept;
};

struct OfferQuery {                // R->P (2단계)
  uint64_t knownRevision = 0;
};
struct OfferQueryReply {
  uint64_t epochTag = 0;
  uint64_t revision = 0;           // 0 = no offer
  bool unchanged = true;           // revision == knownRevision: no list follows
  uint64_t offerId = 0;
  std::vector<OfferItem> items;
};

struct PasteQuery {
  uint64_t offerId = 0;
};
struct PasteQueryReply {
  uint64_t offerId = 0;
  PasteState state = PasteState::None;
  uint64_t pasteOp = 0;            // the paste that began (Begun) or the one reported
  PasteEndReason reason = PasteEndReason::None;
};

struct PreparedItem {
  uint32_t index = 0;
  uint16_t status = 0;             // file_copy::Status of the pin
  uint64_t size = 0;
  uint64_t mtime = 0;
  uint32_t attributes = 0;
};
struct Prepare {
  Direction direction = Direction::PtoR;
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  std::vector<PreparedItem> items; // P->R: what the viewer pinned; R->P: empty (the host pins)
};
struct PrepareReply {
  Direction direction = Direction::PtoR;
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  Verdict verdict = Verdict::Accept;
  uint64_t epochTag = 0;           // quoted back on every bulk message of this paste
  uint32_t bulkGen = 0;            // the bulk stream generation of this paste
  std::vector<PreparedItem> items; // R->P: what the host pinned; P->R: empty
};

struct End {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;            // 0 = the whole offer (withdraw)
  PasteEndReason reason = PasteEndReason::None;
};
struct EndReply {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  PasteState state = PasteState::None;
};

struct StatusQuery {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
};
struct StatusReply {
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  PasteState state = PasteState::None;
  PasteEndReason reason = PasteEndReason::None;
  uint64_t bytesDelivered = 0;     // verified bytes handed to the consumer so far
};

// Bulk. Every identifier is checked by the receiving side before anything is used.
struct Pull {
  uint64_t epochTag = 0;
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  uint32_t bulkGen = 0;
  uint32_t fileIndex = 0;
  uint64_t requestId = 0;          // unique within the paste; a resend is the same id, same bytes
  uint64_t offset = 0;
  uint32_t length = 0;
  uint64_t triggerRequestId = kNoTrigger;  // the chunk whose arrival released this pull (RTT sample)
};
struct Chunk {
  uint64_t epochTag = 0;
  uint64_t offerId = 0;
  uint64_t pasteOp = 0;
  uint32_t bulkGen = 0;
  uint32_t fileIndex = 0;
  uint64_t requestId = 0;
  uint64_t offset = 0;
  std::array<uint8_t, kShaBytes> sha256{};  // of `data`
  std::vector<uint8_t> data;
};

// ------------------------------------------------------------------------------ codec

namespace detail {
inline void put_items(ByteWriter& w, const std::vector<OfferItem>& items) {
  w.u32(static_cast<uint32_t>(items.size()));
  for (const OfferItem& it : items) {
    w.u32(it.index);
    w.str16(it.name);
    w.u64(it.size);
    w.u64(it.mtime);
    w.u32(it.attributes);
  }
}
inline bool get_items(ByteReader& r, std::vector<OfferItem>* items) {
  uint32_t n = 0;
  if (!r.u32(&n) || n > kMaxOfferFiles) return false;
  items->resize(n);
  for (OfferItem& it : *items) {
    if (!r.u32(&it.index) || !r.str16(&it.name, kMaxNameUnits) || !r.u64(&it.size) || !r.u64(&it.mtime) ||
        !r.u32(&it.attributes)) {
      return false;
    }
  }
  return true;
}
inline void put_prepared(ByteWriter& w, const std::vector<PreparedItem>& items) {
  w.u32(static_cast<uint32_t>(items.size()));
  for (const PreparedItem& it : items) {
    w.u32(it.index);
    w.u16(it.status);
    w.u64(it.size);
    w.u64(it.mtime);
    w.u32(it.attributes);
  }
}
inline bool get_prepared(ByteReader& r, std::vector<PreparedItem>* items) {
  uint32_t n = 0;
  if (!r.u32(&n) || n > kMaxOfferFiles) return false;
  items->resize(n);
  for (PreparedItem& it : *items) {
    if (!r.u32(&it.index) || !r.u16(&it.status) || !r.u64(&it.size) || !r.u64(&it.mtime) || !r.u32(&it.attributes)) {
      return false;
    }
  }
  return true;
}
template <class E>
inline bool get_enum(ByteReader& r, E* e, uint8_t maxValue) {
  uint8_t v = 0;
  if (!r.u8(&v) || v > maxValue) return false;
  *e = static_cast<E>(v);
  return true;
}
inline bool get_direction(ByteReader& r, Direction* d) {
  uint8_t v = 0;
  if (!r.u8(&v) || (v != 1 && v != 2)) return false;
  *d = static_cast<Direction>(v);
  return true;
}
}  // namespace detail

inline std::vector<uint8_t> body(const Offer& m) {
  ByteWriter w;
  w.u64(m.epochTag);
  w.u64(m.offerId);
  w.u64(m.revision);
  detail::put_items(w, m.items);
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, Offer* m) {
  ByteReader r(b);
  return r.u64(&m->epochTag) && r.u64(&m->offerId) && r.u64(&m->revision) && detail::get_items(r, &m->items) && r.done();
}

inline std::vector<uint8_t> body(const OfferReply& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u8(static_cast<uint8_t>(m.verdict));
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, OfferReply* m) {
  ByteReader r(b);
  return r.u64(&m->offerId) && detail::get_enum(r, &m->verdict, 8) && r.done();
}

inline std::vector<uint8_t> body(const OfferQuery& m) {
  ByteWriter w;
  w.u64(m.knownRevision);
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, OfferQuery* m) {
  ByteReader r(b);
  return r.u64(&m->knownRevision) && r.done();
}

inline std::vector<uint8_t> body(const OfferQueryReply& m) {
  ByteWriter w;
  w.u64(m.epochTag);
  w.u64(m.revision);
  w.u8(m.unchanged ? 1 : 0);
  if (!m.unchanged) {
    w.u64(m.offerId);
    detail::put_items(w, m.items);
  }
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, OfferQueryReply* m) {
  ByteReader r(b);
  uint8_t unchanged = 0;
  if (!r.u64(&m->epochTag) || !r.u64(&m->revision) || !r.u8(&unchanged) || unchanged > 1) return false;
  m->unchanged = unchanged == 1;
  m->offerId = 0;
  m->items.clear();
  if (!m->unchanged && (!r.u64(&m->offerId) || !detail::get_items(r, &m->items))) return false;
  return r.done();
}

inline std::vector<uint8_t> body(const PasteQuery& m) {
  ByteWriter w;
  w.u64(m.offerId);
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, PasteQuery* m) {
  ByteReader r(b);
  return r.u64(&m->offerId) && r.done();
}

inline std::vector<uint8_t> body(const PasteQueryReply& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u8(static_cast<uint8_t>(m.state));
  w.u64(m.pasteOp);
  w.u8(static_cast<uint8_t>(m.reason));
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, PasteQueryReply* m) {
  ByteReader r(b);
  return r.u64(&m->offerId) && detail::get_enum(r, &m->state, 5) && r.u64(&m->pasteOp) &&
         detail::get_enum(r, &m->reason, 11) && r.done();
}

inline std::vector<uint8_t> body(const Prepare& m) {
  ByteWriter w;
  w.u8(static_cast<uint8_t>(m.direction));
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  detail::put_prepared(w, m.items);
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, Prepare* m) {
  ByteReader r(b);
  return detail::get_direction(r, &m->direction) && r.u64(&m->offerId) && r.u64(&m->pasteOp) &&
         detail::get_prepared(r, &m->items) && r.done();
}

inline std::vector<uint8_t> body(const PrepareReply& m) {
  ByteWriter w;
  w.u8(static_cast<uint8_t>(m.direction));
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u8(static_cast<uint8_t>(m.verdict));
  w.u64(m.epochTag);
  w.u32(m.bulkGen);
  detail::put_prepared(w, m.items);
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, PrepareReply* m) {
  ByteReader r(b);
  return detail::get_direction(r, &m->direction) && r.u64(&m->offerId) && r.u64(&m->pasteOp) &&
         detail::get_enum(r, &m->verdict, 8) && r.u64(&m->epochTag) && r.u32(&m->bulkGen) &&
         detail::get_prepared(r, &m->items) && r.done();
}

inline std::vector<uint8_t> body(const End& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u8(static_cast<uint8_t>(m.reason));
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, End* m) {
  ByteReader r(b);
  return r.u64(&m->offerId) && r.u64(&m->pasteOp) && detail::get_enum(r, &m->reason, 11) && r.done();
}

inline std::vector<uint8_t> body(const EndReply& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u8(static_cast<uint8_t>(m.state));
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, EndReply* m) {
  ByteReader r(b);
  return r.u64(&m->offerId) && r.u64(&m->pasteOp) && detail::get_enum(r, &m->state, 5) && r.done();
}

inline std::vector<uint8_t> body(const StatusQuery& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, StatusQuery* m) {
  ByteReader r(b);
  return r.u64(&m->offerId) && r.u64(&m->pasteOp) && r.done();
}

inline std::vector<uint8_t> body(const StatusReply& m) {
  ByteWriter w;
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u8(static_cast<uint8_t>(m.state));
  w.u8(static_cast<uint8_t>(m.reason));
  w.u64(m.bytesDelivered);
  return w.take();
}
inline bool parse(const std::vector<uint8_t>& b, StatusReply* m) {
  ByteReader r(b);
  return r.u64(&m->offerId) && r.u64(&m->pasteOp) && detail::get_enum(r, &m->state, 5) &&
         detail::get_enum(r, &m->reason, 11) && r.u64(&m->bytesDelivered) && r.done();
}

// ---- control framing

/** A whole control message: header + body. False when the body is over kMaxControlPayload. */
inline bool frame_control(FileMsg type, uint32_t seq, const std::vector<uint8_t>& b, std::vector<uint8_t>* out) {
  if (b.size() > kMaxControlPayload) return false;
  FileControlHeader h{};
  h.header.type = static_cast<uint16_t>(type);
  h.header.size = sizeof(FileControlHeader);
  h.seq = seq;
  h.payloadBytes = static_cast<uint32_t>(b.size());
  out->resize(sizeof(h) + b.size());
  std::memcpy(out->data(), &h, sizeof(h));
  if (!b.empty()) std::memcpy(out->data() + sizeof(h), b.data(), b.size());
  return true;
}

/** Whether `type` is one of the file-copy control messages (65~76). */
inline bool is_file_control(uint16_t type) { return type >= 65 && type <= 76; }

// ---- bulk framing: MessageHeader + body, one bulk-channel message

inline std::vector<uint8_t> frame_bulk(const Pull& m) {
  ByteWriter w;
  MessageHeader h{};
  h.type = static_cast<uint16_t>(FileMsg::Pull);
  h.size = sizeof(MessageHeader);
  w.bytes(&h, sizeof(h));
  w.u64(m.epochTag);
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u32(m.bulkGen);
  w.u32(m.fileIndex);
  w.u64(m.requestId);
  w.u64(m.offset);
  w.u32(m.length);
  w.u64(m.triggerRequestId);
  return w.take();
}
inline std::vector<uint8_t> frame_bulk(const Chunk& m) {
  ByteWriter w;
  MessageHeader h{};
  h.type = static_cast<uint16_t>(FileMsg::Chunk);
  h.size = sizeof(MessageHeader);
  w.bytes(&h, sizeof(h));
  w.u64(m.epochTag);
  w.u64(m.offerId);
  w.u64(m.pasteOp);
  w.u32(m.bulkGen);
  w.u32(m.fileIndex);
  w.u64(m.requestId);
  w.u64(m.offset);
  w.bytes(m.sha256.data(), m.sha256.size());
  w.blob(m.data);
  return w.take();
}

/** The type of a bulk message, or 0 when it is not a well-formed file-copy bulk message header. */
inline uint16_t bulk_type(const uint8_t* p, size_t n) {
  if (n < sizeof(MessageHeader)) return 0;
  MessageHeader h{};
  std::memcpy(&h, p, sizeof(h));
  if (h.magic != kMagic || h.size != sizeof(MessageHeader)) return 0;
  if (h.type != static_cast<uint16_t>(FileMsg::Pull) && h.type != static_cast<uint16_t>(FileMsg::Chunk)) return 0;
  return h.type;
}
inline bool parse_bulk(const uint8_t* p, size_t n, Pull* m) {
  if (bulk_type(p, n) != static_cast<uint16_t>(FileMsg::Pull)) return false;
  ByteReader r(p + sizeof(MessageHeader), n - sizeof(MessageHeader));
  return r.u64(&m->epochTag) && r.u64(&m->offerId) && r.u64(&m->pasteOp) && r.u32(&m->bulkGen) &&
         r.u32(&m->fileIndex) && r.u64(&m->requestId) && r.u64(&m->offset) && r.u32(&m->length) &&
         r.u64(&m->triggerRequestId) && r.done() && m->length <= kMaxFileChunkBytes;
}
inline bool parse_bulk(const uint8_t* p, size_t n, Chunk* m) {
  if (bulk_type(p, n) != static_cast<uint16_t>(FileMsg::Chunk)) return false;
  ByteReader r(p + sizeof(MessageHeader), n - sizeof(MessageHeader));
  return r.u64(&m->epochTag) && r.u64(&m->offerId) && r.u64(&m->pasteOp) && r.u32(&m->bulkGen) &&
         r.u32(&m->fileIndex) && r.u64(&m->requestId) && r.u64(&m->offset) && r.bytes(m->sha256.data(), kShaBytes) &&
         r.blob(&m->data, kMaxFileChunkBytes) && r.done();
}

}  // namespace remote60::native_poc::file_copy::net
