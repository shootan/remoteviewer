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
// prelogin-field D2 -- the two WinRT calls of startup on the ORDINARY path (the pre-login path is the login e2e's, run
// with the same trap): a client's Hello (plain UDP, as a LAN client sends it) takes the host past the connect, then
//   probe-trap    GNLINK_STREAM_TEST_WINRT_TRAP=probe   -> exit 49 naming IsSupported at startup_init_graphics
//                 (the trap is live on this path: a pre-login run that passes with it set did not make the call)
//   wrapper-trap  GNLINK_STREAM_TEST_WINRT_TRAP=wrapper -> exit 49 naming the D3D wrapper at startup_init_encoder
//   wgc-throw     IsSupported throws  -> exit 6, "WGC not supported (IsSupported threw hr=0x80070005)", no FATAL
//   wgc-false     IsSupported false   -> exit 6, "WGC not supported", no "threw"
//   normal        nothing injected    -> past both (the encoder line), no FATAL
//
// 2026-10-28 (Codex codex_test_injection_finding.md, fatal_exception; r5 F1-F4): every host here is started with
// `--input-injection-mode none --host-power-management off`, bound to 127.0.0.1 on ports nobody holds, with its own
// LOCALAPPDATA inside this run's folder (not the user's; not every interface -- no firewall prompt). The run() cases
// std / winrt are startup faults (a throw at the first configure step); `none` is the NORMAL control (a plain start,
// no fault, 3 s). This test opens no socket to any of them -- no client of this test connects (other local programs
// could reach a loopback listener; nothing here does). Their power effects are zero by the order in main(): the
// policy is bound from the command line before HostPowerKeepalive exists, and OFF makes no request at all -- the
// host's own "host power management=off" line is checked after the fact (not a gate: nothing connects anyway). The run_with_client() cases send a client Hello only AFTER this launch said the whole
// "input injection disabled (unsupported mode) mode=none" line; a host that dies before it gets no Hello at all; any
// other injection state ends the case at once, nothing sent (INVALID). Their logs and the pre-login log root live in
// a NEW folder of this run under the trusted test root (REMOTE60_TEST_TMP directly under
// D:/remote/remote/.claude/test-tmp, as host_agent_punch_test) -- the only place this test deletes in, and only
// inside that folder, and only files this test knows by name plus the folders it made, one by one, never
// recursively (r5 F1); a delete that fails counts as a FAIL; a folder that is not empty stays. (Before: logs beside
// the executable and a fatal_prelogin_logroot there, remove_all'd -- a recursive delete outside the repository for a
// build outside it.)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sddl.h>

#include <thread>
#include <atomic>

#include "poc_protocol.hpp"
#include "product_version.hpp"
#include "e2e_injection_guard.hpp"
#include "e2e_isolation.hpp"
#include "e2e_child_util.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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
const wchar_t kTestAnchor[] = L"D:\\remote\\remote\\.claude\\test-tmp";
std::wstring gRun;
bool gKeepPreloginLog = false;  // r6 F-b
// r7 N2: the cases whose child's end was NOT confirmed -- their logs are kept (never deleted at the end)
std::set<std::wstring> gKeepLogs;

// r7 N2: a safety contradiction (input / power) or an injection-gate refusal ends the WHOLE test here: no further
// launch, nothing of this run deleted (every log kept), RESULT: INVALID, exit 2. The case's own child has already been
// ended by its launcher (confirmed, or recorded as not confirmed in gKeepLogs, which is said).
[[noreturn]] void fatal_invalid(const std::wstring& tag, const std::string& why) {
  remote60::native_poc::e2e::e2e_case_invalid(gRun, gKeepLogs, tag, why);  // e2e_child_util.hpp (its negative control:
                                                                           // e2e_guard_runtime_test)
}  // this run's own new folder (with a trailing backslash): logs and the log root live in it

