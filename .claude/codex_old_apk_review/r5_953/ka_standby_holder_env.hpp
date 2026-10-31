#pragma once
// rel-0164 old-APK lane r5 (verifier oa-s / oa-p 2026-10-30 00:14: the managed viewer exited at once, "unsupported
// codec: h264 (enable REMOTE60_NATIVE_ENCODED_EXPERIMENT or set env REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)"):
// the variables the standby holder's VIEWER must have are written into the child's own environment block -- never
// left to whatever the caller's environment happens to hold. The product shell does the same before it launches a
// managed viewer (client_managed_viewers.hpp:149). TEST TOOL ONLY. Pure, so ka_standby_holder_verdict_test checks it.
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace remote60::native_poc::ka_standby {

// `block`: a Windows environment block (NAME=value\0 ... \0\0). Every NAME in `set` replaces an entry of the same name
// (case-insensitive) or is added; the result is again a whole block.
inline std::vector<wchar_t> env_block_with(const std::vector<wchar_t>& block,
                                           const std::vector<std::pair<std::wstring, std::wstring>>& set) {
  auto upper = [](std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towupper(c));
    return s;
  };
  std::vector<wchar_t> out;
  for (size_t i = 0; i < block.size() && block[i] != L'\0';) {
    const std::wstring entry(&block[i]);
    i += entry.size() + 1;
    const size_t eq = entry.find(L'=', 1);  // an entry may start with '=' (the per-drive cwd ones)
    const std::wstring name = upper(eq == std::wstring::npos ? entry : entry.substr(0, eq));
    bool replaced = false;
    for (const auto& kv : set) replaced = replaced || upper(kv.first) == name;
    if (replaced) continue;
    out.insert(out.end(), entry.begin(), entry.end());
    out.push_back(L'\0');
  }
  for (const auto& kv : set) {
    const std::wstring entry = kv.first + L"=" + kv.second;
    out.insert(out.end(), entry.begin(), entry.end());
    out.push_back(L'\0');
  }
  out.push_back(L'\0');
  return out;
}

// What a value is in a block (empty when absent) -- for the checks.
inline std::wstring env_block_value(const std::vector<wchar_t>& block, const std::wstring& name) {
  for (size_t i = 0; i < block.size() && block[i] != L'\0';) {
    const std::wstring entry(&block[i]);
    i += entry.size() + 1;
    if (entry.size() > name.size() && entry[name.size()] == L'=' && _wcsnicmp(entry.c_str(), name.c_str(), name.size()) == 0)
      return entry.substr(name.size() + 1);
  }
  return L"";
}

}  // namespace remote60::native_poc::ka_standby
