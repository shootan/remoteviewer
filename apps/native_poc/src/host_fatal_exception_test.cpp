// prelogin-field D2: when the streaming host's main thread throws, its exit 49 says what threw and at which
// step. The 0.2.160 pre-login worker died with 49 after each viewer connect and nothing said where.
//
// The host is GNLinkStreamMigrationTest (the product's sources; REMOTE60_STREAM_TEST_SEAM adds
// GNLINK_STREAM_TEST_THROW_AT_STEP, which throws at the named step). Each case starts it with no client and a
// step that comes before any network wait, and reads its output and exit code. The catch, the step names and
// the line are the product's (native_video_host_main.cpp); only the throw is injected.
//   std   std::runtime_error at startup_configure_from_env  -> FATAL std::exception "...", step named, 49
//   winrt winrt::hresult_error at startup_configure_from_env         -> FATAL winrt::hresult_error hr=0x80070005, 49
//   none  no injection (control): no FATAL line from the injection path

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
int gChecks = 0, gFailures = 0;
void check(const std::string& what, bool ok, const std::string& detail = {}) {
  ++gChecks;
  if (!ok) ++gFailures;
  std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
  std::fflush(stdout);
}
std::string read_all(const std::wstring& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
// Runs the host with `step` injected (empty = none); returns the exit code (or 0xFFFFFFFF on timeout).
DWORD run(const std::wstring& dir, const std::wstring& tag, const std::string& step, std::string* out, int timeoutMs) {
  const std::wstring log = dir + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = h;
  si.hStdError = h;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  // a port nobody uses, a short run; the injected steps come before any wait for a client
  std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --transport udp --codec h264 --port 0 --seconds 3";
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
      GetExitCodeProcess(pi.hProcess, &code);
    } else {
      TerminateProcess(pi.hProcess, 99);
      WaitForSingleObject(pi.hProcess, 5000);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(h);
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
  *out = read_all(log);
  return code;
}
}  // namespace

int wmain() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the streaming host's exit 49 names what threw and where (prelogin-field D2)\n\n");
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring dir(self);
  dir = dir.substr(0, dir.find_last_of(L'\\') + 1);
  check("the stream test build is beside this test", std::filesystem::exists(dir + L"GNLinkStreamMigrationTest.exe"));

  std::string out;
  DWORD code = run(dir, L"std", "startup_configure_from_env", &out, 20000);
  check("std: exit 49", code == 49, std::to_string(code));
  check("std: the line says std::exception, its what(), and the step",
        out.find("FATAL: std::exception \"stream-test-seam: injected throw\" at step=startup_configure_from_env -- exit 49") !=
            std::string::npos);

  code = run(dir, L"winrt", "winrt:startup_configure_from_env", &out, 20000);
  check("winrt: exit 49", code == 49, std::to_string(code));
  check("winrt: the line says winrt::hresult_error, its HRESULT, its message, and the step",
        out.find("FATAL: winrt::hresult_error hr=0x80070005 \"stream-test-seam: injected winrt\" at step=startup_configure_from_env") !=
            std::string::npos);

  code = run(dir, L"none", "", &out, 30000);
  check("control (nothing injected): no FATAL line", out.find("FATAL:") == std::string::npos, "exit " + std::to_string(code));

  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
