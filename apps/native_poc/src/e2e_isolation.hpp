#pragma once

// Keeping end-to-end tests off the user's machine. (RV-00, 2026-09-23)
//
// On 2026-09-23 two tests overwrote the user's real %LOCALAPPDATA%\remote60\host.json and
// %LOCALAPPDATA%\GNLink\client.txt: a host started by an e2e registered with a fake directory and
// saved its token cache to the default per-user path, and a test that compiles the shell in wrote
// its settings file there. Nothing about either was malicious or even unusual -- the product did
// exactly what it does for a user, against the user's own files, because nothing told it otherwise.
//
// So every launch of a product process from a test goes through two things here:
//
//   e2e_isolated_environment  -- the child's environment, with LOCALAPPDATA pointed into the
//                                test's staging directory. The diagnostic mirror and every other
//                                path that reads the variable follow it. Built as an explicit block
//                                for the child, so the test process's own environment is untouched.
//   e2e_command_is_isolated   -- a check made BEFORE the process starts: a command line that talks
//                                to a directory must also say where to keep its cache, and that
//                                place must be under the staging directory. Paths resolved through
//                                SHGetKnownFolderPath ignore the environment, which is why the cache
//                                needs its own argument and cannot rely on LOCALAPPDATA alone.
//
// The guard is what makes the mutation test safe. Removing the isolation argument makes the check
// refuse and the host is never launched -- a mutation that would otherwise have written to the
// user's real file becomes a failed assertion instead.

// winsock2 before windows.h, whichever of the two a test includes first: windows.h alone drags in
// the old winsock.h and every later winsock2 include then redefines its structs.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include <string>
#include <vector>

namespace remote60::native_poc::e2e {

inline std::wstring e2e_lower(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
  return s;
}

/** Normalised for a prefix test: lower case, forward slashes turned back, no trailing separator. */
inline std::wstring e2e_norm_path(std::wstring p) {
  for (auto& c : p) {
    if (c == L'/') c = L'\\';
  }
  while (!p.empty() && p.back() == L'\\') p.pop_back();
  return e2e_lower(p);
}

inline bool e2e_path_is_under(const std::wstring& path, const std::wstring& root) {
  const std::wstring p = e2e_norm_path(path);
  const std::wstring r = e2e_norm_path(root);
  if (r.empty() || p.size() <= r.size()) return false;
  return p.compare(0, r.size(), r) == 0 && p[r.size()] == L'\\';
}

/** The value following `flag` in a command line, with quotes removed. Empty if absent. */
inline std::wstring e2e_arg_value(const std::wstring& cmd, const std::wstring& flag) {
  const std::wstring needle = L" " + flag + L" ";
  const size_t at = cmd.find(needle);
  if (at == std::wstring::npos) return {};
  size_t i = at + needle.size();
  if (i < cmd.size() && cmd[i] == L'"') {
    const size_t end = cmd.find(L'"', i + 1);
    return end == std::wstring::npos ? std::wstring() : cmd.substr(i + 1, end - i - 1);
  }
  const size_t end = cmd.find(L' ', i);
  return cmd.substr(i, end == std::wstring::npos ? std::wstring::npos : end - i);
}

/**
 * Whether a product command line is safe to launch from a test.
 *
 * A command line that names a directory must also name a cache under the staging directory. One
 * that does not talk to a directory has no cache to worry about. `why` says which rule failed.
 */
inline bool e2e_command_is_isolated(const std::wstring& cmd, const std::wstring& stagingDir,
                                    std::string* why) {
  // Only the host keeps a directory token cache (and the viewer, which also takes --directory-url,
  // has no cache and reaches LOCALAPPDATA only through the environment -- viewer_unlock.cpp).
  const bool isHost = e2e_lower(cmd).find(L"gnlinkstream.exe") != std::wstring::npos;
  const bool usesDirectory = isHost && cmd.find(L" --directory-url ") != std::wstring::npos;
  if (!usesDirectory) return true;
  const std::wstring cache = e2e_arg_value(cmd, L"--directory-cache");
  if (cache.empty()) {
    if (why) *why = "--directory-url without --directory-cache: the cache would go to the user's real path";
    return false;
  }
  if (!e2e_path_is_under(cache, stagingDir)) {
    if (why) *why = "--directory-cache is not under the staging directory";
    return false;
  }
  return true;
}

/**
 * The current environment with LOCALAPPDATA replaced, as a block CreateProcessW can take with
 * CREATE_UNICODE_ENVIRONMENT. Variables the test set on itself beforehand are carried over.
 */
inline std::vector<wchar_t> e2e_isolated_environment(const std::wstring& localAppData) {
  std::vector<wchar_t> block;
  LPWCH env = GetEnvironmentStringsW();
  if (env) {
    for (const wchar_t* p = env; *p; p += wcslen(p) + 1) {
      const std::wstring entry(p);
      if (_wcsnicmp(entry.c_str(), L"LOCALAPPDATA=", 13) == 0) continue;
      block.insert(block.end(), entry.begin(), entry.end());
      block.push_back(L'\0');
    }
    FreeEnvironmentStringsW(env);
  }
  const std::wstring mine = L"LOCALAPPDATA=" + localAppData;
  block.insert(block.end(), mine.begin(), mine.end());
  block.push_back(L'\0');
  block.push_back(L'\0');
  return block;
}

/** The LOCALAPPDATA a block would give its child, for a check before launch. */
inline std::wstring e2e_block_localappdata(const std::vector<wchar_t>& block) {
  for (const wchar_t* p = block.data(); p < block.data() + block.size() && *p; p += wcslen(p) + 1) {
    if (_wcsnicmp(p, L"LOCALAPPDATA=", 13) == 0) return std::wstring(p + 13);
  }
  return {};
}

/** Recursive delete, refusing anything that is not strictly inside `root`. Test scratch only. */
inline void remove_tree_under(const std::wstring& path, const std::wstring& root) {
  if (!e2e_path_is_under(path, root)) return;
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = fd.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring child = path + L"\\" + name;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;  // never follow out
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree_under(child, root);
      else DeleteFileW(child.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(path.c_str());
}

}  // namespace remote60::native_poc::e2e
