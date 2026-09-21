#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// The single writer for host_app.log keeps every line. (updater-health-gate D1, items 1 and 3)
//
// What it is defending: the updater's health gate reads this file for
// "[host-app] health version=X directory=ok", and that line is written while a streaming child is
// running and filling the same file with its stdout. Before this, those were two independent
// writers -- one holding the file, one unable to open it -- and the health line did not arrive.
// The gate then rolled a good update back.
//
// Every case below writes lines carrying a record id and counts how many come back, across
// rotation as well, because "it wrote" and "it is still there" are different claims.

#include <windows.h>

#include <atomic>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "host_app_log.hpp"

using namespace remote60::native_poc;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::cout << (ok ? "PASS  " : "FAIL  ") << name;
  if (!detail.empty()) std::cout << "  " << detail;
  std::cout << "\n";
}

std::wstring scratch_dir() {
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  std::wstring dir = std::wstring(temp) + L"gnlink-hostlog-" + std::to_wstring(GetCurrentProcessId());
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::string read_all(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

/** Every generation together: a line that survived rotation is still a line that survived. */
std::string read_all_generations(const std::wstring& path, int maxBackups = 10) {
  std::string all = read_all(path);
  for (int i = 1; i <= maxBackups; ++i) all += read_all(path + L"." + std::to_wstring(i));
  return all;
}

size_t count_ids(const std::string& hay, const std::string& prefix, int n) {
  size_t found = 0;
  for (int i = 0; i < n; ++i) {
    if (hay.find(prefix + std::to_string(i) + "#") != std::string::npos) ++found;
  }
  return found;
}

void remove_all(const std::wstring& dir) {
  WIN32_FIND_DATAW find{};
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &find);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = find.cFileName;
      if (name != L"." && name != L"..") DeleteFileW((dir + L"\\" + name).c_str());
    } while (FindNextFileW(h, &find));
    FindClose(h);
  }
  RemoveDirectoryW(dir.c_str());
}

}  // namespace

