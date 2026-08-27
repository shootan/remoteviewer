// Command-line quoting (ledger H-20).
//
// The assertions are round-trips through CommandLineToArgvW -- the same parser the child's CRT
// uses -- so this pins the actual contract ("the child sees the values I passed") rather than the
// exact escape string, which is an implementation detail.

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "host_command_line.hpp"

using remote60::native_poc::build_windows_command_line;
using remote60::native_poc::quote_windows_arg;

namespace {

int gFailures = 0;

std::string narrow(const std::wstring& w) {
  std::string s;
  for (const wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
  return s;
}

// Parse a built command line back the way the child's CRT will.
std::vector<std::wstring> parse(const std::wstring& cmd) {
  std::vector<std::wstring> out;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(cmd.c_str(), &argc);
  if (!argv) return out;
  for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
  LocalFree(argv);
  return out;
}

void round_trip(const wchar_t* label, const std::vector<std::wstring>& args) {
  const std::wstring exe = L"C:\\Program Files\\GNLink\\GNLinkStream.exe";
  const std::vector<std::wstring> got = parse(build_windows_command_line(exe, args));
  bool ok = got.size() == args.size() + 1 && !got.empty() && got[0] == exe;
  if (ok) {
    for (size_t i = 0; i < args.size(); ++i) {
      if (got[i + 1] != args[i]) { ok = false; break; }
    }
  }
  if (ok) return;
  std::printf("  FAIL %s: got %zu args\n", narrow(label).c_str(), got.size());
  for (size_t i = 0; i < got.size(); ++i) {
    std::printf("        [%zu] %s\n", i, narrow(got[i]).c_str());
  }
  ++gFailures;
}

void expect(bool ok, const char* what) {
  if (ok) return;
  std::printf("  FAIL %s\n", what);
  ++gFailures;
}

}  // namespace

int main() {
  std::printf("round-trips through CommandLineToArgvW\n");
  round_trip(L"plain", {L"--transport", L"udp", L"--codec", L"h264"});
  round_trip(L"spaces", {L"--host-name", L"Shota's Gaming PC"});
  round_trip(L"embedded quote", {L"--host-name", L"my \"quoted\" pc"});
  // The injection shape: without escaping this ended the --host-name argument and started a new
  // option the shell never meant to pass.
  round_trip(L"argument injection attempt",
             {L"--host-name", L"pc\" --control-port 1", L"--control-port", L"43001"});
  round_trip(L"trailing backslash", {L"--directory-url", L"https://example.com\\"});
  round_trip(L"backslash run before quote", {L"--host-name", L"a\\\\\"b"});
  round_trip(L"empty value", {L"--host-name", L""});
  round_trip(L"only backslashes", {L"--host-name", L"\\\\\\"});
  round_trip(L"unicode-ish path", {L"--directory-url", L"https://a.b/c?d=e&f=g"});

  std::printf("shape\n");
  expect(quote_windows_arg(L"plain") == L"\"plain\"", "plain value is quoted");
  expect(quote_windows_arg(L"") == L"\"\"", "empty value survives as an empty argument");
  // A trailing backslash must not escape the closing quote.
  expect(quote_windows_arg(L"a\\") == L"\"a\\\\\"", "trailing backslash is doubled");

  if (gFailures == 0) {
    std::printf("host_command_line_test: PASS\n");
    return 0;
  }
  std::printf("host_command_line_test: FAIL (%d)\n", gFailures);
  return 1;
}
