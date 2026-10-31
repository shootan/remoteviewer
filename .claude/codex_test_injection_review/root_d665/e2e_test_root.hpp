// Where a guard test may make files (test-injection-finding r9, 2026-10-28: e2e_guard_runtime_test made and deleted
// files under %TEMP% -- outside the repository, against AGENTS.md's deletion boundary). Header-only.
//   * the root: REMOTE60_TEST_TMP, a plain-named folder DIRECTLY under the trusted anchor
//     D:\remote\remote\.claude\test-tmp; no component from the drive down to it a link (reparse point). Anything else
//     is refused BEFORE anything is created. No %TEMP%, no GetTempPath, no fallback.
//   * this run's folder: a NEW folder this process creates inside the root (never an existing one adopted);
//   * a child of the run is handed that folder (REMOTE60_E2E_RUN_DIR) and re-checks it: directly under the root,
//     named with the run's prefix, a plain folder that exists -- it creates no folder of its own;
//   * files: made with CREATE_NEW inside the run folder only; deleted only by the code that made them, only there.
// The run folder itself is left in place (the deletion boundary covers the files this test made, nothing more).
#ifndef REMOTE60_NATIVE_POC_E2E_TEST_ROOT_HPP
#define REMOTE60_NATIVE_POC_E2E_TEST_ROOT_HPP

#include <windows.h>

#include <cstring>
#include <cwchar>
#include <string>

namespace remote60::native_poc::e2e {

inline const wchar_t* e2e_test_anchor() { return L"D:\\remote\\remote\\.claude\\test-tmp"; }

inline bool e2e_is_link(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// Every existing component of `path` (from the drive down, `path` included) is not a link.
inline bool e2e_no_link_on_path(const std::wstring& path, std::string* why) {
  for (size_t at = path.find(L'\\', 3); ; at = path.find(L'\\', at + 1)) {
    const std::wstring part = at == std::wstring::npos ? path : path.substr(0, at);
    if (e2e_is_link(part)) {
      if (why) {
        *why = "a component of the test root is a link: ";
        for (wchar_t ch : part) why->push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
      }
      return false;
    }
    if (at == std::wstring::npos) return true;
  }
}

inline bool e2e_plain_name(const std::wstring& name) {
  if (name.empty() || name == L"." || name == L"..") return false;
  for (wchar_t ch : name) {
    const bool ok = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
                    ch == L'-' || ch == L'_' || ch == L'.';
    if (!ok) return false;
  }
  return true;
}

// The root named by REMOTE60_TEST_TMP, validated; created only when `create` and only after every check passed.
inline bool e2e_test_root(bool create, std::wstring* root, std::string* why) {
  wchar_t given[1024] = {};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", given, 1024);
  if (n == 0 || n >= 1024) {
    *why = "REMOTE60_TEST_TMP is not set";
    return false;
  }
  wchar_t full[1024] = {};
  wchar_t* namePart = nullptr;
  if (GetFullPathNameW(given, 1024, full, &namePart) == 0 || !namePart || !*namePart) {
    *why = "REMOTE60_TEST_TMP is not a usable path";
    return false;
  }
  const std::wstring name(namePart);
  if (!e2e_plain_name(name)) {
    *why = "the test root's name must be a plain ASCII folder name";
    return false;
  }
  std::wstring parent(full, namePart - full);
  while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), e2e_test_anchor()) != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp";
    return false;
  }
  const DWORD anchorAttr = GetFileAttributesW(e2e_test_anchor());
  if (anchorAttr == INVALID_FILE_ATTRIBUTES || !(anchorAttr & FILE_ATTRIBUTE_DIRECTORY)) {
    *why = "the test anchor is missing";
    return false;
  }
  const std::wstring r = std::wstring(e2e_test_anchor()) + L"\\" + name;
  if (!e2e_no_link_on_path(r, why)) return false;  // before anything is created
  if (create && !CreateDirectoryW(r.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    *why = "the test root could not be created";
    return false;
  }
  const DWORD a = GetFileAttributesW(r.c_str());
  if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) {
    *why = "the test root is not a plain folder";
    return false;
  }
  *root = r;
  return true;
}

// A NEW folder of this run inside the root: "<prefix><pid>-<tick>-<n>". Only a folder this process creates.
inline bool e2e_new_run_dir(const std::wstring& root, const wchar_t* prefix, std::wstring* run, std::string* why) {
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::wstring d = root + L"\\" + prefix + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
    if (CreateDirectoryW(d.c_str(), nullptr)) {
      if (e2e_is_link(d)) {
        *why = "this run's folder is a link";
        return false;
      }
      *run = d;
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
  }
  *why = "this run's own folder could not be created";
  return false;
}

// A child's check of the run folder it was handed: directly under the (validated, not created) root, named with
// `prefix`, an existing plain folder. Nothing is created.
inline bool e2e_run_dir_valid(const std::wstring& run, const wchar_t* prefix, std::string* why) {
  std::wstring root;
  if (!e2e_test_root(false, &root, why)) return false;
  const std::wstring head = root + L"\\";
  if (run.size() <= head.size() || _wcsnicmp(run.c_str(), head.c_str(), head.size()) != 0) {
    *why = "the run folder is not inside the test root";
    return false;
  }
  const std::wstring name = run.substr(head.size());
  if (!e2e_plain_name(name) || name.compare(0, std::wcslen(prefix), prefix) != 0) {
    *why = "the run folder is not this test's (directly under the root, its prefix)";
    return false;
  }
  const DWORD a = GetFileAttributesW(run.c_str());
  if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) {
    *why = "the run folder is not an existing plain folder";
    return false;
  }
  return true;
}

}  // namespace remote60::native_poc::e2e

#endif
