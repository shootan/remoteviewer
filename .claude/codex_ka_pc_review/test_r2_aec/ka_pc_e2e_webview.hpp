// ka-pc e2e (Codex codex_ka_pc_test_src_r1.md N1 + the r2 addition): every WebView a ka-pc e2e can bring up gets its
// own user data folder (UDF) inside the run -- never the product's default under the user's TEMP:
//   * the shell  (client_shell_main.cpp create_shell_webview) : GetTempPathW + "GNLinkClient" -- in the shell e2e itself
//   * the macro  (client_macro_window.cpp)                    : GetTempPathW + "GNLinkMacro"  -- in a VIEWER process
// The WebView2 loader lets WEBVIEW2_USER_DATA_FOLDER and the WebView2 registry policies replace that argument, and
// other WEBVIEW2_* variables / policies change the runtime and its arguments. So each WebView's process gets
// WEBVIEW2_USER_DATA_FOLDER = ITS OWN UDF explicitly, and the preflight below (read only) must hold for it before that
// process can create one: the UDF new and inside the run, what the product computes (GetTempPathW into its MAX_PATH
// buffer + the folder name) the same path, the variable that path, no other WEBVIEW2_* variable, no WebView2 policy
// (HKLM / HKCU, 64- and 32-bit views). Additional browser arguments: none (the product passes no options).
// A macro WebView lives in a viewer, where this test cannot ask for its effective folder: one appearing during the
// run (a GNLinkMacroWindow of a process started from this build) is not expected and fails the run.
// Nothing here changes a product default; nothing is deleted.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "ka_pc_e2e_child.hpp"

