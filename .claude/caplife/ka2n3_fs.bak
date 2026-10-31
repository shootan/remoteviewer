#pragma once

// keepalive-r2 ② (b, e; r3 Codex codex_keepalive_r2_scope2.md N3): the install's standby device id + last generation,
// in one small file (the app's files dir on Android; a profile dir on a PC).
//
// Failure model (what a Save that returned true survives): the app killed, and the OS crashing or losing power after
// Save returned. Save returns true only after the new contents are on the device and the replacement is durable:
//   write "<path>.tmp" -> flush the FILE to the device (FlushFileBuffers / fsync) -> close (checked) -> atomic replace
//   (MoveFileExW REPLACE_EXISTING|WRITE_THROUGH / rename) -> POSIX: fsync the directory, so the rename itself is durable.
// Any step failing -> false, and the caller sends nothing under the generation it was saving (GenerationAllocator).
// A crash between the steps leaves either the old file (generation <= any ever sent: nothing was sent under the new
// one) or the new one -- never a smaller generation for the same device id than one already sent.
//
// One writer: the store holds an exclusive lock file ("<path>.lock") from construction; a second store on the same
// path (another allocator, another process) cannot take it and every Load/Save of it fails -- closed, not shared.
// Paths are UTF-8 (std::string); Windows opens them as UTF-16 (non-ASCII directory names work), POSIX as bytes.
// Unreadable, short or long = nothing usable: GenerationAllocator makes a new device id (a new CSPRNG incarnation; the
// old entry at a host lapses on its lease). Nothing secret is in it (an id and a counter); it is never logged.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "standby_entry_client.hpp"

namespace remote60::native_poc {

class FileGenerationStore : public StandbyGenerationStore {
 public:
  // TEST ONLY: called at each step of a Save; returning false makes that step fail (fault injection). Product: empty.
  enum class Step { Write, Flush, Close, Replace, DirSync };
  using FaultHook = std::function<bool(Step)>;

  explicit FileGenerationStore(std::string path, FaultHook fault = nullptr) : path_(std::move(path)), fault_(std::move(fault)) {
    locked_ = Lock();
  }
  ~FileGenerationStore() override { Unlock(); }
  FileGenerationStore(const FileGenerationStore&) = delete;
  FileGenerationStore& operator=(const FileGenerationStore&) = delete;

  // false: another store holds this path (one writer per install), or the lock could not be made.
  bool owns() const { return locked_; }

  bool Load(std::array<uint8_t, 16>* id, uint32_t* last) override {
    if (!locked_) return false;
    uint8_t buf[21] = {};
    size_t n = 0;
#if defined(_WIN32)
    HANDLE h = CreateFileW(Wide(path_).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD got = 0;
    const BOOL ok = ReadFile(h, buf, sizeof(buf), &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    n = got;
#else
    const int fd = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t got = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    if (got < 0) return false;
    n = static_cast<size_t>(got);
#endif
    if (n != 20) return false;  // a short or longer file is damaged
    std::memcpy(id->data(), buf, 16);
    std::memcpy(last, buf + 16, 4);
    return true;
  }

  bool Save(const std::array<uint8_t, 16>& id, uint32_t last) override {
    if (!locked_) return false;
    uint8_t buf[20];
    std::memcpy(buf, id.data(), 16);
    std::memcpy(buf + 16, &last, 4);
    const std::string tmp = path_ + ".tmp";
#if defined(_WIN32)
    HANDLE h = CreateFileW(Wide(tmp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = Pass(Step::Write) && WriteFile(h, buf, sizeof(buf), &wrote, nullptr) && wrote == sizeof(buf);
    ok = ok && Pass(Step::Flush) && FlushFileBuffers(h);
    const bool closed = CloseHandle(h) != 0 && Pass(Step::Close);
    if (!ok || !closed) return false;
    return Pass(Step::Replace) &&
           MoveFileExW(Wide(tmp).c_str(), Wide(path_).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0 &&
           Pass(Step::DirSync);  // WRITE_THROUGH: the move is on the device when it returns
#else
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = Pass(Step::Write) && ::write(fd, buf, sizeof(buf)) == static_cast<ssize_t>(sizeof(buf));
    ok = ok && Pass(Step::Flush) && ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0 && Pass(Step::Close);
    if (!ok || !closed) return false;
    if (!Pass(Step::Replace) || ::rename(tmp.c_str(), path_.c_str()) != 0) return false;
    // the rename is durable only once its directory is
    const std::string dir = path_.find('/') == std::string::npos ? std::string(".") : path_.substr(0, path_.rfind('/'));
    const int dfd = ::open(dir.empty() ? "/" : dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (dfd < 0) return false;
    const bool synced = ::fsync(dfd) == 0;
    ::close(dfd);
    return synced && Pass(Step::DirSync);
#endif
  }

 private:
  bool Pass(Step s) const { return !fault_ || fault_(s); }

#if defined(_WIN32)
  static std::wstring Wide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
    return w;
  }
  bool Lock() {
    lock_ = CreateFileW(Wide(path_ + ".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0 /* no sharing: one writer */, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return lock_ != INVALID_HANDLE_VALUE;
  }
  void Unlock() {
    if (lock_ != INVALID_HANDLE_VALUE) CloseHandle(lock_);
    lock_ = INVALID_HANDLE_VALUE;
  }
  HANDLE lock_ = INVALID_HANDLE_VALUE;
#else
  bool Lock() {
    lock_ = ::open((path_ + ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_ < 0) return false;
    if (::flock(lock_, LOCK_EX | LOCK_NB) != 0) {
      ::close(lock_);
      lock_ = -1;
      return false;
    }
    return true;
  }
  void Unlock() {
    if (lock_ >= 0) ::close(lock_);  // the lock goes with the descriptor
    lock_ = -1;
  }
  int lock_ = -1;
#endif

  std::string path_;
  FaultHook fault_;
  bool locked_ = false;
};

}  // namespace remote60::native_poc
