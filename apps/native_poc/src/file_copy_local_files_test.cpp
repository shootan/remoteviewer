// The helper's local-file side (file_copy_local_files.hpp) in process, against files this test
// makes in the repository's scratch root. (file-copy-helper r1, contract ⑤ ⑧)
//
// What is pinned down: a file is identified without being held (stat); a paste pins it with
// FILE_SHARE_READ so a writer / rename / delete is refused while it is held and refused if one
// was already there; the FileId the offer named is checked (a deleted-and-recreated file is
// refused as Replaced), and so are the size / time (Changed); reads are positional and content-
// exact; the lease is refreshed by reads and, with a fake clock, runs out and releases the
// handles; directories, .lnk / .url, junctions, device / stream syntax are never opened.
//
// Build: remote60_file_copy_local_files_test (CMake). Tags: pure-logic + filesystem (scratch).

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "file_copy_local_files.hpp"
#include "test_scratch_dir.hpp"

using namespace remote60::native_poc::file_copy;
namespace ts = remote60::native_poc::test_support;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : "  ", detail.c_str());
}

uint8_t content_byte(uint64_t i, uint32_t k) { return static_cast<uint8_t>((i * 131 + k * 7) & 0xFF); }

bool write_file(const std::wstring& path, uint64_t size, uint32_t k) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  std::vector<uint8_t> buf(1 << 16);
  uint64_t off = 0;
  while (off < size) {
    const DWORD n = static_cast<DWORD>(std::min<uint64_t>(buf.size(), size - off));
    for (DWORD i = 0; i < n; ++i) buf[i] = content_byte(off + i, k);
    DWORD w = 0;
    if (!WriteFile(h, buf.data(), n, &w, nullptr) || w != n) {
      CloseHandle(h);
      return false;
    }
    off += n;
  }
  CloseHandle(h);
  return true;
}

bool append_bytes(const std::wstring& path, size_t n) {
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  std::vector<uint8_t> z(n, 0xEE);
  DWORD w = 0;
  const bool ok = WriteFile(h, z.data(), static_cast<DWORD>(n), &w, nullptr) && w == n;
  CloseHandle(h);
  return ok;
}

HANDLE open_writer(const std::wstring& path) {
  return CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                     OPEN_EXISTING, 0, nullptr);
}