namespace ka_pc_e2e {

constexpr wchar_t kWebView2Policies[] = L"SOFTWARE\\Policies\\Microsoft\\Edge\\WebView2";
constexpr wchar_t kShellUdfName[] = L"GNLinkClient";  // client_shell_main.cpp create_shell_webview
constexpr wchar_t kMacroUdfName[] = L"GNLinkMacro";   // client_macro_window.cpp
constexpr wchar_t kMacroWindowClass[] = L"GNLinkMacroWindow";

// Names of every WEBVIEW2_* variable in this process's environment except the UDF one ("" = none).
inline std::string webview_variables_other_than_udf() {
  std::string found;
  wchar_t* block = GetEnvironmentStringsW();
  if (!block) return "the environment could not be read";
  for (const wchar_t* e = block; *e; e += wcslen(e) + 1) {
    const std::wstring entry(e);
    const size_t eq = entry.find(L'=', 1);
    const std::wstring name = entry.substr(0, eq);
    if (name.size() >= 9 && _wcsnicmp(name.c_str(), L"WEBVIEW2_", 9) == 0 &&
        _wcsicmp(name.c_str(), L"WEBVIEW2_USER_DATA_FOLDER") != 0) {
      found += (found.empty() ? "" : ", ") + narrow_path(name);
    }
  }
  FreeEnvironmentStringsW(block);
  return found;
}

// Every WebView2 policy present (subkey or value names) or unreadable, HKLM / HKCU, both views ("" = none). Read only.
inline std::string webview_policies_present() {
  std::string found;
  for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
    for (REGSAM view : {static_cast<REGSAM>(KEY_WOW64_64KEY), static_cast<REGSAM>(KEY_WOW64_32KEY)}) {
      const std::string where = std::string(root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU") +
                                (view == KEY_WOW64_64KEY ? "(64)" : "(32)");
      HKEY k = nullptr;
      const LONG r = RegOpenKeyExW(root, kWebView2Policies, 0, KEY_READ | view, &k);
      if (r == ERROR_FILE_NOT_FOUND) continue;
      if (r != ERROR_SUCCESS) {
        found += where + ": unreadable (error " + std::to_string(r) + "); ";
        continue;
      }
      DWORD subkeys = 0, values = 0;
      if (RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr,
                           nullptr) != ERROR_SUCCESS) {
        found += where + ": its contents unreadable; ";
      } else {
        for (DWORD i = 0; i < subkeys; ++i) {
          wchar_t n[256] = {};
          DWORD len = 256;
          if (RegEnumKeyExW(k, i, n, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
            found += where + " key " + narrow_path(n) + "; ";
          else
            found += where + " key #" + std::to_string(i) + "; ";
        }
        for (DWORD i = 0; i < values; ++i) {
          wchar_t n[256] = {};
          DWORD len = 256;
          if (RegEnumValueW(k, i, n, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
            found += where + " value '" + narrow_path(n) + "'; ";
          else
            found += where + " value #" + std::to_string(i) + "; ";
        }
      }
      RegCloseKey(k);
    }
  }
  return found;
}

// What the product would ask for right now for `name`: its own computation (GetTempPathW into MAX_PATH + name), or ""
// when that would not fit its buffer (wcscat_s would not return).
inline std::wstring product_udf_now(const wchar_t* name) {
  wchar_t b[MAX_PATH] = {};
  const DWORD n = GetTempPathW(MAX_PATH, b);
  if (n == 0 || n >= MAX_PATH || n + wcslen(name) + 1 > MAX_PATH) return {};
  return std::wstring(b, n) + name;
}

// The preflight for one WebView (read only, nothing set): empty = every condition holds for `expectedUdf` as the folder
// the product names `name`; otherwise why not.
inline std::string webview_preflight(const std::wstring& root, const wchar_t* name, const std::wstring& expectedUdf) {
  if (!is_inside(expectedUdf, root)) return "the UDF is not inside this run";
  const std::wstring asked = product_udf_now(name);
  if (asked.empty()) return "the product's UDF would not fit MAX_PATH";
  if (!same_path(asked, expectedUdf)) return "the product would ask for " + narrow_path(asked) + ", not " + narrow_path(expectedUdf);
  wchar_t v[2048] = {};
  const DWORD n = GetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", v, 2048);
  if (n == 0 || n >= 2048 || !same_path(std::wstring(v, n), expectedUdf)) return "WEBVIEW2_USER_DATA_FOLDER is not the UDF";
  const DWORD a = GetFileAttributesW(expectedUdf.c_str());
  if (a != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND) return "the UDF is not new (it exists already)";
  const std::wstring parent = expectedUdf.substr(0, expectedUdf.find_last_of(L'\\'));
  if (!resolves_to_itself(parent)) return "the UDF's folder does not resolve to itself";
  const std::string vars = webview_variables_other_than_udf();
  if (!vars.empty()) return "other WebView2 variables are set: " + vars;
  const std::string policies = webview_policies_present();
  if (!policies.empty()) return "WebView2 policies are present: " + policies;
  return {};
}

// Sets WEBVIEW2_USER_DATA_FOLDER to `udf` and runs the preflight for it: empty = holds.
inline std::string webview_fix_and_preflight(const std::wstring& root, const wchar_t* name, const std::wstring& udf) {
  if (!SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", udf.c_str()))
    return "WEBVIEW2_USER_DATA_FOLDER could not be set (error " + std::to_string(GetLastError()) + ")";
  return webview_preflight(root, name, udf);
}

// A macro window (GNLinkMacroWindow) of a process whose image lies in `buildDir` -- a viewer this run started, directly
// or through the shell. (Only image paths are read; nothing is opened for more than a limited query.)
inline bool own_macro_window_exists(const std::wstring& buildDir) {
  struct Ctx {
    const std::wstring* dir;
    bool found;
  } ctx{&buildDir, false};
  EnumWindows(
      [](HWND h, LPARAM p) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(p);
        wchar_t cls[32] = {};
        GetClassNameW(h, cls, 32);
        if (wcscmp(cls, kMacroWindowClass) != 0) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        HANDLE q = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!q) return TRUE;
        wchar_t image[MAX_PATH] = {};
        DWORD len = MAX_PATH;
        const bool named = QueryFullProcessImageNameW(q, 0, image, &len) != 0;
        CloseHandle(q);
        if (named && is_inside(std::wstring(image, len), *c->dir)) {
          c->found = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}

}  // namespace ka_pc_e2e
