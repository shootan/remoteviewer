#pragma once

// Clipboard image v1: the OS clipboard side (Windows only), and the package digest.
//
// Role:    clip_image_read_snapshot -- copies what an image copy put on the clipboard (the "PNG"
//          format first, then CF_DIBV5, then CF_DIB; plus same-revision CF_UNICODETEXT) and closes
//          the clipboard at once: nothing is encoded or sent while it is open (plan r1 ⑸).
//          clip_image_publish -- the publish sequence of plan r2 8-4: with the clipboard OPEN,
//          compare its sequence number with the one recorded at accept; if it moved, close without
//          touching it (superseded). Otherwise Empty, then Set "PNG", CF_DIBV5 and (if any) text; a
//          failed Set empties it again rather than leave half an image behind.
//          clip_sha256 -- SHA-256 over the package (BCrypt).
// Thread:  the clipboard calls need a thread with a message queue; the host runs publish on its
//          clipboard monitor thread, the viewer reads on its UI thread.
// Callers: clipboard_monitor.cpp (host publish), viewer_window_proc.cpp (viewer read),
//          host_clip_image.cpp / viewer_clip_image.cpp (digest), clip_image_clipboard_test.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace remote60::native_poc {

enum class ClipSnapshotKind : uint8_t { None = 0, Png, Dib };

/**
 * The length of a NUL-terminated UTF-16 text in an allocation of `cap` units, looking at no more
 * than `max` + 1 units: `max` + 1 means "over the limit" (or no NUL within it), and nothing past
 * that is read.
 */
inline size_t clip_bounded_text_length(const wchar_t* t, size_t cap, size_t max) {
  const size_t look = cap < max + 1 ? cap : max + 1;
  size_t n = 0;
  while (n < look && t[n] != L'\0') ++n;
  return n == look && look == max + 1 ? max + 1 : n;
}

enum class ClipSnapshotResult : uint8_t {
  Ok = 0,
  NoImage,       // nothing image-like on the clipboard (text v1 handles it)
  OpenFailed,    // busy / secure desktop: try again on the next change
  TooLarge,      // over the limits before anything was copied
  Unreadable,
};

struct ClipSnapshot {
  ClipSnapshotKind kind = ClipSnapshotKind::None;
  std::vector<uint8_t> bytes;  // PNG file bytes, or the packed DIB (header + masks + palette + rows)
  std::u16string text;         // CF_UNICODETEXT of the same copy (may be empty)
  uint64_t sequence = 0;       // GetClipboardSequenceNumber() while the clipboard was open
};

/** Whether the clipboard holds an image format this feature reads (no open, no copy). */
bool clip_image_available();

/** Copies the image (and text) of the current clipboard; closes it before returning. */
ClipSnapshotResult clip_image_read_snapshot(HWND owner, ClipSnapshot* out);

enum class ClipPublishResult : uint8_t {
  Published = 0,
  Superseded,   // the clipboard changed since the offer was accepted: left untouched
  OpenFailed,
  SetFailed,    // a Set failed after Empty: emptied again (the local clipboard is now empty)
};

/**
 * Publishes an image with the clipboard open, if and only if its sequence number is still
 * `expectSequence`. Takes ownership of `pngGlobal` and `dibv5` in every outcome (the clipboard owns
 * them, or they are freed). `text` may be null or empty. On Published, *sequenceAfter is the number
 * the write left.
 */
ClipPublishResult clip_image_publish(HWND owner, uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                                     const std::u16string* text, uint64_t* sequenceAfter);

/** A movable HGLOBAL holding a copy of `bytes` (null on failure). */
HGLOBAL clip_global_copy(const void* bytes, size_t len);

/** SHA-256 of `bytes` (BCrypt). False only if the provider is unavailable. */
bool clip_sha256(const uint8_t* bytes, size_t len, uint8_t out[32]);

}  // namespace remote60::native_poc
