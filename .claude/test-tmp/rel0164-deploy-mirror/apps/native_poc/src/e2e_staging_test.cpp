// The staging directory a host e2e stages into (e2e_isolation.hpp StagingDir): the narrow
// boundary cases of RV-20 r2 -- inside the repository, only what this process created, removed
// through the scratch boundary, a failure kept and reported, and "gone" meaning gone.
//
//   - --keep-dir: the directory outlives the object, and says so;
//   - the tidy path: files, a subdirectory and a junction inside it -- the junction points at a
//     sentinel OUTSIDE the scratch root and the sentinel survives (the link is removed as a link);
//   - a name already taken is never adopted: a directory pre-made at the next name keeps its
//     contents while a fresh one is handed out;
//   - the start point replaced by a junction: removal does not go through it -- the sentinel it
//     points at survives;
//   - INVALID_FILE_ATTRIBUTES is "gone" only for FILE_NOT_FOUND / PATH_NOT_FOUND.
//
// Everything this test makes is inside the repository (the scratch root, or a sibling of it in
// the build directory for the sentinel); %TEMP% is neither written nor read.
// Build: remote60_e2e_staging_test. pure-logic (filesystem inside the build directory).

#include "e2e_isolation.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace remote60::native_poc;
using namespace remote60::native_poc::e2e;
namespace ts = remote60::native_poc::test_support;

