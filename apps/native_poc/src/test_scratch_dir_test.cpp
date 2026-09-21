#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// The boundary that keeps a test's cleanup inside the test. (updater-abandon-race r5, fixed in r6)
//
// A recursive delete is about to be run from an elevated shell (item G's service fixture), so the
// ways it can go wrong are checked here rather than reasoned about. r5's version reasoned about
// one of them and got it wrong: it checked that the TARGET was not a link, and a junction in the
// middle of the path is not the target. root\junction\child is textually under the root, and
// `child` looks like an ordinary directory because the filesystem followed the junction before
// anyone asked about its attributes.
//
// The counter-example below is the real thing: a junction inside the scratch area pointing at a
// directory OUTSIDE it, and a delete aimed through the junction at a child. Without the fix it
// deletes what the junction points at.

#include <windows.h>
#include <winioctl.h>

#include <iostream>
#include <string>
#include <vector>

#include "test_scratch_dir.hpp"

using namespace remote60::native_poc::test_support;

namespace {

int gChecks = 0;
int gFailures = 0;
int gSkips = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

void skip(const std::string& name, const std::string& why) {
  ++gSkips;
  std::cout << "SKIP  " << name << "  " << why << "\n";
}

std::string narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }

void write_text(const std::wstring& path, const std::string& text) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wrote = 0;
  WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr);
  CloseHandle(h);
}

bool exists(const std::wstring& path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The NT object-manager prefix a junction stores, built from characters rather than written so
// no editor can mangle the escapes: backslash, question, question, backslash.
const std::wstring kNtPrefix =
    std::wstring(1, wchar_t(92)) + std::wstring(2, wchar_t(63)) + wchar_t(92);

/**
 * The mount-point form of a reparse point buffer.
 *
 * Declared here because REPARSE_DATA_BUFFER lives in ntifs.h, which is a driver header: user-mode
 * code that wants to CREATE one has to say what it looks like. The layout is the documented one --
 * an eight byte header, then four offsets into a path buffer that holds the substitute name and
 * the print name back to back, each terminated.
 */
#pragma pack(push, 1)
struct MountPointReparse {
  DWORD ReparseTag;
  WORD ReparseDataLength;
  WORD Reserved;
  WORD SubstituteNameOffset;
  WORD SubstituteNameLength;
  WORD PrintNameOffset;
  WORD PrintNameLength;
  wchar_t PathBuffer[1];
};
#pragma pack(pop)

/**
 * Makes `link` a junction pointing at `target`.
 *
 * A mount point, not a symbolic link: symbolic links need SeCreateSymbolicLinkPrivilege, and a
 * junction does not, so this reproduces the hazard without needing an elevated run to do it.
 */
bool make_junction(const std::wstring& link, const std::wstring& target) {
  if (!CreateDirectoryW(link.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    return false;
  }
  HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;

  const std::wstring sub = kNtPrefix + target;
  const size_t subBytes = sub.size() * sizeof(wchar_t);
  const size_t printBytes = target.size() * sizeof(wchar_t);
  const size_t pathBytes = subBytes + sizeof(wchar_t) + printBytes + sizeof(wchar_t);
  const size_t headerBytes = offsetof(MountPointReparse, PathBuffer);

  std::vector<uint8_t> raw(headerBytes + pathBytes, 0);
  auto* buffer = reinterpret_cast<MountPointReparse*>(raw.data());
  buffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  // Counted from SubstituteNameOffset onward: the four WORDs plus the path buffer.
  buffer->ReparseDataLength = static_cast<WORD>(4 * sizeof(WORD) + pathBytes);
  buffer->SubstituteNameOffset = 0;
  buffer->SubstituteNameLength = static_cast<WORD>(subBytes);
  buffer->PrintNameOffset = static_cast<WORD>(subBytes + sizeof(wchar_t));
  buffer->PrintNameLength = static_cast<WORD>(printBytes);
  memcpy(raw.data() + headerBytes, sub.c_str(), subBytes);
  memcpy(raw.data() + headerBytes + subBytes + sizeof(wchar_t), target.c_str(), printBytes);

  DWORD returned = 0;
  const bool ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, raw.data(),
                                  static_cast<DWORD>(raw.size()), nullptr, 0, &returned,
                                  nullptr) != FALSE;
  CloseHandle(h);
  if (!ok) RemoveDirectoryW(link.c_str());
  return ok;
}

/**
 * Removes the out-of-root victim, by the exact names this test created and no others.
 *
 * Deliberately not a recursion and deliberately not remove_scratch_tree -- which would refuse it,
 * correctly, because it is outside the boundary. A test that needs something outside the boundary
 * has to clean it up by naming every part of it.
 */
void remove_victim(const std::wstring& victim) {
  DeleteFileW((victim + L"\\inner\\precious.txt").c_str());
  RemoveDirectoryW((victim + L"\\inner").c_str());
  RemoveDirectoryW(victim.c_str());
}

}  // namespace

