#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// The boundary that keeps a test's cleanup inside the test. (updater-abandon-race r5, item 3)
//
// This checks the safety property rather than the convenience: a recursive delete is about to be
// run from an elevated shell (item G's service fixture), and the two ways that goes wrong are a
// path that is not where it was meant to be and a junction that leads somewhere else entirely.
// Both are produced here rather than reasoned about.

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

/**
 * The mount-point form of a reparse point buffer.
 *
 * Declared here because REPARSE_DATA_BUFFER lives in ntifs.h, which is a driver header: user-mode
 * code that wants to CREATE one has to say what it looks like. The layout is the documented one --
 * an eight byte header, then four offsets into a path buffer that holds the substitute name and
 * the print name back to back, each terminated.
 */
// The NT object-manager prefix a junction stores, built rather than written so the escapes
// cannot be mangled: backslash, question, question, backslash. Not the win32 long-path prefix,
// which looks similar and is a different thing -- a junction stores the object-manager form.
const std::wstring kNtPrefix =
    std::wstring(1, wchar_t(92)) + std::wstring(2, wchar_t(63)) + wchar_t(92);

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

}  // namespace

int main() {
  std::cout << "test_scratch_dir_test\n";

  const std::wstring root = scratch_root();
  check("a scratch root exists", !root.empty(), narrow(root));
  if (root.empty()) {
    std::cout << "\nRESULT: FAILED  (no root, nothing else can be checked)\n";
    return 1;
  }
  check("...it is a real directory", exists(root));
  check("...and it is not a link", !is_reparse_point(root));
  check("...and it is inside the build tree, not a shared temp directory",
        root.find(L"\\build") != std::wstring::npos || root.find(L"/build") != std::wstring::npos,
        narrow(root));

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

  // -------------------------------------------------------------- the junction, which is the point
  {
    const std::wstring victim = make_scratch_dir(L"junction-target");
    const std::wstring holder = make_scratch_dir(L"junction-holder");
    write_text(victim + L"\\precious.txt", "do not delete me");
    const std::wstring link = holder + L"\\link-to-victim";

    if (!make_junction(link, victim)) {
      skip("a junction is removed as a link, not followed",
           "this filesystem or session would not create a junction (err " +
               std::to_string(GetLastError()) + ")");
      remove_scratch_tree(holder);
      remove_scratch_tree(victim);
    } else {
      check("the junction was created", is_reparse_point(link));
      check("...and it really does lead to the victim", exists(link + L"\\precious.txt"));

      check("the holder is removed", remove_scratch_tree(holder));
      check("...the junction is gone", !exists(link));
      // The whole reason this file exists.
      check("...and what it pointed at was NOT touched", exists(victim + L"\\precious.txt"));

      remove_scratch_tree(victim);
      check("the victim can then be removed deliberately", !exists(victim));
    }
  }

  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED") << "  (" << gChecks
            << " checks, " << gFailures << " failed, " << gSkips << " skipped)\n";
  return gFailures == 0 ? 0 : 1;
}
