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
  ~HostDiagnosticLog() { if (file_) std::fclose(file_); }
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
      bytes_ = 0;
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
    return new HostDiagnosticLog(dir + L"\\stream-" + std::to_wstring(GetCurrentProcessId()) +
                                 L"-" + std::to_wstring(GetTickCount64()));
  }();
  sink->Write(stamp, line);
}
}  // namespace remote60::native_poc