int main() {
  std::cout << "test_scratch_dir_test\n";

  const std::wstring root = scratch_root();
  const std::string problem = scratch_root_problem();
  check("a scratch root exists", !root.empty(), problem.empty() ? narrow(root) : problem);
  if (root.empty()) {
    std::cout << "\nRESULT: FAILED  (no root, nothing else can be checked)\n";
    return 1;
  }
  check("...with nothing to report about it", problem.empty(), problem);
  check("...it is a real directory", exists(root));
  check("...and it is not a link", !is_reparse_point(root));
  check("...and no ancestor of it is a link either", reparse_ancestors(root).empty(),
        reparse_ancestors(root).empty() ? "" : narrow(reparse_ancestors(root).front()));
  // r5 claimed this because of where CMAKE_BINARY_DIR usually is. It is checked now, at runtime,
  // and a root that fails the check is refused rather than used -- so reaching here means it held.
  check("...and it really is inside the repository, not merely assumed to be",
        root.find(L"\\build") != std::wstring::npos || root.find(L"/build") != std::wstring::npos,
        narrow(root));

  // ------------------------------------------------------------------------- one run, one place
  const std::wstring run = scratch_run_dir();
  check("this run has its own directory", !run.empty(), narrow(run));
  check("...beneath the root", is_strictly_under(root, run));
  check("...named after this process, so two runs cannot share it",
        run.find(std::to_wstring(GetCurrentProcessId())) != std::wstring::npos, narrow(run));
  check("a named scratch path lands inside it", is_strictly_under(run, scratch_path(L"something")),
        narrow(scratch_path(L"something")));

  // ------------------------------------------------------------------------ the boundary itself
  check("the root is not strictly under itself", !is_strictly_under(root, root));
  check("a child is", is_strictly_under(root, root + L"\\child"));
  check("a grandchild is", is_strictly_under(root, root + L"\\a\\b\\c"));
  // The prefix trap: a sibling whose name STARTS with the root's name.
  check("a sibling with a longer name is not", !is_strictly_under(root, root + L"-elsewhere"));
  // The climb: a path that is textually under the root and canonically is not.
  check("a path that climbs back out is not",
        !is_strictly_under(root, root + L"\\..\\..\\Windows"));
  check("...even with a separator suffix", !is_strictly_under(root, root + L"\\child\\..\\.."));
  check("an unrelated absolute path is not", !is_strictly_under(root, L"C:\\Windows\\System32"));
  check("an empty path is not", !is_strictly_under(root, L""));

  // ----------------------------------------------------------------- what the remover refuses
  check("removing the root itself is refused", !remove_scratch_tree(root));
  check("...and the root is still there", exists(root));
  check("removing something outside the root is refused",
        !remove_scratch_tree(L"C:\\Windows\\System32\\drivers"));
  check("...and that directory is still there", exists(L"C:\\Windows\\System32\\drivers"));
  check("removing a path that climbs out is refused",
        !remove_scratch_tree(root + L"\\..\\..\\Windows"));
  check("...and Windows is still there", exists(L"C:\\Windows"));

  // ------------------------------------------------------------------------ ordinary removal
  {
    const std::wstring dir = make_scratch_dir(L"tree");
    check("a scratch directory was made", !dir.empty(), narrow(dir));
    CreateDirectoryW((dir + L"\\deep").c_str(), nullptr);
    CreateDirectoryW((dir + L"\\deep\\deeper").c_str(), nullptr);
    write_text(dir + L"\\top.txt", "x");
    write_text(dir + L"\\deep\\mid.txt", "x");
    write_text(dir + L"\\deep\\deeper\\bottom.txt", "x");
    check("a nested tree is removed whole", remove_scratch_tree(dir));
    check("...and it is gone", !exists(dir));
  }

  {
    const std::wstring dir = make_scratch_dir(L"readonly");
    write_text(dir + L"\\locked.txt", "x");
    SetFileAttributesW((dir + L"\\locked.txt").c_str(), FILE_ATTRIBUTE_READONLY);
    check("a read-only file does not stop the cleanup", remove_scratch_tree(dir));
    check("...and it is gone", !exists(dir));
  }

  {
    const std::wstring a = make_scratch_dir(L"unique");
    const std::wstring b = make_scratch_dir(L"unique");
    check("two scratch directories with the same tag are different", a != b,
          narrow(a) + " vs " + narrow(b));
    remove_scratch_tree(a);
    remove_scratch_tree(b);
  }

  // ==================================================== the junction, which is the point of all
  //
  // The victim lives OUTSIDE the scratch root -- a sibling of it, inside the build directory, so
  // it belongs to this build and to nobody else. That is what makes the counter-example real: a
  // delete that follows the junction leaves the boundary, and the check is whether something
  // outside it survived.
  {
    const std::wstring victim = root + L"-victim-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(victim.c_str(), nullptr);
    CreateDirectoryW((victim + L"\\inner").c_str(), nullptr);
    write_text(victim + L"\\inner\\precious.txt", "do not delete me");
    check("the out-of-root victim was set up", exists(victim + L"\\inner\\precious.txt"),
          narrow(victim));
    check("...and it really is outside the boundary", !is_strictly_under(root, victim),
          narrow(victim));

    const std::wstring holder = make_scratch_dir(L"junction-holder");
    const std::wstring link = holder + L"\\link-outside";

    if (!make_junction(link, victim)) {
      skip("a junction in the MIDDLE of the path is refused",
           "this filesystem or session would not create a junction (err " +
               std::to_string(GetLastError()) + ")");
      remove_scratch_tree(holder);
      remove_victim(victim);
    } else {
      check("the junction was created", is_reparse_point(link));
      check("...and it really does lead out of the boundary",
            exists(link + L"\\inner\\precious.txt"));

      // What the boundary says on its own, and why it is not enough.
      check("a path THROUGH the junction still looks like it is under the root",
            is_strictly_under(root, link + L"\\inner"),
            "textually under -- which is exactly the trap");
      check("...which is why the middle of the path is checked separately",
            has_reparse_between(root, link + L"\\inner"));
      check("...while an ordinary path has nothing in the middle",
            !has_reparse_between(root, holder));

      // r5's version deleted what this points at. The target here is `inner`, an ordinary
      // directory on the far side of the link, so nothing about IT would have raised a flag.
      check("a junction in the MIDDLE of the path is refused",
            !remove_scratch_tree(link + L"\\inner"));
      check("...and what the junction pointed at is untouched",
            exists(victim + L"\\inner\\precious.txt"));

      // And the case r5 did get right, kept: the link as the target is removed as a link.
      check("the holder is removed", remove_scratch_tree(holder));
      check("...the junction is gone", !exists(link));
      check("...and what it pointed at was still not touched",
            exists(victim + L"\\inner\\precious.txt"));

      remove_victim(victim);
      check("the victim is removed deliberately, by name, at the end", !exists(victim));
    }
  }

  const bool runGone = remove_scratch_run_dir();
  check("this run's directory is removed at the end", runGone, narrow(run));

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed, " << gSkips << " skipped)\n";
  return gFailures == 0 ? 0 : 1;
}