bool is_reparse(const std::wstring& p) {
  const DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// REMOTE60_TEST_TMP: one plain folder directly under the anchor (no links); in it, a NEW folder of this run's own
// (CreateDirectory fails on any existing name). Checked before anything is created; refuses otherwise.
bool init_run_root(std::string* why) {
  wchar_t given[1024] = {};
  const DWORD n = GetEnvironmentVariableW(L"REMOTE60_TEST_TMP", given, 1024);
  if (n == 0 || n >= 1024) { *why = "REMOTE60_TEST_TMP is not set"; return false; }
  wchar_t full[1024] = {};
  wchar_t* namePart = nullptr;
  if (GetFullPathNameW(given, 1024, full, &namePart) == 0 || !namePart || !*namePart) {
    *why = "REMOTE60_TEST_TMP is not a usable path";
    return false;
  }
  const std::wstring name(namePart);
  for (wchar_t ch : name) {
    const bool ok = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') ||
                    ch == L'-' || ch == L'_' || ch == L'.';
    if (!ok || name == L"." || name == L"..") { *why = "the test root's name must be a plain ASCII folder name"; return false; }
  }
  std::wstring parent(full, namePart - full);
  while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/')) parent.pop_back();
  if (_wcsicmp(parent.c_str(), kTestAnchor) != 0) {
    *why = "REMOTE60_TEST_TMP must be a folder directly under D:/remote/remote/.claude/test-tmp";
    return false;
  }
  const DWORD anchorAttr = GetFileAttributesW(kTestAnchor);
  if (anchorAttr == INVALID_FILE_ATTRIBUTES || !(anchorAttr & FILE_ATTRIBUTE_DIRECTORY) || is_reparse(kTestAnchor)) {
    *why = "the test anchor is missing or a link";
    return false;
  }
  const std::wstring root = std::wstring(kTestAnchor) + L"\\" + name;
  if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    *why = "the test root could not be created";
    return false;
  }
  const DWORD rootAttr = GetFileAttributesW(root.c_str());
  if (rootAttr == INVALID_FILE_ATTRIBUTES || !(rootAttr & FILE_ATTRIBUTE_DIRECTORY) || (rootAttr & FILE_ATTRIBUTE_REPARSE_POINT)) {
    *why = "the test root is not a plain folder";
    return false;
  }
  for (int attempt = 0; attempt < 16; ++attempt) {
    const std::wstring run = root + L"\\fatal-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
    if (CreateDirectoryW(run.c_str(), nullptr)) {
      if (is_reparse(run)) { *why = "this run's folder is a link"; return false; }
      gRun = run + L"\\";
      return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) break;
  }
  *why = "no new folder for this run could be made";
  return false;
}

// r5 F1: no recursive delete anywhere. A file this test knows by name, directly in this run's folder tree, removed
// alone; a folder it made, removed only if empty. A failure (other than "already gone") is a FAIL; a folder that is
// not empty stays where it is (said, not forced).
bool inside_run(const std::wstring& path) {
  return !gRun.empty() && path.size() > gRun.size() && _wcsnicmp(path.c_str(), gRun.c_str(), gRun.size()) == 0 &&
         path.find(L"..") == std::wstring::npos;
}
void delete_known_file(const std::wstring& path) {
  if (!inside_run(path)) {
    std::printf("FAIL  refused to delete outside this run's folder\n");
    ++gFailures;
    return;
  }
  if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
    std::printf("FAIL  a file of this run could not be removed (%lu)\n", GetLastError());
    ++gFailures;
  }
}
void remove_made_folder(const std::wstring& path) {
  if (!inside_run(path + L"\\") && path + L"\\" != gRun) {
    std::printf("FAIL  refused to remove a folder outside this run's folder\n");
    ++gFailures;
    return;
  }
  if (!RemoveDirectoryW(path.c_str())) {
    const DWORD e = GetLastError();
    if (e == ERROR_DIR_NOT_EMPTY) {
      std::printf("NOTE  kept (not empty, inside the test root): %ls\n", path.c_str());
    } else if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
      std::printf("FAIL  a folder of this run could not be removed (%lu)\n", e);
      ++gFailures;
    }
  }
}

uint16_t free_udp_port();  // below
uint16_t port_or_invalid(const char* where);  // below (r6 F-a)
bool end_child_confirmed(HANDLE process, UINT code);  // below (r6 F-c)

// r5 F3: the host's environment -- this process's, with LOCALAPPDATA in this run's folder (its diagnostics land
// there, not in the user's). Kept with the run's folder; never removed recursively.
std::vector<wchar_t> host_env(const std::wstring& tag) {
  const std::wstring lad = gRun + L"lad_" + tag;
  CreateDirectoryW(lad.c_str(), nullptr);
  return remote60::native_poc::e2e::e2e_isolated_environment(lad);
}

// r5 F4: the host said its power management OFF, whole (the effect-0 basis is main()'s order; this is the record)
bool said_power_off(const std::string& out) {
  std::string why;
  return remote60::native_poc::e2e::e2e_power_state(out, &why) == 1;
}

