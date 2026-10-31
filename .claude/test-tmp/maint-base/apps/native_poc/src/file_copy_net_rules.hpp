#pragma once

// file-copy network rules: what an offer may say, which ranges a pull may ask for, whether a chunk
// is the one asked for, and when "whole file verified" may be said. (t-zdmsd4gb r1, debate D2 / D3)
//
// The same functions run on both ends: the side that sends an offer checks it before sending, and
// the side that receives it checks it again before it reaches the helper / the clipboard. Win32 only
// for the two things a pure header should not do: the case-insensitive name comparison the file
// system itself uses (CompareStringOrdinal) and SHA-256 (BCrypt).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "file_copy_wire.hpp"

namespace remote60::native_poc::file_copy::net {

/**
 * `offset` and `length` name bytes inside a file of `size` bytes, and `length` is within a chunk:
 * length <= maxLength, offset <= size, length <= size - offset. Written so nothing can overflow.
 */
inline bool range_ok(uint64_t size, uint64_t offset, uint64_t length, uint64_t maxLength) {
  return length <= maxLength && offset <= size && length <= size - offset;
}

/**
 * The offer rules, in the order they are checked: at most kMaxOfferFiles items; every name a valid
 * basename (validate_remote_name: separators, stream colon, device / reserved names, control
 * characters, trailing space or dot -- "a..b" is an ordinary name, only "." and ".." are refused);
 * no two names equal ignoring case (they would land on one file); no index twice; the sizes summing
 * to at most kMaxOfferTotalBytes without overflowing. `*why` names the first rule broken.
 */
Verdict check_offer_items(const std::vector<OfferItem>& items, std::string* why);

/** SHA-256 of `n` bytes. False only if the platform refuses. */
bool sha256(const uint8_t* p, size_t n, std::array<uint8_t, kShaBytes>* out);

/** What was wrong with a chunk (Ok = it is exactly the bytes asked for). */
enum class ChunkCheck : uint8_t {
  Ok = 0,
  WrongIdentity,  // epoch / offer / paste / generation / file / request id / offset: not this pull
  WrongLength,    // not the length asked for
  WrongHash,      // the bytes are not the ones the sender hashed ("전송 데이터 검증 실패")
};

/** Checks a chunk against the pull it answers. Nothing of a failed chunk may be handed on. */
ChunkCheck check_chunk(const Pull& asked, const Chunk& got);

/**
 * "Whole file verified" (the counters' name; 검증용 결론, r2 ⑦ wording): the verified chunks KEPT for a
 * file covered 0..size exactly once, ascending and contiguous -- i.e. every chunk of the whole range
 * passed its own SHA-256 check. It is NOT a SHA-256 of the whole file (none is computed or compared),
 * and it cannot tell one reading stream from several: it says only that the kept chunks were
 * contiguous. A Seek, a repeated range or a gap makes it "chunk verified" -- every byte handed on was
 * still checked. An empty file counts once its size 0 is confirmed.
 */
class CoverageTracker {
 public:
  explicit CoverageTracker(uint64_t size) : size_(size) {}
  void OnVerified(uint64_t offset, uint64_t length) {
    if (!sequential_) return;
    if (offset != next_) {
      sequential_ = false;
      return;
    }
    next_ += length;
  }
  bool whole_file_verified() const { return sequential_ && next_ == size_; }
  bool sequential() const { return sequential_; }

 private:
  uint64_t size_ = 0;
  uint64_t next_ = 0;
  bool sequential_ = true;
};

}  // namespace remote60::native_poc::file_copy::net
