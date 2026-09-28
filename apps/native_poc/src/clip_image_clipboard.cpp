// See clip_image_clipboard.hpp.

#include "clip_image_clipboard.hpp"

#include <bcrypt.h>

#include <algorithm>
#include <cstring>

#include "clip_image_core.hpp"
#include "clipboard_win32.hpp"

namespace remote60::native_poc {

namespace {

UINT png_format() {
  static const UINT f = RegisterClipboardFormatW(L"PNG");
  return f;
}

// The largest packed DIB worth copying: the decoded limit plus a V5 header, masks and a full palette.
constexpr uint64_t kMaxDibSnapshotBytes = kClipImageMaxDecodedBytes + 124 + 12 + 256 * 4;

bool copy_global(HANDLE h, uint64_t maxBytes, std::vector<uint8_t>* out, bool* tooLarge) {
  *tooLarge = false;
  if (!h) return false;
  const SIZE_T size = GlobalSize(h);
  if (size == 0) return false;
  if (size > maxBytes) {
    *tooLarge = true;
    return false;
  }
  const void* p = GlobalLock(h);
  if (!p) return false;
  try {
    out->assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + size);
  } catch (...) {
    GlobalUnlock(h);
    return false;
  }
  GlobalUnlock(h);
  return true;
}

}  // namespace

HGLOBAL clip_global_copy(const void* data, size_t bytes) {
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!g) return nullptr;
  void* p = GlobalLock(g);
  if (!p) {
    GlobalFree(g);
    return nullptr;
  }
  std::memcpy(p, data, bytes);
  GlobalUnlock(g);
  return g;
}

bool clip_image_available() {
  return IsClipboardFormatAvailable(png_format()) || IsClipboardFormatAvailable(CF_DIBV5) ||
         IsClipboardFormatAvailable(CF_DIB);
}

ClipSnapshotResult clip_image_read_snapshot(HWND owner, ClipSnapshot* out) {
  *out = ClipSnapshot{};
  if (!clip_image_available()) return ClipSnapshotResult::NoImage;
  if (!clipboard_open_with_retry(owner)) return ClipSnapshotResult::OpenFailed;
  ClipSnapshotResult result = ClipSnapshotResult::Unreadable;
  bool tooLarge = false;
  out->sequence = GetClipboardSequenceNumber();
  // PNG first (browsers, Office): it is the original bytes, alpha and colour as the source meant.
  if (IsClipboardFormatAvailable(png_format()) &&
      copy_global(GetClipboardData(png_format()), kClipImageMaxPngBytes, &out->bytes, &tooLarge)) {
    out->kind = ClipSnapshotKind::Png;
    result = ClipSnapshotResult::Ok;
  } else if (!tooLarge) {
    const UINT dibFormat = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
    if (copy_global(GetClipboardData(dibFormat), kMaxDibSnapshotBytes, &out->bytes, &tooLarge)) {
      out->kind = ClipSnapshotKind::Dib;
      result = ClipSnapshotResult::Ok;
    }
  }
  if (result == ClipSnapshotResult::Ok && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
      if (const wchar_t* t = static_cast<const wchar_t*>(GlobalLock(h))) {
        const SIZE_T cap = GlobalSize(h) / sizeof(wchar_t);
        size_t n = 0;
        while (n < cap && t[n] != L'\0') ++n;  // bounded by the allocation, not by trust in a NUL
        out->text.assign(reinterpret_cast<const char16_t*>(t), n);
        GlobalUnlock(h);
      }
    }
  }
  CloseClipboard();
  if (tooLarge) {
    *out = ClipSnapshot{};
    return ClipSnapshotResult::TooLarge;
  }
  if (result != ClipSnapshotResult::Ok) *out = ClipSnapshot{};
  return result;
}

ClipPublishResult clip_image_publish(HWND owner, uint64_t expectSequence, HGLOBAL pngGlobal, HGLOBAL dibv5,
                                     const std::u16string* text, uint64_t* sequenceAfter) {
  auto release = [&] {
    if (pngGlobal) GlobalFree(pngGlobal);
    if (dibv5) GlobalFree(dibv5);
    pngGlobal = dibv5 = nullptr;
  };
  if (!pngGlobal || !dibv5) {
    release();
    return ClipPublishResult::SetFailed;
  }
  if (!clipboard_open_with_retry(owner)) {
    release();
    return ClipPublishResult::OpenFailed;
  }
  // Checked with the clipboard held, so nothing can change it between this look and the write.
  if (GetClipboardSequenceNumber() != static_cast<DWORD>(expectSequence)) {
    CloseClipboard();
    release();
    return ClipPublishResult::Superseded;
  }
  if (!EmptyClipboard()) {
    CloseClipboard();
    release();
    return ClipPublishResult::SetFailed;
  }
  bool ok = true;
  if (SetClipboardData(png_format(), pngGlobal)) pngGlobal = nullptr;  // the clipboard owns it now
  else ok = false;
  if (ok) {
    if (!SetClipboardData(CF_DIBV5, dibv5)) ok = false;
    else dibv5 = nullptr;  // the clipboard owns it now
  }
  if (ok && text && !text->empty()) {
    const size_t bytes = (text->size() + 1) * sizeof(char16_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void* p = g ? GlobalLock(g) : nullptr;
    if (p) {
      std::memcpy(p, text->data(), text->size() * sizeof(char16_t));
      static_cast<char16_t*>(p)[text->size()] = u'\0';
      GlobalUnlock(g);
      if (!SetClipboardData(CF_UNICODETEXT, g)) {
        GlobalFree(g);
        ok = false;
      }
    } else {
      if (g) GlobalFree(g);
      ok = false;
    }
  }
  if (!ok) EmptyClipboard();  // no half image: an empty clipboard, and the log says so
  CloseClipboard();
  release();
  if (!ok) return ClipPublishResult::SetFailed;
  if (sequenceAfter) *sequenceAfter = GetClipboardSequenceNumber();
  return ClipPublishResult::Published;
}

bool clip_sha256(const uint8_t* bytes, size_t len, uint8_t out[32]) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
  size_t done = 0;
  while (ok && done < len) {
    const ULONG take = static_cast<ULONG>(std::min<size_t>(len - done, 1u << 30));
    ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes + done), take, 0) == 0;
    done += take;
  }
  if (ok) ok = BCryptFinishHash(hash, out, 32, 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

}  // namespace remote60::native_poc
