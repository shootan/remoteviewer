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

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace remote60::native_poc::e2e {

/**
 * The exit code of an e2e test that did not run (RV-18). Not 0: a skipped run and a passing one
 * used to be indistinguishable to anything that reads exit codes, so "all green" could mean "none
 * of them ran". 77 is the automake/CTest SKIP convention. Not 1 either -- a skip is not a failure,
 * and a runner that treats it as one teaches people to ignore red.
 */
constexpr int kE2eSkippedExit = 77;

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

/**
 * Ports nobody on this machine holds right now, `count` of them, all different (RV-19,
 * 2026-09-28). The host e2e tests used to name their ports in the source (44720..44899), so two
 * of them running at once -- a worker's measurement and the verifier's regression sweep -- fought
 * over the same number and the second host died at bind ("udp bind failed on every candidate
 * port"). Asked to bind port 0, the OS hands out a port from its dynamic range; every socket is
 * kept open until all `count` are picked, so the ports differ, and then all are released for the
 * host, the proxy, the shaper or the stand-in source to bind for real. `type` is SOCK_DGRAM (the
 * host's media port, every proxy) or SOCK_STREAM (the host's control port). The pick itself is
 * retried a few times; an empty vector means no port could be found.
 *
 * Limit: bind-0 / close / the host's own bind is NOT a reservation. The ports are released before
 * they are handed over, so another process may bind one in between; when that happens the host
 * fails at its bind and that test fails, as it did before -- there is no retry at launch.
 *
 * A caller must not start a host on an empty or short result: the host reads --bind-port 0 as
 * "no candidates" and falls back to the product's default port (parse_bind_port_candidates), which
 * is exactly the collision this is here to prevent. REMOTE60_E2E_PICK_PORT_FAIL=1 makes every pick
 * fail, so a test can show that it then starts nothing.
 */
inline std::vector<uint16_t> e2e_pick_free_ports(int type, size_t count) {
  {
    wchar_t fail[8]{};
    if (GetEnvironmentVariableW(L"REMOTE60_E2E_PICK_PORT_FAIL", fail, 8) != 0 && fail[0] == L'1') return {};
  }
  WSADATA wsa{};
  const bool started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;  // reference counted: harmless if already up
  std::vector<uint16_t> ports;
  for (int attempt = 0; attempt < 3 && ports.size() < count; ++attempt) {
    ports.clear();
    std::vector<SOCKET> held;
    for (size_t i = 0; i < count; ++i) {
      SOCKET s = socket(AF_INET, type, type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP);
      if (s == INVALID_SOCKET) break;
      sockaddr_in local{};
      local.sin_family = AF_INET;
      local.sin_addr.s_addr = htonl(INADDR_ANY);  // free on every address, so free on loopback too
      local.sin_port = 0;
      sockaddr_in bound{};
      int len = sizeof(bound);
      if (bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0 ||
          getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) != 0 || bound.sin_port == 0) {
        closesocket(s);
        break;
      }
      held.push_back(s);
      ports.push_back(ntohs(bound.sin_port));
    }
    for (SOCKET s : held) closesocket(s);
    if (ports.size() < count) ports.clear();
  }
  if (started) WSACleanup();
  return ports;
}

/** One free UDP port (0 = none found). */
inline uint16_t e2e_pick_free_udp_port() {
  const std::vector<uint16_t> p = e2e_pick_free_ports(SOCK_DGRAM, 1);
  return p.empty() ? 0 : p[0];
}

/** One free TCP port (0 = none found). */
inline uint16_t e2e_pick_free_tcp_port() {
  const std::vector<uint16_t> p = e2e_pick_free_ports(SOCK_STREAM, 1);
  return p.empty() ? 0 : p[0];
}

/**
 * A window title no other test process can be carrying (RV-19 r2, 2026-09-28): `base`, this
 * process id and a random tag. The host resolves --capture-window-title and --input-target-title
 * as a substring of the title, so every test that named its window "c3 inject target" was
 * offering it to every other test's host: run side by side, a control-resume host captured the
 * window of the mouse-x-button test. Pair the title with the pid (e2e_capture_window_args), so
 * both must match and there is exactly one answer.
 */
inline std::wstring e2e_unique_window_title(const std::wstring& base) {
  std::random_device rd;
  uint32_t v = rd();
  std::wstring tag;
  for (int i = 0; i < 8; ++i) {
    tag.push_back(L"0123456789abcdef"[v & 15u]);
    v >>= 4;
  }
  return base + L" pid " + std::to_wstring(GetCurrentProcessId()) + L" " + tag;
}

/** The host arguments that resolve exactly one window: this process's pid AND `title`. */
inline std::wstring e2e_capture_window_args(const std::wstring& title) {
  return L" --capture-window-pid " + std::to_wstring(GetCurrentProcessId()) + L" --capture-window-title \"" + title +
         L"\"";
}

/**
 * Whether the host's log says it captured `hwnd` of THIS process -- the "capture-window target
 * hwnd=0x... pid=..." line it writes at startup (the fallback line says "not found"). Read it once
 * the host has exited: its stdout into a file is block-buffered. `lineOut` receives the line the
 * host wrote, or a note that it wrote none, for the report.
 */
inline bool e2e_host_captured_window(const std::wstring& hostLogPath, HWND hwnd, std::string* lineOut) {
  char want[80]{};
  std::snprintf(want, sizeof(want), "capture-window target hwnd=0x%llx pid=%lu ",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd)),
                static_cast<unsigned long>(GetCurrentProcessId()));
  std::ifstream in(hostLogPath);
  std::string line;
  bool found = false;
  bool said = false;
  while (std::getline(in, line)) {
    if (line.find("capture-window target") == std::string::npos) continue;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (lineOut) *lineOut = line;
    said = true;
    found = hwnd != nullptr && line.find(want) != std::string::npos;
    break;
  }
  if (!said && lineOut) *lineOut = "the host never said what it captured";
  return found;
}

/**
 * Removes one staging directory this run created -- and nothing else: only `dir`, only if it is
 * strictly inside `root` (the temp directory), never anything found by name or pattern (RV-20,
 * 2026-09-28). The host may still be releasing its image when the test tears down (a job's
 * kill-on-close is asynchronous), so a delete can fail once or twice; retried for a few seconds.
 * Returns whether the directory is gone.
 */
inline void remove_tree_under(const std::wstring& path, const std::wstring& root);  // below

inline bool e2e_remove_staging_dir(const std::wstring& dir, const std::wstring& root, int attempts = 40) {
  std::wstring path = dir;
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
  if (path.empty() || !e2e_path_is_under(path, root)) return false;
  for (int i = 0; i < attempts; ++i) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    remove_tree_under(path, root);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    Sleep(100);
  }
  return false;
}

/**
 * Removes the staging directory when it goes out of scope -- on success, on failure and on an
 * early return alike -- unless `keep` was set by an explicit option (--keep-dir), so a run that
 * stops halfway leaves nothing behind in the user's temp directory. Declare it right after the
 * directory is created. A crash still leaves the directory: that is the one case this cannot cover.
 */
struct StagingDirCleanup {
  std::wstring dir;
  std::wstring root;
  bool keep = false;
  ~StagingDirCleanup() {
    if (!keep) (void)e2e_remove_staging_dir(dir, root);
  }
};

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