// Runs the host with `step` injected (empty = none); returns the exit code (or 0xFFFFFFFF on timeout).
DWORD run(const std::wstring& dir, const std::wstring& tag, const std::string& step, std::string* out, int timeoutMs) {
  const std::wstring log = gRun + L"fatal_" + tag + L".log";
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", step.empty() ? nullptr : step.c_str());
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = h;
  si.hStdError = h;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  // loopback, ports nobody holds, a short run. This test opens no socket to it (no client of this test connects);
  // a throw comes at the first configure step, before any wait for a client; `none` is the normal control (r5 F2)
  std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(port_or_invalid("run")) + L" --control-port " +
                     std::to_wstring(port_or_invalid("run")) + L" --seconds 3" + remote60::native_poc::e2e::e2e_injection_off_arg(dir);
  std::vector<wchar_t> env = host_env(tag);
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(),
                     dir.c_str(), &si, &pi)) {
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
      GetExitCodeProcess(pi.hProcess, &code);
    } else {
      if (!end_child_confirmed(pi.hProcess, 99)) gKeepLogs.insert(tag);  // r7 N2: not confirmed -> its log kept
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(h);
  SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
  *out = read_all(log);
  return code;
}
// r6 F-a / F-c: the shared helpers (e2e_child_util.hpp, their negative controls in e2e_guard_runtime_test)
uint16_t free_udp_port() { return remote60::native_poc::e2e::e2e_free_loopback_udp_port(); }
uint16_t port_or_invalid(const char* where) { return remote60::native_poc::e2e::e2e_port_or_invalid(where); }
bool end_child_confirmed(HANDLE process, UINT code) {
  const bool ok = remote60::native_poc::e2e::e2e_end_child_confirmed(process, code);
  if (!ok) ++gFailures;  // an end not confirmed is a FAIL of this run
  return ok;
}

// The host on `port` with `env` set (the product's stream switches always), a client Hello every 200 ms until it
// ends or `seenNeedle` appears; returns the exit code (0xFFFFFFFF: killed at the deadline -- normal for "normal").
constexpr DWORD kInjectionInvalid = 0xFFFFFFFEu;  // the injection gate refused: the case is INVALID, nothing sent
DWORD run_with_client(const std::wstring& dir, const std::wstring& tag, const std::vector<std::pair<std::string, std::string>>& env,
                      const std::string& seenNeedle, std::string* out, int* hellosSent) {
  *hellosSent = 0;
  const uint16_t port = port_or_invalid("run_with_client");
  const std::wstring log = gRun + L"fatal_" + tag + L".log";
  for (const auto& kv : env) SetEnvironmentVariableA(kv.first.c_str(), kv.second.c_str());
  SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE", "1");
  SetEnvironmentVariableA("REMOTE60_NATIVE_ENCODER_TUNE_MODE", "low_latency");
  SetEnvironmentVariableA("REMOTE60_NATIVE_NV12_SURFACE", "0");
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = h;
  si.hStdError = h;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --transport udp --codec h264 --bind-address 127.0.0.1" +
                     L" --bind-port " + std::to_wstring(port) + L" --control-port " + std::to_wstring(port_or_invalid("run_with_client")) +
                     L" --seconds 20" + remote60::native_poc::e2e::e2e_injection_off_arg(dir);
  std::vector<wchar_t> hostEnv = host_env(tag);
  PROCESS_INFORMATION pi{};
  DWORD code = 0xFFFFFFFFu;
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                     hostEnv.data(), dir.c_str(), &si, &pi)) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    remote60::native_poc::UdpHelloPacket hello{};
    hello.magic = remote60::native_poc::kMagic;
    hello.kind = static_cast<uint16_t>(remote60::native_poc::UdpPacketKind::Hello);
    hello.version = remote60::native_poc::kUdpProtocolVersion;
    hello.features = remote60::native_poc::kUdpFeatureVideoFec;
    const ULONGLONG deadline = GetTickCount64() + 25000;
    for (;;) {
      if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
        break;
      }
      if (!seenNeedle.empty() && read_all(log).find(seenNeedle) != std::string::npos) {
        if (!end_child_confirmed(pi.hProcess, 0xFFFFFFFFu)) gKeepLogs.insert(tag);  // r7 N2: not confirmed -> its log kept
        break;
      }
      if (GetTickCount64() > deadline) {
        if (!end_child_confirmed(pi.hProcess, 0xFFFFFFFFu)) gKeepLogs.insert(tag);  // r7 N2: not confirmed -> its log kept
        break;
      }
      // the gate: a Hello only after THIS launch said its whole input-injection-OFF line; another state -> the
      // case ends here, nothing sent (a host that dies before saying it simply never gets one)
      std::string injWhy;
      const int inj = remote60::native_poc::e2e::e2e_injection_state(read_all(log), &injWhy);
      if (inj < 0) {
        if (!end_child_confirmed(pi.hProcess, 0xFFFFFFFFu)) gKeepLogs.insert(tag);  // r7 N2: not confirmed -> its log kept
        code = kInjectionInvalid;
        std::printf("FAIL  SETUP %ls: input injection not provably off (%s) -- no Hello sent\n", tag.c_str(), injWhy.c_str());
        break;
      }
      if (inj == 0) continue;  // not yet said: no Hello
      ++*hellosSent;
      sendto(s, reinterpret_cast<const char*>(&hello), sizeof(hello), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }
    closesocket(s);
    // r6 G1: the whole launch output judged again at the end (a later contradiction -> this case INVALID)
    {
      std::string why;
      if (code != kInjectionInvalid && remote60::native_poc::e2e::e2e_injection_contradicted(read_all(log), &why)) {
        std::printf("FAIL  RUN %ls: input / power state contradicted during the run (%s)\n", tag.c_str(), why.c_str());
        code = kInjectionInvalid;
      }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
  CloseHandle(h);
  for (const auto& kv : env) SetEnvironmentVariableA(kv.first.c_str(), nullptr);
  *out = read_all(log);
  return code;
}
}  // namespace

