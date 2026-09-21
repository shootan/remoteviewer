#include "host_app_log.hpp"

#include <windows.h>

#include <share.h>

namespace remote60::native_poc {
namespace {

std::wstring default_log_path() {
  wchar_t base[MAX_PATH] = {};
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  std::wstring dir = std::wstring(base) + L"\\GNLink";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\host_app.log";
}

}  // namespace

HostAppLog::HostAppLog(std::wstring path, uint64_t rotateAtBytes, int maxBackups)
    : path_(std::move(path)), rotateAtBytes_(rotateAtBytes), maxBackups_(maxBackups) {}

HostAppLog::~HostAppLog() {
  if (file_) std::fclose(file_);
}

void HostAppLog::Close() {
  std::lock_guard<std::mutex> lock(mu_);
  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }
}

HostAppLogStats HostAppLog::stats() const {
  std::lock_guard<std::mutex> lock(mu_);
  return stats_;
}

bool HostAppLog::WriteStamped(const std::string& line) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  char stamp[24]{};
  std::snprintf(stamp, sizeof(stamp), "%02d-%02d %02d:%02d:%02d ", now.wMonth, now.wDay,
                now.wHour, now.wMinute, now.wSecond);
  // The shape the updater's health scanner parses. Byte-identical to what append_host_app_log
  // wrote before, on purpose: an already-installed updater reads this file and must keep working.
  return WriteLocked(std::string(stamp) + line + "\n");
}

bool HostAppLog::WriteRaw(const std::string& line) {
  return WriteLocked(line + "\n");
}

bool HostAppLog::WriteLocked(const std::string& text) {
  std::lock_guard<std::mutex> lock(mu_);
  if (path_.empty()) {
    ++stats_.failed;
    return false;
  }
  if (!file_) {
    // _wfsopen with _SH_DENYNO rather than fopen: the CRT's fopen family asks for no sharing, so
    // it opens deny-write and a second writer -- or a tail, or the Open log button -- is refused.
    // That refusal is what silently swallowed the health report while the child was running.
    file_ = _wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
  }
  if (!file_) {
    ++stats_.failed;
    return false;
  }

  // Rotation is here, under the same lock as the write, so nothing can append into a file that is
  // being renamed out from under it. It used to live in the reader's loop, where the other writer
  // could not see it.
  if (rotateAtBytes_ > 0 && static_cast<uint64_t>(_ftelli64(file_)) > rotateAtBytes_) {
    RotateLocked();
  }
  if (!file_) {
    ++stats_.failed;
    return false;
  }

  if (std::fputs(text.c_str(), file_) == EOF || std::fflush(file_) != 0) {
    ++stats_.failed;
    // A write that failed leaves the handle in an unknown state; the next line reopens.
    std::fclose(file_);
    file_ = nullptr;
    return false;
  }
  ++stats_.written;
  return true;
}

void HostAppLog::RotateLocked() {
  std::fclose(file_);
  file_ = nullptr;

  // Numbered generations: host_app.log.1 is the newest backup, .N the oldest, and the oldest goes.
  // A single .old meant the second rotation destroyed the window a long repro needed.
  DeleteFileW((path_ + L"." + std::to_wstring(maxBackups_)).c_str());
  for (int i = maxBackups_ - 1; i >= 1; --i) {
    MoveFileExW((path_ + L"." + std::to_wstring(i)).c_str(),
                (path_ + L"." + std::to_wstring(i + 1)).c_str(), MOVEFILE_REPLACE_EXISTING);
  }
  if (MoveFileExW(path_.c_str(), (path_ + L".1").c_str(), MOVEFILE_REPLACE_EXISTING)) {
    ++stats_.rotations;
  } else {
    // A viewer holding the file open refuses the rename. The file then runs past the cap until it
    // lets go, which is better than losing the line we were about to write.
    ++stats_.rotationsRefused;
  }

  // The legacy single-generation .old, adopted AFTER the shift into the first free slot ascending.
  // The rotation deletes .N first and assumes the generations are contiguous from .1, so parking
  // it at a sparse high slot would erase it on the very next rotation.
  const std::wstring legacy = path_ + L".old";
  if (GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
    for (int i = 2; i <= maxBackups_; ++i) {
      if (MoveFileExW(legacy.c_str(), (path_ + L"." + std::to_wstring(i)).c_str(), 0)) break;
    }
  }

  file_ = _wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
}

HostAppLog& host_app_log() {
  static HostAppLog log(default_log_path());
  return log;
}

}  // namespace remote60::native_poc
