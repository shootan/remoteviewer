#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "test_scratch_dir.hpp"

#include <windows.h>

#include <atomic>
#include <iterator>

#ifndef REMOTE60_TEST_SCRATCH_ROOT
#error "REMOTE60_TEST_SCRATCH_ROOT must be set by the build -- there is no default on purpose"
#endif

namespace remote60::native_poc::test_support {
namespace {

std::atomic<int> gCounter{0};

std::wstring widen(const char* narrow) {
  if (!narrow) return {};
  const int need = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, nullptr, 0);
  if (need <= 0) return {};
  std::wstring out(static_cast<size_t>(need - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, narrow, -1, out.data(), need);
  return out;
}

/** Trailing separators removed, so a prefix comparison cannot be defeated by one. */
std::wstring without_trailing_sep(std::wstring p) {
  while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  return p;
}

bool create_directory_chain(const std::wstring& path) {
  if (CreateDirectoryW(path.c_str(), nullptr)) return true;
  if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
  if (GetLastError() != ERROR_PATH_NOT_FOUND) return false;
  const size_t cut = path.find_last_of(L"\\/");
  if (cut == std::wstring::npos || cut < 3) return false;
  if (!create_directory_chain(path.substr(0, cut))) return false;
  return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace

std::wstring canonical_path(const std::wstring& path) {
  if (path.empty()) return {};
  // GetFullPathName resolves . and .. and makes the path absolute. It is deliberately NOT
  // GetFinalPathNameByHandle: that one follows reparse points, and following them is the thing
  // this file exists to avoid. A junction INSIDE the root stays inside the root by this measure
  // and is removed as a link, never descended into.
  wchar_t full[32768]{};
  const DWORD used = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full,
                                      nullptr);
  if (used == 0 || used >= std::size(full)) return {};
  return without_trailing_sep(full);
}

bool is_reparse_point(const std::wstring& path) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool is_strictly_under(const std::wstring& root, const std::wstring& path) {
  const std::wstring r = canonical_path(root);
  const std::wstring p = canonical_path(path);
  if (r.empty() || p.empty()) return false;
  if (p.size() <= r.size() + 1) return false;  // equal to the root is not "under" it
  if (p[r.size()] != L'\\') return false;      // ...and neither is a sibling with a longer name
  return CompareStringOrdinal(p.c_str(), static_cast<int>(r.size()), r.c_str(),
                              static_cast<int>(r.size()), TRUE) == CSTR_EQUAL;
}

std::wstring scratch_root() {
  static const std::wstring root = []() -> std::wstring {
    const std::wstring configured = canonical_path(widen(REMOTE60_TEST_SCRATCH_ROOT));
    if (configured.empty()) return {};
    if (!create_directory_chain(configured)) return {};
    // A root that is itself a link would put everything below it somewhere else. Refused rather
    // than worked around: there is no correct way to continue from here.
    if (is_reparse_point(configured)) return {};
    return configured;
  }();
  return root;
}

std::wstring make_scratch_dir(const std::wstring& tag) {
  const std::wstring root = scratch_root();
  if (root.empty()) return {};
  const std::wstring dir = root + L"\\" + tag + L"-" + std::to_wstring(GetCurrentProcessId()) +
                           L"-" + std::to_wstring(++gCounter);
  if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
  return dir;
}

bool remove_scratch_tree(const std::wstring& dir) {
  const std::wstring root = scratch_root();
  if (root.empty()) return false;
  // The boundary. Everything below depends on this having been checked FIRST, for every level of
  // the recursion, not only for the top call.
  if (!is_strictly_under(root, dir)) return false;
  const std::wstring here = canonical_path(dir);
  if (here.empty()) return false;

  const DWORD attrs = GetFileAttributesW(here.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return true;  // already gone
  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    // A link. Remove the link itself and do not look at what it points to -- which may be
    // anywhere, and is not ours.
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(here.c_str()) != FALSE
                                              : DeleteFileW(here.c_str()) != FALSE;
  }
  if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    if (attrs & FILE_ATTRIBUTE_READONLY) {
      SetFileAttributesW(here.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    return DeleteFileW(here.c_str()) != FALSE;
  }

  bool allGone = true;
  WIN32_FIND_DATAW found{};
  HANDLE h = FindFirstFileW((here + L"\\*").c_str(), &found);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = found.cFileName;
      if (name == L"." || name == L"..") continue;
      // Built from the canonical parent, so the child is canonical too and the recursion's own
      // boundary check cannot be fooled by a name that climbs.
      if (!remove_scratch_tree(here + L"\\" + name)) allGone = false;
    } while (FindNextFileW(h, &found));
    FindClose(h);
  }
  if (!RemoveDirectoryW(here.c_str())) allGone = false;
  return allGone;
}

}  // namespace remote60::native_poc::test_support