int wmain() {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("the streaming host's exit 49 names what threw and where (prelogin-field D2)\n\n");
  wchar_t self[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  std::wstring dir(self);
  dir = dir.substr(0, dir.find_last_of(L'\\') + 1);
  check("the stream test build is beside this test", std::filesystem::exists(dir + L"GNLinkStreamMigrationTest.exe"));
  {
    std::string why;
    if (!init_run_root(&why)) {
      std::printf("FAIL  no valid test root (%s): refused to run -- nothing created or deleted\nRESULT: FAILED\n", why.c_str());
      return 2;
    }
  }

  std::string out;
  DWORD code = run(dir, L"std", "startup_configure_from_env", &out, 20000);
  check("std: the host said its power management OFF (r5 F4, the record)", said_power_off(out));
  {
    std::string why;  // r7 N2: a contradiction ends the whole test INVALID (no further launch, logs kept)
    if (remote60::native_poc::e2e::e2e_injection_contradicted(out, &why)) fatal_invalid(L"std", why);
    check("std: no input / power contradiction in its whole output (r6 G1)", true);
  }
  check("std: exit 49", code == 49, std::to_string(code));
  check("std: the line says std::exception, its what(), and the step",
        out.find("FATAL: std::exception \"stream-test-seam: injected throw\" at step=startup_configure_from_env -- exit 49") !=
            std::string::npos);

  code = run(dir, L"winrt", "winrt:startup_configure_from_env", &out, 20000);
  check("winrt: the host said its power management OFF (r5 F4, the record)", said_power_off(out));
  {
    std::string why;  // r7 N2: a contradiction ends the whole test INVALID (no further launch, logs kept)
    if (remote60::native_poc::e2e::e2e_injection_contradicted(out, &why)) fatal_invalid(L"winrt", why);
    check("winrt: no input / power contradiction in its whole output (r6 G1)", true);
  }
  check("winrt: exit 49", code == 49, std::to_string(code));
  check("winrt: the line says winrt::hresult_error, its HRESULT, its message, and the step",
        out.find("FATAL: winrt::hresult_error hr=0x80070005 \"stream-test-seam: injected winrt\" at step=startup_configure_from_env") !=
            std::string::npos);

  // r5 F2: the NORMAL control -- no fault injected, a plain 3 s start (not a startup fault); no client of this test
  code = run(dir, L"none", "", &out, 30000);
  check("normal control (nothing injected): no FATAL line", out.find("FATAL:") == std::string::npos, "exit " + std::to_string(code));
  {
    std::string why;  // r7 N2: a contradiction ends the whole test INVALID (no further launch, logs kept)
    if (remote60::native_poc::e2e::e2e_injection_contradicted(out, &why)) fatal_invalid(L"none", why);
    check("normal control: no input / power contradiction in its whole output (r6 G1)", true);
  }
  check("normal control: the host said input injection OFF and power management OFF, both whole (r5 F4)",
        remote60::native_poc::e2e::e2e_injection_state(out, nullptr) == 1);

  // ---- r3 (D3 through the worker's own main): --prelogin with the test log root, a throw at the first step ->
  // exit 49, and the worker's log holds its header and the exit-49 line (the sink installed first thing, drained at
  // main()'s return). No client is needed: the throw comes before any wait.
  {
    // inside this run's own new folder: nothing to clear first
    const std::wstring root = gRun + L"fatal_prelogin_logroot";
    const std::wstring logs = root + L"\\GNLink\\logs";
    CreateDirectoryW(root.c_str(), nullptr);
    {
      // the folders as the product makes them, naming this user (the test build's owner rule trusts it)
      HANDLE tok = nullptr;
      OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
      BYTE buf[256];
      DWORD n = 0;
      GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n);
      CloseHandle(tok);
      LPWSTR sid = nullptr;
      ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &sid);
      const std::wstring sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + std::wstring(sid) + L")";
      LocalFree(sid);
      for (const std::wstring& d : {root + L"\\GNLink", logs}) {
        PSECURITY_DESCRIPTOR sd = nullptr;
        ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr);
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        CreateDirectoryW(d.c_str(), &sa);
        LocalFree(sd);
      }
    }
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", root.c_str());
    SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", "startup_configure_from_env");
    // r5 F3: the worker's own test seam names its listener -- loopback, a port nobody holds, a short life (without
    // it, a worker takes the product's ports on every interface)
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_ADDRESS", L"127.0.0.1");
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_PORT", std::to_wstring(port_or_invalid("prelogin")).c_str());
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECONDS", L"5");
    const std::wstring log = gRun + L"fatal_prelogin.log";
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h;
    si.hStdError = h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    // input injection OFF and power management OFF from its first line (Finding-3), no client either way
    std::wstring cmd = L"\"" + dir + L"GNLinkStreamMigrationTest.exe\" --prelogin" + remote60::native_poc::e2e::e2e_injection_off_arg(dir);
    PROCESS_INFORMATION pi{};
    DWORD pcode = 0xFFFFFFFFu;
    bool ended = false;
    bool workerGone = true;  // r6 F-b: false = its end not confirmed -> nothing of it is removed
    std::vector<wchar_t> workerEnv = host_env(L"prelogin");
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                       workerEnv.data(), dir.c_str(), &si, &pi)) {
      ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
      if (ended) GetExitCodeProcess(pi.hProcess, &pcode);
      else workerGone = end_child_confirmed(pi.hProcess, 99);  // r6 F-b: confirmed, or its logs are kept below
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
    CloseHandle(h);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_LOG_ROOT", nullptr);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_ADDRESS", nullptr);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_BIND_PORT", nullptr);
    SetEnvironmentVariableW(L"GNLINK_STREAM_TEST_SECONDS", nullptr);
    SetEnvironmentVariableA("GNLINK_STREAM_TEST_THROW_AT_STEP", nullptr);
    const std::string wl = read_all(logs + L"\\prelogin_worker.log");
    check("--prelogin worker, throw at the first step: exit 49 by itself", ended && pcode == 49, "exit " + std::to_string(pcode));
    check("...its own log: the start header, then the exit-49 line naming the step (written before main returned)",
          wl.find("=== GNLinkStream --prelogin worker start") != std::string::npos &&
              wl.find("FATAL: std::exception \"stream-test-seam: injected throw\" at step=startup_configure_from_env -- exit 49") !=
                  std::string::npos &&
              wl.find("=== GNLinkStream --prelogin worker start") < wl.find("FATAL:"),
          std::to_string(wl.size()) + " bytes");
    {
      const std::wstring v = remote60::native_poc::kProductVersion;
      check("...the header names this build's version (the one kProductVersion)",
            wl.find("version=" + std::string(v.begin(), v.end()) + " ===") != std::string::npos, std::string(v.begin(), v.end()));
    }
    check("...and the same line still reached the stdout it was started with (the test's)",
          read_all(log).find("at step=startup_configure_from_env -- exit 49") != std::string::npos);
    check("...the worker said its power management OFF (r5 F4, the record)", said_power_off(read_all(log)));
    // r5 F1: the one file it writes there, then the folders made for it -- each alone, none recursively.
    // r6 F-b: only once the worker's end is confirmed; otherwise everything of it is kept (said), cleanup deferred
    if ((ended || workerGone)) {
      delete_known_file(logs + L"\\prelogin_worker.log");
      remove_made_folder(logs);
      remove_made_folder(root + L"\\GNLink");
      remove_made_folder(root);
    } else {
      std::printf("NOTE  the worker's end is not confirmed: its log root and log are kept, cleanup deferred: %ls\n", root.c_str());
      gKeepPreloginLog = true;
    }
  }

  // ---- D2: the ordinary path's two WinRT calls ----
  int hellos = 0;
  code = run_with_client(dir, L"probe_trap", {{"GNLINK_STREAM_TEST_WINRT_TRAP", "probe"}}, "", &out, &hellos);
  if (code == kInjectionInvalid) fatal_invalid(L"probe_trap", "the injection gate refused or the run contradicted it");  // r7 N2
  check("...the injection gate held (no Hello before the OFF line) and nothing contradicted it", true);
  check("ordinary path, probe trap: exit 49 naming IsSupported at startup_init_graphics (the trap is live here)",
        code == 49 && out.find("WinRT trap reached: GraphicsCaptureSession::IsSupported\" at step=startup_init_graphics") != std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wrapper_trap", {{"GNLINK_STREAM_TEST_WINRT_TRAP", "wrapper"}}, "", &out, &hellos);
  if (code == kInjectionInvalid) fatal_invalid(L"wrapper_trap", "the injection gate refused or the run contradicted it");  // r7 N2
  check("...the injection gate held (no Hello before the OFF line) and nothing contradicted it", true);
  check("ordinary path, wrapper trap: exit 49 naming the D3D wrapper at startup_init_encoder (the ordinary path keeps it)",
        code == 49 && out.find("WinRT trap reached: CreateDirect3D11DeviceFromDXGIDevice\" at step=startup_init_encoder") != std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wgc_throw", {{"GNLINK_STREAM_TEST_WGC_SUPPORTED", "throw"}}, "", &out, &hellos);
  if (code == kInjectionInvalid) fatal_invalid(L"wgc_throw", "the injection gate refused or the run contradicted it");  // r7 N2
  check("...the injection gate held (no Hello before the OFF line) and nothing contradicted it", true);
  check("IsSupported throws: the explicit 'not supported' exit 6 with the HRESULT -- not 49, not taken as supported",
        code == 6 && out.find("WGC not supported (IsSupported threw hr=0x80070005)") != std::string::npos &&
            out.find("FATAL:") == std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"wgc_false", {{"GNLINK_STREAM_TEST_WGC_SUPPORTED", "false"}}, "", &out, &hellos);
  if (code == kInjectionInvalid) fatal_invalid(L"wgc_false", "the injection gate refused or the run contradicted it");  // r7 N2
  check("...the injection gate held (no Hello before the OFF line) and nothing contradicted it", true);
  check("IsSupported false: exit 6, 'WGC not supported' (as before)",
        code == 6 && out.find("WGC not supported") != std::string::npos && out.find("threw") == std::string::npos,
        "exit " + std::to_string(code));
  code = run_with_client(dir, L"normal", {}, "H264 encoder backend=", &out, &hellos);
  if (code == kInjectionInvalid) fatal_invalid(L"normal", "the injection gate refused or the run contradicted it");  // r7 N2
  check("...the injection gate held (no Hello before the OFF line) and nothing contradicted it", true);
  check("nothing injected: the ordinary path passes both calls (the encoder line is reached), no FATAL",
        out.find("H264 encoder backend=") != std::string::npos && out.find("FATAL:") == std::string::npos &&
            out.find("WGC not supported") == std::string::npos);

  // r5 F1: this run's own logs, by name, one by one; then its folder only if empty. The hosts' LOCALAPPDATA folders
  // (lad_*: their diagnostics) are not removed -- they stay inside the test root, said
  for (const wchar_t* t : {L"std", L"winrt", L"none", L"probe_trap", L"wrapper_trap", L"wgc_throw", L"wgc_false", L"normal"}) {
    if (gKeepLogs.count(t)) {  // r7 N2: its child's end was not confirmed -- its log stays, said
      std::printf("NOTE  kept (its child's end not confirmed): %lsfatal_%ls.log\n", gRun.c_str(), t);
      continue;
    }
    delete_known_file(gRun + L"fatal_" + t + L".log");
  }
  if (!gKeepPreloginLog) delete_known_file(gRun + L"fatal_prelogin.log");  // r6 F-b: kept if its worker's end is unconfirmed
  remove_made_folder(gRun.substr(0, gRun.size() - 1));
  std::printf("\nRESULT: %s  (%d checks, %d failed)\n", gFailures ? "FAILED" : "ALL PASS", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
