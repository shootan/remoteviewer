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

bool path_exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
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
    if (prefix.size() <= r.size()) continue;  // at or above the root: checked separately
    if (prefix == p) continue;                // the target itself is removed as a link if it is one
    if (is_reparse_point(prefix)) return true;
  }
  return false;
}

const char* root_verdict_name(RootVerdict v) {
  switch (v) {
    case RootVerdict::Ok: return "ok";
    case RootVerdict::Unresolvable: return "unresolvable";
    case RootVerdict::NotInRepository: return "not-in-repository";
    case RootVerdict::AncestorIsLink: return "ancestor-is-link";
    case RootVerdict::RootIsLink: return "root-is-link";
    case RootVerdict::CannotCreate: return "cannot-create";
  }
  return "?";
}

RootVerdict validate_and_create_root(const std::wstring& repoRoot, const std::wstring& root,
                                     std::string* why) {
  const auto explain = [why](const std::string& text) {
    if (why) *why = text;
  };
  if (why) why->clear();

  const std::wstring repo = canonical_path(repoRoot);
  const std::wstring here = canonical_path(root);
  if (repo.empty() || here.empty()) {
    explain("the configured scratch root or repository root cannot be resolved");
    return RootVerdict::Unresolvable;
  }

  // The claim r5 made without checking it. CMAKE_BINARY_DIR is wherever the person building put
  // it, and "inside the repository" is a property of this machine's layout, not of the source.
  if (!is_strictly_under(repo, here)) {
    explain("the build directory (" + narrow_of(here) + ") is not inside the repository (" +
            narrow_of(repo) + "); no scratch root will be created anywhere else");
    return RootVerdict::NotInRepository;
  }

  // BEFORE anything is created. r6's version created the chain first and looked at the ancestors
  // afterwards, so an existing junction above the root meant directories had already been made on
  // the far side of it -- the refusal arrived after the thing it was refusing had happened.
  //
  // Only components that ALREADY EXIST can be links; the ones this is about to make cannot be.
  for (const std::wstring& prefix : prefixes_of(here)) {
    if (!path_exists(prefix)) continue;
    if (!is_reparse_point(prefix)) continue;
    if (prefix == here) {
      explain("the scratch root is itself a reparse point: " + narrow_of(here));
      return RootVerdict::RootIsLink;
    }
    explain("a component above the scratch root is a reparse point: " + narrow_of(prefix) +
            " -- nothing was created");
    return RootVerdict::AncestorIsLink;
  }

  if (!create_directory_chain(here)) {
    explain("the scratch root could not be created: " + narrow_of(here));
    return RootVerdict::CannotCreate;
  }

  // And again afterwards. Between the check above and the creation, something could have replaced
  // a component; the window is small and closing it entirely would need handles rather than
  // paths, but a second look costs nothing and catches the ordinary case of a stale layout.
  for (const std::wstring& prefix : prefixes_of(here)) {
    if (!is_reparse_point(prefix)) continue;
    if (prefix == here) {
      explain("the scratch root is a reparse point: " + narrow_of(here));
      return RootVerdict::RootIsLink;
    }
    explain("a component above the scratch root is a reparse point: " + narrow_of(prefix));
    return RootVerdict::AncestorIsLink;
  }
  return RootVerdict::Ok;
}

std::string scratch_root_problem() {
  scratch_root();
  std::lock_guard<std::mutex> guard(gRootProblemLock);
  return gRootProblem;
}

const std::wstring& scratch_root() {
  // One object for the lifetime of the process, handed out by reference: see the header for what
  // returning a copy cost.
  static const std::wstring root = []() -> std::wstring {
    const std::wstring configured = widen(REMOTE60_TEST_SCRATCH_ROOT);
    const std::wstring repo = widen(REMOTE60_TEST_REPO_ROOT);
    std::string why;
    if (validate_and_create_root(repo, configured, &why) != RootVerdict::Ok) {
      set_root_problem(why);
      return {};
    }
    return canonical_path(configured);
  }();
  return root;
}

