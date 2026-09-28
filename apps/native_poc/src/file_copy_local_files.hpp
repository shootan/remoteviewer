#pragma once

// The helper's own files: what the host's CF_HDROP names, identified, opened and read AS THE USER.
// (file-copy-helper r1, contract ⑤ / ⑧)
//
// Role:    stat_source_file identifies one path (FileId, size, time, attributes) and classifies
//          what this feature will never copy -- directories, .lnk / .url, reparse points, device
//          and stream syntax. LocalFileTable holds the files a paste has PINNED: opened with
//          GENERIC_READ + FILE_SHARE_READ so a writer cannot change, rename or delete them for the
//          duration, checked against the FileId the offer named (a replaced file is refused), and
//          released by Unpin, by the lease running out, or by ReleaseAll at shutdown.
// Thread:  the helper's pipe reader thread owns the table; nothing else touches it.
// Privilege: this process IS the interactive user (Medium). There is no impersonation and no
//          privileged retry: an open that fails is the answer (Codex ⑤ -- AccessCheck-then-
//          privileged-open is the confused deputy this exists to avoid).
//
// The paste semantics are "the content at the moment the paste started" (3차 합의 ③), not a
// snapshot from Ctrl+C: Pin is called when a paste begins, and the lease is the bound on how long
// a paste may keep the user's file locked if nothing ever says it ended.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "file_copy_pipe.hpp"

namespace remote60::native_poc::file_copy {

/**
 * What the path text alone says. BadPath for device / object-manager / stream syntax and an
 * empty path; Excluded for a .lnk / .url; Ok otherwise (the open decides the rest).
 * "\\?\" and "\\.\" prefixes are refused: the host hands over paths as the shell put them on the
 * clipboard, which are ordinary DOS paths, and the object-manager forms are how a caller reaches
 * devices and volumes by name.
 */
Status classify_source_path(const std::wstring& path);

/** The basename of a path (after the last separator). */
std::u16string basename_of(const std::wstring& path);

/**
 * Identifies one file without holding it: opens with every share mode (a writer elsewhere is not
 * a reason the file cannot be OFFERED -- Pin decides at paste time), reads FileId / size / time /
 * attributes, closes. Directories, reparse points, non-disk files and the classify_source_path
 * refusals come back with the matching status and no identity.
 */
StatEntry stat_source_file(const std::wstring& path);

class LocalFileTable {
 public:
  using NowMs = std::function<uint64_t()>;
  explicit LocalFileTable(NowMs now = nullptr);
  ~LocalFileTable();
  LocalFileTable(const LocalFileTable&) = delete;
  LocalFileTable& operator=(const LocalFileTable&) = delete;

  /**
   * Opens every entry for a paste: GENERIC_READ + FILE_SHARE_READ (a writer already there means
   * SharingViolation), then the handle's FileId must equal `expectedId` (else Replaced) and, when
   * an expected size / time was given (non-zero), match them (else Changed). A refused entry
   * holds nothing; the others are held under one lease. A pinId already in use is released first.
   * Returns false only for a malformed request (too many entries).
   */
  bool Pin(uint64_t pinId, uint32_t leaseMs, const std::vector<PinRequestEntry>& entries,
           std::vector<PinResultEntry>* results);

  /**
   * A positional read from a pinned file. Refreshes the lease. Fewer bytes than asked only at the
   * end of the file. BadRequest for an index / offset outside the item or a length over the chunk
   * bound; LeaseExpired when the lease ran out (the handles are then closed).
   */
  Status Read(uint64_t pinId, uint32_t fileIndex, uint64_t offset, uint32_t length, std::vector<uint8_t>* out);

  /** Closes a pin's handles. Returns how many were held. */
  uint32_t Unpin(uint64_t pinId);

  /** Closes every pin whose lease has run out. Returns how many handles were released. */
  uint32_t ExpireLeases();

  /** Closes everything (shutdown, disconnect). */
  void ReleaseAll();

  size_t pinned_handles() const;
  size_t pin_count() const { return pins_.size(); }

 private:
  struct PinnedFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    uint64_t size = 0;
  };
  struct PinSet {
    std::vector<PinnedFile> files;
    uint32_t leaseMs = 0;
    uint64_t deadlineMs = 0;
  };
  uint32_t release(PinSet& set);

  NowMs now_;
  std::map<uint64_t, PinSet> pins_;
};

}  // namespace remote60::native_poc::file_copy