bool make_junction(const std::wstring& link, const std::wstring& target) {
  std::wstring cmd = L"cmd.exe /c mklink /J \"" + link + L"\" \"" + target + L"\"";
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(0);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
  WaitForSingleObject(pi.hProcess, 10000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0 && (GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::u16string u16(const std::wstring& s) { return std::u16string(s.begin(), s.end()); }

PinRequestEntry pin_entry(const std::wstring& path, const StatEntry& st, bool withSize = true) {
  PinRequestEntry e;
  e.path = u16(path);
  e.expectedId = st.id;
  e.expectedSize = withSize ? st.size : 0;
  e.expectedMtime = withSize ? st.mtime : 0;
  return e;
}

bool read_all_matches(LocalFileTable& t, uint64_t pinId, uint32_t index, uint64_t size, uint32_t k, std::string* why) {
  uint64_t off = 0;
  while (off < size) {
    std::vector<uint8_t> data;
    const Status st = t.Read(pinId, index, off, 65536, &data);
    if (st != Status::Ok) {
      *why = std::string("Read -> ") + status_name(st) + " at " + std::to_string(off);
      return false;
    }
    if (data.empty()) {
      *why = "empty read before EOF at " + std::to_string(off);
      return false;
    }
    for (size_t i = 0; i < data.size(); ++i) {
      if (data[i] != content_byte(off + i, k)) {
        *why = "byte mismatch at " + std::to_string(off + i);
        return false;
      }
    }
    off += data.size();
  }
  std::vector<uint8_t> tail;
  if (t.Read(pinId, index, size, 4096, &tail) != Status::Ok || !tail.empty()) {
    *why = "a read at EOF is not an empty Ok";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("--- path classification (text only) ---\n");
  check("device prefix \\\\.\\ is BadPath", classify_source_path(L"\\\\.\\C:\\x") == Status::BadPath);
  check("object-manager prefix \\\\?\\ is BadPath", classify_source_path(L"\\\\?\\C:\\x.txt") == Status::BadPath);
  check("\\??\\ is BadPath", classify_source_path(L"\\??\\C:\\x") == Status::BadPath);
  check("an alternate data stream is BadPath", classify_source_path(L"C:\\a.txt:secret") == Status::BadPath);
  check("CON: is BadPath", classify_source_path(L"CON:") == Status::BadPath);
  check("an empty path is BadPath", classify_source_path(L"") == Status::BadPath);
  check("a non-letter drive is BadPath", classify_source_path(L"2:\\x") == Status::BadPath);
  check(".lnk is Excluded", classify_source_path(L"C:\\x\\shortcut.lnk") == Status::Excluded);
  check(".URL (any case) is Excluded", classify_source_path(L"C:\\x\\site.URL") == Status::Excluded);
  check("an ordinary path is Ok", classify_source_path(L"C:\\Users\\me\\a.bin") == Status::Ok);
  check("a UNC path is Ok (the open decides)", classify_source_path(L"\\\\server\\share\\a.bin") == Status::Ok);
  check("basename", basename_of(L"C:\\x\\y\\a.bin") == u"a.bin" && basename_of(L"a.bin") == u"a.bin");

  const std::wstring root = ts::make_scratch_dir(L"file_copy_local");
  check("a scratch directory inside the repository", !root.empty(), ts::scratch_root_problem());
  if (root.empty()) return 1;
  const std::wstring a = root + L"\\a.bin";
  const std::wstring b = root + L"\\b.txt";
  const std::wstring empty = root + L"\\empty.bin";
  const std::wstring dir = root + L"\\dir";
  const std::wstring lnk = root + L"\\x.lnk";
  const std::wstring junction = root + L"\\jn";
  const uint64_t aSize = 300 * 1024 + 17;
  check("fixtures", write_file(a, aSize, 0) && write_file(b, 10, 1) && write_file(empty, 0, 2) && CreateDirectoryW(dir.c_str(), nullptr) &&
                        write_file(lnk, 3, 3));
  const bool haveJunction = make_junction(junction, dir);
  check("a junction fixture (mklink /J)", haveJunction);

  std::printf("\n--- stat: identify without holding ---\n");
  const StatEntry sa = stat_source_file(a);
  const StatEntry sb = stat_source_file(b);
  const StatEntry se = stat_source_file(empty);
  check("a.bin: Ok, size, name, an id", sa.status == Status::Ok && sa.size == aSize && sa.name == u"a.bin" && sa.id.volumeSerial != 0,
        status_name(sa.status));
  check("b.txt: Ok", sb.status == Status::Ok && sb.size == 10 && sb.name == u"b.txt");
  check("empty.bin: Ok with size 0", se.status == Status::Ok && se.size == 0);
  check("two files have different ids", sa.id != sb.id);
  check("a directory is NotAFile", stat_source_file(dir).status == Status::NotAFile);
  check("a .lnk is Excluded before any open", stat_source_file(lnk).status == Status::Excluded);
  if (haveJunction) check("a junction is Excluded (not followed)", stat_source_file(junction).status == Status::Excluded);
  check("a missing file is NotFound", stat_source_file(root + L"\\missing.bin").status == Status::NotFound);
  check("a device path is BadPath without an open", stat_source_file(L"\\\\.\\PhysicalDrive0").status == Status::BadPath);
  {
    HANDLE w = open_writer(a);
    const StatEntry s = stat_source_file(a);
    check("stat succeeds while a writer holds the file (offer is allowed; the paste decides)", w != INVALID_HANDLE_VALUE &&
                                                                                               s.status == Status::Ok && s.id == sa.id);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
  }

  std::printf("\n--- pin: hold for the paste, as the user ---\n");
  uint64_t clock = 1000;
  LocalFileTable t([&clock] { return clock; });
  {
    std::vector<PinResultEntry> r;
    check("Pin a + b", t.Pin(1, 60000, {pin_entry(a, sa), pin_entry(b, sb)}, &r) && r.size() == 2 && r[0].status == Status::Ok &&
                           r[1].status == Status::Ok && r[0].id == sa.id && r[0].size == aSize && t.pinned_handles() == 2);
    std::string why;
    check("a.bin reads back exactly (64 KiB positional reads, empty Ok at EOF)", read_all_matches(t, 1, 0, aSize, 0, &why), why);
    check("b.txt reads back exactly", read_all_matches(t, 1, 1, 10, 1, &why), why);
    std::vector<uint8_t> d;
    check("offset past the end is BadRequest", t.Read(1, 0, aSize + 1, 16, &d) == Status::BadRequest);
    check("length over the chunk bound is BadRequest", t.Read(1, 0, 0, kMaxChunkBytes + 1, &d) == Status::BadRequest);
    check("an index outside the pin is BadRequest", t.Read(1, 5, 0, 16, &d) == Status::BadRequest);
    check("an unknown pin is UnknownId", t.Read(99, 0, 0, 16, &d) == Status::UnknownId);
    HANDLE w = open_writer(a);
    check("A WRITER IS REFUSED WHILE PINNED (ERROR_SHARING_VIOLATION)", w == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION,
          "err=" + std::to_string(GetLastError()));
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    check("...and so is a delete", !DeleteFileW(a.c_str()) && GetLastError() == ERROR_SHARING_VIOLATION);
    check("...and a rename", !MoveFileW(a.c_str(), (root + L"\\a2.bin").c_str()) && GetLastError() == ERROR_SHARING_VIOLATION);
    check("Unpin releases both", t.Unpin(1) == 2 && t.pinned_handles() == 0);
    w = open_writer(a);
    check("...and the writer may open now", w != INVALID_HANDLE_VALUE);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    check("Unpin of an unknown pin is 0", t.Unpin(1) == 0);
  }

  std::printf("\n--- the counter-examples ---\n");
  {
    // Replaced: b.txt deleted and recreated under the same name -> a different FileId.
    check("delete + recreate b.txt", DeleteFileW(b.c_str()) && write_file(b, 10, 1));
    const StatEntry sb2 = stat_source_file(b);
    check("...the recreated file has a new id", sb2.status == Status::Ok && sb2.id != sb.id);
    std::vector<PinResultEntry> r;
    check("Pin with the OLD id is Replaced and holds nothing", t.Pin(2, 60000, {pin_entry(b, sb)}, &r) && r[0].status == Status::Replaced &&
                                                                    t.pinned_handles() == 0);
    check("Pin with the new id is Ok", t.Pin(2, 60000, {pin_entry(b, sb2)}, &r) && r[0].status == Status::Ok && t.Unpin(2) == 1);
  }
  {
    // Changed: the same file grew since the offer.
    check("append to a.bin", append_bytes(a, 5));
    std::vector<PinResultEntry> r;
    check("Pin with the offer's size is Changed (same id, different size)",
          t.Pin(3, 60000, {pin_entry(a, sa)}, &r) && r[0].status == Status::Changed && t.pinned_handles() == 0);
    const StatEntry sa2 = stat_source_file(a);
    check("Pin with the current size is Ok", t.Pin(3, 60000, {pin_entry(a, sa2)}, &r) && r[0].status == Status::Ok && r[0].size == aSize + 5 &&
                                                 t.Unpin(3) == 1);
    check("Pin without an expected size / time checks the id only", t.Pin(3, 60000, {pin_entry(a, sa, false)}, &r) &&
                                                                        r[0].status == Status::Ok && t.Unpin(3) == 1);
  }
  {
    // A writer already there: refused, no privileged retry.
    HANDLE w = open_writer(b);
    const StatEntry sb2 = stat_source_file(b);
    std::vector<PinResultEntry> r;
    check("Pin while a writer holds the file is SharingViolation", w != INVALID_HANDLE_VALUE &&
                                                                       t.Pin(4, 60000, {pin_entry(b, sb2)}, &r) &&
                                                                       r[0].status == Status::SharingViolation && t.pinned_handles() == 0);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
  }
  {
    // Mixed: the refused entries do not stop the good ones.
    const StatEntry sa2 = stat_source_file(a);
    PinRequestEntry d;
    d.path = u16(dir);
    PinRequestEntry l;
    l.path = u16(lnk);
    PinRequestEntry m;
    m.path = u16(root + L"\\missing.bin");
    PinRequestEntry dev;
    dev.path = u"\\\\.\\PhysicalDrive0";
    std::vector<PinResultEntry> r;
    check("a directory / .lnk / missing / device entry each refused, the file held",
          t.Pin(5, 60000, {pin_entry(a, sa2), d, l, m, dev}, &r) && r.size() == 5 && r[0].status == Status::Ok && r[1].status == Status::NotAFile &&
              r[2].status == Status::Excluded && r[3].status == Status::NotFound && r[4].status == Status::BadPath && t.pinned_handles() == 1);
    std::vector<uint8_t> data;
    check("a read on a refused entry is UnknownId", t.Read(5, 1, 0, 16, &data) == Status::UnknownId);
    if (haveJunction) {
      PinRequestEntry j;
      j.path = u16(junction);
      check("a junction entry is Excluded", t.Pin(6, 60000, {j}, &r) && r[0].status == Status::Excluded);
      t.Unpin(6);
    }
    t.Unpin(5);
  }

  std::printf("\n--- the lease (fake clock) ---\n");
  {
    const StatEntry sa2 = stat_source_file(a);
    std::vector<PinResultEntry> r;
    clock = 10000;
    check("Pin with a 1000 ms lease", t.Pin(7, 1000, {pin_entry(a, sa2)}, &r) && r[0].status == Status::Ok);
    std::vector<uint8_t> d;
    clock = 10800;
    check("a read at 800 ms is Ok and refreshes the lease", t.Read(7, 0, 0, 16, &d) == Status::Ok && d.size() == 16);
    clock = 11600;
    check("a read at 1600 ms (800 since the last) is still Ok", t.Read(7, 0, 16, 16, &d) == Status::Ok);
    clock = 12700;
    check("A READ 1100 ms AFTER THE LAST IS LeaseExpired AND THE HANDLE IS GONE", t.Read(7, 0, 32, 16, &d) == Status::LeaseExpired &&
                                                                                       t.pinned_handles() == 0 && t.pin_count() == 0);
    HANDLE w = open_writer(a);
    check("...so a writer may open", w != INVALID_HANDLE_VALUE);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
    check("Pin again, then ExpireLeases after the lease releases it", t.Pin(8, 1000, {pin_entry(a, sa2)}, &r) && t.ExpireLeases() == 0 &&
                                                                          (clock += 1001, t.ExpireLeases() == 1) && t.pinned_handles() == 0);
    check("...an expired pin is UnknownId afterwards", t.Read(8, 0, 0, 16, &d) == Status::UnknownId);
  }
  {
    const StatEntry sa2 = stat_source_file(a);
    std::vector<PinResultEntry> r;
    t.Pin(9, 60000, {pin_entry(a, sa2)}, &r);
    t.ReleaseAll();
    check("ReleaseAll closes everything", t.pinned_handles() == 0 && t.pin_count() == 0);
    HANDLE w = open_writer(a);
    check("...and the writer may open", w != INVALID_HANDLE_VALUE);
    if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
  }

  check("the scratch run directory is removed", ts::remove_scratch_run_dir());
  std::printf("\n%s  (%d checks, %d failed)\n", gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
