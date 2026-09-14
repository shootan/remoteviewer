#pragma once

#include <windows.h>
#include <cstdio>
#include <mutex>
#include <string>

namespace remote60::native_poc {

// Process-owned, bounded fallback for numeric pipeline diagnostics. It does not depend on
// the supervisor's stdout pipe or upload thread. Never mirror arbitrary logs/credentials.
class HostDiagnosticLog {
 public:
  explicit HostDiagnosticLog(std::wstring base) : base_(std::move(base)) {}
  ~HostDiagnosticLog() {
    if (file_) std::fclose(file_);
    if (slotLock_ != INVALID_HANDLE_VALUE) CloseHandle(slotLock_);
  }
  static HostDiagnosticLog* Acquire(const std::wstring& dir) {
    // Four fixed banks bound storage across restarts, not just within one process.
    // An exclusive file handle prevents concurrent sessions from truncating each other.
    // Windows releases the handle on crashes; no stale lock cleanup or deletion is needed.
    for (unsigned n = 0; n < 4; ++n) {
      const unsigned slot = (GetCurrentProcessId() + n) % 4;
      const std::wstring base = dir + L"\\stream-slot-" + std::to_wstring(slot);
      HANDLE lock = CreateFileW((base + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (lock == INVALID_HANDLE_VALUE) continue;
      auto* sink = new HostDiagnosticLog(base);
      sink->slotLock_ = lock;
      return sink;
    }
    return new HostDiagnosticLog(L"");  // All banks busy: stdout remains available.
  }
  static bool Accept(const std::string& line) {
    return line.rfind("[native-video-host] wire seq=", 0) == 0 ||
           line.rfind("[native-video-host][capture-timing]", 0) == 0 ||
           line.rfind("[native-video-host][input-timing]", 0) == 0 ||
           line.rfind("[native-video-host] encodedFrames=", 0) == 0;
  }
  void Write(const std::string& stamp, const std::string& line) {
    if (!Accept(line) || line.size() > 8192 || base_.empty()) return;
    std::lock_guard<std::mutex> lock(mu_);
    if (!file_ || bytes_ + stamp.size() + line.size() > 8ULL * 1024 * 1024) {
      if (file_) { std::fclose(file_); file_ = nullptr; segment_ ^= 1; }
      const std::wstring path = base_ + L"." + std::to_wstring(segment_) + L".log";
      if (_wfopen_s(&file_, path.c_str(), L"wb") != 0) return;
      const int headerBytes = std::fprintf(file_, "# stream diagnostics pid=%lu segmentOpenTick=%llu\n",
                                            GetCurrentProcessId(), GetTickCount64());
      bytes_ = headerBytes > 0 ? static_cast<size_t>(headerBytes) : 0;
    }
    bytes_ += std::fwrite(stamp.data(), 1, stamp.size(), file_);
    bytes_ += std::fwrite(line.data(), 1, line.size(), file_);
    const ULONGLONG now = GetTickCount64();
    if (now - lastFlushMs_ >= 1000) { std::fflush(file_); lastFlushMs_ = now; }
  }
 private:
  std::wstring base_;
  std::mutex mu_;
  FILE* file_ = nullptr;
  HANDLE slotLock_ = INVALID_HANDLE_VALUE;
  size_t bytes_ = 0;
  unsigned segment_ = 0;
  ULONGLONG lastFlushMs_ = 0;
};

inline void mirror_host_diagnostic(const std::string& stamp, const std::string& line) {
  if (!HostDiagnosticLog::Accept(line)) return;
  // Deliberately process-lifetime: cout/cerr may write during static destruction.
  static HostDiagnosticLog* sink = []() {
    wchar_t appData[32768]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768);
    if (n == 0 || n >= 32768) return new HostDiagnosticLog(L"");
    std::wstring dir = std::wstring(appData) + L"\\remote60";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\diagnostics";
    CreateDirectoryW(dir.c_str(), nullptr);
    return HostDiagnosticLog::Acquire(dir);
  }();
  sink->Write(stamp, line);
}
}  // namespace remote60::native_poc