const std::wstring& scratch_run_dir() {
  static const std::wstring run = []() -> std::wstring {
    const std::wstring root = scratch_root();
    if (root.empty()) return {};
    // Only a directory this process CREATES is acceptable. r6 treated ERROR_ALREADY_EXISTS as
    // success, which meant it could adopt whatever was sitting at that name -- another run's
    // directory, or a junction somebody left there -- and then delete it recursively at the end.
    // The tick was also truncated to 24 bits, so "unique" was a hope rather than a property.
    for (int attempt = 0; attempt < 64; ++attempt) {
      LARGE_INTEGER counter{};
      QueryPerformanceCounter(&counter);
      const std::wstring dir = root + L"\\run-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                               std::to_wstring(static_cast<unsigned long long>(counter.QuadPart)) +
                               L"-" + std::to_wstring(attempt);
      if (!CreateDirectoryW(dir.c_str(), nullptr)) continue;  // taken, or refused: try another
      // It was made by this call, so it cannot be a link -- checked anyway, because the cost is
      // nothing and the assumption is the kind that stops being true quietly.
      if (is_reparse_point(dir)) continue;
      return dir;
    }
    return {};
  }();
  return run;
}

std::wstring scratch_path(const std::wstring& name) {
  const std::wstring& run = scratch_run_dir();
  return run.empty() ? std::wstring() : run + L"\\" + name;
}

std::wstring make_scratch_dir(const std::wstring& tag) {
  const std::wstring& run = scratch_run_dir();
  if (run.empty()) return {};
  for (int attempt = 0; attempt < 64; ++attempt) {
    const std::wstring dir = run + L"\\" + tag + L"-" + std::to_wstring(++gCounter);
    if (!CreateDirectoryW(dir.c_str(), nullptr)) continue;
    if (is_reparse_point(dir)) continue;
    return dir;
  }
  return {};
}

namespace {

/**
 * The recursion, after the entry path has been validated.
 *
 * `here` is canonical and known to be beneath `root` with nothing linked in between. Children are
 * built from it, so they inherit that; each one's own link status is checked at the top of the
 * call, which is what keeps the recursion from entering one.
 */
bool remove_tree_worker(const std::wstring& here, const std::wstring& root) {
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
      const std::wstring child = here + L"\\" + name;
      // Belt and braces: a child of a validated parent is under the root by construction, and it
      // is cheap to say so rather than rely on the construction being right.
      if (!is_strictly_under(root, child)) {
        allGone = false;
        continue;
      }
      if (!remove_tree_worker(child, root)) allGone = false;
    } while (FindNextFileW(h, &found));
    FindClose(h);
  }
  if (!RemoveDirectoryW(here.c_str())) allGone = false;
  return allGone;
}

}  // namespace

bool remove_scratch_tree(const std::wstring& dir) {
  const std::wstring& root = scratch_root();
  if (root.empty()) return false;
  // Re-checked here, not trusted from start-up. The root was validated once when the process
  // began; this runs much later, after fixtures have come and gone, and the question being asked
  // is whether it is safe to delete things NOW.
  if (is_reparse_point(root)) return false;
  if (!reparse_ancestors(root).empty()) return false;
  // Being textually under the root says nothing about where the path LEADS; a junction anywhere
  // between the two puts the rest of this somewhere else entirely, and the attributes of what is
  // below it look perfectly ordinary because the filesystem has already followed the link.
  if (!is_strictly_under(root, dir)) return false;
  if (has_reparse_between(root, dir)) return false;
  const std::wstring here = canonical_path(dir);
  if (here.empty()) return false;
  return remove_tree_worker(here, root);
}

bool remove_scratch_run_dir() {
  const std::wstring& run = scratch_run_dir();
  if (run.empty()) return false;
  return remove_scratch_tree(run);
}

}  // namespace remote60::native_poc::test_support
