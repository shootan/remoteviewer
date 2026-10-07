#pragma once

// Clipboard text sync (K1): the two Win32 clipboard operations, shared by the host monitor and the
// viewer's window procedure.
//
// Role:    read CF_UNICODETEXT from, and write it to, the calling session's clipboard, with the
//          OpenClipboard retry the API needs (another process may hold the clipboard for a moment)
//          and no dependence on any format but Unicode text.
// Thread:  each call must run on a thread that owns a window / message queue -- OpenClipboard
//          associates the clipboard with the current task. The host calls both on its monitor
//          thread; the viewer calls both on its UI thread.
// Callers: clipboard_monitor.cpp (host), viewer_window_proc.cpp (viewer).
//
// A locked workstation, a UAC prompt or any other secure desktop denies OpenClipboard; the retry
// then the false return surface that as "could not read/write now", which the caller treats as a
// skip rather than an error -- the sync simply resumes when the desktop is back.

#include <windows.h>
#include <shellapi.h>  // DragQueryFileW (CF_HDROP)
#pragma comment(lib, "shell32.lib")

#include <cstring>
#include <string>
#include <vector>

namespace remote60::native_poc {

// Win32 speaks wchar_t; the sync core and the wire speak std::u16string, because wchar_t is 32-bit
// on Android and the protocol carries UTF-16 code units (clipboard_sync.hpp). On Windows the two
// are the same 16 bits, so these convert by copying units -- no transcoding, and no chance of the
// two ends of a session disagreeing about what a code unit is.
static_assert(sizeof(wchar_t) == sizeof(char16_t),
              "Windows wchar_t must be 16-bit for the clipboard conversions below");

inline std::u16string wide_to_u16(const std::wstring& text) {
  return std::u16string(reinterpret_cast<const char16_t*>(text.data()), text.size());
}

inline std::wstring u16_to_wide(const std::u16string& text) {
  return std::wstring(reinterpret_cast<const wchar_t*>(text.data()), text.size());
}

// Number of OpenClipboard attempts and the pause between them. ~200ms total: long enough to ride
// out another app holding the clipboard for a paint, short enough not to stall the caller.
inline bool clipboard_open_with_retry(HWND owner) {
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (OpenClipboard(owner)) return true;
    Sleep(20);
  }
  return false;
}

// Reads the clipboard's Unicode text into `out`. Returns false only when the clipboard could not be
// opened (busy / secure desktop); a successful open that finds no text clears `out` and returns
// true, so the caller reads "no text here" as an empty clipboard rather than a failure.
inline bool clipboard_read_unicode_text(HWND owner, std::wstring* out) {
  if (!out) return false;
  out->clear();
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return true;  // image / files / empty
  if (!clipboard_open_with_retry(owner)) return false;
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (handle) {
    if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
      *out = text;  // NUL-terminated; assigns up to the terminator
      GlobalUnlock(handle);
    }
  }
  CloseClipboard();
  return true;
}

// File copy (t-zdmsd4gb): the paths a copy of files names (CF_HDROP), as strings -- nothing is opened.
// At most `maxPaths` (the caller passes one over its limit, so its rules can say "too many"); a path
// longer than the long-path maximum is skipped. False only when the clipboard could not be opened; no
// CF_HDROP clears `out` and returns true.
inline bool clipboard_read_file_paths(HWND owner, size_t maxPaths, std::vector<std::wstring>* out) {
  if (!out) return false;
  out->clear();
  if (!IsClipboardFormatAvailable(CF_HDROP)) return true;
  if (!clipboard_open_with_retry(owner)) return false;
  if (HANDLE h = GetClipboardData(CF_HDROP)) {
    if (auto* drop = static_cast<HDROP>(GlobalLock(h))) {
      const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < n && out->size() < maxPaths; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        if (len == 0 || len > 32767) continue;
        std::wstring p(len, L'\0');
        DragQueryFileW(drop, i, p.data(), len + 1);
        out->push_back(std::move(p));
      }
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return true;
}

// Replaces the clipboard with `text` as CF_UNICODETEXT, if `still_ok()` -- asked once the clipboard is
// open, when nothing else can change it -- says so (`*refused` then false). Returns false when it was
// refused, or the clipboard could not be opened or the data could not be set.
template <class StillOk>
inline bool clipboard_set_unicode_text_if(HWND owner, const std::wstring& text, StillOk still_ok, bool* refused) {
  *refused = false;
  if (!clipboard_open_with_retry(owner)) return false;
  if (!still_ok()) {
    *refused = true;
    CloseClipboard();
    return false;
  }
  bool ok = false;
  if (EmptyClipboard()) {
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (global) {
      if (void* dst = GlobalLock(global)) {
        std::memcpy(dst, text.c_str(), text.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(dst)[text.size()] = L'\0';
        GlobalUnlock(global);
        if (SetClipboardData(CF_UNICODETEXT, global)) {
          ok = true;
          global = nullptr;  // the clipboard owns the memory now; freeing it would double-free
        }
      }
      if (global) GlobalFree(global);
    }
  }
  CloseClipboard();
  return ok;
}

// Replaces the clipboard with `text` as CF_UNICODETEXT. Returns false when the clipboard could not
// be opened or the data could not be set.
inline bool clipboard_set_unicode_text(HWND owner, const std::wstring& text) {
  bool refused = false;
  return clipboard_set_unicode_text_if(owner, text, [] { return true; }, &refused);
}

// The same write, saying where it failed: 1 = OpenClipboard, 2 = EmptyClipboard, 3 = allocating or
// setting the data; `win32` is GetLastError at that point. Paste on demand reports this to the viewer
// (paste_apply_wire.hpp PasteApplyStage), because "the paste key was not sent" is only actionable with
// the reason beside it.
inline bool clipboard_set_unicode_text_staged(HWND owner, const std::wstring& text, uint8_t* stage, uint32_t* win32) {
  *stage = 0;
  *win32 = 0;
  if (!clipboard_open_with_retry(owner)) {
    *stage = 1;
    *win32 = GetLastError();
    return false;
  }
  bool ok = false;
  if (!EmptyClipboard()) {
    *stage = 2;
    *win32 = GetLastError();
  } else {
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (global) {
      if (void* dst = GlobalLock(global)) {
        std::memcpy(dst, text.c_str(), text.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(dst)[text.size()] = L'\0';
        GlobalUnlock(global);
        if (SetClipboardData(CF_UNICODETEXT, global)) {
          ok = true;
          global = nullptr;  // the clipboard owns the memory now
        }
      }
      if (!ok) {
        *stage = 3;
        *win32 = GetLastError();
      }
      if (global) GlobalFree(global);
    } else {
      *stage = 3;
      *win32 = GetLastError();
    }
  }
  CloseClipboard();
  return ok;
}

}  // namespace remote60::native_poc
