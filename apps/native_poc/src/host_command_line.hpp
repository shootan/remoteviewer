#pragma once

// Building a Windows child command line out of argument values.
//
// Role:    quote_windows_arg / build_windows_command_line turn a vector of argument VALUES into
//          the single command-line string CreateProcessW takes, escaped so the child's CRT parses
//          back exactly the values that went in.
// Thread:  pure.
// Callers: host_app_main.cpp (launching GNLinkStream), host_command_line_test.cpp.
//
// Why this exists (ledger H-20): the child command line was built by concatenating
// L" --host-name \"" + hostName + L"\"". hostName comes from an edit box, and the account id and
// server URL are user input too, so a value containing a quote broke the argument boundary and
// could inject or corrupt the child's options. CreateProcessW is not a shell, so this is not
// arbitrary command execution -- but it is a local config-injection and a correctness bug for
// anyone whose PC name legitimately contains a quote or a trailing backslash.
//
// The rules are the ones the MS CRT documents for parsing argv:
//   - a run of backslashes followed by a quote: each backslash doubles, then the quote escapes;
//   - a run of backslashes at the end of a quoted argument: each backslash doubles;
//   - backslashes not followed by a quote are literal.

#include <string>
#include <vector>

namespace remote60::native_poc {

// Quote one argument value. Always quoted, even when it needs nothing: an unquoted empty string
// would vanish from argv entirely.
inline std::wstring quote_windows_arg(const std::wstring& value) {
  std::wstring out;
  out.push_back(L'"');
  size_t backslashes = 0;
  for (const wchar_t c : value) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');  // double the run, then escape the quote
      backslashes = 0;
      out.push_back(L'"');
      continue;
    }
    out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');  // trailing run precedes the closing quote
  out.push_back(L'"');
  return out;
}

/**
 * argv[0] plus the rest, space-separated and individually quoted. Pass the executable path as
 * `exe`; CreateProcessW should also receive it as lpApplicationName so the child is chosen by
 * path rather than by parsing this string.
 */
inline std::wstring build_windows_command_line(const std::wstring& exe,
                                               const std::vector<std::wstring>& args) {
  std::wstring cmd = quote_windows_arg(exe);
  for (const auto& a : args) {
    cmd.push_back(L' ');
    cmd.append(quote_windows_arg(a));
  }
  return cmd;
}

}  // namespace remote60::native_poc
