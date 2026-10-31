// See file_copy_net_rules.hpp.

#include "file_copy_net_rules.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <set>

namespace remote60::native_poc::file_copy::net {

Verdict check_offer_items(const std::vector<OfferItem>& items, std::string* why) {
  auto refuse = [&](Verdict v, const std::string& w) {
    if (why) *why = w;
    return v;
  };
  if (items.size() > kMaxOfferFiles) return refuse(Verdict::TooMany, "more than " + std::to_string(kMaxOfferFiles) + " files");
  std::set<uint32_t> indices;
  uint64_t total = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    const OfferItem& it = items[i];
    if (!validate_remote_name(it.name)) return refuse(Verdict::BadRequest, "item " + std::to_string(i) + ": not a basename");
    if (!indices.insert(it.index).second) return refuse(Verdict::BadRequest, "item " + std::to_string(i) + ": index twice");
    for (size_t j = 0; j < i; ++j) {
      // The file system's own case rule (ordinal, ignoring case): two such names are one file.
      if (CompareStringOrdinal(reinterpret_cast<const wchar_t*>(it.name.data()), static_cast<int>(it.name.size()),
                               reinterpret_cast<const wchar_t*>(items[j].name.data()),
                               static_cast<int>(items[j].name.size()), TRUE) == CSTR_EQUAL) {
        return refuse(Verdict::BadRequest, "items " + std::to_string(j) + " and " + std::to_string(i) + ": same name");
      }
    }
    if (it.size > kMaxOfferTotalBytes - total) return refuse(Verdict::TooLarge, "over the total size limit");
    total += it.size;
  }
  if (why) why->clear();
  return Verdict::Accept;
}

bool sha256(const uint8_t* p, size_t n, std::array<uint8_t, kShaBytes>* out) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
  // BCryptHashData takes a ULONG length: feed in pieces so nothing over 4 GiB is truncated.
  size_t done = 0;
  while (ok && done < n) {
    const ULONG piece = static_cast<ULONG>((std::min<size_t>)(n - done, 1u << 30));
    ok = BCryptHashData(hash, const_cast<PUCHAR>(p + done), piece, 0) == 0;
    done += piece;
  }
  ok = ok && BCryptFinishHash(hash, out->data(), static_cast<ULONG>(out->size()), 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

ChunkCheck check_chunk(const Pull& asked, const Chunk& got) {
  if (got.epochTag != asked.epochTag || got.offerId != asked.offerId || got.pasteOp != asked.pasteOp ||
      got.bulkGen != asked.bulkGen || got.fileIndex != asked.fileIndex || got.requestId != asked.requestId ||
      got.offset != asked.offset) {
    return ChunkCheck::WrongIdentity;
  }
  if (got.data.size() != asked.length) return ChunkCheck::WrongLength;
  std::array<uint8_t, kShaBytes> h{};
  if (!sha256(got.data.data(), got.data.size(), &h) || h != got.sha256) return ChunkCheck::WrongHash;
  return ChunkCheck::Ok;
}

}  // namespace remote60::native_poc::file_copy::net
