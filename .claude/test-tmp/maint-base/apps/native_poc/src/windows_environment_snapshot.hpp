#pragma once

#include <windows.h>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>

namespace remote60::native_poc {

// A process-local override must restore the exact original, including long and empty values.
// GetEnvironmentStrings gives a stable copy rather than racing a size query with a later read.
class WindowsEnvironmentSnapshot {
 public:
  explicit WindowsEnvironmentSnapshot(const wchar_t* name) : name_(name) {
    std::unique_ptr<wchar_t, decltype(&FreeEnvironmentStringsW)> block(GetEnvironmentStringsW(), &FreeEnvironmentStringsW);
    if (!block) return;
    captured_ = true;
    for (const wchar_t* p = block.get(); *p; p += std::wcslen(p) + 1) {
      if (_wcsnicmp(p, name_.c_str(), name_.size()) == 0 && p[name_.size()] == L'=') {
        original_ = p + name_.size() + 1;
        break;
      }
    }
  }
  ~WindowsEnvironmentSnapshot() { if (captured_) SetEnvironmentVariableW(name_.c_str(), Original()); }
  WindowsEnvironmentSnapshot(const WindowsEnvironmentSnapshot&) = delete;
  WindowsEnvironmentSnapshot& operator=(const WindowsEnvironmentSnapshot&) = delete;
  const wchar_t* Original() const { return original_ ? original_->c_str() : nullptr; }
  bool Set(const wchar_t* value) const {
    return captured_ && SetEnvironmentVariableW(name_.c_str(), value) != FALSE;
  }
 private:
  std::wstring name_;
  std::optional<std::wstring> original_;
  bool captured_ = false;
};
}  // namespace remote60::native_poc