namespace {

int gChecks = 0;
int gFailures = 0;
void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
}
std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }
bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool write_text(const std::wstring& path, const char* text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  return out.good();
}
std::wstring bare(const std::wstring& withSlash) {
  return withSlash.empty() ? withSlash : withSlash.substr(0, withSlash.size() - 1);
}
bool make_junction(const std::wstring& link, const std::wstring& target) {
  const std::wstring cmd = L"cmd /c mklink /J \"" + link + L"\" \"" + target + L"\" > nul";
  return _wsystem(cmd.c_str()) == 0 && ts::is_reparse_point(link);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::wstring root = ts::scratch_root();
  check("a scratch root inside the repository", !root.empty(), ts::scratch_root_problem());
  if (root.empty()) {
    std::printf("\nRESULT: FAILED  (%d checks, %d failed)\n", gChecks, gFailures);
    return 1;
  }
  // The sentinel: outside the scratch boundary, inside the repository (a sibling of the root in
  // the build directory), made and removed by this test with plain calls.
  const std::wstring sentinelDir = root + L"-sentinel-" + std::to_wstring(GetCurrentProcessId());
  const std::wstring sentinelFile = sentinelDir + L"\\do-not-delete.txt";
  check("a sentinel directory outside the scratch root, inside the repository",
        CreateDirectoryW(sentinelDir.c_str(), nullptr) && write_text(sentinelFile, "sentinel"),
        narrow(sentinelDir));
  check("...which the scratch boundary refuses to remove", !ts::remove_scratch_tree(sentinelDir) && exists(sentinelFile));

  std::printf("\n--- gone means gone ---\n");
  {
    DWORD err = 0;
    check("a path that does not exist is gone (FILE_NOT_FOUND)", e2e_path_gone(root + L"\\nothing-here", &err) && err == ERROR_FILE_NOT_FOUND,
          std::to_string(err));
    check("a path beneath a missing directory is gone (PATH_NOT_FOUND)",
          e2e_path_gone(root + L"\\nothing-here\\deeper", &err) && err == ERROR_PATH_NOT_FOUND, std::to_string(err));
    check("the root, which exists, is not gone", !e2e_path_gone(root, &err) && err == ERROR_SUCCESS);
    check("a malformed path (bad name) is NOT gone: its attributes are unreadable, which is a different thing",
          !e2e_path_gone(root + L"\\bad<>name", &err) && err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND,
          std::to_string(err));
  }

  std::printf("\n--- keep: the directory outlives the object ---\n");
  std::wstring keptPath;
  {
    StagingDir keep;
    check("created", keep.Create(L"keep"), keep.why());
    keptPath = bare(keep.path());
    check("...inside this run's scratch directory", ts::is_strictly_under(ts::scratch_run_dir(), keptPath));
    write_text(keep.path() + L"host.log", "log");
    keep.set_keep(true, "--keep-dir");
    check("Remove() on a kept directory returns false and leaves it", !keep.Remove() && exists(keptPath + L"\\host.log"));
  }
  check("...and it is still there after the object is gone", exists(keptPath + L"\\host.log"));
  check("...until removed through the boundary", ts::remove_scratch_tree(keptPath) && !exists(keptPath));

  std::printf("\n--- the tidy path: files, a subdirectory, a junction out ---\n");
  {
    StagingDir s;
    check("created", s.Create(L"tidy"), s.why());
    const std::wstring p = s.path();
    write_text(p + L"GNLinkStream.exe", "not really");
    write_text(p + L"host.log", "log");
    CreateDirectoryW((p + L"localappdata").c_str(), nullptr);
    CreateDirectoryW((p + L"localappdata\\remote60").c_str(), nullptr);
    write_text(p + L"localappdata\\remote60\\host.json", "{}");
    check("a junction inside it that points at the sentinel", make_junction(p + L"link-out", sentinelDir));
    check("...through which the sentinel is visible", exists(p + L"link-out\\do-not-delete.txt"));
    check("Remove() removes the directory", s.Remove(), s.why());
    check("...it is gone", e2e_path_gone(bare(p)));
    check("THE SENTINEL BEHIND THE JUNCTION SURVIVES", exists(sentinelFile));
    check("removed() reports it, why() is empty", s.removed() && s.why().empty(), s.why());
    check("Remove() again is a harmless yes", s.Remove());
  }

  std::printf("\n--- a name already taken is never adopted ---\n");
  {
    // make_scratch_dir names are <tag>-<counter>; the counter is process-wide, so the next name
    // is predictable from a probe. Put a directory there first, with something in it.
    const std::wstring probe = ts::make_scratch_dir(L"pre");
    check("probe directory made", !probe.empty(), narrow(probe));
    const size_t dash = probe.empty() ? std::wstring::npos : probe.find_last_of(L'-');
    const int next = dash == std::wstring::npos ? 1 : std::stoi(probe.substr(dash + 1)) + 1;
    const std::wstring taken = ts::scratch_run_dir() + L"\\pre-" + std::to_wstring(next);
    check("the next name is pre-made by 'somebody else'",
          CreateDirectoryW(taken.c_str(), nullptr) && write_text(taken + L"\\theirs.txt", "theirs"), narrow(taken));
    StagingDir s;
    check("Create() still succeeds", s.Create(L"pre"), s.why());
    check("...on a DIFFERENT directory, not the one already there", bare(s.path()) != taken, narrow(bare(s.path())));
    check("Remove() removes only its own", s.Remove() && exists(taken + L"\\theirs.txt"), s.why());
    check("the pre-made directory and its contents are untouched", exists(taken + L"\\theirs.txt"));
    check("(cleanup) the pre-made directory goes through the boundary", ts::remove_scratch_tree(taken));
    check("(cleanup) the probe too", ts::remove_scratch_tree(probe));
  }

  std::printf("\n--- the start point replaced by a junction ---\n");
  {
    StagingDir s;
    check("created", s.Create(L"swapped"), s.why());
    const std::wstring p = bare(s.path());
    check("the directory is swapped for a junction to the sentinel", RemoveDirectoryW(p.c_str()) && make_junction(p, sentinelDir));
    check("...through which the sentinel is visible", exists(p + L"\\do-not-delete.txt"));
    const bool removed = s.Remove();
    std::printf("      Remove() -> %s (%s)\n", removed ? "true" : "false", s.why().c_str());
    check("THE SENTINEL SURVIVES: the junction was removed as a link (or refused), never entered", exists(sentinelFile));
    check("no file inside the sentinel directory was touched", exists(sentinelFile));
    if (ts::is_reparse_point(p)) {
      check("(cleanup) the junction itself goes through the boundary", ts::remove_scratch_tree(p));
    }
  }

  std::printf("\n--- a failure is reported, not discarded ---\n");
  {
    StagingDir s;
    check("created", s.Create(L"held"), s.why());
    const std::wstring p = s.path();
    HANDLE held = CreateFileW((p + L"held.log").c_str(), GENERIC_WRITE, 0 /* no sharing: cannot be deleted */, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    check("a file inside it is held open without sharing", held != INVALID_HANDLE_VALUE);
    check("Remove() (2 attempts) fails and says why", !s.Remove(2) && !s.why().empty() && !s.removed(), s.why());
    check("...and the directory is still there", exists(bare(p)));
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    check("once released, Remove() succeeds", s.Remove(), s.why());
  }

  // Everything this test made, out through the boundary; the sentinel with plain calls.
  check("(cleanup) this run's scratch directory is removed", ts::remove_scratch_run_dir());
  check("(cleanup) the sentinel, by this test, with plain calls",
        DeleteFileW(sentinelFile.c_str()) && RemoveDirectoryW(sentinelDir.c_str()));

  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
