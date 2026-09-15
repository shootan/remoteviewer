#pragma once
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>

namespace remote60::native_poc {
// Override one child variable without changing the parent's environment. Another concurrent
// launch (notably the updater) must not inherit a temporary capture-backend quarantine.
inline std::vector<wchar_t> child_environment_with(const std::wstring& name, const std::wstring& value,
                                                   bool overrideValue = true) {
  LPWCH raw = GetEnvironmentStringsW();
  if (!raw) throw std::runtime_error("cannot read child environment");
  struct Owner { LPWCH value; ~Owner() { FreeEnvironmentStringsW(value); } } owner{raw};
  std::vector<std::wstring> entries;
  const std::wstring prefix = name + L"=";
  for (const wchar_t* entry = raw; *entry; entry += std::wcslen(entry) + 1) {
    if (!overrideValue || _wcsnicmp(entry, prefix.c_str(), prefix.size()) != 0) entries.emplace_back(entry);
  }
  if (overrideValue) entries.push_back(prefix + value);
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
  std::vector<wchar_t> result;
  for (const auto& entry : entries) { result.insert(result.end(), entry.begin(), entry.end()); result.push_back(0); }
  result.push_back(0);
  return result;
}
}
