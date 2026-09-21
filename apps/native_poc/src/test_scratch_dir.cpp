#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "test_scratch_dir.hpp"

#include <windows.h>

#include <atomic>
#include <iterator>
#include <mutex>

#ifndef REMOTE60_TEST_SCRATCH_ROOT
#error "REMOTE60_TEST_SCRATCH_ROOT must be set by the build -- there is no default on purpose"
#endif
#ifndef REMOTE60_TEST_REPO_ROOT
#error "REMOTE60_TEST_REPO_ROOT must be set by the build -- the boundary claim is checked, not assumed"
#endif

namespace remote60::native_poc::test_support {
namespace {

std::atomic<int> gCounter{0};
std::string gRootProblem;
std::mutex gRootProblemLock;

void set_root_problem(const std::string& why) {
  std::lock_guard<std::mutex> guard(gRootProblemLock);
  gRootProblem = why;
}

std::wstring widen(const char* narrow) {
  if (!narrow) return {};
  const int need = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, nullptr, 0);
  if (need <= 0) return {};
  std::wstring out(static_cast<size_t>(need - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, narrow, -1, out.data(), need);
  return out;
}

std::string narrow_of(const std::wstring& w) { return std::string(w.begin(), w.end()); }

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

/** Every prefix of a canonical path, shortest first, starting at the drive root. */
std::vector<std::wstring> prefixes_of(const std::wstring& canonical) {
  std::vector<std::wstring> out;
  if (canonical.size() < 3) return out;
  for (size_t i = 3; i <= canonical.size(); ++i) {
    if (i == canonical.size() || canonical[i] == L'\\') out.push_back(canonical.substr(0, i));
  }
  return out;
}

}  // namespace

std::wstring canonical_path(const std::wstring& path) {
  if (path.empty()) return {};
  // Text only: GetFullPathName resolves . and .. and makes the path absolute, and that is ALL it
  // does. It is deliberately not GetFinalPathNameByHandle -- resolving links would let a junction
  // silently relocate the boundary -- but the consequence has to be faced rather than assumed
  // away: since the text is not the truth about where a path leads, every component between the
  // root and a target is examined separately. See has_reparse_between.
  wchar_t full[32768]{};
  const DWORD used =
      GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
  if (used == 0 || used >= std::size(full)) return {};
  return without_trailing_sep(full);
}

bool is_reparse_point(const std::wstring& path) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::vector<std::wstring> reparse_ancestors(const std::wstring& path) {
  std::vector<std::wstring> found;
  const std::wstring canonical = canonical_path(path);
  if (canonical.empty()) return found;
  for (const std::wstring& prefix : prefixes_of(canonical)) {
    if (prefix == canonical) continue;
    if (is_reparse_point(prefix)) found.push_back(prefix);
  }
  return found;
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

bool has_reparse_between(const std::wstring& root, const std::wstring& path) {
  const std::wstring r = canonical_path(root);
  const std::wstring p = canonical_path(path);
  if (r.empty() || p.empty()) return true;  // cannot establish it is safe, so it is not
  for (const std::wstring& prefix : prefixes_of(p)) {
    if (prefix.size() <= r.size()) continue;  // at or above the root: validated when accepted
    if (prefix == p) continue;                // the target itself is removed as a link if it is one
    if (is_reparse_point(prefix)) return true;
  }
  return false;
}

std::string scratch_root_problem() {
  scratch_root();
  std::lock_guard<std::mutex> guard(gRootProblemLock);
  return gRootProblem;
}

std::wstring scratch_root() {
  static const std::wstring root = []() -> std::wstring {
    const std::wstring configured = canonical_path(widen(REMOTE60_TEST_SCRATCH_ROOT));
    const std::wstring repo = canonical_path(widen(REMOTE60_TEST_REPO_ROOT));
    if (configured.empty() || repo.empty()) {
      set_root_problem("the configured scratch root or repository root cannot be resolved");
      return {};
    }
    // The claim r5 made without checking it. CMAKE_BINARY_DIR is wherever the person building
    // put it, and "inside the repository" is a property of this machine's layout, not of the
    // source. If it is false, there is no safe place to fall back to and nothing is created.
    if (!is_strictly_under(repo, configured)) {
      set_root_problem("the build directory (" + narrow_of(configured) +
                       ") is not inside the repository (" + narrow_of(repo) +
                       "); no scratch root will be created anywhere else");
      return {};
    }
    if (!create_directory_chain(configured)) {
      set_root_problem("the scratch root could not be created: " + narrow_of(configured));
      return {};
    }
    // The root itself, and everything above it. A link anywhere in that chain means the boundary
    // does not describe where files actually go, and there is no correct way to continue.
    if (is_reparse_point(configured)) {
      set_root_problem("the scratch root is itself a reparse point: " + narrow_of(configured));
      return {};
    }
    const std::vector<std::wstring> links = reparse_ancestors(configured);
    if (!links.empty()) {
      set_root_problem("an ancestor of the scratch root is a reparse point: " +
                       narrow_of(links.front()));
      return {};
    }
    return configured;
  }();
  return root;
}

std::wstring scratch_run_dir() {
  static const std::wstring run = []() -> std::wstring {
    const std::wstring root = scratch_root();
    if (root.empty()) return {};
    const std::wstring dir = root + L"\\run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64() & 0xffffff);
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
      return {};
    }
    return dir;
  }();
  return run;
}

std::wstring scratch_path(const std::wstring& name) {
  const std::wstring run = scratch_run_dir();
  return run.empty() ? std::wstring() : run + L"\\" + name;
}

std::wstring make_scratch_dir(const std::wstring& tag) {
  const std::wstring run = scratch_run_dir();
  if (run.empty()) return {};
  const std::wstring dir = run + L"\\" + tag + L"-" + std::to_wstring(++gCounter);
  if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
  return dir;
}

bool remove_scratch_tree(const std::wstring& dir) {
  const std::wstring root = scratch_root();
  if (root.empty()) return false;
  // Two checks, and the second is the one r5 was missing. Being textually under the root says
  // nothing about where the path LEADS; a junction anywhere between the two puts the rest of this
  // function somewhere else entirely, and the attributes of what is below it look perfectly
  // ordinary because the filesystem has already followed the link.
  if (!is_strictly_under(root, dir)) return false;
  if (has_reparse_between(root, dir)) return false;
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
      // Built from the canonical parent, and re-validated by the recursive call, so a name that
      // climbs or a link that appeared since cannot slip past.
      if (!remove_scratch_tree(here + L"\\" + name)) allGone = false;
    } while (FindNextFileW(h, &found));
    FindClose(h);
  }
  if (!RemoveDirectoryW(here.c_str())) allGone = false;
  return allGone;
}

bool remove_scratch_run_dir() {
  const std::wstring run = scratch_run_dir();
  if (run.empty()) return false;
  return remove_scratch_tree(run);
}

}  // namespace remote60::native_poc::test_support