int main() {
  std::cout << "host_app_log_test\n";
  const std::wstring dir = scratch_dir();

  // ------------------------------------------------- the case the incident was: both at once
  {
    const std::wstring path = dir + L"\\both.log";
    DeleteFileW(path.c_str());
    HostAppLog log(path, 0);  // no rotation here; this is about the two writers

    // The child-output side, as fast as a busy host produces lines.
    constexpr int kRaw = 4000;
    constexpr int kStamped = 200;
    std::atomic<bool> go{false};
    std::thread child([&] {
      while (!go.load()) Sleep(0);
      for (int i = 0; i < kRaw; ++i) {
        log.WriteRaw("09-21 15:11:54.282 [native-video-host] child raw-" + std::to_string(i) +
                     "# padding-to-a-realistic-length-01234567890123456789");
      }
    });

    std::vector<bool> ok(kStamped, false);
    go.store(true);
    for (int i = 0; i < kStamped; ++i) {
      ok[i] = log.WriteStamped("[host-app] health version=0.2.131 directory=ok stamped-" +
                               std::to_string(i) + "#");
      Sleep(0);
    }
    child.join();
    log.Close();

    const std::string all = read_all(path);
    size_t reported = 0;
    for (bool b : ok) if (b) ++reported;
    check("every supervisor line reports that it was written", reported == kStamped,
          std::to_string(reported) + " of " + std::to_string(kStamped));
    check("...and every one of them is in the file",
          count_ids(all, "stamped-", kStamped) == kStamped,
          std::to_string(count_ids(all, "stamped-", kStamped)) + " of " +
              std::to_string(kStamped));
    check("...while the child's lines are all there too",
          count_ids(all, "raw-", kRaw) == kRaw,
          std::to_string(count_ids(all, "raw-", kRaw)) + " of " + std::to_string(kRaw));

    const HostAppLogStats stats = log.stats();
    check("nothing was counted as lost", stats.failed == 0, std::to_string(stats.failed));
    check("...and the written count matches what went in",
          stats.written == kRaw + kStamped, std::to_string(stats.written));
  }

  // ------------------------------------------------- rotation, with both kinds interleaving
  {
    const std::wstring path = dir + L"\\rotate.log";
    DeleteFileW(path.c_str());
    for (int i = 1; i <= 10; ++i) DeleteFileW((path + L"." + std::to_wstring(i)).c_str());
    HostAppLog log(path, 64 * 1024);  // small cap, so this rotates several times

    constexpr int kRaw = 3000;
    constexpr int kStamped = 300;
    std::atomic<bool> go{false};
    std::thread child([&] {
      while (!go.load()) Sleep(0);
      for (int i = 0; i < kRaw; ++i) {
        log.WriteRaw("09-21 15:11:54.282 [native-video-host] rot raw-" + std::to_string(i) +
                     "# padding-padding-padding-0123456789012345678901234567890123456789");
      }
    });
    go.store(true);
    for (int i = 0; i < kStamped; ++i) {
      log.WriteStamped("[host-app] health rot stamped-" + std::to_string(i) + "#");
      Sleep(0);
    }
    child.join();
    log.Close();

    const HostAppLogStats stats = log.stats();
    check("the cap actually rotated the file", stats.rotations > 1,
          std::to_string(stats.rotations) + " rotations");
    const std::string all = read_all_generations(path);
    check("no supervisor line was lost across a rotation",
          count_ids(all, "rot stamped-", kStamped) == kStamped,
          std::to_string(count_ids(all, "rot stamped-", kStamped)) + " of " +
              std::to_string(kStamped));
    check("no child line was lost across a rotation",
          count_ids(all, "rot raw-", kRaw) == kRaw,
          std::to_string(count_ids(all, "rot raw-", kRaw)) + " of " + std::to_string(kRaw));
    check("and nothing was counted as lost", stats.failed == 0, std::to_string(stats.failed));

    // File identity: after rotation the live file is a different file from the backup, and the
    // writer is writing to the live one. Checked by index rather than by name.
    BY_HANDLE_FILE_INFORMATION liveInfo{}, backupInfo{};
    HANDLE live = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE backup = CreateFileW((path + L".1").c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    const bool haveBoth = live != INVALID_HANDLE_VALUE && backup != INVALID_HANDLE_VALUE &&
                          GetFileInformationByHandle(live, &liveInfo) &&
                          GetFileInformationByHandle(backup, &backupInfo);
    check("the rotated-out generation is a different file, not the same one renamed back",
          haveBoth && (liveInfo.nFileIndexLow != backupInfo.nFileIndexLow ||
                       liveInfo.nFileIndexHigh != backupInfo.nFileIndexHigh));
    if (live != INVALID_HANDLE_VALUE) CloseHandle(live);
    if (backup != INVALID_HANDLE_VALUE) CloseHandle(backup);
  }

  // ------------------------------------------------- a failure is returned, not swallowed
  {
    // A path that cannot be opened. The old code returned in silence here, which is how the
    // health report vanished without anything recording that it had.
    HostAppLog broken(dir + L"\\no-such-dir\\nope.log");
    check("a line that cannot be written says so", !broken.WriteStamped("[host-app] health"));
    check("...and is counted", broken.stats().failed == 1,
          std::to_string(broken.stats().failed));
    check("...and is not counted as written", broken.stats().written == 0);

    HostAppLog nowhere(L"");
    check("an unusable log path fails rather than pretending", !nowhere.WriteRaw("x"));
    check("...and counts it", nowhere.stats().failed == 1);
  }

  // ------------------------------------------------- the line shape the old updater parses
  {
    // Byte-for-byte what append_host_app_log wrote before. An updater that is already installed
    // reads this file, and the whole point of fixing the writer is that it now arrives -- not
    // that it arrives in a new shape the old reader cannot match.
    const std::wstring path = dir + L"\\shape.log";
    DeleteFileW(path.c_str());
    {
      HostAppLog log(path, 0);
      log.WriteStamped("[host-app] health version=0.2.131 directory=ok");
      log.Close();
    }
    const std::string all = read_all(path);
    check("the health line still carries the stamp the old format had",
          all.size() > 15 && all[2] == '-' && all[5] == ' ' && all[8] == ':' && all[11] == ':',
          all.substr(0, 20));
    check("...followed by the exact text the scanner looks for",
          all.find(" [host-app] health version=0.2.131 directory=ok\n") != std::string::npos,
          all);
  }

  remove_all(dir);
  std::cout << "\n" << (gFailures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILED")
            << "  (" << gChecks << " checks, " << gFailures << " failed)\n";
  return gFailures == 0 ? 0 : 1;
}
